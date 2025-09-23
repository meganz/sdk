#include <mega/common/error_or.h>
#include <mega/common/node_info.h>
#include <mega/common/normalized_path.h>
#include <mega/common/testing/client.h>
#include <mega/common/testing/cloud_path.h>
#include <mega/common/testing/file.h>
#include <mega/common/upload.h>
#include <mega/common/utility.h>

#include <atomic>
#include <env_var_accounts.h>
#include <future>
#include <mutex>
#include <test.h>

namespace mega
{
namespace common
{
namespace testing
{

using namespace common;

class Client::Uploader
{
    // Called when a directory has been made or a file uploaded.
    void completed(std::unique_lock<std::mutex> lock, ErrorOr<NodeHandle> result);

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
    ErrorOr<NodeHandle> operator()(const std::string& name, NodeHandle parentHandle, Path path);
}; // Uploader

ErrorOr<NodeHandle> Client::handle(NodeHandle parent, const std::string& name) const
{
    return client().handle(parent, name);
}

void Client::makeDirectory(MakeDirectoryCallback callback,
                           const std::string& name,
                           NodeHandle parentHandle)
{
    // Called when our directory has been made.
    auto made = [this](MakeDirectoryCallback& callback, ErrorOr<NodeInfo> result)
    {
        // Invokes our callback in a safe context.
        auto wrapper =
            [](MakeDirectoryCallback& callback, ErrorOr<NodeInfo> result, const Task& task)
        {
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
        execute(std::bind(std::move(wrapper),
                          std::move(callback),
                          std::move(result),
                          std::placeholders::_1));
    }; // result

    // Try and make the directory.
    client().makeDirectory(std::bind(std::move(made), std::move(callback), std::placeholders::_1),
                           name,
                           parentHandle);
}

ErrorOr<NodeHandle> Client::uploadFile(const std::string& name,
                                       NodeHandle parentHandle,
                                       const Path& path)
{
    // Get the handle of any existing child with this name.
    auto handle = this->handle(parentHandle, name);

    // Create the upload.
    auto upload = client().upload(LocalPath(), name, parentHandle, path);

    // So we can wait for the upload's result.
    auto notifier = makeSharedPromise<ErrorOr<NodeHandle>>();

    // Called when our file's been bound.
    BoundCallback bound = [notifier](auto result)
    {
        notifier->set_value(result);
    }; // bound

    // Called when our file's data has been uploaded.
    UploadCallback uploaded = [bound, handle, notifier](auto result)
    {
        // Couldn't upload the file's data.
        if (!result)
            return notifier->set_value(unexpected(result.error()));

        // Bind the file.
        (*result)(std::move(bound), handle.valueOr(NodeHandle()));
    }; // uploaded

    // Try and upload the file.
    upload->begin(std::move(uploaded));

    // Return the upload's result to the caller.
    return waitFor(notifier->get_future());
}

Client::Client(const std::string&, const Path& databasePath, const Path& storagePath):
    mNodesCurrent(false),
    mNodesCurrentCV(),
    mNodesCurrentLock(),
    mDatabasePath(databasePath),
    mStoragePath(storagePath)
{}

Client::~Client() {}

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

ErrorOr<std::set<std::string>> Client::childNames(CloudPath path) const
{
    // Try and resolve the parent's handle.
    auto parentHandle = path.resolve(*this);

    // Parent doesn't exist.
    if (!parentHandle)
        return unexpected(parentHandle.error());

    // Retrieve the names of the parent's children.
    return client().childNames(*parentHandle);
}

void Client::desynchronize(::mega::handle id)
{
    client().desynchronize(id);
}

Task Client::execute(std::function<void(const Task&)> function)
{
    // Sanity.
    assert(function);

    // Queue the function for execution.
    return client().execute(std::move(function));
}

ErrorOr<NodeInfo> Client::get(CloudPath parentPath, const std::string& name) const
{
    // Try and resolve the parent's handle.
    auto parentHandle = parentPath.resolve(*this);

    // Parent doens't exist.
    if (!parentHandle)
        return unexpected(parentHandle.error());

    // Try and get info about the specified child.
    return client().get(*parentHandle, name);
}

ErrorOr<NodeInfo> Client::get(CloudPath path) const
{
    auto handle = path.resolve(*this);

    if (handle)
        return client().get(*handle);

    return unexpected(handle.error());
}

auto Client::getPublicLink(CloudPath path) -> ErrorOr<PublicLink>
{
    // Try and resolve the path to a node handle.
    auto handle = path.resolve(*this);

    // Couldn't resolve the path to a node handle.
    if (!handle)
        return unexpected(handle.error());

    // So we can signal when the link has been retrieved.
    auto notifier = makeSharedPromise<ErrorOr<PublicLink>>();

    // Called when the link has been retrieved.
    auto linked = [notifier](ErrorOr<PublicLink> result)
    {
        notifier->set_value(std::move(result));
    }; // linked

    // Ask the client to get (or create) this node's public link.
    getPublicLink(std::move(linked), *handle);

    // Return the link to our caller.
    return waitFor(notifier->get_future());
}

ErrorOr<NodeHandle> Client::handle(CloudPath parentPath, const std::string& name) const
{
    // Resolve the parent's handle.
    auto parentHandle = parentPath.resolve(*this);

    // Parent doesn't exist.
    if (!parentHandle)
        return unexpected(parentHandle.error());

    // Try and retrieve the child's handle.
    return client().handle(*parentHandle, name);
}

ErrorOr<NodeHandle> Client::handle(const std::string& path) const
{
    // Try and locate the specified node.
    auto info = client().lookup(RemotePath(path), rootHandle());

    // Found the specified node.
    if (info)
        return info->mHandle;

    // Couldn't locate the specified node.
    return unexpected(info.error());
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

ErrorOr<NodeHandle> Client::makeDirectory(const std::string& name, CloudPath parent)
{
    assert(!name.empty());

    auto parentHandle = parent.resolve(*this);

    if (!parentHandle)
        return unexpected(parentHandle.error());

    auto result = client().makeDirectory(name, *parentHandle);

    if (result)
        return result->mHandle;

    return unexpected(result.error());
}

Error Client::move(const std::string& name, CloudPath source, CloudPath target)
{
    // Sanity.
    assert(!name.empty());

    auto sourceHandle = source.resolve(*this);
    auto targetHandle = target.resolve(*this);

    // Source doesn't exist.
    if (!sourceHandle)
        return sourceHandle.error();

    // Target doesn't exist.
    if (!targetHandle)
        return targetHandle.error();

    // Move the node.
    return client().move(name, *sourceHandle, *targetHandle);
}

auto Client::partialDownload(PartialDownloadCallback& callback,
                             CloudPath path,
                             std::uint64_t length,
                             std::uint64_t offset) -> ErrorOr<PartialDownloadPtr>
{
    auto handle = path.resolve(*this);

    if (handle)
        return client().partialDownload(callback, *handle, length, offset);

    return unexpected(handle.error());
}

Error Client::remove(CloudPath path)
{
    auto handle = path.resolve(*this);

    if (handle)
        return client().remove(*handle);

    return handle.error();
}

Error Client::removeAll(CloudPath path)
{
    auto handle = path.resolve(*this);

    if (handle)
        return client().removeAll(*handle);

    return handle.error();
}

Error Client::replace(CloudPath source, CloudPath target)
{
    auto sourceHandle = source.resolve(*this);
    auto targetHandle = target.resolve(*this);

    // Source doesn't exist.
    if (!sourceHandle)
        return sourceHandle.error();

    // Target doesn't exist.
    if (!targetHandle)
        return targetHandle.error();

    // Replace target with source.
    return client().replace(*sourceHandle, *targetHandle);
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
    return client().synchronize(path.localPath(), target.resolve(*this).valueOr(NodeHandle()));
}

ErrorOr<NodeHandle> Client::upload(const std::string& name, CloudPath parent, const Path& path)
{
    // Sanity.
    assert(!name.empty());

    // Resolve parent node.
    auto parentHandle = parent.resolve(*this);

    // Parent doesn't exist.
    if (!parentHandle)
        return unexpected(parentHandle.error());

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
            return Uploader(*this)(name, *parentHandle, path);
        case fs::file_type::regular:
            return uploadFile(name, *parentHandle, path);
        default:
            break;
    }

    // Can't upload something that isn't a directory or file.
    return unexpected(API_EARGS);
}

ErrorOr<NodeHandle> Client::upload(const std::string& content,
                                   const std::string& name,
                                   CloudPath parent)
try
{
    // Find out where we can create temporary files.
    auto temporaryPath = fs::temp_directory_path();

    // Create a temporary file for us to upload.
    File temporary(content, name, temporaryPath);

    // Upload the file to the cloud.
    return upload(name, std::move(parent), temporary.path());
}

catch (...)
{
    return unexpected(API_EFAILED);
}

ErrorOr<NodeHandle> Client::upload(CloudPath parent, const Path& path)
{
    return upload(path.localPath().leafName().toPath(false), parent, path);
}

Error Client::waitForNodesCurrent(TimePoint when)
{
    // Acquire lock.
    std::unique_lock<std::mutex> lock(mNodesCurrentLock);

    // Checks if our view of the cloud is current.
    auto isCurrent = [&]()
    {
        return mNodesCurrent;
    }; // isCurrent

    // Wait until when for our view to become current.
    if (!mNodesCurrentCV.wait_until(lock, when, isCurrent))
        return API_EFAILED;

    // Our view of the cloud is current.
    return API_OK;
}

void Client::Uploader::completed(std::unique_lock<std::mutex> lock, ErrorOr<NodeHandle> result)
{
    // Operation couldn't be completed.
    if (!result && mResult == API_OK)
    {
        // Latch error.
        mResult = result.error();

        // Try and cancel any pending uploads.
        for (auto& f: mPendingFiles)
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
    completed(std::unique_lock<std::mutex>(mLock), std::move(result));
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
    for (; i != j; ++i)
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
    mClient.makeDirectory(std::bind(&Uploader::made, this, path, std::placeholders::_1),
                          path.path().filename().u8string(),
                          parentHandle);
}

void Client::Uploader::upload(Path path, NodeHandle parentHandle)
{
    // Acquire lock.
    std::unique_lock<std::mutex> lock(mLock);

    // Create the upload.
    auto upload =
        mClient.client().upload(LocalPath(), path.path().filename().u8string(), parentHandle, path);

    // Record that a file is being uploaded.
    auto i = mPendingFiles.emplace(path, upload);

    // Sanity.
    assert(i.second);

    // So we can use our uploaded method as a callback.
    BoundCallback uploaded =
        std::bind(&Uploader::uploaded, this, std::move(path), std::placeholders::_1);

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

Client::Uploader::Uploader(Client& client):
    mClient(client),
    mLock(),
    mNotifier(),
    mPendingDirectories{1u},
    mPendingFiles(),
    mResult{API_OK}
{}

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

Client::PublicLink::PublicLink(const std::string& link):
    mLink(link)
{}

const std::string& Client::PublicLink::get() const
{
    return mLink;
}

Client::SessionToken::SessionToken(const std::string& value):
    mValue(value)
{}

const std::string& Client::SessionToken::get() const
{
    return mValue;
}

} // testing
} // common
} // mega
