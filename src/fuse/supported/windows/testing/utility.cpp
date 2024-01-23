#include <mega/fuse/platform/windows.h>

#include <mega/fuse/common/testing/path.h>
#include <mega/fuse/common/testing/utility.h>
#include <mega/fuse/platform/date_time.h>
#include <mega/fuse/platform/handle.h>

namespace mega
{
namespace fuse
{
namespace testing
{

DateTime lastWriteTime(const Path& path, std::error_code& result)
{
    WIN32_FILE_ATTRIBUTE_DATA attributes;

    if (GetFileAttributesExW(path.path().c_str(),
                             GetFileExInfoStandard,
                             &attributes))
        return attributes.ftLastWriteTime;

    result = std::error_code(GetLastError(), std::system_category());

    return DateTime();
}

void lastWriteTime(const Path path,
                   const DateTime& modified,
                   std::error_code& result)
{
    using platform::Handle;

    FILETIME modified_ = modified;

    Handle<> handle(CreateFileW(path.path().c_str(),
                                FILE_WRITE_ATTRIBUTES,
                                FILE_SHARE_READ | FILE_SHARE_WRITE,
                                nullptr,
                                OPEN_EXISTING,
                                FILE_FLAG_BACKUP_SEMANTICS,
                                nullptr));

    if (handle && SetFileTime(handle.get(), nullptr, nullptr, &modified_))
        return;

    result = std::error_code(GetLastError(), std::system_category());
}

} // testing
} // fuse
} // mega

