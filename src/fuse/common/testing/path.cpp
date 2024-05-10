#include <ostream>

#include <mega/fuse/common/testing/path.h>

namespace mega
{
namespace fuse
{
namespace testing
{

Path::Path(const LocalPath& path)
  : mPath(path.toPath(false))
{
}

Path::Path(const fs::path& path)
  : mPath(path)
{
}

Path::Path(const std::string& path)
  : Path(fs::u8path(path))
{
}

Path::Path(const char* path)
  : Path(fs::u8path(path))
{
}

Path& Path::operator/=(const Path& rhs)
{
    mPath /= rhs.mPath;

    return *this;
}

Path Path::operator/(const Path& rhs) const
{
    return Path(*this) /= rhs;
}

bool Path::operator==(const Path& rhs) const
{
    return mPath == rhs.mPath;
}

bool Path::operator<(const Path& rhs) const
{
    return mPath < rhs.mPath;
}

bool Path::operator!=(const Path& rhs) const
{
    return mPath != rhs.mPath;
}

Path::operator LocalPath() const
{
    return localPath();
}

Path::operator fs::path() const
{
    return path();
}

Path::operator std::string() const
{
    return string();
}

LocalPath Path::localPath() const
{
    return LocalPath::fromAbsolutePath(mPath.u8string());
}

fs::path Path::path() const
{
    return mPath;
}

std::string Path::string() const
{
    return mPath.u8string();
}

std::ostream& operator<<(std::ostream& ostream, const Path& path)
{
    return ostream << path.string();
}

} // testing
} // fuse
} // mega

