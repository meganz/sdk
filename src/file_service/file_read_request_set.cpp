#include <mega/file_service/file_read_request.h>
#include <mega/file_service/file_read_request_set.h>

namespace mega
{
namespace file_service
{

bool FileReadRequestLess::operator()(const FileReadRequest& lhs, const FileReadRequest& rhs) const
{
    // Convenience.
    auto [i, j] = lhs.mRange;
    auto [m, n] = rhs.mRange;

    if (i < m)
        return true;

    if (i > m)
        return false;

    return j < n;
}

bool FileReadRequestLess::operator()(std::uint64_t lhs, const FileReadRequest& rhs) const
{
    return lhs < rhs.mRange.mBegin;
}

} // file_service
} // mega
