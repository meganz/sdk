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
    static file_service::FileID from(const Field& field);

    static void to(Parameter& parameter, file_service::FileID id);
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

    bool operator==(const FileID& rhs) const;

    bool operator<(const FileID& rhs) const;

    bool operator!=(const FileID& rhs) const;

    bool operator!() const;

    static FileID from(NodeHandle handle);

    static FileID from(std::uint64_t u64);

    NodeHandle toHandle() const;

    std::uint64_t toU64() const;
}; // FileID

bool synthetic(FileID id);

bool synthetic(std::uint64_t u64);

std::string toString(FileID id);

} // file_service
} // mega
