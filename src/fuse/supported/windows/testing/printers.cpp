#include <iomanip>
#include <string>

#include <mega/fuse/common/mount_inode_id.h>
#include <mega/fuse/platform/date_time.h>
#include <mega/fuse/platform/security_descriptor.h>
#include <mega/fuse/platform/testing/printers.h>
#include <mega/fuse/platform/utility.h>

static UINT64 toUint64(DWORD low, DWORD high);

static const std::string indent = std::string(6, ' ');

void PrintTo(const BY_HANDLE_FILE_INFORMATION& info, std::ostream* ostream)
{
    using namespace mega::fuse;

    auto fill = ostream->fill();
    auto flags = ostream->flags();
    auto index = MountInodeID(toUint64(info.nFileIndexLow, info.nFileIndexHigh));
    auto width = ostream->width();

    *ostream << "\n"
             << indent
             << "accessed: "
             << DateTime(info.ftLastAccessTime)
             << "\n"
             << indent
             << "attributes: "
             << std::hex
             << std::setfill('0')
             << std::setw(8)
             << info.dwFileAttributes
             << "\n"
             << indent
             << "created: "
             << DateTime(info.ftCreationTime)
             << "\n"
             << indent
             << "index: "
             << toString(index)
             << "\n"
             << indent
             << "size: "
             << std::dec
             << std::setfill(fill)
             << std::setw(width)
             << toUint64(info.nFileSizeLow, info.nFileSizeHigh)
             << "\n"
             << indent
             << "written: "
             << DateTime(info.ftLastWriteTime);

    ostream->flags(flags);
}

void PrintTo(const FILETIME& value, std::ostream* ostream)
{
    using namespace mega::fuse;

    *ostream << DateTime(value);
}

void PrintTo(const WIN32_FILE_ATTRIBUTE_DATA& info, std::ostream* ostream)
{
    using namespace mega::fuse;

    auto fill = ostream->fill();
    auto flags = ostream->flags();
    auto width = ostream->width();

    *ostream << "\n"
             << indent
             << "accessed: "
             << info.ftLastAccessTime
             << "\n"
             << indent
             << "attributes: "
             << std::hex
             << std::setfill('0')
             << std::setw(8)
             << info.dwFileAttributes
             << "\n"
             << indent
             << "created: "
             << DateTime(info.ftCreationTime)
             << "\n"
             << indent
             << "size: "
             << std::dec
             << std::setfill(fill)
             << std::setw(width)
             << toUint64(info.nFileSizeLow, info.nFileSizeHigh)
             << "\n"
             << indent
             << "written: "
             << DateTime(info.ftLastWriteTime);

    ostream->flags(flags);
}

UINT64 toUint64(DWORD low, DWORD high)
{
    auto low_ = static_cast<UINT64>(low);
    auto high_ = static_cast<UINT64>(high);

    return (high_ << 32) | low_;
}

namespace mega
{
namespace fuse
{
namespace platform
{

void PrintTo(const SecurityDescriptor& descriptor, std::ostream* ostream)
{
    *ostream << "\n"
             << indent
             << toString(descriptor);
}

} // platform

namespace testing
{

void PrintTo(const FileTimes& value, std::ostream* ostream)
{
    *ostream << "\n"
             << indent
             << "accessed: "
             << DateTime(value.mAccessed)
             << "\n"
             << indent
             << "created: "
             << DateTime(value.mCreated)
             << "\n"
             << indent
             << "written: "
             << DateTime(value.mWritten);
}

} // testing
} // fuse
} // mega

