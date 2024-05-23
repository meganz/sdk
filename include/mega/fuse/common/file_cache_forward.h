#pragma once

#include <mutex>

namespace mega
{
namespace fuse
{

class FileCache;

using FileCacheLock = std::unique_lock<const FileCache>;

} // fuse
} // mega

