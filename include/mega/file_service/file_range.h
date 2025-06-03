#pragma once

#include <cstdint>
#include <ostream>
#include <string>

namespace mega
{
namespace file_service
{

struct FileRange
{
    FileRange() = default;

    FileRange(std::uint64_t begin, std::uint64_t end);

    bool operator==(const FileRange& rhs) const
    {
        return mBegin == rhs.mBegin && mEnd == rhs.mEnd;
    }

    bool operator!=(const FileRange& rhs) const
    {
        return !(*this == rhs);
    }

    std::uint64_t mBegin;
    std::uint64_t mEnd;
}; // FileRange

std::ostream& operator<<(std::ostream& ostream, const FileRange& range);

FileRange combine(const FileRange& lhs, const FileRange& rhs);

FileRange extend(const FileRange& range, std::uint64_t adjustment);

std::string toString(const FileRange& range);

} // file_service
} // mega
