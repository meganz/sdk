#pragma once

#include <mega/fuse/common/file_move_flag_forward.h>

namespace mega
{
namespace fuse
{

enum FileMoveFlag : unsigned int
{
    FILE_MOVE_EXCHANGE = 1,
    FILE_MOVE_NO_REPLACE = 2
}; // FileMoveFlag

} // fuse
} // mega

