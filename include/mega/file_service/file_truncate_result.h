#pragma once

#include <mega/file_service/file_truncate_result_forward.h>

#include <cstdint>

namespace mega
{
namespace file_service
{

struct FileTruncateResult
{
    bool operator==(const FileTruncateResult& rhs) const
    {
        return mNewSize == rhs.mNewSize && mOldSize == rhs.mOldSize;
    }

    bool operator!=(const FileTruncateResult& rhs) const
    {
        return !(*this == rhs);
    }

    std::uint64_t mNewSize;
    std::uint64_t mOldSize;
}; // FileTruncateResult

} // file_service
} // mega
