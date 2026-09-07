#include <mega/common/utility.h>
#include <mega/file_service/file_id.h>
#include <mega/file_service/file_info.h>
#include <mega/file_service/file_read_result.h>
#include <mega/file_service/file_result.h>
#include <mega/file_service/file_result_or.h>
#include <mega/file_service/file_write_result.h>
#include <mega/file_service/source.h>
#include <mega/fuse/common/file_inode.h>
#include <mega/fuse/common/file_open_flag.h>
#include <mega/fuse/common/logging.h>
#include <mega/fuse/common/mount.h>
#include <mega/fuse/platform/file_context.h>
#include <mega/fuse/platform/mount.h>

#include <functional>

namespace mega
{
namespace fuse
{
namespace platform
{

using namespace common;
using namespace file_service;

class ReadContext
{
    // Where we'll store the data we've read.
    std::string mBuffer;

    // The file we're reading data from.
    file_service::File mFile;

    // How we'll transmit our result to any waiters.
    std::promise<FileResultOr<std::string>> mNotifier;

    // Called when we've received file data.
    void data(std::shared_ptr<ReadContext>& context,
              std::uint64_t offset,
              std::uint64_t remaining,
              FileResultOr<FileReadResult> result)
    {
        // Sanity: context should reference this instance.
        assert(context.get() == this);

        // Couldn't read data from the file.
        if (!result)
        {
            // But we were able to read *some* data.
            if (!mBuffer.empty())
                return mNotifier.set_value(std::move(mBuffer));

            // Couldn't read any data from the file.
            return mNotifier.set_value(unexpected(result.error()));
        }

        // Convenience.
        auto length = result->mLength;

        // We've read all the file could give us.
        if (!length)
            return mNotifier.set_value(std::move(mBuffer));

        // What is the buffer's current size?
        auto size = mBuffer.size();

        // How large does our buffer need to be?
        auto needed = length + size;

        // Allocate necessary buffer space.
        mBuffer.resize(static_cast<std::size_t>(needed));

        // Try and copy the data we've read into our buffer.
        auto [count, _] = result->mSource.read(&mBuffer[size], 0, length);

        // Couldn't copy all of the data into our buffer.
        if (count != length)
        {
            // Reduce buffer size as needed.
            mBuffer.resize(static_cast<std::size_t>(count + size));

            // We were able to copy some data into our buffer.
            if (!mBuffer.empty())
                return mNotifier.set_value(std::move(mBuffer));

            // Couldn't copy any data into our buffer.
            return mNotifier.set_value(unexpected(FILE_FAILED));
        }

        // Read remaining data.
        read(std::move(context), offset + length, remaining - length);
    }

public:
    ReadContext(file_service::File file):
        mBuffer(),
        mFile(std::move(file)),
        mNotifier()
    {}

    // Try and read data from our file.
    void read(std::shared_ptr<ReadContext> context, std::uint64_t offset, std::uint64_t length)
    {
        // Sanity: context should reference this instance.
        assert(context.get() == this);

        // Try and read data from the file.
        mFile.read(std::bind(&ReadContext::data,
                             this,
                             std::move(context),
                             offset,
                             length,
                             std::placeholders::_1),
                   offset,
                   length,
                   NormalReadTag{});
    }

    // Return the waiter associated with this context.
    auto waiter()
    {
        return mNotifier.get_future();
    }
}; // ReadContext

FileContext::FileContext(fuse::Mount& mount,
                         file_service::File file,
                         FileOpenFlags flags,
                         FileInodeRef inode):
    Context(mount),
    mFile(std::move(file)),
    mFlags(flags),
    mInode(std::move(inode))
{
    FUSEDebugF("File Context %s created", toString(mFile.info().id()).c_str());
}

FileContext::~FileContext()
{
    FUSEDebugF("File Context %s destroyed", toString(mFile.info().id()).c_str());
}

FileContext* FileContext::file()
{
    return this;
}

Error FileContext::flush()
{
    // So we can wait for the result of our flush.
    auto notifier = makeSharedPromise<FileResult>();

    // Try and flush the file's changes to the cloud.
    mFile.flush(
        [notifier](auto result)
        {
            // Assume the flush was successful.
            notifier->set_value(result);
        });

    // Couldn't flush the file's changes to the cloud.
    if (notifier->get_future().get() != FILE_SUCCESS)
        return API_EFAILED;

    // File's changes have been flushed to the cloud.
    return API_OK;
}

InodeRef FileContext::inode() const
{
    return mInode;
}

ErrorOr<std::string> FileContext::read(m_off_t offset, unsigned int size)
{
    // Clamp reads to at most 1MiB.
    //
    // libfuse will read in 64KiB chunks by default.
    // WinFSP will read in chunks between 64KiB and 1MiB in size.
    size = std::min(1u << 20, size);

    // Instantiate a context to manage our read.
    auto context = std::make_shared<ReadContext>(mFile);

    // Get our hands on the context's waiter.
    auto waiter = context->waiter();

    // Try and read data from our file.
    context->read(context, static_cast<std::uint64_t>(offset), size);

    // Context will manage its own lifetime.
    context.reset();

    // Retrieve the result of our read.
    auto result = waiter.get();

    // Couldn't read any data from our file.
    if (!result)
        return unexpected(API_EREAD);

    // Return read data to our caller.
    return std::move(*result);
}

Error FileContext::touch(m_time_t modified)
{
    // So we can wait for the result of our touch.
    auto notifier = makeSharedPromise<FileResult>();

    // Try and touch the file.
    mFile.touch(
        [notifier](auto result)
        {
            notifier->set_value(result);
        },
        modified);

    // Couldn't touch the file.
    if (notifier->get_future().get() != FILE_SUCCESS)
        return API_EWRITE;

    // File's been touched.
    return API_OK;
}

Error FileContext::truncate(m_off_t offset)
{
    // Can't truncate a file that's been opened only for reading.
    if (!(mFlags & FOF_WRITABLE))
        return API_FUSE_EROFS;

    // So we can wait for the result of our truncate.
    auto notifier = makeSharedPromise<FileResult>();

    // Try and truncate the file.
    mFile.truncate(
        [notifier](auto result)
        {
            notifier->set_value(result);
        },
        static_cast<std::uint64_t>(offset));

    // Couldn't truncate the file.
    if (notifier->get_future().get() != FILE_SUCCESS)
        return API_EWRITE;

    // File has been truncated.
    return API_OK;
}

ErrorOr<std::size_t> FileContext::write(const void* data, m_off_t length, m_off_t offset, bool)
{
    // File's only open for reading.
    if (!(mFlags & FOF_WRITABLE))
        return API_FUSE_EBADF;

    // File's open for appending.
    if ((mFlags & FOF_APPEND))
    {
        // So we can wait for the result of our append.
        auto notifier = makeSharedPromise<FileResult>();

        // Try and append our data to the file.
        mFile.append(
            data,
            [notifier](auto result)
            {
                notifier->set_value(result);
            },
            static_cast<std::uint64_t>(length));

        // Couldn't append data to our file.
        if (notifier->get_future().get() != FILE_SUCCESS)
            return unexpected(API_EWRITE);

        // Our data's been appended to the file.
        return static_cast<std::size_t>(length);
    }

    // So we can wait for the result of our write.
    auto notifier = makeSharedPromise<FileResultOr<FileWriteResult>>();

    // Try and write the data to our file.
    mFile.write(
        data,
        [notifier](auto result)
        {
            notifier->set_value(result);
        },
        static_cast<std::uint64_t>(offset),
        static_cast<std::uint64_t>(length));

    // Wait for our write to complete.
    auto result = notifier->get_future().get();

    // Couldn't write any data to our file.
    if (!result)
        return unexpected(API_EWRITE);

    // Let our caller know how many bytes of data we wrote to the file.
    return static_cast<std::size_t>(result->mLength);
}

} // platform
} // fuse
} // mega
