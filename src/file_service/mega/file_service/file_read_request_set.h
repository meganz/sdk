#pragma once

#include <mega/file_service/file_read_request_forward.h>

#include <cstdint>
#include <set>

namespace mega
{
namespace file_service
{

struct FileReadRequestLess
{
    using is_transparent = void;

    bool operator()(const FileReadRequest& lhs, const FileReadRequest& rhs) const;

    bool operator()(std::uint64_t lhs, const FileReadRequest& rhs) const;
}; // FileReadRequestLess

using FileReadRequestSet = std::set<FileReadRequest, FileReadRequestLess>;

} // file_service
} // mega
