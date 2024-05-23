#pragma once

#include <mega/fuse/common/file_open_flag_forward.h>

namespace mega
{
namespace fuse
{

enum FileOpenFlag : unsigned int
{
    FOF_APPEND = 1,
    FOF_TRUNCATE = 2,
    FOF_WRITABLE = 4
}; // FileOpenFlag

} // fuse
} // mega

