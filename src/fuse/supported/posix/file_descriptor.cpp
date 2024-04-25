#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <mutex>
#include <set>
#include <utility>

#include <mega/fuse/common/logging.h>
#include <mega/fuse/platform/file_descriptor.h>

namespace mega
{
namespace fuse
{
namespace platform
{

FileDescriptor::FileDescriptor(int descriptor, bool closeOnFork)
  : mDescriptor(descriptor)
{
    if (mDescriptor >= 0)
        this->closeOnFork(closeOnFork);
}

FileDescriptor::FileDescriptor(FileDescriptor&& other)
  : mDescriptor(std::move(other.mDescriptor))
{
    other.mDescriptor = -1;
}

FileDescriptor::~FileDescriptor()
{
    if (mDescriptor < 0)
        return;

    auto result = close(mDescriptor);

    while (result < 0 && errno == EINTR)
        result = close(mDescriptor);

    if (result < 0)
        FUSEErrorF("Unable to close descriptor: %d: %s",
                   mDescriptor,
                   std::strerror(errno));
}

FileDescriptor::operator bool() const
{
    return mDescriptor >= 0;
}

bool FileDescriptor::operator!() const
{
    return mDescriptor < 0;
}

FileDescriptor& FileDescriptor::operator=(FileDescriptor&& rhs)
{
    FileDescriptor temp(std::move(rhs));

    swap(temp);

    return *this;
}

void FileDescriptor::closeOnFork(bool closeOnFork)
{
    auto flags = this->flags() | FD_CLOEXEC;

    if (closeOnFork)
        return this->flags(flags | FD_CLOEXEC);

    this->flags(flags & ~FD_CLOEXEC);
}

bool FileDescriptor::closeOnFork() const
{
    return (flags() & FD_CLOEXEC) > 0;
}

void FileDescriptor::flags(int flags)
{
    errno = 0;

    if (fcntl(mDescriptor, F_SETFD, flags) < 0)
        throw FUSEErrorF("Unable to set descriptor flags: %d: %s",
                         mDescriptor,
                         std::strerror(errno));
}

int FileDescriptor::flags() const
{
    errno = 0;

    auto flags = fcntl(mDescriptor, F_GETFD);

    if (flags < 0)
        throw FUSEErrorF("Unable to retrieve descriptor flags: %d: %s",
                         mDescriptor,
                         std::strerror(errno));

    return flags;
}

int FileDescriptor::get() const
{
    return mDescriptor;
}

std::size_t FileDescriptor::read(void* buffer, std::size_t length)
{
    auto* m = reinterpret_cast<char*>(buffer);

    std::size_t numRead = 0;

    while (numRead < length)
    {
        auto result = ::read(mDescriptor, m + numRead, length - numRead);

        if (result < 0)
        {
            if (errno == EINTR)
                continue;

            throw FUSEErrorF("Unable to read from descriptor: %d: %s",
                             mDescriptor,
                             std::strerror(errno));
        }

        if (!result)
            return numRead;

        numRead += static_cast<std::size_t>(result);
    }

    return numRead;
}

std::size_t FileDescriptor::read(void* buffer,
                                 std::size_t length,
                                 m_off_t offset)
{
    auto* m = reinterpret_cast<char*>(buffer);

    std::size_t numRead = 0;

    while (numRead < length)
    {
        auto result = pread(mDescriptor,
                            m + numRead,
                            length - numRead,
                            offset);

        if (result < 0)
        {
            if (errno == EINTR)
                continue;

            throw FUSEErrorF("Unable to read from descriptor: %d: %s",
                             mDescriptor,
                             std::strerror(errno));
        }

        if (!result)
            return numRead;

        numRead += static_cast<std::size_t>(result);
        offset += result;
    }

    return numRead;
}

std::string FileDescriptor::readAll()
{
    for (std::string buffer; ; )
    {
        constexpr std::size_t BLOCK_SIZE = 4096;

        // Latch buffer's current size.
        auto size = buffer.size();

        // Allocate additional buffer space.
        buffer.resize(size + BLOCK_SIZE);

        // Read bytes into buffer.
        auto numRead = read(&buffer[size], BLOCK_SIZE);

        // Read an entire block.
        if (numRead == BLOCK_SIZE)
            continue;

        // Shrink buffer to size.
        buffer.resize(size + numRead);
        buffer.shrink_to_fit();

        // Return buffer to caller.
        return buffer;
    }
}

void FileDescriptor::redirect(const FileDescriptor& other)
{
    while (true)
    {
        if (dup2(mDescriptor, other.mDescriptor) >= 0)
            return;

        if (errno == EINTR)
            continue;

        throw FUSEErrorF("Unable to redirect descriptor %d to %d: %s",
                         mDescriptor,
                         other.mDescriptor,
                         std::strerror(errno));
    }
}

int FileDescriptor::release()
{
    auto descriptor = mDescriptor;

    mDescriptor = -1;

    return descriptor;
}

void FileDescriptor::reset(int descriptor)
{
    operator=(FileDescriptor(descriptor));
}

void FileDescriptor::swap(FileDescriptor& other)
{
    using std::swap;
    
    swap(mDescriptor, other.mDescriptor);
}

std::size_t FileDescriptor::write(const void* buffer, std::size_t length)
{
    auto* m = reinterpret_cast<const char*>(buffer);

    std::size_t numWritten = 0;

    while (numWritten < length)
    {
        auto result = ::write(mDescriptor,
                              m + numWritten,
                              length - numWritten);

        if (result > 0)
        {
            numWritten += static_cast<std::size_t>(result);
            continue;
        }

        if (errno == EINTR)
            continue;

        throw FUSEErrorF("Unable to write to descriptor: %d: %s",
                         mDescriptor,
                         std::strerror(errno));
    }

    return numWritten;
}

std::size_t FileDescriptor::write(const void* buffer,
                                  std::size_t length,
                                  m_off_t offset)
{
    auto* m = static_cast<const char*>(buffer);

    std::size_t numWritten = 0;

    while (numWritten < length)
    {
        auto result = pwrite(mDescriptor,
                             m + numWritten,
                             length - numWritten,
                             offset);

        if (result > 0)
        {
            numWritten += static_cast<std::size_t>(result);
            offset += result;
            continue;
        }

        if (errno == EINTR)
            continue;

        throw FUSEErrorF("Unable to write to descriptor: %d: %s",
                         mDescriptor,
                         std::strerror(errno));
    }

    return numWritten;
}

} // platform
} // fuse
} // mega

