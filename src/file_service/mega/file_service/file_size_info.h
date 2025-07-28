#pragma once

#include <mega/file_service/file_size_info_forward.h>

#include <cstdint>

namespace mega
{
namespace file_service
{

class FileSizeInfo
{
protected:
    FileSizeInfo() = default;

public:
    virtual ~FileSizeInfo() = default;

    // Update this file's allocated size.
    virtual void allocatedSize(std::uint64_t allocatedSize) = 0;

    // How much disk space has been allocated to this file?
    virtual std::uint64_t allocatedSize() const = 0;

    // Update this file's reported size.
    virtual void reportedSize(std::uint64_t reportedSize) = 0;

    // How large does the filesystem say this file is?
    virtual std::uint64_t reportedSize() const = 0;

    // How large is this file conceptually?
    virtual std::uint64_t size() const = 0;
}; // FileSizeInfo

} // file_service
} // mega
