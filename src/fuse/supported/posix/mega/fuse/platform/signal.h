#pragma once

#include <string>

#include <mega/fuse/platform/file_descriptor.h>

namespace mega
{
namespace fuse
{
namespace platform
{

class Signal
{
    std::string mName;
    FileDescriptor mReader;
    FileDescriptor mWriter;

public:
    Signal(const std::string& name);

    Signal(Signal&& other) = default;
    
    ~Signal() = default;

    Signal& operator=(Signal&& rhs) = default;

    void clear();

    int descriptor() const;

    const std::string& name() const;

    void raise();

    void swap(Signal& other);
}; // Signal

} // platform
} // fuse
} // mega

