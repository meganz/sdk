#include <fstream>

#include <mega/fuse/common/testing/file.h>

#include <mega/logging.h>

namespace mega
{
namespace fuse
{
namespace testing
{

File::File(const std::string& content,
           const std::string& name,
           const Path& parentPath)
  : mPath(parentPath.path() / fs::u8path(name))
{
    std::ofstream ostream;

    // Throw on failure.
    ostream.exceptions(std::ios::badbit | std::ios::failbit);

    // Open file for writing.
    ostream.open(mPath.string(), std::ios::trunc);

    // Write data to the file.
    ostream.write(content.data(),
                  static_cast<std::streamsize>(content.size()));

    // Flush content to disk.
    ostream.flush();
}

File::File(const std::string& content,
           const std::string& name)
  : File(content, name, fs::current_path())
{
}

File::~File()
{
    std::error_code error;

    // Try and remove the file.
    fs::remove(mPath, error);

    if (!error)
        return;

    LOG_warn << "Unable to remove file at: "
             << mPath.localPath();
}

const Path& File::path() const
{
    return mPath;
}

} // testing
} // fuse
} // mega

