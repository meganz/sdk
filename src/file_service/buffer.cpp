#include <mega/file_service/buffer.h>
#include <mega/file_service/displaced_buffer.h>
#include <mega/file_service/file_buffer.h>
#include <mega/file_service/memory_buffer.h>

namespace mega
{
namespace file_service
{

BufferPtr Buffer::create(FileAccess& file, std::uint64_t offset, std::uint64_t length)
{
    // How large can a buffer be before we write directly to file?
    constexpr std::uint64_t maximum = 1u << 24;

    // Buffer's small enough that we can hold it in memory.
    if (length <= maximum)
        return std::make_shared<MemoryBuffer>(length);

    // Buffer's too large to fit into memory so write to file.
    auto buffer = std::make_shared<FileBuffer>(file);

    // Buffer doesn't need to be displaced.
    if (!offset)
        return buffer;

    // Return a displaced buffer to our caller.
    return std::make_shared<DisplacedBuffer>(std::move(buffer), offset);
}

} // file_service
} // mega
