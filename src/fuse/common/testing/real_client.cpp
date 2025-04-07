#include "mega.h"

#include <mega/common/error_or.h>
#include <mega/common/utility.h>
#include <mega/db/sqlite.h>
#include <mega/fuse/common/logging.h>
#include <mega/fuse/common/testing/cloud_path.h>
#include <mega/fuse/common/testing/real_client.h>
#include <mega/gfx.h>
#include <mega/log_level.h>
#include <mega/logging.h>
#include <tests/integration/test.h>

#include <chrono>
#include <future>
#include <memory>
#include <utility>

namespace mega
{
namespace fuse
{
namespace testing
{

using namespace common;

static std::unique_ptr<GfxProc> createGfxProc()
{
    auto provider = IGfxProvider::createInternalGfxProvider();
    if (!provider)
    {
        return nullptr;
    }

    auto gfx = std::make_unique<GfxProc>(std::move(provider));
    if (gfx)
        gfx->startProcessingThread();
    return gfx;
}

class RealClient::RealContact
  : public Contact
{
    // Do we need to verify this contact?
    bool mustVerify(std::unique_lock<std::mutex>& lock) const;

    // What client contains this contact?
    RealClient& mClient;

    // The contact's email.
    std::string mEmail;

    // The contact's ID.
    Handle mID;

public:
    RealContact(RealClient& client, const User& user);

    // Remove the contact.
    Error remove() override;

    // Has this contact been verified?
    bool verified() const override;

    // Verify the contact.
    Error verify() override;
}; // RealContact

class RealClient::RealInvite
  : public Invite
{
    // Perform an operation on an invite we've received.
    Error execute(ipcactions_t action);

    // Perform an operation on an invite we've sent.
    Error execute(opcactions_t action);

    // What client contains this invite?
    RealClient& mClient;

    // What invite does this instance represent?
    Handle mID;

public:
    RealInvite(RealClient& client, Handle id);

    // Accept the invitation.
    Error accept() override;

    // Cancel the invitation.
    Error cancel() override;

    // Decline the invitation.
    Error decline() override;
}; // RealInvite

common::Client& RealClient::client() const
{
    return mClient->mClientAdapter;
}

std::string RealClient::email() const
{
    std::lock_guard<std::mutex> guard(mClientLock);

    if (auto* user = mClient->finduser(mClient->me, 0))
        return user->email;

    return std::string();
}

Error RealClient::fetch(bool ignoreCache)
{
    // So we can wait for the client's result.
    auto notifier = makeSharedPromise<Error>();

    // Ask client to describe our cloud content.
    {
        std::lock_guard<std::mutex> guard(mClientLock);

        // Transmits client's result to notifier.
        auto completion = [notifier](error result) {
            notifier->set_value(result);
        }; // completion

        // Generate a key uniquely identifying this request.
        auto key = std::make_pair(RT_FETCH, mClient->nextreqtag());

        // Move completion function into request map.
        auto result = mPendingRequests.emplace(std::move(key),
                                               std::move(completion));

        // Sanity.
        assert(result.second);

        // Silence compiler.
        static_cast<void>(result);

        // Description isn't current until some time after fetch.
        nodesCurrent(false);

        // Ask the client to describe our cloud content.
        mClient->fetchnodes(ignoreCache, true, false);

        // Let the client know it has work to do.
        mClient->waiter->notify();
    }

    // Return client's result to caller.
    return waitFor(notifier->get_future());
}

void RealClient::fetchnodes_result(const Error& error)
{
    requestCompleted(std::make_pair(RT_FETCH, mClient->restag), error);
}

auto RealClient::invited(const std::string& email, std::unique_lock<std::mutex>&) const -> InvitePtr
{
    // Compares two characters case insensitively.
    auto characterEquals = [](std::uint8_t lhs, std::uint8_t rhs)
    {
        return std::tolower(lhs) == std::tolower(rhs);
    }; // characterEquals

    // Compares two strings case insensitively.
    auto stringEquals = [&](const std::string& lhs, const std::string& rhs)
    {
        return std::equal(lhs.begin(), lhs.end(), rhs.begin(), rhs.end(), characterEquals);
    }; // stringEquals

    // Convenience.
    auto& self = const_cast<RealClient&>(*this);

    for (auto& i : mClient->pcrindex)
    {
        // Convenience.
        auto& request = *i.second;

        // Request received from email or sent to email.
        if ((request.isoutgoing && stringEquals(request.targetemail, email)) ||
            stringEquals(request.originatoremail, email))
            return std::make_unique<RealInvite>(self, request.id);
    }

    // No matching request.
    return nullptr;
}

Error RealClient::login(const std::string& email,
                        const std::string& password,
                        const std::string& salt)
{
    // So we can wait for the client's result.
    auto notifier = makeSharedPromise<Error>();

    // Transmits the client's result to our notifier.
    auto completion = [notifier](error result) {
        notifier->set_value(result);
    }; // completion

    // Ask the client to log the user in.
    do
    {
        std::lock_guard<std::mutex> guard(mClientLock);
        
        // Convenience.
        auto& accountVersion = mClient->accountversion;

        // Unknown account version.
        if (!accountVersion || accountVersion > 2)
            return API_EINTERNAL;

        // User has a V1 account.
        if (accountVersion == 1)
        {
            byte passwordKey[SymmCipher::KEYLENGTH];

            // Compute password key.
            auto result = mClient->pw_key(password.c_str(), passwordKey);

            // Couldn't compute password key.
            if (result != API_OK)
                return result;

            // Try and log the user in.
            mClient->login(email.c_str(),
                           passwordKey,
                           nullptr,
                           std::move(completion));

            // Let the client know it has work to do.
            mClient->waiter->notify();

            break;
        }

        // User has a V2 account but has no salt.
        if (salt.empty())
            return API_EINTERNAL;

        // Try and log the user in.
        mClient->login2(email.c_str(),
                        password.c_str(),
                        &salt,
                        nullptr,
                        std::move(completion));

        // Let the client know it has work to do.
        mClient->waiter->notify();
    }
    while (0);

    // Wait for the client's result.
    return waitFor(notifier->get_future());
}

void RealClient::loop()
{
    LOG_verbose << "Client thread started";

    while (!mClientTerminate)
    {
        // Acquire lock.
        std::unique_lock<std::mutex> lock(mClientLock);

        // Check whether the client needs any attention.
        auto result = mClient->preparewait();

        // Release lock.
        lock.unlock();

        // Wait for activity, if necessary.
        if (!result)
            result = mClient->dowait();

        // Acquire lock.
        lock.lock();

        // Check if any activity has occcured.
        result |= mClient->checkevents();

        // Give client control if necessary.
        if ((result & Waiter::NEEDEXEC))
            mClient->exec();
    }

    // Make sure the client's shut down.
    mClient->locallogout(false, true);

    LOG_verbose << "Client thread stopped";
}

void RealClient::nodes_current()
{
    nodesCurrent(true);
}

void RealClient::onFuseEvent(const MountEvent& event)
{
    mountEvent(event);
}

Error RealClient::openShareDialog(NodeHandle handle)
{
    std::unique_lock<std::mutex> lock(mClientLock);

    // Try and locate the specified node.
    auto node = mClient->nodeByHandle(handle);

    // Node doesn't exist.
    if (!node)
        return API_ENOENT;

    auto notifier = makeSharedPromise<Error>();

    // Called when we've opened a share dialog.
    auto opened = [notifier](Error result) {
        notifier->set_value(result);
    }; // opened 

    // Ask the client to open a share dialog.
    mClient->openShareDialog(node.get(), std::move(opened));

    // Make sure the client's awake to perform our task.
    mClient->waiter->notify();

    // Release the lock so the client can process our task.
    lock.unlock();

    // Pass result to caller.
    return waitFor(notifier->get_future());
}

ErrorOr<std::string> RealClient::prelogin(const std::string& email)
{
    // So we can wait for the client's result.
    auto notifier = makeSharedPromise<ErrorOr<std::string>>();

    // Transmits the client's result to our notifier.
    auto completion = [notifier](error result, std::string* salt) {
        // Couldn't perform prelogin.
        if (result != API_OK)
            return notifier->set_value(unexpected(result));

        // Transmit salt to caller.
        notifier->set_value(*salt);
    }; // completion

    // Ask the client to perform prelogin.
    {
        std::lock_guard<std::mutex> guard(mClientLock);

        mClient->prelogin(email.c_str(),
                          std::bind(std::move(completion),
                                    std::placeholders::_4,
                                    std::placeholders::_3));
        
        // Let the client know it has work to do.
        mClient->waiter->notify();
    }

    // Return client's result to caller.
    return waitFor(notifier->get_future());
}

void RealClient::requestCompleted(RequestKey key, Error result)
{
    auto i = mPendingRequests.find(key);

    // Sanity.
    assert(i != mPendingRequests.end());

    // Latch request callback.
    auto callback = std::move(i->second);

    // Remove request from map.
    mPendingRequests.erase(i);

    // Forward result to callback.
    callback(result);
}

Service& RealClient::service() const
{
    return mClient->mFuseService;
}

bool RealClient::shared(const std::string& email,
                        NodeHandle handle,
                        accesslevel_t permissions) const
{
    // Sanity.
    assert(!email.empty());
    assert(!handle.isUndef());

    // Acquire client lock.
    std::lock_guard<std::mutex> guard(mClientLock);

    // Try and retrieve a reference to the specified node.
    auto node = mClient->nodeByHandle(handle);

    // Node doesn't exist so it can't be shared.
    if (!node)
        return false;

    // Scans "shares" for the specified user.
    auto scan = [&](const share_map& shares) {
        // Search shares for the specified user.
        for (auto& s : shares)
        {
            // Convenience.
            const auto& share = *s.second;

            // Mismatched access level.
            if (share.access != permissions)
                continue;

            // Found a share to an established contact.
            if (share.user && share.user->email == email)
                return true;

            // Sanity.
            assert(share.pcr);

            // Found a share to a pending contact.
            if (share.pcr->targetemail == email)
                return true;
        }

        // No matching share to specified user.
        return false;
    }; // scan

    // Have we shared our node with the specified user?
    return (node->outshares && scan(*node->outshares))
           || (node->pendingshares && scan(*node->pendingshares));
}

RealClient::RealClient(const std::string& clientName,
                       const Path& databasePath,
                       const Path& storagePath):
    Client(clientName, databasePath, storagePath),
    MegaApp(),
    mClient(),
    mClientLock(),
    mClientTerminate{false},
    mClientThread(),
    mHTTPIO(new CurlHttpIO()),
    mPendingRequests(),
    mWaiter(std::make_shared<WAIT_CLASS>()),
    mGfxProc(createGfxProc())
{
    // Sanity.
    assert(!clientName.empty());

    // Instantiate the client.
    mClient.reset(new MegaClient(this,
                                 mWaiter,
                                 mHTTPIO.get(),
                                 new DBACCESS_CLASS(databasePath),
                                 mGfxProc.get(),
                                 "N9tSBJDC",
                                 USER_AGENT.c_str(),
                                 THREADS_PER_MEGACLIENT));

    // Make sure the client has a recognizable name.
    mClient->clientname = clientName + " ";

    // Make sure FUSE logs *everything*.
    mClient->mFuseService.logLevel(logDebug);

    // Instantiate the client's worker thread.
    mClientThread = std::thread(&RealClient::loop, this);

    LOG_verbose << "Client constructed";
}

RealClient::~RealClient()
{
    // Try and log the client out.
    logout(false);

    LOG_verbose << "Waiting for client thread to stop";

    // Let the client thread know it needs to terminate.
    mClientTerminate = true;

    // Wake the client up if it's sleeping.
    mClient->waiter->notify();

    // Wait for the client thread to terminate.
    mClientThread.join();

    // Destroy client.
    mClient.reset();

    LOG_verbose << "Client destroyed";
}

auto RealClient::contact(const std::string& email) const -> ContactPtr
{
    std::lock_guard<std::mutex> guard(mClientLock);

    // Convenience.
    auto& self = const_cast<RealClient&>(*this);

    // Try and locate the specified user.
    auto* user = mClient->finduser(email.c_str(), 0);

    // User's a contact.
    if (user && user->show == VISIBLE)
        return std::make_unique<RealContact>(self, *user);

    // User's not a contact.
    return nullptr;
}

bool RealClient::hasFileAttribute(NodeHandle handle, fatype type) const
{
    std::lock_guard<std::mutex> guard(mClientLock);

    // Try and locate the specified node.
    auto node = mClient->nodeByHandle(handle);

    // Node doesn't exist.
    if (!node)
        return false;

    return node->hasfileattribute(type) != 0;
}

auto RealClient::invite(const std::string& email) -> ErrorOr<InvitePtr>
{
    std::unique_lock<std::mutex> lock(mClientLock);

    // Already sent an invite to or recieved an invite from email.
    if (auto invite = invited(email, lock))
        return invite;

    auto notifier = makeSharedPromise<ErrorOr<InvitePtr>>();

    // Called when the invitation has been sent.
    auto invited = [=](Handle id, Error result, opcactions_t) {
        // Invitation was sent.
        if (result == API_OK)
            return notifier->set_value(std::make_unique<RealInvite>(*this, id));

        // Couldn't send invitation.
        notifier->set_value(unexpected(result));
    }; // invited

    // Send the invite.
    mClient->setpcr(email.c_str(),
                    OPCA_ADD,
                    nullptr,
                    nullptr,
                    UNDEF,
                    std::move(invited));

    // Release lock.
    lock.unlock();

    // Return result to caller.
    return waitFor(notifier->get_future());
}

auto RealClient::invited(const std::string& email) const -> InvitePtr
{
    std::unique_lock<std::mutex> guard(mClientLock);

    return invited(email, guard);
}

Error RealClient::login(const std::string& email,
                        const std::string& password)
{
    // Try and retrieve the user's salt.
    auto salt = prelogin(email);

    // Couldn't get the user's salt.
    if (!salt)
        return salt.error();

    // Try and log the user in.
    auto result = login(email, password, *salt);

    // Couldn't log the user in.
    if (result != API_OK)
        return result;

    // Try and retrieve a description of the user's cloud content.
    result = fetch(true);

    // Couldn't get a description of the cloud.
    if (result != API_OK)
        return result;

    // Convenience.
    using std::chrono::seconds;

    // Wait for our view of the cloud to be up to date.
    return waitForNodesCurrent(seconds(8));
}

Error RealClient::login(const std::string& sessionToken)
{
    auto notifier = makeSharedPromise<Error>();

    // So we can wait for the client's result.
    {
        std::lock_guard<std::mutex> guard(mClientLock);

        // Called when the user's been logged in.
        auto completion = [notifier](error result) {
            notifier->set_value(result);
        }; // result

        // Try and log in the user.
        mClient->login(sessionToken, std::move(completion));

        // Let the client know it has work to do.
        mClient->waiter->notify();
    }

    // Wait for the client's result.
    auto result = waitFor(notifier->get_future());

    // Couldn't log the user in.
    if (result != API_OK)
        return result;

    // Try and retrieve a description of the user's cloud content.
    result = fetch(false);

    // Couldn't get a description of the cloud.
    if (result != API_OK)
        return result;

    // Convenience.
    using std::chrono::seconds;

    // Wait for our view of the cloud to be up to date.
    return waitForNodesCurrent(seconds(8));
}

sessiontype_t RealClient::loggedIn() const
{
    std::lock_guard<std::mutex> guard(mClientLock);

    return mClient->loggedin();
}

Error RealClient::logout(bool keepSession)
{
    // So we can receive a result from the client.
    auto notifier = makeSharedPromise<Error>();

    // Try and log the user out.
    {
        std::lock_guard<std::mutex> guard(mClientLock);

        // User isn't logged in.
        if (mClient->loggedin() == NOTLOGGEDIN)
            return API_OK;

        // User wants to keep the session intact.
        if (keepSession)
            return mClient->locallogout(false, true), API_OK;

        // Transmits result to notifier.
        auto completion = [notifier](error result) {
            notifier->set_value(result);
        }; // completion

        // Try and log the user out.
        mClient->logout(false, std::move(completion));
    }

    // Return client's result to caller.
    return waitFor(notifier->get_future());
}

Error RealClient::reload()
{
    return fetch(false);
}

NodeHandle RealClient::rootHandle() const
{
    std::lock_guard<std::mutex> guard(mClientLock);

    return mClient->mNodeManager.getRootNodeFiles();
}

std::string RealClient::sessionToken() const
{
    std::lock_guard<std::mutex> guard(mClientLock);

    std::string sessionToken;

    mClient->dumpsession(sessionToken);

    return sessionToken;
}

Error RealClient::share(const std::string& email,
                        CloudPath path,
                        accesslevel_t permissions)
{
    // Sanity.
    assert(!email.empty());

    // Resolve directory path to a handle.
    auto handle = path.resolve(*this);

    // Directory doesn't exist.
    if (handle.isUndef())
        return API_ENOENT;

    // Have we already shared this node with email?
    if (shared(email, handle, permissions))
        return API_OK;

    // Couldn't prepare node for sharing.
    if (auto result = openShareDialog(handle))
        return result;

    // Open share dialog if necessary.
    auto notifier = makeSharedPromise<Error>();

    auto shared = [notifier](Error result, bool) {
        notifier->set_value(result);
    }; // shared

    std::unique_lock<std::mutex> lock(mClientLock);

    auto node = mClient->nodeByHandle(handle);

    // Node no longer exists.
    if (!node)
        return API_ENOENT;

    // Try and create share.
    mClient->setshare(std::move(node),
                      email.c_str(),
                      permissions,
                      false,
                      nullptr,
                      mClient->nextreqtag(),
                      std::move(shared));

    // Make sure the client's awake.
    mClient->waiter->notify();

    lock.unlock();

    // Return result to caller.
    return waitFor(notifier->get_future());
}

bool RealClient::shared(const std::string& email,
                        CloudPath path,
                        accesslevel_t permissions) const
{
    // Sanity.
    assert(!email.empty());

    // Resolve path to a node handle.
    auto handle = path.resolve(*this);

    // Can't share a node that doesn't exist.
    if (handle.isUndef())
        return false;

    // Have we shared node with the user?
    return shared(email, handle, permissions);
}

void RealClient::useVersioning(bool useVersioning)
{
    // Convenience.
    auto& versionsDisabled = mClient->versions_disabled;

    // Versioning state hasn't changed.
    if (versionsDisabled == !useVersioning)
        return;

    // Versioning state has changed.
    FUSEDebugF("%sabling file versioning",
               (useVersioning ? "En" : "Dis"));

    versionsDisabled = !useVersioning;
}

Error RealClient::RealInvite::execute(ipcactions_t action)
{
    auto notifier = makeSharedPromise<Error>();

    auto executed = [notifier](error result, ipcactions_t) {
        notifier->set_value(result);
    }; // executed

    std::unique_lock<std::mutex> lock(mClient.mClientLock);

    mClient.mClient->updatepcr(mID, action, std::move(executed));

    mClient.mClient->waiter->notify();

    lock.unlock();

    return waitFor(notifier->get_future());
}

Error RealClient::RealInvite::execute(opcactions_t)
{
    auto notifier = makeSharedPromise<Error>();

    auto executed = [notifier](Handle, error result, opcactions_t) {
        notifier->set_value(result);
    }; // executed

    std::unique_lock<std::mutex> lock(mClient.mClientLock);

    mClient.mClient->setpcr(nullptr,
                            OPCA_DELETE,
                            nullptr,
                            nullptr,
                            mID,
                            std::move(executed));

    mClient.mClient->waiter->notify();

    lock.unlock();

    return waitFor(notifier->get_future());
}

bool RealClient::RealContact::mustVerify(std::unique_lock<std::mutex>&) const
{
    return mClient.mClient->mKeyManager.getManualVerificationFlag();
}

RealClient::RealContact::RealContact(RealClient& client, const User& user)
  : Contact()
  , mClient(client)
  , mEmail(user.email)
  , mID(user.userhandle)
{
}

Error RealClient::RealContact::remove()
{
    auto notifier = makeSharedPromise<Error>();

    auto removed = [notifier](error result) {
        notifier->set_value(result);
    }; // removed

    std::unique_lock<std::mutex> lock(mClient.mClientLock);

    auto result = mClient.mClient->removecontact(mEmail.c_str(),
                                                 HIDDEN,
                                                 std::move(removed));

    if (result != API_OK)
        return result;

    lock.unlock();

    return waitFor(notifier->get_future());
}

bool RealClient::RealContact::verified() const
{
    std::unique_lock<std::mutex> guard(mClient.mClientLock);

    if (mustVerify(guard))
        return mClient.mClient->areCredentialsVerified(mID);

    return true;
}

Error RealClient::RealContact::verify()
{
    // So we can wait for the client's result if necessary.
    auto notifier = makeSharedPromise<Error>();

    // Check if we we need to verify the contact.
    {
        std::unique_lock<std::mutex> guard(mClient.mClientLock);

        // Convenience.
        auto& client = *mClient.mClient;

        // Do we need to verify the contact?
        if (!mustVerify(guard))
            return API_OK;

        // Has the contact already been verified?
        if (client.areCredentialsVerified(mID))
            return API_OK;

        // Called when the contact has been verified.
        auto verified = [notifier](Error result) {
            notifier->set_value(result);
        }; // verified

        // Try and verify the contact.
        auto result = client.verifyCredentials(mID, std::move(verified));

        // Client's returned an immediate failure.
        if (result != API_OK)
            return result;

        // Let the client know it has work to do.
        client.waiter->notify();
    }

    // Return the client's result to the caller.
    return waitFor(notifier->get_future());
}

RealClient::RealInvite::RealInvite(RealClient& client, Handle id)
  : Invite()
  , mClient(client)
  , mID(id)
{
}

Error RealClient::RealInvite::accept()
{
    return execute(IPCA_ACCEPT);
}

Error RealClient::RealInvite::cancel()
{
    return execute(OPCA_DELETE);
}

Error RealClient::RealInvite::decline()
{
    return execute(IPCA_DENY);
}

} // testing
} // fuse
} // mega

