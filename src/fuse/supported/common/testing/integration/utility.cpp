#include <mega/common/error_or.h>
#include <mega/common/node_info.h>
#include <mega/common/testing/utility.h>
#include <mega/fuse/common/inode_id.h>
#include <mega/fuse/common/inode_info.h>
#include <mega/fuse/common/testing/client.h>
#include <mega/types.h>

#include <chrono>

namespace mega
{
namespace fuse
{
namespace testing
{

using namespace common::testing;
using namespace common;
using namespace std::chrono;

Error befriend(Client& client0, Client& client1)
{
    auto timeout = seconds(16);

    // Both clients should be logged in.
    if (client0.loggedIn() != FULLACCOUNT)
        return API_EARGS;

    if (client1.loggedIn() != FULLACCOUNT)
        return API_EARGS;

    auto email0 = client0.email();
    auto email1 = client1.email();

    // The clients shouldn't be logged in as the same user.
    if (email0 == email1)
        return API_EARGS;

    auto contact0 = client0.contact(email1);
    auto contact1 = client1.contact(email0);

    // The users aren't friends.
    if (!contact0 && !contact1)
    {
        // Try and send a friend invitation.
        auto invited = client0.invite(email1);

        // Couldn't send the friend invite.
        if (!invited)
            return invited.error();

        // Wait for our invitation to be received.
        auto invite = waitFor(
            [&]()
            {
                return client1.invited(email0);
            },
            timeout,
            nullptr);

        // Invite wasn't received.
        if (!invite)
            return LOCAL_ETIMEOUT;

        // Try and accept the invitation.
        auto accepted = invite->accept();

        // Couldn't accept the invitation.
        if (accepted != API_OK)
            return accepted;

        // Wait for friendship to be confirmed.
        auto confirmed = waitFor(
            [&]()
            {
                contact0 = client0.contact(email1);
                contact1 = client1.contact(email0);

                return contact0 && contact1;
            },
            timeout);

        // Couldn't confirm friendship.
        if (!confirmed)
            return LOCAL_ETIMEOUT;
    }

    // Contacts should be visible at this point.
    assert(contact0);
    assert(contact1);

    // Verifies a contact, if necessary.
    auto verify = [&](Client::Contact& contact)
    {
        // Try and verify the contact.
        auto result = contact.verify();

        // Couldn't verify the contact.
        if (result != API_OK)
            return result;

        // Wait for verification to complete.
        auto verified = waitFor(
            [&]()
            {
                return contact.verified();
            },
            timeout);

        // Verification timed out.
        if (!verified)
            return Error(LOCAL_ETIMEOUT);

        // Verification's complete.
        return Error(API_OK);
    }; // verify

    // Try and verify friendship.
    auto verified = verify(*contact0);

    if (verified == API_OK)
        return verify(*contact1);

    return verified;
}

NodeHandle id(const NodeInfo& info)
{
    return info.mHandle;
}

InodeID id(const InodeInfo& info)
{
    return info.mID;
}

NodeHandle parentID(const NodeInfo& info)
{
    return info.mParentHandle;
}

InodeID parentID(const InodeInfo& info)
{
    return info.mParentID;
}

std::string toString(NodeHandle handle)
{
    return toNodeHandle(handle);
}

std::uint64_t toUint64(InodeID id)
{
    return id.get();
}

std::uint64_t toUint64(NodeHandle handle)
{
    return handle.as8byte();
}

} // testing
} // fuse
} // mega
