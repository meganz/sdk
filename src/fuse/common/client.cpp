#include <cassert>
#include <condition_variable>
#include <future>
#include <mutex>

#include <mega/fuse/common/bind_handle.h>
#include <mega/fuse/common/client.h>
#include <mega/fuse/common/error_or.h>
#include <mega/fuse/common/node_info.h>
#include <mega/fuse/common/utility.h>

#include <mega/filesystem.h>
#include <mega/types.h>

namespace mega
{
namespace fuse
{

Client::Client()
  : mEventObserver(nullptr)
{
}

Client::~Client()
{
}

void Client::eventObserver(NodeEventObserver* observer)
{
    mEventObserver = observer;
}

template<typename T>
auto Client::lookup(const T& path, NodeHandle parent)
  -> typename EnableIfPath<T, ErrorOr<NodeInfo>>::type
{
    // Clarity.
    using Path = T;

    // Convenience.
    const auto& fsAccess = this->fsAccess();

    // Make sure the parent exists.
    auto info = get(parent);

    // Parent doesn't exist.
    if (!info)
        return info.error();

    // Parent isn't a directory.
    if (!info->mIsDirectory)
        return API_FUSE_ENOTDIR;

    Path name;
    std::size_t index = 0;

    // Traverse tree fragment by fragment.
    while (path.nextPathComponent(index, name))
    {
        // Node doesn't exist.
        if (!info)
            return info.error();

        // Node isn't a directory.
        if (!info->mIsDirectory)
            return API_FUSE_ENOTDIR;

        // Try and locate the next node in the path.
        info = get(info->mHandle, name.toName(fsAccess));
    }

    // Node doesn't exist.
    if (!info)
        return info.error();

    // Node exists.
    return info;
}

ErrorOr<NodeInfo> Client::makeDirectory(const std::string& name,
                                        NodeHandle parent)
{
    auto notifier = makeSharedPromise<ErrorOr<NodeInfo>>();

    // Called when our directory has been created.
    auto created = [notifier](ErrorOr<NodeInfo> result) {
        notifier->set_value(std::move(result));
    }; // created

    // Try and create the directory.
    makeDirectory(std::move(created), name, parent);

    // Return result to caller.
    return waitFor(notifier->get_future());
}

void Client::move(MoveCallback callback,
                  const std::string& name,
                  NodeHandle source,
                  NodeHandle target)
{
    // Sanity.
    assert(callback);
    assert(!name.empty());
    assert(!source.isUndef());
    assert(!target.isUndef());

    // Called when source has been renamed.
    auto renamed = [=](MoveCallback& callback, Error result) {
        // Couldn't rename source to name.
        if (result != API_OK)
            return callback(result);

        // Ask the client to move source to target.
        move(std::move(callback), source, target);
    }; // renamed

    // Ask the client to rename source to name.
    rename(std::bind(std::move(renamed),
                     std::move(callback),
                     std::placeholders::_1),
           name,
           source);
}

Error Client::move(const std::string& name,
                   NodeHandle source,
                   NodeHandle target)
{
    // Sanity.
    assert(!name.empty());
    assert(!source.isUndef());
    assert(!target.isUndef());

    // So we can wait for the client's result.
    auto notifier = makeSharedPromise<Error>();

    // Called when source has been renamed and moved to target.
    auto moved = [notifier](Error result) {
        notifier->set_value(result);
    }; // moved

    // Ask the client to rename source and move it to target.
    move(std::move(moved), name, source, target);

    // Return the client's result to our caller.
    return waitFor(notifier->get_future());
}

Error Client::move(NodeHandle source,
                   NodeHandle target)
{
    // Sanity.
    assert(!source.isUndef());
    assert(!target.isUndef());

    // So we can wait for the client's result.
    auto notifier = makeSharedPromise<Error>();

    // Transmits the client's result to our notifier.
    auto moved = [notifier](Error result) {
        notifier->set_value(result);
    }; // moved

    // Ask the client to move source to target.
    move(std::move(moved), source, target);

    // Return the client's result to our caller.
    return waitFor(notifier->get_future());
}

Error Client::remove(NodeHandle handle)
{
    // So we can wait for the client's result.
    auto notifier = makeSharedPromise<Error>();

    // Transmits the client's result to our notifier.
    auto removed = [notifier](Error result) {
        notifier->set_value(result);
    }; // removed

    // Ask the client to remove the specified node.
    remove(std::move(removed), handle);

    // Return the result to the caller.
    return waitFor(notifier->get_future());
}

Error Client::removeAll(NodeHandle handle)
{
    // Bundle necessary state for convenience.
    struct Context
    {
        // So we can wait for the client's result.
        std::condition_variable mCV;

        // Serializes access to mNumOutstanding and mResult.
        std::mutex mLock;

        // Tracks how many removals are in progress.
        std::size_t mNumOutstanding;

        // Representative result of removal.
        Error mResult = API_OK;
    }; // Context

    // Instantiate context.
    auto context = std::make_shared<Context>();

    // Executed when a node has been removed.
    RemoveCallback removed = [context](Error result) {
        // Acquire lock.
        std::lock_guard<std::mutex> guard(context->mLock);

        // Update result.
        if (context->mResult == API_OK)
            context->mResult = result;

        // Sanity.
        assert(context->mNumOutstanding);

        // Decrement counter.
        --context->mNumOutstanding;

        // Notify any waiters.
        context->mCV.notify_one();
    }; // removed

    // Try and remove all of this node's children.
    each([&](NodeInfo info) {
        // Increment counter.
        {
            std::lock_guard<std::mutex> guard(context->mLock);
            ++context->mNumOutstanding;
        }

        // Try and remove this child.
        remove(removed, info.mHandle);
    }, handle);

    // Check whether:
    // - Any removals have failed.
    // - All removals have completed.
    auto completed = [&]() {
        return context->mResult != API_OK
               || !context->mNumOutstanding;
    }; // completed

    // Acquire lock.
    std::unique_lock<std::mutex> lock(context->mLock);

    // Wait the removals to complete.
    auto result = context->mCV.wait_for(lock,
                                        defaultTimeout(),
                                        completed);

    // One or more of our removals timed out.
    if (!result)
        return LOCAL_ETIMEOUT;

    // Return the result to our caller.
    return context->mResult;
}

Error Client::rename(const std::string& name,
                     NodeHandle handle)
{
    // Sanity.
    assert(!name.empty());
    assert(!handle.isUndef());

    // So we can wait for the client's result.
    auto notifier = makeSharedPromise<Error>();

    // Called when handle has been renamed.
    auto renamed = [notifier](Error result) {
        // Transmit client's result to our notifier.
        notifier->set_value(result);
    }; // renamed

    // Ask the client to rename the node.
    rename(std::move(renamed), name, handle);

    // Return the result to the caller.
    return waitFor(notifier->get_future());
}

Error Client::replace(NodeHandle source,
                      NodeHandle target)
{
    // Sanity.
    assert(!source.isUndef());
    assert(!target.isUndef());

    // Try and get our hands on target's description.
    auto target_ = get(target);

    // Couldn't get our hands on target's description.
    if (!target_)
        return target_.error();

    // So we can wait for the client's result.
    auto notifier = makeSharedPromise<Error>();

    // Called when source has been renamed and moved.
    auto moved = [=](Error result) {
        // Couldn't rename or move source.
        if (result != API_OK)
            return notifier->set_value(result);

        // Called when target has been removed.
        auto removed = [=](Error result) {
            notifier->set_value(result);
        }; // removed

        // Ask the client to remove target.
        remove(std::move(removed), target);
    }; // moved

    // Ask the client to rename and move source.
    move(std::move(moved),
         target_->mName,
         source,
         target_->mParentHandle);

    // Return the client's result to the caller.
    return waitFor(notifier->get_future());
}

ErrorOr<StorageInfo> Client::storageInfo()
{
    // So we can wait for the client's result.
    auto notifier = makeSharedPromise<ErrorOr<StorageInfo>>();

    // Called when we've retrieved the user's storage info.
    auto retrieved = [notifier](ErrorOr<StorageInfo> result) {
        notifier->set_value(std::move(result));
    }; // retrieved

    // Ask the client to retrieve the user's storage info.
    storageInfo(std::move(retrieved));

    // Return the client's result to our caller.
    return waitFor(notifier->get_future());
}

Error Client::touch(NodeHandle handle, m_time_t modified)
{
    // Sanity.
    assert(!handle.isUndef());

    // So we can wait for the client's result.
    auto notifier = makeSharedPromise<Error>();

    // Called when the node's timestamp has been updated.
    auto updated = [notifier](Error result) {
        notifier->set_value(result);
    }; // updated

    // Ask the client to update the node's timestamp.
    touch(std::move(updated), handle, modified);

    // Return the client's result to the caller.
    return waitFor(notifier->get_future());
}

ErrorOr<UploadPtr> Client::upload(BoundCallback callback,
                                  const LocalPath& logicalPath,
                                  const std::string& name,
                                  NodeHandle parent,
                                  const LocalPath& physicalPath)
{
    // Called when the file's content has been uploaded.
    auto uploaded = [](BoundCallback& bound,
                       ErrorOr<UploadResult> result) {
        // Couldn't upload the file's content.
        if (!result)
            return bound(result.error());

        // Extract bind callback.
        auto bind = std::move(std::get<0>(*result));

        // Sanity.
        assert(bind);

        // Try and bind a name to our uploaded content.
        bind(std::move(bound), NodeHandle());
    }; // uploaded

    UploadCallback wrapper =
      std::bind(std::move(uploaded),
                std::move(callback),
                std::placeholders::_1);

    // Try and upload the file's content.
    return this->upload(std::move(wrapper),
                        logicalPath,
                        name,
                        parent,
                        physicalPath);
}

template ErrorOr<NodeInfo> Client::lookup(const LocalPath&, NodeHandle);
template ErrorOr<NodeInfo> Client::lookup(const RemotePath&, NodeHandle);

} // fuse
} // mega

