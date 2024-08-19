#include <cassert>
#include <chrono>
#include <condition_variable>
#include <future>
#include <utility>

#include <mega/fuse/common/bind_handle.h>
#include <mega/fuse/common/client.h>
#include <mega/fuse/common/error_or.h>
#include <mega/fuse/common/file_cache.h>
#include <mega/fuse/common/file_info.h>
#include <mega/fuse/common/file_inode.h>
#include <mega/fuse/common/file_io_context.h>
#include <mega/fuse/common/inode_db.h>
#include <mega/fuse/common/inode_info.h>
#include <mega/fuse/common/lock.h>
#include <mega/fuse/common/logging.h>
#include <mega/fuse/common/logging.h>
#include <mega/fuse/common/logging.h>
#include <mega/fuse/common/service_flags.h>
#include <mega/fuse/common/task_executor.h>
#include <mega/fuse/common/upload.h>
#include <mega/fuse/platform/mount.h>
#include <mega/fuse/platform/service_context.h>

namespace mega
{
namespace fuse
{

void LockableTraits<FileIOContext>::acquired(const FileIOContext& context)
{
    FUSEDebugF("Acquired lock on file IO context %s",
               toString(context.id()).c_str());
}

void LockableTraits<FileIOContext>::acquiring(const FileIOContext& context)
{
    FUSEDebugF("Acquiring lock on file IO context %s",
               toString(context.id()).c_str());
}

void LockableTraits<FileIOContext>::couldntAcquire(const FileIOContext& context)
{
    FUSEDebugF("Couldn't acquire lock on file IO context %s",
               toString(context.id()).c_str());
}

void LockableTraits<FileIOContext>::released(const FileIOContext& context)
{
    FUSEDebugF("Released lock on file IO context %s",
               toString(context.id()).c_str());
}

void LockableTraits<FileIOContext>::tryAcquire(const FileIOContext& context)
{
    FUSEDebugF("Trying to acquire lock on file IO context %s",
               toString(context.id()).c_str());
}

class FileIOContext::FlushContext
{
    // The client which will perform our upload.
    Client& client() const;

    // The InodeDB that describes the inode we are uploading.
    InodeDB& inodeDB() const;

    // Called when our content has been uploaded to the cloud.
    void uploaded(ErrorOr<UploadResult> result);

    // Wakes any threads waiting for the upload's result.
    mutable std::condition_variable mCV;

    // The context whose content we are uploading.
    FileIOContext& mContext;

    // Serializes access to instance members.
    mutable std::mutex mLock;

    // The actual upload used to send content to the cloud.
    UploadPtr mUpload;

public:
    FlushContext(FileIOContext& context,
                 LocalPath logicalPath);

    // Try and cancel any upload in progress.
    bool cancel();

    // Retrieve the upload's result.
    Error result() const;
}; // FlushContext

ErrorOr<FileAccessSharedPtr> FileIOContext::create()
{
    // Sanity.
    assert(mFileAccess.expired());
    assert(!mFileInfo);
    assert(mFilePath.empty());

    FileAccessSharedPtr fileAccess;

    // Create an empty file in the cache.
    auto info = mFileCache.create(mFile->extension(),
                                  mFile->id(),
                                  &fileAccess,
                                  &mFilePath);

    // Couldn't create the file.
    if (!info)
        return info.error();

    // File's been successfully created.
    mFileInfo = std::move(*info);

    // Inject description into inode.
    mFile->fileInfo(mFileInfo);

    // Inode has a local presence: Make sure it's in the database.
    inodeDB().add(*mFile);
    
    // Make file access object visible to other threads.
    mFileAccess = fileAccess;

    // Return file access object to caller.
    return fileAccess;
}

ErrorOr<FileAccessSharedPtr> FileIOContext::download(const Mount& mount)
{
    // Sanity.
    assert(!mFile->handle().isUndef());
    assert(mFileAccess.expired());
    assert(!mFileInfo);
    assert(mFilePath.empty());

    // Convenience.
    auto& client = mFileCache.client();

    // So we can wait for the client's result.
    std::promise<Error> waiter;

    // Transmits the client's result to our waiter.
    auto wrapper = [&waiter](Error result) {
        waiter.set_value(result);
    }; // wrapper

    // Convenience.
    auto extension = mFile->extension();
    auto id = mFile->id();

    // What node is this file associated with?
    auto handle = mFile->handle();

    // Where should we say we downloaded the file's content?
    auto logicalPath = ([&]() {
        // Compute the file's path relative to the mount.
        auto logicalPath = mFile->path(mount.handle());

        // File's present under the mount.
        if (logicalPath)
            logicalPath->prependWithSeparator(mount.path());

        // Return logical path to caller.
        return logicalPath;
    })();

    // File's no longer present under the mount.
    if (!logicalPath)
        return API_EREAD;

    // Where should we download the file's content?
    auto path = mFileCache.path(extension, id);

    // Ask the client to download our content.
    client.download(std::move(wrapper),
                    handle,
                    std::move(*logicalPath),
                    path);

    // Wait for the file to be downloaded.
    auto result = waiter.get_future().get();

    // Couldn't download the file.
    if (result != API_OK)
        return result;

    FileAccessSharedPtr fileAccess;

    // Try and retrieve this file's description.
    auto info = mFileCache.create(extension, path, id, &fileAccess);

    // Couldn't retrieve this file's description.
    if (!info)
        return info.error();

    // File's been successfully downloaded.
    mFileInfo = std::move(*info);
    mFilePath = std::move(path);

    // Inject description into inode.
    mFile->fileInfo(mFileInfo);

    // Inode has a local presence: Make sure it's in the database.
    inodeDB().add(*mFile);

    // Make file access object visible to other threads.
    mFileAccess = fileAccess;

    // Return file access object to caller.
    return fileAccess;
}

std::chrono::seconds FileIOContext::flushDelay() const
{
    return mFileCache.mContext.serviceFlags().mFlushDelay;
}

InodeDB& FileIOContext::inodeDB() const
{
    return mFileCache.mContext.mInodeDB;
}

Error FileIOContext::manualFlush(FileIOContextSharedLock& contextLock,
                                 std::unique_lock<std::mutex>& flushLock,
                                 NodeHandle mountHandle,
                                 LocalPath mountPath)
{
    // Sanity.
    assert(contextLock.owns_lock());
    assert(flushLock.owns_lock());

    // Content's already been flushed.
    if (!mFlushNeeded)
        return API_OK;

    // Flush context doesn't exist.
    if (!mFlushContext)
    {
        // Compute the inode's path relative to the mount.
        auto filePath = mFile->path(mountHandle);

        // File's no longer below this mount.
        if (!filePath)
            return API_OK;

        // Add the inode's path to the mount's.
        mountPath.appendWithSeparator(*filePath, false);

        // Instantiate a new flush context.
        mFlushContext =
          std::make_shared<FlushContext>(*this, std::move(mountPath));
    }

    // Retrieve flush context.
    auto context = mFlushContext;

    // Release flush lock.
    flushLock.unlock();

    // Wait for the flush to complete.
    auto result = context->result();

    // Reacquire flush lock.
    flushLock.lock();

    // Another thread completed the flush.
    //
    // This is possible because several threads might be in this function at
    // the exact same time. One way that this could happen is if the user
    // executes the sync(...) system call, at the same time, via several
    // different programs.
    //
    // Note mFlushLock above: We acquire it, only as long as necessary to
    // create (or retrieve) the mFlushContext. Once we have that context, we
    // release the lock and then wait for the flush's result.
    //
    // When we get that result, we then try to reacquire the lock but by the
    // time we've actually acquired the lock, another thread - also waiting
    // on the flush's result - may have already completed flush processing.
    //
    // When we want to clear the flush context, we need to make sure that
    // any context that is live, is exactly the same as the one we're trying
    // to release because it's possible that the flush we were waiting has
    // failed and been replaced by another flush that's in progress.
    //
    // That's the rationale for the below :)
    if (mFlushContext != context)
        return result;

    // Clear flush context.
    mFlushContext.reset();

    // We were unable to flush the content to the cloud.
    if (result != API_OK)
        return result;

    // Content's been flushed to the cloud.
    if (mFlushNeeded)
        mFile->modified(false);

    mFlushNeeded = false;

    // Return result to caller.
    return result;
}

void FileIOContext::onPeriodicFlush(FileIOContextRef& context,
                                    m_time_t lastModified,
                                    NodeHandle mountHandle,
                                    LocalPath& mountPath,
                                    const Task& task)
{
    // Sanity.
    assert(context);

    // Acquire context lock.
    FileIOContextSharedLock contextLock(*this);

    // Acquire flush lock.
    std::unique_lock<std::mutex> flushLock(mFlushLock);

    // Flush has been cancelled or is no longer necessary.
    if (task.cancelled() || !mFlushNeeded)
        return;

    // When was our content last modified?
    auto modified = mFileInfo->modified();

    // Content was modified since the flush was queued.
    if (lastModified != modified)
    {
        // Another task will flush the modifications.
        if (mPeriodicFlushTask != task)
            return;

        // Reschedule the flush for later.
        mPeriodicFlushTask = mFileCache.executor().execute(
                               std::bind(&FileIOContext::onPeriodicFlush,
                                         this,
                                         std::move(context),
                                         modified,
                                         mountHandle,
                                         std::move(mountPath),
                                         std::placeholders::_1),
                               flushDelay(),
                               true);

        // We're all done for now.
        return;
    }

    // Perform the flush.
    manualFlush(contextLock,
                flushLock,
                mountHandle,
                std::move(mountPath));

    // Sanity.
    assert(contextLock.owns_lock());
    assert(flushLock.owns_lock());

    // Clear flush task if necessary.
    if (mPeriodicFlushTask == task)
        mPeriodicFlushTask.reset();
}

auto FileIOContext::open(FileIOContextLock& lock,
                         const Mount& mount,
                         m_off_t hint)
  -> ErrorOr<FileAccessSharedPtr>
{
    // Sanity.
    assert(lock.owns_lock());

    // Inode's already open.
    if (auto fileAccess = mFileAccess.lock())
        return fileAccess;

    // Inode has no local file.
    if (!mFileInfo)
    {
        // Don't download a file just to truncate it.
        if (!hint)
            return create();

        // Can't download a file that doesn't exist.
        if (mFile->removed())
            return create();

        // File's content must be downloaded.
        return download(mount);
    }

    // Inode has a local file.
    // 
    // Try and open the file for reading and writing.
    auto fileAccess = mFileInfo->open(mFilePath);

    // Couldn't open the file.
    if (!fileAccess)
        return fileAccess.error();

    // Make file visible to other threads.
    mFileAccess = *fileAccess;

    // Let the caller know the file's open.
    return std::move(*fileAccess);
}

auto FileIOContext::open(FileIOContextSharedLock& shared,
                         const Mount& mount,
                         m_off_t hint)
  -> ErrorOr<FileAccessSharedPtr>
{
    // Sanity.
    assert(shared.owns_lock());

    // Inode's already open.
    if (auto fileAccess = mFileAccess.lock())
        return fileAccess;

    // Release shared lock.
    shared.unlock();

    // Try and download file.
    auto result = ([&]() {
        // Acquire exclusive lock.
        auto exclusive = FileIOContextLock(*this);

        // Try and download the file.
        return open(exclusive, mount, hint);
    })();

    // Reacquire shared lock.
    shared.lock();

    // Return result to caller.
    return result;
}

FileIOContext::FileIOContext(FileCache& cache,
                             FileInodeRef file,
                             FileInfoRef info,
                             bool modified)
  : Lockable()
  , mFile(std::move(file))
  , mFileAccess()
  , mFileCache(cache)
  , mFileInfo(std::move(info))
  , mFilePath()
  , mFlushContext()
  , mFlushLock()
  , mFlushNeeded(modified)
  , mPeriodicFlushTask()
  , mReferences(0u)
{
    assert(mFile);

    FUSEDebugF("File Context constructed: %s",
               toString(mFile->id()).c_str());
}

FileIOContext::~FileIOContext()
{
    FUSEDebugF("File Context destroyed: %s",
               toString(mFile->id()).c_str());
}

void FileIOContext::cancel(bool pendingFlush)
{
    Task flushTask;

    // Cancel upload and latch periodic flush.
    {
        // Acquire flush lock.
        std::lock_guard<std::mutex> guard(mFlushLock);

        // Cancel any upload in progress.
        if (mFlushContext && mFlushContext->cancel())
            mFlushContext.reset();

        // Latch periodic flush.
        if (pendingFlush)
            flushTask = std::move(mPeriodicFlushTask);
    }

    // Cancel periodic flush, if any.
    flushTask.cancel();
}

void FileIOContext::cancel()
{
    auto cancel = [&](FileIOContextRef&, const Task& task) {
        if (!task.cancelled())
            this->cancel(true);
    }; // cancel

    mFileCache.executor().execute(std::bind(std::move(cancel),
                                            FileIOContextRef(this),
                                            std::placeholders::_1),
                                  true);
}

InodeID FileIOContext::id() const
{
    // Sanity.
    assert(mFile);

    return mFile->id();
}

FileInodeRef FileIOContext::file() const
{
    return mFile;
}

Error FileIOContext::manualFlush(const Mount& mount)
{
    // Acquire context lock.
    FileIOContextSharedLock contextLock(*this);

    // Can't flush a file that couldn't have been modified.
    if (!mFileInfo)
        return API_OK;

    // Acquire flush lock.
    std::unique_lock<std::mutex> flushLock(mFlushLock);

    // Try and perform the flush.
    auto result = manualFlush(contextLock,
                              flushLock,
                              mount.handle(),
                              mount.path());

    // Try and cancel any periodic flush.
    auto flushTask = std::move(mPeriodicFlushTask);

    // Release flush lock.
    flushLock.unlock();

    // Cancel periodic flush.
    flushTask.cancel();

    // Return result to caller.
    return result;
}

void FileIOContext::modified(const Mount& mount)
{
    // Acquire flush lock.
    std::lock_guard<std::mutex> guard(mFlushLock);

    // Mark file as having been modified.
    if (!mFlushNeeded)
        mFile->modified(true);

    mFlushNeeded = true;

    // A flush has already been queued.
    if (mPeriodicFlushTask && !mPeriodicFlushTask.completed())
        return;

    // Queue a periodic flush.
    mPeriodicFlushTask = mFileCache.executor().execute(
                           std::bind(&FileIOContext::onPeriodicFlush,
                                     this,
                                     FileIOContextRef(this),
                                     mFileInfo->modified(),
                                     mount.handle(),
                                     mount.path(),
                                     std::placeholders::_1),
                           flushDelay(),
                           true);
}

Error FileIOContext::open(const Mount& mount,
                          bool truncate)
{
    // Truncate existing content.
    if (truncate || mFile->removed())
        return this->truncate(mount, 0, false);

    // Update file's access time.
    mFile->accessed();

    // File's open.
    return API_OK;
}

ErrorOr<std::string> FileIOContext::read(const Mount& mount,
                                         m_off_t offset,
                                         unsigned int size)
{
    assert(offset >= 0);
    assert(size);

    // Update file's access time.
    mFile->accessed();

    // Make sure nothing else is touching this file.
    FileIOContextSharedLock guard(*this);

    // Make sure the file's present and open.
    auto result = open(guard, mount);

    // Couldn't download (or open) the file.
    if (!result)
        return result.error();

    auto fileAccess = std::move(*result);
        
    // Sanity.
    assert(fileAccess);
    assert(mFileInfo);

    // Clamp offset.
    offset = std::min(offset, fileAccess->size);

    // How much data can actually be read?
    auto remaining = fileAccess->size - offset;

    // Clamp size.
    size = std::min(static_cast<unsigned int>(remaining), size);

    // No data available for reading.
    if (!size)
        return std::string();

    std::string buffer;

    // Couldn't read from the file.
    if (!fileAccess->fread(&buffer,
                           size,
                           0,
                           offset,
                           FSLogging::logOnError))
        return API_EREAD;

    // Return result to caller.
    return buffer;
}

void FileIOContext::ref(RefBadge) 
{
    // Make sure nothing else touches our reference count.
    FileCacheLock lock(mFileCache);

    // Increase the number of references.
    ++mReferences;

    // Sanity.
    assert(mReferences);
}

m_off_t FileIOContext::size() const
{
    FileIOContextLock guard(*this);

    if (mFileInfo)
        return mFileInfo->size();

    return mFile->info().mSize;
}

Error FileIOContext::touch(const Mount& mount,
                           m_time_t modified)
{
    // Update file's access time.
    mFile->accessed();

    // Try and cancel any pending flush.
    cancel(false);

    // Make sure no one else is altering this file.
    FileIOContextLock guard(*this);

    // Convenience.
    auto& client = mFileCache.client();

    // File exists only in the cloud.
    if (!mFileInfo)
        return client.touch(mFile->handle(), modified);

    // File's modification time hasn't changed.
    if (mFileInfo->modified() == modified)
        return API_OK;

    // Open the file if necessary.
    auto result = open(guard, mount);

    // Couldn't download or open the file.
    if (!result)
        return result.error();

    auto fileAccess = std::move(*result);

    // Couldn't update the file's modification time.
    if (!client.fsAccess().setmtimelocal(mFilePath, modified))
        return API_EWRITE;

    // Couldn't get the file's info.
    if (!fileAccess->fstat())
        return API_EWRITE;

    // Update the file's info.
    mFileInfo->set(fileAccess->mtime, fileAccess->size);

    // Invalidate the file's attributes.
    mFileCache.mContext.mMountDB.each([&](Mount& mount) {
        mount.invalidateAttributes(mFile->id());
    });

    // Mark the file as having been modified.
    this->modified(mount);

    // File's modification time has been updated.
    return API_OK;
}

Error FileIOContext::truncate(const Mount& mount,
                              m_off_t size,
                              bool dontGrow)
{
    // Sanity.
    assert(size >= 0);

    // Update file's access time.
    mFile->accessed();

    // Try and cancel any pending flush.
    cancel(false);

    // Make sure no one else is altering this file.
    FileIOContextLock lock(*this);

    // Make sure the file's been opened.
    auto result = open(lock, mount, size);

    // Couldn't open the file.
    if (!result)
        return result.error();

    auto fileAccess = std::move(*result);

    // Sanity.
    assert(fileAccess);
    assert(mFileInfo);

    // Caller doesn't want to extend the file's size.
    if (dontGrow)
        size = std::min(fileAccess->size, size);

    // Couldn't truncate the file.
    if (!fileAccess->ftruncate(size))
        return API_EWRITE;

    // Couldn't get the file's info.
    if (!fileAccess->fstat())
        return API_EWRITE;

    // Latch the file's previous size.
    auto previousSize = mFileInfo->size();

    // Update the file's info.
    mFileInfo->set(fileAccess->mtime, fileAccess->size);

    // For capture.
    auto id = mFile->id();

    // Invalidate the file's attributes and data.
    mFileCache.mContext.mMountDB.each([=](Mount& mount) {
        mount.invalidateAttributes(id);

        // Inavlidate data only if necessary.
        if (size < previousSize)
            mount.invalidateData(id, size, previousSize - size);
    });

    // Mark the file as having been modified.
    modified(mount);

    // We're all done.
    return API_OK;
}

void FileIOContext::unref(RefBadge)
{
    // Make sure nothing else touches our reference counter.
    FileCacheLock lock(mFileCache);

    // Decrement the number of references.
    --mReferences;

    // All references have been dropped.
    if (!mReferences)
        mFileCache.remove(*this, std::move(lock));
}

ErrorOr<std::size_t> FileIOContext::write(const Mount& mount,
                                          const void* data,
                                          m_off_t length,
                                          m_off_t offset,
                                          bool noGrow)
{
    // Update file's access time.
    mFile->accessed();

    // Try and cancel any pending flush.
    cancel(false);

    // Make sure no one else touches the file.
    FileIOContextLock guard(*this);

    // Make sure the file exists and has been opened.
    auto result = open(guard, mount);

    // Couldn't download or open the file.
    if (!result)
        return result.error();

    auto fileAccess = std::move(*result);

    // Sanity.
    assert(fileAccess);
    assert(mFileInfo);

    // Retrieve the file's current size.
    auto size = mFileInfo->size();

    // Data's being appended to the end of the file.
    if (offset < 0)
        offset = size;

    // Caller doesn't want the file's size to change.
    if (noGrow)
    {
        offset = std::min(offset, size);
        length = std::min(offset + length, size) - offset;
    }

    // Writing nothing is always successful.
    if (!length)
        return 0u;

    // Convenience.
    auto data_ = reinterpret_cast<const byte*>(data);
    auto length_ = static_cast<unsigned int>(length);

    // Couldn't write the data to disk.
    if (!fileAccess->fwrite(data_, length_, offset))
        return API_EWRITE;

    // Couldn't get the file's info.
    if (!fileAccess->fstat())
        return API_EWRITE;

    // Update the file's info.
    mFileInfo->set(fileAccess->mtime, fileAccess->size);

    // Invalidate the file's attributes and data.
    mFileCache.mContext.mMountDB.each([&](Mount& mount) {
        mount.invalidateAttributes(mFile->id());
        mount.invalidateData(mFile->id(), offset, length);
    });

    // Mark the file as having been modified.
    modified(mount);

    // Let the user know whether the write succeeded.
    return static_cast<std::size_t>(length);
}

void doRef(RefBadge badge, FileIOContext& entry)
{
    entry.ref(badge);
}

void doUnref(RefBadge badge, FileIOContext& entry)
{
    entry.unref(badge);
}

Client& FileIOContext::FlushContext::client() const
{
    return mContext.mFileCache.client();
}

InodeDB& FileIOContext::FlushContext::inodeDB() const
{
    return mContext.inodeDB();
}

void FileIOContext::FlushContext::uploaded(ErrorOr<UploadResult> result)
{
    // Acquire lock.
    std::lock_guard<std::mutex> guard(mLock);

    // Couldn't upload the file's content.
    if (!result)
        return mCV.notify_all();

    // The file we were uploading has been removed.
    if (mContext.mFile->removed())
    {
        if (mUpload)
        {
            mUpload->cancel();
        }
        mCV.notify_all();
        return;
    }

    // Extract bind callback and bind handle.
    auto bind       = std::move(std::get<0>(*result));
    auto bindHandle = std::move(std::get<1>(*result));

    // Sanity.
    assert(bind);
    assert(bindHandle);

    // Let the cache know which file's associated with this bind handle.
    auto i = inodeDB().binding(*mContext.mFile, std::move(bindHandle));

    // Called when we've bound a name to our uploaded content.
    auto bound = [i, this](ErrorOr<NodeHandle> result) {
        // A name's been bound to our content.
        if (result)
            mContext.mFile->handle(*result);

        // Let the cache know we're done with the bind handle.
        inodeDB().bound(*mContext.mFile, i);

        // Let waiters know the upload's complete.
        mCV.notify_all();
    }; // bound

    // Try and bind a name to our uploaded content.
    bind(std::move(bound), mContext.mFile->handle());
}

FileIOContext::FlushContext::FlushContext(FileIOContext& context,
                                          LocalPath logicalPath)
  : mCV()
  , mContext(context)
  , mLock()
  , mUpload()
{
    // Sanity.
    assert(mContext.mFile);

    // Retrieve the content's current name and parent.
    auto info = mContext.mFile->info();

    // Compute the file's path if necessary.
    auto filePath = mContext.mFilePath;

    // Path hasn't been generated yet.
    if (filePath.empty())
        filePath = mContext.mFileInfo->path();

    // Sanity.
    assert(!filePath.empty());
    assert(!logicalPath.empty());

    // Create our upload.
    mUpload = client().upload(std::move(logicalPath),
                              info.mName,
                              static_cast<NodeHandle>(info.mParentID),
                              std::move(filePath));

    // Wrap our uploaded(...) method so we can use it as a callback.
    UploadCallback uploaded = std::bind(&FlushContext::uploaded,
                                        this,
                                        std::placeholders::_1);

    // Try and upload our content.
    mUpload->begin(std::move(uploaded));
}

bool FileIOContext::FlushContext::cancel()
{
    // Acquire lock.
    std::lock_guard<std::mutex> guard(mLock);

    // Try and cancel the upload.
    mUpload->cancel();

    // Let the caller know if the upload was cancelled.
    return mUpload->cancelled();
}

Error FileIOContext::FlushContext::result() const
{
    // Acquire lock.
    std::unique_lock<std::mutex> lock(mLock);

    // Waits for the upload to complete.
    auto uploaded = [&]() {
        return mUpload->completed();
    }; // completed

    // Wait for the upload to complete.
    mCV.wait(lock, std::move(uploaded));

    // Return the upload's result to the caller.
    return mUpload->result();
}

} // fuse
} // mega

