#pragma once

#include <chrono>
#include <cstddef>

#include <mega/fuse/common/inode_cache_flags_forward.h>

namespace mega
{
namespace fuse
{

struct InodeCacheFlags
{
    // Inodes older than this value are candidates for eviction.
    std::chrono::seconds mCleanAgeThreshold = std::chrono::seconds(5 * 60);

    // How often should the cache try to reduce its size?
    std::chrono::seconds mCleanInterval = std::chrono::seconds(5 * 60);

    // Inodes can be evicted when the cache stores more than this value.
    std::size_t mCleanSizeThreshold = 64u;

    // How many inodes is the cache allowed to store?
    std::size_t mMaxSize = 256u;
}; // InodeCacheFlags

} // fuse
} // mega

