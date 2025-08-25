#pragma once

#include <mega/common/testing/path.h>
#include <tests/stdfs.h>

namespace mega
{
namespace fuse
{
namespace testing
{

class File
{
    common::testing::Path mPath;

public:
    File(const std::string& content,
         const std::string& name,
         const common::testing::Path& parentPath);

    File(const std::string& content,
         const std::string& name);

    ~File();

    const common::testing::Path& path() const;
}; // File

} // testing
} // fuse
} // mega

