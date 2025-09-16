#pragma once

#include "stdfs.h"

#include <mega/common/testing/path.h>

namespace mega
{
namespace common
{
namespace testing
{

class File
{
    Path mPath;

public:
    File(const std::string& content, const std::string& name, const Path& parentPath);

    File(const std::string& content, const std::string& name);

    ~File();

    const Path& path() const;
}; // File

} // testing
} // common
} // mega
