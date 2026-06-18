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
#include <mega/file_service/file_context.h>
#include <mega/file_service/file_context_badge.h>
#include <mega/file_service/file_fetch_request.h>
#include <mega/file_service/file_flush_request.h>
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
#include <mega/file_service/file_touch_request.h>
#include <mega/file_service/file_truncate_request.h>
#include <mega/file_service/file_write_request.h>
#include <mega/file_service/logging.h>
#include <mega/file_service/sparse_file_buffer.h>
#include <mega/file_service/type_traits.h>
#include <mega/filesystem.h>

#include <cassert>
#include <chrono>
#include <iterator>

namespace mega
{
namespace file_service
{

using namespace common;

using std::chrono::duration_cast;
using std::chrono::milliseconds;
using std::chrono::seconds;
using std::chrono::steady_clock;

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

    // True if this is a large download.
    bool isLargeDownload() const;

    // True if this download has been replaced.
    bool replaced() const;

    // Logs instance lifetime.
    InstanceLogger<DownloadContext> mInstanceLogger;

    // Keeps our manager alive until we're dead.
    Activity mActivity;

    // Callbacks to execute when this range's fetch completes.
    std::vector<FileFetchCallback> mCallbacks;

    // Which file is responsible for this context?
    FileContext& mContext;

    // The download that's retrieving this file range's data.
    PartialDownloadPtr mDownload;

    // Which of our file's download maps are present in?
    FileRangeMap<DownloadContextPtr>& mDownloading;

    // What is our position in mMap?
    FileRangeMap<DownloadContextPtr>::Iterator mIterator;

    // What range has this context downloaded?
    FileRange mRange;

    // How many times have we failed in a row?
    std::uint64_t mRetries;

public:
    DownloadContext(Activity activity,
                    FileContext& context,
                    FileRangeMap<DownloadContextPtr>& downloading,
                    FileRangeMap<DownloadContextPtr>::Iterator iterator);

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

    // Logs instance lifetime.
    InstanceLogger<FlushContext> mInstanceLogger;

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
    static void cancel(FlushContextPtr context, Lock&& lock);

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

auto FileContext::cancel(const FileRangeMap<DownloadContextPtr>& downloading,
                         const FileRange& range) -> std::list<DownloadContextPtr>
{
    // Do any ranges end after range?
    auto begin = downloading.endsAfter(range.mBegin);

    // No ranges end after range.
    if (begin == downloading.end() || begin->first.mBegin >= range.mEnd)
        return {};

    // A range overlaps the beginning of range.
    if (begin->first.mBegin < range.mBegin && begin->first.mEnd <= range.mEnd)
        begin->second->range(begin->first.mBegin, range.mBegin);

    // Do any ranges end after range?
    auto end = downloading.endsAfter(range.mEnd);

    // A range overlaps the ending of range.
    if (end != downloading.end() && end->first.mBegin < range.mEnd)
        end->second->range(range.mEnd, end->first.mEnd);

    // Recompute begin and end;
    std::tie(begin, end) = downloading.find(range);

    std::list<DownloadContextPtr> downloads;

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

    // Tracks which downloads we need to cancel.
    std::list<DownloadContextPtr> downloads;

    // Figure out which downloads we need to cancel, if any.
    for (auto* map: {&mDownloading.mLarge, &mDownloading.mSmall})
        downloads.splice(downloads.end(), cancel(*map, range));

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

    // Cancel all downloads in progress.
    cancel(FileRange(0, mInfo->size()));

    // Cancel the flush if necessary.
    if (std::unique_lock lock(mFlushContextLock); mFlushContext)
        mFlushContext->cancel(mFlushContext, std::move(lock));

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
    auto wrapper = [=](auto& callback, auto& cookie, auto&, auto& tag, auto&&...) mutable
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

        // See if we can't execute any queued requests.
        context->execute();
    }; // wrapper

    // Convenience.
    auto& executor = mService.executor();

    // Queue the user's request for completion.
    executor.execute(std::bind(std::move(wrapper),
                               swallow(std::move(request.mCallback), request.name()),
                               weak_from_this(),
                               std::placeholders::_1,
                               tag(request),
                               std::forward<Captures>(captures)...),
                     true);
}

void FileContext::completed(const FileRange& range)
{
    dispatch(
        [this](auto end, auto& request)
        {
            // Clamp the read as necessary.
            request.mRange.mEnd = std::min(end, request.mRange.mEnd);

            // Displace the file's buffer as necessary.
            auto buffer = displace(mBuffer, request.mRange.mBegin);

            // Complete the request.
            completed(std::move(buffer), std::move(request));
        },
        range);
}

void FileContext::completed(FileWriteRequest&& request)
{
    // Convenience.
    auto [begin, end] = request.mRange;

    // Complete the user's request.
    completed(std::move(request), FileWriteResult{begin, end - begin});
}

template<typename Dispatcher>
void FileContext::dispatch(Dispatcher&& dispatcher, const FileRange& range)
{
    // What requests might we be able to satisfy?
    auto i = mPendingReadRequests.lower_bound(range.mBegin);
    auto j = mPendingReadRequests.lower_bound(range.mEnd);

    // Dispatch as many requests as we can.
    while (i != j)
    {
        // Dispatch the request.
        dispatcher(range.mEnd, const_cast<FileReadRequest&>(*i));

        // Move to the next request.
        i = mPendingReadRequests.erase(i);
    }
}

std::uint64_t FileContext::downloadBitrate() const
{
    // Bitrate should always be larger than zero.get_speed
    return std::max<uint64_t>(1, mAverageLargeDownloadBitrate.getValue());
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
                             bool,
                             FileReadRequestTag)
{
    assert(lock.mutex() == &mRequestsLock);
    assert(lock.owns_lock());

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

void FileContext::execute(FileAppendRequest& request)
{
    // Convenience.
    auto size = mInfo->size();

    // Assume there's no range for us to grow.
    FileRange range(size, size + request.mLength);

    // Acquire lock.
    std::lock_guard guard(mLock);

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

    // Compute the file's new modification time.
    auto modified = now();

    // Update ranges on disk and in memory.
    updateRanges(range, transaction);

    // Update the file's access and modification time.
    updateAccessAndModificationTimes(modified, modified, transaction);

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
    executeOrQueue(FileReadRequest{std::bind(&FetchContext::operator(),
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
    executeOrQueue(FileFetchRequest{std::bind(&FlushContext::operator(),
                                              mFlushContext.get(),
                                              mFlushContext,
                                              std::placeholders::_1)});
}

void FileContext::execute(FileReadRequest& request)
{
    // Make sure the user's read is within the file.
    request.mRange = clamp(request.mRange, 0, mInfo->size());

    // The user doesn't actually need to read anything.
    if (request.mRange.empty())
        return completed(mBuffer, std::move(request));

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

    // Begins a download, if needed, when exiting this function.
    ScopedDestructor downloader;

    // Acquire lock.
    std::unique_lock lock(mLock);

    // Update the file's access time.
    withTransaction(mService.database(),
                    std::bind(&FileContext::updateAccessTime,
                              this,
                              mInfo->accessed(now()),
                              std::placeholders::_1));

    // Add a download for a given range to the specified map.
    auto addDownload = [&](auto& map, const auto& range)
    {
        // Sanity: Range shouldn't be zero length.
        assert(!range.empty());

        // Where will our download be located in map?
        auto [iterator, added] = map.tryAdd(range, nullptr);

        // Sanity: Adding should always succeed as ranges never overlap.
        assert(added);

        // Instantiate a download context for range.
        auto context =
            std::make_shared<DownloadContext>(mDownloadMonitor.begin(), *this, map, iterator);

        // Make sure our context's linked into map.
        iterator->second = context;

        // Try and create a download for our range.
        auto download = context->download();

        // Couldn't create a download for our range.
        if (!download)
            return;

        // Sanity: We should only begin one download for any given request.
        assert(!downloader);

        // Make sure we begin our download.
        downloader = [download = std::move(download)]()
        {
            download->begin();
        };
    }; // addDownload

    // Complete request if it can be satisfied by data on disk.
    auto completeIfOnDisk = [this](auto& request)
    {
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

        // Queue the request for completion.
        completed(displace(mBuffer, range.mBegin), std::move(request));

        // Let our caller know the request was completed.
        return true;
    }; // completeIfOnDisk

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
        if (!request.mIsJumpCandidate)
            return nullptr;

        // Request isn't a large read.
        if (request.mRange.length() <= immediateThreshold)
            return nullptr;

        // No large range download is in progress.
        if (mDownloading.mLarge.empty())
            return nullptr;

        // Request can be completely satisfied by data on disk.
        if (auto iterator = mOnDisk.contains(request.mRange.mBegin);
            iterator != mOnDisk.end() && iterator->mEnd >= request.mRange.mEnd)
            return nullptr;

        // Get a reference to the large download in progress.
        auto iterator = mDownloading.mLarge.begin();

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
        auto& largeDownloads = mDownloading.mLarge;
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

    // Queue a request for later completion.
    auto queueRequest = [this](auto&& request)
    {
        // We should never queue a request for zero bytes.
        assert(request.mRange.length());

        // Queue the request for later completion.
        mPendingReadRequests.emplace(request);
    }; // queueRequest

    // Sanity: We should only ever have one large download at most.
    assert(mDownloading.mLarge.size() <= 1);

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
        if (!completeIfOnDisk(request))
            queueRequest(std::move(request));

        // A large download is already in progress.
        if (!mDownloading.mLarge.empty())
            return;

        // Extend range to the left if possible.
        // divide 8 from bits to Bytes, divide 1000 from Ms to seconds
        uint64_t backwardBytes = backwardDistanceInMs * downloadBitrate() / 8 / 1000;
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
            addDownload(mDownloading.mLarge, range);

        // Nothing more to do.
        return;
    }

    // Convenience.
    auto range = request.mRange;

    // Sanity.
    assert(range.length() <= immediateThreshold);

    // Request can be satisfied by data already on disk.
    if (completeIfOnDisk(request))
        return;

    // Queue the request for later completion.
    queueRequest(std::move(request));

    // Request can be satisfied by an existing large download.
    if (auto download = downloading(mDownloading.mLarge, range);
        download && download->timeUntil(range.mBegin) <= seconds(1))
        return;

    // Extend the small range's length so our download is worthwhile.
    auto length = std::max(minimumRangeSize, range.length());

    // Make sure the small range doesn't extend beyond the end of the file.
    length = std::min(length, mInfo->size() - range.mBegin);

    // Update the small range's end point.
    range.mEnd = range.mBegin + length;

    // Find all gaps in range that aren't covered by a small download.
    auto current = gaps(mDownloading.mSmall, range);
    auto end = current.end();

    // Range is already covered by small downloads.
    if (current == end)
        return;

    // Bump beginning of range.
    range.mBegin = current->mBegin;
    range.mEnd = current->mEnd;

    // Sanity: range should never be empty.
    assert(!range.empty());

    // Try and add a small download that'll cover request.
    addDownload(mDownloading.mSmall, range);
}

// When this request is executed, any pending downloads will have completed.
void FileContext::execute(FileReclaimRequest& request)
{
    // Make sure no one is messing with our range maps.
    std::lock_guard guard(mLock);

    // All downloads should be completed.
    if (!mDownloading.mLarge.empty() || !mDownloading.mSmall.empty())
        return completed(std::move(request), UINT64_C(0));

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
        return completed(std::move(request), FILE_FAILED);

    // Remove all written ranges from memory.
    mOnDisk.clear();

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
    auto removed = [replaced, this](auto&&, auto&& request, auto result) mutable
    {
        // File was removed from the cloud.
        if (result == API_OK)
            return completed(std::move(request), setRemoved(replaced));

        // Couldn't remove the file from the cloud.
        completed(std::move(request), fileResultFromError(result));
    }; // removed

    // Ask the client to remove our file.
    mService.client().remove(
        std::bind(std::move(removed), mMonitor.begin(), std::move(request), std::placeholders::_1),
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

    // Cancel any downloads contained within range when returning.
    auto canceller = makeScopedDestructor(
        [&]()
        {
            cancel(range);
        }); // canceller

    // Acquire lock.
    std::lock_guard guard(mLock);

    // Disambiguate.
    using file_service::write;

    // Try and write the caller's content to storage.
    std::tie(length, std::ignore) = mBuffer->write(request.mBuffer, range.mBegin, length);

    // Compute actual end of the written range.
    range.mEnd = range.mBegin + length;

    // Couldn't write any content to storage.
    if (!length)
        return completed(std::move(request), FILE_FAILED);

    // Convenience.
    auto& database = mService.database();

    // Acquire database lock.
    UniqueLock databaseLock(database);

    // Start a transaction so we can safely modify the database.
    auto transaction = database.transaction();

    // Update ranges on disk and in memory.
    updateRanges(range, transaction);

    // Compute the file's new modification time.
    auto modified = now();

    // Update the file's access and modification times in the database.
    updateAccessAndModificationTimes(modified, modified, transaction);

    // Persist our changes.
    transaction.commit();

    // Update the file's attributes.
    mInfo->written(modified, range);

    // Queue the user's request for completion.
    completed(std::move(request));
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
        if (!executable(lock, false, mRequests.front()))
            return;

        // Pop the request off the queue.
        auto request = std::move(mRequests.front());

        mRequests.pop_front();

        // Release lock.
        lock.unlock();

        // Execute the request.
        execute(request);
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
        return completed(std::forward<Request>(request), result);

    // Otherwise execute the request.
    execute(request);
}
catch (std::exception& exception)
{
    // Encountered an unhandled exception while executing request.
    FSErrorF("Unable to execute %s request: %s", request.name(), exception.what());

    // Try and fail the request.
    completed(std::move(request), FILE_FAILED);
}
catch (...)
{
    // Encountered an unknown exception while executing request.
    FSErrorF("Unable to execute %s request: %s", request.name(), "Unknown exception");

    // Try and fail the request.
    completed(std::move(request), FILE_FAILED);
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
    if (std::unique_lock lock(mRequestsLock); !executable(lock, true, request))
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

void FileContext::failed(const FileRange& range, FileResult result)
{
    auto current = mPendingReadRequests.lower_bound(range.mBegin);
    auto end = mPendingReadRequests.upper_bound(range.mEnd);

    // Iterate over each pending read within range.
    while (current != end)
    {
        // Keeps logic simple.
        auto iterator = current++;

        // Convenience.
        auto begin = iterator->mRange.mBegin;

        // Request can be satisfied by a large download.
        if (mDownloading.mLarge.contains(begin))
            continue;

        // Request can be satisfied by a small download.
        if (mDownloading.mSmall.contains(begin))
            continue;

        // Dirty but necessary.
        auto& request = const_cast<FileReadRequest&>(*iterator);

        // Fail the request.
        completed(std::move(request), result);

        // Remove read from our set of pending reads.
        mPendingReadRequests.erase(iterator);
    }
}

auto FileContext::grow(std::uint64_t newSize, std::uint64_t oldSize)
    -> std::pair<UniqueLock<Database>, Transaction>
{
    // Make sure we have exclusive access to mOnDisk.
    std::lock_guard guard(mLock);

    // Convenience.
    auto& database = mService.database();

    // Acquire database lock.
    UniqueLock databaseLock(database);

    // So we can safely modify the database.
    auto transaction = database.transaction();

    // Update the file's ranges on disk and in memory.
    updateRanges(FileRange(oldSize, newSize), transaction);

    // Return the transaction to our caller.
    return std::make_pair(std::move(databaseLock), std::move(transaction));
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

    // So we have exclusive access to mOnDisk.
    std::lock_guard guard(mLock);

    // Convenience.
    auto& database = mService.database();

    // Acquire database lock.
    UniqueLock databaseLock(database);

    // So we can safely modify the database.
    auto transaction = database.transaction();

    // Couldn't reduce the file's size.
    if (!mBuffer->truncate(newSize))
        throw FSError1("Couldn't reduce file size");

    // What ranges end after our file's new size?
    auto begin = mOnDisk.endsAfter(newSize);

    // No ranges end after our new size.
    if (begin == mOnDisk.end())
        return std::make_pair(std::move(databaseLock), std::move(transaction));

    // Convenience.
    FileRange range(begin->mBegin, oldSize);

    // Remove affected ranges from the database.
    removeRanges(range, transaction);

    // Update the file's size in the database.
    updateSize(newSize, transaction);

    // Remove affected ranges from memory.
    mOnDisk.remove(begin, mOnDisk.end());

    // First range has been "cut" by the file's new size.
    if (range.mBegin < newSize)
    {
        // Adjust the range's end point.
        range.mEnd = newSize;

        // Readd the range to the database.
        addRange(range, transaction);

        // Readd the range to memory.
        mOnDisk.add(range);
    }

    // Return the transaction to our caller.
    return std::make_pair(std::move(databaseLock), std::move(transaction));
}

void FileContext::updateAccessTime(std::int64_t accessed, Transaction& transaction)
{
    auto query = transaction.query(mService.queries().mSetFileAccessTime);

    query.param(":accessed").set(accessed);
    query.param(":id").set(mInfo->id());

    query.execute();
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

FileRange FileContext::updateRanges(FileRange range, Transaction& transaction)
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

    // Update the file's size in the database.
    updateSize(std::max(mInfo->size(), to), transaction);

    // Remove obsolete ranges from memory.
    mOnDisk.remove(begin, end);

    // Add our new range to memory.
    mOnDisk.add(range);

    // Return effective range to our caller.
    return range;
}

catch (std::runtime_error& exception)
{
    // Let debuggers know what went wrong.
    FSErrorF("Couldn't update range in database: %s: %s",
             toString(range).c_str(),
             exception.what());

    // Propagate the original exception.
    throw;
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
                         std::optional<NodeKeyData> keyData,
                         FileRangeSet ranges,
                         FileServiceContext& service):
    enable_shared_from_this(),
    mInstanceLogger("FileContext", *this, logger()),
    mActivity(std::move(activity)),
    mAverageLargeDownloadBitrate(256),
    mBuffer(std::make_shared<SparseFileBuffer>(*file, *info)),
    mDownloading(),
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
    // Estimated bitrate based on the file's duration, if any.
    const auto fileBitrate = mInfo->bitrate().value_or(0);

    // Estimated bitrate provided by the service.
    const auto serviceBitrate = serviceOptions().mEstimatedDownloadBitrate;

    // Prime average bitrate based on of the two bitrates above.
    mAverageLargeDownloadBitrate.update(std::max(fileBitrate, serviceBitrate));
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

void FileContext::append(FileContextPtr, FileAppendRequest request)
{
    executeOrQueue(std::move(request));
}

FileRangeVector FileContext::downloading() const
{
    FileRangeVector downloading;

    // So we have exclusive access to the range trees.
    std::lock_guard guard(mLock);

    // What large ranges are being downloaded?
    for (const auto& [range, _]: mDownloading.mLarge)
        downloading.emplace_back(range);

    // What small ranges are being downloaded?
    for (const auto& [range, _]: mDownloading.mSmall)
        downloading.emplace_back(range);

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
    executeOrQueue(std::move(request));
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
    executeOrQueue(FileFlushRequest{std::move(flushed)});
}

void FileContext::remove(FileContextPtr, FileRemoveRequest request)
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

        // Remove ourselves from our file's map of downloading ranges.
        mDownloading.remove(mIterator);
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

    // Acquire lock.
    std::lock_guard guard(mContext.mLock);

    // Reset retry counter.
    mRetries = 0;

    // Update our file's average large download bitrate.
    const auto fileBitrate = mContext.mInfo->bitrate().value_or(0);
    if (isLargeDownload())
    {
        mContext.mAverageLargeDownloadBitrate.update(
            std::max(fileBitrate, speed.mCircularMean << 3));
    }

    // Download's been replaced.
    if (replaced())
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
    auto& database = mContext.mService.database();

    // Acquire database lock.
    std::lock_guard databaseLock(database);

    // Begin a transaction so we can safely modify the database.
    auto transaction = database.transaction();

    // Update ranges on disk and in memory.
    mContext.updateRanges(written, transaction);

    // Persist database changes.
    transaction.commit();

    // Bump end of written range.
    mRange.mEnd = written.mEnd;

    // Dispatch requests we can now satisfy.
    mContext.completed(written);

    // Couldn't write all of the data to disk.
    if (current != end)
        return Abort();

    // We have no more gaps to fill so cancel the download.
    if (gaps(onDisk, mRange.mEnd, mIterator->first.mEnd).empty())
        return Abort();

    // Continue the download.
    return Continue();
}

catch (std::runtime_error&)
{
    return Abort();
}

auto FileContext::DownloadContext::failed(Error result, int) -> std::variant<Abort, Retry>
{
    // Failure isn't due to a retryable error.
    if (!retryable(result))
        return Abort();

    // Convenience.
    auto options = mContext.mService.serviceOptions();

    // Or if we've already retried the download too many times.
    if (mRetries >= options.mMaximumRangeRetries)
        return Abort();

    // Retry the download.
    return options.mRangeRetryBackoff * (1 << mRetries++);
}

bool FileContext::DownloadContext::isLargeDownload() const
{
    return &mDownloading == &mContext.mDownloading.mLarge;
}

bool FileContext::DownloadContext::replaced() const
{
    return mIterator == mDownloading.end();
}

FileContext::DownloadContext::DownloadContext(Activity activity,
                                              FileContext& context,
                                              FileRangeMap<DownloadContextPtr>& downloading,
                                              FileRangeMap<DownloadContextPtr>::Iterator iterator):
    PartialDownloadCallback(),
    mInstanceLogger("FileContext::DownloadContext", *this, logger()),
    mActivity(std::move(activity)),
    mCallbacks(),
    mContext(context),
    mDownload(),
    mDownloading(downloading),
    mIterator(iterator),
    mRange(iterator->first.mBegin, iterator->first.mBegin),
    mRetries(0)
{}

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

    // Remove ourselves from our file's map of downloading ranges.
    mDownloading.remove(mIterator);

    [[maybe_unused]] auto added = false;

    // Add ourselves back into our file's map of downloading ranges.
    std::tie(mIterator, added) = mDownloading.tryAdd(range, std::move(context));

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
    mDownloading.remove(mIterator);

    // End iterator is a sentinel that this download has been replaced.
    mIterator = mDownloading.end();
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
    const auto bitrate = mContext.downloadBitrate();

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
        mContext.completed(std::move(request), result);
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
    mInstanceLogger("FileContext::FlushContext", *this, logger()),
    mActivity(context.mMonitor.begin()),
    mContext(context),
    mHandle(context.mInfo->handle()),
    mLocation(context.mInfo->location().value()),
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
    assert(lock.mutex() == &context->mContext.mFlushContextLock);
    assert(lock.owns_lock());

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

    // Release reclaim context lock.
    lock.unlock();

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
        catch (...)
        {
            FSErrorF("User %s callback threw an unknown exception", name);
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
