#pragma once

namespace mega
{
namespace fuse
{

struct CachedOnlyTag
{};

struct FUSETag
{};

struct MemoryOnlyTag
{};

constexpr CachedOnlyTag CachedOnly;
constexpr MemoryOnlyTag MemoryOnly;

} // fuse
} // mega
