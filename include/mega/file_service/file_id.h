#pragma once

#include <mega/common/query_forward.h>
#include <mega/file_service/file_id_forward.h>

#include <cstdint>
#include <string>

namespace mega
{

class NodeHandle;

namespace common
{

template<>
struct SerializationTraits<file_service::FileID>
{
    static auto from(const Field& field) -> file_service::FileID;

    static auto to(Parameter& parameter, file_service::FileID id) -> void;
}; // SerializationTraits<file_service::FileID>

} // common

namespace file_service
{

class FileID
{
    explicit FileID(std::uint64_t id);

    std::uint64_t mID;

public:
    FileID();

    operator bool() const;

    auto operator==(const FileID& rhs) const -> bool;

    auto operator<(const FileID& rhs) const -> bool;

    auto operator!=(const FileID& rhs) const -> bool;

    auto operator!() const -> bool;

    static auto from(NodeHandle handle) -> FileID;

    static auto from(std::uint64_t u64) -> FileID;

    auto toHandle() const -> NodeHandle;

    auto toU64() const -> std::uint64_t;
}; // FileID

auto synthetic(FileID id) -> bool;

auto synthetic(std::uint64_t u64) -> bool;

auto toString(FileID id) -> std::string;

} // file_service
} // mega
