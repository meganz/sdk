#include <mega/common/client.h>
#include <mega/common/database.h>
#include <mega/common/lock.h>
#include <mega/common/node_info.h>
#include <mega/common/partial_download.h>
#include <mega/common/scoped_query.h>
#include <mega/common/task_queue.h>
#include <mega/common/transaction.h>
#include <mega/common/upload.h>
#include <mega/common/utility.h>
#include <mega/file_service/displaced_buffer.h>
#include <mega/file_service/file_access.h>
#include <mega/file_service/file_append_request.h>
#include <mega/file_service/file_context.h>
#include <mega/file_service/file_context_badge.h>
#include <mega/file_service/file_fetch_request.h>
#include <mega/file_service/file_flush_request.h>
#include <mega/file_service/file_id.h>
#include <mega/file_service/file_info.h>
#include <mega/file_service/file_info_context.h>
#include <mega/file_service/file_location.h>
#include <mega/file_service/file_range_context.h>
#include <mega/file_service/file_read_request.h>
#include <mega/file_service/file_reclaim_request.h>
#include <mega/file_service/file_remove_request.h>
#include <mega/file_service/file_result.h>
#include <mega/file_service/file_service_context.h>
#include <mega/file_service/file_touch_request.h>
#include <mega/file_service/file_truncate_request.h>
#include <mega/file_service/file_write_request.h>
#include <mega/file_service/logging.h>
#include <mega/file_service/sparse_file_buffer.h>
#include <mega/file_service/type_traits.h>
#include <mega/filesystem.h>

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

class FileContext::FetchContext
{
    // Called when the fetch has been completed.
    void completed(FileResult result);

    // Keep mContext alive as long as we are alive.
    Activity mActivity;

    // What file are we fetching?
    FileContext& mContext;

    // What fetch requests are we executing?
    std::vector<FileFetchRequest> mRequests;

public:
    FetchContext(FileContext& context, FileFetchRequest request);

    // Called when we've received file data.
    void operator()(FetchContextPtr& context, FileResultOr<FileReadResult> result);

    // Queue a fetch request for execution.
    void queue(FileFetchRequest request);
}; // FetchContext

class FileContext::FlushContext
{
    // Called when the file's content has been uploaded.
    void bound(FlushContextPtr& context, ErrorOr<NodeHandle> result);

    // Called when the flush has been completed.
    template<typename Lock>
    void completed(FlushContextPtr context, Lock&& lock, FileResult result);

    // Called to check that our upload target is valid.
    //
    // Populates mHandle, mName and mParentHandle.
    Error resolve(Client& client);

    // Called when the file's data has been uploaded.
    void uploaded(FlushContextPtr& context, ErrorOr<UploadResult> result);

    // Keep mContext alive as long as we are alive.
    Activity mActivity;

    // What file are we flushing?
    FileContext& mContext;

    // The file's current node handle.
    NodeHandle mHandle;

    // Where is this file stored in the cloud?
    FileLocation mLocation;

    // What flush requests are we executing?
    std::vector<FileFlushRequest> mRequests;

    // The upload that's pushing our content to the cloud.
    UploadPtr mUpload;

public:
    FlushContext(FileContext& context, FileFlushRequest request);

    // Called when we've retrieved all of this file's content.
    void operator()(FlushContextPtr& context, FileResult result);

    // Cancel the flush.
    template<typename Lock>
    void cancel(FlushContextPtr context, Lock&& lock);

    // Queue a flush request for execution.
    void queue(FileFlushRequest request);
}; // FlushContext

class FileContext::ReclaimContext
{
    // Called when the reclaim request has completed.
    template<typename Lock>
    void completed(ReclaimContextPtr context, Lock&& lock, FileResultOr<std::uint64_t> result);

    // Keep mContext alive as long as we are alive.
    Activity mActivity;

    // How much space was the file taking when we started reclaiming?
    std::uint64_t mAllocatedSize;

    // Who should we call when the reclaim completes?
    std::vector<FileReclaimCallback> mCallbacks;

    // What file are we reclaiming?
    FileContext& mContext;

public:
    ReclaimContext(FileContext& context);

    // Cancel the reclamation.
    template<typename Lock>
    void cancel(ReclaimContextPtr& context, Lock&& lock);

    // Called when the file's data has been flushed to the cloud.
    void flushed(ReclaimContextPtr& context, FileResult result);

    // Queue a callback for execution when the reclaim completes.
    void queue(FileReclaimCallback callback);
}; // ReclaimContext

// Retrieve an instance of a request's type tag.
template<typename Request>
auto tag(const Request& request)
    -> std::enable_if_t<IsFileRequestV<Request>, typename Request::Type>;

// Wrap a callback to ensure that exceptions are always handled.
template<typename Callback>
Callback swallow(Callback callback, const char* name);

void FileContext::addRange(const FileRange& range, Transaction& transaction)
{
    auto query = transaction.query(mService.queries().mAddFileRange);

    query.param(":begin").set(range.mBegin);
    query.param(":end").set(range.mEnd);
    query.param(":id").set(mInfo->id());

    query.execute();
}

void FileContext::cancel(const FileRange& range)
{
    // Make sure we have exclusive access to mRanges.
    std::unique_lock lock(mRangesLock);

    // What ranges intersect range?
    auto [begin, end] = mRanges.find(range);

    // No ranges intersect range.
    if (begin == end)
        return;

    // True if a download is in progress.
    auto downloading = [](const auto& entry)
    {
        return entry.second != nullptr;
    }; // downloading

    // How many of these ranges have a download in progress?
    std::atomic count = std::count_if(begin, end, downloading);

    // No reads are in progress.
    if (!count)
        return;

    // So we can signal when the ranges have finished downloading.
    std::promise<void> notifier;

    // Called when a range has finished downloading.
    auto completed = [&](auto)
    {
        // Wake up our waiter when all downloads have finished.
        if (count.fetch_sub(1) == 1)
            notifier.set_value();
    };

    // Cancel the downloads in progress.
    for (auto lock_ = std::move(lock); begin != end;)
    {
        // Necessary due to cancel() below.
        auto current = begin++;

        // Sanity.
        assert(current->second);

        // So we know when the download has completed.
        current->second->queue(completed);

        // Cancel the download.
        current->second->cancel();
    }

    // Wait for the downloads to complete.
    notifier.get_future().get();
}

void FileContext::cancel(FileRequest& request)
{
    // Cancel the request.
    std::visit(
        [&](auto& request)
        {
            completed(std::move(request), FILE_CANCELLED);
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

    // Cancel the flush if necessary.
    {
        std::unique_lock lock(mFlushContextLock);

        if (mFlushContext)
            mFlushContext->cancel(mFlushContext, std::move(lock));
    }

    // Cancel reclamation if necessary.
    {
        std::unique_lock lock(mReclaimContextLock);

        if (mReclaimContext)
            mReclaimContext->cancel(mReclaimContext, std::move(lock));
    }

    // Latch the request queue.
    auto requests = [this]()
    {
        // Make sure no one else is messing with our request queue.
        std::lock_guard guard(mRequestsLock);

        // Latch the request queue.
        auto requests = std::exchange(mRequests, FileRequestList());

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
    // Convenience.
    auto offset = range.mBegin;
    auto length = range.mEnd - offset;

    // No data for this range was downloaded.
    if (!length)
        return mRanges.remove(iterator), void();

    // Flush this range's data to storage if necessary.
    if (buffer.isMemoryBuffer())
        std::tie(length, std::ignore) = buffer.copy(*mBuffer, 0, offset, length);

    // Couldn't flush any of this range's data to storage.
    if (!length)
        return mRanges.remove(iterator), void();

    // Compute the range's actual end point.
    range.mEnd = range.mBegin + length;

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

    // Convenience.
    auto& database = mService.database();

    // Acquire database lock.
    UniqueLock databaseLock(database);

    // Start transaction so we can safely access the database.
    auto transaction = database.transaction();

    // Remove obsolete ranges from the database.
    removeRanges(range, transaction);

    // Add our new range to the database.
    addRange(range, transaction);

    // Update the file's size.
    updateSize(mInfo->size(), transaction);

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
    // Sanity.
    assert(request.mCallback);

    // Make sure request has been passed by rvalue reference.
    static_assert(std::is_rvalue_reference_v<decltype(request)>);

    // Called to complete the user's request.
    auto callback = [=](auto& callback, auto& cookie, auto&, auto& tag, auto&&...)
    {
        // Are we passing a file result?
        if constexpr (std::is_same_v<FileResult, Result>)
        {
            // Determine the callback's concrete type.
            using Callback = decltype(callback);

            // Check if we have to pass result as an unexpected.
            if constexpr (std::is_invocable_v<Callback, FileResult>)
                callback(result);
            else
                callback(unexpected(result));
        }
        else
        {
            // Pass the result as is.
            callback(result);
        }

        // Check if our context is still alive.
        auto context = cookie.lock();

        // Context isn't alive.
        if (!context)
            return;

        // Let the context know the request has completed.
        executed(tag);

        // See if we can't execute any queued requests.
        context->execute();
    }; // callback

    // Queue the user's request for completion.
    mService.execute(std::bind(std::move(callback),
                               swallow(std::move(request.mCallback), request.name()),
                               weak_from_this(),
                               std::placeholders::_1,
                               tag(request),
                               std::forward<Captures>(captures)...));
}

void FileContext::completed(FileWriteRequest&& request)
{
    // Convenience.
    auto [begin, end] = request.mRange;

    // Complete the user's request.
    completed(std::move(request), FileWriteResult{begin, end - begin});
}

void FileContext::dequeued([[maybe_unused]] std::unique_lock<std::mutex> lock, FileReadRequestTag)
{
    assert(lock.mutex() == &mRequestsLock);
    assert(lock.owns_lock());
}

void FileContext::dequeued([[maybe_unused]] std::unique_lock<std::mutex> lock, FileWriteRequestTag)
{
    assert(lock.mutex() == &mRequestsLock);
    assert(lock.owns_lock());

    // Sanity.
    assert(mNumPendingWriteRequests);

    --mNumPendingWriteRequests;
}

void FileContext::dequeued(std::unique_lock<std::mutex> lock, const FileRequest& request)
{
    assert(lock.mutex() == &mRequestsLock);
    assert(lock.owns_lock());

    std::visit(
        [&lock, this](auto&& request)
        {
            this->dequeued(std::move(lock), tag(request));
        },
        request);
}

bool FileContext::executable(std::unique_lock<std::mutex>& lock,
                             bool queuing,
                             const FileRequest& request)
{
    assert(lock.mutex() == &mRequestsLock);
    assert(lock.owns_lock());

    return std::visit(
        [&lock, queuing, this](auto&& request)
        {
            return this->executable(lock, queuing, tag(request));
        },
        request);
}

bool FileContext::executable([[maybe_unused]] std::unique_lock<std::mutex>& lock,
                             bool queuing,
                             FileReadRequestTag)
{
    assert(lock.mutex() == &mRequestsLock);
    assert(lock.owns_lock());

    if (queuing && mNumPendingWriteRequests)
        return false;

    return mReadWriteState.read();
}

bool FileContext::executable([[maybe_unused]] std::unique_lock<std::mutex>& lock,
                             bool,
                             FileWriteRequestTag)
{
    assert(lock.mutex() == &mRequestsLock);
    assert(lock.owns_lock());

    return mReadWriteState.write();
}

void FileContext::execute(std::function<void()> function)
{
    // Sanity.
    assert(function);

    // Wrap the caller's function.
    auto wrapper = [function = std::move(function)](auto&)
    {
        // Execute the caller's function.
        function();
    }; // wrapper

    // Queue function for execution on the service's thread pool.
    mService.execute(std::move(wrapper));
}

void FileContext::execute(FileAppendRequest& request)
{
    // Convenience.
    auto size = mInfo->size();

    // Assume there's no range for us to grow.
    FileRange range(size, size + request.mLength);

    // Acquire ranges lock.
    std::unique_lock rangesLock(mRangesLock);

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
    auto [length, _] = mBuffer->write(request.mBuffer, size, request.mLength);

    // Couldn't write all of the user's data to disk.
    if (length < request.mLength)
        return completed(std::move(request), FILE_FAILED);

    // Convenience.
    auto& database = mService.database();

    // Acquire database lock.
    UniqueLock databaseLock(database);

    // Start a transaction so we can safely modify the database.
    auto transaction = database.transaction();

    // Remove obsolete ranges from the database.
    removeRanges(range, transaction);

    // Add a new range to the database.
    addRange(range, transaction);

    // Compute the file's new modification time.
    auto modified = now();

    // Update the file's access and modification time.
    updateAccessAndModificationTimes(modified, modified, transaction);

    // Update the file's size.
    updateSize(range.mEnd, transaction);

    // Remove obsolete ranges from memory.
    mRanges.remove(candidate, mRanges.end());

    // Add new range to memory.
    mRanges.add(range, nullptr);

    // Persist our changes.
    transaction.commit();

    // Tweak range.
    range.mBegin = size;

    // Update the file's attributes.
    mInfo->written(modified, range);

    // Queue the user's request for execution.
    completed(std::move(request), FILE_SUCCESS);
}

void FileContext::execute(FileFetchRequest& request)
{
    // Acquire fetch context lock.
    std::unique_lock lock(mFetchContextLock);

    // A fetch is already in progress.
    if (mFetchContext)
        return mFetchContext->queue(std::move(request));

    // Instantiate a context for our fetch.
    mFetchContext = std::make_shared<FetchContext>(*this, std::move(request));

    // Release flush context lock.
    lock.unlock();

    // Try and read all of the file's data.
    read(FileReadRequest{std::bind(&FetchContext::operator(),
                                   mFetchContext.get(),
                                   mFetchContext,
                                   std::placeholders::_1),
                         FileRange(0, mInfo->size())});
}

void FileContext::execute(FileFlushRequest request)
{
    // The file hasn't been modified.
    if (!mInfo->dirty())
        return completed(std::move(request), FILE_SUCCESS);

    // Acquire flush context lock.
    std::unique_lock lock(mFlushContextLock);

    // A flush is already in progress.
    if (mFlushContext)
        return mFlushContext->queue(std::move(request));

    // Instantiate a new flush context.
    mFlushContext = std::make_shared<FlushContext>(*this, std::move(request));

    // Unlock flush context lock.
    lock.unlock();

    // Fetch all of this file's data.
    fetch(FileFetchRequest{std::bind(&FlushContext::operator(),
                                     mFlushContext.get(),
                                     mFlushContext,
                                     std::placeholders::_1)});
}

// This function is pretty complex as it handles a lot of cases.
//
// The basic idea is that we want to get the most value out of any download
// from the cloud we perform.
//
// For instance, if the user wants to read only 2KiB and we can't satisfy
// that request, we might as well download 2MiB because it will take the
// same time for the servers. That is, if it takes the same amount of time
// to download 2KiB and 2MiB, we might as well download 2MiB because it's
// better value.
//
// We also want to remove holes that surround a new range when it's
// economical to do so. For instance, imagine the user is performing a read
// and that the read is surrounded by ranges on either side. If those ranges
// are not too far away, we might as well extend the read so that the range
// that we wind up creating fills the space between the surrounding ranges
// completely.
//
// Note that there are really two ranges we're dealing with when we are
// executing a user's read: there's the range the user gave us directly and
// then there's the effective range, the one that we actually use for
// downloading. The effective range will always be the same or larger than
// the range the user provided.
//
// The cases are roughly as follows:
//
// - A user's request can be completely satisfied by an existing range.
//   - The existing range has already been downloaded.
//     - Execute the user's request and do no further processing.
//   - The existing range is still being downloaded.
//     - Queue the user's request for later completion.
//
// - A user's request can be partly satisfied by an existing range.
//   - The range contains the beginning of the user's read.
//     - The range has already been downloaded.
//       - Execute the user's request immediately.
//     - The range is still being downloaded.
//       - Queue the user's request for later completion.
//   - Extend the user's read so that we download a sane amount.
//     - The sane amount being at least mMinimumRangeSize.
//   - Check if there are any ranges before or after our extended range.
//     - If they are close enough, extend the range again.
//       - Close enough being mMinimumRangeDistance.
//       - That is, fill the holes if it's cheap enough to do so.
//   - Fill all of the holes in our extended range.
//     - That is, download all of the subranges we don't have.
//
// - A user's request cannot be satisfied by any existing range.
//   - This case is handled pretty much the same as the case above.
//   - It differs in that the user's request will be executed when the
//     first hole is downloaded.
void FileContext::execute(FileReadRequest& request)
{
    // The service's current options.
    auto options = mService.options();

    // The range the user wants to read.
    auto range = request.mRange;

    // The file's current size.
    auto size = mInfo->size();

    // Convenience.
    auto& [begin, end] = range;

    // Make sure the user's read doesn't extend beyond the end of the file.
    end = std::min(end, size);

    // Make sure the user's read doesn't end before it begins.
    end = std::max(begin, end);

    // Make sure the request's range has been clamped.
    request.mRange = range;

    // The user doesn't actually need to read anything.
    if (begin == end)
        return completed(mBuffer, std::move(request));

    // Update the file's access time.
    mInfo->accessed(now());

    // Make sure we have exclusive access to mRanges.
    std::unique_lock lock(mRangesLock);

    // Try and locate the range that either:
    // - Contains the beginning of our read.
    // - Contains the read completely.
    // - Preceeds the read.
    auto i = [&range, this]()
    {
        // Search for the first range that ends at or after our read begins.
        auto i = mRanges.endsAfter(range.mBegin);

        // No ranges end at or after our read begins.
        //
        // Assume a range exists before our read.
        if (i == mRanges.end())
            return mRanges.last();

        // The range preceeds our read.
        if (i->first.mEnd <= range.mBegin)
        {
            // Does this range have a successor?
            auto j = std::next(i);

            // Range's successor contains either:
            // - The beginning of our read.
            // - All of our read.
            if (j != mRanges.end() && j->first.mBegin <= range.mBegin)
                return j;

            // Range preceeds our read.
            return i;
        }

        // The range contains:
        // - The beginning of our read.
        // - All of our read.
        if (i->first.mBegin <= range.mBegin)
            return i;

        // No ranges contain or preceed our read.
        if (i == mRanges.begin())
            return mRanges.end();

        // Assume a range exists before our read.
        return std::prev(i);
    }();

    // We found a range that either contains or preceeds our read.
    while (i != mRanges.end())
    {
        // The range preceeds our read.
        if (i->first.mEnd <= begin)
        {
            // Compute the distance between the range and our read.
            auto distance = begin - i->first.mEnd;

            // Begin the read earlier if the distance is small enough.
            if (distance <= options.mMinimumRangeDistance)
                begin = i->first.mEnd;

            break;
        }

        // The range contains all or part of our read.
        //
        // Clamp the request as necessary.
        request.mRange.mEnd = std::min(i->first.mEnd, request.mRange.mEnd);

        // Is the range still being downloaded?
        if (i->second)
        {
            // Queue the read as the range is still being downloaded.
            i->second->queue(std::move(request));
        }
        else
        {
            // Range has been downloaded so complete the read now.
            completed(displace(mBuffer, range.mBegin), std::move(request));
        }

        // The range only partially contained our read.
        if (i->first.mEnd < end)
        {
            // Bump our range's beginning.
            range.mBegin = i->first.mEnd;
            break;
        }

        // Nothing more to do as the range completely contained our read.
        return;
    }

    // Add a range to our map and return a reference to its context.
    auto add = [this](const FileRange& range)
    {
        // Add the range to the map.
        auto [iterator, added] = mRanges.tryAdd(range, nullptr);

        // Adding should always succeed as we're filling holes.
        assert(added);

        // Convenience.
        auto& context = iterator->second;

        // Instantiate a context to track the range's download.
        context.reset(new FileRangeContext(mActivities.begin(), iterator, *this));

        // Return a reference to the context to our caller.
        return context.get();
    }; // add

    // Extend the read if necessary so that the download is worthwhile.
    end = begin + std::max(end - begin, options.mMinimumRangeSize);

    // Make sure the read doesn't extend past the end of the file.
    end = std::min(end, size);

    // Try and find the first range that begins after our read begins.
    i = mRanges.beginsAfter(begin);

    // Try and find the first range that begins after our read ends.
    auto j = mRanges.beginsAfter(end);

    // Extend the read if it's worthwhile to do so.
    if (j != mRanges.end() && j->first.mBegin - end <= options.mMinimumRangeDistance)
        end = j++->first.mBegin;

    // Tracks the ranges that we need to download.
    std::vector<FileRangeContext*> ranges;

    // Keeps track of a hole's range.
    auto scratch = range;

    // Iterate over the holes, creating ranges as needed.
    for (; i != j; ++i)
    {
        // The range begins after scratch.
        if (i->first.mBegin > scratch.mBegin)
        {
            // Tweak scratch so that it represents the hole.
            scratch.mEnd = i->first.mBegin;

            // Add a new range and keep track of its context.
            ranges.emplace_back(add(scratch));
        }

        // Bump scratch's beginning.
        scratch.mBegin = i->first.mEnd;
    }

    // A final hole still remains.
    if (scratch.mBegin < range.mEnd)
        ranges.emplace_back(add(FileRange(scratch.mBegin, end)));

    // Sanity.
    assert(!ranges.empty() || !request.mCallback);

    // No holes need to be filled.
    if (ranges.empty())
        return;

    // Queue the request if it hasn't already been done.
    if (request.mCallback)
        ranges.front()->queue(std::move(request));

    // Keep track of the downloads we need to begin.
    std::vector<PartialDownloadPtr> downloads;

    // We know how many downloads we need to begin.
    downloads.reserve(ranges.size());

    // Convenience.
    auto& client = mService.client();

    // The handle of the node we're downloading.
    auto handle = mInfo->handle();

    // Try and create downloads for our ranges.
    for (auto* range_: ranges)
    {
        if (auto download = range_->download(client, mBuffer, handle))
            downloads.emplace_back(std::move(download));
    }

    // Release our mRanges lock so we can safely begin the downloads.
    lock.unlock();

    // Begin the downloads.
    for (auto& download: downloads)
        download->begin();
}

// When this request is executed, any pending downloads will have completed.
void FileContext::execute(FileReclaimRequest& request)
{
    // Make sure no one else is modifying mRanges.
    std::lock_guard rangesLock(mRangesLock);

    // Convenience.
    auto& database = mService.database();

    // Acquire database lock.
    UniqueLock databaseLock(database);

    // So we can safely modify the database.
    auto transaction = database.transaction();

    // Represents the entire file.
    FileRange range(0, mInfo->size());

    // Remove all of this file's ranges from the database.
    removeRanges(range, transaction);

    // Couldn't reduce the file's size.
    if (!mBuffer->truncate(0))
        return completed(std::move(request), FILE_FAILED);

    // Remove all of the ranges from memory.
    mRanges.clear();

    // Update the file's size.
    updateSize(mInfo->size(), transaction);

    // Persist our changes.
    transaction.commit();

    // How much space did we reclaim?
    auto reclaimed = request.mAllocatedSize - mInfo->allocatedSize();

    // Let waiters know how much space we reclaimed.
    completed(std::move(request), reclaimed);
}

void FileContext::execute(FileRemoveRequest& request)
{
    // File's already been removed.
    if (mInfo->removed())
        return completed(std::move(request), FILE_SUCCESS);

    // Cancel any pending downloads.
    cancel(FileRange(0, mInfo->size()));

    // Convenience.
    auto handle = mInfo->handle();
    auto replaced = request.mReplaced;
    auto serviceOnly = request.mServiceOnly;

    // We only need to remove the file from the service.
    if (handle.isUndef() || serviceOnly)
        return completed(std::move(request), setRemoved(replaced));

    // Called when the file's been removed.
    auto removed = [replaced, this](auto&&, auto&& request, auto result)
    {
        // File was removed from the cloud.
        if (result == API_OK)
            return completed(std::move(request), setRemoved(replaced));

        // Couldn't remove the file from the cloud.
        completed(std::move(request), fileResultFromError(result));
    }; // removed

    // Ask the client to remove our file.
    mService.client().remove(std::bind(std::move(removed),
                                       mActivities.begin(),
                                       std::move(request),
                                       std::placeholders::_1),
                             handle);
}

void FileContext::execute(FileTouchRequest& request)
{
    // Compute the file's new access time.
    auto accessed = now();

    // Convenience.
    auto& database = mService.database();

    // Acquire database lock.
    UniqueLock databaseLock(database);

    // Start a transaction so we can safely modify the database.
    auto transaction = database.transaction();

    // Update the file's access and modification time.
    updateAccessAndModificationTimes(accessed, request.mModified, transaction);

    // Persist our changes.
    transaction.commit();

    // Update file attributes.
    mInfo->modified(accessed, request.mModified);

    // Queue the user's request for completion.
    completed(std::move(request), FILE_SUCCESS);
}

void FileContext::execute(FileTruncateRequest& request)
{
    // Convenience.
    auto newSize = request.mSize;
    auto oldSize = mInfo->size();

    // User isn't changing this file's size.
    if (newSize == oldSize)
        return completed(std::move(request), FILE_SUCCESS);

    // Grow or shrink the file as necessary.
    auto [databaseLock, transaction] =
        newSize > oldSize ? grow(newSize, oldSize) : shrink(newSize, oldSize);

    // Compute the file's new modification time.
    auto modified = now();

    // Update the file's access and modification times in the database.
    updateAccessAndModificationTimes(modified, modified, transaction);

    // Update the file's size in the database.
    updateSize(newSize, transaction);

    // Persist our changes.
    transaction.commit();

    // Update the file's attributes.
    mInfo->truncated(modified, newSize);

    // Queue the user's request to for completion.
    completed(std::move(request), FILE_SUCCESS);
}

void FileContext::execute(FileWriteRequest& request)
{
    // Convenience.
    auto& range = request.mRange;

    auto length = range.mEnd - range.mBegin;

    // Caller doesn't actually want to write anything.
    if (!length)
        return completed(std::move(request));

    // Caller hasn't passed us a valid buffer.
    if (!request.mBuffer)
        return completed(std::move(request), FILE_INVALID_ARGUMENTS);

    // Cancel any downloads in progress that intersect our write.
    cancel(range);

    // Get exclusive access to mRanges.
    std::unique_lock rangesLock(mRangesLock);

    // Disambiguate.
    using file_service::write;

    // Try and write the caller's content to storage.
    std::tie(length, std::ignore) = mBuffer->write(request.mBuffer, range.mBegin, length);

    // Couldn't write any content to storage.
    if (!length)
        return completed(std::move(request), FILE_FAILED);

    // Compute actual end of the written range.
    range.mEnd = range.mBegin + length;

    // Convenience.
    using Iterator = decltype(mRanges.begin());

    Iterator begin;
    Iterator end;

    // Compute initial effective range.
    FileRange effectiveRange = {std::min(mInfo->size(), range.mBegin), range.mEnd};

    // Find out which ranges we've touched.
    std::tie(begin, end) = mRanges.find(extend(effectiveRange, 1));

    // Refine our effective range.
    effectiveRange = [&]()
    {
        // Assume range has no contiguous siblings.
        auto from = effectiveRange.mBegin;
        auto to = effectiveRange.mEnd;

        // Range has no siblings.
        if (begin == mRanges.end())
            return FileRange(from, to);

        // Range has a sibling.
        from = std::min(begin->first.mBegin, from);
        to = std::max(begin->first.mEnd, to);

        // Range has a right sibling.
        if (end != mRanges.end())
        {
            // Clarity.
            auto sibling = std::prev(end);

            // Recompute range's end point.
            to = std::max(sibling->first.mEnd, to);

            // Return effective range to caller.
            return FileRange(from, to);
        }

        // Range may have a right sibling.
        auto candidate = mRanges.crbegin();

        // Range doesn't have a right sibling.
        if (candidate == mRanges.crend())
            return FileRange(from, to);

        // Recompute range's end point.
        to = std::max(candidate->first.mEnd, to);

        // Return effective range to caller.
        return FileRange(from, to);
    }();

    // Convenience.
    auto& database = mService.database();

    // Acquire database lock.
    UniqueLock databaseLock(database);

    // Start a transaction so we can safely modify the database.
    auto transaction = database.transaction();

    // Remove obsolete ranges from the database.
    removeRanges(effectiveRange, transaction);

    // Add a new range to the database.
    addRange(effectiveRange, transaction);

    // Compute the file's new modification time.
    auto modified = now();

    // Update the file's access and modification times in the database.
    updateAccessAndModificationTimes(modified, modified, transaction);

    // Update the file's size in the database.
    updateSize(std::max(mInfo->size(), effectiveRange.mEnd), transaction);

    // Remove obsolete ranges from memory.
    mRanges.remove(begin, end);

    // Add our new range to memory.
    mRanges.add(effectiveRange, nullptr);

    // Persist our changes.
    transaction.commit();

    // Update the file's attributes.
    mInfo->written(modified, range);

    // Queue the user's request for completion.
    completed(std::move(request));
}

void FileContext::execute(FileRequest& request)
{
    // Executes a user's request.
    auto execute = [this](auto& request)
    {
        try
        {
            // Sanity.
            assert(request.mCallback);

            // Try and execute the request.
            this->execute(request);
        }
        catch (std::exception& exception)
        {
            // Threw an exception while executing request.
            FSErrorF("Unable to execute %s request: %s", request.name(), exception.what());

            // Try and fail the request.
            completed(std::move(request), FILE_FAILED);
        }
    }; // execute

    // Execute the user's request.
    std::visit(std::move(execute), request);
}

void FileContext::execute()
{
    // Execute as many requests as we can.
    while (true)
    {
        // Acquire lock.
        std::unique_lock lock(mRequestsLock);

        // There are no requests waiting to execute.
        if (mRequests.empty())
            return;

        // Request isn't executable.
        if (!executable(lock, false, mRequests.front()))
            return;

        // Pop the request off the queue.
        auto request = std::move(mRequests.front());

        mRequests.pop_front();

        // Perform post dequeue actions.
        dequeued(std::unique_lock(std::move(lock)), request);

        // Execute the request.
        execute(request);
    }
}

template<typename Request>
auto FileContext::executeOrQueue(Request&& request) -> std::enable_if_t<IsFileRequestV<Request>>
{
    // Sanity.
    assert(request.mCallback);

    // Make sure the request's been passed by rvalue reference.
    static_assert(std::is_rvalue_reference_v<decltype(request)>);

    // Request isn't executable so queue it for later execution.
    if (std::unique_lock lock(mRequestsLock); !executable(lock, true, request))
        return queue(std::move(lock), std::forward<Request>(request));

    // Otherwise execute the request.
    execute(request);
}

void FileContext::executed(FileReadRequestTag)
{
    mReadWriteState.readCompleted();
}

void FileContext::executed(FileWriteRequestTag)
{
    mReadWriteState.writeCompleted();
}

void FileContext::failed(FileReadRequest&& request, FileResult result)
{
    // Delegate to template function.
    completed<>(std::move(request), result);
}

auto FileContext::grow(std::uint64_t newSize, std::uint64_t oldSize)
    -> std::pair<UniqueLock<Database>, Transaction>
{
    // Make sure we have exclusive access to mRanges.
    std::lock_guard rangesLock(mRangesLock);

    // Convenience.
    auto& database = mService.database();

    // Acquire database lock.
    UniqueLock databaseLock(database);

    // So we can safely modify the database.
    auto transaction = database.transaction();

    // Get our hands on this file's last range.
    auto last = mRanges.rbegin();

    // Assume the file has no range for us to enlarge.
    FileRange range(oldSize, newSize);

    // File has a range we can enlarge.
    if (last != mRanges.rend() && last->first.mEnd == oldSize)
    {
        // Remove the range from the database.
        removeRanges(last->first, transaction);

        // Tweak our range.
        range.mBegin = last->first.mBegin;

        // Remove the range from memory.
        mRanges.remove(last);
    }

    // (Re)?add the range to the database.
    addRange(range, transaction);

    // (Re)?add the range to memory.
    mRanges.add(range, nullptr);

    // Return the transaction to our caller.
    return std::make_pair(std::move(databaseLock), std::move(transaction));
}

std::unique_lock<std::recursive_mutex> FileContext::lock() const
{
    return std::unique_lock(mRangesLock);
}

std::recursive_mutex& FileContext::mutex() const
{
    return mRangesLock;
}

FileServiceOptions FileContext::options() const
{
    return mService.options();
}

template<typename Request>
auto FileContext::queue(std::unique_lock<std::mutex> lock, Request&& request)
    -> std::enable_if_t<IsFileRequestV<Request>>
{
    assert(lock.mutex() == &mRequestsLock);
    assert(lock.owns_lock());

    // Convenience.
    using Type = std::remove_reference_t<Request>;
    using Tag = std::in_place_type_t<Type>;

    // Push all but reclaim requests onto the end of our queue.
    if constexpr (IsFileReclaimRequestV<Type>)
        mRequests.emplace_front(Tag(), std::forward<Request>(request));
    else
        mRequests.emplace_back(Tag(), std::forward<Request>(request));

    // Perform post-queue actions.
    queued(std::move(lock), tag(request));
}

void FileContext::queued([[maybe_unused]] std::unique_lock<std::mutex> lock, FileReadRequestTag)
{
    assert(lock.mutex() == &mRequestsLock);
    assert(lock.owns_lock());
}

void FileContext::queued([[maybe_unused]] std::unique_lock<std::mutex> lock, FileWriteRequestTag)
{
    assert(lock.mutex() == &mRequestsLock);
    assert(lock.owns_lock());

    ++mNumPendingWriteRequests;
}

void FileContext::removeRanges(const FileRange& range, Transaction& transaction)
{
    auto query = transaction.query(mService.queries().mRemoveFileRanges);

    query.param(":begin").set(range.mBegin);
    query.param(":end").set(range.mEnd);
    query.param(":id").set(mInfo->id());

    query.execute();
}

FileResult FileContext::setRemoved(bool replaced)
try
{
    // Convenience.
    auto& database = mService.database();
    auto& queries = mService.queries();

    // Acquire database lock.
    std::lock_guard lock(database);

    // Mark the file as removed in the database.
    auto transaction = database.transaction();
    auto query = transaction.query(queries.mSetFileRemoved);

    query.param(":id").set(mInfo->id());
    query.execute();

    // Persist our changes.
    transaction.commit();

    // Mark the file as removed in memory.
    mInfo->removed(replaced);

    // Let the caller know the file was removed.
    return FILE_SUCCESS;
}

catch (std::runtime_error& exception)
{
    // Let debuggers know why we couldn't remove the file.
    FSErrorF("Unable to mark file %s as removed: %s",
             toString(mInfo->id()).c_str(),
             exception.what());

    // Let our caller know we couldn't remove the file.
    return FILE_FAILED;
}

auto FileContext::shrink(std::uint64_t newSize, std::uint64_t oldSize)
    -> std::pair<UniqueLock<Database>, Transaction>
{
    // Cancel any downloads in progress that would be "cut off."
    cancel(FileRange(oldSize, newSize));

    // So we have exclusive access to mRanges.
    std::lock_guard rangesLock(mRangesLock);

    // Convenience.
    auto& database = mService.database();

    // Acquire database lock.
    UniqueLock databaseLock(database);

    // So we can safely modify the database.
    auto transaction = database.transaction();

    // Couldn't reduce the file's size.
    if (!mBuffer->truncate(newSize))
        throw FSError1("Couldn't reduce file size");

    // What ranges end at or after our file's new size?
    auto begin = mRanges.endsAfter(newSize);

    // No ranges end at or after our new size.
    if (begin == mRanges.end())
        return std::make_pair(std::move(databaseLock), std::move(transaction));

    // Convenience.
    FileRange range(begin->first.mBegin, oldSize);

    // Remove affected ranges from the database.
    removeRanges(range, transaction);

    // Remove affected ranges from memory.
    mRanges.remove(begin, mRanges.end());

    // First range has been "cut" by the file's new size.
    if (range.mBegin < newSize)
    {
        // Adjust the range's end point.
        range.mEnd = newSize;

        // Readd the range to the database.
        addRange(range, transaction);

        // Readd the range to memory.
        mRanges.add(range, nullptr);
    }

    // Return the transaction to our caller.
    return std::make_pair(std::move(databaseLock), std::move(transaction));
}

void FileContext::updateAccessAndModificationTimes(std::int64_t accessed,
                                                   std::int64_t modified,
                                                   Transaction& transaction)
{
    auto query = transaction.query(mService.queries().mSetFileModificationTime);

    query.param(":accessed").set(accessed);
    query.param(":modified").set(modified);
    query.param(":id").set(mInfo->id());

    query.execute();
}

void FileContext::updateSize(std::uint64_t size, Transaction& transaction)
{
    auto query = transaction.query(mService.queries().mSetFileSize);

    query.param(":allocated_size").set(mInfo->allocatedSize());
    query.param(":id").set(mInfo->id());
    query.param(":reported_size").set(mInfo->reportedSize());
    query.param(":size").set(size);

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
    mBuffer(std::make_shared<SparseFileBuffer>(*file, *info)),
    mInfo(std::move(info)),
    mFetchContext(),
    mFetchContextLock(),
    mFile(std::move(file)),
    mFlushContext(),
    mFlushContextLock(),
    mNumPendingWriteRequests(0u),
    mRanges(),
    mRangesLock(),
    mReadWriteState(),
    mReclaimContext(),
    mReclaimContextLock(),
    mRequests(),
    mRequestsLock(),
    mService(service),
    mActivities()
{
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

FileEventObserverID FileContext::addObserver(FileEventObserver observer)
{
    return mInfo->addObserver(std::move(observer));
}

void FileContext::append(FileAppendRequest request)
{
    executeOrQueue(std::move(request));
}

void FileContext::fetch(FileFetchRequest request)
{
    executeOrQueue(std::move(request));
}

void FileContext::flush(FileFlushRequest request)
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

void FileContext::reclaim(FileReclaimCallback callback)
{
    // Make sure we have exclusive access to mReclaimContext.
    std::lock_guard lock(mReclaimContextLock);

    // A reclaim request is already in progress.
    if (mReclaimContext)
        return mReclaimContext->queue(std::move(callback));

    // Create a new reclaim context.
    mReclaimContext = std::make_shared<ReclaimContext>(*this);

    // Queue our callback for later execution.
    mReclaimContext->queue(std::move(callback));

    // So we can use the context's flushed method as a callback.
    FileFlushCallback flushed = std::bind(&ReclaimContext::flushed,
                                          mReclaimContext.get(),
                                          mReclaimContext,
                                          std::placeholders::_1);

    // Make sure this file's data has been flushed to the cloud.
    flush(FileFlushRequest{std::move(flushed)});
}

void FileContext::remove(FileRemoveRequest request)
{
    executeOrQueue(std::move(request));
}

void FileContext::removeObserver(FileEventObserverID id)
{
    mInfo->removeObserver(id);
}

bool FileContext::removed() const
{
    return mInfo->removed();
}

void FileContext::touch(FileTouchRequest request)
{
    executeOrQueue(std::move(request));
}

void FileContext::truncate(FileTruncateRequest request)
{
    executeOrQueue(std::move(request));
}

void FileContext::write(FileWriteRequest request)
{
    executeOrQueue(std::move(request));
}

void FileContext::FetchContext::completed(FileResult result)
{
    // Acquire fetch context lock.
    std::unique_lock lock(mContext.mFetchContextLock);

    // Clear fetch context.
    mContext.mFetchContext = nullptr;

    // Steal queued requests.
    auto requests = std::exchange(mRequests, decltype(mRequests)());

    // Release fetch context lock.
    lock.unlock();

    // Execute queued requests.
    for (auto& request: requests)
        mContext.completed(std::move(request), result);
}

FileContext::FetchContext::FetchContext(FileContext& context, FileFetchRequest request):
    mActivity(context.mActivities.begin()),
    mContext(context),
    mRequests()
{
    // Queue the request.
    queue(std::move(request));
}

void FileContext::FetchContext::operator()(FetchContextPtr& context,
                                           FileResultOr<FileReadResult> result)
{
    // Couldn't read this file's data.
    if (!result)
        return completed(result.error());

    // No more content to read.
    if (!result->mLength)
        return completed(FILE_SUCCESS);

    // Convenience.
    auto offset = result->mOffset + result->mLength;
    auto length = mContext.mInfo->size() - offset;

    // Try and read the rest of the file's data.
    mContext.read(FileReadRequest{
        std::bind(&FetchContext::operator(), this, std::move(context), std::placeholders::_1),
        FileRange(offset, offset + length)});
}

void FileContext::FetchContext::queue(FileFetchRequest request)
{
    // Acquire fetch context lock.
    std::lock_guard guard(mContext.mFetchContextLock);

    // Queue the request.
    mRequests.emplace_back(std::move(request));
}

void FileContext::FlushContext::bound(FlushContextPtr& context, ErrorOr<NodeHandle> result)
{
    // Acquire flush context lock.
    std::unique_lock lock(mContext.mFlushContextLock);

    // Couldn't flush the file's content.
    if (!result)
        return completed(std::move(context), std::move(lock), fileResultFromError(result.error()));

    // Try and update the file's handle.
    try
    {
        // Convenience.
        auto& info = *mContext.mInfo;
        auto& service = mContext.mService;
        auto& database = service.database();

        // Acquire database lock.
        UniqueLock databaseLock(database);

        // Try and update the file's handle.
        auto transaction = database.transaction();
        auto query = transaction.query(service.queries().mSetFileHandle);

        query.param(":handle").set(*result);
        query.param(":id").set(info.id());

        query.execute();

        // Persist our changes.
        transaction.commit();

        // Update the file's node handle.
        info.flushed(*result);

        // File's flushed.
        completed(std::move(context), std::move(lock), FILE_SUCCESS);
    }
    catch (std::exception& exception)
    {
        // Let debuggers know why the flush failed.
        FSErrorF("Couldn't update file handle: %s: %s",
                 toString(mContext.mInfo->id()).c_str(),
                 exception.what());

        // Couldn't flush the file's content.
        completed(std::move(context), std::move(lock), FILE_FAILED);
    }
}

template<typename Lock>
void FileContext::FlushContext::completed(FlushContextPtr context, Lock&& lock, FileResult result)
{
    // Sanity.
    assert(lock.mutex() == &mContext.mFlushContextLock);
    assert(lock.owns_lock());

    // Make sure we're still the file's current flush context.
    if (mContext.mFlushContext == context)
    {
        // We are. Let our file know this flush has completed.
        mContext.mFlushContext = nullptr;
    }

    // Steal queued requests.
    auto requests = std::exchange(mRequests, decltype(mRequests)());

    // Release lock.
    lock.unlock();

    // Execute queued requests.
    for (auto& request: requests)
        mContext.completed(std::move(request), result);
}

Error FileContext::FlushContext::resolve(Client& client)
{
    // File's never been flushed before.
    if (mHandle.isUndef())
        return client.get(mLocation.mParentHandle).errorOr(API_OK);

    // Check if the file's node still exists.
    auto node = client.get(mHandle);

    // File's node no longer exists.
    if (!node)
        return node.error();

    // Latch the node's current name and parent.
    mLocation.mName = std::move(node->mName);
    mLocation.mParentHandle = node->mParentHandle;

    // Let the caller know the node still exists.
    return API_OK;
}

void FileContext::FlushContext::uploaded(FlushContextPtr& context, ErrorOr<UploadResult> result)
{
    // Acquire flush context lock.
    std::unique_lock lock(mContext.mFlushContextLock);

    // Couldn't upload the file's data.
    if (!result)
        return completed(std::move(context), std::move(lock), fileResultFromError(result.error()));

    // The file's been removed.
    if (mContext.mInfo->removed())
        return completed(std::move(context), std::move(lock), FILE_REMOVED);

    // Upload's been cancelled.
    if (mRequests.empty())
        return;

    // Release the lock.
    lock.unlock();

    // So we can use our bound method as a callback.
    BoundCallback bound =
        std::bind(&FlushContext::bound, this, std::move(context), std::placeholders::_1);

    // Bind a name to our file's uploaded data.
    (*result)(std::move(bound), mHandle);
}

FileContext::FlushContext::FlushContext(FileContext& context, FileFlushRequest request):
    mActivity(context.mActivities.begin()),
    mContext(context),
    mHandle(context.mInfo->handle()),
    mLocation(context.mInfo->location()),
    mRequests(),
    mUpload()
{
    mRequests.emplace_back(std::move(request));
}

void FileContext::FlushContext::operator()(FlushContextPtr& context, FileResult result)
{
    // Convenience.
    auto& mutex = mContext.mFlushContextLock;

    // Couldn't retrieve this file's content.
    if (result != FILE_SUCCESS)
        return completed(std::move(context), std::unique_lock(mutex), result);

    // Convenience.
    auto& service = mContext.mService;
    auto& client = service.client();
    auto& info = *mContext.mInfo;

    // Check whether the file or its intended parent still exists.
    result = fileResultFromError(resolve(client));

    // Acquire context lock.
    std::unique_lock lock(mutex);

    // File or its intended parent no longer exists.
    if (result != FILE_SUCCESS)
        return completed(std::move(context), std::move(lock), result);

    // No requests? Flush must have been cancelled.
    if (mRequests.empty())
        return;

    // Where is this file's data stored?
    auto path = service.path(info.id());

    // Convenience.
    auto& [name, parent] = mLocation;

    // Instantiate an upload.
    mUpload = client.upload(path, name, parent, path);

    // So we can use our uploaded method as a callback.
    UploadCallback callback =
        std::bind(&FlushContext::uploaded, this, std::move(context), std::placeholders::_1);

    // Begin the upload.
    mUpload->begin(std::move(callback));
}

template<typename Lock>
void FileContext::FlushContext::cancel(FlushContextPtr context, Lock&& lock)
{
    assert(context);
    assert(lock.mutex() == &mContext.mFlushContextLock);
    assert(lock.owns_lock());

    auto upload = std::exchange(mUpload, nullptr);

    // No upload's in progress.
    if (!upload)
        return completed(std::move(context), std::move(lock), FILE_CANCELLED);

    // Release the lock.
    lock.unlock();

    // Cancel the upload.
    upload->cancel();
}

void FileContext::FlushContext::queue(FileFlushRequest request)
{
    // Acquire flush context lock.
    std::lock_guard guard(mContext.mFlushContextLock);

    // Queue the request.
    mRequests.emplace_back(std::move(request));
}

template<typename Lock>
void FileContext::ReclaimContext::completed(ReclaimContextPtr context,
                                            Lock&& lock,
                                            FileResultOr<std::uint64_t> result)
{
    // Sanity.
    assert(lock.mutex() == &mContext.mReclaimContextLock);
    assert(lock.owns_lock());

    // Make sure we're still the current reclaim context.
    if (mContext.mReclaimContext == context)
    {
        // We are so let the file know we've completed.
        mContext.mReclaimContext = nullptr;
    }

    // Steal queued callbacks.
    auto callbacks = std::exchange(mCallbacks, decltype(mCallbacks)());

    // Release reclaim context lock.
    lock.unlock();

    // Execute queued callbacks.
    for (auto& callback: callbacks)
        callback(result);
}

FileContext::ReclaimContext::ReclaimContext(FileContext& context):
    mActivity(context.mActivities.begin()),
    mAllocatedSize(context.mInfo->allocatedSize()),
    mCallbacks(),
    mContext(context)
{}

template<typename Lock>
void FileContext::ReclaimContext::cancel(ReclaimContextPtr& context, Lock&& lock)
{
    // Sanity.
    assert(lock.mutex() == &mContext.mReclaimContextLock);
    assert(lock.owns_lock());

    completed(std::move(context), std::forward<Lock>(lock), FILE_CANCELLED);
}

void FileContext::ReclaimContext::flushed(ReclaimContextPtr& context, FileResult result)
{
    // Acquire reclaim context lock.
    std::unique_lock lock(mContext.mReclaimContextLock);

    // Couldn't flush this file's data to the cloud.
    if (result != FILE_SUCCESS)
        return completed(std::move(context), std::move(lock), result);

    // Reclamation has been cancelled.
    if (mCallbacks.empty())
        return;

    // So we can use this context's completed method as a callback.
    auto callback = [context = std::move(context), this](auto result) mutable
    {
        completed(std::move(context), std::unique_lock(mContext.mReclaimContextLock), result);
    }; // callback

    // Queue the reclaim request for execution.
    mContext.queue(std::unique_lock(mContext.mRequestsLock),
                   FileReclaimRequest{mAllocatedSize, std::move(callback)});
}

void FileContext::ReclaimContext::queue(FileReclaimCallback callback)
{
    // Sanity.
    assert(callback);

    // Queue the callback for later execution.
    mCallbacks.emplace_back(swallow(std::move(callback), "reclaim"));
}

template<typename Callback>
Callback swallow(Callback callback, const char* name)
{
    return [callback = std::move(callback), name](auto&&... arguments)
    {
        try
        {
            // Try and execute the user's callback.
            std::invoke(callback, std::forward<decltype(arguments)>(arguments)...);
        }
        catch (std::exception& exception)
        {
            // User's callback threw an exception we can log.
            FSErrorF("User %s callback threw an exception: %s", name, exception.what());
        }
    };
}

template<typename Request>
auto tag(const Request&) -> std::enable_if_t<IsFileRequestV<Request>, typename Request::Type>
{
    return typename Request::Type();
}

} // file_service
} // mega
