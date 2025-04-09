#include <chrono>
#include <fstream>
#include <sstream>

#include <mega/common/error_or.h>
#include <mega/common/node_info.h>
#include <mega/fuse/common/date_time.h>
#include <mega/fuse/common/inode_info.h>
#include <mega/fuse/common/logging.h>
#include <mega/fuse/common/testing/client.h>
#include <mega/fuse/common/testing/path.h>
#include <mega/fuse/common/testing/utility.h>

#include "mega/base64.h"
#include "mega/types.h"

namespace mega
{
namespace fuse
{
namespace testing
{

using namespace common;

class StandardInputStream
  : public InputStreamAccess
{
    // What stream are we reading from?
    std::istream& mStream;

    // How much data does this stream contain?
    const m_off_t mSize;

public:
    StandardInputStream(std::istream& stream,
                        m_off_t size);

    // Read count bytes from this stream into buffer.
    bool read(byte* buffer, unsigned int count) override;

    // How many bytes are available for reading?
    m_off_t size() override;
}; // StreamAccess

// Convenience.
using namespace fs;
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
        auto invite = waitFor([&]() {
            return client1.invited(email0);
        }, timeout, nullptr);

        // Invite wasn't received.
        if (!invite)
            return LOCAL_ETIMEOUT;

        // Try and accept the invitation.
        auto accepted = invite->accept();

        // Couldn't accept the invitation.
        if (accepted != API_OK)
            return accepted;

        // Wait for friendship to be confirmed.
        auto confirmed = waitFor([&]() {
            contact0 = client0.contact(email1);
            contact1 = client1.contact(email0);

            return contact0 && contact1;
        }, timeout);

        // Couldn't confirm friendship.
        if (!confirmed)
            return LOCAL_ETIMEOUT;
    }

    // Contacts should be visible at this point.
    assert(contact0);
    assert(contact1);

    // Verifies a contact, if necessary.
    auto verify = [&](Client::Contact& contact) {
        // Try and verify the contact.
        auto result = contact.verify();

        // Couldn't verify the contact.
        if (result != API_OK)
            return result;

        // Wait for verification to complete.
        auto verified = waitFor([&]() {
            return contact.verified();
        }, timeout);

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

ErrorOr<FileFingerprint> fingerprint(const std::string& content,
                                     system_clock::time_point modified)
{
    // Convenience.
    auto modified_ = system_clock::to_time_t(modified);
    auto size_ = static_cast<m_off_t>(content.size());

    std::istringstream isstream(content, std::ios::binary);

    // Wrap input stream.
    StandardInputStream istream(isstream, size_);

    // Try and generate a fingerprint.
    FileFingerprint fingerprint;

    fingerprint.genfingerprint(&istream, modified_);

    // Couldn't generate fingerprint.
    if (!fingerprint.isvalid)
        return unexpected(API_EREAD);

    // Return fingerprint to caller.
    return fingerprint;
}

ErrorOr<FileFingerprint> fingerprint(const Path& path)
{
    std::error_code error;

    // Try and retrieve the file's modification time.
    auto modified = lastWriteTime(path, error);

    // Couldn't determine when the file was modified.
    if (error)
        return unexpected(API_EREAD);

    // Try and determine the file's size.
    auto size = file_size(path.path(), error);

    // Can't get the file's size.
    if (error)
        return unexpected(API_EREAD);

    // Open the file for reading.
    std::ifstream ifstream(path.string(), std::ios::binary);

    // Couldn't open the file for reading.
    if (!ifstream.is_open())
        return unexpected(API_EREAD);

    // Convenience.
    auto size_ = static_cast<m_off_t>(size);

    // Wrap the input stream.
    StandardInputStream istream(ifstream, size_);

    FileFingerprint fingerprint;

    // Try and generate a fingerprint.
    fingerprint.genfingerprint(&istream, modified);

    // Couldn't generate the fingerprint.
    if (!fingerprint.isvalid)
        return unexpected(API_EREAD);

    // Return fingerprint to caller.
    return fingerprint;
}

NodeHandle id(const NodeInfo& info)
{
    return info.mHandle;
}

InodeID id(const InodeInfo& info)
{
    return info.mID;
}

DateTime lastWriteTime(const Path& path)
{
    auto error  = std::error_code();
    auto result = lastWriteTime(path, error);

    if (!error)
        return result;

    throw fs::filesystem_error("Couldn't retrieve modification time",
                               path.path(),
                               error);
}

void lastWriteTime(const Path& path, const DateTime& modified)
{
    std::error_code error;

    lastWriteTime(path, modified, error);

    if (!error)
        return;

    throw fs::filesystem_error("Couldn't set modification time",
                               path.path(),
                               error);
}

NodeHandle parentID(const NodeInfo& info)
{
    return info.mParentHandle;
}

InodeID parentID(const InodeInfo& info)
{
    return info.mParentID;
}

std::string randomBytes(std::size_t length)
{
    static std::mutex mutex;
    static PrnGen rng;

    std::lock_guard<std::mutex> guard(mutex);

    return rng.genstring(length);
}

std::string randomName()
{
    return Base64::btoa(randomBytes(16));
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

StandardInputStream::StandardInputStream(std::istream& stream,
                                         m_off_t size)
  : InputStreamAccess()
  , mStream(stream)
  , mSize(size)
{
}

bool StandardInputStream::read(byte* buffer, unsigned int count)
{
    assert(buffer);
    assert(count);

    auto count_ = static_cast<std::streamsize>(count);

    mStream.read(reinterpret_cast<char*>(buffer), count_);

    return mStream.gcount() == count_;
}

m_off_t StandardInputStream::size()
{
    return mSize;
}

} // testing
} // fuse
} // mega

