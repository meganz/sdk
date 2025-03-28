#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include <mega/fuse/common/directory_inode_forward.h>
#include <mega/fuse/common/directory_inode_results.h>
#include <mega/fuse/common/error_or_forward.h>
#include <mega/fuse/common/file_move_flag_forward.h>
#include <mega/fuse/common/inode.h>
#include <mega/fuse/platform/mount_forward.h>

#include <mega/types.h>

namespace mega
{
namespace fuse
{

class DirectoryInode final
  : public Inode
{
    // Make a new child.
    template<typename Maker>
    ErrorOr<MakeInodeResult> make(Maker&& maker, const std::string& name);

    // Removes this directory from the inode database.
    void remove(RefBadge badge, InodeDBLock lock) override;

public:
    DirectoryInode(InodeID id,
                   const NodeInfo& info,
                   InodeDB& inodeDB);

    ~DirectoryInode();

    // Is this inode in the file cache?
    bool cached() const override;

    // Retrieve a list of this directory's children.
    InodeRefVector children() const;

    // Return a specialized reference to this directory.
    DirectoryInodeRef directory() override;

    // Try and retrieve a reference to the specified child.
    InodeRef get(const std::string& name) const;

    // What cloud node does this directory represent?
    NodeHandle handle() const override;

    // Does this directory contain the specified child?
    bool hasChild(const std::string& name) const;

    // Does this directory contain any children?
    ErrorOr<bool> hasChildren() const;

    // Update this directory's cached description.
    void info(const NodeInfo& info) override;

    // Retrieve a description of this directory.
    InodeInfo info() const override;

    // Make a subdirectory with the specified name.
    ErrorOr<MakeInodeResult> makeDirectory(const platform::Mount& mount,
                                           const std::string& name);

    // Make a file with the specified name.
    ErrorOr<MakeInodeResult> makeFile(const platform::Mount& mount,
                                      const std::string& name);

    // Move a child to a new directory.
    Error move(const std::string& name,
               const std::string& newName,
               DirectoryInodeRef newParent,
               FileMoveFlags flags);

    // Move (or rename) this directory.
    Error move(InodeBadge badge,
               const std::string& name,
               DirectoryInodeRef parent) override;

    // Replace other with this directory.
    Error replace(InodeBadge badge,
                  InodeRef other,
                  const std::string& otherName,
                  DirectoryInodeRef otherParent) override;

    // Unlink a child.
    Error unlink(const std::string& name,
                 std::function<Error(InodeRef)> predicate);

    // Unlink this directory.
    Error unlink(InodeBadge badge) override;
}; // DirectoryInode

} // fuse
} // mega

