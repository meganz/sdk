#include <mega/common/directory.h>
#include <mega/common/logging.h>
#include <mega/filesystem.h>

namespace mega
{
namespace common
{

Directory::Directory(FileSystemAccess& filesystem,
                     Logger& logger,
                     const std::string& name,
                     const LocalPath& rootPath):
    mFilesystem(filesystem),
    mPath(rootPath)
{
    mPath.appendWithSeparator(LocalPath::fromRelativePath(name), true);

    if (!mFilesystem.mkdirlocal(mPath, false, false) && !mFilesystem.target_exists)
        throw LogErrorF(logger, "Couldn't create directory: %s", mPath.toPath(false).c_str());

    LogDebugF(logger, "Created directory: %s", mPath.toPath(false).c_str());
}

Directory::operator const LocalPath&() const
{
    return path();
}

auto Directory::path() const -> const LocalPath&
{
    return mPath;
}

} // common
} // mega
