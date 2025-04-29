#pragma once

#include <cstddef>
#include <mutex>

#include <mega/common/error_or_forward.h>
#include <mega/fuse/common/file_cache_forward.h>
#include <mega/fuse/common/file_extension_db.h>
#include <mega/fuse/common/file_info_forward.h>
#include <mega/fuse/common/file_inode_forward.h>
#include <mega/fuse/common/inode_id.h>

#include <mega/filesystem.h>

namespace mega
{
namespace fuse
{

class FileInfo
{
    // The file's extension.
    FileExtension mExtension;

    // The cache this file belongs to.
    FileCache& mFileCache;

    // The inode that this file represents.
    const InodeID mID;

    // Serializes access to our members.
    mutable std::mutex mLock;

    // When was the file last modified?
    m_time_t mModified;

    // Tracks how many actors are referencing this instance.
    unsigned long mReferences;

    // What is the file's size?
    m_off_t mSize;

public:
    FileInfo(const FileExtension& extension,
             const FileAccess& fileAccess,
             FileCache& fileCache,
             InodeID id);

    ~FileInfo();

    // Retrieve this file's extension.
    FileExtension extension() const;

    // Retrieve this file's current attributes.
    void get(m_time_t& modified, m_off_t& size) const;

    // What inode does this file represent?
    InodeID id() const;

    // Retrieve this file's current modification time.
    m_time_t modified() const;

    // Open this file for writing.
    //
    // If successful, path will be updated to contain the concrete
    // location where this file's content is stored.
    common::ErrorOr<FileAccessSharedPtr> open(LocalPath& path) const;

    // Where is this file's cached content stored?
    LocalPath path() const;

    // Increment this instance's reference counter.
    void ref(RefBadge badge);

    // Retrieve this file's current size.
    m_off_t size() const;

    // Set this file's current attributes.
    void set(m_time_t modified, m_off_t size);

    // Decrements this inode's reference counter.
    //
    // If the instance's reference counter drops to zero,
    // the instance is removed from the file cache.
    void unref(RefBadge badge);
}; // FileInfo

} // fuse
} // mega

