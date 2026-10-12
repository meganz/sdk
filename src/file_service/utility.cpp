#include <mega/file_service/file.h>
#include <mega/file_service/file_result.h>
#include <mega/file_service/file_result_or.h>
#include <mega/file_service/file_stream_result.h>
#include <mega/file_service/read_tag.h>
#include <mega/file_service/source.h>
#include <mega/file_service/utility.h>

#include <cassert>
#include <memory>
#include <vector>

namespace mega
{
namespace file_service
{

class StreamContext;

using StreamContextPtr = std::shared_ptr<StreamContext>;

class StreamContext
{
    // Called when the user wants to stream more file data.
    void onContinue(StreamContextPtr& context,
                    std::uint64_t consumed,
                    std::uint64_t displacement,
                    std::uint64_t length,
                    std::uint64_t offset);

    // Called when we've received file data.
    void onData(StreamContextPtr& context,
                std::uint64_t length,
                std::uint64_t offset,
                FileResultOr<FileReadResult> result);

    // Where we will temporarily store file data.
    std::vector<char> mBuffer;

    // Who should we call with the file's data?
    FileStreamDataCallback mCallback;

    // What file are we streaming?
    File mFile;

public:
    StreamContext(FileStreamDataCallback callback, File file);

    // Start streaming file data.
    void stream(StreamContextPtr context,
                std::uint64_t offset,
                std::uint64_t length,
                bool isJumpCandidate);
}; // StreamContext

void stream(FileStreamDataCallback callback, File file, std::uint64_t offset, std::uint64_t length)
{
    // Sanity: callback should never be null.
    assert(callback);

    // Instantiate streaming context.
    auto context = std::make_shared<StreamContext>(std::move(callback), std::move(file));

    // Start streaming data.
    context->stream(context, offset, length, true);
}

void StreamContext::onContinue(StreamContextPtr& context,
                               std::uint64_t consumed,
                               std::uint64_t displacement,
                               std::uint64_t length,
                               std::uint64_t offset)
{
    // Sanity: context should always refer to this instance.
    assert(context.get() == this);

    // Convenience.
    std::uint64_t size = mBuffer.size();
    std::uint64_t remaining = size - displacement;

    // Make sure consumed is sane.
    consumed = std::min(consumed, remaining);

    // We've exhausted our buffer so try and stream more data.
    if (consumed == remaining)
        return stream(std::move(context), offset + size, length - size, false);

    // Clarity: Keeps callback invocation below simple.
    auto onContinue = std::bind(&StreamContext::onContinue,
                                this,
                                std::move(context),
                                std::placeholders::_1,
                                consumed + displacement,
                                length,
                                offset);

    // Let the user know they've got data to read.
    mCallback(FileStreamResult{std::move(onContinue),
                               mBuffer.data() + displacement + consumed,
                               remaining - consumed});
}

void StreamContext::onData(StreamContextPtr& context,
                           std::uint64_t length,
                           std::uint64_t offset,
                           FileResultOr<FileReadResult> result)
{
    // Sanity: context should always refer to this instance.
    assert(context.get() == this);

    // Couldn't stream file data.
    if (!result)
        return mCallback(unexpected(result.error()));

    // No further data needs to be streamed.
    if (!result->mLength)
        return mCallback(FileStreamResult{});

    // Limit buffer size to 8MiB.
    auto count = std::min<std::uint64_t>(1u << 23, result->mLength);

    // Make sure our buffer's large enough for our data.
    mBuffer.resize(static_cast<std::size_t>(count));

    // Try and transfer the streamed data into our buffer.
    std::tie(count, std::ignore) = result->mSource.read(mBuffer.data(), 0, count);

    // Couldn't read any data from the file.
    if (!count)
        return mCallback(unexpected(FILE_FAILED));

    // Clarity: Keeps callback invocation below simple.
    auto onContinue = std::bind(&StreamContext::onContinue,
                                this,
                                std::move(context),
                                std::placeholders::_1,
                                0,
                                length,
                                offset);

    // Pass streamed data to the user.
    mCallback(FileStreamResult{std::move(onContinue), mBuffer.data(), count});
}

StreamContext::StreamContext(FileStreamDataCallback callback, File file):
    mBuffer(),
    mCallback(std::move(callback)),
    mFile(std::move(file))
{}

void StreamContext::stream(StreamContextPtr context,
                           std::uint64_t offset,
                           std::uint64_t length,
                           bool isJumpCandidate)
{
    // Sanity: context should always refer to this instance.
    assert(context.get() == this);

    // Try and stream some data from the file.
    mFile.read(std::bind(&StreamContext::onData,
                         this,
                         std::move(context),
                         length,
                         offset,
                         std::placeholders::_1),
               offset,
               length,
               StreamReadTag{isJumpCandidate});
}

class StreamContextFD;

using StreamContextFDPtr = std::shared_ptr<StreamContextFD>;

class StreamContextFD
{
    // Called when the user wants to stream more file data.
    void onContinue(StreamContextFDPtr& context,
                    std::uint64_t consumed,
                    std::uint64_t length,
                    std::uint64_t offset);

    // Called when we've received file data.
    void onData(StreamContextFDPtr& context,
                std::uint64_t length,
                std::uint64_t offset,
                FileResultOr<FileReadResult> result);

    // Who should we call with the file's data?
    FileStreamFDCallback mCallback;

    // What file are we streaming?
    File mFile;

public:
    StreamContextFD(FileStreamFDCallback callback, File file);

    // Start streaming file data.
    void stream(StreamContextFDPtr context,
                std::uint64_t offset,
                std::uint64_t length,
                bool isJumpCandidate);
}; // StreamContext

void stream(FileStreamFDCallback callback, File file, std::uint64_t offset, std::uint64_t length)
{
    // Sanity: context should always refer to this instance.
    assert(callback);

    // Instantiate streaming context.
    auto context = std::make_shared<StreamContextFD>(std::move(callback), std::move(file));

    // Start streaming data.
    context->stream(context, offset, length, true);
}

void StreamContextFD::onContinue(StreamContextFDPtr& context,
                                 std::uint64_t consumed,
                                 std::uint64_t length,
                                 std::uint64_t offset)
{
    // Sanity: context should always refer to this instance.
    assert(context.get() == this);

    // Make sure consumed is sane.
    consumed = std::min(consumed, length);

    // Try and stream some more file data.
    stream(std::move(context), offset + consumed, length - consumed, false);
}

void StreamContextFD::onData(StreamContextFDPtr& context,
                             std::uint64_t length,
                             std::uint64_t offset,
                             FileResultOr<FileReadResult> result)
{
    // Sanity: context should always refer to this instance.
    assert(context.get() == this);

    // Couldn't stream file data.
    if (!result)
        return mCallback(unexpected(result.error()));

    // No further data needs to be streamed.
    if (!result->mLength)
        return mCallback(FileStreamFDResult{});

    // Pass streamed data to the user.
    mCallback(FileStreamFDResult{std::bind(&StreamContextFD::onContinue,
                                           this,
                                           std::move(context),
                                           std::placeholders::_1,
                                           length,
                                           offset),
                                 offset,
                                 result->mLength});
}

StreamContextFD::StreamContextFD(FileStreamFDCallback callback, File file):
    mCallback(std::move(callback)),
    mFile(std::move(file))
{}

void StreamContextFD::stream(StreamContextFDPtr context,
                             std::uint64_t offset,
                             std::uint64_t length,
                             bool isJumpCandidate)
{
    // Sanity: context should always refer to this instance.
    assert(context.get() == this);

    // Try and stream some data from the file.
    mFile.read(std::bind(&StreamContextFD::onData,
                         this,
                         std::move(context),
                         length,
                         offset,
                         std::placeholders::_1),
               offset,
               length,
               StreamReadTag{isJumpCandidate});
}
} // file_service
} // mega
