#pragma once

#include <mega/file_service/file_location_forward.h>
#include <mega/types.h>

#include <string>
#include <tuple>

namespace mega
{
namespace file_service
{

struct FileLocation
{
    bool operator<(const FileLocation& rhs) const
    {
        return std::tie(mParentHandle, mName) < std::tie(rhs.mParentHandle, rhs.mName);
    }

    bool operator==(const FileLocation& rhs) const
    {
        return mParentHandle == rhs.mParentHandle && mName == rhs.mName;
    }

    bool operator!=(const FileLocation& rhs) const
    {
        return !(*this == rhs);
    }

    // What is this file's name?
    std::string mName;

    // Who is this file's parent?
    NodeHandle mParentHandle;
}; // FileLocation

} // file_service
} // mega
