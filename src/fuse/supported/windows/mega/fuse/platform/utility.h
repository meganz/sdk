#pragma once

#include <string>

#include <mega/fuse/common/inode_info_forward.h>
#include <mega/fuse/common/utility.h>
#include <mega/fuse/platform/library.h>
#include <mega/fuse/platform/mount_forward.h>

#include <mega/types.h>

namespace mega
{
namespace fuse
{
namespace platform
{

DWORD attributes(const InodeInfo& info, const Mount& mount);

std::string fromWideString(const wchar_t* value, std::size_t length);
std::string fromWideString(const std::wstring& value);

std::wstring normalize(const std::wstring& value);

std::wstring toWideString(const std::string& value);

FSP_FSCTL_DIR_INFO* translate(FSP_FSCTL_DIR_INFO& destination,
                              const Mount& mount,
                              const InodeInfo& source);

FSP_FSCTL_DIR_INFO* translate(std::vector<uint8_t>& destination,
                              const Mount& mount,
                              const std::string& name,
                              const InodeInfo& source);

void translate(FSP_FSCTL_FILE_INFO& destination,
               const Mount& mount,
               const InodeInfo& source);

NTSTATUS translate(Error result);

} // platform
} // fuse
} // mega

