#include <fcntl.h>
#include <mntent.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>

#include <cerrno>

#include <mega/common/utility.h>
#include <mega/fuse/common/logging.h>
#include <mega/fuse/common/mount_result.h>
#include <mega/fuse/platform/file_descriptor.h>
#include <mega/fuse/platform/process.h>
#include <mega/fuse/platform/utility.h>

namespace mega
{
namespace fuse
{
namespace platform
{

using namespace common;

bool abort(const std::string& path)
{
    // Clarity.
    constexpr auto flags = AT_STATX_DONT_SYNC;
    constexpr auto mask  = STATX_BASIC_STATS;

    // Try and retrieve information about path.
    struct statx attributes;

    // Couldn't retrieve information about path.
    if (statx(0, path.c_str(), flags, mask, &attributes))
    {
        FUSEErrorF("Couldn't retrieve information about %s: %s",
                   path.c_str(),
                   std::strerror(errno));

        return false;
    }

    // Compute absolute device number.
    auto device = makedev(attributes.stx_dev_major,
                          attributes.stx_dev_minor);

    // Compute abort file path.
    auto abortPath = format("/sys/fs/fuse/connections/%lu/abort",
                            device);

    // Try and open abort file for writing.
    FileDescriptor abortFile(open(abortPath.c_str(),
                                  O_CLOEXEC | O_SYNC | O_WRONLY));

    // Couldn't open abort file.
    if (!abortFile)
    {
        FUSEErrorF("Couldn't open abort file for writing: %s: %s",
                     abortPath.c_str(),
                     std::strerror(errno));

        return false;
    }

    // Try and write the abort file.
    try
    {
        static const char one[] = "1\n";

        // Might throw.
        abortFile.write(one, sizeof(one));
    }
    catch (std::runtime_error& exception)
    {
        FUSEErrorF("Couldn't write abort file: %s: %s",
                   abortPath.c_str(),
                   std::strerror(errno));

        return false;
    }

    return true;
}

PathVector filesystems(FilesystemPredicate predicate)
{
    // Convenience.
    using FilePtr = std::unique_ptr<FILE, int (*)(FILE*)>;

    // Where should we search for a suitable mtab?
    static const std::vector<std::string> paths = {
        "/proc/mounts",
        "/etc/mtab"
    }; // paths

    FilePtr mounts(nullptr, std::fclose);

    // Try and locate a suitable mtab.
    for (auto& path : paths)
    {
        // Try and open this mtab.
        mounts.reset(setmntent(path.c_str(), "r"));

        // Found a suitable mtab.
        if (!!mounts)
            break;

        // Leave a trail of what we've done.
        FUSEDebugF("Unable to open mtab: %s: %s",
                   path.c_str(),
                   std::strerror(errno));
    }

    // Couldn't find a suitable mtab.
    if (!mounts)
    {
        FUSEWarning1("Unable to locate a suitable mtab");

        return PathVector();
    }

    PathVector matches;

    // Iterate over the mtab's entries.
    for (auto* entry = getmntent(mounts.get());
         entry;
         errno = 0, entry = getmntent(mounts.get()))
    {
        // Latch mount's path and type.
        std::string path = entry->mnt_dir;
        std::string type = entry->mnt_type;

        // Collect path if our predicate is satisfied.
        if (!predicate || predicate(path, type))
            matches.emplace_back(std::move(path));
    }

    // Couldn't iterate over all of the mtab's entries.
    if (errno)
    {
        FUSEWarningF("Unable to iterate over mtab entries: %s",
                     std::strerror(errno));

        return PathVector();
    }

    // Pass matches to our caller.
    return matches;
}

MountResult unmount(const std::string& path, bool abort)
try
{
    // Try and abort the mount.
    if (abort && !platform::abort(path))
        FUSEWarningF("Unable to abort mount: %s",
                     path.c_str());

    // Provided by libfuse.
    static const std::string command = "/usr/bin/fusermount";

    // Populate arguments.
    std::vector<std::string> arguments;

    // Unmount.
    arguments.emplace_back("-u");

    // This mount.
    arguments.emplace_back(path);

    // Execute fusermount.
    auto process = run(command, arguments);

    // Read fusermount's output.
    auto output = process.output().readAll();

    // Wait for fusermount to terminate.
    auto result = process.wait();

    // Mount was successfully unmounted.
    if (!result)
        return MOUNT_SUCCESS;

    // Mount was busy.
    if (output.find("busy") != std::string::npos)
        return MOUNT_BUSY;

    // Couldn't unmount for some unknown reason.
    return MOUNT_UNEXPECTED;
}
catch (std::runtime_error& exception)
{
    FUSEErrorF("Unable to unmount %s: %s",
               path.c_str(),
               exception.what());

    return MOUNT_UNEXPECTED;
}

} // platform
} // fuse
} // mega

