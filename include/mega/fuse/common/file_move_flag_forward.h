#pragma once

namespace mega
{
namespace fuse
{

enum FileMoveFlag : unsigned int;

using FileMoveFlags = unsigned int;

bool valid(FileMoveFlags flags);

} // fuse
} // mega

