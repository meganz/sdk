#include <mega/fuse/common/file_inode.h>
#include <mega/fuse/common/file_io_context.h>
#include <mega/fuse/common/file_open_flag.h>
#include <mega/fuse/common/mount.h>
#include <mega/fuse/platform/file_context.h>
#include <mega/fuse/platform/mount.h>

namespace mega
{
namespace fuse
{
namespace platform
{

FileContext::FileContext(FileIOContextRef context,
                         fuse::Mount& mount,
                         FileOpenFlags flags)
  : Context(mount)
  , mContext(std::move(context))
  , mFlags(flags)
{
    FUSEDebugF("File Context %s created",
               toString(mContext->id()).c_str());
}

FileContext::~FileContext()
{
    FUSEDebugF("File Context %s destroyed",
               toString(mContext->id()).c_str());
}

FileContext* FileContext::file()
{
    return this;
}

Error FileContext::flush()
{
    return mContext->manualFlush(mount());
}

InodeRef FileContext::inode() const
{
    return mContext->file();
}

ErrorOr<std::string> FileContext::read(m_off_t offset, unsigned int size)
{
    return mContext->read(mount(), offset, size);
}

Error FileContext::touch(m_time_t modified)
{
    return mContext->touch(mount(), modified);
}

Error FileContext::truncate(m_off_t size, bool dontGrow)
{
    if ((mFlags & FOF_WRITABLE))
        return mContext->truncate(mount(), size, dontGrow);

    return API_FUSE_EROFS;
}

ErrorOr<std::size_t> FileContext::write(const void* data,
                                        m_off_t length,
                                        m_off_t offset,
                                        bool noGrow)
{
    // File's only open for reading.
    if (!(mFlags & FOF_WRITABLE))
        return API_FUSE_EBADF;

    // File's open for appending.
    if ((mFlags & FOF_APPEND))
        offset = -1;

    // Perform the write.
    return mContext->write(mount(),
                           data,
                           length,
                           offset,
                           noGrow);
}

} // platform
} // fuse
} // mega

