#pragma once

#include <atomic>
#include <mutex>
#include <thread>

#include <mega/fuse/common/activity_monitor.h>
#include <mega/fuse/common/client.h>
#include <mega/fuse/common/error_or_forward.h>
#include <mega/fuse/common/pending_callbacks.h>
#include <mega/fuse/common/task_queue.h>

#include <mega/types.h>

namespace mega
{
namespace fuse
{

// Wraps MegaClient so it can be used by FUSE.
class ClientAdapter
  : public Client
{
    // So we can track when other threads are busy within us.
    mutable ActivityMonitor mActivities;

    // Which client's doing our bidding?
    MegaClient& mClient;

    // Whether this client has been deinitialized.
    std::atomic<bool> mDeinitialized;

    // Serializes access to instance members.
    std::mutex mLock;

    // Tracks callbacks waiting to be called.
    PendingCallbacks mPendingCallbacks;

    // Tracks queued tasks.
    TaskQueue mTaskQueue;

    // So we can check when we're running on the client thread.
    std::thread::id mThreadID;

public:
    explicit ClientAdapter(MegaClient& client);

    ~ClientAdapter();

    // Retrieve the names of a parent's children.
    std::set<std::string> childNames(NodeHandle parent) const override;

    // Get our hands on the underlying client.
    MegaClient& client() const;

    // Compute a suitable path for a database.
    LocalPath dbPath(const std::string& name) const override;

    // Query where databases should be stored.
    LocalPath dbRootPath() const override;

    // Deinitialize the client.
    void deinitialize() override;

    // Remove a sync previously created with synchronize(...)
    void desynchronize(mega::handle id) override;

    // Execute queued functions on the client thread.
    void dispatch();

    // Download a file from the cloud.
    void download(DownloadCallback callback,
                  NodeHandle handle,
                  const LocalPath& logicalPath,
                  const LocalPath& physicalPath) override;

    // Execute a function for each child of a node.
    void each(std::function<void(NodeInfo)> function,
              NodeHandle handle) const override;

    // Emit a FUSE event.
    void emitEvent(const MountEvent& event) override;

    // Execute some function on the client's thread.
    Task execute(std::function<void(const Task&)> function) override;

    // Query whether a node exists in the cloud.
    bool exists(NodeHandle handle) const override;

    // Request access the local filesystem.
    FileSystemAccess& fsAccess() const override;

    // Retrieve a description of a specific node.
    ErrorOr<NodeInfo> get(NodeHandle handle) const override;

    // Retrieve a description of a specific child.
    ErrorOr<NodeInfo> get(NodeHandle parent,
                          const std::string& name) const override;

    // Query what a child's node handle is.
    NodeHandle handle(NodeHandle parent,
                      const std::string& name,
                      BindHandle* bindHandle = nullptr) const override;

    // Query whether a parent contains any children.
    ErrorOr<bool> hasChildren(NodeHandle parent) const override;

    // Initialize the client.
    void initialize() override;

    // Make a new directory in the cloud.
    void makeDirectory(MakeDirectoryCallback callback,
                       const std::string& name,
                       NodeHandle parent) override;

    // Check if path is "mountable."
    bool mountable(const NormalizedPath& path) const override;

    // Move source to target.
    void move(MoveCallback callback,
              NodeHandle source,
              NodeHandle target) override;

    // Is this the client thread?
    bool isClientThread() const;

    // Query who a node's parent is.
    NodeHandle parentHandle(NodeHandle handle) const override;

    // What permissions are applicable to a node?
    accesslevel_t permissions(NodeHandle handle) const override;

    // Remove a node.
    void remove(RemoveCallback callback, NodeHandle handle) override;

    // Rename a node.
    void rename(RenameCallback callback,
                const std::string& name,
                NodeHandle handle) override;

    // Retrieve the client's current session ID.
    std::string sessionID() const override;

    // Retrieve storage statistics from the cloud.
    void storageInfo(StorageInfoCallback callback) override;

    // Synchronize a local tree against some location in the cloud.
    auto synchronize(const NormalizedPath& path, NodeHandle target)
      -> std::tuple<mega::handle, Error, SyncError> override;

    // Set a file's modification time.
    void touch(TouchCallback callback,
               NodeHandle handle,
               m_time_t modified) override;

    // Called when nodes have been updated in the cloud.
    void updated(const sharedNode_vector& nodes);

    // Upload a file to the cloud.
    ErrorOr<UploadPtr> upload(UploadCallback callback,
                              const LocalPath& logicalPath,
                              const std::string& name,
                              NodeHandle parent,
                              const LocalPath& physicalPath) override;

    // Wraps the provided callback such that it can be cancelled.
    template<typename T, typename U = IsErrorLike<T>>
    auto wrap(std::function<void(T)> callback)
      -> typename std::enable_if<U::value, std::function<void(T)>>::type
    {
        return mPendingCallbacks.wrap(std::move(callback));
    }
}; // ClientAdapter

} // fuse
} // mega

