#include <mega/file_service/file_range_tree.h>
#include <mega/file_service/type_traits.h>

#include <utility>

namespace mega
{
namespace file_service
{

template<typename ValueType>
using FileRangeMap = FileRangeTree<SelectFirst, std::pair<const FileRange, ValueType>>;

} // file_service
} // mega
