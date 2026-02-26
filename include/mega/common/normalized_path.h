#pragma once

#include <mega/common/normalized_path_forward.h>

#include <mega/filesystem.h>

namespace mega
{
namespace common
{

class NormalizedPath
  : public LocalPath
{
public:
    using LocalPath::operator==;
    using LocalPath::operator!=;

    NormalizedPath() = default;

    NormalizedPath(const NormalizedPath& other) = default;

    NormalizedPath(const LocalPath& other);

    NormalizedPath& operator=(const NormalizedPath& rhs) = default;

    NormalizedPath& operator=(const LocalPath& rhs);
}; // NormalizedPath

} // common
} // mega

