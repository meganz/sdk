#pragma once

#include <mega/auto_file_handle.h>
#include <mega/common/instance_logger.h>
#include <mega/file_service/application_data_forward.h>
#include <mega/file_service/file_callbacks.h>
#include <mega/file_service/file_context_pointer.h>
#include <mega/file_service/file_forward.h>
#include <mega/file_service/file_info_forward.h>
#include <mega/file_service/file_range_forward.h>
#include <mega/file_service/file_range_vector.h>
#include <mega/file_service/file_service_context_badge_forward.h>
#include <mega/file_service/file_structure_mutex_forward.h>
#include <mega/file_service/file_truncate_flags.h>
#include <mega/file_service/file_write_flags.h>
#include <mega/file_service/read_tag.h>

namespace mega
{

class LocalPath;
class NodeHandle;

namespace file_service
{

class File
{
    // Logs instance lifetime.
    common::InstanceLogger<File> mInstanceLogger;

    // What context is this instance wrapping?
    FileContextPtr mContext;

public:
    File(FileServiceContextBadge badge, FileContextPtr context);

    File(const File& other);

    File(File&& other);

    ~File();

    File& operator=(const File& rhs);

    File& operator=(File&& rhs);

    // Append data to the end of this file.
    void append(const ApplicationData& applicationData,
                const void* buffer,
                FileAppendCallback callback,
                std::uint64_t length);

    // What ranges are currently being downloaded?
    FileRangeVector downloading() const;

    // Duplicate OS file descriptor of storage file, returns an unset AutoFileHandle on errors
    AutoFileHandle dupFileDescriptor();

    // Fetch all of this file's data from the cloud.
    void fetch(FileFetchCallback callback);

    // Wait until all fetches in progress have completed.
    void fetchBarrier(FileFetchBarrierCallback callback);

    // Flush this file's local modifications to the cloud.
    void flush(FileFlushCallback callback, bool cancelOnWrite);

    // Retrieve information about this file.
    FileInfo info() const;

    // What ranges of this file are currently in storage?
    FileRangeVector ranges() const;

    // Read data from this file.
    //
    // Be aware that this function does not guarantee that all data
    // requested will be provided in a single chunk.
    //
    // If you request 1MiB of data, you may get the entire 1MiB or you might
    // get much less. The information passed to the callback will specify
    // how much data you've received and it's your responsibility to request
    // the rest, if necessary
    void read(FileReadCallback callback, std::uint64_t offset, std::uint64_t length, ReadTag tag);

    void read(FileReadCallback callback, const FileRange& range, ReadTag tag);

    // Reclaim this file's storage.
    void reclaim(FileReclaimCallback callback);

    // Remove the file.
    //
    // If serviceOnly is true, the file will only be removed from the cache.
    //
    // If serviceOnly is false and the file exists in the cloud, the file
    // will be removed from the cloud and from the cache.
    void remove(FileRemoveCallback callback, FileStructureUniqueLock& lock, bool serviceOnly);
    void remove(FileRemoveCallback callback, bool serviceOnly);

    // Update the file's modification time.
    void touch(const ApplicationData& applicationData,
               FileTouchCallback callback,
               std::int64_t modified);

    // Truncate this file to a specified size.
    void truncate(const ApplicationData& applicationData,
                  FileTruncateCallback callback,
                  FileTruncateFlags flags,
                  std::uint64_t newSize);

    // Write data to this file.
    void write(const ApplicationData& applicationData,
               const void* buffer,
               FileWriteCallback callback,
               FileWriteFlags flags,
               std::uint64_t offset,
               std::uint64_t length);

    void write(const ApplicationData& applicationData,
               const void* buffer,
               FileWriteCallback callback,
               FileWriteFlags flags,
               const FileRange& range);
}; // File

} // file_service
} // mega
