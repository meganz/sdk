#pragma once
#include <string>

namespace mega
{
namespace fuse
{
namespace platform
{
namespace shell
{

// Init for the calling thread once and before use setView. Caller can call this once per thread.
bool init();

// Sets the view mode of running File Explorer windows to "List View" for whose open folder
// paths started with the given prefix. This affects only currently open File Explorer instances.
// @param prefix The folder path prefix to match (case-insensitive).
void setView(const std::wstring& prefix);

// Uninit once. Pair with init.
void uninit();

} // shell
} // platform
} // fuse
} // mega
