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
#include <mega/types.h>

#include <cassert>

namespace mega
{
namespace file_service
{

using namespace common;

template<typename Lock>
void FileRangeContext::completed(Lock&& lock, Error result)
{
    // Sanity.
    assert(lock.owns_lock());
    assert(lock.mutex() == &mManager.mutex());

    // Grab a reference to this context.
    [[maybe_unused]] auto context = std::move(mIterator->second);

    // Convenience.
    FileRange range(mIterator->first.mBegin, mEnd);

    // Let the manager know this download has completed.
    mManager.completed(*mBuffer, mIterator, range);

    // Complete as many requests as we can.
    dispatch();

    // Download completed successfully.
    if (result == API_OK)
        return;

    // Translate SDK result.
    auto result_ = fileResultFromError(result);

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

void FileRangeContext::completed(Error result)
{
    // Complete the download.
    completed(mManager.lock(), result);
}

auto FileRangeContext::data(const void* buffer, std::uint64_t, std::uint64_t length)
    -> std::variant<Abort, Continue>
{
    // Convenience.
    auto offset = mEnd - mIterator->first.mBegin;

    // Couldn't write data to our buffer.
    if (!mBuffer->write(buffer, offset, length))
        return Abort();

    // Lock our manager.
    auto lock = mManager.lock();

    // Bump our buffer iterator.
    mEnd += length;

    // Dispatch what requests we can.
    dispatch();

    // Let the caller know the download should continue.
    return Continue();
}

void FileRangeContext::dispatch()
{
    // Convenience.
    auto begin = mIterator->first.mBegin;

    // Necessary as some requests may have displaced reads.
    auto buffer = displace(mBuffer, 0);

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
        if (!dispatchable(request))
            continue;

        // Tweak the request.
        request.mRange.mEnd = mEnd;

        // Set the buffer's displacement for this request.
        buffer->displacement(request.mRange.mBegin - begin);

        // Dispatch the request.
        mManager.completed(buffer, std::move(request));

        // Remove the request from our set.
        mRequests.erase(k);
    }
}

bool FileRangeContext::dispatchable(const FileReadRequest& request) const
{
    // Convenience.
    auto& [m, n] = request.mRange;

    // Minimum number of bytes needed to dispatch a request.
    constexpr std::uint64_t minimumLength = 1u << 18;

    // Request is dispatchable if:
    // - We have enough data to fully satisfy the read.
    // - We have enough data to provide minimumLength bytes of data.
    return n <= mEnd || mEnd - m >= minimumLength;
}

auto FileRangeContext::failed(Error, int) -> std::variant<Abort, Retry>
{
    return Abort();
}

FileRangeContext::FileRangeContext(Activity activity,
                                   FileRangeContextPtrMap::Iterator iterator,
                                   FileRangeContextManager& manager):
    PartialDownloadCallback(),
    mActivity(std::move(activity)),
    mBuffer(),
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
    auto buffer = Buffer::create(file, offset, length);

    // Try and create a partial download.
    auto download = client.partialDownload(*this, handle, offset, length);

    // Couldn't create the download.
    if (!download)
        return completed(download.error()), nullptr;

    // Grab buffer and download.
    mBuffer = std::move(buffer);
    mDownload = std::move(*download);

    // Return the download to our caller.
    return mDownload;
}

void FileRangeContext::queue(FileReadRequest request)
{
    // Request isn't dispatchable so queue it for later execution.
    if (!dispatchable(request))
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

} // file_service
} // mega
