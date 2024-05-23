#pragma once

#include <string>

#include <mega/fuse/common/bind_handle_forward.h>

namespace mega
{
namespace fuse
{

class BindHandle
{
    std::string mNodeKey;

public:
    BindHandle() = default;

    explicit BindHandle(std::string nodeKey);

    BindHandle(const BindHandle& other) = default;

    BindHandle(BindHandle&& other) = default;

    BindHandle& operator=(const BindHandle& rhs) = default;

    BindHandle& operator=(BindHandle&& rhs) = default;

    operator bool() const;

    bool operator==(const BindHandle& rhs) const;

    bool operator<(const BindHandle& rhs) const;

    bool operator!() const;

    bool operator!=(const BindHandle& rhs) const;

    const std::string& get() const;
}; // BindHandle

} // fuse
} // mega

