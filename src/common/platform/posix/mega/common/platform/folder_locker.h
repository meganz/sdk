#pragma once

#include <string>

namespace mega
{
namespace common
{
namespace platform
{

// Dummy implementation
class FolderLocker
{
public:
    FolderLocker() = default;

    FolderLocker(const std::string&) {}

    FolderLocker& operator=(FolderLocker&&) = default;

    void release(){};
};

} // platform
} // common
} // mega
