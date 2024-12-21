#include <mega/fuse/platform/windows.h>
#include <sddl.h>

#include <cassert>
#include <memory>
#include <sstream>
#include <utility>

#include <mega/fuse/common/logging.h>
#include <mega/fuse/platform/security_descriptor.h>
#include <mega/fuse/platform/security_identifier.h>

namespace mega
{
namespace fuse
{
namespace platform
{

SecurityDescriptor::SecurityDescriptor()
  : mDescriptor()
{
}

SecurityDescriptor::SecurityDescriptor(LocalPtr<void> descriptor)
  : mDescriptor(std::move(descriptor))
{
}

SecurityDescriptor::SecurityDescriptor(const SecurityDescriptor& other)
  : mDescriptor()
{
    if (!other.mDescriptor)
        return;

    auto length = other.length();

    mDescriptor.reset(LocalAlloc(LMEM_FIXED, length));

    if (!mDescriptor)
        throw FUSEErrorF("Couldn't allocate security descriptor: %u",
                         GetLastError());

    std::memcpy(mDescriptor.get(),
                other.mDescriptor.get(),
                length);
}

SecurityDescriptor::operator bool() const
{
    return !!mDescriptor;
}

bool SecurityDescriptor::operator!() const
{
    return !mDescriptor;
}

SecurityDescriptor& SecurityDescriptor::operator=(const SecurityDescriptor& rhs)
{
    if (this == &rhs)
        return *this;

    SecurityDescriptor temp(rhs);

    swap(temp);

    return *this;
}

bool SecurityDescriptor::operator==(const SecurityDescriptor& rhs) const
{
    if (mDescriptor && rhs.mDescriptor)
        return EqualSid(mDescriptor.get(), rhs.mDescriptor.get());

    return !mDescriptor == !rhs.mDescriptor;
}

bool SecurityDescriptor::operator!=(const SecurityDescriptor& rhs) const
{
    return !(*this == rhs);
}

SecurityDescriptor SecurityDescriptor::fromString(const std::string& text)
{
    return fromString(text.c_str());
}

SecurityDescriptor SecurityDescriptor::fromString(const char* text)
{
    assert(text);

    PSECURITY_DESCRIPTOR descriptor;
    ULONG length;
    auto result =
      ConvertStringSecurityDescriptorToSecurityDescriptorA(
        text,
        SDDL_REVISION_1,
        &descriptor,
        &length);

    if (!result)
        throw FUSEErrorF("Couldn't deserialize security descriptor: %u",
                         GetLastError());

    return SecurityDescriptor(LocalPtr<void>(descriptor));
}

void* SecurityDescriptor::get() const
{
    return mDescriptor.get();
}

std::size_t SecurityDescriptor::length() const
{
    if (mDescriptor)
        return GetSecurityDescriptorLength(mDescriptor.get());

    return 0u;
}

std::uint32_t SecurityDescriptor::modify(const SecurityDescriptor& modifications,
                                         std::uint32_t mask)
{
    return modify(modifications.get(), mask);
}

std::uint32_t SecurityDescriptor::modify(void* modifications, std::uint32_t mask)
{
    static GENERIC_MAPPING mapping = {
        FILE_GENERIC_READ,
        FILE_GENERIC_WRITE,
        FILE_GENERIC_EXECUTE,
        FILE_ALL_ACCESS
    }; // mapping

    assert(modifications);

    // Get our hands on the process' heap.
    auto heap = GetProcessHeap();

    // How large is this descriptor?
    auto length = this->length();

    // Create a mutable copy of this descriptor.
    auto* modified = HeapAlloc(heap, 0, length);

    // Couldn't create a mutable copy of this descriptor.
    if (!modified)
        return GetLastError();

    std::memcpy(modified, mDescriptor.get(), length);

    // Can't update the descriptor.
    if (!SetPrivateObjectSecurity(mask,
                                  modifications,
                                  &modified,
                                  &mapping,
                                  nullptr))
    {
        // Release the mutable copy.
        HeapFree(heap, 0, modified);

        // Let the caller know why we failed.
        return GetLastError();
    }

    // How large is the updated descriptor?
    length = GetSecurityDescriptorLength(modified);

    // Allocate a permanent home for the updated descriptor.
    LocalPtr<void> descriptor(LocalAlloc(LMEM_FIXED, length));

    // Couldn't allocate permanent home.
    if (!descriptor)
    {
        // Release updated descriptor.
        DestroyPrivateObjectSecurity(&modified);

        // Let the caller knwo why we failed.
        return GetLastError();
    }

    // Copy updated descriptor to new home.
    std::memcpy(descriptor.get(), modified, length);

    // Release updated descriptor.
    DestroyPrivateObjectSecurity(&modified);

    // Swap descriptor.
    mDescriptor = std::move(descriptor);

    // Let the caller know the update was successful.
    return ERROR_SUCCESS;
}

void* SecurityDescriptor::release()
{
    return mDescriptor.release();
}

void SecurityDescriptor::reset(void* descriptor)
{
    mDescriptor.reset(descriptor);
}

void SecurityDescriptor::swap(SecurityDescriptor& other)
{
    using std::swap;

    swap(mDescriptor, other.mDescriptor);
}

SecurityDescriptor readOnlySecurityDescriptor()
{
    std::ostringstream ostream;

    ostream << "O:"
            << toString(SecurityIdentifier::user())
            << "G:"
            << toString(SecurityIdentifier::group())
            << "D:P"
            << "(A;;FRFX;;;WD)";

    return SecurityDescriptor::fromString(ostream.str());
}

SecurityDescriptor readWriteSecurityDescriptor()
{
    auto user = toString(SecurityIdentifier::user());

    std::ostringstream ostream;

    ostream << "O:"
            << user
            << "G:"
            << toString(SecurityIdentifier::group())
            << "D:P"
            << "(A;;FA;;;"
            << user
            << ")"
            << "(A;;FRFX;;;WD)";

    return SecurityDescriptor::fromString(ostream.str());
}

void swap(SecurityDescriptor& lhs, SecurityDescriptor& rhs)
{
    lhs.swap(rhs);
}

std::string toString(const SecurityDescriptor& descriptor)
{
    if (!descriptor)
        return std::string();

    PSTR text;
    auto result =
      ConvertSecurityDescriptorToStringSecurityDescriptorA(
        descriptor.get(),
        SDDL_REVISION_1,
        DACL_SECURITY_INFORMATION
        | GROUP_SECURITY_INFORMATION
        | OWNER_SECURITY_INFORMATION
        | SACL_SECURITY_INFORMATION,
        &text,
        nullptr);

    if (!result)
        throw FUSEErrorF("Couldn't serialize security descriptor: %u",
                         GetLastError());

    LocalPtr<CHAR> text_(text);

    return std::string(text_.get());
}

} // platform
} // fuse
} // mega

