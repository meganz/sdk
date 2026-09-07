#pragma once

#include <mega/common/error_or_forward.h>
#include <mega/file_service/file.h>
#include <mega/fuse/common/file_inode_forward.h>
#include <mega/fuse/common/file_open_flag_forward.h>
#include <mega/fuse/common/ref.h>
#include <mega/fuse/platform/context.h>
#include <mega/fuse/platform/file_context_forward.h>
#include <mega/types.h>

namespace mega
{
namespace fuse
{
namespace platform
{

class FileContext: public Context
{
    // How we actually perform IO operations.
    file_service::File mFile;

    // Controls how we perform IO.
    FileOpenFlags mFlags;

    // What inode is this context associated with?
    FileInodeRef mInode;

public:
    FileContext(fuse::Mount& mount,
                file_service::File file,
                FileOpenFlags flags,
                FileInodeRef inode);

    ~FileContext();

    // Check if this context represents a file.
    FileContext* file() override;

    // Flush any modifications to the cloud.
    Error flush();

    // What inode does this context represent?
    InodeRef inode() const override;

    // Read data from the file.
    common::ErrorOr<std::string> read(m_off_t offset, unsigned int size);

    // Update the file's modification time.
    Error touch(m_time_t modified);

    // Truncate the file to a specific size.
    Error truncate(m_off_t size);

    // Write data to the file.
    common::ErrorOr<std::size_t>
        write(const void* data, m_off_t length, m_off_t offset, bool noGrow);
}; // FileContext

} // platform
} // fuse
} // mega
