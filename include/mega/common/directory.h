#pragma once

#include <mega/common/logger_forward.h>
#include <mega/localpath.h>

namespace mega
{

namespace common
{

class Directory
{
    FileSystemAccess& mFilesystem;
    LocalPath mPath;

public:
    Directory(FileSystemAccess& filesystem,
              Logger& logger,
              const std::string& name,
              const LocalPath& rootPath);

    operator const LocalPath&() const;

    auto path() const -> const LocalPath&;
}; // Directory

} // common
} // mega
