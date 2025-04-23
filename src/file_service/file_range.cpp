#include <mega/common/utility.h>
#include <mega/file_service/file_range.h>

#include <cinttypes>
#include <utility>

namespace mega
{
namespace file_service
{

using namespace common;

FileRange combine(const FileRange& lhs, const FileRange& rhs)
{
    auto begin = std::min(lhs.mBegin, rhs.mBegin);
    auto end = std::max(lhs.mEnd, rhs.mEnd);

    return FileRange(begin, end);
}

std::string toString(const FileRange& range)
{
    return format("[0x%" PRIx64 "-0x%" PRIx64 "]", range.mBegin, range.mEnd);
}

std::ostream& operator<<(std::ostream& ostream, const FileRange& range)
{
    return ostream << toString(range);
}

} // file_service
} // mega
