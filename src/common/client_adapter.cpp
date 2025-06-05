#include <mega/base64.h>
#include <mega/common/client_adapter.h>
#include <mega/common/error_or.h>
#include <mega/common/logging.h>
#include <mega/common/node_event.h>
#include <mega/common/node_event_observer.h>
#include <mega/common/node_event_queue.h>
#include <mega/common/node_event_type.h>
#include <mega/common/node_info.h>
#include <mega/common/normalized_path.h>
#include <mega/common/partial_download.h>
#include <mega/common/partial_download_callback.h>
#include <mega/common/status_flag.h>
#include <mega/common/upload.h>
#include <mega/common/utility.h>
#include <mega/file.h>
#include <mega/megaapp.h>
#include <mega/megaclient.h>
#include <mega/node.h>
#include <mega/scoped_helpers.h>

#include <atomic>
#include <cassert>
#include <functional>
#include <memory>
#include <mutex>
#include <sstream>

namespace mega
{
namespace common
{

// Convenience.
using NewNodeVector = std::vector<NewNode>;
using NewNodeVectorPtr = std::shared_ptr<NewNodeVector>;

class ClientTransfer
  : public File
{
protected:
    ClientTransfer() = default;

    ~ClientTransfer() = default;

    // Constantly true.
    bool isFuseTransfer() const override;
}; // ClientTransfer

class ClientDownload
  : public ClientTransfer
{
    // Called when the download has completed.
    void completed(Transfer* transfer, putsource_t source) override;

    // Called when the download has terminated (cancellation, failure.)
    void terminated(mega::error result) override;

    // Who do we call when we've completed?
    std::function<void(Error)> mCallback;

public:
    ClientDownload(std::function<void(Error)> callback,
                   const LocalPath& logicalPath,
                   const Node& node,
                   const LocalPath& physicalPath);

    // Begin the download.
    bool begin(MegaClient& client);

    // Called when the download has completed.
    //
    // Forwards result to callback and deletes the instance.
    void completed(Error result);
}; // ClientDownload

class ClientNodeEvent
  : public NodeEvent
{
public:
    ClientNodeEvent(sharedNode_vector::const_iterator position);

    // Is this node a directory?
    bool isDirectory() const override;

    // What is this node's handle?
    NodeHandle handle() const override;

    // Retrieve this node's description.
    NodeInfo info() const override;

    // What is this node's name?
    const std::string& name() const override;

    // Who is this node's parent?
    NodeHandle parentHandle() const override;

    // What kind of event is this?
    NodeEventType type() const override;

    // What node does this event represent?
    sharedNode_vector::const_iterator mPosition;
}; // ClientNodeEvent

class ClientNodeEventQueue
  : public NodeEventQueue
{
    ClientNodeEvent mCurrent;
    const sharedNode_vector& mEvents;

public:
    ClientNodeEventQueue(const sharedNode_vector& events);

    // Is the queue empty?
    bool empty() const override;

    // Return a reference to the first event in the queue.
    const ClientNodeEvent& front() const override;

    // Pop an event from the queue.
    void pop_front() override;

    // How many events are in the queue?
    std::size_t size() const override;
}; // ClientNodeEventQueue

class ClientPartialDownload:
    public PartialDownload,
    public std::enable_shared_from_this<ClientPartialDownload>
{
    // Convenience.
    using Data = DirectRead::Data;
    using Event = DirectRead::CallbackParam;
    using Failure = DirectRead::Failure;

    // Called when the download's been completed.
    void completed(Error result);

    // Called when the SDK wants to feed us file content.
    void data(Data& data);

    // Called when the SDK wants to report a download failure.
    void failure(Failure& failure);

    // Signal that the download is now in progress.
    //
    // Returns false if:
    // - The download has already begun.
    // - The download has already completed.
    bool inProgress();

    // Acquire mLock if we're not executing within a callback.
    template<template<typename> typename Lock>
    Lock<std::shared_mutex> lock() const;

    // Called when the SDK wants to notify us about our download.
    static void notify(PartialDownloadWeakPtr cookie, Event& event);

    // The callback that will receive file content.
    PartialDownloadCallback& mCallback;

    // The client that will perform our download.
    ClientAdapter& mClient;

    // Is the current thread executing a callback?
    thread_local static bool mExecuting;

    // The file that we're downloading.
    const NodeHandle mHandle;

    // Serializes access to instance members.
    mutable std::shared_mutex mLock;

    // At what location in the file should we start downloading?
    std::uint64_t mOffset;

    // How many bytes of content do we still have to download?
    std::uint64_t mRemaining;

    // Tracks the status of the download.
    StatusFlags mStatus;

public:
    ClientPartialDownload(PartialDownloadCallback& callback,
                          ClientAdapter& client,
                          NodeHandle handle,
                          std::uint64_t offset,
                          std::uint64_t length);

    ~ClientPartialDownload();

    // Begin the partial download.
    void begin() override;

    // Cancel the partial download.
    bool cancel() override;

    // Is this download cancellable?
    bool cancellable() const override;

    // Has the download been cancelled?
    bool cancelled() const override;

    // Has this download completed?
    bool completed() const override;
}; // ClientPartialDownload

class ClientUpload;

// Convenience.
using ClientUploadPtr = std::shared_ptr<ClientUpload>;
using ClientUploadWeakPtr = std::weak_ptr<ClientUpload>;

class ClientUpload
  : public ClientTransfer
{
    // Bind our uploaded data to a name.
    void bind(BoundCallback callback,
              FileNodeKey fileKey,
              NodeHandle lastHandle,
              ClientUploadPtr self,
              UploadHandle uploadHandle,
              std::string fileAttr,
              UploadToken uploadToken);

    // Called when our upload has been bound to a name.
    void bound(BoundCallback callback,
               NewNodeVector& nodes,
               bool overriden,
               Error result);

    // Called when the upload has completed.
    void completed(Transfer* transfer, putsource_t source) override;

    // Called when the upload has been terminated (cancellation, failure.)
    void terminated(mega::error result) override;

    // Who do we tell when our data is uploaded?
    UploadCallback mCallback;

    // Which client is responsible for us?
    ClientAdapter& mClient;

    // Records the result of the upload.
    Error mResult;

    // A strong reference to ourselves.
    //
    // Dropped when the upload has completed.
    ClientUploadPtr mSelf;

    // Tracks the status of the upload.
    std::atomic<StatusFlags> mStatus;

public:
    ClientUpload(ClientAdapter& client,
                 const LocalPath& logicalPath,
                 NodeHandle parentHandle,
                 const std::string& name,
                 const LocalPath& physicalPath);

    ~ClientUpload();

    // Begin the upload.
    void begin(UploadCallback callback);

    // Cancel the upload.
    bool cancel();

    // Has this upload been cancelled?
    bool cancelled() const;

    // Has this upload completed?
    bool completed() const;

    // Inject a reference to ourselves.
    void inject(ClientUploadPtr self);

    // How did this upload complete?
    Error result() const;
}; // ClientUpload

class ClientUploadAdapter
  : public Upload
{
    // The actual upload.
    ClientUploadPtr mUpload;

public:
    ClientUploadAdapter(ClientUploadPtr upload);

    // Begin the upload.
    void begin(UploadCallback callback) override;

    // Cancel the upload.
    bool cancel() override;

    // Has the upload been cancelled?
    bool cancelled() const override;

    // Has the upload completed?
    bool completed() const override;
    
    // Did the upload succeed?
    Error result() const override;
}; // ClientUploadAdapter

// Retrieves a reference to a specific child.
static std::shared_ptr<Node> child(MegaClient& client,
                                   NodeHandle parent,
                                   const std::string& name);

// Translates a node into a description.
static void describe(NodeInfo& destination,
                     accesslevel_t permissions,
                     Node& source);

static NodeInfo describe(Node& node);

thread_local bool ClientPartialDownload::mExecuting{};

ClientAdapter::ClientAdapter(MegaClient& client)
  : Client(common::logger())
  , mActivities()
  , mClient(client)
  , mDeinitialized{false}
  , mLock()
  , mPendingCallbacks()
  , mTaskQueue()
  , mThreadID(std::this_thread::get_id())
{
}

ClientAdapter::~ClientAdapter()
{
    deinitialize();
}

MegaApp& ClientAdapter::application()
{
    assert(mClient.app);

    return *mClient.app;
}

std::set<std::string> ClientAdapter::childNames(NodeHandle parent) const
{
    // Make sure deinitialize(...) waits for this call to complete.
    auto activity = mActivities.begin();

    // Client's being torn down.
    if (mDeinitialized)
        return std::set<std::string>();

    // Acquire RNT lock.
    std::lock_guard<std::recursive_mutex> guard(mClient.nodeTreeMutex);

    // Try and locate specified parent.
    auto parent_ = mClient.nodeByHandle(parent);

    // Node doesn't exist.
    if (!parent_)
        return std::set<std::string>();

    // Keeps track of duplicate names.
    std::set<const std::string*> duplicates;
    std::set<std::string> names;

    // Collect child names.
    for (auto child : mClient.getChildren(parent_.get()))
    {
        // Add the child's name to the set.
        auto result = names.emplace(child->displayname());

        // Name's already been seen.
        if (!result.second)
            duplicates.emplace(&*result.first);
    }

    // Prune duplicate names.
    while (!duplicates.empty())
    {
        // Get an iterator to the duplicate we have to remove.
        auto duplicate = duplicates.begin();

        // Get an iterator to the name we have to remove.
        auto name = names.find(**duplicate);

        // Remove the duplicate from our duplicates set.
        duplicates.erase(duplicate);

        // Remove the duplicated name from our names set.
        names.erase(name);
    }
    
    // Return names to caller.
    return names;
}

MegaClient& ClientAdapter::client() const
{
    return mClient;
}

LocalPath ClientAdapter::dbPath(const std::string& name) const
{
    // FUSE requires database support.
    assert(mClient.dbaccess);
    
    // Where should FUSE store its database?
    return mClient.dbaccess->databasePath(fsAccess(),
                                          name,
                                          DbAccess::DB_VERSION);
}

LocalPath ClientAdapter::dbRootPath() const
{
    // FUSE requires database support.
    assert(mClient.dbaccess);

    // Where is the client storing its databases?
    return mClient.dbaccess->rootPath();
}

void ClientAdapter::deinitialize()
{
    // Remember that we've been deinitialized.
    mDeinitialized = true;

    // Wait for any calls to complete.
    mActivities.waitUntilIdle();

    // Cancel any pending callbacks.
    mPendingCallbacks.cancel();

    // Acquire lock.
    std::unique_lock<std::mutex> lock(mLock);

    // Cancel outstanding tasks.
    while (!mTaskQueue.empty())
    {
        std::deque<Task> tasks;

        // Dequeue all tasks.
        mTaskQueue.dequeue(tasks, tasks.max_size());

        // Release lock.
        lock.unlock();

        // Cancel tasks.
        while (!tasks.empty())
        {
            tasks.back().cancel();
            tasks.pop_back();
        }

        // Reacquire lock.
        lock.lock();
    }
}

void ClientAdapter::dispatch()
{
    // Acquire lock.
    std::unique_lock<std::mutex> lock(mLock);

    // Execute ready tasks.
    while (mTaskQueue.ready())
    {
        // Pop a task from the queue.
        auto task = mTaskQueue.dequeue();

        // Release lock.
        lock.unlock();

        // Complete the task.
        task.complete();

        // Reacquire lock.
        lock.lock();
    }
}

void ClientAdapter::download(DownloadCallback callback,
                             NodeHandle handle,
                             const LocalPath& logicalPath,
                             const LocalPath& physicalPath)
{
    // Sanity.
    assert(callback);
    assert(!handle.isUndef());
    assert(!physicalPath.empty());

    // Asks the client to download the file.
    auto download = [this](DownloadCallback& callback,
                           NodeHandle handle,
                           LocalPath& logicalPath,
                           LocalPath& physicalPath,
                           const Task& task) {
        // Client's being torn down.
        if (task.cancelled())
            return callback(API_EINCOMPLETE);

        // Try and locate the node to be downloaded.
        auto node = mClient.nodeByHandle(handle);

        // Node doesn't exist.
        if (!node)
            return callback(API_ENOENT);

        // Node's not a file.
        if (node->type != FILENODE)
            return callback(API_EARGS);

        // Instantiate a download for the file.
        auto download = std::make_unique<ClientDownload>(std::move(callback),
                                                            logicalPath, 
                                                            *node,
                                                            physicalPath);

        // Couldn't start the download.
        if (!download->begin(mClient))
            return;

        // Download's now owned by the client.
        static_cast<void>(download.release());
    }; // download

    // Ask the client to download the file.
    execute(std::bind(std::move(download),
                      std::move(callback),
                      handle,
                      logicalPath,
                      physicalPath,
                      std::placeholders::_1));
}

void ClientAdapter::each(std::function<void(NodeInfo)> function,
                         NodeHandle handle) const
{
    // Sanity.
    assert(function);

    // Make sure deinitialize(...) waits for this call to complete.
    auto activity = mActivities.begin();

    // Client's being torn down.
    if (mDeinitialized)
        return;

    // Acquire RNT lock.
    std::lock_guard<std::recursive_mutex> guard(mClient.nodeTreeMutex);

    // Try and locate the specified node.
    auto node = mClient.nodeByHandle(handle);

    // Couldn't locate the specified node.
    if (!node)
        return;

    // Node isn't a directory.
    if (node->type == FILENODE)
        return;

    // Assume directory is read-only.
    auto permissions = RDONLY;

    // Directory's actually writable.
    if (mClient.checkaccess(node.get(), FULL))
        permissions = FULL;

    // Enumerate over the node's children.
    for (auto child : mClient.getChildren(node.get()))
    {
        NodeInfo info;

        // Latch the child's description.
        describe(info, permissions, *child);

        // Pass description to user callback.
        function(std::move(info));
    }
}

Task ClientAdapter::execute(std::function<void(const Task&)> function)
{
    // Sanity.
    assert(function);

    // Instantiate a new task.
    auto task = Task(std::move(function), mLogger);

    // Acquire lock.
    std::unique_lock<std::mutex> lock(mLock);

    // Client's being deinitialized.
    if (mDeinitialized)
    {
        // Release lock.
        lock.unlock();

        // Cancel the task.
        task.cancel();

        // Return task to caller.
        return task;
    }

    // Queue the task for execution.
    mTaskQueue.queue(task);

    // Release lock.
    lock.unlock();

    // Let the client know it has work to do.
    mClient.waiter->notify();

    // Return task to caller.
    return task;
}

bool ClientAdapter::exists(NodeHandle handle) const
{
    // Make sure deinitialize(...) waits for this call to complete.
    auto activity = mActivities.begin();

    // Client's being torn down.
    if (mDeinitialized)
        return false;

    // Acquire RNT lock.
    std::lock_guard<std::recursive_mutex> guard(mClient.nodeTreeMutex);

    // Check if the node exists.
    return !!mClient.nodeByHandle(handle);
}

FileSystemAccess& ClientAdapter::fsAccess() const
{
    return *mClient.fsaccess;
}

ErrorOr<NodeInfo> ClientAdapter::get(NodeHandle handle) const
{
    // Make sure deinitialize(...) waits for this call to complete.
    auto activity = mActivities.begin();

    // Client's being torn down.
    if (mDeinitialized)
        return unexpected(API_ENOENT);

    // Acquire RNT lock.
    std::lock_guard<std::recursive_mutex> guard(mClient.nodeTreeMutex);

    // Try and locate the specified node.
    auto node = mClient.nodeByHandle(handle);

    // Node doesn't exist.
    if (!node)
        return unexpected(API_ENOENT);

    // Return description to caller.
    return describe(*node);
}

ErrorOr<NodeInfo> ClientAdapter::get(NodeHandle parent,
                                     const std::string& name) const
{
    // Make sure deinitialize(...) waits for this call to complete.
    auto activity = mActivities.begin();

    // Client's being torn down.
    if (mDeinitialized)
        return unexpected(API_ENOENT);

    // Acquire RNT lock.
    std::lock_guard<std::recursive_mutex> guard(mClient.nodeTreeMutex);

    // Retrieve the child's description.
    if (auto node = child(mClient, parent, name))
        return describe(*node);

    // Parent or child doesn't exist.
    return unexpected(API_ENOENT);
}

NodeHandle ClientAdapter::handle(NodeHandle parent,
                                 const std::string& name) const
{
    // Make sure deinitialize(...) waits for this call to complete.
    auto activity = mActivities.begin();

    // Client's being torn down.
    if (mDeinitialized)
        return NodeHandle();

    // Acquire RNT lock.
    std::lock_guard<std::recursive_mutex> guard(mClient.nodeTreeMutex);

    // Retrieve the child's handle.
    if (auto node = child(mClient, parent, name))
        return node->nodeHandle();

    // Parent or child doesn't exist.
    return NodeHandle();
}

ErrorOr<bool> ClientAdapter::hasChildren(NodeHandle parent) const
{
    // Make sure deinitialize(...) waits for this call to complete.
    auto activity = mActivities.begin();

    // Client's being torn down.
    if (mDeinitialized)
        return API_ENOENT;

    // Acquire RNT lock.
    std::lock_guard<std::recursive_mutex> guard(mClient.nodeTreeMutex);

    // Try and locate the specified parent.
    auto parent_ = mClient.nodeByHandle(parent);

    // Parent doesn't exist.
    if (!parent_)
        return API_ENOENT;
    
    // How many children does the parent contain?
    return mClient.getNumberOfChildren(parent) > 0;
}

void ClientAdapter::initialize()
{
    // Clear deinitialization flag.
    mDeinitialized = false;
}

ErrorOr<bool> ClientAdapter::isFile(NodeHandle handle) const
{
    // Make sure deinitialize(...) waits for this call to complete.
    auto activity = mActivities.begin();

    // Client's being torn down.
    if (mDeinitialized)
        return unexpected(API_ENOENT);

    // Acquire RNT lock.
    std::lock_guard<std::recursive_mutex> guard(mClient.nodeTreeMutex);

    // Try and locate the specified node.
    auto node = mClient.nodeByHandle(handle);

    // Node doesn't exist.
    if (!node)
        return unexpected(API_ENOENT);

    // Let the caller know if the node's a file.
    return node->type == FILENODE;
}

void ClientAdapter::makeDirectory(MakeDirectoryCallback callback,
                                  const std::string& name,
                                  NodeHandle parent)
{
    // Responsible for making our new directory.
    auto make = [](MakeDirectoryCallback& callback,
                   MegaClient& client,
                   const std::string& name,
                   NodeHandle parent,
                   const Task& task) {
        // Client's being torn down.
        if (task.cancelled())
            return callback(unexpected(API_EINCOMPLETE));

        NewNodeVector nodes(1);

        // Describe our new node.
        client.putnodes_prepareOneFolder(&nodes[0], name, false);

        // Forwards result to our notiifer.
        auto created = [](MakeDirectoryCallback& callback,
                          MegaClient& client,
                          NewNodeVector& nodes,
                          Error result) {
            // Couldn't make the directory.
            if (result != API_OK)
                return callback(unexpected(result));

            // Convenience.
            auto handle = NodeHandle().set6byte(nodes[0].mAddedHandle);

            // Get our hands on the new node.
            auto node = client.nodeByHandle(handle);

            // Node doesn't exist.
            if (!node)
                return callback(unexpected(API_EINTERNAL));

            // Transmit node's description to caller.
            callback(describe(*node));
        }; // created

        // Ask the client to create our new directory.
        client.putnodes(parent,
                        UseLocalVersioningFlag,
                        std::move(nodes),
                        nullptr,
                        0,
                        false,
                        {}, // customerIpPort
                        std::bind(std::move(created),
                                  std::move(callback),
                                  std::ref(client),
                                  std::placeholders::_3,
                                  std::placeholders::_1));
    }; // make

    // Ask the client to make our new directory.
    execute(std::bind(std::move(make),
                      wrap(std::move(callback)),
                      std::ref(mClient),
                      name,
                      parent,
                      std::placeholders::_1));
}

void ClientAdapter::move(MoveCallback callback,
                         NodeHandle source,
                         NodeHandle target)
{
    // Sanity.
    assert(callback);
    assert(!source.isUndef());
    assert(!target.isUndef());

    // Actually moves the node.
    auto move = [=](MoveCallback& callback,
                    const Task& task) {
        // Client's being torn down.
        if (task.cancelled())
            return callback(API_EINCOMPLETE);

        // Get our hands on the nodes in question.
        auto source_ = mClient.nodeByHandle(source);
        auto target_ = mClient.nodeByHandle(target);

        // Either node no longer exists.
        if (!source_ || !target_)
            return callback(API_ENOENT);

        // Ask the client to move the node.
        auto result =
          mClient.rename(source_,
                         target_,
                         SYNCDEL_NONE,
                         NodeHandle(),
                         nullptr,
                         false,
                         std::bind(callback, std::placeholders::_2));

        // Client error.
        if (result != API_OK)
            callback(result);
    }; // move

    execute(std::bind(std::move(move),
                      wrap(std::move(callback)),
                      std::placeholders::_1));
}

bool ClientAdapter::isClientThread() const
{
    return std::this_thread::get_id() == mThreadID;
}

NodeHandle ClientAdapter::parentHandle(NodeHandle handle) const
{
    // Make sure deinitialize(...) waits for this call to complete.
    auto activity = mActivities.begin();

    // Client's being torn down.
    if (mDeinitialized)
        return NodeHandle();

    // Acquire RNT lock.
    std::lock_guard<std::recursive_mutex> guard(mClient.nodeTreeMutex);

    // Locate specified node.
    auto node = mClient.nodeByHandle(handle);

    // Node exists.
    if (node)
        return node->parentHandle();

    // Node doesn't exist.
    return NodeHandle();
}

auto ClientAdapter::partialDownload(PartialDownloadCallback& callback,
                                    NodeHandle handle,
                                    std::uint64_t offset,
                                    std::uint64_t length) -> ErrorOr<PartialDownloadPtr>
{
    // Check if the specified node exists and denotes a file.
    auto isFile = this->isFile(handle);

    // Node exists.
    if (isFile)
    {
        // And it denotes a file.
        if (*isFile)
            return std::make_shared<ClientPartialDownload>(callback, *this, handle, offset, length);

        // Node isn't a file.
        return unexpected(API_FUSE_EISDIR);
    }

    // Node doesn't exist or isn't accessible.
    return unexpected(isFile.error());
}

accesslevel_t ClientAdapter::permissions(NodeHandle handle) const
{
    // Make sure deinitialize(...) waits for this call to complete.
    auto activity = mActivities.begin();

    // Client's being torn down.
    if (mDeinitialized)
        return RDONLY;

    // Acquire RNT lock.
    std::lock_guard<std::recursive_mutex> guard(mClient.nodeTreeMutex);

    // Try and locate the specified node.
    auto node = mClient.nodeByHandle(handle);

    // Node doesn't exist.
    if (!node)
        return ACCESS_UNKNOWN;

    // Do we have full access to this node?
    if (mClient.checkaccess(node.get(), FULL))
        return FULL;

    // Node's effectively read-only.
    return RDONLY;
}

void ClientAdapter::remove(RemoveCallback callback, NodeHandle handle)
{
    // Actually removes the node.
    auto remove = [=](RemoveCallback& callback, const Task& task) {
        // Client's being torn down.
        if (task.cancelled())
            return callback(API_EINCOMPLETE);

        // Locate the specified node.
        auto node = mClient.nodeByHandle(handle);

        // Node doesn't exist.
        if (!node)
            return callback(API_ENOENT);

        // Ask the client to remove the node.
        auto result =
          mClient.unlink(node.get(),
                         false,
                         0,
                         false,
                         std::bind(callback, std::placeholders::_2));

        // Client error.
        if (result != API_OK)
            callback(result);
    }; // task

    // Ask the client to remove the node.
    execute(std::bind(std::move(remove),
                      wrap(std::move(callback)),
                      std::placeholders::_1));
}

void ClientAdapter::rename(RenameCallback callback,
                           const std::string& name,
                           NodeHandle handle)
{
    // Sanity.
    assert(callback);
    assert(!name.empty());
    assert(!handle.isUndef());

    // Actually renames the node.
    auto rename = [=](RenameCallback& callback,
                      const Task& task) {
        // Client's being torn down.
        if (task.cancelled())
            return callback(API_EINCOMPLETE);

        // Try and locate the specified node.
        auto node = mClient.nodeByHandle(handle);

        // Node doesn't exist.
        if (!node)
            return callback(API_ENOENT);

        // Node already has the specified name.
        if (node->hasName(name))
            return callback(API_OK);

        // Ask the client to rename the node.
        auto result =
          mClient.setattr(node,
                          attr_map('n', name),
                          std::bind(callback, std::placeholders::_2),
                          false);

        // Client error.
        if (result != API_OK)
            return callback(result);
    }; // rename

    // Ask the client to rename the node.
    execute(std::bind(std::move(rename),
                      wrap(std::move(callback)),
                      std::placeholders::_1));
}

std::string ClientAdapter::sessionID() const
{
    // Sanity.
    assert(mClient.sid.size() >= MegaClient::SIDLEN);

    // Extract session ID.
    auto id = mClient.sid.substr(sizeof(mClient.key.key));

    // Return ID to caller.
    return Base64::btoa(id);
}

void ClientAdapter::storageInfo(StorageInfoCallback callback)
{
    // Actually retrieves the user's storage info.
    auto getStorageInfo = [=](StorageInfoCallback& callback,
                              const Task& task) {
        // Client's being torn down.
        if (task.cancelled())
            return callback(unexpected(API_EINCOMPLETE));

        // Forward result to user callback.
        auto retrieved = [=](StorageInfoCallback& callback,
                            const StorageInfo& info,
                            Error result) {
            if (result != API_OK)
                return callback(unexpected(result));

            callback(info);
        }; // retrieved
        
        // Ask the client for our storage statistics.
        mClient.getstorageinfo(
          std::bind(std::move(retrieved),
                    std::move(callback),
                    std::placeholders::_1,
                    std::placeholders::_2));
    }; // getStorageInfo

    // Ask the client to retrieve our storage statistics.
    execute(std::bind(std::move(getStorageInfo),
                      wrap(std::move(callback)),
                      std::placeholders::_1));
}

void ClientAdapter::touch(TouchCallback callback,
                          NodeHandle handle,
                          m_time_t modified)
{
    // Sanity.
    assert(callback);
    assert(!handle.isUndef());

    // Actually updates the node's timestamp.
    auto touch = [=](TouchCallback& callback, const Task& task) {
        // Client's being torn down.
        if (task.cancelled())
            return callback(API_EINCOMPLETE);

        // Try and locate the specified node.
        auto node = mClient.nodeByHandle(handle);

        // Node doesn't exist.
        if (!node)
            return callback(API_ENOENT);

        // Node doesn't describe a file.
        if (node->type != FILENODE)
            return callback(API_FUSE_EISDIR);

        // Node's modification time hasn't changed.
        if (node->mtime == modified)
            return callback(API_OK);

        // Compute the node's new fingerprint attribute.
        auto attribute = ([&]() {
            // Grab the node's current fingerprint.
            auto fingerprint = node->fingerprint();

            // Update the modification time.
            fingerprint.mtime = modified;

            std::string attribute;

            // Serialize the fingerprint into an attribute.
            fingerprint.serializefingerprint(&attribute);

            // Return attribute to caller.
            return attribute;
        })();

        // Ask the client to update the node's attribute.
        auto result =
          mClient.setattr(node,
                          attr_map('c', std::move(attribute)),
                          std::bind(std::move(callback),
                                    std::placeholders::_2),
                          false);

        // Can't update the node's fingerprint attribute.
        if (result != API_OK)
            callback(result);
    }; // touch

    // Ask the client to update the node's modification time.
    execute(std::bind(std::move(touch),
                      wrap(std::move(callback)),
                      std::placeholders::_1));
}

void ClientAdapter::updated(const sharedNode_vector& nodes)
{
    // No observer? Nothing to do!
    if (!mEventObserver)
        return;

    // Translate node vector into event queue.
    ClientNodeEventQueue events(nodes);

    // Process node events.
    mEventObserver->updated(events);
}

UploadPtr ClientAdapter::upload(const LocalPath& logicalPath,
                                const std::string& name,
                                NodeHandle parent,
                                const LocalPath& physicalPath)
{
    // Sanity.
    assert(!name.empty());
    assert(!parent.isUndef());
    assert(!physicalPath.empty());

    // Instantiate an object to perform our upload.
    auto upload = std::make_shared<ClientUpload>(*this,
                                                 logicalPath,
                                                 parent,
                                                 name,
                                                 physicalPath);

    // Let the upload know about itself.
    upload->inject(upload);

    // Return the upload to the caller.
    return std::make_shared<ClientUploadAdapter>(std::move(upload));
}

bool ClientTransfer::isFuseTransfer() const
{
    return true;
}

void ClientDownload::completed(Transfer*, putsource_t)
{
    // Tell waiter that we've completed.
    mCallback(API_OK);

    // Delete ourselves.
    delete this;
}

void ClientDownload::terminated(mega::error result)
{
    // A terminated download should always be an error.
    if (result == API_OK)
        result = API_EINCOMPLETE;

    // Tell waiter that we encountered an error.
    mCallback(result);

    // Delete ourselves.
    delete this;
}

ClientDownload::ClientDownload(std::function<void(Error)> callback,
                               const LocalPath& logicalPath,
                               const Node& node,
                               const LocalPath& physicalPath)
  : ClientTransfer()
  , mCallback(std::move(callback))
{
    // What node do we want to download?
    h = node.nodeHandle();

    // Where should the user think we've downloaded the file?
    this->logicalPath(logicalPath);

    // What is the name of the file we're downloading?
    name = node.displayname();

    // Where do we want to save the node's content?
    setLocalname(physicalPath);

    // What are the file's current attributes?
    static_cast<FileFingerprint&>(*this) = node;
}

bool ClientDownload::begin(MegaClient& client)
{
    TransferDbCommitter committer(client.tctable);

    auto result = API_OK;

    // Try and start the transfer.
    client.startxfer(GET,
                     this,
                     committer,
                     false,
                     false,
                     true,
                     NoVersioning,
                     &result,
                     client.nextreqtag());

    // Transfer's been started.
    if (result == API_OK)
        return client.waiter->notify(), true;

    // Couldn't start the transfer.
    mCallback(result);

    return false;
}

std::shared_ptr<Node> child(MegaClient& client,
                            NodeHandle parent,
                            const std::string& name)
{
    // Locate specified parent.
    auto parent_ = client.nodeByHandle(parent);

    // Parent doesn't exist.
    if (!parent_)
        return nullptr;

    std::shared_ptr<Node> candidate;

    // Try and locate unique child.
    for (auto child : client.getChildren(parent_.get()))
    {
        // Child's name isn't a match.
        if (!child->hasName(name))
            continue;

        // Already seen an instance of this name.
        if (candidate)
            return nullptr;

        // Remember that we've seen this name.
        candidate = child;
    }

    // Return child (if any) to caller.
    return candidate;
}

void describe(NodeInfo& destination,
              accesslevel_t permissions,
              Node& source)
{
    // Populate description of node.
    destination.mIsDirectory = source.type != FILENODE;
    destination.mHandle = source.nodeHandle();
    destination.mModified = source.ctime;
    destination.mName = source.displayname();
    destination.mParentHandle = source.parentHandle();
    destination.mPermissions = permissions;
    destination.mSize = 4096;

    // Directories don't have a sane mtime or size.
    if (destination.mIsDirectory)
        return;

    destination.mModified = source.mtime;
    destination.mSize = source.size;
}

NodeInfo describe(Node& node)
{
    // Assume node is read-only.
    auto permissions = RDONLY;

    // Node's actually writable.
    if (node.client->checkaccess(&node, FULL))
        permissions = FULL;

    NodeInfo info;

    // Populate description of node.
    describe(info, permissions, node);

    // Return description to caller.
    return info;
}

ClientNodeEvent::ClientNodeEvent(sharedNode_vector::const_iterator position)
  : mPosition(position)
{
}

bool ClientNodeEvent::isDirectory() const
{
    return (*mPosition)->type != FILENODE;
}

NodeHandle ClientNodeEvent::handle() const
{
    return (*mPosition)->nodeHandle();
}

NodeInfo ClientNodeEvent::info() const
{
    return describe(**mPosition);
}

const std::string& ClientNodeEvent::name() const
{
    if (!(*mPosition)->hasName())
        return Node::CRYPTO_ERROR;

    auto& name = (*mPosition)->attrs.map.at('n');

    if (name.empty())
        return Node::BLANK;

    return name;
}

NodeHandle ClientNodeEvent::parentHandle() const
{
    return (*mPosition)->parentHandle();
}

NodeEventType ClientNodeEvent::type() const
{
    // Convenience.
    const auto& node = **mPosition;

    // Node's been added.
    if (node.changed.newnode)
        return NODE_EVENT_ADDED;

    // Node's been removed.
    if (node.changed.removed)
        return NODE_EVENT_REMOVED;

    // Node's been moved or renamed.
    if (node.changed.parent || node.changed.name)
        return NODE_EVENT_MOVED;

    // A share's permissions have changed.
    if (node.changed.inshare)
        return NODE_EVENT_PERMISSIONS;

    // Node's been modified in some unspecified way.
    return NODE_EVENT_MODIFIED;
}

ClientNodeEventQueue::ClientNodeEventQueue(const sharedNode_vector& events)
  : mCurrent(events.begin())
  , mEvents(events)
{
}

bool ClientNodeEventQueue::empty() const
{
    return mCurrent.mPosition == mEvents.end();
}

const ClientNodeEvent& ClientNodeEventQueue::front() const
{
    return mCurrent;
}

void ClientNodeEventQueue::pop_front()
{
    ++mCurrent.mPosition;
}

std::size_t ClientNodeEventQueue::size() const
{
    return mEvents.size();
}

void ClientPartialDownload::completed(Error result)
{
    // Acquire lock if necessary.
    auto lock = this->lock<std::unique_lock>();

    // Download's already been completed.
    if (!mExecuting && (mStatus & SF_COMPLETED))
        return;

    // Download's completed.
    mStatus = SF_COMPLETED;

    // Download's effectively cancelled.
    if (result == API_EINCOMPLETE)
        mStatus |= SF_CANCELLED;

    // Signal that we're executing a callback.
    auto executing = makeScopedValue(mExecuting, true);

    // Let the user know the download's completed.
    mCallback.completed(result);
}

void ClientPartialDownload::data(Data& data)
{
    // Assume the download should be terminated.
    data.ret = false;

    // Convenience.
    auto buffer = reinterpret_cast<const void*>(data.buffer);
    auto offset = static_cast<std::uint64_t>(data.offset);
    auto length = static_cast<std::uint64_t>(data.len);

    // Clamp length.
    length = std::min(length, mRemaining);

    // Pass data to the user callback.
    mCallback.data(buffer, offset, length);

    // Figure out how many bytes we still have to download.
    mRemaining -= length;

    // The user's callback cancelled the download.
    if (cancelled())
        return completed(API_EINCOMPLETE);

    // We've got all the data we asked for.
    if (!mRemaining)
        return completed(API_OK);

    // Continue the download.
    data.ret = true;
}

void ClientPartialDownload::failure(Failure& failure)
{
    // Assume the download will be terminated.
    failure.ret = NEVER;

    // Client's being torn down or the read's been aborted.
    if (failure.e == API_EINCOMPLETE)
        return completed(API_EINCOMPLETE);

    // Dispatch the callback.
    auto result = mCallback.failed(failure.e, failure.retry);

    // The user's callback cancelled the download.
    if (cancelled())
        return completed(API_EINCOMPLETE);

    // Convenience.
    using Abort = PartialDownloadCallback::Abort;
    using Retry = PartialDownloadCallback::Retry;

    // Let the SDK know if it should abort the download or retry.
    std::visit(overloaded{[&](const Abort&)
                          {
                              // Let the user know why the read has completed.
                              completed(failure.e);
                          },
                          [&](const Retry& retry)
                          {
                              // Retry in mWhen ds.
                              failure.ret = retry.mWhen.count();
                          }},
               result);
}

bool ClientPartialDownload::inProgress()
{
    auto guard = this->lock<std::unique_lock>();

    // Download's already begun or already completed.
    if (mStatus != SF_CANCELLABLE)
        return false;

    // Signal that the download is in progress.
    mStatus |= SF_IN_PROGRESS;

    // Let the caller know the download's now in progress.
    return true;
}

template<template<typename> typename Lock>
Lock<std::shared_mutex> ClientPartialDownload::lock() const
{
    Lock<std::shared_mutex> lock(mLock, std::defer_lock);

    // Acquire the lock if we're not executing within a callback.
    if (!mExecuting)
        lock.lock();

    // Return the lock.
    return lock;
}

void ClientPartialDownload::notify(PartialDownloadWeakPtr cookie, Event& event)
{
    // Convenience.
    using Revoke = DirectRead::Revoke;
    using Valid = DirectRead::IsValid;

    // Try and get a reference to ourselves.
    auto download = std::static_pointer_cast<ClientPartialDownload>(cookie.lock());

    // Download's been destroyed.
    if (!download)
    {
        // Let the SDK know it can terminate the download.
        return std::visit(overloaded{[&](Data& data)
                                     {
                                         data.ret = false;
                                     },
                                     [&](Failure& failure)
                                     {
                                         failure.ret = NEVER;
                                     },
                                     [&](Revoke& revoke)
                                     {
                                         revoke.ret = true;
                                     },
                                     [&](Valid& valid)
                                     {
                                         valid.ret = false;
                                     }},
                          event);
    }

    // Acquire the lock so other threads must wait to cancel us.
    auto lock = download->lock<std::unique_lock>();

    // We're executing in the context of a callback.
    auto executing = makeScopedValue(download->mExecuting, true);

    // Dispatch the event.
    std::visit(overloaded{[&](Data& data)
                          {
                              // Process the data only if we haven't been cancelled.
                              if (!download->cancelled())
                                  return download->data(data);

                              data.ret = false;
                          },
                          [&](Failure& failure)
                          {
                              // Handle the failure if we haven't been cancelled.
                              if (!download->cancelled())
                                  return download->failure(failure);

                              failure.ret = NEVER;
                          },
                          [&](Revoke& revoke)
                          {
                              // Never revoked by intermediate layer.
                              revoke.ret = false;
                          },
                          [&](Valid& valid)
                          {
                              // We're valid if we're sane.
                              valid.ret = !download->cancelled();
                          }},
               event);
}

ClientPartialDownload::ClientPartialDownload(PartialDownloadCallback& callback,
                                             ClientAdapter& client,
                                             NodeHandle handle,
                                             std::uint64_t offset,
                                             std::uint64_t length):
    PartialDownload(),
    enable_shared_from_this(),
    mCallback(callback),
    mClient(client),
    mHandle(handle),
    mLock(),
    mOffset(offset),
    mRemaining(length),
    mStatus(SF_CANCELLABLE)
{}

ClientPartialDownload::~ClientPartialDownload()
{
    // Cancel the download if necessary.
    cancel();
}

void ClientPartialDownload::begin()
{
    // Do nothing if we're being invoked from a callback.
    if (mExecuting)
        return;

    // Download's already begun or already been completed.
    if (!inProgress())
        return;

    // So we can test later whether the user's destroyed this instance.
    auto cookie = weak_from_this();

    // Try and begin the download.
    mClient.execute(
        [=](const Task& task) mutable
        {
            // Client's being torn down.
            if (task.cancelled())
                return;

            // Check whether this download is still alive.
            auto download = cookie.lock();

            // Download's been destroyed.
            if (!download)
                return;

            // Download's been cancelled.
            if (cancelled())
                return;

            // Get our hands on the node we want to download.
            auto node = mClient.client().nodeByHandle(mHandle);

            // Node doesn't exist.
            if (!node)
                return completed(API_ENOENT);

            // Convenience.
            auto size = static_cast<std::uint64_t>(node->size);

            // Sanitize the user's offset and length.
            mOffset = std::min(mOffset, size);
            mRemaining = std::min(mRemaining, size - mOffset);

            // Sanitized length is zero so complete the download early.
            if (!mRemaining)
                return completed(API_OK);

            // Begin the download.
            mClient.client().pread(node.get(),
                                   static_cast<m_off_t>(mOffset),
                                   static_cast<m_off_t>(mRemaining),
                                   std::bind(&ClientPartialDownload::notify,
                                             std::move(cookie),
                                             std::placeholders::_1));
        });
}

bool ClientPartialDownload::cancel()
{
    // Acquire lock if necessary.
    auto lock = this->lock<std::unique_lock>();

    // Download's already been completed.
    if ((mStatus & SF_COMPLETED))
        return false;

    // Signal that the download has been cancelled.
    mStatus = SF_CANCELLED | SF_COMPLETED;

    // Bail if we're executing within another callback.
    //
    // The user's completed(...) callback will be executed when they return
    // control directly back to us.
    //
    // For instance, if they have called cancel(...) from their data(...)
    // callback, we'll execute their completed(...) callback when they
    // return control back to us from data(...).
    if (mExecuting)
        return true;

    // Signal that we're executing a callback.
    auto executing = makeScopedValue(mExecuting, true);

    // Invoke the user's callback.
    mCallback.completed(API_EINCOMPLETE);

    // Let the caller know the download's been cancelled.
    return true;
}

bool ClientPartialDownload::cancellable() const
{
    // Acquire lock if necessary.
    auto lock = this->lock<std::shared_lock>();

    // Check if we can be cancelled.
    return (mStatus & SF_CANCELLABLE) > 0;
}

bool ClientPartialDownload::cancelled() const
{
    // Acquire lock if necessary.
    auto lock = this->lock<std::shared_lock>();

    // Check if we've been cancelled.
    return (mStatus & SF_CANCELLED) > 0;
}

bool ClientPartialDownload::completed() const
{
    // Acquire lock if necessary.
    auto lock = this->lock<std::shared_lock>();

    // Check if we've been completed.
    return (mStatus & SF_COMPLETED) > 0;
}

void ClientUpload::bind(BoundCallback callback,
                        FileNodeKey fileKey,
                        NodeHandle lastHandle,
                        ClientUploadPtr self,
                        UploadHandle uploadHandle,
                        std::string fileAttr,
                        UploadToken uploadToken)
{
    static NewNodeVector empty;

    // Sanity.
    assert(callback);

    StatusFlags expected = SF_CANCELLABLE;

    // The upload's been cancelled.
    if (!mStatus.compare_exchange_weak(expected, 0u))
        return bound(std::move(callback),
                     empty,
                     false,
                     mResult);

    // Binds our data to a name.
    auto bind = [this](BoundCallback& callback,
                       FileNodeKey& fileKey,
                       NodeHandle lastHandle,
                       const Task& task,
                       ClientUploadPtr self,
                       UploadHandle& uploadHandle,
                       std::string& fileAttr,
                       UploadToken& uploadToken)
    {
        // Client's being torn down.
        if (task.cancelled())
            return bound(std::move(callback),
                         empty,
                         false,
                         API_EINCOMPLETE);

        // Ask the client to bind a name to our data.
        sendPutnodesOfUpload(&mClient.client(),
                             std::move(uploadHandle),
                             std::move(fileAttr),
                             std::move(uploadToken),
                             std::move(fileKey),
                             PUTNODES_APP,
                             lastHandle,
                             std::bind(&ClientUpload::bound,
                                       std::move(self),
                                       mClient.wrap(std::move(callback)),
                                       std::placeholders::_3,
                                       std::placeholders::_4,
                                       std::placeholders::_1),
                             nullptr,
                             false);
    }; // bind

    // Called when our content has been bound to a name.
    auto bound = [this](BoundCallback& callback,
                        ErrorOr<NodeHandle> result) {
        // Mark upload as having been completed.
        mStatus.store(SF_COMPLETED);

        // Forward result to user callback.
        callback(result);
    }; // bound

    // Wrap user callback.
    callback = std::bind(std::move(bound),
                         std::move(callback),
                         std::placeholders::_1);

    // Ask the client to bind a name to our data.
    mClient.execute(std::bind(std::move(bind),
                              std::move(callback),
                              std::move(fileKey),
                              lastHandle,
                              std::placeholders::_1,
                              std::move(self),
                              std::move(uploadHandle),
                              std::move(fileAttr),
                              std::move(uploadToken)));
}

void ClientUpload::bound(BoundCallback callback,
                         NewNodeVector& nodes,
                         bool overridden,
                         Error result)
{
    // Assume we couldn't bind the content to a name.
    ErrorOr<NodeHandle> handle = unexpected(result);

    // Mark upload as having been completed.
    mStatus.store(SF_COMPLETED);

    // Content was actually bound to a name.
    if (result == API_OK)
        handle = NodeHandle().set6byte(nodes.front().mAddedHandle);

    // Forward result to user callback.
    callback(handle);

    // Notify application directly if we're on the client thread.
    if (mClient.isClientThread())
        return mClient.client().app->putnodes_result(result,
                                                     NODE_HANDLE,
                                                     nodes,
                                                     overridden,
                                                     tag);

    // Invokes application callback.
    auto wrapper = [](MegaApp& application,
                      NewNodeVectorPtr& nodes,
                      bool overridden,
                      Error result,
                      int ownTag,
                      const Task&)
    {
        application.putnodes_result(result, NODE_HANDLE, *nodes, overridden, ownTag);
    }; // wrapper

    // Wrapper takes ownership of the new nodes.
    auto nodes_ = std::make_shared<NewNodeVector>(std::move(nodes));

    // Notify application on the client thread.
    mClient.execute(std::bind(std::move(wrapper),
                              std::ref(*mClient.client().app),
                              std::move(nodes_),
                              overridden,
                              result,
                              tag,
                              std::placeholders::_1));
}

void ClientUpload::completed(Transfer* upload, putsource_t)
{
    // Sanity.
    assert(upload);

    std::string fileAttr;
    mClient.client().pendingattrstring(upload->uploadhandle, &fileAttr);

    // Instantiate bind callback.
    BindCallback bind = std::bind(&ClientUpload::bind,
                                  this,
                                  std::placeholders::_1,
                                  upload->filekey,
                                  std::placeholders::_2,
                                  std::move(mSelf),
                                  upload->uploadhandle,
                                  std::move(fileAttr),
                                  *upload->ultoken);

    // Latch callback.
    auto callback = std::move(mCallback);

    // Let the user know they can bind a name to their data.
    callback(std::move(bind));
}

void ClientUpload::terminated(mega::error result)
{
    // Make sure we always have a sane result.
    mResult = result == API_OK ? API_EINCOMPLETE : result;

    // Signal that the upload has completed.
    mStatus |= SF_COMPLETED;

    // Let the user know the upload failed.
    mCallback(unexpected(mResult));

    // Let ourselves be destroyed.
    mSelf.reset();
}

ClientUpload::ClientUpload(ClientAdapter& client,
                           const LocalPath& logicalPath,
                           NodeHandle parentHandle,
                           const std::string& name,
                           const LocalPath& physicalPath)
  : ClientTransfer()
  , mCallback()
  , mClient(client)
  , mResult(API_OK)
  , mSelf()
  , mStatus{SF_CANCELLABLE}
{
    // Sanity.
    assert(!parentHandle.isUndef());
    assert(!name.empty());
    assert(!physicalPath.empty());

    // Who will be the parent of our new node?
    h = parentHandle;

    // What file should we say we are uploading?
    this->logicalPath(logicalPath);

    // What file are we uploading?
    setLocalname(physicalPath);

    // What shall our new node be called?
    this->name = name;
}

ClientUpload::~ClientUpload()
{
    static NewNodeVector empty;

    // File's been uploaded but hasn't been bound.
    if (!mStatus)
        bound([](ErrorOr<NodeHandle>) { },
              empty,
              false,
              API_EINCOMPLETE);
}

void ClientUpload::begin(UploadCallback callback)
{
    // Make sure the upload hasn't already been started.
    assert(!mCallback);

    // Squirrel away the upload callback.
    mCallback = std::move(callback);

    // Ask the client to begin the upload.
    mClient.execute([this](const Task& task) {
        // Client's being torn down.
        if (task.cancelled())
            return terminated(API_EINCOMPLETE);

        // We've been cancelled.
        if (cancelled())
            return;

        // Convenience.
        auto& client = mClient.client();

        // So we can alter the transfer database.
        TransferDbCommitter committer(client.tctable);

        auto result = API_OK;

        // Try and begin the upload.
        client.startxfer(PUT,
                         this,
                         committer,
                         false,
                         false,
                         true,
                         UseServerVersioningFlag,
                         &result,
                         client.nextreqtag());

        // Couldn't begin the upload.
        if (result != API_OK)
            return terminated(result);

        // Let the client know it has work to do.
        client.waiter->notify();
    });
}

bool ClientUpload::cancel()
{
    StatusFlags desired  = SF_CANCELLED | SF_COMPLETED;
    StatusFlags expected = SF_CANCELLABLE;

    // Upload's not in a cancellable state.
    //
    // Say, the upload's been completed or we're in the
    // process of binding the uploaded data to a name.
    if (!mStatus.compare_exchange_weak(expected, desired))
        return false;

    auto terminate = [](ClientUploadWeakPtr cookie, const Task& task) {
        // Client's being torn down.
        if (task.cancelled())
            return;

        // Make sure we're still alive.
        auto self = cookie.lock();

        // We've been released.
        if (!self)
            return;

        // Get our hands on the real client.
        auto& client = self->mClient.client();

        // So we can modify the transfer database.
        TransferDbCommitter committer(client.tctable);

        // Stop the upload.
        client.stopxfer(self.get(), &committer);
    }; // terminate

    // Ask the client to terminate the upload.
    mClient.execute(
      std::bind(std::move(terminate),
                ClientUploadWeakPtr(mSelf),
                std::placeholders::_1));

    // Let the caller know the upload's been cancelled.
    return true;
}

bool ClientUpload::cancelled() const
{
    return (mStatus & SF_CANCELLED) > 0;
}

bool ClientUpload::completed() const
{
    return (mStatus & SF_COMPLETED) > 0;
}

void ClientUpload::inject(ClientUploadPtr self)
{
    assert(self);
    assert(self.get() == this);

    mSelf = std::move(self);
}

Error ClientUpload::result() const
{
    return mResult;
}

ClientUploadAdapter::ClientUploadAdapter(ClientUploadPtr upload)
  : Upload()
  , mUpload(std::move(upload))
{
}

void ClientUploadAdapter::begin(UploadCallback callback)
{
    return mUpload->begin(std::move(callback));
}

bool ClientUploadAdapter::cancel()
{
    return mUpload->cancel();
}

bool ClientUploadAdapter::cancelled() const
{
    return mUpload->cancelled();
}

bool ClientUploadAdapter::completed() const
{
    return mUpload->completed();
}

Error ClientUploadAdapter::result() const
{
    return mUpload->result();
}

} // common
} // mega

