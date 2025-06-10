#include <mega/common/database.h>
#include <mega/common/partial_download.h>
#include <mega/common/scoped_query.h>
#include <mega/common/task_queue.h>
#include <mega/common/transaction.h>
#include <mega/common/utility.h>
#include <mega/file_service/displaced_buffer.h>
#include <mega/file_service/file_access.h>
#include <mega/file_service/file_append_request.h>
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
#include <mega/file_service/file_truncate_request.h>
#include <mega/file_service/file_write_request.h>
#include <mega/file_service/logging.h>
#include <mega/file_service/type_traits.h>
#include <mega/filesystem.h>
#include <mega/overloaded.h>

#include <chrono>
#include <iterator>
#include <limits>
#include <type_traits>
#include <variant>

namespace mega
{
namespace file_service
{

using namespace common;

// Check if T is a file read request.
template<typename T>
constexpr auto IsFileReadRequestV = IsOneOfV<T, FileReadRequest>;

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
    completed(std::move(request), FileReadResult{*buffer, begin, end - begin}, std::move(buffer));
}

template<typename Request, typename Result, typename... Captures>
auto FileContext::completed(Request&& request, Result result, Captures&&... captures)
    -> std::enable_if_t<IsFileRequestV<Request>>
{
    // Make sure request has been passed by rvalue reference.
    static_assert(!std::is_reference_v<Request>);

    // What kind of request are we completing?
    constexpr auto complete = IsFileReadRequestV<Request> ? &FileReadWriteState::readCompleted :
                                                            &FileReadWriteState::writeCompleted;

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

void FileContext::completed(FileWriteRequest&& request)
{
    // Convenience.
    auto [begin, end] = request.mRange;

    // Complete the user's request.
    completed(std::move(request), FileWriteResult{begin, end - begin});
}

bool FileContext::execute(FileAppendRequest& request)
{
    // Can't execute an append if there's another request in progress.
    if (!mReadWriteState.write())
        return false;

    // Convenience.
    auto size = mInfo->size();

    // Assume there's no range for us to grow.
    FileRange range(size, size + request.mLength);

    // Acquire ranges lock.
    std::unique_lock lock(mRangesLock);

    // Assume we can grow the last range.
    auto candidate = mRanges.rbegin();

    // Can grow the last range.
    if (!mRanges.empty() && candidate->first.mEnd == size)
        range.mBegin = candidate->first.mBegin;
    else
        candidate = mRanges.end();

    // Disambiguate.
    using file_service::write;

    // Try and write the user's data to disk.
    auto length = write(*mFile, request.mBuffer, size, request.mLength);

    // Couldn't write all of the user's data to disk.
    if (length < request.mLength)
        return failed(std::move(request), FILE_FAILED), true;

    auto transaction = mService.database().transaction();

    // Remove obsolete ranges from the database.
    removeRanges(range, transaction);

    // Add a new range to the database.
    addRange(range, transaction);

    // Compute the file's new modification time.
    auto modified = now();

    // Update the file's modification time.
    updateModificationTime(modified, transaction);

    // Remove obsolete ranges from memory.
    mRanges.remove(candidate, mRanges.end());

    // Add new range to memory.
    mRanges.add(range, nullptr);

    // Persist our changes.
    transaction.commit();

    // Tweak range.
    range.mBegin = size;

    // Update the file's attributes.
    mInfo->written(range, modified);

    // Queue the user's request for execution.
    completed(std::move(request), FILE_SUCCESS);

    // Let the caller know the request was executed.
    return true;
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

bool FileContext::execute(FileTruncateRequest& request)
{
    // Can't truncate if there's another request in progress.
    if (!mReadWriteState.write())
        return false;

    // User isn't changing the file's size.
    if (mInfo->size() == request.mSize)
        return completed(std::move(request), FILE_SUCCESS), true;

    // What range is affected by the truncation?
    FileRange range(mInfo->size(), request.mSize);

    // Whether we should add a new range.
    auto resize = request.mSize > mInfo->size();

    // Convenience.
    auto size = range.mBegin;

    // Make sure we have exclusive access to mRanges.
    std::unique_lock lock(mRangesLock);

    // Find the first range that begins after size.
    auto begin = mRanges.endsAfter(size);

    // Range contains size.
    if (begin != mRanges.end() && begin->first.mBegin < size)
    {
        // Tweak our effective range.
        range.mBegin = begin->first.mBegin;

        // Remember that we need to resize an existing range.
        resize = true;
    }

    auto transaction = mService.database().transaction();

    // Remove affected ranges.
    removeRanges(range, transaction);

    // Tweak effective range.
    range.mEnd = request.mSize;

    // Readd existing range to database if necessary.
    if (resize)
        addRange(range, transaction);

    // Compute the file's new modification time.
    auto modified = now();

    // Update the file's modification time.
    updateModificationTime(modified, transaction);

    // Remove obsolete ranges from memory.
    mRanges.remove(begin, mRanges.end());

    // Readd existing range to memory if necessary.
    if (resize)
        mRanges.add(range, nullptr);

    // Couldn't truncate this file's storage.
    if (!mFile->ftruncate(static_cast<m_off_t>(request.mSize)))
        return failed(std::move(request), FILE_FAILED), true;

    // Persist changes.
    transaction.commit();

    // Update the file's attributes.
    mInfo->truncated(modified, request.mSize);

    // Queue the user's request for completion.
    completed(std::move(request), FILE_SUCCESS);

    // Let the caller know the request's been executed.
    return true;
}

bool FileContext::execute(FileWriteRequest& request)
{
    // Can't execute a write if there's another request in progress.
    if (!mReadWriteState.write())
        return false;

    // Convenience.
    auto& range = request.mRange;

    auto length = range.mEnd - range.mBegin;

    // Caller doesn't actually want to write anything.
    if (!length)
        return completed(std::move(request)), true;

    // Caller hasn't passed us a valid buffer.
    if (!request.mBuffer)
        return failed(std::move(request), FILE_INVALID_ARGUMENTS), true;

    // Get exclusive access to mRanges.
    std::unique_lock lock(mRangesLock);

    // Disambiguate.
    using file_service::write;

    // Try and write the caller's content to storage.
    length = write(*mFile, request.mBuffer, range.mBegin, length);

    // Couldn't write any content to storage.
    if (!length)
        return failed(std::move(request), FILE_FAILED), true;

    // Compute actual end of the written range.
    range.mEnd = range.mBegin + length;

    // Find out which ranges we've touched.
    auto [begin, end] = mRanges.find(extend(range, 1));

    // Compute effective range.
    auto effectiveRange = [&]()
    {
        // Assume range has no contiguous siblings.
        auto from = range.mBegin;
        auto to = range.mEnd;

        // Range has a left sibling.
        if (begin != mRanges.end())
            from = std::min(from, begin->first.mBegin);

        // Range has a right sibling.
        if (end != mRanges.end())
            return FileRange(from, std::max(std::prev(end)->first.mEnd, to));

        // Range may have a right sibling.
        auto candidate = mRanges.crbegin();

        // Range has a right sibling.
        if (candidate != mRanges.crend())
            to = std::max(candidate->first.mEnd, to);

        // Return effective range to caller.
        return FileRange(from, to);
    }();

    auto transaction = mService.database().transaction();

    // Remove obsolete ranges from the database.
    removeRanges(effectiveRange, transaction);

    // Add a new range to the database.
    addRange(effectiveRange, transaction);

    // Compute the file's new modification time.
    auto modified = now();

    // Update the file's modification time in the database.
    updateModificationTime(modified, transaction);

    // Remove obsolete ranges from memory.
    mRanges.remove(begin, end);

    // Add our new range to memory.
    mRanges.add(effectiveRange, nullptr);

    // Persist our changes.
    transaction.commit();

    // Update the file's attributes.
    mInfo->written(range, modified);

    // Queue the user's request for completion.
    completed(std::move(request));

    // Let the caller know the write was successful.
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

template<typename Request>
auto FileContext::executeOrQueue(Request&& request) -> std::enable_if_t<IsFileRequestV<Request>>
{
    // Make sure the request's been passed by rvalue reference.
    static_assert(!std::is_reference_v<Request>);

    // Can't execute the request so queue it for later execution.
    if (!execute(request))
        queue(std::forward<Request>(request));
}

void FileContext::failed(FileReadRequest&& request, FileResult result)
{
    // Delegate to template function.
    failed<>(std::move(request), result);
}

template<typename Request>
auto FileContext::failed(Request&& request, FileResult result)
    -> std::enable_if_t<IsFileRequestV<Request>>
{
    // Make sure request has been passed by rvalue reference.
    static_assert(!std::is_reference_v<Request>);

    // What kind of request are we failing?
    constexpr auto complete = IsFileReadRequestV<Request> ? &FileReadWriteState::readCompleted :
                                                            &FileReadWriteState::writeCompleted;

    // Called to fail the user's request.
    auto callback = [=](auto& cookie, auto& request, auto&)
    {
        // Determine the callback's type.
        using Callback = decltype(request.mCallback);

        // Let the user know their request's failed.
        if constexpr (std::is_invocable_v<Callback, FileResult>)
            request.mCallback(result);
        else
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
auto FileContext::queue(Request&& request) -> std::enable_if_t<IsFileRequestV<Request>>
{
    // Make sure we're the only one changing the queue.
    std::lock_guard guard(mRequestsLock);

    // Convenience.
    using Tag = std::in_place_type_t<std::remove_reference_t<Request>>;

    // Push the request onto the end of our queue.
    mRequests.emplace_back(Tag(), std::forward<Request>(request));
}

void FileContext::removeRanges(const FileRange& range, Transaction& transaction)
{
    auto query = transaction.query(mService.queries().mRemoveFileRanges);

    query.param(":begin").set(range.mBegin);
    query.param(":end").set(range.mEnd);
    query.param(":id").set(mInfo->id());

    query.execute();
}

void FileContext::updateModificationTime(std::int64_t modified, Transaction& transaction)
{
    auto query = transaction.query(mService.queries().mSetFileModificationTime);

    query.param(":modified").set(modified);
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

void FileContext::append(FileAppendRequest request)
{
    executeOrQueue(std::move(request));
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
    executeOrQueue(std::move(request));
}

void FileContext::ref()
{
    adjustRef(1);
}

void FileContext::truncate(FileTruncateRequest request)
{
    executeOrQueue(std::move(request));
}

void FileContext::unref()
{
    adjustRef(-1);
}

void FileContext::write(FileWriteRequest request)
{
    executeOrQueue(std::move(request));
}

} // file_service
} // mega
