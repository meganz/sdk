#pragma once

#include <cstddef>
#include <mutex>
#include <string>

#include <mega/common/client_callbacks.h>
#include <mega/common/error_or.h>
#include <mega/common/lock_forward.h>
#include <mega/common/lockable.h>
#include <mega/common/shared_mutex.h>
#include <mega/common/task_queue.h>
#include <mega/common/utility.h>
#include <mega/fuse/common/file_cache_forward.h>
#include <mega/fuse/common/file_info_forward.h>
#include <mega/fuse/common/file_inode_forward.h>
#include <mega/fuse/common/file_io_context_forward.h>
#include <mega/fuse/common/inode_db_forward.h>
#include <mega/fuse/common/inode_id_forward.h>
#include <mega/fuse/common/mount_forward.h>
#include <mega/fuse/common/ref.h>

#include <mega/filesystem.h>
#include <mega/types.h>

namespace mega
{
namespace common
{

template<>
struct LockableTraits<fuse::FileIOContext>
{
    using LockType = SharedMutex;

    static void acquired(const fuse::FileIOContext& context);

    static void acquiring(const fuse::FileIOContext& context);

    static void couldntAcquire(const fuse::FileIOContext& context);

    static void released(const fuse::FileIOContext& context);

    static void tryAcquire(const fuse::FileIOContext& context);
}; // LockableTraits<fuse::FileIOContext>

} // common

namespace fuse
{

class FileIOContext
  : public common::Lockable<FileIOContext>
{
    // Bundles up state required to perform a flush.
    class FlushContext;

    // Convenience.
    using FlushContextPtr = std::shared_ptr<FlushContext>;

    // Create the file.
    common::ErrorOr<FileAccessSharedPtr> create();

    // Download the file from the cloud.
    common::ErrorOr<FileAccessSharedPtr> download(const Mount& mount);

    // How long should we wait before we flush modifications?
    std::chrono::seconds flushDelay() const;

    // Retrieve a reference to the inode DB.
    InodeDB& inodeDB() const;

    // Flush this context's content to the cloud.
    Error manualFlush(FileIOContextSharedLock& contextLock,
                      std::unique_lock<std::mutex>& flushLock,
                      NodeHandle mountHandle,
                      LocalPath mountPath);

    // Called when it's time to perform a queued flush.
    void onPeriodicFlush(FileIOContextRef& context,
                         m_time_t lastModified,
                         NodeHandle mountHandle,
                         LocalPath& mountPath,
                         const common::Task& task);

    // Open the file for IO.
    auto open(FileIOContextLock& lock,
             const Mount& mount,
             m_off_t hint = -1)
      -> common::ErrorOr<FileAccessSharedPtr>;

    auto open(FileIOContextSharedLock& lock,
              const Mount& mount,
              m_off_t hint = -1)
      -> common::ErrorOr<FileAccessSharedPtr>;

    // What file does this entry represent?
    FileInodeRef mFile;

    // How we manipulate the file on disk.
    FileAccessWeakPtr mFileAccess;

    // What cache contains this context?
    FileCache& mFileCache;

    // Where is that file's local info stored?
    FileInfoRef mFileInfo;

    // Where is the file stored on disk?
    LocalPath mFilePath;

    // State required for the current flush, if any.
    FlushContextPtr mFlushContext;

    // Serializes access to mFlush* members.
    std::mutex mFlushLock;

    // True if we need to flush this file's content to the cloud.
    bool mFlushNeeded;

    // Represents a queued periodic flush, if any.
    common::Task mPeriodicFlushTask;

    // Tracks how many actors reference this instance.
    unsigned long mReferences;

public:
    FileIOContext(FileCache& cache,
                  FileInodeRef file,
                  FileInfoRef info,
                  bool modified);

    ~FileIOContext();

    // Cancel pending flush and/or upload.
    void cancel(bool pendingFlush);

    // The same as above but performed off-thread.
    void cancel();

    // What ID is this context associated with?
    InodeID id() const;

    // What file does this context represent?
    FileInodeRef file() const;

    // Flush any modifications to the cloud.
    Error manualFlush(const Mount& mount);

    // Called when the file's been modified.
    //
    // Responsible for queuing a flush if necessary.
    void modified(const Mount& mount);

    // Open the file for manipulation.
    Error open(const Mount& mount,
               bool truncate);

    // Read data from the file.
    common::ErrorOr<std::string> read(const Mount& mount,
                                      m_off_t offset,
                                      unsigned int size);

    // Increment this instance's reference count.
    void ref(RefBadge badge);

    // Retrieve the file's current size.
    m_off_t size() const;

    // Set the file's modification time.
    Error touch(const Mount& mount,
                m_time_t modified);

    // Truncate the file to a specified size.
    Error truncate(const Mount& mount,
                   m_off_t size, bool dontGrow);
    
    // Decrement this instance's reference count.
    void unref(RefBadge badge);

    // Write data to the file.
    common::ErrorOr<std::size_t> write(const Mount& mount,
                                       const void* data,
                                       m_off_t length,
                                       m_off_t offset,
                                       bool noGrow);
}; // FileIOContext

} // fuse
} // mega
