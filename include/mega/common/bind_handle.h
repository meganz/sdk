#pragma once

#include <string>

#include <mega/common/bind_handle_forward.h>
#include <mega/common/query_forward.h>

namespace mega
{
namespace common
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

template<>
struct SerializationTraits<common::BindHandle>
{
    static common::BindHandle from(const Field& field);

    static void to(Parameter& parameter, const common::BindHandle& value);
}; // SerializationTraits<BindHandle>

} // common
} // mega

