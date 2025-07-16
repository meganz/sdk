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

    // How large is this file?
    virtual std::uint64_t logicalSize() const = 0;

    // Update the file's physical size.
    virtual void physicalSize(std::uint64_t physicalSize) = 0;

    // What is the file's size on disk?
    virtual std::uint64_t physicalSize() const = 0;
}; // FileSizeInfo

} // file_service
} // mega
