#include <cassert>

#include <mega/common/error_or.h>
#include <mega/fuse/common/client.h>
#include <mega/fuse/common/file_cache.h>
#include <mega/fuse/common/file_info.h>

namespace mega
{
namespace fuse
{

using namespace common;

FileInfo::FileInfo(const FileExtension& extension,
                   const FileAccess& fileAccess,
                   FileCache& fileCache,
                   InodeID id)
  : mExtension(extension)
  , mFileCache(fileCache)
  , mID(id)
  , mLock()
  , mModified(fileAccess.mtime)
  , mReferences(0)
  , mSize(fileAccess.size)
{
}

FileInfo::~FileInfo() = default;

FileExtension FileInfo::extension() const
{
    return mExtension;
}

void FileInfo::get(m_time_t& modified, m_off_t& size) const
{
    std::lock_guard<std::mutex> guard(mLock);

    modified = mModified;
    size = mSize;
}

InodeID FileInfo::id() const
{
    return mID;
}

m_time_t FileInfo::modified() const
{
    std::lock_guard<std::mutex> guard(mLock);

    return mModified;
}

ErrorOr<FileAccessSharedPtr> FileInfo::open(LocalPath& path) const
{
    // Convenience.
    auto& fsAccess = mFileCache.client().fsAccess();

    // Create a new file access object.
    auto fileAccess = fsAccess.newfileaccess(false);

    // Compute the file's path.
    auto path_ = this->path();

    // Couldn't open the file for reading and writing.
    if (!fileAccess->fopen(path_, true, true, FSLogging::logOnError))
        return unexpected(API_EREAD);

    // Make sure the file's attributes have been loaded.
    if (!fileAccess->fstat())
        return unexpected(API_EREAD);

    // Sanity check.
    {
        std::lock_guard<std::mutex> guard(mLock);

        assert(fileAccess->mtime == mModified);
        assert(fileAccess->size == mSize);
    }

    // Pass path to caller.
    path = std::move(path_);

    // Return file access to caller.
    return fileAccess.release();
}

LocalPath FileInfo::path() const
{
    return mFileCache.path(mExtension, mID);
}

void FileInfo::ref(RefBadge)
{
    // Make sure no one else touches our counter.
    FileCacheLock lock(mFileCache);

    // Increment our reference count.
    ++mReferences;

    // Sanity.
    assert(mReferences);
}

void FileInfo::unref(RefBadge)
{
    // Make sure no one else touches our counter.
    FileCacheLock lock(mFileCache);

    // Sanity.
    assert(mReferences);

    // Decrement the counter.
    --mReferences;

    // Remove the info as all references have been dropped.
    if (!mReferences)
        mFileCache.remove(*this, std::move(lock));
}

m_off_t FileInfo::size() const
{
    std::lock_guard<std::mutex> guard(mLock);

    return mSize;
}

void FileInfo::set(m_time_t modified, m_off_t size)
{
    std::lock_guard<std::mutex> guard(mLock);

    mModified = modified;
    mSize = size;
}

void doRef(RefBadge badge, FileInfo& info)
{
    info.ref(badge);
}

void doUnref(RefBadge badge, FileInfo& info)
{
    info.unref(badge);
}

} // fuse
} // mega

