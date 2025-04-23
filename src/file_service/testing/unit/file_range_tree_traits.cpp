#include <mega/file_service/file_range_tree_traits.h>

namespace mega
{
namespace file_service
{
namespace detail
{

static_assert(IsValidValueTypeV<const FileRange>);
static_assert(IsValidValueTypeV<FileRange>);
static_assert(!IsValidValueTypeV<int>);

static_assert(IsValidValueTypeV<std::pair<const FileRange, int>>);
static_assert(IsValidValueTypeV<std::pair<FileRange, int>>);
static_assert(!IsValidValueTypeV<std::pair<std::pair<FileRange, int>, int>>);

} // detail
} // file_service
} // mega
