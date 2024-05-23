#include <mega/fuse/common/testing/directory.h>

#include <mega/logging.h>

namespace mega
{
namespace fuse
{
namespace testing
{

Directory::Directory(const std::string& name,
                     const Path& parentPath)
  : mPath(parentPath.path() / fs::u8path(name))
{
    fs::create_directories(mPath.path());
}

Directory::Directory(const std::string& name)
  : mPath(name)
{
}

Directory::~Directory()
{
    std::error_code error;

    fs::remove_all(mPath.path(), error);

    if (!error)
        return;

    LOG_warn << "Unable to remove directory at: "
             << mPath.localPath();
}

const Path& Directory::path() const
{
    return mPath;
}

} // testing
} // fuse
} // mega

