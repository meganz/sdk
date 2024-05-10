#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include <mega/fuse/platform/local_pointer.h>
#include <mega/fuse/platform/security_descriptor_forward.h>

namespace mega
{
namespace fuse
{
namespace platform
{

class SecurityDescriptor
{
    LocalPtr<void> mDescriptor;

public:
    SecurityDescriptor();

    explicit SecurityDescriptor(LocalPtr<void> descriptor);

    SecurityDescriptor(const SecurityDescriptor& other);

    SecurityDescriptor(SecurityDescriptor&& other) = default;

    operator bool() const;

    bool operator!() const;

    SecurityDescriptor& operator=(const SecurityDescriptor& rhs);

    SecurityDescriptor& operator=(SecurityDescriptor&& rhs) = default;

    bool operator==(const SecurityDescriptor& rhs) const;

    bool operator!=(const SecurityDescriptor& rhs) const;

    static SecurityDescriptor fromString(const std::string& text);

    static SecurityDescriptor fromString(const char* text);

    void* get() const;

    std::size_t length() const;

    std::uint32_t modify(const SecurityDescriptor& modifications,
                         std::uint32_t mask);

    std::uint32_t modify(void* modifications, std::uint32_t mask);

    void* release();

    void reset(void* descriptor = nullptr);

    void swap(SecurityDescriptor& other);
}; // SecurityDescriptor

SecurityDescriptor readOnlySecurityDescriptor();

SecurityDescriptor readWriteSecurityDescriptor();

void swap(SecurityDescriptor& lhs, SecurityDescriptor& rhs);

std::string toString(const SecurityDescriptor& descriptor);

} // platform
} // fuse
} // mega

