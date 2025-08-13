#include <mega/common/activity_monitor.h>
#include <mega/common/shared_mutex.h>
#include <mega/file_service/file_event_emitter.h>
#include <mega/file_service/file_event_observer.h>
#include <mega/file_service/file_event_observer_id.h>
#include <mega/file_service/file_id.h>
#include <mega/file_service/file_info_context_forward.h>
#include <mega/file_service/file_location.h>
#include <mega/file_service/file_range_forward.h>
#include <mega/file_service/file_service_context_forward.h>
#include <mega/file_service/file_size_info.h>
#include <mega/types.h>

#include <mutex>
#include <optional>
#include <type_traits>

namespace mega
{
namespace file_service
{

class FileInfoContext: FileEventEmitter, public FileSizeInfo
{
    // Maps observer IDs to observer callbacks.
    using FileEventObserverMap = std::map<FileEventObserverID, FileEventObserver>;

    // Retrieve one of our properties in a thread-safe manner.
    template<typename T>
    auto get(T FileInfoContext::* const property) const;

    // Set one of our properties in a thread-safe manner.
    template<typename T, typename U>
    void set(T FileInfoContext::*property, U&& value);

    // Transmit an event to all registered observers.
    void notify(const FileEvent& event);

    // When was the file last accessed?
    std::int64_t mAccessed;

    // Makes sure mService isn't destroyed until we are.
    common::Activity mActivity;

    // How much disk space has been allocated to this file?
    std::uint64_t mAllocatedSize;

    // Whether this file's been locally modified.
    bool mDirty;

    // The node in the cloud this file is associated with, if any.
    NodeHandle mHandle;

    // The unique identifier for this file.
    const FileID mID;

    // Where is this file located in the cloud?
    FileLocation mLocation;

    // Serializes access to our members.
    mutable common::SharedMutex mLock;

    // The time the file was last modified.
    std::int64_t mModified;

    // Has this file been removed?
    bool mRemoved;

    // How large does the filesystem say this file is?
    std::uint64_t mReportedSize;

    // The service managing this instance.
    FileServiceContext& mService;

    // How large is this file conceptually?
    std::uint64_t mSize;

public:
    FileInfoContext(std::int64_t accessed,
                    common::Activity activity,
                    std::uint64_t allocatedSize,
                    bool dirty,
                    NodeHandle handle,
                    FileID id,
                    const FileLocation& location,
                    std::int64_t modified,
                    std::uint64_t reportedSize,
                    FileServiceContext& service,
                    std::uint64_t size);

    ~FileInfoContext();

    // Set the file's last access time.
    void accessed(std::int64_t accessed);

    // When was this file last accessed?
    std::int64_t accessed() const;

    // Add an observer.
    using FileEventEmitter::addObserver;

    // Update this file's allocated size.
    void allocatedSize(std::uint64_t allocatedSize) override;

    // How much disk space has been allocated to this file?
    std::uint64_t allocatedSize() const override;

    // Has the file been locally modified?
    bool dirty() const;

    // Signal that this file has been flushed to the cloud.
    void flushed(NodeHandle handle);

    // What node is this file associated with?
    auto handle() const -> NodeHandle;

    // What is this file's identifier?
    auto id() const -> FileID;

    // Specify where this file is located in the cloud.
    void location(const FileLocation& location);

    // Where is this file located in the cloud?
    FileLocation location() const;

    // Update the file's access and modification time.
    void modified(std::int64_t accessed, std::int64_t modified);

    // When was this file last modified?
    auto modified() const -> std::int64_t;

    // Remove an observer.
    using FileEventEmitter::removeObserver;

    // Specify whether this file has been removed.
    void removed(bool replaced);

    // Has this file been removed?
    bool removed() const;

    // Update this file's reported size.
    void reportedSize(std::uint64_t reportedSize) override;

    // How large does the filesystem say this file is?
    std::uint64_t reportedSize() const override;

    // How large is this file conceptually?
    std::uint64_t size() const override;

    // Signal that the file has been truncated.
    void truncated(std::int64_t modified, std::uint64_t size);

    // Signal that data has been written to the file.
    void written(std::int64_t modified, const FileRange& range);
}; // FileInfoContext

} // file_service
} // mega
