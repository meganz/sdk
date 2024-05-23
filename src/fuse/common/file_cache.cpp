#include <atomic>
#include <cassert>
#include <chrono>
#include <mutex>
#include <stdexcept>
#include <tuple>
#include <utility>

#include <mega/fuse/common/bind_handle.h>
#include <mega/fuse/common/client.h>
#include <mega/fuse/common/file_cache.h>
#include <mega/fuse/common/file_info.h>
#include <mega/fuse/common/file_inode.h>
#include <mega/fuse/common/file_io_context.h>
#include <mega/fuse/common/inode_db.h>
#include <mega/fuse/common/inode_id.h>
#include <mega/fuse/common/logging.h>
#include <mega/fuse/common/ref.h>
#include <mega/fuse/platform/service_context.h>

namespace mega
{
namespace fuse
{

static LocalPath cachePath(const Client& client);

static void ensureCachePathExists(Client& client, const LocalPath& path);

ErrorOr<FileInfoRef> FileCache::create(const FileExtension& extension,
                                       const LocalPath& path,
                                       InodeID id,
                                       FileAccessSharedPtr* fileAccess,
                                       bool create)
{
    // Sanity.
    assert(!path.empty());
    assert(id);

    // Acquire cache lock.
    FileCacheLock guard(*this);

    // More sanity.
    assert(!mInfoByID.count(id));

    // So we can access the filesystem.
    auto fileAccess_ = client().fsAccess().newfileaccess(false);

    // Can't create a file to store the file's content.
    //
    // Open for reading only if create is false.
    // Open for writing only if create is true.
    if (!fileAccess_->fopen(path, !create, create, FSLogging::logOnError))
        return API_EWRITE;

    // Make sure the file's attributes have been loaded.
    if (!fileAccess_->fstat())
        return API_EWRITE;

    // Create and populate a new file description.
    auto info = this->info(extension, *fileAccess_, id);

    // Caller isn't interested in the file access object.
    if (!fileAccess)
        return info;

    // Close file.
    fileAccess_->fclose();

    // Couldn't reopen the file for reading and writing.
    if (!fileAccess_->fopen(path, true, true, FSLogging::logOnError))
        return API_EWRITE;

    // Transfer ownership of file access to caller.
    fileAccess->reset(fileAccess_.release());

    // Return file description to caller.
    return info;
}

FileInfoRef FileCache::info(const FileExtension& extension,
                            const FileAccess& fileAccess,
                            InodeID id)
{
    FileCacheLock guard(*this);

    // Does the cache already know about this inode?
    auto i = mInfoByID.find(id);

    // Cache already knows about this inode.
    if (i != mInfoByID.end())
        return FileInfoRef(i->second.get());

    // Instantiate a file info representing this inode.
    auto info = std::make_unique<FileInfo>(extension, fileAccess, *this, id);

    // Add the info to the index.
    i = mInfoByID.emplace(id, std::move(info)).first;

    // Return a reference to the caller.
    return FileInfoRef(i->second.get());
}

void FileCache::remove(const FileIOContext& context,
                       FileCacheLock lock)
{
    // Get our hands on the context's pointer.
    auto i = mContextByID.find(context.id());

    // Sanity.
    assert(i != mContextByID.end());

    // Latch the context's pointer.
    auto ptr = std::move(i->second);

    // Remove the context from the index.
    mContextByID.erase(i);

    // Let any waiters know that a context's been removed.
    mRemoved.notify_all();

    // Release the lock.
    lock.unlock();

    // Release the context.
    ptr.reset();
}

void FileCache::remove(const FileInfo& info, FileCacheLock lock)
{
    // Get our hands on the info's pointer.
    auto i = mInfoByID.find(info.id());

    // Sanity.
    assert(i != mInfoByID.end());

    // Latch the info's pointer.
    auto ptr = std::move(i->second);

    // Remove the info from the index.
    mInfoByID.erase(i);

    // Release the lock.
    lock.unlock();

    // Release the info.
    ptr.reset();
}

FileCache::FileCache(platform::ServiceContext& context)
  : Lockable()
  , mContextByID()
  , mInfoByID()
  , mRemoved()
  , mCachePath(cachePath(context.client()))
  , mContext(context)
{
    FUSEDebug1("File Cache constructed");

    ensureCachePathExists(client(), mCachePath);
}

FileCache::~FileCache()
{
    assert(mContextByID.empty());
    assert(mInfoByID.empty());

    FUSEDebug1("File Cache destroyed");
}

void FileCache::cancel()
{
    // What contexts currently exist?
    auto contexts = ([this]() {
        // Acquire lock.
        FileCacheLock guard(*this);

        FileIOContextRefVector contexts;

        // Reserve space for the contexts.
        contexts.reserve(mContextByID.size());

        // Latch contexts.
        for (auto& c : mContextByID)
            contexts.emplace_back(c.second.get());

        // Return contexts to caller.
        return contexts;
    })();

    FUSEDebug1("Waiting for file IO contexts to be purged from memory");

    // Iterate over contexts, cancelling any pending flushes.
    while (!contexts.empty())
    {
        contexts.back()->cancel(true);
        contexts.pop_back();
    }

    // True when all of the contexts have been destroyed.
    auto empty = [this]() {
        return mContextByID.empty();
    }; // empty

    FileCacheLock lock(*this);

    // Wait for all of the contexts to be destroyed.
    mRemoved.wait(lock, empty);

    FUSEDebug1("File IO contexts have been purged from memory");
}

Client& FileCache::client() const
{
    return mContext.client();
}

FileIOContextRef FileCache::context(FileInodeRef file,
                                    bool inMemoryOnly) const
{
    assert(file);

    // Convenience.
    auto extension = file->extension();
    auto id = file->id();

    // Determine whether the file was previously modified.
    auto modified = file->wasModified();

    // Acquire lock.
    FileCacheLock guard(*this);

    // Do we already have a context for this file in memory?
    auto c = mContextByID.find(id);

    // Context's already in memory.
    if (c != mContextByID.end())
        return FileIOContextRef(c->second.get());

    // Context's not in memory and we don't want to create it.
    if (inMemoryOnly)
        return FileIOContextRef();

    // Instantiate a context to represent this file.
    auto ptr = std::make_unique<FileIOContext>(const_cast<FileCache&>(*this),
                                          std::move(file),
                                          info(extension, id, true),
                                          modified);

    // Add the context to our index.
    c = mContextByID.emplace(id, std::move(ptr)).first;

    // Return context to caller.
    return FileIOContextRef(c->second.get());
}

ErrorOr<FileInfoRef> FileCache::create(const FileExtension& extension,
                                       const LocalPath& path,
                                       InodeID id,
                                       FileAccessSharedPtr* fileAccess)
{
    // Sanity.
    assert(id);

    // Try and create a new description based on a file in the cache.
    return create(extension, path, id, fileAccess, false);
}

ErrorOr<FileInfoRef> FileCache::create(const FileExtension& extension,
                                       InodeID id,
                                       FileAccessSharedPtr* fileAccess,
                                       LocalPath* filePath)
{
    // Sanity.
    assert(id);

    // Compute to the path to the inode's content.
    auto path = this->path(extension, id);

    // Try and create a new file to contain the inode's content.
    auto result = create(extension, path, id, fileAccess, true);

    // Couldn't create the file.
    if (!result)
        return result;

    // Latch the file's path if requested.
    if (filePath)
        *filePath = std::move(path);

    // Return description to caller.
    return result;
}

void FileCache::current()
{
    // Convenience.
    auto& fsAccess = client().fsAccess();

    auto dirAccess = fsAccess.newdiraccess();
    auto path = mCachePath;

    // Try and open the cache directory for iteration.
    if (!dirAccess->dopen(&path, nullptr, false))
        return;

    LocalPath name;
    nodetype_t type;

    // Iterate over each file the cache.
    while (dirAccess->dnext(path, name, false, &type))
    {
        // Entry isn't a file.
        if (type != FILENODE)
            continue;
        
        // Convert file name to inode ID.
        auto id = InodeID::fromFileName(name.toPath(false));

        // Invalid ID.
        if (!id)
            continue;

        // Inode's still present in the database.
        if (mContext.mInodeDB.exists(id))
            continue;

        ScopedLengthRestore restorer(path);

        path.appendWithSeparator(name, true);

        // Try and remove the file.
        if (!fsAccess.unlinklocal(path))
            FUSEWarningF("Couldn't remove stale cache file: %s",
                         path.toPath(false).c_str());
    }
}

TaskExecutor& FileCache::executor() const
{
    return mContext.mExecutor;
}

void FileCache::flush(const Mount& mount, FileInodeRefVector inodes)
{
    // Flush each inode to the cloud.
    while (!inodes.empty())
    {
        // Grab the inode.
        auto inode = std::move(inodes.back());

        // Pop the stack.
        inodes.pop_back();

        // Get our hands on a suitable IO context.
        auto context = this->context(std::move(inode));

        // Queue the inode for upload.
        context->modified(mount);
    }
}

FileInfoRef FileCache::info(const FileExtension& extension,
                            InodeID id,
                            bool inMemoryOnly) const
{
    FileCacheLock guard(*this);

    // Is info about this inode already in memory?
    auto i = mInfoByID.find(id);

    // Info's already in memory.
    if (i != mInfoByID.end())
        return FileInfoRef(i->second.get());
    
    // Info isn't in memory and we don't want to create it.
    if (inMemoryOnly)
        return FileInfoRef();

    auto fileAccess = client().fsAccess().newfileaccess(false);
    auto filePath = path(extension, id);

    // File doesn't exist or couldn't be accessed.
    if (!fileAccess->fopen(filePath, true, false, FSLogging::eNoLogging))
        return FileInfoRef();

    // File isn't actually a file.
    assert(fileAccess->type == FILENODE);

    // Track the file's info.
    auto info = std::make_unique<FileInfo>(extension,
                                      *fileAccess,
                                      const_cast<FileCache&>(*this),
                                      id);

    // Add info to the index.
    i = mInfoByID.emplace(id, std::move(info)).first;

    // Return info to the caller.
    return FileInfoRef(i->second.get());
}

LocalPath FileCache::path(const FileExtension& extension, InodeID id) const
{
    auto name = LocalPath::fromRelativePath(toFileName(id));
    auto path = mCachePath;

    path.appendWithSeparator(name, false);
    path.append(LocalPath::fromRelativePath(extension));

    return path;
}

void FileCache::remove(const FileExtension& extension, InodeID id)
{
    // Sanity.
    assert(id);

    // Acquire lock.
    FileCacheLock guard(*this);

    // Is the file being manipulated?
    auto c = mContextByID.find(id);

    // Can't remove the file if it's being manipulated.
    if (c != mContextByID.end())
        return c->second->cancel();

    // Can't remove the file if its description is in memory.
    if (mInfoByID.count(id))
        return;

    // Try and remove the file.
    client().fsAccess().unlinklocal(path(extension, id));
}

LocalPath cachePath(const Client& client)
{
    auto name = LocalPath::fromRelativePath("fuse-cache");
    auto path = client.dbRootPath();

    path.appendWithSeparator(name, false);

    return path;
}

void ensureCachePathExists(Client& client, const LocalPath& path)
{
    auto& fsAccess = client.fsAccess();

    // Directory's been created or already existed.
    if (fsAccess.mkdirlocal(path, false, false)
        || fsAccess.target_exists)
        return;

    throw FUSEErrorF("Unable to create file cache directory: %s",
                     path.toPath(false).c_str());
}

} // fuse
} // mega

