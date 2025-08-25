#pragma once

#include <mega/common/testing/path.h>

#include <string>

namespace mega
{
namespace fuse
{
namespace testing
{

class Directory
{
    common::testing::Path mPath;

public:
    Directory(const std::string& name, const common::testing::Path& parentPath);

    Directory(const std::string& name);

    ~Directory();

    const common::testing::Path& path() const;
}; // Directory

} // testing
} // fuse
} // mega

