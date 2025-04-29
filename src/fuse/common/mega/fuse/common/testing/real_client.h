#pragma once

#include <atomic>
#include <functional>
#include <map>
#include <mutex>
#include <thread>

#include <mega/fuse/common/testing/client.h>

#include <mega/db.h>
#include <mega/megaapp.h>
#include <mega/megaclient.h>
#include <mega/waiter.h>

namespace mega
{
namespace fuse
{
namespace testing
{

class RealClient
  : public Client
  , private MegaApp
{
    class RealContact;
    class RealInvite;

    // What kind of function handles a request's response?
    using RequestCallback = std::function<void(Error)>;

    // What kind of a request are we waiting for?
    enum RequestType
    {
        RT_CATCHUP,
        RT_FETCH
    }; // RequestType

    // [type, tag] should uniquely identify a request.
    using RequestKey = std::pair<RequestType, int>;

    // Associates a callback with a pending request.
    using RequestCallbackMap = std::map<RequestKey, RequestCallback>;

    // Retrieve this client's high-level interface.
    common::Client& client() const override;

    // What is the email of the currenty logged in user?
    std::string email() const override;

    // Try and retrieve a description of the user's cloud content.
    Error fetch(bool ignoreCache);

    // Called when we receive a response for a fetch request.
    void fetchnodes_result(const Error& result) override;

    // Is a friendship invite associated with the specified user?
    auto invited(const std::string& email,
                 std::unique_lock<std::mutex>& lock) const -> InvitePtr;

    // Try and login the specified user.
    Error login(const std::string& email,
                const std::string& password,
                const std::string& salt);

    // Peforms client activity.
    void loop();

    // Called when the client emits a "nodes current" event.
    void nodes_current() override;

    // Called when the client emits a mount event.
    void onFuseEvent(const MountEvent& event) override;

    // Prepare the specified node for sharing.
    Error openShareDialog(NodeHandle handle);

    // Try and retrieve the user's salt.
    common::ErrorOr<std::string> prelogin(const std::string& email);

    // Called when a request has completed.
    void requestCompleted(RequestKey key, Error result);

    // Retrieve this client's FUSE interface.
    Service& service() const override;

    // Check if a directory has already been shared with the specified user.
    bool shared(const std::string& email,
                NodeHandle handle,
                accesslevel_t permissions) const;

    // The actual client.
    std::unique_ptr<MegaClient> mClient;

    // Serializes access to mClient.
    mutable std::mutex mClientLock;

    // Signals when the client's worker thread should terminate.
    std::atomic<bool> mClientTerminate;

    // The client's worker thread.
    std::thread mClientThread;

    // How the client performs HTTP requests.
    std::unique_ptr<HttpIO> mHTTPIO;

    // Tracks pending requests.
    RequestCallbackMap mPendingRequests;

    // How the client waits for activity to occur.
    std::shared_ptr<Waiter> mWaiter;

public:
    RealClient(const std::string& clientName, const Path& databasePath, const Path& storagePath);

    ~RealClient();

    // Is the specified user a contact?
    auto contact(const std::string& email) const -> ContactPtr override;

    // Send a friendship invite to the specified user.
    auto invite(const std::string& email) -> common::ErrorOr<InvitePtr> override;

    // Is a friendship invite associated with the specified user?
    auto invited(const std::string& email) const -> InvitePtr override;

    // Try and log the specified user in.
    Error login(const std::string& email,
                const std::string& password) override;

    // Try and log the user into an existing session.
    Error login(const std::string& sessionToken) override;

    // Check if the user is logged in.
    sessiontype_t loggedIn() const override;

    // Try and log the user out.
    Error logout(bool keepSession) override;

    // Reload the cloud tree.
    Error reload() override;

    // Retrieve the handle of the root node.
    NodeHandle rootHandle() const override;

    // Retrieve this user's session token.
    std::string sessionToken() const override;

    // Share a directory with another user.
    Error share(const std::string& email,
                CloudPath path,
                accesslevel_t permissions) override;

    // Check if a directory has already been shared with the specified user.
    bool shared(const std::string& email,
                CloudPath path,
                accesslevel_t permissions) const override;

    // Specify whether files should be versioned.
    void useVersioning(bool useVersioning) override;
}; // RealClient

} // testing
} // fuse
} // mega

