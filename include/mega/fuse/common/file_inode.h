#pragma once

#include <mega/common/error_or_forward.h>
#include <mega/fuse/common/directory_inode_forward.h>
#include <mega/fuse/common/file_extension_db_forward.h>
#include <mega/fuse/common/file_info_forward.h>
#include <mega/fuse/common/file_inode_forward.h>
#include <mega/fuse/common/file_open_flag_forward.h>
#include <mega/fuse/common/inode.h>
#include <mega/fuse/common/mount_forward.h>
#include <mega/fuse/common/ref.h>
#include <mega/fuse/platform/file_context_forward.h>

namespace mega
{
namespace fuse
{

// Represents an individual file.
class FileInode final
  : public Inode
{
    // Removes this file from the inode database.
    void remove(RefBadge badge, InodeDBLock lock) override;

    // Tracks which cloud node we're associated with, if any.
    NodeHandle mHandle;

    // Tracks the local state of this file.
    FileInfoRef mInfo;

    // Serializes access to mInfo.
    mutable std::mutex mInfoLock;

public:
    FileInode(InodeID id,
              const common::NodeInfo& info,
              InodeDB& inodeDB);

    ~FileInode();

    // Is this inode in the file cache?
    bool cached() const override;

    // Retrieve this file's extension.
    FileExtension extension() const;

    // Return a specialized reference to this file.
    FileInodeRef file() override;

    // Set this file's file info.
    void fileInfo(FileInfoRef info);

    // Retrieve a reference to this file's file info.
    FileInfoRef fileInfo() const;

    // Specify which cloud node this file is associatd with.
    void handle(NodeHandle handle);

    // What cloud node, if any, is associated with this file?
    NodeHandle handle() const override;

    // Update this file's cached description.
    void info(const common::NodeInfo& info) override;

    // Retrieve a description of this file.
    InodeInfo info() const override;

    // Specify whether this file has been modified.
    void modified(bool modified);

    // Move (or rename) this file.
    Error move(InodeBadge badge,
               const std::string& name,
               DirectoryInodeRef parent) override;

    // Open this file for reading or writing.
    common::ErrorOr<platform::FileContextPtr> open(Mount& mount,
                                                   FileOpenFlags flags);

    // Replace other with this file.
    Error replace(InodeBadge badge,
                  InodeRef other,
                  const std::string& otherName,
                  DirectoryInodeRef otherParent) override;

    // Truncate the file to the specified size.
    Error truncate(const Mount& mount,
                   m_off_t size,
                   bool dontGrow);

    // Update the file's modification time.
    Error touch(const Mount& mount,
                m_time_t modified);

    // Unlink this file.
    Error unlink(InodeBadge badge) override;

    // Query whether this file has been modified.
    bool wasModified() const;
}; // FileInode

} // fuse
} // mega

