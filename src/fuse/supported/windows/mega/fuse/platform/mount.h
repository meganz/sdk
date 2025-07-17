#pragma once

#include <mega/common/activity_monitor.h>
#include <mega/common/error_or_forward.h>
#include <mega/common/task_executor.h>
#include <mega/fuse/common/inode_db_forward.h>
#include <mega/fuse/common/inode_forward.h>
#include <mega/fuse/common/inode_info_forward.h>
#include <mega/fuse/common/mount.h>
#include <mega/fuse/common/mount_result_forward.h>
#include <mega/fuse/platform/context_forward.h>
#include <mega/fuse/platform/dispatcher.h>
#include <mega/fuse/platform/library.h>

namespace mega
{
namespace fuse
{
namespace platform
{

class Mount
  : public fuse::Mount
{
    // So the dispatcher can invoke our callbacks.
    friend class Dispatcher;

    NTSTATUS canDelete(PVOID context);

    void cleanup(PVOID context,
                 const std::wstring& path,
                 ULONG flags);

    void close(PVOID context);

    NTSTATUS create(const std::wstring& path,
                    UINT32 options,
                    UINT32 access,
                    PVOID& context,
                    FSP_FSCTL_FILE_INFO& info);

    NTSTATUS flush(PVOID context,
                   FSP_FSCTL_FILE_INFO& info);

    NTSTATUS getDirInfoByName(PVOID context,
                              const std::wstring& path,
                              FSP_FSCTL_DIR_INFO& info);

    NTSTATUS getFileInfo(PVOID context,
                         FSP_FSCTL_FILE_INFO& info);

    NTSTATUS getSecurity(PVOID context,
                         PSECURITY_DESCRIPTOR descriptor,
                         SIZE_T& descriptorLength);

    NTSTATUS getSecurity(PSECURITY_DESCRIPTOR descriptor,
                         SIZE_T& descriptorLength,
                         InodeInfo info);

    NTSTATUS getSecurityByName(const std::wstring& path,
                               PUINT32 attributes,
                               PSECURITY_DESCRIPTOR descriptor,
                               SIZE_T* descriptorLength);

    NTSTATUS getVolumeInfo(FSP_FSCTL_VOLUME_INFO& info);

    // For convenience.
    InodeDB& inodeDB() const;

    NTSTATUS open(const std::wstring& path,
                  UINT32 options,
                  UINT32 access,
                  PVOID& context,
                  FSP_FSCTL_FILE_INFO& info);

    NTSTATUS overwrite(PVOID context,
                       FSP_FSCTL_FILE_INFO& info);

    NTSTATUS read(PVOID context,
                  PVOID buffer,
                  UINT64 offset,
                  ULONG length,
                  ULONG& numRead);

    NTSTATUS readDirectory(PVOID context,
                           const std::string& pattern,
                           const std::string& marker,
                           PVOID buffer,
                           ULONG length,
                           ULONG& numWritten);

    NTSTATUS rename(PVOID context,
                    const std::wstring& targetPath,
                    BOOLEAN replace);

    NTSTATUS setBasicInfo(PVOID context,
                          UINT32 attributes,
                          UINT64 created,
                          UINT64 accessed,
                          UINT64 written,
                          UINT64 changed,
                          FSP_FSCTL_FILE_INFO& info);

    NTSTATUS setFileSize(PVOID context,
                         UINT64 size,
                         BOOLEAN allocated,
                         FSP_FSCTL_FILE_INFO& info);

    NTSTATUS setSecurity(PVOID context,
                         SECURITY_INFORMATION security,
                         PSECURITY_DESCRIPTOR descriptor);

    void stopped(BOOLEAN normally);

    NTSTATUS write(PVOID context,
                   PVOID buffer,
                   UINT64 offset,
                   ULONG length,
                   BOOLEAN append,
                   BOOLEAN noGrow,
                   ULONG& numWritten,
                   FSP_FSCTL_FILE_INFO& info);

    // Tracks whether any requests are in progress.
    common::ActivityMonitor mActivities;

    // Responsible for receiving requests from WinFSP.
    Dispatcher mDispatcher;

    // Responsible for performing select requests.
    common::TaskExecutor mExecutor;

public:
    Mount(const MountInfo& info,
          MountDB& mountDB);

    ~Mount();

    // Invalidate an inode's attributes.
    void invalidateAttributes(InodeID id) override;

    // Invalidate an inode's data.
    void invalidateData(InodeID id,
                        m_off_t offset,
                        m_off_t size) override;

    void invalidateData(InodeID id) override;

    // Invalidate a directory entry.
    void invalidateEntry(const std::string& name,
                         InodeID child,
                         InodeID parent) override;

    void invalidateEntry(const std::string& name,
                         InodeID parent) override;

    // Translate a mount-speicifc inode ID to a system-wide inode ID.
    InodeID map(MountInodeID id) const override;

    // Translate a system-wide inode ID to a mount-specific inode ID.
    MountInodeID map(InodeID id) const override;

    // What local path is this mount mapping from?
    common::NormalizedPath path() const override;

    // Remove the mount from memory.
    MountResult remove();

    void notifyFileExplorerSetter();
}; // Mount

} // platform
} // fuse
} // mega
