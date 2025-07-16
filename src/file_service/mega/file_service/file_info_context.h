#include <mega/common/activity_monitor.h>
#include <mega/common/shared_mutex.h>
#include <mega/file_service/file_event_observer.h>
#include <mega/file_service/file_event_observer_id.h>
#include <mega/file_service/file_id.h>
#include <mega/file_service/file_info_context_forward.h>
#include <mega/file_service/file_range_forward.h>
#include <mega/file_service/file_service_context_forward.h>
#include <mega/file_service/file_size_info.h>
#include <mega/types.h>

#include <mutex>

namespace mega
{
namespace file_service
{

class FileInfoContext: public FileSizeInfo
{
    // Maps observer IDs to observer callbacks.
    using FileEventObserverMap = std::map<FileEventObserverID, FileEventObserver>;

    // Retrieve one of our properties in a thread-safe manner.
    template<typename T>
    auto get(T FileInfoContext::* const property) const;

    // Transmit an event to all registered observers.
    void notify(const FileEvent& event);

    // When was the file last accessed?
    std::int64_t mAccessed;

    // Makes sure mService isn't destroyed until we are.
    common::Activity mActivity;

    // Whether this file's been locally modified.
    bool mDirty;

    // The node in the cloud this file is associated with, if any.
    NodeHandle mHandle;

    // The unique identifier for this file.
    const FileID mID;

    // Serializes access to our members.
    mutable common::SharedMutex mLock;

    // The file's logical size.
    std::uint64_t mLogicalSize;

    // The time the file was last modified.
    std::int64_t mModified;

    // The file's size on disk.
    std::uint64_t mPhysicalSize;

    // Who should we notify when this file's information changes?
    FileEventObserverMap mObservers;

    // Serializes access to mObservers.
    std::recursive_mutex mObserversLock;

    // The service managing this instance.
    FileServiceContext& mService;

public:
    FileInfoContext(std::int64_t accessed,
                    common::Activity activity,
                    bool dirty,
                    NodeHandle handle,
                    FileID id,
                    std::uint64_t logicalSize,
                    std::int64_t modified,
                    std::uint64_t physicalSize,
                    FileServiceContext& service);

    ~FileInfoContext();

    // Set the file's last access time.
    void accessed(std::int64_t accessed);

    // When was this file last accessed?
    std::int64_t accessed() const;

    // Add an observer.
    FileEventObserverID addObserver(FileEventObserver observer);

    // Has the file been locally modified?
    bool dirty() const;

    // Specify the node that this file is associated with.
    void handle(NodeHandle handle);

    // What node is this file associated with?
    auto handle() const -> NodeHandle;

    // What is this file's identifier?
    auto id() const -> FileID;

    // How large is this file?
    std::uint64_t logicalSize() const override;

    // Update the file's access and modification time.
    void modified(std::int64_t accessed, std::int64_t modified);

    // When was this file last modified?
    auto modified() const -> std::int64_t;

    // Update the file's physical size.
    void physicalSize(std::uint64_t physicalSize) override;

    // What is the file's size on disk?
    std::uint64_t physicalSize() const override;

    // Remove an observer.
    void removeObserver(FileEventObserverID id);

    // Signal that the file has been truncated.
    void truncated(std::int64_t modified, std::uint64_t size);

    // Signal that data has been written to the file.
    void written(const FileRange& range, std::int64_t modified);
}; // FileInfoContext

} // file_service
} // mega
