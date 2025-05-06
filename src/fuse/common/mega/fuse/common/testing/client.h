#pragma once

#include <mega/common/client_callbacks.h>
#include <mega/common/client_forward.h>
#include <mega/common/error_or_forward.h>
#include <mega/common/node_info_forward.h>
#include <mega/common/normalized_path_forward.h>
#include <mega/common/partial_download_forward.h>
#include <mega/common/task_queue_forward.h>
#include <mega/common/upload_callbacks.h>
#include <mega/common/upload_forward.h>
#include <mega/fuse/common/inode_info_forward.h>
#include <mega/fuse/common/mount_event_forward.h>
#include <mega/fuse/common/mount_flags_forward.h>
#include <mega/fuse/common/mount_info_forward.h>
#include <mega/fuse/common/mount_result_forward.h>
#include <mega/fuse/common/service_forward.h>
#include <mega/fuse/common/testing/client_forward.h>
#include <mega/fuse/common/testing/cloud_path_forward.h>
#include <mega/fuse/common/testing/mount_event_observer_forward.h>
#include <mega/fuse/common/testing/path.h>
#include <mega/types.h>

#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>

namespace mega
{
namespace fuse
{
namespace testing
{

class Client
{
    // Responsible for uploading directory trees.
    class Uploader;

    // Get our hands on the client's high level interface.
    virtual common::Client& client() const = 0;

    // Retrieve the handle associated with the specified child.
    NodeHandle handle(NodeHandle parent, const std::string& name) const;

    using MakeDirectoryCallback =
      std::function<void(common::ErrorOr<NodeHandle>)>;

    // Create a directory in the cloud.
    void makeDirectory(MakeDirectoryCallback callback,
                       const std::string& name,
                       NodeHandle parentHandle);

    // Upload a file to the cloud.
    common::ErrorOr<NodeHandle> uploadFile(const std::string& name,
                                           NodeHandle parentHandle,
                                           const Path& path);

    // Get our hands on the client's FUSE interface.
    virtual Service& service() const = 0;

    // What observers are monitoring mount events?
    MountEventObserverWeakPtrSet mMountEventObservers;

    // Serializes access to mMountEventObservers.
    std::mutex mMountEventObserversLock;

    // Is our view of the cloud current?
    bool mNodesCurrent;

    // Signalled when our view of the cloud is current.
    std::condition_variable mNodesCurrentCV;

    // Necessary to wait on mNodesCurrentCV.
    std::mutex mNodesCurrentLock;

protected:
    Client(const std::string& clientName, const Path& databasePath, const Path& storagePath);

    // Called when a mount event has been emitted.
    void mountEvent(const MountEvent& event);

    // Specify whether our view of the cloud is current.
    void nodesCurrent(bool nodesCurrent);

    // Where should we create our databases?
    const Path mDatabasePath;

    // Where should we create our test files?
    const Path mStoragePath;

public:
    // Represents an individual contact.
    class Contact
    {
    protected:
        Contact() = default;

    public:
        virtual ~Contact() = default;

        // Remove the contact.
        virtual Error remove() = 0;

        // Has this contact been verified?
        virtual bool verified() const = 0;

        // Verify the contact.
        virtual Error verify() = 0;
    }; // Contact

    // Represents an invitation to be friends.
    class Invite
    {
    protected:
        Invite() = default;

    public:
        virtual ~Invite() = default;

        // Accept the invitation.
        virtual Error accept() = 0;

        // Cancel the invitation.
        virtual Error cancel() = 0;

        // Decline the invitation.
        virtual Error decline() = 0;
    }; // Invite

    // Convenience.
    using Clock = std::chrono::steady_clock;
    using ContactPtr = std::unique_ptr<Contact>;
    using Handle = mega::handle;
    using InvitePtr = std::unique_ptr<Invite>;

    template<typename Rep, typename Period>
    using Duration = std::chrono::duration<Rep, Period>;

    using TimePoint = Clock::time_point;

    virtual ~Client();

    // Add a new mount to the database.
    MountResult addMount(const MountInfo& info);

    // Retrieve the names of this node's children.
    std::set<std::string> childNames(CloudPath path) const;

    // Is the specified user a contact?
    virtual auto contact(const std::string& email) const -> ContactPtr = 0;

    // Describe the inode associated with the specified path.
    common::ErrorOr<InodeInfo> describe(const Path& path) const;

    // Remove a sync previously created with synchronize(...)
    void desynchronize(::mega::handle id);

    // Disable a previously enabled mount.
    MountResult disableMount(const std::string& name, bool remember);

    // Disable all enabled mounts.
    MountResult disableMounts(bool remember);

    // Discard node events.
    MountResult discard(bool discard);

    // What is the email of the currenty logged in user?
    virtual std::string email() const = 0;

    // Enable a previously added mount.
    MountResult enableMount(const std::string& name, bool remember);

    // Execute some function on the client thread.
    common::Task execute(std::function<void(const common::Task&)> function);

    // Retrieve information about a specific child.
    common::ErrorOr<common::NodeInfo> get(CloudPath parentPath, const std::string& name) const;

    // Retrieve information about a node.
    common::ErrorOr<common::NodeInfo> get(CloudPath path) const;

    // Query what a child's node handle is.
    NodeHandle handle(CloudPath parentPath, const std::string& name) const;

    // Retrieve the handle of the node at the specified path.
    NodeHandle handle(const std::string& path) const;

    // Query whether a node has file attributes.
    virtual bool hasFileAttribute(NodeHandle handle, fatype type) const = 0;

    // Send a friendship invite to the specified user.
    virtual auto invite(const std::string& email) -> common::ErrorOr<InvitePtr> = 0;

    // Is a friendship invite associated with the specified user?
    virtual auto invited(const std::string& email) const -> InvitePtr = 0;

    // Check if a file is cached.
    bool isCached(const Path& path) const;

    // Try and log the specified user in.
    virtual Error login(const std::string& email,
                        const std::string& password) = 0;

    // Try and log into a user specified in the environment.
    Error login(std::size_t accountIndex);

    // Try and log the user into an existing session.
    virtual Error login(const std::string& sessionToken) = 0;

    // Check if the user is logged in.
    virtual sessiontype_t loggedIn() const = 0;

    // Try and log the user out.
    virtual Error logout(bool keepSession) = 0;

    // Create a directory in the cloud.
    common::ErrorOr<NodeHandle> makeDirectory(const std::string& name, CloudPath parent);

    // Return a reference to a new mount event observer.
    MountEventObserverPtr mountEventObserver();

    // Query whether a mount is enabled.
    bool mountEnabled(const std::string& name) const;

    // Update a mount's flags.
    MountResult mountFlags(const std::string& name, const MountFlags& flags);

    // Retrieve a mount's flags.
    MountFlagsPtr mountFlags(const std::string& name) const;

    // Retrieve a mount's description.
    MountInfoPtr mountInfo(const std::string& name) const;

    // Retrieve the path associated with the specified name.
    common::NormalizedPath mountPath(const std::string& name) const;

    // Retrieve a description of each (enabled) mount.
    MountInfoVector mounts(bool onlyEnabled) const;

    // Move a node in the cloud.
    Error move(const std::string& name,
               CloudPath source,
               CloudPath target);

    // Download part of a file from the cloud.
    auto partialDownload(CloudPath path) -> common::ErrorOr<common::PartialDownloadPtr>;

    // Reload the cloud tree.
    virtual Error reload() = 0;

    // Remove a node in the cloud.
    Error remove(CloudPath path);

    // Remove all children beneath the specified node.
    Error removeAll(CloudPath path);

    // Remove a mount from the database.
    MountResult removeMount(const std::string& name);

    // Remove all mounts from the database.
    MountResult removeMounts(bool disable);

    // Replace a node in the cloud.
    Error replace(CloudPath source, CloudPath target);

    // Retrieve the handle of the root node.
    virtual NodeHandle rootHandle() const = 0;

    // Retrieve this user's session token.
    virtual std::string sessionToken() const = 0;

    // Share a directory with another user.
    virtual Error share(const std::string& email,
                        CloudPath path,
                        accesslevel_t permissions) = 0;

    // Check if a directory has already been shared with the specified user.
    virtual bool shared(const std::string& email,
                        CloudPath path,
                        accesslevel_t permissions) const = 0;

    // Retrieve storage statistics from the cloud.
    common::ErrorOr<StorageInfo> storageInfo();

    // Where are we storing our files
    const Path& storagePath() const;

    // Synchronize a local tree against some location in the cloud.
    auto synchronize(const Path& path, CloudPath target)
      -> std::tuple<::mega::handle, Error, SyncError>;

    // Upload a directory tree or file to the cloud.
    common::ErrorOr<NodeHandle> upload(const std::string& name,
                                       CloudPath parent,
                                       const Path& path);

    common::ErrorOr<NodeHandle> upload(CloudPath parent,
                                       const Path& path);

    // Specify whether files should be versioned.
    virtual void useVersioning(bool useVersioning) = 0;

    // Wait until when for our view of the cloud to be current.
    Error waitForNodesCurrent(TimePoint when);

    // Wait for our view of the cloud to be current.
    template<typename Rep, typename Period>
    Error waitForNodesCurrent(Duration<Rep, Period> delay)
    {
        return waitForNodesCurrent(Clock::now() + delay);
    }
}; // Client

} // testing
} // fuse
} // mega

