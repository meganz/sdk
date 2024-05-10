#include <iomanip>
#include <sstream>
#include <string>

#include <mega/fuse/common/mount_inode_id.h>
#include <mega/fuse/platform/date_time.h>
#include <mega/fuse/platform/testing/printers.h>

struct Mode
{
    Mode(mode_t mode)
      : mMode(mode)
    {
    }

    mode_t mMode;
}; // Mode

std::ostream& operator<<(std::ostream& ostream, const Mode& mode);

static const auto indent = std::string(6, ' ');

void PrintTo(const struct dirent& entry, std::ostream* ostream)
{
    using namespace mega::fuse;

    *ostream << "\n"
             << indent
             << "d_ino: "
             << toString(MountInodeID(entry.d_ino))
             << "\n"
             << indent
             << "d_name: "
             << entry.d_name;
}

void PrintTo(const struct stat& stat, std::ostream* ostream)
{
    using namespace mega::fuse;

    *ostream << "\n"
             << indent
             << "st_ino: "
             << toString(MountInodeID(stat.st_ino))
             << "\n"
             << indent
             << "st_mode: "
             << Mode(stat.st_mode)
             << "\n"
             << indent
             << "st_nlink: "
             << stat.st_nlink
             << "\n"
             << indent
             << "st_uid: "
             << stat.st_uid
             << "\n"
             << indent
             << "st_gid: "
             << stat.st_gid
             << "\n"
             << indent
             << "st_size: "
             << stat.st_size
             << "\n"
             << indent
             << "st_blksize: "
             << stat.st_blksize
             << "\n"
             << indent
             << "st_blocks: "
             << stat.st_blocks
             << "\n"
             << indent
             << "st_atime: "
             << DateTime(stat.st_atime)
             << "\n"
             << indent
             << "st_mtime: "
             << DateTime(stat.st_mtime)
             << "\n"
             << indent
             << "st_ctime: "
             << DateTime(stat.st_ctime);
}

std::ostream& operator<<(std::ostream& ostream, const Mode& mode)
{
    std::ostringstream osstream;

    osstream << std::oct
             << mode.mMode;

    return ostream << osstream.str();
}

