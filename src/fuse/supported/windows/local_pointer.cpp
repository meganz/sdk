#include <mega/fuse/platform/windows.h>

#include <mega/fuse/platform/local_pointer.h>

namespace mega
{
namespace fuse
{
namespace platform
{

void LocalDeleter::operator()(void* instance)
{
    LocalFree(instance);
}

} // platform
} // fuse
} // megea

