#include <mega/file_service/buffer.h>

namespace mega
{
namespace file_service
{

bool Buffer::isMemoryBuffer() const
{
    return !isFileBuffer();
}

} // file_service
} // mega
