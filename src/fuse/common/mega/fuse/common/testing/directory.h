#pragma once

#include <string>

#include <mega/fuse/common/testing/path.h>

namespace mega
{
namespace fuse
{
namespace testing
{

class Directory
{
    Path mPath;

public:
    Directory(const std::string& name,
              const Path& parentPath);

    Directory(const std::string& name);

    ~Directory();

    const Path& path() const;
}; // Directory

} // testing
} // fuse
} // mega

