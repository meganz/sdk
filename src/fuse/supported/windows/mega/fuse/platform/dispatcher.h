#pragma once

#include <memory>

#include <mega/fuse/platform/dispatcher_forward.h>
#include <mega/fuse/platform/library.h>
#include <mega/fuse/platform/mount_forward.h>

namespace mega
{
namespace fuse
{
namespace platform
{

// Responsible for receiving and dispatching filesystem requests.
class Dispatcher
{
    static NTSTATUS canDelete(FSP_FILE_SYSTEM* filesystem,
                              PVOID context,
                              PWSTR path);

    static void cleanup(FSP_FILE_SYSTEM* filesystem,
                        PVOID context,
                        PWSTR path,
                        ULONG flags);

    static void close(FSP_FILE_SYSTEM* filesystem,
                      PVOID context);

    static NTSTATUS create(FSP_FILE_SYSTEM* filesystem,
                           PWSTR path,
                           UINT32 options,
                           UINT32 access,
                           UINT32 attributes,
                           PSECURITY_DESCRIPTOR descriptor,
                           UINT64 allocation,
                           PVOID* context,
                           FSP_FSCTL_FILE_INFO* info);

    static NTSTATUS flush(FSP_FILE_SYSTEM* filesystem,
                          PVOID context,
                          FSP_FSCTL_FILE_INFO* info);

    static NTSTATUS getDirInfoByName(FSP_FILE_SYSTEM* filesystem,
                                     PVOID context,
                                     PWSTR path,
                                     FSP_FSCTL_DIR_INFO* info);

    static NTSTATUS getFileInfo(FSP_FILE_SYSTEM *filesystem,
                                PVOID context,
                                FSP_FSCTL_FILE_INFO* info);

    static NTSTATUS getSecurity(FSP_FILE_SYSTEM*filesystem,
                                PVOID context,
                                PSECURITY_DESCRIPTOR descriptor,
                                SIZE_T* descriptorLength);

    static NTSTATUS getSecurityByName(FSP_FILE_SYSTEM* filesystem,
                                      PWSTR path,
                                      PUINT32 attributes,
                                      PSECURITY_DESCRIPTOR descriptor,
                                      SIZE_T* descriptorLength);

    static NTSTATUS getVolumeInfo(FSP_FILE_SYSTEM* filesystem,
                                  FSP_FSCTL_VOLUME_INFO* info);

    static NTSTATUS open(FSP_FILE_SYSTEM* filesystem,
                         PWSTR path,
                         UINT32 options,
                         UINT32 access,
                         PVOID* context,
                         FSP_FSCTL_FILE_INFO* info);

    static NTSTATUS overwrite(FSP_FILE_SYSTEM* filesystem,
                              PVOID context,
                              UINT32 attributes,
                              BOOLEAN replaceAttributes,
                              UINT64 allocation,
                              FSP_FSCTL_FILE_INFO* info);

    static NTSTATUS read(FSP_FILE_SYSTEM* filesystem,
                         PVOID context,
                         PVOID buffer,
                         UINT64 offset,
                         ULONG Length,
                         PULONG numRead);

    static NTSTATUS readDirectory(FSP_FILE_SYSTEM* filesystem,
                                  PVOID context,
                                  PWSTR pattern,
                                  PWSTR marker,
                                  PVOID buffer,
                                  ULONG length,
                                  PULONG numWritten);

    static NTSTATUS rename(FSP_FILE_SYSTEM* filesystem,
                           PVOID context,
                           PWSTR sourceName,
                           PWSTR targetName,
                           BOOLEAN replace);

    static NTSTATUS setBasicInfo(FSP_FILE_SYSTEM* filesystem,
                                 PVOID context,
                                 UINT32 attributes,
                                 UINT64 created,
                                 UINT64 accessed,
                                 UINT64 written,
                                 UINT64 changed,
                                 FSP_FSCTL_FILE_INFO* info);

    static NTSTATUS setFileSize(FSP_FILE_SYSTEM* filesystem,
                                PVOID context,
                                UINT64 size,
                                BOOLEAN allocated,
                                FSP_FSCTL_FILE_INFO* info);
    
    static NTSTATUS setSecurity(FSP_FILE_SYSTEM* filesystem,
                                PVOID context,
                                SECURITY_INFORMATION security,
                                PSECURITY_DESCRIPTOR descriptor);

    static void stopped(FSP_FILE_SYSTEM* filesystem,
                        BOOLEAN normally);

    static NTSTATUS write(FSP_FILE_SYSTEM* filesystem,
                          PVOID context,
                          PVOID buffer,
                          UINT64 offset,
                          ULONG length,
                          BOOLEAN append,
                          BOOLEAN noGrow,
                          PULONG numWritten,
                          FSP_FSCTL_FILE_INFO* info);

    // The filesystem we're dispatching requests for.
    FSP_FILE_SYSTEM* mFilesystem;

    // What mount are dispatching requests to?
    Mount& mMount;

    // Who should be called for what requests?
    static const FSP_FILE_SYSTEM_INTERFACE mOperations;

public:
    explicit Dispatcher(Mount& mount);

    ~Dispatcher();

    void reply(FSP_FSCTL_TRANSACT_RSP& response, Error result);
    void reply(FSP_FSCTL_TRANSACT_RSP& response, NTSTATUS result);

    FSP_FSCTL_TRANSACT_REQ& request() const;

    void start();

    void stop();
}; // Dispatcher

} // platform
} // fuse
} // mega
