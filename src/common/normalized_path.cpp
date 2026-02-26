#include <mega/common/normalized_path.h>

namespace mega
{
namespace common
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

} // common
} // mega

