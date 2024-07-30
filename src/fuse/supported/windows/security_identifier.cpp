#include <mega/fuse/platform/windows.h>
#include <sddl.h>

#include <cassert>
#include <cstring>

#include <mega/fuse/common/logging.h>
#include <mega/fuse/platform/security_identifier.h>

namespace mega
{
namespace fuse
{
namespace platform
{

static LocalPtr<void> get(TOKEN_INFORMATION_CLASS type);

SecurityIdentifier::SecurityIdentifier()
  : mIdentifier()
{
}

SecurityIdentifier::SecurityIdentifier(void* identifier)
  : mIdentifier(identifier)
{
}

SecurityIdentifier::SecurityIdentifier(const SecurityIdentifier& other)
  : mIdentifier()
{
    if (!other)
        return;

    auto length = GetLengthSid(other.mIdentifier.get());

    mIdentifier.reset(LocalAlloc(LMEM_FIXED, length));

    if (!mIdentifier)
        throw FUSEErrorF("Couldn't allocate security identifier: %u",
                         GetLastError());

    if (!CopySid(length, mIdentifier.get(), other.mIdentifier.get()))
        throw FUSEErrorF("Couldn't copy security identifier: %u",
                         GetLastError());
}

SecurityIdentifier::operator bool() const
{
    return !!mIdentifier;
}

bool SecurityIdentifier::operator!() const
{
    return !mIdentifier;
}

SecurityIdentifier& SecurityIdentifier::operator=(const SecurityIdentifier& rhs)
{
    if (this == &rhs)
        return *this;

    SecurityIdentifier temp(rhs);

    swap(temp);

    return *this;
}

SecurityIdentifier SecurityIdentifier::fromString(const std::string& text)
{
    return fromString(text.c_str());
}

SecurityIdentifier SecurityIdentifier::fromString(const char* text)
{
    assert(text);

    PSID sid;

    if (!ConvertStringSidToSidA(text, &sid))
        throw FUSEErrorF("Couldn't deserialize security identifier: %u",
                         GetLastError());

    return SecurityIdentifier(sid);
}

void* SecurityIdentifier::get() const
{
    return mIdentifier.get();
}

SecurityIdentifier SecurityIdentifier::group()
{
    auto identifier = platform::get(TokenPrimaryGroup);

    if (!identifier)
        throw FUSEErrorF("Couldn't retrieve group security identifier: %u",
                         GetLastError());

    return SecurityIdentifier(identifier.release());
}

std::size_t SecurityIdentifier::length() const
{
    assert(mIdentifier);

    return GetLengthSid(mIdentifier.get());
}

void* SecurityIdentifier::release()
{
    return mIdentifier.release();
}

void SecurityIdentifier::reset(void* identifier)
{
    mIdentifier.reset(identifier);
}

void SecurityIdentifier::swap(SecurityIdentifier& other)
{
    using std::swap;

    swap(mIdentifier, other.mIdentifier);
}

SecurityIdentifier SecurityIdentifier::user()
{
    auto identifier = platform::get(TokenOwner);

    if (!identifier)
        throw FUSEErrorF("Couldn't retrieve user security identifier: %u",
                         GetLastError());

    return SecurityIdentifier(identifier.release());
}

void swap(SecurityIdentifier& lhs, SecurityIdentifier& rhs)
{
    lhs.swap(rhs);
}

std::string toString(const SecurityIdentifier& identifier)
{
    assert(identifier);

    PSTR text;

    if (!ConvertSidToStringSidA(identifier.get(), &text))
        throw FUSEErrorF("Couldn't serialize security identifier: %u",
                         GetLastError());

    LocalPtr<CHAR> text_(text);

    return std::string(text_.get());
}

LocalPtr<void> get(TOKEN_INFORMATION_CLASS type)
{
    // Sanity.
    assert(type == TokenOwner || type == TokenPrimaryGroup);

    // Get our hands on this process's handle.
    auto handle = GetCurrentProcess();

    // Try and get our hands on the thread's token.
    if (!OpenProcessToken(handle, TOKEN_QUERY, &handle))
        return nullptr;

    auto required = 0ul;

    // Try and determine how much buffer space we need.
    auto result = GetTokenInformation(handle, type, nullptr, 0, &required);

    // Couldn't determine buffer requirement.
    if (!result && GetLastError() != ERROR_INSUFFICIENT_BUFFER)
        return nullptr;

    // Try and allocate memory for our buffer.
    LocalPtr<void*> temp(static_cast<void**>(LocalAlloc(LMEM_FIXED, required)));

    // Couldn't allocate memory for buffer.
    if (!temp)
        return nullptr;

    // Try and retrieve requested information.
    result = GetTokenInformation(handle,
                                 type,
                                 temp.get(), 
                                 required,
                                 &required);

    // Couldn't retrieve information.
    if (!result)
        return nullptr;

    // How large is the SID we've retrieved?
    required = GetLengthSid(*temp);

    // Try and allocate buffer for SID.
    LocalPtr<void> identifier(LocalAlloc(LMEM_FIXED, required));

    // Couldn't allocate buffer for SID.
    if (!identifier)
        return nullptr;

    // Copy SID from temporary buffer.
    if (!CopySid(required, identifier.get(), *temp))
        return nullptr;

    // Return SID to caller.
    return identifier;
}

} // platform
} // fuse
} // mega

