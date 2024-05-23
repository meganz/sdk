#pragma once

#include <cstddef>
#include <string>

#include <mega/fuse/platform/file_descriptor_forward.h>

#include <mega/types.h>

namespace mega
{
namespace fuse
{
namespace platform
{

class FileDescriptor
{
    int mDescriptor;

public:
    explicit FileDescriptor(int descriptor = -1, bool closeOnFork = true);

    FileDescriptor(FileDescriptor&& other);

    ~FileDescriptor();

    operator bool() const;

    bool operator!() const;

    FileDescriptor& operator=(FileDescriptor&& rhs);

    void closeOnFork(bool closeOnFork);

    bool closeOnFork() const;

    void flags(int flags);

    int flags() const;

    int get() const;

    std::size_t read(void* buffer, std::size_t length);

    std::size_t read(void* buffer,
                     std::size_t length,
                     m_off_t offset);

    std::string readAll();

    void redirect(const FileDescriptor& other);

    int release();

    void reset(int descriptor = -1);

    void swap(FileDescriptor& other);

    std::size_t write(const void* buffer, std::size_t length);

    std::size_t write(const void* buffer,
                      std::size_t length,
                      m_off_t offset);
}; // FileDescriptor

} // platform
} // fuse
} // mega

