#include <mega/file_service/type_traits.h>

namespace mega
{
namespace file_service
{

static_assert(IsNoneSuchV<NoneSuch>);
static_assert(!IsNoneSuchV<int>);

static_assert(IsNotNoneSuchV<int>);
static_assert(!IsNotNoneSuchV<NoneSuch>);

} // file_service
} // mega
