#include <atomic>
#include <future>
#include <mutex>

#include <mega/common/error_or.h>
#include <mega/common/node_info.h>
#include <mega/common/normalized_path.h>
#include <mega/common/normalized_path.h>
#include <mega/common/upload.h>
#include <mega/common/utility.h>
#include <mega/fuse/common/client.h>
#include <mega/fuse/common/inode_info.h>
#include <mega/fuse/common/logging.h>
#include <mega/fuse/common/mount_event.h>
#include <mega/fuse/common/mount_info.h>
#include <mega/fuse/common/service.h>
#include <mega/fuse/common/testing/client.h>
#include <mega/fuse/common/testing/cloud_path.h>
#include <mega/fuse/common/testing/mount_event_observer.h>

#include <tests/integration/test.h>
#include <tests/integration/env_var_accounts.h>

namespace mega
{
namespace fuse
{
namespace testing
{

using namespace common;

class Client::Uploader
{
    // Called when a directory has been made or a file uploaded.
    void completed(std::unique_lock<std::mutex> lock,
                   ErrorOr<NodeHandle> result);

    void completed(ErrorOr<NodeHandle> result);

    // Called when a directory has been made.
    void made(Path& path, ErrorOr<NodeHandle> result);

    // Makes a new direcory.
    void make(const Path& path, NodeHandle parentHandle);

    // Uploads a file.
    void upload(Path path, NodeHandle parentHandle);

    // Called when a file has been uploaded.
    void uploaded(const Path& path, ErrorOr<NodeHandle> result);

    // What client's performing our uploads?
    Client& mClient;

    // Serializes access to members.
    std::mutex mLock;

    // Signalled when the upload has completed.
    std::promise<void> mNotifier;

    // What directories are being made?
    std::atomic<std::size_t> mPendingDirectories;

    // What files are being uploaded?
    std::map<Path, UploadPtr> mPendingFiles;

    // Tracks overall result of the upload.
    std::atomic<error> mResult;

public:
    Uploader(Client& client);

    // Uploads the directory tree.
    ErrorOr<NodeHandle> operator()(const std::string& name,
                                   NodeHandle parentHandle,
                                   Path path);
}; // Uploader

NodeHandle Client::handle(NodeHandle parent, const std::string& name) const
{
    return client().handle(parent, name);
}

void Client::makeDirectory(MakeDirectoryCallback callback,
                           const std::string& name,
                           NodeHandle parentHandle)
{
    // Called when our directory has been made.
    auto made = [this](MakeDirectoryCallback& callback,
                       ErrorOr<NodeInfo> result) {
        // Invokes our callback in a safe context.
        auto wrapper = [](MakeDirectoryCallback& callback,
                          ErrorOr<NodeInfo> result,
                          const Task& task) {
            // Client's being torn down.
            if (task.cancelled())
                return callback(unexpected(API_EINCOMPLETE));

            // Couldn't make the directory.
            if (!result)
                return callback(unexpected(result.error()));

            // Directory's been made.
            callback(result->mHandle);
        }; // wrapper

        // Invoke the callback in a safe context.
        service().execute(std::bind(std::move(wrapper),
                                    std::move(callback),
                                    std::move(result),
                                    std::placeholders::_1));
    }; // result

    // Try and make the directory.
    client().makeDirectory(std::bind(std::move(made),
                                     std::move(callback),
                                     std::placeholders::_1),
                           name,
                           parentHandle);
}

ErrorOr<NodeHandle> Client::uploadFile(const std::string& name,
                                       NodeHandle parentHandle,
                                       const Path& path)
{
    // Create the upload.
    auto upload = client().upload(LocalPath(),
                                  name,
                                  parentHandle,
                                  path);

    // So we can wait for the upload's result.
    auto notifier = makeSharedPromise<ErrorOr<NodeHandle>>();

    // Called when our file has been bound.
    BoundCallback bound = [notifier](ErrorOr<NodeHandle> result) {
        // Broadcast result to our waiter.
        notifier->set_value(result);
    }; // bound

    // Try and upload the file.
    upload->begin(std::move(bound));

    // Return the upload's result to the caller.
    return waitFor(notifier->get_future());
}

Client::Client(const std::string&, const Path& databasePath, const Path& storagePath):
    mMountEventObservers(),
    mMountEventObserversLock(),
    mNodesCurrent(false),
    mNodesCurrentCV(),
    mNodesCurrentLock(),
    mDatabasePath(databasePath),
    mStoragePath(storagePath)
{
}

Client::~Client()
{
}

void Client::mountEvent(const MountEvent& event)
{
    std::lock_guard<std::mutex> guard(mMountEventObserversLock);

    // Inform observers that an event has been emitted.
    auto i = mMountEventObservers.begin();

    while (i != mMountEventObservers.end())
    {
        // Check if the observer's still alive.
        auto observer = i->lock();

        // Observer's dead.
        if (!observer)
        {
            i = mMountEventObservers.erase(i);
            continue;
        }

        // Let the observer know an event has been emitted.
        observer->emitted(event);

        // Move to the next observer.
        ++i;
    }
}

void Client::nodesCurrent(bool nodesCurrent)
{
    // Safely set the value of mNodesCurrent.
    {
        std::lock_guard<std::mutex> guard(mNodesCurrentLock);
        mNodesCurrent = nodesCurrent;
    }

    // Notify waiters.
    if (nodesCurrent)
        mNodesCurrentCV.notify_all();
}

MountResult Client::addMount(const MountInfo& info)
{
    return service().add(info);
}

std::set<std::string> Client::childNames(CloudPath path) const
{
    // Try and resolve the parent's handle.
    auto parentHandle = path.resolve(*this);

    // Parent doesn't exist.
    if (parentHandle.isUndef())
        return std::set<std::string>();

    // Retrieve the names of the parent's children.
    return client().childNames(parentHandle);
}

ErrorOr<InodeInfo> Client::describe(const Path& path) const
{
    return service().describe(path.localPath());
}

void Client::desynchronize(::mega::handle id)
{
    client().desynchronize(id);
}

MountResult Client::disableMount(const std::string& name, bool remember)
{
    // So we can wait for the mount to be disabled.
    std::promise<MountResult> notifier;

    // Called when the mount has been disabled.
    auto disabled = [&notifier](MountResult result) {
        notifier.set_value(result);
    }; // disabled

    // Try and disable the mount.
    service().disable(std::move(disabled),
                      name,
                      remember);

    // Wait for the mount to be disabled.
    auto result = notifier.get_future().get();

    // Couldn't disable the mount.
    if (result != MOUNT_SUCCESS)
        FUSEErrorF("Couldn't disable mount: %s: %s",
                   name.c_str(),
                   toString(result));

    // Return the result to the caller.
    return result;
}

MountResult Client::disableMounts(bool remember)
{
    // What mounts are currently enabled?
    auto mounts = this->mounts(true);

    // No mounts are enabled.
    if (mounts.empty())
        return MOUNT_SUCCESS;

    // How long should we wait for a mount to become idle?
    constexpr auto idleTime = std::chrono::seconds(4);

    // How many times should we try and disable a mount?
    constexpr auto numAttempts = 4;

    // Try and disable each mount.
    while (!mounts.empty())
    {
        // Pick a mount to disable.
        auto& mount = mounts.back();

        // Keep trying to disable the mount if necessary.
        auto disabled = [](MountResult result) {
            return result == MOUNT_UNKNOWN
                   || result == MOUNT_SUCCESS;
        }; // disabled

        // Try and disable the mount.
        auto result = disableMount(mount.name(), remember);

        // Keep trying to disable the mount if necessary.
        for (auto attempts = 0;
             !disabled(result) && attempts < numAttempts;
             ++attempts)
        {
            // Give the mount a little time to become idle.
            std::this_thread::sleep_for(idleTime);

            // Try and disable the mount again.
            result = disableMount(mount.name(), remember);
        }

        // Try and disable the next mount.
        mounts.pop_back();
    }

    // We weren't able to disable all the mounts.
    if (!mounts.empty())
        return MOUNT_BUSY;

    // All mounts have been disabled.
    return MOUNT_SUCCESS;
}

MountResult Client::discard(bool discard)
{
    return service().discard(discard);
}

MountResult Client::enableMount(const std::string& name, bool remember)
{
    const auto ret = service().enable(name, remember);
    // Tell FUSE mount that we need to access mount
    if (const auto flags = service().flags(name); flags)
    {
        flags->mAllowSelfAccess = true;
        service().flags(name, *flags);
    }
    return ret;
}

Task Client::execute(std::function<void(const Task&)> function)
{
    // Sanity.
    assert(function);

    // Queue the function for execution.
    return client().execute(std::move(function));
}

ErrorOr<NodeInfo> Client::get(CloudPath parentPath,
                              const std::string& name) const
{
    // Try and resolve the parent's handle.
    auto parentHandle = parentPath.resolve(*this);

    // Parent doens't exist.
    if (parentHandle.isUndef())
        return unexpected(API_ENOENT);

    // Try and get info about the specified child.
    return client().get(parentHandle, name);
}

ErrorOr<NodeInfo> Client::get(CloudPath path) const
{
    auto handle = path.resolve(*this);

    if (!handle.isUndef())
        return client().get(handle);

    return unexpected(API_ENOENT);
}

NodeHandle Client::handle(CloudPath parentPath,
                          const std::string& name) const
{
    // Resolve the parent's handle.
    auto parentHandle = parentPath.resolve(*this);

    // Parent doesn't exist.
    if (parentHandle.isUndef())
        return NodeHandle();

    // Try and retrieve the child's handle.
    return client().handle(parentHandle, name);
}

NodeHandle Client::handle(const std::string& path) const
{
    // Try and locate the specified node.
    auto info = client().lookup(RemotePath(path), rootHandle());

    // Found the specified node.
    if (info)
        return info->mHandle;

    // Couldn't locate the specified node.
    return NodeHandle();
}

bool Client::isCached(const Path& path) const
{
    return service().cached(path.localPath());
}

Error Client::login(std::size_t accountIndex)
{
    if (accountIndex >= getEnvVarAccounts().size())
        return API_EFAILED;

    // Extract email, password from environment.
    const auto [email, password] = getEnvVarAccounts().getVarValues(accountIndex);

    // Email and/or password isn't present.
    if (email.empty() || password.empty())
        return API_EFAILED;

    // Try and log the user in.
    return login(email, password);
}

ErrorOr<NodeHandle> Client::makeDirectory(const std::string& name,
                                          CloudPath parent)
{
    assert(!name.empty());

    auto parentHandle = parent.resolve(*this);

    if (parentHandle.isUndef())
        return unexpected(API_ENOENT);

    auto result = client().makeDirectory(name, parentHandle);

    if (result)
        return result->mHandle;

    return unexpected(result.error());
}

MountEventObserverPtr Client::mountEventObserver()
{
    auto observer = MountEventObserver::create();

    std::lock_guard<std::mutex> guard(mMountEventObserversLock);

    mMountEventObservers.emplace(observer);

    return observer;
}

bool Client::mountEnabled(const std::string& name) const
{
    return service().enabled(name);
}

MountResult Client::mountFlags(const std::string& name, const MountFlags& flags)
{
    return service().flags(name, flags);
}

MountFlagsPtr Client::mountFlags(const std::string& name) const
{
    return service().flags(name);
}

MountInfoPtr Client::mountInfo(const std::string& name) const
{
    return service().get(name);
}

NormalizedPath Client::mountPath(const std::string& name) const
{
    return service().path(name);
}

MountInfoVector Client::mounts(bool onlyEnabled) const
{
    return service().get(onlyEnabled);
}

Error Client::move(const std::string& name,
                   CloudPath source,
                   CloudPath target)
{
    // Sanity.
    assert(!name.empty());

    auto sourceHandle = source.resolve(*this);
    auto targetHandle = target.resolve(*this);

    // Source and/or target doesn't exist.
    if (sourceHandle.isUndef() || targetHandle.isUndef())
        return API_ENOENT;

    // Move the node.
    return client().move(name, sourceHandle, targetHandle);
}

auto Client::partialDownload(PartialDownloadCallback& callback,
                             CloudPath path,
                             std::uint64_t offset,
                             std::uint64_t length) -> ErrorOr<PartialDownloadPtr>
{
    auto handle = path.resolve(*this);

    if (!handle.isUndef())
        return client().partialDownload(callback, handle, offset, length);

    return unexpected(API_ENOENT);
}

Error Client::remove(CloudPath path)
{
    auto handle = path.resolve(*this);

    if (!handle.isUndef())
        return client().remove(handle);

    return API_ENOENT;
}

Error Client::removeAll(CloudPath path)
{
    auto handle = path.resolve(*this);

    if (!handle.isUndef())
        return client().removeAll(handle);

    return API_ENOENT;
}

MountResult Client::removeMount(const std::string& name)
{
    // Try and remove the mount.
    auto result = service().remove(name);

    // Couldn't remove the mount.
    if (result != MOUNT_SUCCESS)
        FUSEErrorF("Unable to remove mount: %s: %s",
                   name.c_str(),
                   toString(result));

    // Return result to caller.
    return result;
}

MountResult Client::removeMounts(bool disable)
{
    auto result = MOUNT_SUCCESS;

    // Disable enabled mounts if requested.
    if (disable)
        result = disableMounts(true);

    // Mounts couldn't be disabled.
    if (result != MOUNT_SUCCESS)
        return result;

    // What mounts are known to us?
    auto mounts = this->mounts(false);

    // Try and remove each mount.
    while (!mounts.empty())
    {
        // Select a mount to remove.
        auto& mount = mounts.back();

        // Try and remove the mount.
        auto result = removeMount(mount.name());

        // Couldn't remove the mount.
        if (result != MOUNT_SUCCESS)
            return result;

        // Mount's been removed.
        mounts.pop_back();
    }

    // All mounts have been removed.
    return result;
}

Error Client::replace(CloudPath source,
                      CloudPath target)
{
    auto sourceHandle = source.resolve(*this);
    auto targetHandle = target.resolve(*this);

    // Source and/or target doesn't exist.
    if (sourceHandle.isUndef() || targetHandle.isUndef())
        return API_ENOENT;

    // Replace target with source.
    return client().replace(sourceHandle, targetHandle);
}

ErrorOr<StorageInfo> Client::storageInfo()
{
    return client().storageInfo();
}

const Path& Client::storagePath() const
{
    return mStoragePath;
}

auto Client::synchronize(const Path& path, CloudPath target)
  -> std::tuple<::mega::handle, Error, SyncError>
{
    return client().synchronize(path.localPath(), target.resolve(*this));
}

ErrorOr<NodeHandle> Client::upload(const std::string& name,
                                   CloudPath parent,
                                   const Path& path)
{
    // Sanity.
    assert(!name.empty());

    // Resolve parent node.
    auto parentHandle = parent.resolve(*this);

    // Parent doesn't exist.
    if (parentHandle.isUndef())
        return unexpected(API_ENOENT);

    std::error_code error;

    // What kind of entity are we uploading?
    auto status = fs::status(path, error);

    // Couldn't determine type of entity.
    if (error)
        return unexpected(API_EREAD);

    // Upload the entity.
    switch (status.type())
    {
    case fs::file_type::directory:
        return Uploader(*this)(name, parentHandle, path);
    case fs::file_type::regular:
        return uploadFile(name, parentHandle, path);
    default:
        break;
    }

    // Can't upload something that isn't a directory or file.
    return unexpected(API_EARGS);
}

ErrorOr<NodeHandle> Client::upload(CloudPath parent,
                                   const Path& path)
{
    return upload(path.localPath().leafName().toPath(false),
                  parent,
                  path);
}

Error Client::waitForNodesCurrent(TimePoint when)
{
    // Acquire lock.
    std::unique_lock<std::mutex> lock(mNodesCurrentLock);

    // Checks if our view of the cloud is current.
    auto isCurrent = [&]() {
        return mNodesCurrent;
    }; // isCurrent

    // Wait until when for our view to become current.
    if (!mNodesCurrentCV.wait_until(lock, when, isCurrent))
        return API_EFAILED;

    // Our view of the cloud is current.
    return API_OK;
}

void Client::Uploader::completed(std::unique_lock<std::mutex> lock,
                                 ErrorOr<NodeHandle> result)
{
    // Operation couldn't be completed.
    if (!result && mResult == API_OK)
    {
        // Latch error.
        mResult = result.error();

        // Try and cancel any pending uploads.
        for (auto& f : mPendingFiles)
            f.second->cancel();
    }

    // Some directories are still being made.
    if (mPendingDirectories)
        return;

    // Some files are still being uploaded.
    if (!mPendingFiles.empty())
        return;

    // Release lock.
    lock.unlock();

    // Upload's complete.
    mNotifier.set_value();
}

void Client::Uploader::completed(ErrorOr<NodeHandle> result)
{
    completed(std::unique_lock<std::mutex>(mLock),
              std::move(result));
}

void Client::Uploader::made(Path& path, ErrorOr<NodeHandle> result)
{
    // Sanity.
    assert(mPendingDirectories);

    // Attempt to make the directory has completed.
    --mPendingDirectories;

    // Couldn't make the directory.
    if (!result)
        return completed(std::move(result));

    // Some other operation couldn't complete.
    if (mResult != API_OK)
        return completed(std::move(result));

    std::error_code error;

    // Try and open the directory for iteration.
    auto i = fs::directory_iterator(path, error);
    auto j = fs::directory_iterator();

    // Couldn't open directory for iteration.
    if (error)
        return completed(unexpected(API_EREAD));

    // Upload this directory's content.
    for ( ; i != j; ++i)
    {
        auto path_ = i->path();
        auto type = i->status().type();

        switch (type)
        {
        case fs::file_type::directory:
            make(std::move(path_), *result);
            break;
        case fs::file_type::regular:
            upload(std::move(path_), *result);
            break;
        default:
            break;
        }
    }

    // Directory's been made.
    completed(std::move(result));
}

void Client::Uploader::make(const Path& path, NodeHandle parentHandle)
{
    // Record that a directory is being made.
    ++mPendingDirectories;

    // Try and make the directory.
    mClient.makeDirectory(std::bind(&Uploader::made,
                                    this,
                                    path,
                                    std::placeholders::_1),
                          path.path().filename().u8string(),
                          parentHandle);
}

void Client::Uploader::upload(Path path, NodeHandle parentHandle)
{
    // Acquire lock.
    std::unique_lock<std::mutex> lock(mLock);

    // Create the upload.
    auto upload = mClient.client().upload(LocalPath(),
                                          path.path().filename().u8string(),
                                          parentHandle,
                                          path);

    // Record that a file is being uploaded.
    auto i = mPendingFiles.emplace(path, upload);

    // Sanity.
    assert(i.second);

    // So we can use our uploaded method as a callback.
    BoundCallback uploaded = std::bind(&Uploader::uploaded,
                                       this,
                                       std::move(path),
                                       std::placeholders::_1);

    // Try and upload the file.
    upload->begin(std::move(uploaded));

    // Silence compiler.
    static_cast<void>(i);
}

void Client::Uploader::uploaded(const Path& path, ErrorOr<NodeHandle> result)
{
    // Acquire lock.
    std::unique_lock<std::mutex> lock(mLock);

    // Upload's completed.
    auto count = mPendingFiles.erase(path);

    // Sanity.
    assert(count);

    // Silence compiler.
    static_cast<void>(count);

    // Report the file's been uploaded.
    completed(std::move(lock), std::move(result));
}

Client::Uploader::Uploader(Client& client)
  : mClient(client)
  , mLock()
  , mNotifier()
  , mPendingDirectories{1u}
  , mPendingFiles()
  , mResult{API_OK}
{
}

ErrorOr<NodeHandle> Client::Uploader::operator()(const std::string& name,
                                                 NodeHandle parentHandle,
                                                 Path path)
{
    // Try and make the root directory.
    auto handle = mClient.makeDirectory(name, parentHandle);

    // Couldn't make the root directory.
    if (!handle)
        return handle;

    // Try and upload the root directory's content.
    made(path, *handle);

    // Wait for the upload to complete.
    mNotifier.get_future().get();

    // Couldn't upload the root's contents.
    if (mResult != API_OK)
        return unexpected(mResult.load());

    // The root's content has been uploaded.
    return handle;
}

} // testing
} // fuse
} // mega

