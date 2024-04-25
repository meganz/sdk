#pragma once

#include <mega/fuse/common/testing/path.h>

#include <tests/stdfs.h>

namespace mega
{
namespace fuse
{
namespace testing
{

class File
{
    Path mPath;

public:
    File(const std::string& content,
         const std::string& name,
         const Path& parentPath);

    File(const std::string& content,
         const std::string& name);

    ~File();

    const Path& path() const;
}; // File

} // testing
} // fuse
} // mega

