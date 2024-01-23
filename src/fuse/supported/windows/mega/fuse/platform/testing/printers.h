#pragma once

#include <mega/fuse/platform/windows.h>

#include <mega/fuse/common/testing/printers.h>
#include <mega/fuse/platform/security_descriptor_forward.h>
#include <mega/fuse/platform/testing/wrappers.h>

void PrintTo(const BY_HANDLE_FILE_INFORMATION& info, std::ostream* ostream);

void PrintTo(const FILETIME& value, std::ostream* ostream);

void PrintTo(const WIN32_FILE_ATTRIBUTE_DATA& info, std::ostream* ostream);

namespace mega
{
namespace fuse
{
namespace platform
{

void PrintTo(const SecurityDescriptor& descriptor, std::ostream* ostream);

}  // platform

namespace testing
{

void PrintTo(const FileTimes& value, std::ostream* ostream);

} // testing
} // fuse
} // mega

