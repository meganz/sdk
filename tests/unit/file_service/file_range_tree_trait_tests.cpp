#include <mega/file_service/file_range.h>
#include <mega/file_service/file_range_tree_traits.h>
#include <mega/file_service/type_traits.h>

namespace mega
{
namespace file_service
{
namespace detail
{

struct KeyFunctionA
{
    void operator()(const FileRange&) const;
}; // KeyFunctionC

struct KeyFunctionB
{
    const FileRange& operator()(int) const;
}; // KeyFunctionD

static_assert(IsValidKeyFunctionV<Identity, const FileRange>);
static_assert(IsValidKeyFunctionV<SelectFirst, std::pair<const FileRange, int>>);

static_assert(!IsValidKeyFunctionV<KeyFunctionA, const FileRange>);
static_assert(!IsValidKeyFunctionV<KeyFunctionB, const FileRange>);

static_assert(IsValidValueTypeV<const FileRange>);
static_assert(IsValidValueTypeV<FileRange>);
static_assert(!IsValidValueTypeV<int>);

static_assert(IsValidValueTypeV<std::pair<const FileRange, int>>);
static_assert(IsValidValueTypeV<std::pair<FileRange, int>>);
static_assert(!IsValidValueTypeV<std::pair<std::pair<FileRange, int>, int>>);

} // detail
} // file_service
} // mega
