#include <mega/common/platform/date_time.h>
#include <mega/common/testing/path.h>
#include <mega/common/testing/utility.h>
#include <sys/stat.h>

#include <utime.h>

namespace mega
{
namespace common
{
namespace testing
{

DateTime lastWriteTime(const Path& path, std::error_code& result)
{
    struct stat attributes;

    if (!stat(path.path().c_str(), &attributes))
        return attributes.st_mtime;

    result = std::error_code(errno, std::system_category());

    return DateTime();
}

void lastWriteTime(const Path path, const DateTime& modified, std::error_code& result)
{
    struct utimbuf times = {modified, modified}; // times

    if (utime(path.path().c_str(), &times))
        result = std::error_code(errno, std::system_category());
}

} // testing
} // common
} // mega
