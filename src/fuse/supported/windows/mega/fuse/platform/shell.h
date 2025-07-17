#pragma once
#include <string>
#include <vector>
namespace mega
{
namespace fuse
{
namespace platform
{
namespace shell
{

using Prefixes = std::vector<std::wstring>;

// Init for the calling thread once and before use setView. Caller can call this once per thread.
bool init();

// Sets the view mode of running File Explorer windows to "List View" for whose open folder
// paths started with one of given prefix in the set. This affects only currently open File Explorer
// instances.
// @param prefixes The folder path prefixes set to match
void setView(const Prefixes& prefixes);

// Uninit once. Pair with init.
void uninit();

} // shell
} // platform
} // fuse
} // mega
