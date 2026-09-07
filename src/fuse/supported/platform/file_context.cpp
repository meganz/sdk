#include <mega/common/utility.h>
#include <mega/file_service/application_data.h>
#include <mega/file_service/file_id.h>
#include <mega/file_service/file_info.h>
#include <mega/file_service/file_read_result.h>
#include <mega/file_service/file_result.h>
#include <mega/file_service/file_result_or.h>
#include <mega/file_service/file_truncate_result.h>
#include <mega/file_service/file_write_result.h>
#include <mega/file_service/source.h>
#include <mega/fuse/common/constants.h>
#include <mega/fuse/common/file_inode.h>
#include <mega/fuse/common/file_open_flag.h>
#include <mega/fuse/common/logging.h>
#include <mega/fuse/common/mount.h>
#include <mega/fuse/platform/file_context.h>
#include <mega/fuse/platform/mount.h>
#include <mega/fuse/platform/mount_db.h>

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

    // Who should we call when the read is complete?
    std::function<void(ErrorOr<std::string>)> mCallback;

    // The file we're reading data from.
    file_service::File mFile;

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
                return mCallback(std::move(mBuffer));

            // Couldn't read any data from the file.
            return mCallback(unexpected(API_EREAD));
        }

        // Convenience.
        auto length = result->mLength;

        // We've read all the file could give us.
        if (!length)
            return mCallback(std::move(mBuffer));

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
                return mCallback(std::move(mBuffer));

            // Couldn't copy any data into our buffer.
            return mCallback(unexpected(API_EREAD));
        }

        // Read remaining data.
        read(std::move(context), offset + length, remaining - length);
    }

public:
    ReadContext(std::function<void(ErrorOr<std::string>)> callback, file_service::File file):
        mBuffer(),
        mCallback(std::move(callback)),
        mFile(std::move(file))
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

void FileContext::append(std::function<void(common::ErrorOr<std::size_t>)> callback,
                         const void* data,
                         m_off_t length)
{
    // Sanity: callback should never be null.
    assert(callback);

    // Sanity: data should never be null unless length is zero.
    assert(data || length == 0);

    // File isn't opened for writing.
    if (!(mFlags & FOF_WRITABLE))
        return callback(API_FUSE_EBADF);

    // Called when our append has completed.
    auto appended = [](auto& callback, auto id, auto& mount, auto result)
    {
        // Couldn't append data to the file.
        if (!result)
            return callback(unexpected(API_EWRITE));

        // Invalidate this inode's attributes on all but this mount.
        mount.mMountDB.each(
            [&](auto& mount)
            {
                mount.invalidateAttributes(id);
            },
            mount);

        // Appended data to the file.
        callback(static_cast<std::size_t>(result->mLength));
    }; // appended

    // Try and append data to the end of our file.
    mFile.append({mount().id(), ApplicationID::generate<FUSETag>()},
                 data,
                 std::bind(appended,
                           std::move(callback),
                           mInode->id(),
                           std::ref(mount()),
                           std::placeholders::_1),
                 static_cast<std::uint64_t>(length));
}

common::ErrorOr<std::size_t> FileContext::append(const void* data, m_off_t length)
{
    // So we know when our append has completed.
    auto notifier = makeSharedPromise<ErrorOr<std::size_t>>();

    // Called when our append has completed.
    append(
        [notifier](auto result)
        {
            notifier->set_value(result);
        },
        data,
        length);

    // Let our caller know if the append was successful.
    return notifier->get_future().get();
}

FileContext* FileContext::file()
{
    return this;
}

void FileContext::flush(std::function<void(Error)> callback)
{
    // Try and flush the file's changes to the cloud.
    mFile.flush(
        [callback = std::move(callback)](auto result)
        {
            // Couldn't flush the file to the cloud.
            if (result != FILE_SUCCESS)
                return callback(API_EFAILED);

            // File's been flushed to the cloud.
            callback(API_OK);
        },
        false);
}

Error FileContext::flush()
{
    // So we can wait for the result of our flush.
    auto notifier = makeSharedPromise<Error>();

    // Try and flush the file to the cloud.
    flush(
        [notifier](auto result)
        {
            notifier->set_value(result);
        });

    // Let our caller know whether the file was flushed to the cloud.
    return notifier->get_future().get();
}

InodeRef FileContext::inode() const
{
    return mInode;
}

void FileContext::read(std::function<void(ErrorOr<std::string>)> callback,
                       m_off_t offset,
                       unsigned int size)
{
    // Sanity: callback should always have a target.
    assert(callback);

    // Instantiate a context to manage our read.
    auto context = std::make_shared<ReadContext>(std::move(callback), mFile);

    // Try and read data from our file.
    context->read(context, static_cast<std::uint64_t>(offset), size);
}

ErrorOr<std::string> FileContext::read(m_off_t offset, unsigned int size)
{
    // So we can wait for our read to complete.
    auto notifier = makeSharedPromise<ErrorOr<std::string>>();

    // Try and read data from the file.
    read(
        [notifier](auto result)
        {
            notifier->set_value(std::move(result));
        },
        offset,
        size);

    // Return the read's result to our caller.
    return notifier->get_future().get();
}

void FileContext::touch(std::function<void(Error)> callback, m_time_t modified)
{
    // Called when our touch has completed.
    auto touched = [](auto& callback, auto id, auto& mount, auto result)
    {
        // Couldn't touch the file.
        if (result != FILE_SUCCESS)
            return callback(API_EFAILED);

        // Invalidate the inode's attributes on all but this mount.
        mount.mMountDB.each(
            [&](auto& mount)
            {
                mount.invalidateAttributes(id);
            },
            mount);

        // File's been touched.
        callback(API_OK);
    }; // touched

    // Try and touch the file.
    mFile.touch({mount().id(), ApplicationID::generate<FUSETag>()},
                std::bind(touched,
                          std::move(callback),
                          mInode->id(),
                          std::ref(mount()),
                          std::placeholders::_1),
                modified);
}

Error FileContext::touch(m_time_t modified)
{
    // So we can wait for the result of our touch.
    auto notifier = makeSharedPromise<Error>();

    // Try and touch the file.
    touch(
        [notifier](auto result)
        {
            notifier->set_value(result);
        },
        modified);

    // Let our caller know whether the file was touched.
    return notifier->get_future().get();
}

void FileContext::truncate(std::function<void(Error)> callback, m_off_t size, bool noGrow)
{
    // Can't truncate a file that's been opened only for reading.
    if (!(mFlags & FOF_WRITABLE))
        return callback(API_FUSE_EROFS);

    // Called the file has been truncated.
    auto truncated = [](auto& callback, auto id, auto& mount, auto result)
    {
        // Couldn't truncate the file.
        if (!result)
            return callback(API_EWRITE);

        // Convenience.
        const auto newSize = static_cast<m_off_t>(result->mNewSize);
        const auto oldSize = static_cast<m_off_t>(result->mOldSize);

        // Figure out what region of data needs to be invalidated.
        const auto from = std::min(newSize, oldSize);
        const auto to = std::max(newSize, oldSize);

        // Invalidate the inode's attributes and data in all but this mount.
        mount.mMountDB.each(
            [&](auto& mount)
            {
                mount.invalidateAttributes(id);
                mount.invalidateData(id, from, to);
            },
            mount);

        // File's been truncated.
        callback(API_OK);
    }; // truncated

    // Assume the truncate requires no special flags.
    FileTruncateFlags flags = 0;

    // Caller doesn't want the file's size to grow.
    if (noGrow)
        flags |= FILE_TRUNCATE_FLAG_NO_GROW;

    // Try and truncate the file.
    mFile.truncate({mount().id(), ApplicationID::generate<FUSETag>()},
                   std::bind(truncated,
                             std::move(callback),
                             mInode->id(),
                             std::ref(mount()),
                             std::placeholders::_1),
                   flags,
                   static_cast<std::uint64_t>(size));
}

Error FileContext::truncate(m_off_t size, bool noGrow)
{
    // So we can wait for the result of our truncate.
    auto notifier = makeSharedPromise<Error>();

    // Try and truncate the file.
    truncate(
        [notifier](auto result)
        {
            notifier->set_value(result);
        },
        size,
        noGrow);

    // Let our caller know whether the file was truncated.
    return notifier->get_future().get();
}

void FileContext::write(std::function<void(ErrorOr<std::size_t>)> callback,
                        const void* data,
                        m_off_t length,
                        m_off_t offset,
                        bool noGrow)
{
    // File's only open for reading.
    if (!(mFlags & FOF_WRITABLE))
        return callback(API_FUSE_EBADF);

    // Data should be written to the end of the file.
    if ((mFlags & FOF_APPEND))
        return append(std::move(callback), data, length);

    // Called when our write has completed.
    auto written = [](auto& callback, auto id, auto& mount, auto result)
    {
        // Couldn't write any data to the file.
        if (!result)
            return callback(unexpected(API_EWRITE));

        // Convenience.
        const auto offset = static_cast<m_off_t>(result->mOffset);
        const auto length = static_cast<m_off_t>(result->mLength);

        // Invalidate attributes and data in all but this mount.
        mount.mMountDB.each(
            [&](auto& mount)
            {
                mount.invalidateAttributes(id);
                mount.invalidateData(id, offset, length);
            },
            mount);

        // We wrote some or all data to the file.
        callback(static_cast<std::size_t>(result->mLength));
    }; // written

    // Assume the write requires no special flags.
    FileWriteFlags flags = 0;

    // Convenience.
    auto length_ = static_cast<std::uint64_t>(length);
    auto offset_ = static_cast<std::uint64_t>(offset);

    // Caller doesn't want the file's size to grow.
    if (noGrow)
        flags |= FILE_WRITE_FLAG_NO_GROW;

    // Try and write data to our file.
    mFile.write({mount().id(), ApplicationID::generate<FUSETag>()},
                data,
                std::bind(written,
                          std::move(callback),
                          mInode->id(),
                          std::ref(mount()),
                          std::placeholders::_1),
                flags,
                offset_,
                length_);
}

ErrorOr<std::size_t>
    FileContext::write(const void* data, m_off_t length, m_off_t offset, bool noGrow)
{
    // So we can wait for the result of our write.
    auto notifier = makeSharedPromise<ErrorOr<std::size_t>>();

    // Try and write the data to our file.
    write(
        [notifier](auto result)
        {
            notifier->set_value(result);
        },
        data,
        length,
        offset,
        noGrow);

    // Let our caller know how bytes were written to the file.
    return notifier->get_future().get();
}

} // platform
} // fuse
} // mega
