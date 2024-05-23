#include <mega/fuse/common/mount_result.h>
#include <mega/fuse/common/service.h>
#include <mega/fuse/platform/constants.h>
#include <mega/fuse/platform/utility.h>

namespace mega
{
namespace fuse
{

// Convenience.
using platform::FilesystemName;
using platform::FilesystemPredicate;

// Wrap an abort predicate in a filesystem predicate.
static FilesystemPredicate wrap(AbortPredicate predicate);

MountResult Service::abort(AbortPredicate predicate)
{
    // Convenience.
    using platform::filesystems;
    using platform::unmount;

    // What mounts should we abort?
    auto matches = filesystems(wrap(std::move(predicate)));
    auto result  = MOUNT_SUCCESS;

    // (Try to) abort and unmount each match.
    for (const auto& match : matches)
    {
        // Abort and unmount match.
        auto result_ = unmount(match, true);

        // Latch first failure, if any.
        if (result == MOUNT_SUCCESS)
            result = result_;
    }

    // Let caller know if we were successful.
    return result;
}

FilesystemPredicate wrap(AbortPredicate predicate)
{
    static const auto wrapper = [](AbortPredicate& next,
                                   const std::string& path,
                                   const std::string& type) {
        // Type looks like a megafs mount.
        if (type.find(FilesystemName) != type.npos)
            return next(path);

        // Type doesn't look like megafs.
        return false;
    }; // wrapper

    // Wrap user callback.
    return std::bind(wrapper,
                     std::move(predicate),
                     std::placeholders::_1,
                     std::placeholders::_2);
}

} // fuse
} // mega

