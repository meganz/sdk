#include <mega/file_service/file_range.h>
#include <mega/file_service/file_range_traits.h>

namespace mega
{
namespace file_service
{

static_assert(IsFileRangeV<const FileRange>);
static_assert(IsFileRangeV<const volatile FileRange>);
static_assert(IsFileRangeV<volatile FileRange>);
static_assert(IsFileRangeV<FileRange>);

static_assert(!IsFileRangeV<const FileRange*>);
static_assert(!IsFileRangeV<const FileRange&>);
static_assert(!IsFileRangeV<FileRange*>);
static_assert(!IsFileRangeV<FileRange&>);

static_assert(!IsFileRangeV<int>);

} // file_service
} // mega
