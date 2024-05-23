#pragma once

#include <mega/fuse/platform/local_pointer.h>
#include <mega/fuse/platform/security_identifier_forward.h>

#include <cstddef>
#include <cstdint>
#include <string>

namespace mega
{
namespace fuse
{
namespace platform
{

class SecurityIdentifier
{
    LocalPtr<void> mIdentifier;

public:
    SecurityIdentifier();

    explicit SecurityIdentifier(void* identifier);

    SecurityIdentifier(const SecurityIdentifier& other);

    SecurityIdentifier(SecurityIdentifier&& other) = default;

    operator bool() const;

    bool operator!() const;

    SecurityIdentifier& operator=(const SecurityIdentifier& rhs);

    SecurityIdentifier& operator=(SecurityIdentifier&& rhs) = default;

    static SecurityIdentifier fromString(const std::string& text);

    static SecurityIdentifier fromString(const char* text);

    void* get() const;

    static SecurityIdentifier group();

    std::size_t length() const;

    void* release();

    void reset(void* identifier = nullptr);

    void swap(SecurityIdentifier& other);

    static SecurityIdentifier user();
}; // SecurityIdentifier

void swap(SecurityIdentifier& lhs, SecurityIdentifier& rhs);

std::string toString(const SecurityIdentifier& identifier);

} // platform
} // fuse
} // mega

