#include <mega/common/database.h>
#include <mega/common/partial_download.h>
#include <mega/common/scoped_query.h>
#include <mega/common/task_queue.h>
#include <mega/common/transaction.h>
#include <mega/common/utility.h>
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
#include <mega/file_service/type_traits.h>
#include <mega/filesystem.h>
#include <mega/overloaded.h>

#include <iterator>
#include <type_traits>
#include <variant>

namespace mega
{
namespace file_service
{

using namespace common;

void FileContext::cancel(FileRequest& request)
{
    // Cancel the request.
    std::visit(overloaded{[&](FileReadRequest& request)
                          {
                              failed(request, FILE_CANCELLED);
                          }},
               request);
}

void FileContext::cancel()
{
    // Cancel any downloads in progress.
    {
        // Make sure no one else changes mRanges.
        std::lock_guard guard(mRangesLock);

        // Cancel any downloads in progress.
        for (auto i = mRanges.begin(); i != mRanges.end();)
        {
            // Keeps logic simple.
            auto j = i++;

            // Range is being downloaded.
            if (j->second)
                j->second->cancel();
        }
    }

    // Cancel any pending requests.
    while (!mRequests.empty())
    {
        // Cancel the request.
        cancel(mRequests.front());

        // Remove the request from our queue.
        mRequests.pop_front();
    }
}

void FileContext::completed(Buffer& buffer,
                            FileRangeContextPtrMap::Iterator iterator,
                            const FileRange& range)
{
    // Convenience.
    auto offset = range.mBegin;
    auto length = range.mEnd - offset;

    // No data for this range was downloaded.
    if (!length)
        return mRanges.remove(iterator), void();

    // Can't flush this range's data to the storage.
    if (!buffer.transfer(*mBuffer, 0, offset, length))
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
        if (candidate->first.mEnd != offset)
            return iterator;

        // Recompute offset and length.
        offset = candidate->first.mBegin;
        length = range.mEnd - offset;

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

        // Recompute length.
        length = candidate->first.mEnd - offset;

        // Return iterator to caller.
        return std::next(candidate);
    }();

    // Mark range as present.
    iterator->second.reset();

    // Convenience.
    auto& queries = mService.queries();

    // Remove obsolete ranges from the database.
    auto transaction = mService.database().transaction();
    auto query = transaction.query(queries.mRemoveFileRanges);

    query.param(":begin").set(offset);
    query.param(":end").set(offset + length);
    query.param(":id").set(mInfo->id());
    query.execute();

    // Add our new range to the database.
    query = transaction.query(queries.mAddFileRange);

    query.param(":begin").set(offset);
    query.param(":end").set(offset + length);
    query.param(":id").set(mInfo->id());
    query.execute();

    // Persist database changes.
    transaction.commit();

    // Remove contiguous ranges.
    mRanges.remove(begin, end);

    // Add updated range.
    mRanges.add(std::piecewise_construct,
                std::forward_as_tuple(offset, offset + length),
                std::forward_as_tuple(nullptr));
}

void FileContext::completed(BufferPtr buffer, FileReadRequest& request)
{
    // Sanity.
    assert(buffer);

    // Called to complete the user's request.
    auto callback = [=](FileContextPtr&, FileReadRequest& request, const Task&)
    {
        // Convenience.
        auto [begin, end] = request.mRange;

        // Populate result.
        FileReadResult result = {*buffer, begin, end - begin}; // result

        // Let the user know their read has completed.
        request.mCallback(std::move(result));

        // Let the context know the read has completed.
        mReadWriteState.readCompleted();

        // See if we can't execute any queued requests.
        execute();
    }; // callback

    // Queue the user's request for completion.
    mService.execute(std::bind(std::move(callback),
                               shared_from_this(),
                               std::move(request),
                               std::placeholders::_1));
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
        return completed(mBuffer, request), true;

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
        // We've already downloaded the range.
        if (!context)
            return completed(mBuffer, request), true;

        // Range is still being downloaded.
        context->queue(request);

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
    return std::visit(overloaded{[this](FileReadRequest& request)
                                 {
                                     return execute(request);
                                 }},
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

void FileContext::failed(FileReadRequest& request, FileResult result)
{
    // Queue the user's callback for execution.
    mService.execute(std::bind(std::move(request.mCallback), unexpected(result)));

    // Let the context know the read's completed.
    mReadWriteState.readCompleted();

    // See if we can't execute any queued requests.
    execute();
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
    // Push the request onto the end of our queue.
    mRequests.emplace_after(mRequests.end(),
                            std::in_place_type_t<std::remove_reference_t<Request>>(),
                            std::forward<Request>(request));
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

void FileContext::read(FileReadRequest request)
{
    // Couldn't execute the read as a write is in progress.
    if (!execute(request))
        queue(std::move(request));
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

} // file_service
} // mega
