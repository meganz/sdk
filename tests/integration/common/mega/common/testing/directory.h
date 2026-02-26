#pragma once

#include <mega/common/testing/path.h>

#include <string>

namespace mega
{
namespace common
{
namespace testing
{

class Directory
{
    Path mPath;

public:
    Directory(const std::string& name, const Path& parentPath);

    Directory(const std::string& name);

    ~Directory();

    const Path& path() const;
}; // Directory

} // testing
} // common
} // mega
