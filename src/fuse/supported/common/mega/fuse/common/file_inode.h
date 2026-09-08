#pragma once

#include <mega/common/badge_forward.h>
#include <mega/common/error_or_forward.h>
#include <mega/common/transaction_forward.h>
#include <mega/file_service/file_info.h>
#include <mega/file_service/file_structure_mutex.h>
#include <mega/fuse/common/directory_inode_forward.h>
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
class FileInode final: public Inode
{
    // Called to acquire a lock on this inode.
    void doLock() const override;

    // Called to try and acquire a lock on this inode.
    bool doTryLock() const override;

    // Called to release a lock on this inode.
    void doUnlock() const override;

    // Removes this file from the inode database.
    void remove(RefBadge badge, InodeDBLock lock) override;

    // Tracks which cloud node we're associated with, if any.
    NodeHandle mHandle;

    // Tracks the local state of this file.
    file_service::FileInfo mInfo;

    // Lock acquired on mMutex, if any.
    mutable file_service::FileStructureUniqueLock mLock;

    // Serializes access to this inode.
    file_service::FileStructureMutex& mMutex;

public:
    FileInode(const file_service::FileInfo& fileInfo,
              InodeID id,
              const common::NodeInfo& nodeInfo,
              InodeDB& inodeDB);

    ~FileInode();

    // Return a specialized reference to this file.
    FileInodeRef file() override;

    // Retrieve a reference to this file's file info.
    const file_service::FileInfo& fileInfo() const;

    // Update what cloud node is associated with this inode.
    void handle(common::Badge<InodeDB> badge,
                NodeHandle handle,
                common::Transaction& transaction) override;

    void handle(NodeHandle handle) override;

    // What cloud node, if any, is associated with this file?
    NodeHandle handle() const override;

    // Update this file's cached description.
    void info(const common::NodeInfo& info) override;

    // Retrieve a description of this file.
    InodeInfo info() const override;

    // Specify whether this file has been modified.
    void modified(bool modified);

    // Move (or rename) this file.
    Error move(InodeBadge badge, const std::string& name, DirectoryInodeRef parent) override;

    // Open this file for reading or writing.
    common::ErrorOr<platform::FileContextPtr> open(Mount& mount, FileOpenFlags flags);

    // Replace other with this file.
    Error replace(InodeBadge badge,
                  InodeRef other,
                  const std::string& otherName,
                  DirectoryInodeRef otherParent) override;

    // Return the file's structure mutex lock.
    file_service::FileStructureUniqueLock& structureLock() const;

    // Truncate the file to the specified size.
    // When noGrow is true the file is truncated if and only if it is larger
    // than size.
    Error truncate(Mount& mount, m_off_t size, bool noGrow);

    // Update the file's modification time.
    Error touch(Mount& mount, m_time_t modified);

    // Unlink this file.
    Error unlink(InodeBadge badge) override;

    // Query whether this file has been modified.
    bool wasModified() const;
}; // FileInode

} // fuse
} // mega
