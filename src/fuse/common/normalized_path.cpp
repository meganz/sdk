#include <mega/fuse/common/normalized_path.h>

namespace mega
{
namespace fuse
{

NormalizedPath::NormalizedPath(const LocalPath& other)
  : LocalPath(other)
{
    trimNonDriveTrailingSeparator();
}

NormalizedPath& NormalizedPath::operator=(const LocalPath& rhs)
{
    return operator=(NormalizedPath(rhs));
}

} // fuse
} // mega

