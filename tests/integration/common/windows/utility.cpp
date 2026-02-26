#include <mega/common/platform/date_time.h>
#include <mega/common/platform/handle.h>
#include <mega/common/platform/windows.h>
#include <mega/common/testing/path.h>
#include <mega/common/testing/utility.h>

namespace mega
{
namespace common
{
namespace testing
{

using platform::Handle;

DateTime lastWriteTime(const Path& path, std::error_code& result)
{
    WIN32_FILE_ATTRIBUTE_DATA attributes;

    if (GetFileAttributesExW(path.path().c_str(), GetFileExInfoStandard, &attributes))
        return attributes.ftLastWriteTime;

    result = std::error_code(GetLastError(), std::system_category());

    return DateTime();
}

void lastWriteTime(const Path path, const DateTime& modified, std::error_code& result)
{
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
} // common
} // mega
