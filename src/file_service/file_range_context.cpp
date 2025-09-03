#include <mega/common/client.h>
#include <mega/common/expected.h>
#include <mega/common/partial_download.h>
#include <mega/file_service/buffer.h>
#include <mega/file_service/displaced_buffer.h>
#include <mega/file_service/file_range.h>
#include <mega/file_service/file_range_context.h>
#include <mega/file_service/file_range_context_manager.h>
#include <mega/file_service/file_read_request.h>
#include <mega/file_service/file_result.h>
#include <mega/file_service/file_service_options.h>
#include <mega/types.h>

#include <cassert>

namespace mega
{
namespace file_service
{

using namespace common;

constexpr std::uint64_t MinimumLength = 1u << 18;

// Check if result is a retryable error.
static bool retryable(const Error& result);

template<typename Lock>
void FileRangeContext::completed([[maybe_unused]] Lock&& lock, Error result)
{
    // Sanity.
    assert(lock.owns_lock());
    assert(lock.mutex() == &mManager.mutex());

    // Convenience.
    FileRange range(mIterator->first.mBegin, mEnd);

    // Let the manager know this download has completed.
    mManager.completed(*mBuffer, mIterator, range);

    // Complete as many requests as we can.
    dispatch(range.mBegin, 1);

    // Translate SDK result.
    auto result_ = fileResultFromError(result);

    // Download didn't complete successfully.
    if (result_ != FILE_SUCCESS)
    {
        // Fail any remaining requests.
        for (auto i = mRequests.begin(); i != mRequests.end();)
        {
            // Convenience.
            auto& request = const_cast<FileReadRequest&>(*i);

            // Fail the request.
            mManager.failed(std::move(request), result_);

            // Remove the request from our set.
            i = mRequests.erase(i);
        }
    }

    // Let any waiters know this range's download has completed.
    for (auto& callback: mCallbacks)
        mManager.execute(std::bind(std::move(callback), result_));
}

void FileRangeContext::completed(Error result)
{
    // Get a reference to our context.
    //
    // We're doing this here for two reasons:
    //
    // 1. We want to make sure this instance is kept alive until we've
    //    finished processing this donwload's completion.
    //
    // 2. We want to make sure that the lock we acquire immediately below is
    //    released before this instance itself is destroyed.
    [[maybe_unused]] auto context = std::move(mIterator->second);

    // Complete the download.
    completed(mManager.lock(), result);
}

auto FileRangeContext::data(const void* buffer, std::uint64_t, std::uint64_t length)
    -> std::variant<Abort, Continue>
{
    // Convenience.
    auto offset = mEnd - mIterator->first.mBegin;

    // Try and write data to our buffer.
    auto [count, success] = mBuffer->write(buffer, offset, length);

    // Lock our manager.
    auto lock = mManager.lock();

    // Bump our buffer iterator.
    mEnd += count;

    // Couldn't write all of the data to our buffer.
    if (!success)
        return Abort();

    // Don't dispatch any requests here if this is the last piece of the
    // file. Instead, dispatch them when the download is completed.
    //
    // This is necessary to stabilize the integration tests as they expect
    // all necessary processing to have completed by the time any final read
    // callbacks have been executed.
    if (mEnd == mIterator->first.mEnd)
        return Continue();

    // Dispatch what requests we can.
    dispatch(mIterator->first.mBegin, MinimumLength);

    // Let the caller know the download should continue.
    return Continue();
}

void FileRangeContext::dispatch(const std::uint64_t begin, std::uint64_t minimumLength)
{
    // What requests might we be able to satisfy?
    auto i = mRequests.begin();
    auto j = mRequests.upper_bound(mEnd);

    // Dispatch as many requests as we can.
    while (i != j)
    {
        // Copying the iterator keeps logic clean.
        auto k = i++;

        // Evil but necessary.
        auto& request = const_cast<FileReadRequest&>(*k);

        // Can't dispatch this request.
        if (!dispatchable(request, minimumLength))
            continue;

        // Convenience.
        auto& range = request.mRange;

        // Tweak the request.
        range.mEnd = std::min(mEnd, range.mEnd);

        // Create a suitably displaced buffer.
        auto buffer = mBuffer;

        if (auto displacement = range.mBegin - begin)
            buffer = displace(std::move(buffer), displacement);

        // Dispatch the request.
        mManager.completed(std::move(buffer), std::move(request));

        // Remove the request from our set.
        mRequests.erase(k);
    }
}

bool FileRangeContext::dispatchable(const FileReadRequest& request,
                                    std::uint64_t minimumLength) const
{
    // Convenience.
    auto& [m, n] = request.mRange;

    // Request is dispatchable if:
    // - We have enough data to fully satisfy the read.
    // - We have enough data to provide minimumLength bytes of data.
    return n <= mEnd || mEnd - std::min(m, mEnd) >= minimumLength;
}

auto FileRangeContext::failed(Error result, int retries) -> std::variant<Abort, Retry>
{
    // Failure isn't due to a retryable error.
    if (!retryable(result))
        return Abort();

    // Convenience.
    auto options = mManager.options();

    // Or if we've already retried the download too many times.
    if (static_cast<std::uint64_t>(retries) >= options.mMaximumRangeRetries)
        return Abort();

    // Retry the download.
    return options.mRangeRetryBackoff;
}

FileRangeContext::FileRangeContext(Activity activity,
                                   FileRangeContextPtrMap::Iterator iterator,
                                   FileRangeContextManager& manager):
    PartialDownloadCallback(),
    mActivity(std::move(activity)),
    mBuffer(),
    mCallbacks(),
    mDownload(),
    mEnd(iterator->first.mBegin),
    mIterator(iterator),
    mManager(manager),
    mRequests()
{}

FileRangeContext::~FileRangeContext()
{
    // No requests should be queued at this point.
    assert(mRequests.empty());
}

void FileRangeContext::cancel()
{
    // Download's alive so cancel it.
    if (auto download = mDownload)
        download->cancel();
}

auto FileRangeContext::download(Client& client, FileAccess& file, NodeHandle handle)
    -> PartialDownloadPtr
{
    // Sanity.
    assert(!mBuffer);
    assert(!mDownload);

    // Convenience.
    auto offset = mIterator->first.mBegin;
    auto length = mIterator->first.mEnd - offset;

    // Create a buffer for this range's data.
    mBuffer = Buffer::create(file, offset, length);

    // Try and create a partial download.
    auto download = client.partialDownload(*this, handle, offset, length);

    // Couldn't create the download.
    if (!download)
        return completed(download.error()), nullptr;

    // Grab download.
    mDownload = std::move(*download);

    // Return the download to our caller.
    return mDownload;
}

void FileRangeContext::queue(FileFetchCallback callback)
{
    // Queue the callback for later execution.
    mCallbacks.emplace_back(std::move(callback));
}

void FileRangeContext::queue(FileReadRequest request)
{
    // Request isn't dispatchable so queue it for later execution.
    if (!dispatchable(request, MinimumLength))
        return mRequests.emplace(std::move(request)), void();

    // Assume the request requires no displacement.
    auto buffer = mBuffer;

    // What is the request's displacement?
    auto displacement = request.mRange.mBegin - mIterator->first.mBegin;

    // Buffer needs to be displaced.
    if (displacement)
        buffer = displace(std::move(buffer), displacement);

    // Dispatch the request.
    mManager.completed(std::move(buffer), std::move(request));
}

bool retryable(const Error& result)
{
    // Client's being torn down or the download has been cancelled.
    if (result == API_EINCOMPLETE)
        return false;

    // File's been taken down because it breached our terms and conditions.
    if (result == API_ETOOMANY && result.hasExtraInfo())
        return false;

    // Retry all other failures.
    return true;
}

} // file_service
} // mega
