#include <mega/fuse/common/file_move_flag.h>

namespace mega
{
namespace fuse
{

bool valid(FileMoveFlags flags)
{
    // Make sure only a single flag has been set.
    return (flags & (flags - 1)) == 0;
}

} // fuse
} // mega

