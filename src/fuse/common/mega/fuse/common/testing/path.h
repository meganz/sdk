#pragma once

#include <iosfwd>

#include <mega/fuse/common/testing/path_forward.h>

#include <mega/filesystem.h>

#include <tests/stdfs.h>

namespace mega
{
namespace fuse
{
namespace testing
{

class Path
{
    fs::path mPath;

public:
    Path() = default;

    Path(const Path& other) = default;

    Path(Path&& other) = default;

    Path(const LocalPath& path);

    Path(const fs::path& path);

    Path(const std::string& path);

    Path(const char* path);

    Path& operator/=(const Path& rhs);

    Path operator/(const Path& rhs) const;

    Path& operator=(const Path& rhs) = default;

    Path& operator=(Path&& rhs) = default;

    bool operator==(const Path& rhs) const;

    bool operator<(const Path& rhs) const;

    bool operator!=(const Path& rhs) const;

    operator LocalPath() const;

    operator fs::path() const;

    operator std::string() const;

    LocalPath localPath() const;

    fs::path path() const;

    std::string string() const;
}; // Path

std::ostream& operator<<(std::ostream& ostream, const Path& path);

} // testing
} // fuse
} // mega

