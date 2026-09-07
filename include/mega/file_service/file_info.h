#pragma once

#include <mega/common/instance_logger.h>
#include <mega/file_service/file_context_badge_forward.h>
#include <mega/file_service/file_id_forward.h>
#include <mega/file_service/file_info_context_pointer.h>
#include <mega/file_service/file_info_forward.h>
#include <mega/file_service/file_service_context_badge_forward.h>

#include <cstdint>
#include <optional>
#include <shared_mutex>
#include <type_traits>
#include <utility>

namespace mega
{

class NodeHandle;

namespace file_service
{

class FileInfo
{
    // So shared_lock can call (un)?lock_shared.
    friend class std::shared_lock<const FileInfo>;

    // Logs instance lifetime.
    common::InstanceLogger<FileInfo> mInstanceLogger;

    // What context does this instance wrap?
    FileInfoContextPtr mContext;

    // Acquire a read lock on this file's info.
    void lock_shared() const;

    // Release a read lock on this file's info.
    void unlock_shared() const;

public:
    FileInfo(FileContextBadge badge, FileInfoContextPtr context);

    FileInfo(FileServiceContextBadge badge, FileInfoContextPtr context);

    FileInfo(const FileInfo& other);

    ~FileInfo();

    // Does rhs describe the same file as we do?
    bool operator==(const FileInfo& rhs) const
    {
        return mContext == rhs.mContext;
    }

    // Does rhs describe a different file than we do?
    bool operator!=(const FileInfo& rhs) const
    {
        return !operator==(rhs);
    }

    // Assign rhs to this instance.
    FileInfo& operator=(const FileInfo& rhs);

    // When was this file last accessed?
    std::int64_t accessed() const;

    // How much disk space has been allocated to this file?
    std::uint64_t allocatedSize() const;

    // What is the estimated bitrate of this file?
    //
    // Meaningful only for media files such as videos.
    std::optional<std::uint64_t> bitrate() const;

    // Has this file been locally modified?
    bool dirty() const;

    // How long does this file play for?
    //
    // Meaningful only for media files such as videos.
    std::optional<std::uint32_t> duration() const;

    // What node is this file associated with?
    NodeHandle handle() const;

    // What node is this file associated with?
    FileID id() const;

    // When was this file last modified?
    std::int64_t modified() const;

    // Has this file been removed?
    bool removed() const;

    // How large does the filesystem say this file is?
    std::uint64_t reportedSize() const;

    // How large is this file conceptually?
    std::uint64_t size() const;

    // Invoke function while holding a lock on this file's info.
    template<typename Function,
             std::enable_if_t<std::is_invocable_v<Function, const FileInfo&>>* = nullptr>
    auto whileLocked(Function&& function) const
    {
        std::shared_lock guard(*this);

        return std::invoke(std::forward<Function>(function), *this);
    }
}; // FileInfo

} // file_service
} // mega
