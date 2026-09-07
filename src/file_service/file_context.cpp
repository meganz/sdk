#include <mega/common/client.h>
#include <mega/common/database.h>
#include <mega/common/database_utilities.h>
#include <mega/common/lock.h>
#include <mega/common/node_info.h>
#include <mega/common/partial_download.h>
#include <mega/common/partial_download_callback.h>
#include <mega/common/scoped_query.h>
#include <mega/common/task_queue.h>
#include <mega/common/transaction.h>
#include <mega/common/upload.h>
#include <mega/common/utility.h>
#include <mega/file_service/displaced_buffer.h>
#include <mega/file_service/file_access.h>
#include <mega/file_service/file_append_request.h>
#include <mega/file_service/file_buffer.h>
#include <mega/file_service/file_context.h>
#include <mega/file_service/file_context_badge.h>
#include <mega/file_service/file_fetch_request.h>
#include <mega/file_service/file_flush_request.h>
#include <mega/file_service/file_flusher.h>
#include <mega/file_service/file_id.h>
#include <mega/file_service/file_info.h>
#include <mega/file_service/file_info_context.h>
#include <mega/file_service/file_location.h>
#include <mega/file_service/file_range_tree_utilities.h>
#include <mega/file_service/file_read_request.h>
#include <mega/file_service/file_reclaim_request.h>
#include <mega/file_service/file_remove_request.h>
#include <mega/file_service/file_result.h>
#include <mega/file_service/file_service_context.h>
#include <mega/file_service/file_service_result.h>
#include <mega/file_service/file_service_result_or.h>
#include <mega/file_service/file_structure_mutex.h>
#include <mega/file_service/file_touch_request.h>
#include <mega/file_service/file_truncate_request.h>
#include <mega/file_service/file_truncate_result.h>
#include <mega/file_service/file_write_request.h>
#include <mega/file_service/flush_utilities.h>
#include <mega/file_service/logging.h>
#include <mega/file_service/read_tag.h>
#include <mega/file_service/type_traits.h>
#include <mega/filesystem.h>

#include <cassert>
#include <chrono>
#include <exception>
#include <iterator>
#include <utility>

namespace mega
{
namespace file_service
{

using namespace common;

using std::chrono::duration_cast;
using std::chrono::milliseconds;
using std::chrono::seconds;
using std::chrono::steady_clock;

// RAII helper that completes a request when it goes out of scope.
//
// Declare it as the FIRST local in an execute() overload so it is destroyed
// LAST: on the inline path the user's callback then runs only after every
// lock and ScopedDestructor in that function has unwound (matching the async
// path, where any cleanup happens before the callback). Call operator()
// exactly once with the request's result.
template<typename Request>
class FileContext::DeferredCompletion
{
    FileContext& mContext;
    std::function<void()> mDeferred;
    Request& mRequest;
    bool mOnCurrentThread;

public:
    DeferredCompletion(FileContext& context, Request& request, bool onCurrentThread):
        mContext(context),
        mDeferred([]() {}),
        mRequest(request),
        mOnCurrentThread(onCurrentThread)
    {}

    ~DeferredCompletion()
    {
        mDeferred();
    }

    template<typename Result>
    void operator()(Result&& result)
    {
        // Asynchronous path: Complete request via executor.
        if (!mOnCurrentThread)
            return mContext.completed(std::move(mRequest), std::forward<Result>(result), false);

        // Synchronous path: Defer completion until this object has been
        // destroyed so that the callback runs on this thread once all locks
        // have been released.
        mDeferred = [result = std::forward<Result>(result), this]() mutable
        {
            mContext.completed(std::move(mRequest), std::move(result), true);
        };
    }
}; // DeferredCompletion

class FileContext::DownloadContext: private PartialDownloadCallback
{
    // Called when the file range has been downloaded.
    void completed(Error result) override;

    // Called repeatedly as data is donwloaded from the cloud.
    auto data(const void* buffer,
              std::uint64_t offset,
              std::uint64_t length,
              const Speeds&) -> std::variant<Abort, Continue> override;

    // Called when our download's encountered a failure.
    virtual auto failed(Error result, int retries) -> std::variant<Abort, Retry> override;

    // Will this download satisfy any pending read requests?
    bool hasPendingReadRequests() const;

    // True if this download has been replaced.
    bool replaced() const;

    // Logs instance lifetime.
    InstanceLogger<DownloadContext> mInstanceLogger;

    // Keeps our manager alive until we're dead.
    Activity mActivity;

    // Callbacks to execute when this range's fetch completes.
    std::vector<FileFetchCallback> mCallbacks;

    // What category does this download belong to?
    DownloadCategory& mCategory;

    // Which file is responsible for this context?
    FileContext& mContext;

    // The download that's retrieving this file range's data.
    PartialDownloadPtr mDownload;

    // What is our position in mMap?
    FileRangeMap<DownloadContextPtr>::Iterator mIterator;

    // What range has this context downloaded?
    FileRange mRange;

    // How many times have we failed in a row?
    std::uint64_t mRetries;

public:
    DownloadContext(DownloadCategory& category,
                    FileContext& context,
                    FileRangeMap<DownloadContextPtr>::Iterator iterator);

    // What is this download's estimated bitrate?
    std::uint64_t bitrate() const;

    // Cancel this range's download.
    void cancel();

    // How far is position from the end of our downloaded data?
    std::uint64_t distance(std::uint64_t position) const;

    // Create a download this range.
    auto download() -> PartialDownloadPtr;

    // Where does our downloaded data currently end?
    std::uint64_t end() const;

    // Queue a callback for execution when this range has downloaded.
    void queue(FileFetchCallback callback);

    // Update this context's range.
    void range(std::uint64_t begin, std::uint64_t end);
    void range(const FileRange& range);

    // Retrieve this context's range.
    const FileRange& range() const;

    // Mark this download as having been replaced.
    void replaced(const DownloadContextPtr& self);

    // How long until we can satisfy a read at position?
    milliseconds timeUntil(std::uint64_t position) const;
}; // DownloadContext

class FileContext::FetchContext
{
    // Called when the fetch has been completed.
    void completed(FileResult result);

    // Logs instance lifetime.
    InstanceLogger<FetchContext> mInstanceLogger;

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
    // Called to bind a name to our uploaded data.
    void bind(BindCallback& bind, FlushContextPtr& context, FileStructureUniqueLock structureLock);

    // Called when a name has been bound to our uploaded data.
    void bound(FlushContextPtr& context, ErrorOr<NodeHandle> result);

    // Called when the flush has been completed.
    template<typename Lock>
    void completed(FlushContextPtr context, Lock&& lock, FileResult result);

    // Called to determine where our file should be uploaded.
    FileServiceResultOr<FileLocation> location() const;

    // Called when the file's data has been uploaded.
    void uploaded(FlushContextPtr& context, ErrorOr<UploadResult> result);

    // Logs instance lifetime.
    InstanceLogger<FlushContext> mInstanceLogger;

    // Keep mContext alive as long as we are alive.
    Activity mActivity;

    // What file are we flushing?
    FileContext& mContext;

    // Ensures no other operations can change this file's cloud state.
    //
    // Acquired when bind(...) is called.
    // Released when bound(...) is called.
    FileStructureUniqueLock mStructureLock;

    // What flush requests are we executing?
    std::vector<FileFlushRequest> mRequests;

    // The upload that's pushing our content to the cloud.
    UploadPtr mUpload;

    // Should writes cancel this flush?
    bool mCancelOnWrite;

public:
    FlushContext(FileContext& context, FileFlushRequest request);

    // Called when we've retrieved all of this file's content.
    void operator()(FlushContextPtr& context, FileResult result);

    // Cancel the flush.
    template<typename Lock>
    static void cancel(FlushContextPtr context, Lock&& lock, bool dueToWrite);

    // Queue a flush request for execution.
    void queue(FileFlushRequest request);
}; // FlushContext

class FileContext::ReclaimContext
{
    // Called when the reclaim request has completed.
    template<typename Lock>
    void completed(ReclaimContextPtr context, Lock&& lock, FileResultOr<std::uint64_t> result);

    // Logs instance lifetime.
    InstanceLogger<ReclaimContext> mInstanceLogger;

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
    static void cancel(ReclaimContextPtr& context, Lock&& lock);

    // Called when the file's data has been flushed to the cloud.
    void flushed(ReclaimContextPtr& context, FileResult result);

    // Queue a callback for execution when the reclaim completes.
    void queue(FileReclaimCallback callback);
}; // ReclaimContext

class TruncateOnException
{
    // What file do we want to truncate?
    FileBuffer& mFile;

    // What size do we want to truncate the file to?
    const std::uint64_t mSize;

public:
    TruncateOnException(FileBuffer& file, std::uint64_t size);

    ~TruncateOnException();
}; // TruncateOnException

// Retrieve an instance of a request's type tag.
template<typename Request>
auto tag(const Request& request)
    -> std::enable_if_t<IsFileRequestV<Request>, typename Request::Type>;

// Wrap a callback to ensure that exceptions are always handled.
template<typename Callback>
Callback swallow(Callback callback, const char* name);

thread_local unsigned short FileContext::mCallbackDepth{0};

void FileContext::addRange(const FileRange& range, Transaction& transaction)
{
    auto query = transaction.query(mService.queries().mAddFileRange);

    query.param(":begin").set(range.mBegin);
    query.param(":end").set(range.mEnd);
    query.param(":id").set(mInfo->id());

    query.execute();
}

auto FileContext::addScopedDownload(DownloadCategory& category,
                                    const FileRange& range) -> ScopedDestructor
{
    // Sanity: Range shouldn't be zero length.
    assert(!range.empty());

    // Where will our download be located in map?
    auto [iterator, added] = category.mInProgress.tryAdd(range, nullptr);

    // Sanity: Adding should always succeed as ranges never overlap.
    assert(added);

    if (!added)
        return ScopedDestructor();

    // Instantiate a download context for range.
    auto context = std::make_shared<DownloadContext>(category, *this, iterator);

    // Make sure our context's linked into map.
    iterator->second = context;

    // Try and create a download for our range.
    auto download = context->download();

    // Couldn't create a download for our range.
    if (!download)
        return ScopedDestructor();

    // Make sure we begin our download.
    return ScopedDestructor{[download = std::move(download)]()
                            {
                                download->begin();
                            }};
}

auto FileContext::cancel(const DownloadCategory& category,
                         const FileRange& range) -> DownloadContextPtrList
{
    // Convenience.
    const auto& inProgress = category.mInProgress;

    // Do any ranges end after range?
    auto begin = inProgress.endsAfter(range.mBegin);

    // No ranges end after range.
    if (begin == inProgress.end() || begin->first.mBegin >= range.mEnd)
        return {};

    // A range overlaps the beginning of range.
    if (begin->first.mBegin < range.mBegin && begin->first.mEnd <= range.mEnd)
        begin->second->range(begin->first.mBegin, range.mBegin);

    // Do any ranges end after range?
    auto end = inProgress.endsAfter(range.mEnd);

    // A range overlaps the ending of range.
    if (end != inProgress.end() && end->first.mBegin < range.mEnd)
        end->second->range(range.mEnd, end->first.mEnd);

    // Recompute begin and end;
    std::tie(begin, end) = inProgress.find(range);

    DownloadContextPtrList downloads;

    // Populate a list of the downloads that need to be cancelled.
    std::transform(begin, end, std::back_inserter(downloads), Select<1>());

    // Let our caller know which downloads need to be cancelled.
    return downloads;
}

auto FileContext::cancel(const FileRange& range) -> std::future<void>
{
    // So we can tell our caller when all downloads have completed.
    auto notifier = makeSharedPromise<void>();

    // So our caller can wait until all downloads have completed.
    auto waiter = notifier->get_future();

    // Make sure we have exclusive access to mDownloading.
    std::unique_lock lock(mLock);

    // Convenience.
    const DownloadCategory* categories[] = {&mDownloadCategories.mLarge,
                                            &mDownloadCategories.mNormal,
                                            &mDownloadCategories.mSmall}; // categories

    // Tracks which downloads we need to cancel.
    std::list<DownloadContextPtr> downloads;

    // Figure out which downloads we need to cancel, if any.
    for (auto* category: categories)
        downloads.splice(downloads.end(), cancel(*category, range));

    // No downloads need to be cancelled.
    if (downloads.empty())
        return notifier->set_value(), std::move(waiter);

    // Tracks how many downloads are still in progress.
    auto count = std::make_shared<std::atomic_size_t>(downloads.size());

    // Called when a download has completed.
    auto completed = [count, notifier](auto)
    {
        // All downloads have completed.
        if (count->fetch_sub(1) == 1)
            notifier->set_value();
    }; // completed

    // Make sure each download calls our callback when it completes.
    for (auto& download: downloads)
        download->queue(completed);

    // Release lock.
    //
    // Any downloads that were waiting on the lock will now complete.
    lock.unlock();

    // Cancel any downloads still in progress.
    for (auto& download: downloads)
        download->cancel();

    // Return waiter to our caller.
    return waiter;
}

void FileContext::cancel(FileRequest& request)
{
    // Cancel the request.
    std::visit(
        [&](auto& request)
        {
            completed(std::move(request), FILE_CANCELLED, false);
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

    // Cancel all downloads in progress.
    cancel(FileRange(0, mInfo->size()));

    // Cancel the flush if necessary.
    cancelFlushInProgress(false);

    // Cancel reclamation if necessary.
    if (std::unique_lock lock(mReclaimContextLock); mReclaimContext)
        mReclaimContext->cancel(mReclaimContext, std::move(lock));

    // Latch the request queue.
    auto requests = [&]()
    {
        std::lock_guard guard(mRequestsLock);
        return std::exchange(mRequests, FileRequestList());
    }();

    // Cancel any pending requests.
    //
    // We know this won't cause any other requests to be queued as we know
    // there are no live references to this instance.
    for (; !requests.empty(); requests.pop_front())
        cancel(requests.front());
}

void FileContext::cancelFlushInProgress(bool dueToWrite)
{
    if (std::unique_lock lock(mFlushContextLock); mFlushContext)
        mFlushContext->cancel(mFlushContext, std::move(lock), dueToWrite);
}

void FileContext::completed(BufferPtr buffer, FileReadRequest&& request, bool onCurrentThread)
{
    // Sanity.
    assert(buffer);

    // Convenience.
    auto [begin, end] = request.mRange;

    // Complete the user's request.
    completed(std::move(request),
              FileReadResult{*buffer, begin, end - begin},
              onCurrentThread,
              std::move(buffer));
}

template<typename Request, typename Result, typename... Captures>
auto FileContext::completed(Request&& request,
                            Result result,
                            bool onCurrentThread,
                            Captures&&... captures) -> std::enable_if_t<IsFileRequestV<Request>>
{
    // Sanity.
    assert(request.mCallback);

    // Make sure request has been passed by rvalue reference.
    static_assert(std::is_rvalue_reference_v<decltype(request)>);

    // Called to complete the user's request.
    auto wrapper =
        [=](auto& callback, auto& cookie, bool isWorker, auto&, auto& tag, auto&&...) mutable
    {
        // Determine the callback's concrete type.
        using Callback = std::remove_reference_t<decltype(callback)>;

        // Pass result as is when possible otherwise pass it as an unexpected.
        if constexpr (std::is_invocable_v<Callback, Result>)
            std::exchange(callback, Callback())(result);
        else
            std::exchange(callback, Callback())(unexpected(result));

        // Check if our context is still alive.
        auto context = cookie.lock();

        // Context isn't alive.
        if (!context)
            return;

        // Let the context know the request has completed.
        executed(tag);

        // Already running on a FileService worker,
        if (isWorker)
            return context->execute();

        // Inline path: No need to drain an empty request queue.
        if (!context->hasPendingRequests())
            return;

        // Inline path: Drain request queue on a worker thread.
        context->mService.executor().execute(
            [context](auto&)
            {
                context->execute();
            },
            true);
    }; // wrapper

    // Here simply to reduce duplication.
    auto bindWrapper = [&](bool isWorker)
    {
        return std::bind(std::move(wrapper),
                         swallow(std::move(request.mCallback), request.name()),
                         weak_from_this(),
                         isWorker,
                         std::placeholders::_1,
                         tag(request),
                         std::forward<Captures>(captures)...);
    }; // makeWrapper

    // Complete via worker.
    if (mCallbackDepth >= MAX_CALLBACK_DEPTH || !onCurrentThread)
    {
        mService.executor().execute(bindWrapper(true), true);
        return;
    }

    // Complete on the calling thread.
    //
    // Make sure we maintain this thread's callback depth.
    auto incrementer = makeScopedIncrementer(mCallbackDepth);

    // Execute the user's callback directly.
    Task task;

    bindWrapper(false)(task);
}

std::size_t FileContext::completed(const FileRange& range)
{
    return dispatch(
        [this](auto end, auto& request)
        {
            // Clamp the read as necessary.
            request.mRange.mEnd = std::min(end, request.mRange.mEnd);

            // Displace the file's buffer as necessary.
            auto buffer = displace(mBuffer, request.mRange.mBegin);

            // Complete the request.
            completed(std::move(buffer), std::move(request), false);
        },
        range);
}

bool FileContext::completeReadIfOnDisk(std::unique_lock<std::recursive_mutex>& lock,
                                       FileReadRequest& request,
                                       bool onCurrentThread)
{
    // Sanity: mLock should be held on entry.
    assert(lock.mutex() == &mLock);
    assert(lock.owns_lock());

    // Convenience.
    auto& range = request.mRange;

    // A request's range should never be zero.
    assert(range.length());

    // Can any range on disk partially satisfy request?
    auto iterator = mOnDisk.contains(range.mBegin);

    // No range on disk can partially satisfy request.
    if (iterator == mOnDisk.end())
        return false;

    // Clamp request's range as needed.
    range.mEnd = std::min(iterator->mEnd, range.mEnd);

    // Displace the file's buffer as necessary.
    auto buffer = displace(mBuffer, range.mBegin);

    // Caller wants to complete on this thread.
    if (onCurrentThread)
        lock.unlock();

    // Complete the request.
    completed(std::move(buffer), std::move(request), onCurrentThread);

    // Let the caller know we've completed the request.
    return true;
}

template<typename Dispatcher>
std::size_t FileContext::dispatch(Dispatcher&& dispatcher, const FileRange& range)
{
    // What requests might we be able to satisfy?
    auto i = mPendingReadRequests.lower_bound(range.mBegin);
    auto j = mPendingReadRequests.lower_bound(range.mEnd);

    // Tracks how many requests we've satisfied.
    std::size_t satisfied = 0;

    // Dispatch as many requests as we can.
    for (; i != j; ++satisfied)
    {
        // Dispatch the request.
        dispatcher(range.mEnd, const_cast<FileReadRequest&>(*i));

        // Move to the next request.
        i = mPendingReadRequests.erase(i);
    }

    // Let our caller know how many requests were satisfied.
    return satisfied;
}

bool FileContext::executable(std::unique_lock<std::mutex>& lock, const FileRequest& request)
{
    assert(lock.mutex() == &mRequestsLock);
    assert(lock.owns_lock());

    return std::visit(
        [&lock, this](auto&& request)
        {
            return this->executable(false /*checkOnly*/, lock, tag(request));
        },
        request);
}

bool FileContext::executableCheck(std::unique_lock<std::mutex>& lock, const FileRequest& request)
{
    assert(lock.mutex() == &mRequestsLock);
    assert(lock.owns_lock());

    return std::visit(
        [&lock, this](auto&& request)
        {
            return this->executable(true /*checkOnly*/, lock, tag(request));
        },
        request);
}

bool FileContext::executable(bool checkOnly,
                             [[maybe_unused]] std::unique_lock<std::mutex>& lock,
                             FileReadRequestTag)
{
    assert(lock.mutex() == &mRequestsLock);
    assert(lock.owns_lock());

    return mReadWriteState.read(checkOnly);
}

bool FileContext::executable(bool checkOnly,
                             [[maybe_unused]] std::unique_lock<std::mutex>& lock,
                             FileWriteRequestTag)
{
    assert(lock.mutex() == &mRequestsLock);
    assert(lock.owns_lock());

    // Writes may be able to cancel any flush in progress.
    //
    // Flushes queued after this write are unaffected and will execute after
    // this write has completed.
    if (!checkOnly)
        cancelFlushInProgress(true);

    return mReadWriteState.write(checkOnly);
}

void FileContext::execute(FileAppendRequest& request)
{
    // An immediate append runs the user's callback on the calling thread.
    DeferredCompletion complete(*this, request, true);

    // Convenience.
    auto size = mInfo->size();

    // Schedules a flush when exiting this function, if needed.
    ScopedDestructor flusher;

    // Acquire lock.
    std::lock_guard guard(mLock);

    // Caller doesn't actually want to write anything.
    if (!request.mLength)
        return complete(FileWriteResult{size, 0ul});

    // Caller hasn't passed us a valid buffer.
    if (!request.mBuffer)
        return complete(FILE_INVALID_ARGUMENTS);

    // Disambiguate.
    using file_service::write;

    // Try and write the user's data to disk.
    auto [length, _] = mBuffer->write(request.mBuffer, size, request.mLength);

    // Couldn't write any of the user's data to disk.
    if (length == 0)
        return complete(FILE_FAILED);

    // Compute the file's new modification time.
    const auto modified = now();

    // Compute range based on how much data we wrote to disk.
    const FileRange range = {size, size + length};

    // Try and update the file's metadata.
    {
        // Convenience.
        auto& database = mService.database();

        // Try and remove the data we wrote if we encounter an exception.
        TruncateOnException truncater(*mBuffer, size);

        // Acquire database lock.
        UniqueLock databaseLock(database);

        // Start a transaction so we can safely modify the database.
        auto transaction = database.transaction();

        // Update ranges in the database.
        const auto effectiveRange = updateRangeInDatabase(range, transaction, modified);

        // Persist our changes.
        transaction.commit();

        // Update ranges in memory.
        updateRangeInMemory(effectiveRange);
    }

    // Schedule our modifications for flushing.
    flusher = scopedScheduleFlush();

    // Update the file's attributes.
    mInfo->written(request.mApplicationData, modified, range);

    // Queue the user's request for execution.
    complete(FileWriteResult{size, length});
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
    executeOrQueue(FileReadRequest{std::bind(&FetchContext::operator(),
                                             mFetchContext.get(),
                                             mFetchContext,
                                             std::placeholders::_1),
                                   FileRange(0, mInfo->size())});
}

// A flush must never complete on the calling thread. It is issued by reclaim() while holding
// mReclaimContextLock and its callback (ReclaimContext::flushed) re-acquires that non-recursive
// lock, so completing inline would self-deadlock.
void FileContext::execute(FileFlushRequest request)
{
    // The file hasn't been modified.
    if (!mInfo->dirty())
        return completed(std::move(request), FILE_SUCCESS, false);

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
    executeOrQueue(FileFetchRequest{std::bind(&FlushContext::operator(),
                                              mFlushContext.get(),
                                              mFlushContext,
                                              std::placeholders::_1)});
}

void FileContext::execute(FileReadRequest& request)
{
    std::visit(
        [&](auto& tag)
        {
            execute(request, tag);
        },
        request.mReadTag);
}

void FileContext::execute(FileReadRequest& request, const NormalReadTag&)
{
    assert(std::holds_alternative<NormalReadTag>(request.mReadTag));

    // Make sure the user's read is within the file.
    request.mRange = clamp(request.mRange, 0, mInfo->size());

    // The user doesn't actually need to read anything.
    if (request.mRange.empty())
        return completed(mBuffer, std::move(request), true);

    // Get a snapshot of the current service options.
    const auto options = mService.serviceOptions();

    // Convenience.
    const auto maxGapDistance = options.mMaximumGapDistance;
    const auto splitThreshold = options.mRangeSplitThreshold;

    // Begin a download if necessary when exiting this function.
    std::optional<ScopedDestructor> downloader;

    // We don't update the access time in database on every read.
    // And this can be performed outside mLock.
    bumpAccessTime(options.mAccessTimeUpdateInterval);

    // Acquire lock.
    std::unique_lock lock(mLock);

    // Request can be satisfied by data already on disk. On an immediate
    // execution a cache hit completes on this thread: the lock is released
    // inside the call, so we must return right away.
    if (completeReadIfOnDisk(lock, request, true))
        return;

    // Sanity: lock must remain held.
    assert(lock.owns_lock());

    // Copy the request's range so it can be queued.
    auto range = request.mRange;

    // For easier access.
    auto& [begin, end] = range;

    // Queue the request for later completion.
    queueReadRequest(std::move(request));

    // Convenience.
    auto& category = mDownloadCategories.mNormal;
    auto& inProgress = category.mInProgress;

    // Request is contained within an existing download.
    if (auto i = inProgress.contains(begin); i != inProgress.end())
    {
        // For the sake of clarity.
        auto [otherRange, context] = *i;

        // Which is likely to satisfy our request shortly.
        if (context->timeUntil(begin) <= splitThreshold)
            return;

        // Fragment the download's range.
        context->range(otherRange.mBegin, begin);
    }

    // Assume our new download extends until the end of the file.
    end = mInfo->size();

    // Compute effective range.
    FileRange effectiveRange;

    // Adds a download for the specified range.
    auto download = [&](auto range)
    {
        // Sanity: Downloads should never be zero length.
        assert(!range.empty());

        // Try and download our effective range.
        downloader = addScopedDownload(category, range);
    }; // download

    // Iterate over each on-disk gap contained within our range.
    for (auto& diskGap: gaps(mOnDisk, range))
    {
        // Iterate over each undownloaded gap within that gap.
        for (auto& downloadGap: gaps(inProgress, diskGap))
        {
            // Effective range begins from the first undownloaded gap.
            if (effectiveRange.empty())
            {
                effectiveRange = downloadGap;

                // Gap is adjacent to an existing download.
                if (inProgress.beginsAt(downloadGap.mEnd))
                    return download(effectiveRange);

                // Step to the next gap.
                continue;
            }

            // Gap is too distant from its predecessor.
            if (downloadGap.mBegin - effectiveRange.mEnd > maxGapDistance)
                return download(effectiveRange);

            // Bump the end of the effective range.
            effectiveRange.mEnd = downloadGap.mEnd;

            // Gap is adjacent to an existing download.
            if (inProgress.beginsAt(downloadGap.mEnd))
                return download(effectiveRange);
        }
    }

    // Try and download our range.
    download(effectiveRange);
}

void FileContext::execute(FileReadRequest& request, const StreamReadTag&)
{
    // Stream reading does not complete on the current thread because it may execute
    // on the HTTP server's libuv event loop thread.
    // This can be enabled if that behavior changes.
    constexpr bool onCurrentThread = false;

    assert(std::holds_alternative<StreamReadTag>(request.mReadTag));

    // Make sure the user's read is within the file.
    request.mRange = clamp(request.mRange, 0, mInfo->size());

    // The user doesn't actually need to read anything.
    if (request.mRange.empty())
        return completed(mBuffer, std::move(request), onCurrentThread);

    // Get a snapshot of the current service options.
    const auto options = mService.serviceOptions();

    // Convenience.
    const auto backwardAlignment = options.mJumpBackwardAlignment;
    const auto backwardDistanceInMs = static_cast<uint64_t>(options.mJumpBackwardDistance.count());
    const auto forwardDistance = options.mJumpForwardDistance;
    const auto immediateThreshold = options.mImmediateDownloadThreshold;
    const auto minimumRangeSize = options.mMinimumRangeSize;

    // Cancels a download, if needed, when exiting this function.
    ScopedDestructor canceller;

    // Begins a download afterwards, if needed, when exiting this function
    std::optional<ScopedDestructor> downloader;

    // Convenience.
    auto& largeCategory = mDownloadCategories.mLarge;
    auto& largeDownloads = largeCategory.mInProgress;
    auto& smallCategory = mDownloadCategories.mSmall;
    auto& smallDownloads = smallCategory.mInProgress;

    // Update access time in database
    bumpAccessTime(options.mAccessTimeUpdateInterval);

    // Acquire lock.
    std::unique_lock lock(mLock);

    // True if range is covered by a download in map.
    auto downloading = [&](auto& map, auto& range)
    {
        DownloadContextPtr download;

        // Does any download in map contain request's beginning?
        auto iterator = map.contains(range.mBegin);

        // A download contains request's beginning.
        if (iterator != map.end())
            download = iterator->second;

        // Return download if any to our caller.
        return download;
    }; // downloading

    // Extends a position to the left boundary of a cached on-disk range.
    // Given a position, this function checks whether the position lies within
    // or directly at the end of an existing on-disk range. If so, it returns the
    // beginning of that range, effectively extending the position to the left.
    auto leftExtendOnDisk = [&](uint64_t position)
    {
        if (auto iterator = mOnDisk.endsAtOrAfter(position);
            iterator != mOnDisk.end() && iterator->mBegin < position)
            return iterator->mBegin;

        return position;
    };

    // True if request is a jump.
    auto isJump = [&](auto& request) -> DownloadContextPtr
    {
        // A request's range should never be zero.
        assert(request.mRange.length());

        // Request isn't a jump candidate.
        if (!isJumpCandidate(request.mReadTag))
            return nullptr;

        // Request isn't a large read.
        if (request.mRange.length() <= immediateThreshold)
            return nullptr;

        // No large range download is in progress.
        if (largeDownloads.empty())
            return nullptr;

        // Request can be completely satisfied by data on disk.
        if (auto iterator = mOnDisk.contains(request.mRange.mBegin);
            iterator != mOnDisk.end() && iterator->mEnd >= request.mRange.mEnd)
            return nullptr;

        // Get a reference to the large download in progress.
        auto iterator = largeDownloads.begin();

        // Left extend the begin of from the disk cache
        auto effectiveBegin = leftExtendOnDisk(iterator->first.mBegin);

        // Convenience.
        auto context = iterator->second;

        // Request begins before our adjacent disk cache + the large read.
        if (request.mRange.mBegin < effectiveBegin)
        {
            // How far ahead is the request?
            auto distance = effectiveBegin - request.mRange.mBegin;

            // Not far enough to be considered a jump.
            if (distance <= immediateThreshold)
                return nullptr;

            // Far enough to be considered a jump.
            return context;
        }

        // Request sits on the adjacent disk cache, not a jump
        if (request.mRange.mBegin < iterator->first.mBegin)
            return nullptr;

        // Existing large download will satisfy request shortly.
        if (context->timeUntil(request.mRange.mBegin) <= forwardDistance)
            return nullptr;

        // We can satisfy request quicker if we begin a new download.
        return context;
    }; // isJump

    // Check if request is a small prefix of a large download.
    //
    // If true, request will be clamped so it does not overlap the leading
    // edge of the large download.
    auto isSmallPrefix = [&](auto& request)
    {
        // Convenience.
        auto& range = request.mRange;

        // Does request overlap an existing large download?
        auto iterator = largeDownloads.beginsAfter(range.mBegin);

        // Request doesn't overlap an existing large download.
        if (iterator == largeDownloads.end() || range.mEnd <= iterator->first.mBegin)
            return false;

        // Request isn't a small prefix of the large download.
        if (iterator->first.mBegin - range.mBegin > immediateThreshold)
            return false;

        // Translate request into a small read.
        range.mEnd = iterator->first.mBegin;

        // Range should never be empty.
        assert(!range.empty());

        // Let our caller know that request is a small prefix.
        return true;
    }; // isSmallPrefix

    // Sanity: We should only ever have one large download at most.
    assert(largeDownloads.size() <= 1);

    // Request is a jump.
    if (auto context = isJump(request))
    {
        // Mark download as being replaced.
        // This will remove it from mDownloading.mLarge.
        context->replaced(context);

        // Cancel the download when we exit this function.
        canceller = [context]()
        {
            context->cancel();
        };
    }

    // We're performing a large read.
    //
    // While used here purely for control flow effects.
    while (request.mRange.length() > immediateThreshold)
    {
        // Request is a small prefix of a large download.
        if (isSmallPrefix(request))
            break;

        // Convenience.
        auto range = request.mRange;

        // Request can't be satisfied by data on disk.
        //
        // Never complete inline here: this loop continues to inspect and
        // mutate lock-protected state after a hit, so the lock must stay held.
        if (!completeReadIfOnDisk(lock, request, false))
            queueReadRequest(std::move(request));

        // Sanity: lock must remain held.
        assert(lock.owns_lock());

        // A large download is already in progress.
        if (!largeDownloads.empty())
            return;

        // Extend range to the left if possible.

        // Compute bitrate.
        auto bitrate = std::max<std::uint64_t>(1, largeCategory.mAverager.getValue());

        // divide 8 from bits to Bytes, divide 1000 from Ms to seconds
        uint64_t backwardBytes = backwardDistanceInMs * bitrate / 8 / 1000;

        range.mBegin -= std::min(backwardBytes, range.mBegin);

        // Round down to the nearest boundary.
        range.mBegin &= ~((UINT64_C(1) << backwardAlignment) - 1);

        // Extend range to the end the file.
        range.mEnd = mInfo->size();

        // Bump range as necessary so we don't redownload data.
        if (auto iterator = mOnDisk.endsAfter(range.mBegin);
            iterator != mOnDisk.end() && iterator->mBegin <= range.mBegin)
            range.mBegin = iterator->mEnd;

        // Try and download the file's remaining data if necessary.
        if (range.length())
            downloader = addScopedDownload(largeCategory, range);

        // Nothing more to do.
        return;
    }

    // Convenience.
    auto range = request.mRange;

    // Sanity.
    assert(range.length() <= immediateThreshold);

    // Request can be satisfied by data already on disk.
    // And the lock is released inside the call.
    if (completeReadIfOnDisk(lock, request, onCurrentThread))
        return;

    // Sanity: lock must remain held.
    assert(lock.owns_lock());

    // Queue the request for later completion.
    queueReadRequest(std::move(request));

    // Request can be satisfied by an existing large download.
    if (auto download = downloading(largeDownloads, range);
        download && download->timeUntil(range.mBegin) <= seconds(1))
        return;

    // Extend the small range's length so our download is worthwhile.
    auto length = std::max(minimumRangeSize, range.length());

    // Make sure the small range doesn't extend beyond the end of the file.
    length = std::min(length, mInfo->size() - range.mBegin);

    // Update the small range's end point.
    range.mEnd = range.mBegin + length;

    // Find all gaps in range that aren't covered by a small download.
    auto current = gaps(smallDownloads, range);
    auto end = current.end();

    // Range is already covered by small downloads.
    if (current == end)
        return;

    // Bump beginning of range.
    range.mBegin = current->mBegin;
    range.mEnd = current->mEnd;

    // Sanity: range should never be empty.
    assert(!range.empty());

    // Sanity: a range only has a download
    assert(!downloader);

    // Try and add a small download that'll cover request.
    downloader = addScopedDownload(smallCategory, range);
}

// When this request is executed, any pending downloads will have completed.
// Reclaim requests always complete via a worker.
void FileContext::execute(FileReclaimRequest& request)
{
    // Make sure no one is messing with our range maps.
    std::lock_guard guard(mLock);

    // All downloads should be completed.
    if (!mDownloadCategories.mLarge.mInProgress.empty() ||
        !mDownloadCategories.mNormal.mInProgress.empty() ||
        !mDownloadCategories.mSmall.mInProgress.empty())
        return completed(std::move(request), UINT64_C(0), false);

    // Sanity: no pending read.
    assert(mPendingReadRequests.empty());

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
        return completed(std::move(request), FILE_FAILED, false);

    // Remove all written ranges from memory.
    mOnDisk.clear();

    // Update the file's size.
    updateSize(mInfo->size(), transaction);

    // Persist our changes.
    transaction.commit();

    // How much space did we reclaim?
    auto reclaimed = request.mAllocatedSize - mInfo->allocatedSize();

    // Let waiters know how much space we reclaimed.
    completed(std::move(request), reclaimed, false);
}

// Touch requests always complete via a worker.
void FileContext::execute(FileTouchRequest& request)
{
    // Compute the file's new access time.
    auto accessed = now();

    // Convenience.
    auto& database = mService.database();

    // Schedules a flush when exiting this function, if needed.
    ScopedDestructor flusher;

    // Acquire database lock.
    UniqueLock databaseLock(database);

    // Start a transaction so we can safely modify the database.
    auto transaction = database.transaction();

    // Convenience.
    auto modified = request.mModified;

    // Couldn't update the file's modification time.
    if (!mFile->ftouch(modified))
        return completed(std::move(request), FILE_FAILED, false);

    // Update the file's access and modification time.
    updateAccessAndModificationTimes(accessed, modified, transaction);

    // Persist our changes.
    transaction.commit();

    // Schedule our modifications for flushing.
    flusher = scopedScheduleFlush();

    // Update file attributes.
    mInfo->modified(accessed, request.mApplicationData, modified);

    // Queue the user's request for completion.
    completed(std::move(request), FILE_SUCCESS, false);
}

// Truncate requests always complete via a worker.
void FileContext::execute(FileTruncateRequest& request)
{
    // Convenience.
    const auto newSize = request.mSize;
    const auto oldSize = mInfo->size();

    // User isn't changing this file's size.
    if (newSize == oldSize)
        return completed(std::move(request), FileTruncateResult{newSize, newSize}, false);

    // We currently use truncate to implement the Windows SetFileSize callback with
    // AllocationSize. This callback is intended for preallocating disk space. For File Service,
    // however, there is no need to perform actual preallocation, so we only need to
    // return success. At the same time, we must not modify the file's actual size,
    // otherwise the file content would be incorrectly extended.
    if (newSize > oldSize && (request.mFlags & FILE_TRUNCATE_FLAG_NO_GROW))
        return completed(std::move(request), FileTruncateResult{oldSize, oldSize}, false);

    // Cancels any downloads that would be cut-off or overtaken.
    ScopedDestructor canceller;

    // Schedules a flush when exiting this function, if needed.
    ScopedDestructor flusher;

    // Grow or shink this file's size as required.
    auto [lock, modified] = [&]()
    {
        // User's extending this file's size.
        if (newSize > oldSize)
            return grow(request);

        // User's reducing this file's size.
        return shrink(request);
    }();

    // Cancel downloads that would be cut-off or overtaken.
    canceller = [=]()
    {
        const auto from = std::min(newSize, oldSize);
        const auto to = std::max(newSize, oldSize);

        cancel(FileRange(from, to));
    };

    // Schedule our modifications for flushing.
    flusher = scopedScheduleFlush();

    // Queue the user's request to for completion.
    completed(std::move(request), FileTruncateResult{newSize, oldSize}, false);
}

void FileContext::execute(FileWriteRequest& request)
{
    DeferredCompletion complete(*this, request, true);

    // Caller hasn't passed us a valid buffer.
    if (!request.mBuffer)
        return complete(FILE_INVALID_ARGUMENTS);

    // Caller doesn't want to extend the file's size.
    if (request.mFlags & FILE_WRITE_FLAG_NO_GROW)
    {
        // Convenience.
        auto& [begin, end] = request.mRange;

        // Make sure written range doesn't extend past the end of the file.
        end = std::min(mInfo->size(), end);

        // Make sure written range doesn't begin after it ends.
        begin = std::min(begin, end);
    }

    // Caller doesn't actually want to write anything.
    if (auto [begin, end] = request.mRange; begin == end)
        return complete(FileWriteResult{begin, end - begin});

    // Cancel downloads within the written range on return.
    ScopedDestructor canceller;

    // Flushes our modifications to the cloud on return.
    ScopedDestructor flusher;

    // Acquire lock.
    std::lock_guard guard(mLock);

    // Get and cache a reference to our database.
    auto& database = mService.database();

    // Update range metadata in the database.
    auto updateRangeInDatabase = [&database, this](auto range)
    {
        // So we have exclusive access to the database.
        std::unique_lock lockDatabase(database);

        // So we can safely update the database.
        auto transaction = database.transaction();

        // Update metadata in the database.
        range = this->updateRangeInDatabase(range, transaction);

        // Persist database changes.
        transaction.commit();

        // Return effective range.
        return range;
    };

    // Update range metadata.
    auto updateRange = [&](auto begin, auto end)
    {
        // How much data should we say was written to disk?
        std::uint64_t count = 0;

        // A range on disk contains all or part of our range.
        if (auto i = mOnDisk.contains(begin); i != mOnDisk.end())
        {
            // Range on disk contains all of our range.
            if (i->mEnd >= end)
                return end - begin;

            // How much of our data does this range contain?
            count = i->mEnd - begin;
        }

        // Try and update range metadata.
        try
        {
            // Convenience.
            FileRange range(begin, end);

            // Update metadata in the database.
            const auto effectiveRange = updateRangeInDatabase(range);

            // Update metadata in memory.
            updateRangeInMemory(effectiveRange);

            // Metadata has been updated.
            return end - begin;
        }
        catch (...)
        {
            auto exception = std::current_exception();

            FSWarningF("Couldn't update range metadata in database: %s: %s",
                       toString(mInfo->id()).c_str(),
                       exceptionMessage().c_str());

            // We wrote data into a gap and couldn't add a new range.
            if (count == 0)
                std::rethrow_exception(exception);

            // We wrote data into an existing range.
            return count;
        }
    }; // updateRange

    // Update the file's access and modification times in the database.
    auto updateTimesInDatabase = [&]()
    {
        // Latch the file's current dirty status and modification time.
        const auto oldDirty = mInfo->dirty();
        const auto oldModified = mInfo->modified();

        // Try and update the file's modification time.
        try
        {
            // Compute the file's new modification time.
            const auto modified = now();

            // No need to update dirty status or modification time.
            if (oldDirty && oldModified == modified)
                return modified;

            // Make sure we have exclusive access to the database.
            std::unique_lock lockDatabase(database);

            // So we can safely update the database.
            auto transaction = database.transaction();

            // Try and update the modification time in the database.
            updateAccessAndModificationTimes(modified, modified, transaction);

            // Persist our database changes.
            transaction.commit();

            // Return updated modification time to our caller.
            return modified;
        }
        catch (...)
        {
            // Couldn't update the file's modification time.
            return oldModified;
        }
    }; // updateModificationTimeInDatabase

    // Write data to disk and update metadata as needed.
    auto write = [&]()
    {
        // Translate request's buffer into a byte buffer.
        auto* buffer = static_cast<const std::uint8_t*>(request.mBuffer);

        // Return the function that'll actually do the writing.
        return [&, buffer](auto offset, auto length) mutable
        {
            // Try and write data to disk.
            auto [count, _] = mBuffer->write(buffer, offset, length);

            // Couldn't write any data to disk.
            if (count == 0)
                return count;

            // Update range metadata.
            count = updateRange(offset, offset + count);

            // Bump buffer.
            buffer += count;

            // Return number of bytes written to disk.
            return count;
        };
    }();

    // Called when the write has completed.
    auto writeCompleted = [&]()
    {
        // Convenience.
        const auto& range = request.mRange;

        // Cancel any downloads within the written range.
        canceller = [=]()
        {
            cancel(range);
        };

        // Flush our modifications to the cloud.
        flusher = scopedScheduleFlush();

        // Update the file's access and modification times.
        const auto modified = updateTimesInDatabase();

        // Update file attributes.
        mInfo->written(request.mApplicationData, modified, range);

        // Complete the request.
        complete(FileWriteResult{range.mBegin, range.length()});
    }; // writeCompleted

    // Called when we're unable to write data to disk.
    auto writeFailed = [&](auto offset)
    {
        // Update request to reflect the amount of data written to disk.
        request.mRange.mEnd = offset;

        // Couldn't write any data to disk.
        if (request.mRange.empty())
            return complete(FILE_FAILED);

        // Partially complete the request.
        writeCompleted();
    }; // writeFailed

    // Where in our file should we write the data?
    auto offset = request.mRange.mBegin;

    // Write data to disk by filling gaps and extending ranges.
    for (auto& gap: gaps(mOnDisk, request.mRange))
    {
        // Writing into an existing range.
        if (const auto length = gap.mBegin - offset; length > 0)
        {
            // Couldn't write all data to disk.
            if (const auto count = write(offset, length); count < length)
                return writeFailed(count + offset);

            // Bump offset.
            offset += length;
        }

        // How much data do we need to write to disk?
        const auto length = gap.length();

        // Couldn't write all data to disk.
        if (const auto count = write(offset, length); count < length)
            return writeFailed(count + offset);

        // Bump offset.
        offset += length;
    }

    // Do we still have to write any data to disk?
    const auto length = request.mRange.mEnd - offset;

    // All data has been written to disk.
    if (length == 0)
        return writeCompleted();

    // Couldn't write remaining data to disk.
    if (const auto count = write(offset, length); count < length)
        return writeFailed(count + offset);

    // All data has been written to disk.
    writeCompleted();
}

void FileContext::execute(FileRequest& request)
{
    // Execute the user's request.
    std::visit(
        [this](auto& request)
        {
            executeCommon(std::move(request));
        },
        request);
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
        if (!executable(lock, mRequests.front()))
            return;

        // Pop the request off the queue.
        auto request = std::move(mRequests.front());

        mRequests.pop_front();

        // Execute on the current thread if this is the last request or the next
        // one isn't ready yet: no worker hand-off needed.
        //
        // Otherwise dispatch to a worker so the next executable request isn't
        // delayed waiting for this one to complete.
        bool executeInline = mRequests.empty() || !executableCheck(lock, mRequests.front());

        // Release lock.
        lock.unlock();

        if (executeInline)
        {
            execute(request);
            continue;
        }

        mService.executor().execute(
            [context = weak_from_this(), request = std::move(request)](const Task&) mutable
            {
                if (auto ptr = context.lock())
                    ptr->execute(request);
            },
            true);
    }
}

template<typename Request>
auto FileContext::executeCommon(Request&& request) -> std::enable_if_t<IsFileRequestV<Request>>
try
{
    // Immediately reject the request if necessary.
    //
    // completed(...) needs to be called here as it expects a request to hold some lock.
    if (auto result = reject(request); result != FILE_SUCCESS)
        return completed(std::forward<Request>(request), result, false);

    // Otherwise execute the request.
    //
    // Each overload decides what an immediate execution means for its request
    // type: requests that can be satisfied synchronously (a read whose data is
    // already on disk, a write, an append) run the user's callback on this
    // thread, skipping a worker hand-off, the other overloads ignore the
    // argument and always complete via a worker.
    execute(request);
}
catch (...)
{
    // Encountered an unhandled exception while executing request.
    FSErrorF("Unable to execute %s request: %s", request.name(), exceptionMessage().c_str());

    // Try and fail the request.
    completed(std::move(request), FILE_FAILED, false);
}

template<typename Request>
auto FileContext::executeOrQueue(Request&& request) -> std::enable_if_t<IsFileRequestV<Request>>
{
    // Sanity.
    assert(request.mCallback);

    // Make sure the request's been passed by rvalue reference.
    static_assert(std::is_rvalue_reference_v<decltype(request)>);

    // Request isn't executable so queue it for later execution.
    //
    // If executable(...) returns true, request will have acquired a read (or write) lock.
    if (std::unique_lock lock(mRequestsLock); !executable(lock, request))
        return queue(std::move(lock), std::forward<Request>(request));

    // Otherwise execute the request.
    executeCommon(std::forward<Request>(request));
}

void FileContext::executed(FileReadRequestTag)
{
    mReadWriteState.readCompleted();
}

void FileContext::executed(FileWriteRequestTag)
{
    mReadWriteState.writeCompleted();
}

// Because Microsoft redefine small to be "char."
#ifdef _WIN32
#pragma push_macro("small")
#undef small
#endif // _WIN32

void FileContext::failed(const FileRange& range, FileResult result)
{
    // Convenience.
    const auto& large = mDownloadCategories.mLarge.mInProgress;
    const auto& normal = mDownloadCategories.mNormal.mInProgress;
    const auto& small = mDownloadCategories.mSmall.mInProgress;

    // What pending read requests are within range?
    auto current = mPendingReadRequests.lower_bound(range.mBegin);
    auto end = mPendingReadRequests.upper_bound(range.mEnd);

    // Iterate over each pending read within range.
    while (current != end)
    {
        // Keeps logic simple.
        auto iterator = current++;

        // Convenience.
        const auto begin = iterator->mRange.mBegin;

        // True if begin is contained within an existing download.
        auto contained = overloaded{[&](const NormalReadTag&)
                                    {
                                        // begin is contained within an existing normal download.
                                        return normal.contains(begin) != normal.end();
                                    },
                                    [&](const StreamReadTag&)
                                    {
                                        // begin is contained within an existing streaming download.
                                        return large.contains(begin) != large.end() ||
                                               small.contains(begin) != small.end();
                                    }};

        // begin is contained within an existing download.
        if (std::visit(contained, iterator->mReadTag))
            continue;

        // Dirty but necessary.
        auto& request = const_cast<FileReadRequest&>(*iterator);

        // Fail the request.
        completed(std::move(request), result, false);

        // Remove read from our set of pending reads.
        mPendingReadRequests.erase(iterator);
    }
}

#ifdef _WIN32
#pragma pop_macro("small")
#endif // _WIN32

auto FileContext::grow(const FileTruncateRequest& request)
    -> std::pair<std::unique_lock<std::recursive_mutex>, std::int64_t>
{
    // Convenience.
    const auto newSize = request.mSize;
    const auto oldSize = mInfo->size();

    // Make sure we have exclusive access to mOnDisk.
    std::unique_lock lock(mLock);

    // Couldn't extend the file's size.
    if (!mBuffer->truncate(newSize))
        throw FSError1("Couldn't extend file size");

    // Compute the file's new modification time.
    const auto modified = now();

    // Update ranges on disk and in memory.
    {
        // Convenience.
        auto& database = mService.database();

        // Acquire database lock.
        UniqueLock databaseLock(database);

        // So we can safely modify the database.
        auto transaction = database.transaction();

        // Keeps the line below short.
        const FileRange range(oldSize, newSize);

        // Update the file's ranges in the database.
        const auto effectiveRange = updateRangeInDatabase(range, transaction, modified);

        // Persist our changes.
        transaction.commit();

        // Update the file's ranges in memory.
        updateRangeInMemory(effectiveRange);
    }

    // Update the file's attributes.
    mInfo->truncated(request.mApplicationData, modified, newSize);

    // Return lock and modification time to our caller.
    return std::make_pair(std::move(lock), modified);
}

bool FileContext::hasPendingRequests() const
{
    std::lock_guard guard(mRequestsLock);

    return !mRequests.empty();
}

std::chrono::steady_clock::time_point FileContext::lastNormalReadActivityTime() const
{
    return std::chrono::steady_clock::time_point(mLastNormalReadActivityTime.load());
}

void FileContext::queueReadRequest(FileReadRequest&& request)
{
    // We should never queue a request for zero bytes.
    assert(request.mRange.length());

    // Queue the request for later completion.
    mPendingReadRequests.emplace(std::move(request));
}

template<typename Request>
auto FileContext::queue([[maybe_unused]] std::unique_lock<std::mutex> lock,
                        Request&& request) -> std::enable_if_t<IsFileRequestV<Request>>
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
}

template<typename Request>
auto FileContext::reject([[maybe_unused]] const Request& request)
    -> std::enable_if_t<IsFileRequestV<Request>, FileResult>
{
    // File Service is being torn down.
    if (mService.deinitializing())
        return FILE_CANCELLED;

    if constexpr (!IsFileReclaimRequestV<Request> && IsFileWriteRequestV<Request>)
    {
        if constexpr (IsFileRemoveRequestV<Request>)
        {
            if (request.mServiceOnly)
                return FILE_SUCCESS;
        }

        if (mKeyData)
            return FILE_READONLY;
    }

    return FILE_SUCCESS;
}

void FileContext::removeRanges(const FileRange& range, Transaction& transaction)
{
    auto query = transaction.query(mService.queries().mRemoveFileRanges);

    query.param(":begin").set(range.mBegin);
    query.param(":end").set(range.mEnd);
    query.param(":id").set(mInfo->id());

    query.execute();
}

ScopedDestructor FileContext::scopedScheduleFlush()
{
    // Return scoped destructor to our caller.
    return makeScopedDestructor(
        [this]()
        {
            const auto options = mService.serviceOptions();
            const auto delay = computeFlushDelay(options, mInfo->size());

            mService.flusher().schedule(mInfo->id(), delay);
        });
}

FileResult FileContext::setRemoved()
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
    mInfo->removed(std::true_type{});

    // Let the caller know the file was removed.
    return FILE_SUCCESS;
}

catch (...)
{
    // Let debuggers know why we couldn't remove the file.
    FSErrorF("Unable to mark file %s as removed: %s",
             toString(mInfo->id()).c_str(),
             exceptionMessage().c_str());

    // Let our caller know we couldn't remove the file.
    return FILE_FAILED;
}

auto FileContext::shrink(const FileTruncateRequest& request)
    -> std::pair<std::unique_lock<std::recursive_mutex>, std::int64_t>
{
    // Convenience.
    const auto newSize = request.mSize;
    const auto oldSize = mInfo->size();

    // Convenience.
    auto& database = mService.database();

    // Called to add a reduced range to the database.
    std::function<void(Transaction&)> addRangeToDatabase = [](auto&) {};

    // Called to add a reduced range to memory.
    std::function<void()> addRangeToMemory = []() {};

    // Called to remove affected ranges from the database.
    std::function<void(Transaction&)> removeFromDatabase = [](auto&) {};

    // Called to remove affected ranges from memory.
    std::function<void()> removeFromMemory = []() {};

    // So we have exclusive access to mOnDisk.
    std::unique_lock lock(mLock);

    // Figure out what changs we need to make.
    do
    {
        // What ranges end after our file's new size?
        auto begin = mOnDisk.endsAfter(newSize);

        // No ranges end after our new size.
        if (begin == mOnDisk.end())
            break;

        // Convenience.
        FileRange range(begin->mBegin, oldSize);

        // Remove affected ranges from the database.
        removeFromDatabase = [range, this](auto& transaction)
        {
            removeRanges(range, transaction);
        };

        // Remove affected ranges from memory.
        removeFromMemory = [begin, this]()
        {
            mOnDisk.remove(begin, mOnDisk.end());
        };

        // No range needs to be reduced.
        if (range.mBegin >= newSize)
            break;

        // Adjust the range's end point.
        range.mEnd = newSize;

        // Add reduced range to the database.
        addRangeToDatabase = [range, this](auto& transaction)
        {
            addRange(range, transaction);
        };

        // Add reduced range to memory.
        addRangeToMemory = [range, this]()
        {
            mOnDisk.add(range);
        };
    }
    while (0);

    // Compute the file's new modification time.
    const auto modified = now();

    // Update the database as needed.
    {
        // Acquire database lock.
        UniqueLock databaseLock(database);

        // So we can safely modify the database.
        auto transaction = database.transaction();

        // Remove affected ranges from the database.
        removeFromDatabase(transaction);

        // Add reduced range to the database.
        addRangeToDatabase(transaction);

        // Update the file's access time, modification time and size in the database.
        updateSizeAndTime(modified, modified, newSize, transaction);

        // Persist our changes.
        transaction.commit();
    }

    // Remove affected ranges from memory.
    removeFromMemory();

    // Add reduced range to memory.
    addRangeToMemory();

    // Update the file's attributes.
    mInfo->truncated(request.mApplicationData, modified, newSize);

    // Try and reduce the file's physical size.
    if (mBuffer->truncate(newSize))
        updateSize(newSize);
    else
        FSWarningF("Couldn't reduce file size: %s", toString(mInfo->id()).c_str());

    // Return lock and modification time to our caller.
    return std::make_pair(std::move(lock), modified);
}

void FileContext::updateAccessTime(std::int64_t accessed, Transaction& transaction)
{
    auto query = transaction.query(mService.queries().mSetFileAccessTime);

    query.param(":accessed").set(accessed);
    query.param(":id").set(mInfo->id());

    query.execute();
}

void FileContext::bumpAccessTime(std::chrono::seconds interval)
{
    const auto currentAccessTime = now();
    const auto previousAccessTime = mInfo->accessed();

    if (currentAccessTime - previousAccessTime < interval.count())
        return;

    withTransaction(mService.database(),
                    std::bind(&FileContext::updateAccessTime,
                              this,
                              mInfo->accessed(currentAccessTime),
                              std::placeholders::_1));
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

void FileContext::touchNormalReadActivity()
{
    mLastNormalReadActivityTime.store(std::chrono::steady_clock::now().time_since_epoch());
}

FileRange FileContext::updateRangeInDatabase(FileRange range,
                                             Transaction& transaction,
                                             std::optional<std::int64_t> modified)
try
{
    // Convenience.
    auto& [from, to] = range;

    // Compute initial effective range.
    from = std::min(from, mInfo->size());

    // What ranges did our write touch?
    auto [begin, end] = mOnDisk.find(extend(range, 1));

    // Calculate our effective range.
    do
    {
        // Range has no siblings.
        if (begin == mOnDisk.end())
            break;

        // Range has a sibling.
        from = std::min(begin->mBegin, from);
        to = std::max(begin->mEnd, to);

        // Range has a right sibling.
        if (end != mOnDisk.end())
        {
            // Recompute range's end point.
            to = std::max(std::prev(end)->mEnd, to);
            break;
        }

        // Range may have a right sibling.
        auto candidate = mOnDisk.crbegin();

        // Range doesn't have a right sibling.
        if (candidate == mOnDisk.crend())
            break;

        // Recompute range's end point.
        to = std::max(candidate->mEnd, to);
    }
    while (0);

    // Remove obsolete ranges from the database.
    removeRanges(range, transaction);

    // Add our new range to the database.
    addRange(range, transaction);

    // Update the file's size in the database. When the caller supplies a
    // modification time, fold the access/modification/dirty update into the
    // same UPDATE (both touch the same files row) to save a statement.
    if (modified)
        updateSizeAndTime(*modified, *modified, std::max(mInfo->size(), to), transaction);
    else
        updateSize(std::max(mInfo->size(), to), transaction);

    // Return effective range to our caller.
    return range;
}
catch (...)
{
    auto exception = std::current_exception();

    // Let debuggers know what went wrong.
    FSErrorF("Couldn't update range in database: %s: %s",
             toString(range).c_str(),
             exceptionMessage().c_str());

    // Propagate the original exception.
    std::rethrow_exception(exception);
}

void FileContext::updateRangeInMemory(const FileRange range)
{
    mOnDisk.merge(range);
}

void FileContext::updateSize(std::uint64_t size, Transaction& transaction)
{
    // Query the file's current allocated and reported size on disk.
    auto sizes = mFile->getFileSize();

    // Couldn't retrieve the file's current sizes.
    if (!sizes)
        throw FSErrorF("Couldn't get file sizes: %s", toString(mInfo->id()).c_str());

    // Clarity.
    auto [allocatedSize, reportedSize] = *sizes;

    // Update the file's allocated and reported sizes in memory.
    mInfo->allocatedSize(allocatedSize);
    mInfo->reportedSize(reportedSize);

    // Update the file's sizes in the database.
    auto query = transaction.query(mService.queries().mSetFileSize);

    query.param(":allocated_size").set(allocatedSize);
    query.param(":id").set(mInfo->id());
    query.param(":reported_size").set(reportedSize);
    query.param(":size").set(size);

    query.execute();
}

FileResult FileContext::updateSize(std::uint64_t size)
try
{
    withTransaction(mService.database(),
                    [&](auto& transaction)
                    {
                        updateSize(size, transaction);
                    });

    return FILE_SUCCESS;
}
catch (...)
{
    FSWarningF("Couldn't update size: %s: %s",
               toString(mInfo->id()).c_str(),
               exceptionMessage().c_str());

    return FILE_FAILED;
}

void FileContext::updateSizeAndTime(std::int64_t accessed,
                                    std::int64_t modified,
                                    std::uint64_t size,
                                    Transaction& transaction)
{
    // Query the file's current allocated and reported size on disk.
    auto sizes = mFile->getFileSize();

    // Couldn't retrieve the file's current sizes.
    if (!sizes)
        throw FSErrorF("Couldn't get file sizes: %s", toString(mInfo->id()).c_str());

    // Clarity.
    auto [allocatedSize, reportedSize] = *sizes;

    // Update the file's allocated and reported sizes in memory.
    mInfo->allocatedSize(allocatedSize);
    mInfo->reportedSize(reportedSize);

    // Update the file's sizes and access/modification times in a single UPDATE.
    auto query = transaction.query(mService.queries().mSetFileSizeAndModificationTime);

    query.param(":accessed").set(accessed);
    query.param(":allocated_size").set(allocatedSize);
    query.param(":id").set(mInfo->id());
    query.param(":modified").set(modified);
    query.param(":reported_size").set(reportedSize);
    query.param(":size").set(size);

    query.execute();
}

// Note: normal read hydration is highly sensitive to alphaLow.
// The value below was determined experimentally.
FileContext::FileContext(Activity activity,
                         FileAccessPtr file,
                         FileInfoContextPtr info,
                         std::optional<NodeKeyData> keyData,
                         FileRangeSet ranges,
                         FileServiceContext& service):
    enable_shared_from_this(),
    mInstanceLogger("FileContext", *this, logger()),
    mActivity(std::move(activity)),
    mBuffer(std::make_shared<FileBuffer>(*file)),
    mDownloadCategories{/*mLarge*/ {true, 256, 256},
                        /*mNormal*/ {false, 2, 200}, /*very slow to down, fast to up*/
                        /*mSmall*/ {true, 256, 256}},
    mInfo(std::move(info)),
    mFetchContext(),
    mFetchContextLock(),
    mFile(std::move(file)),
    mFlushContext(),
    mFlushContextLock(),
    mKeyData(std::move(keyData)),
    mLock(),
    mOnDisk(std::move(ranges)),
    mPendingReadRequests(),
    mPinTask(),
    mPinTaskLock(),
    mReadWriteState(),
    mReclaimContext(),
    mReclaimContextLock(),
    mRequests(),
    mRequestsLock(),
    mService(service),
    mDownloadMonitor(),
    mMonitor()
{
    // Sanity: we should never open an unmanaged file.
    assert(mInfo->managed());

    // Estimated bitrate based on the file's duration, if any.
    const auto fileBitrate = mInfo->bitrate().value_or(0);

    // Estimated bitrate provided by the service.
    const auto serviceBitrate = serviceOptions().mEstimatedDownloadBitrate;

    // Prime normal averager based on the service's bitrate.
    mDownloadCategories.mNormal.mAverager.update(mService.speed());

    // Prime streaming averagers based on the largest of the two bitrates above.
    const auto bitrate = std::max(fileBitrate, serviceBitrate);

    mDownloadCategories.mSmall.mAverager.update(bitrate);
    mDownloadCategories.mLarge.mAverager.update(bitrate);
}

FileContext::~FileContext()
{
    // Cancel any downloads or pending requests.
    cancel();

    // Remove ourselves from our service's index.
    mService.removeFromIndex(FileContextBadge(), mInfo->id());
}

void FileContext::append(FileContextPtr, FileAppendRequest request)
{
    executeOrQueue(std::move(request));
}

FileRangeVector FileContext::downloading() const
{
    // Convenience.
    const DownloadCategory* categories[] = {&mDownloadCategories.mLarge,
                                            &mDownloadCategories.mNormal,
                                            &mDownloadCategories.mSmall}; // categories

    FileRangeVector downloading;

    // So we have exclusive access to the range trees.
    std::lock_guard guard(mLock);

    // What ranges are being downloaded?
    for (const auto* category: categories)
    {
        for (const auto& [range, _]: category->mInProgress)
            downloading.emplace_back(range);
    }

    // Let our caller know which ranges are being downloaded.
    return downloading;
}

AutoFileHandle FileContext::dupFileDescriptor()
{
    assert(mFile);

    return mFile->dupFileDescriptor();
}

void FileContext::fetch(FileContextPtr, FileFetchRequest request)
{
    executeOrQueue(std::move(request));
}

void FileContext::fetchBarrier(FileFetchBarrierCallback callback, FileContextPtr)
{
    // Sanity.
    assert(callback);

    // Execute callback when all downloads have completed.
    mDownloadMonitor.whenIdle(std::move(callback));
}

void FileContext::flush(FileContextPtr, FileFlushRequest request)
{
    executeOrQueue(std::move(request));
}

FileInfo FileContext::info() const
{
    return FileInfo(FileContextBadge(), mInfo);
}

void FileContext::pinUntil(FileContextPtr context, steady_clock::time_point when)
{
    // What is the current time?
    auto now = steady_clock::now();

    // Acquire pin task lock.
    std::lock_guard guard(mPinTaskLock);

    // Caller doesn't actually need to pin this context.
    if (now >= when)
        return mPinTask.cancel(), void();

    // Called when we can release our reference on this context.
    auto callback = [context = std::move(context), this](auto& task)
    {
        // Acquire task lock.
        std::lock_guard guard(mPinTaskLock);

        // Another pin task is keeping this context in memory.
        if (mPinTask != task)
            return;

        // Let our context know this task has completed.
        mPinTask.reset();
    }; // callback

    // Queue our callback for later execution.
    mPinTask = mService.executor().execute(std::move(callback), when, false);
}

ServiceOptions FileContext::serviceOptions() const
{
    return mService.serviceOptions();
}

FileRangeVector FileContext::ranges() const
{
    // Make sure no one is messing with our range maps.
    std::lock_guard guard(mLock);

    // Return ranges to our caller.
    return FileRangeVector(mOnDisk.begin(), mOnDisk.end());
}

void FileContext::read(FileContextPtr, FileReadRequest request)
{
    // Stream read request.
    if (std::holds_alternative<StreamReadTag>(request.mReadTag))
        return executeOrQueue(std::move(request));

    // Normal read request.

    // Updates activity time
    touchNormalReadActivity();

    // Wrap the user's callback so we can update our normal read activity time when their request
    // completes.
    auto wrapper = [context = weak_from_this(),
                    callback = std::move(request.mCallback)](FileResultOr<FileReadResult> result)
    {
        if (auto c = context.lock())
            c->touchNormalReadActivity();

        callback(std::move(result));
    };
    FileReadRequest wrappedRequest{std::move(wrapper),
                                   std::move(request.mRange),
                                   std::move(request.mReadTag)};
    executeOrQueue(std::move(wrappedRequest));
}

void FileContext::reclaim(FileReclaimCallback callback, FileContextPtr)
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
    executeOrQueue(FileFlushRequest{std::move(flushed), true});
}

void FileContext::remove(FileContextPtr context,
                         FileStructureUniqueLock& lock,
                         FileRemoveRequest request)
{
    // Convenience.
    auto& callback = request.mCallback;

    // Sanity: callback should never be null.
    assert(callback);

    // Sanity: context should refer to this context instance.
    assert(context.get() == this);

    // Sanity: lock should be acquired.
    assert(lock.owns_lock());

    // Sanity: lock should refer this context's structure mutex.
    assert(lock.mutex() == &mInfo->structureMutex());

    // Make sure unhandled exceptions don't escape from the caller's callback.
    callback = swallow(std::move(callback), "remove");

    // Immediately reject the request if necessary.
    if (auto result = reject(request); result != FILE_SUCCESS)
        return callback(result);

    // File's already been removed.
    if (mInfo->removed())
        return callback(FILE_SUCCESS);

    // Cancel any pending downloads.
    cancel(FileRange(0, mInfo->size()));

    // Cancel any scheduled flushes.
    mService.flusher().cancel(mInfo->id());

    // Convenience.
    const auto handle = mInfo->handle();

    // We only need to remove the file from the service.
    if (handle.isUndef() || request.mServiceOnly)
        return callback(setRemoved());

    // Called when the file's been removed.
    auto removed = [this](auto& activity, auto& callback, auto result) mutable
    {
        auto fileResult = result == API_OK ? setRemoved() : fileResultFromError(result);

        callback(fileResult);

        // Release our activity now that the caller's callback has finished:
        // when the callback is destroyed it may drop the last reference to
        // this context, whose destruction waits until all activities have
        // completed. Nothing may touch this context beyond this point.
        activity = Activity();
    }; // removed

    // Ask the client to remove our file.
    mService.client().remove(
        std::bind(std::move(removed), mMonitor.begin(), std::move(callback), std::placeholders::_1),
        handle);
}

void FileContext::remove(FileContextPtr context, FileRemoveRequest request)
{
    // Cleaner than a lambda.
    class LockCallback
    {
        FileContextPtr mContext;
        FileRemoveRequest mRequest;

    public:
        LockCallback(FileContextPtr context, FileRemoveRequest request):
            mContext(std::move(context)),
            mRequest(std::move(request))
        {}

        void operator()(FileStructureUniqueLock&& lock)
        {
            mContext->remove(mContext, lock, std::move(mRequest));
        }
    };

    // Sanity: callback should never be null.
    assert(request.mCallback);

    // Sanity: context should refer to this file instance.
    assert(context.get() == this);

    // Immediately reject the request if necessary.
    if (auto result = reject(request); result != FILE_SUCCESS)
        return request.mCallback(result);

    // Remove the file after acquiring a lock on this file's structure mutex.
    mInfo->structureMutex().lock(LockCallback(std::move(context), std::move(request)));
}

bool FileContext::removed() const
{
    return mInfo->removed();
}

void FileContext::touch(FileContextPtr, FileTouchRequest request)
{
    executeOrQueue(std::move(request));
}

void FileContext::truncate(FileContextPtr, FileTruncateRequest request)
{
    executeOrQueue(std::move(request));
}

void FileContext::write(FileContextPtr, FileWriteRequest request)
{
    executeOrQueue(std::move(request));
}

FileContext::DownloadCategory::DownloadCategory(bool isStreaming,
                                                uint32_t alphaDown,
                                                uint32_t alphaUp):
    mAverager(alphaDown, alphaUp),
    mInProgress(),
    mIsStreaming(isStreaming)
{}

void FileContext::DownloadContext::completed(Error error)
{
    // Used to keep this context alive until we exit this function.
    DownloadContextPtr self;

    // Acquire lock.
    std::lock_guard guard(mContext.mLock);

    // Download hasn't been replaced.
    if (!replaced())
    {
        // Keep this context alive until we exit this function.
        self = std::move(mIterator->second);

        // Update mRange so it tracks the data we haven't downloaded.
        mRange = {mRange.mEnd, mIterator->first.mEnd};

        // Remove ourselves from our category's map of downloading ranges.
        mCategory.mInProgress.remove(mIterator);
    }

    // Translate SDK error code to a file result.
    auto result = fileResultFromError(error);

    // Fail reads that can't be satisfied.
    mContext.failed(mRange, result);

    // Convenience.
    auto& executor = mContext.mService.executor();

    // Let any waiters know this download has completed.
    for (auto& callback: mCallbacks)
        executor.execute(std::bind(std::move(callback), result), true);
}

auto FileContext::DownloadContext::data(const void* buffer,
                                        std::uint64_t offset,
                                        std::uint64_t length,
                                        const Speeds& speed) -> std::variant<Abort, Continue>
try
{
    // Convenience.
    auto* bytebuffer = static_cast<const std::uint8_t*>(buffer);
    auto& onDisk = mContext.mOnDisk;
    auto options = mContext.mService.serviceOptions();

    // Acquire context lock.
    std::unique_lock lock(mContext.mLock);

    // Reset retry counter.
    mRetries = 0;

    // Assume this isn't a streaming download.
    auto bitrate = speed.mCircularMean * 8;

    // Actually a streaming download.
    if (mCategory.mIsStreaming)
    {
        bitrate = std::max(bitrate, mContext.mInfo->bitrate().value_or(0));
    }

    // Update our download category's average bitrate.
    mCategory.mAverager.update(bitrate);

    // Update service speed if only the range is large enough
    if (mIterator->first.length() > options.mSpeedStatThreshold)
        mContext.mService.speed(bitrate);

    // Download's been replaced.
    if (replaced())
        return Abort();

    // Downloaded data is outside of the file's range.
    if (offset >= mContext.mInfo->size())
        return Abort();

    // Downloaded data is outside of this range.
    if (offset >= mIterator->first.mEnd)
        return Abort();

    // Original range of our write.
    FileRange range(offset, std::min(offset + length, mIterator->first.mEnd));

    // Effective range our write.
    FileRange written(offset, offset);

    // Convenience.
    auto current = gaps(onDisk, range);
    auto end = current.end();

    // Write data to each gap in range.
    for (; current != end; ++current)
    {
        offset = current->mBegin;
        length = current->mEnd - offset;

        // Try and write data to disk.
        auto [count, _] =
            mContext.mBuffer->write(bytebuffer + (offset - range.mBegin), offset, length);

        // Bump end of written range.
        written.mEnd = offset + count;

        // Couldn't write all of the data to disk.
        if (count < length)
            break;
    }

    // We didn't fill any gaps.
    if (written.mBegin == written.mEnd)
    {
        // Because no gaps needed to be filled.
        if (current == end)
        {
            // But act as if we did.
            mRange.mEnd = range.mEnd;

            // We have no more gaps to fill so cancel the download.
            if (gaps(onDisk, mRange.mEnd, mIterator->first.mEnd).empty())
                return Abort();

            // Continue the download.
            return Continue();
        }

        // Because we couldn't write any data to the first gap.
        return Abort();
    }

    // Extend written range if all gaps were filled.
    written = current == end ? range : written;

    // Convenience.
    auto& service = mContext.mService;

    // Update ranges on disk and in memory.
    {
        // Convenience.
        auto& database = service.database();

        // Acquire database lock.
        std::lock_guard databaseLock(database);

        // Begin a transaction so we can safely modify the database.
        auto transaction = database.transaction();

        // Update ranges in the database.
        const auto effectiveRange = mContext.updateRangeInDatabase(written, transaction);

        // Persist database changes.
        transaction.commit();

        // Update ranges in memory.
        mContext.updateRangeInMemory(effectiveRange);
    }

    // Bump end of written range.
    mRange.mEnd = written.mEnd;

    // Dispatch requests we can now satisfy.
    auto satisfied = mContext.completed(written);

    // Couldn't write all of the data to disk.
    if (current != end)
        return Abort();

    // We have no more gaps to fill so cancel the download.
    if (gaps(onDisk, mRange.mEnd, mIterator->first.mEnd).empty())
        return Abort();

    // Don't cancel a download if the FileContext has any pending reads.
    if (!mContext.mPendingReadRequests.empty())
    {
        satisfied = true;
        mContext.touchNormalReadActivity();
    }

    // Don't cancel streaming downloads.
    satisfied = satisfied || mCategory.mIsStreaming;

    // Don't cancel if there is recent normal read activity
    satisfied =
        satisfied || (std::chrono::steady_clock::now() - mContext.lastNormalReadActivityTime() <
                      options.mNormalReadInactivityDelay);

    // Download is useful
    if (satisfied)
        return Continue();

    // Cancel the download as it is not useful
    return Abort();
}

catch (...)
{
    FSWarningF("Couldn't write downloaded data to disk: %s: %s",
               toString(mContext.mInfo->id()).c_str(),
               exceptionMessage().c_str());

    return Abort();
}

auto FileContext::DownloadContext::failed(Error result, int) -> std::variant<Abort, Retry>
{
    // Failure isn't due to a retryable error.
    if (!retryable(result))
        return Abort();

    // The account is over its bandwidth quota. Retrying is pointless: the quota window lasts for
    // hours, far beyond our backoff, and every attempt would be rejected before reaching the
    // storage server. Give up so pending reads fail promptly and this download stops blocking the
    // ones that come after it. Whoever is streaming decides when to try again, and has been told
    // about the overquota through MegaEvent::EVENT_STREAM_OVERQUOTA.
    if (result == API_EOVERQUOTA)
        return Abort();

    // Convenience.
    auto options = mContext.mService.serviceOptions();

    // Or if we've already retried the download too many times.
    if (mRetries >= options.mMaximumRangeRetries)
        return Abort();

    // Retry the download.
    return options.mRangeRetryBackoff * (1 << mRetries++);
}

bool FileContext::DownloadContext::hasPendingReadRequests() const
{
    // Acquire context lock.
    std::lock_guard guard(mContext.mLock);

    // Download's been replaced.
    if (replaced())
        return false;

    // Convenience.
    auto& pending = mContext.mPendingReadRequests;

    // One or more reads may be waiting for this download's data.
    if (auto i = pending.lower_bound(mRange.mEnd); i != pending.end())
        return i->mRange.mBegin < mIterator->first.mEnd;

    // No reads are waiting for this download's data.
    return false;
}

bool FileContext::DownloadContext::replaced() const
{
    return mIterator == mCategory.mInProgress.end();
}

FileContext::DownloadContext::DownloadContext(DownloadCategory& category,
                                              FileContext& context,
                                              FileRangeMap<DownloadContextPtr>::Iterator iterator):
    PartialDownloadCallback(),
    mInstanceLogger("FileContext::DownloadContext", *this, logger()),
    mActivity(context.mDownloadMonitor.begin()),
    mCallbacks(),
    mCategory(category),
    mContext(context),
    mDownload(),
    mIterator(iterator),
    mRange(iterator->first.mBegin, iterator->first.mBegin),
    mRetries(0)
{}

std::uint64_t FileContext::DownloadContext::bitrate() const
{
    // Bitrate should always be larger than zero.
    return std::max<uint64_t>(1, mCategory.mAverager.getValue());
}

void FileContext::DownloadContext::cancel()
{
    // Download's alive so cancel it.
    if (auto download = mDownload)
        download->cancel();
}

std::uint64_t FileContext::DownloadContext::distance(std::uint64_t position) const
{
    // Acquire lock.
    std::lock_guard guard(mContext.mLock);

    // Sanity: Method should only be called on an active download.
    assert(!replaced());

    // Sanity: Position should always be within the download's range.
    assert(mIterator->first.contains(position));

    // Return position's distance from mEnd.
    return std::max(mRange.mEnd, position) - mRange.mEnd;
}

auto FileContext::DownloadContext::download() -> PartialDownloadPtr
{
    // Sanity: Downloads should only be created once.
    assert(!mDownload);

    // Convenience.
    auto& client = mContext.mService.client();
    auto& keyData = mContext.mKeyData;

    auto handle = mContext.mInfo->handle();
    auto offset = mIterator->first.mBegin;
    auto length = mIterator->first.mEnd - offset;

    // Try and create a partial download.
    auto download = keyData ? client.partialDownload(*this, handle, *keyData, length, offset) :
                              client.partialDownload(*this, handle, length, offset);

    // Couldn't create the download.
    if (!download)
        return completed(download.error()), nullptr;

    // Grab download.
    mDownload = std::move(*download);

    // Return the download to our caller.
    return mDownload;
}

std::uint64_t FileContext::DownloadContext::end() const
{
    return mRange.mEnd;
}

void FileContext::DownloadContext::queue(FileFetchCallback callback)
{
    // Queue the callback for later execution.
    mCallbacks.emplace_back(std::move(callback));
}

void FileContext::DownloadContext::range(std::uint64_t begin, std::uint64_t end)
{
    range(FileRange(begin, end));
}

void FileContext::DownloadContext::range(const FileRange& range)
{
    // Sanity: Method should only be called on an active download.
    assert(!replaced());

    // Range hasn't actually changed.
    if (mIterator->first == range)
        return;

    // Sanity: Ranges should never be extended.
    assert(range.length() < mIterator->first.length());

    // Keep ourselves alive.
    auto context = std::move(mIterator->second);

    // Remove ourselves from our category's map of downloading ranges.
    mCategory.mInProgress.remove(mIterator);

    [[maybe_unused]] auto added = false;

    // Add ourselves back into our file's map of downloading ranges.
    std::tie(mIterator, added) = mCategory.mInProgress.tryAdd(range, std::move(context));

    // Sanity: The context should always be placed back into the map.
    assert(added);

    // Ensure end of downloaded data is within our new range.
    mRange.mEnd = std::min(range.mEnd, std::max(range.mBegin, mRange.mEnd));
}

const FileRange& FileContext::DownloadContext::range() const
{
    // Sanity: This method should only be called on an active download.
    assert(!replaced());

    // Return a reference to our download's range.
    return mIterator->first;
}

void FileContext::DownloadContext::replaced([[maybe_unused]] const DownloadContextPtr& self)
{
    // Sanity: This context must be referenced.
    assert(self.get() == this);

    // Sanity: This method should only be called on an active download.
    assert(!replaced());

    // Update mRange so it tracks the data we haven't downloaded.
    mRange = {mRange.mEnd, mIterator->first.mEnd};

    // Remove this download from its map.
    mCategory.mInProgress.remove(mIterator);

    // End iterator is a sentinel that this download has been replaced.
    mIterator = mCategory.mInProgress.end();
}

milliseconds FileContext::DownloadContext::timeUntil(std::uint64_t position) const
{
    // Acquire lock.
    std::lock_guard guard(mContext.mLock);

    // Sanity: Method should only be called for an active download.
    assert(!replaced());

    // Sanity: position should always be within the download's range.
    assert(mIterator->first.contains(position));

    // mEnd has already overtaken position.
    if (mRange.mEnd > position)
        return milliseconds(0);

    // Compute estimated bitrate.
    const auto bitrate = this->bitrate();

    // Sanity: Bitrate should never be zero.
    assert(bitrate);

    // How much data do we need to download before mEnd overtakes position?
    const auto remaining = (position - mRange.mEnd) + 1;

    // Estimate how long it'll take to download remaining data.
    const auto estimated = (remaining * 8000) / bitrate;

    // Return estimated download time to our caller.
    return milliseconds(estimated);
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
        mContext.completed(std::move(request), result, false);
}

FileContext::FetchContext::FetchContext(FileContext& context, FileFetchRequest request):
    mInstanceLogger("FileContext::FetchContext", *this, logger()),
    mActivity(context.mMonitor.begin()),
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
    mContext.executeOrQueue(FileReadRequest{
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

void FileContext::FlushContext::bind(BindCallback& bind,
                                     FlushContextPtr& context,
                                     FileStructureUniqueLock structureLock)
{
    // Sanity: context should reference this instance.
    assert(context.get() == this);

    // Try and determine this file's current location.
    auto location = this->location();

    // Acquire context lock.
    std::unique_lock contextLock(mContext.mFlushContextLock);

    // Couldn't determine the file's current location.
    if (!location)
    {
        // Assume we couldn't get the file's location due to some error.
        auto result = FILE_FAILED;

        // Couldn't get the file's location because it's been removed.
        if (location.error() == FILE_SERVICE_FILE_DOESNT_EXIST)
            result = FILE_REMOVED;

        // Let waiters know why we couldn't upload the file.
        return completed(std::move(context), std::move(contextLock), result);
    }

    // Convenience.
    auto& info = *mContext.mInfo;

    // File's been marked as removed.
    if (info.removed())
        return completed(std::move(context), std::move(contextLock), FILE_REMOVED);

    // Flush has been cancelled.
    if (mRequests.empty())
        return;

    // Binding can't be cancelled.
    mCancelOnWrite = false;

    // Save structure lock for later release.
    mStructureLock = std::move(structureLock);

    // Release context lock.
    contextLock.unlock();

    // So we can use our bound method as a callback.
    BoundCallback bound =
        std::bind(&FlushContext::bound, this, std::move(context), std::placeholders::_1);

    // Bind a name to our file's uploaded data.
    bind(
        [&bound, &info, &location]()
        {
            BindContext context;

            context.mCallback = std::move(bound);
            context.mModified = info.modified();
            context.mName = std::move(location->mName);
            context.mParentHandle = location->mParentHandle;
            context.mPreviousHandle = info.handle();

            return context;
        }());
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
    catch (...)
    {
        // Let debuggers know why the flush failed.
        FSErrorF("Couldn't update file handle: %s: %s",
                 toString(mContext.mInfo->id()).c_str(),
                 exceptionMessage().c_str());

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

    // Latch queued requests.
    auto requests = std::exchange(mRequests, decltype(mRequests)());

    // Latch structure lock.
    auto structureLock = std::exchange(mStructureLock, decltype(mStructureLock)());

    // Release context lock.
    lock.unlock();

    // Cancel any scheduled flushes if this flush was successful.
    if (result == FILE_SUCCESS)
        mContext.mService.flusher().cancel(mContext.mInfo->id());

    // Execute queued requests.
    for (auto& request: requests)
        mContext.completed(std::move(request), result, false);
}

FileServiceResultOr<FileLocation> FileContext::FlushContext::location() const
{
    // Has the file already been uploaded to the cloud?
    auto handle = mContext.mInfo->handle();

    // File hasn't been uploaded to the cloud.
    if (handle.isUndef())
        return mContext.mService.location(mContext.mInfo->id());

    // File's still present in the cloud.
    if (auto info = mContext.mService.client().get(handle))
        return FileLocation{std::move(info->mName), info->mParentHandle};

    // File's no longer present in the cloud.
    return unexpected(FILE_SERVICE_FILE_DOESNT_EXIST);
}

void FileContext::FlushContext::uploaded(FlushContextPtr& context, ErrorOr<UploadResult> result)
{
    // Couldn't upload the file's data.
    if (!result)
        return completed(std::move(context),
                         std::unique_lock(mContext.mFlushContextLock),
                         fileResultFromError(result.error()));

    // Convenience.
    auto& client = mContext.mService.client();
    auto& structureMutex = mContext.mInfo->structureMutex();

    // So we can use our bind method as a lock callback.
    auto locked = std::bind(&FlushContext::bind,
                            this,
                            std::move(*result),
                            std::move(context),
                            std::placeholders::_1);

    // Acquire structure lock and try and bind a name to our uploaded data.
    structureMutex.lock(client.onClientThread(std::move(locked)));
}

FileContext::FlushContext::FlushContext(FileContext& context, FileFlushRequest request):
    mInstanceLogger("FileContext::FlushContext", *this, logger()),
    mActivity(context.mMonitor.begin()),
    mContext(context),
    mRequests(),
    mUpload(),
    mCancelOnWrite(request.mCancelOnWrite)
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

    const auto id = mContext.mInfo->id();

    // Try and determine where this file should be located.
    auto location = this->location();

    // Acquire context lock.
    std::unique_lock lock(mutex);

    // Couldn't determine where the file is located.
    if (!location)
    {
        // Assume we couldn't get the file's location due to some error.
        result = FILE_FAILED;

        // Couldn't get the file's location because it's been removed.
        if (location.error() == FILE_SERVICE_FILE_DOESNT_EXIST)
            result = FILE_REMOVED;

        // Let waiters know why we couldn't upload the file.
        return completed(std::move(context), std::move(lock), result);
    }

    // No requests? Flush must have been cancelled.
    if (mRequests.empty())
        return;

    // Convenience.
    auto& file = *mContext.mFile;
    auto& info = *mContext.mInfo;

    // Make sure the file isn't larger than it should be.
    if (!file.ftruncate(static_cast<m_off_t>(info.size())))
        return completed(std::move(context), std::move(lock), FILE_FAILED);

    // Where is this file's data stored?
    auto path = service.userFilePath(id);

    // Convenience.
    auto& [name, parent] = *location;

    // Instantiate an upload.
    mUpload = service.client().upload(path, name, parent, path);

    // So we can use our uploaded method as a callback.
    UploadCallback callback =
        std::bind(&FlushContext::uploaded, this, std::move(context), std::placeholders::_1);

    // Begin the upload.
    mUpload->begin(std::move(callback));
}

template<typename Lock>
void FileContext::FlushContext::cancel(FlushContextPtr context, Lock&& lock, bool dueToWrite)
{
    assert(context);
    assert(lock.mutex() == &context->mContext.mFlushContextLock);
    assert(lock.owns_lock());

    // Flush can't be cancelled by a write.
    if (dueToWrite && !context->mCancelOnWrite)
        return;

    auto upload = std::exchange(context->mUpload, nullptr);

    // No upload's in progress.
    if (!upload)
        return context->completed(context, std::move(lock), FILE_CANCELLED);

    // Release the lock.
    lock.unlock();

    // Cancel the upload.
    upload->cancel();
}

void FileContext::FlushContext::queue(FileFlushRequest request)
{
    // Acquire flush context lock.
    std::lock_guard guard(mContext.mFlushContextLock);

    // This flush can only be cancelled by a write if:
    // - Each flush queued on this context can be cancelled by a write.
    // - This file's data isn't being bound to a name.
    mCancelOnWrite = mCancelOnWrite && request.mCancelOnWrite;

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

    // Convenience.
    auto& executor = mContext.mService.executor();

    // Execute queued callbacks.
    for (auto& callback: callbacks)
        executor.execute(std::bind(std::move(callback), result), true);
}

FileContext::ReclaimContext::ReclaimContext(FileContext& context):
    mInstanceLogger("FileContext::ReclaimContext", *this, logger()),
    mActivity(context.mMonitor.begin()),
    mAllocatedSize(context.mInfo->allocatedSize()),
    mCallbacks(),
    mContext(context)
{}

template<typename Lock>
void FileContext::ReclaimContext::cancel(ReclaimContextPtr& context, Lock&& lock)
{
    // Sanity.
    assert(context);
    assert(lock.mutex() == &context->mContext.mReclaimContextLock);
    assert(lock.owns_lock());

    context->completed(context, std::forward<Lock>(lock), unexpected(FILE_CANCELLED));
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
    //
    // The reclaim context lock must be held until the request is queued: it's
    // what keeps this queueing ordered against FileContext::cancel(). If the
    // lock were released first, cancel() could steal our callbacks and latch
    // the request queue in between, and a request queued after that latch is
    // never executed or cancelled. Its callback would then pin this context
    // (and its activity) forever, deadlocking ~FileContext when it waits for
    // all activity to complete.
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

TruncateOnException::TruncateOnException(FileBuffer& file, std::uint64_t size):
    mFile(file),
    mSize(size)
{}

TruncateOnException::~TruncateOnException()
{
    // Try and truncate the file to size.
    if (std::uncaught_exceptions() > 0)
        mFile.truncate(mSize);
}

template<typename Request>
auto tag(const Request&) -> std::enable_if_t<IsFileRequestV<Request>, typename Request::Type>
{
    return typename Request::Type();
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
        catch (...)
        {
            // Let debuggers know what went wrong.
            FSErrorF("User %s callback threw an exception: %s", name, exceptionMessage().c_str());
        }
    };
}

} // file_service
} // mega
