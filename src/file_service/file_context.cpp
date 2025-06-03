#include <mega/common/database.h>
#include <mega/common/partial_download.h>
#include <mega/common/scoped_query.h>
#include <mega/common/task_queue.h>
#include <mega/common/transaction.h>
#include <mega/common/utility.h>
#include <mega/file_service/displaced_buffer.h>
#include <mega/file_service/file_buffer.h>
#include <mega/file_service/file_context.h>
#include <mega/file_service/file_context_badge.h>
#include <mega/file_service/file_id.h>
#include <mega/file_service/file_info.h>
#include <mega/file_service/file_info_context.h>
#include <mega/file_service/file_range_context.h>
#include <mega/file_service/file_read_request.h>
#include <mega/file_service/file_result.h>
#include <mega/file_service/file_service_context.h>
#include <mega/file_service/logging.h>
#include <mega/file_service/type_traits.h>
#include <mega/filesystem.h>
#include <mega/overloaded.h>

#include <iterator>
#include <limits>
#include <type_traits>
#include <variant>

namespace mega
{
namespace file_service
{

using namespace common;

void FileContext::addRange(const FileRange& range, Transaction& transaction)
{
    auto query = transaction.query(mService.queries().mAddFileRange);

    query.param(":begin").set(range.mBegin);
    query.param(":end").set(range.mEnd);
    query.param(":id").set(mInfo->id());

    query.execute();
}

void FileContext::adjustRef(std::int64_t adjustment)
{
    // Convenience.
    auto& queries = mService.queries();

    // Retrieve this file's reference count.
    auto transaction = mService.database().transaction();
    auto query = transaction.query(queries.mGetFileReferences);

    query.param(":id").set(mInfo->id());
    query.execute();

    // Latch the file's reference count.
    auto count = query.field("num_references").get<std::uint64_t>();

    // If we're increasing the count, make sure we don't overflow.
    assert(adjustment < 0 || count < UINT64_MAX);

    // If we're decreasing the count, make sure we don't underflow.
    assert(adjustment >= 0 || count);

    // Compute the file's new reference count.
    count += static_cast<std::uint64_t>(adjustment);

    // Update the file's reference count.
    query = transaction.query(queries.mSetFileReferences);

    query.param(":id").set(mInfo->id());
    query.param(":num_references").set(count);
    query.execute();

    // Persist our changes.
    transaction.commit();
}

void FileContext::cancel(FileRequest& request)
{
    // Cancel the request.
    std::visit(
        [&](auto& request)
        {
            failed(std::move(request), FILE_CANCELLED);
        },
        request);
}

void FileContext::cancel()
{
    // When we execute this function, we know that no live references to
    // this instance can exist. We know this because this function is only
    // called from the instance's destructor.
    //
    // This doesn't mean that the instance is idle, however, as it is
    // possible that one or more downloads may still be in progress which
    // means that the client servicing those downloads may be executing
    // within us or about to execute within us.

    // Cancel any downloads in progress.
    {
        // Make sure no one else changes mRanges.
        //
        // This is necessary as FileRangeContext instances acquire this lock
        // when they are servicing a partial download callback.
        std::lock_guard guard(mRangesLock);

        // Cancel any downloads in progress.
        for (auto i = mRanges.begin(); i != mRanges.end();)
        {
            // Keeps logic simple.
            auto j = i++;

            // Range is being downloaded.
            //
            // Calling cancel on a FileRangeContext with an active download
            // will cause that context to call us immediately to remove
            // itself from mRanges.
            if (j->second)
                j->second->cancel();
        }
    }

    // Latch the request queue.
    auto requests = [this]()
    {
        // Make sure no one else is messing with our request queue.
        std::lock_guard guard(mRequestsLock);

        // Latch the request queue.
        auto requests = std::move(mRequests);

        // Make sure the queue's in a sane state.
        mRequests.clear();

        // Return queue to caller.
        return requests;
    }();

    // Cancel any pending requests.
    //
    // We know this won't cause any other requests to be queued as we know
    // there are no live references to this instance.
    while (!requests.empty())
    {
        // Cancel the request.
        cancel(requests.front());

        // Remove the request from our queue.
        requests.pop_front();
    }
}

void FileContext::completed(Buffer& buffer,
                            FileRangeContextPtrMap::Iterator iterator,
                            FileRange range)
try
{
    // No data for this range was downloaded.
    if (range.mBegin == range.mEnd)
        return mRanges.remove(iterator), void();

    // Can't flush this range's data to the storage.
    if (!buffer.copy(*mBuffer, 0, range.mBegin, range.mEnd - range.mBegin))
        return mRanges.remove(iterator), void();

    // Figure out what ranges we can coalesce with.
    auto begin = [&]()
    {
        // We don't have a left neighbor.
        if (iterator == mRanges.begin())
            return iterator;

        // Get an iterator to our left neighbor.
        auto candidate = std::prev(iterator);

        // Neighbor hasn't completed downloading.
        if (candidate->second)
            return iterator;

        // Neighbor isn't contiguous.
        if (candidate->first.mEnd != range.mBegin)
            return iterator;

        // Update range.
        range.mBegin = candidate->first.mBegin;

        // Return iterator to caller.
        return candidate;
    }();

    auto end = [&]()
    {
        // Get an iterator to our right neighbor.
        auto candidate = std::next(iterator);

        // We don't have a right neighbor.
        if (candidate == mRanges.end())
            return candidate;

        // Neighbor hasn't completed downloading.
        if (candidate->second)
            return candidate;

        // Neighbor isn't contiguous.
        if (candidate->first.mBegin != range.mEnd)
            return candidate;

        // Update range.
        range.mEnd = candidate->first.mEnd;

        // Return iterator to caller.
        return std::next(candidate);
    }();

    // Mark range as present.
    iterator->second.reset();

    auto transaction = mService.database().transaction();

    // Remove obsolete ranges from the database.
    removeRanges(range, transaction);

    // Add our new range to the database.
    addRange(range, transaction);

    // Remove obsolete ranges from memory.
    mRanges.remove(begin, end);

    // Add our new range to memory.
    mRanges.add(range, nullptr);

    // Persist our changes.
    transaction.commit();
}

catch (std::runtime_error& exception)
{
    // Let debuggers know what went wrong.
    FSWarningF("Unable to complete file range download: %s: %s: %s",
               toString(mInfo->id()).c_str(),
               toString(range).c_str(),
               exception.what());

    // Consider the range absent.
    mRanges.remove(iterator);
}

void FileContext::completed(BufferPtr buffer, FileReadRequest&& request)
{
    // Sanity.
    assert(buffer);

    // Convenience.
    auto [begin, end] = request.mRange;

    // Complete the user's request.
    completed(&FileReadWriteState::readCompleted,
              std::move(request),
              FileReadResult{*buffer, begin, end - begin},
              std::move(buffer));
}

template<typename Request, typename Result, typename... Captures>
void FileContext::completed(void (FileReadWriteState::*complete)(),
                            Request&& request,
                            Result result,
                            Captures&&... captures)
{
    // Called to complete the user's request.
    auto callback = [=](auto& callback, auto& context, auto&, auto&&...)
    {
        // Let the user know their read has completed.
        callback(result);

        // Let the context know the read has completed.
        (context->mReadWriteState.*complete)();

        // See if we can't execute any queued requests.
        context->execute();
    }; // callback

    // Queue the user's request for completion.
    mService.execute(std::bind(std::move(callback),
                               std::move(request.mCallback),
                               shared_from_this(),
                               std::placeholders::_1,
                               std::forward<Captures>(captures)...));
}

bool FileContext::execute(FileReadRequest& request)
{
    // Can't execute a read if there's a write in progress.
    if (!mReadWriteState.read())
        return false;

    // Convenience.
    auto& [begin, end] = request.mRange;
    auto size = mInfo->size();

    // Clamp the request's range.
    end = std::max(begin, std::min(end, size));

    // Caller doesn't actually want to read anything.
    if (begin == end)
        return completed(mBuffer, std::move(request)), true;

    // Get exclusive access to mRanges.
    std::unique_lock lock(mRangesLock);

    // Do we already know of a range that could satisfy this request?
    auto [iterator, added] = mRanges.tryAdd(request.mRange, nullptr);

    // Clamp the request's range if necessary.
    end = std::min(iterator->first.mEnd, end);

    // Convenience.
    auto& context = iterator->second;

    // We have a range that could satisfy this request.
    if (!added)
    {
        // Range is still being downloaded.
        if (context)
            return context->queue(request), true;

        // Range has already been downloaded.
        completed(displace(mBuffer, begin), std::move(request));

        // Let our caller know the request was executed.
        return true;
    }

    // Instantiate a context to track this range.
    context.reset(new FileRangeContext(mActivities.begin(), iterator, *this));

    // Queue the request for later completion.
    context->queue(std::move(request));

    // Try and create a download for this range.
    auto download = context->download(mService.client(), *mFile, mInfo->handle());

    // Release our lock on mRanges.
    lock.unlock();

    // Couldn't create the download.
    if (!download)
        return true;

    // Begin the download.
    download->begin();

    // Let our caller know the request was executed.
    return true;
}

bool FileContext::execute(FileRequest& request)
{
    return std::visit(
        [this](auto& request)
        {
            return execute(request);
        },
        request);
}

void FileContext::execute()
{
    // Lock the request queue.
    std::unique_lock lock(mRequestsLock);

    // Execute as many requests as we can.
    while (!mRequests.empty())
    {
        // Couldn't execute the request.
        if (!execute(mRequests.front()))
            break;

        // Pop the request from our queue.
        mRequests.pop_front();
    }
}

void FileContext::failed(FileReadRequest&& request, FileResult result)
{
    failed(&FileReadWriteState::readCompleted, std::move(request), result);
}

template<typename Request>
void FileContext::failed(void (FileReadWriteState::*complete)(),
                         Request&& request,
                         FileResult result)
{
    // Sanity.
    assert(complete);

    // Called to fail the user's request.
    auto callback = [=](auto& cookie, auto& request, auto&)
    {
        // Let the user know their request has failed.
        request.mCallback(unexpected(result));

        // Check if our context is still alive.
        auto context = cookie.lock();

        // Context isn't alive.
        if (!context)
            return;

        // Let the context know the request has completed.
        (context->mReadWriteState.*complete)();

        // Try and execute any queued requests.
        execute();
    }; // callback

    // Queue the user's request for completion.
    mService.execute(std::bind(std::move(callback),
                               weak_from_this(),
                               std::move(request),
                               std::placeholders::_1));
}

std::unique_lock<std::recursive_mutex> FileContext::lock() const
{
    return std::unique_lock(mRangesLock);
}

std::recursive_mutex& FileContext::mutex() const
{
    return mRangesLock;
}

template<typename Request>
void FileContext::queue(Request&& request)
{
    // Make sure we're the only one changing the queue.
    std::lock_guard guard(mRequestsLock);

    // Push the request onto the end of our queue.
    mRequests.emplace_back(std::in_place_type_t<std::remove_reference_t<Request>>(),
                           std::forward<Request>(request));
}

void FileContext::removeRanges(const FileRange& range, Transaction& transaction)
{
    auto query = transaction.query(mService.queries().mRemoveFileRanges);

    query.param(":begin").set(range.mBegin);
    query.param(":end").set(range.mEnd);
    query.param(":id").set(mInfo->id());

    query.execute();
}

FileContext::FileContext(Activity activity,
                         FileAccessPtr file,
                         FileInfoContextPtr info,
                         const FileRangeVector& ranges,
                         FileServiceContext& service):
    FileRangeContextManager(),
    enable_shared_from_this(),
    mActivity(std::move(activity)),
    mBuffer(std::make_shared<FileBuffer>(*file)),
    mInfo(std::move(info)),
    mFile(std::move(file)),
    mRanges(),
    mRangesLock(),
    mReadWriteState(),
    mRequests(),
    mRequestsLock(),
    mService(service),
    mActivities()
{
    // Remember what ranges we've already downloaded.
    for (auto& range: ranges)
        mRanges.add(range, nullptr);
}

FileContext::~FileContext()
{
    // Cancel any downloads or pending requests.
    cancel();

    // Remove ourselves from our service's index.
    mService.removeFromIndex(FileContextBadge(), mInfo->id());
}

FileInfo FileContext::info() const
{
    return FileInfo(FileContextBadge(), mInfo);
}

FileRangeVector FileContext::ranges() const
{
    // Will store the ranges we'll return our caller.
    FileRangeVector ranges;

    // Get exclusive access to mRanges.
    std::lock_guard guard(mRangesLock);

    // Populate our range vector.
    std::transform(mRanges.begin(), mRanges.end(), std::back_inserter(ranges), SelectFirst());

    // Return ranges to our caller.
    return ranges;
}

void FileContext::read(FileReadRequest request)
{
    // Couldn't execute the read as a write is in progress.
    if (!execute(request))
        queue(std::move(request));
}

void FileContext::ref()
{
    adjustRef(1);
}

void FileContext::unref()
{
    adjustRef(-1);
}

} // file_service
} // mega
