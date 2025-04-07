#include <mega/common/error_or.h>
#include <mega/common/node_info.h>
#include <mega/common/utility.h>
#include <mega/fuse/common/client.h>
#include <mega/fuse/common/database_builder.h>
#include <mega/fuse/common/inode.h>
#include <mega/fuse/common/inode_info.h>
#include <mega/fuse/common/mount_info.h>
#include <mega/fuse/common/mount_result.h>
#include <mega/fuse/common/ref.h>
#include <mega/fuse/common/service.h>
#include <mega/fuse/platform/service_context.h>

#include <algorithm>
#include <cassert>

namespace mega
{
namespace fuse
{
namespace platform
{

using namespace common;

static Database dbInit(const Client& client);

static MountResult dbOperation(void (DatabaseBuilder::*op)(std::size_t),
                               const LocalPath& path,
                               const std::size_t target);

static LocalPath dbPath(const Client& client);

ServiceContext::ServiceContext(const ServiceFlags& flags, Service& service)
  : fuse::ServiceContext(service)
  , mDatabase(dbInit(service.mClient))
  , mExecutor(flags.mServiceExecutorFlags, logger())
  , mFileExtensionDB()
  , mInodeDB(*this)
  , mFileCache(*this)
  , mInodeCache(flags.mInodeCacheFlags)
  , mUnmounter(*this)
  , mMountDB(*this)
{
    // Inject InodeDB as an event observer.
    service.mClient.eventObserver(&mInodeDB);

    // Prune lingering transient mounts from the database.
    mMountDB.prune();
}

ServiceContext::~ServiceContext()
{
    // Detach InodeDB as event observer.
    client().eventObserver(nullptr);

    // Tear down any enabled mounts.
    mMountDB.deinitialize();

    // Cancel any pending uploads.
    mFileCache.cancel();

    // Wait for all inodes to be purged from memory.
    mInodeDB.clear();
}

MountResult ServiceContext::add(const MountInfo& info)
{
    return mMountDB.add(info);
}

bool ServiceContext::cached(NormalizedPath path) const
{
    LocalPath relativePath;

    // What mount contains this path?
    auto mount = mMountDB.contains(path, false, &relativePath);

    // No mount contains this path.
    if (!mount)
        return false;

    // Try to locate the inode associated with this path.
    auto result = mInodeDB.lookup(relativePath, mount->mHandle);

    // Couldn't find the inode.
    if (result.second != API_OK)
        return false;

    // Let the caller know whether the inode is cached or not.
    return result.first->cached();
}

void ServiceContext::current()
{
    mMountDB.current();
}

ErrorOr<InodeInfo> ServiceContext::describe(const NormalizedPath& path) const
{
    LocalPath relativePath;

    // What mount contains this path?
    auto mount = mMountDB.contains(path, true, &relativePath);

    // No enabled mount contains this path.
    if (!mount)
        return unexpected(API_ENOENT);

    // Try and locate the inode associated with this path.
    auto result = mInodeDB.lookup(relativePath, mount->mHandle);

    // We couldn't find the inode.
    if (result.second != API_OK)
        return unexpected(result.second);

    // Retrieve the inode's description.
    auto info = result.first->info();

    // Adjust permissions as necessary.
    if (mount->mFlags.mReadOnly)
        info.mPermissions = RDONLY;

    // Return description to caller.
    return info;
}

void ServiceContext::disable(MountDisabledCallback callback,
                             const std::string& name,
                             bool remember)
{
    mMountDB.disable(std::move(callback),
                     name,
                     remember);
}

MountResult ServiceContext::discard(bool discard)
{
    // Let the inode DB know whether it should process node events.
    mInodeDB.discard(discard);

    // Let the caller know the discard was completed.
    return MOUNT_SUCCESS;
}

MountResult ServiceContext::downgrade(const LocalPath& path,
                                      std::size_t target)
{
    return dbOperation(&DatabaseBuilder::downgrade,
                       path,
                       target);
}

MountResult ServiceContext::enable(const std::string& name, bool remember)
{
    return mMountDB.enable(name, remember);
}

bool ServiceContext::enabled(const std::string& name) const
{
    return mMountDB.enabled(name);
}

Task ServiceContext::execute(std::function<void(const Task&)> function)
{
    return mExecutor.execute(std::move(function), true);
}

MountResult ServiceContext::flags(const std::string& name,
                                  const MountFlags& flags)
{
    return mMountDB.flags(name, flags);
}

MountFlagsPtr ServiceContext::flags(const std::string& name) const
{
    return mMountDB.flags(name);
}

FileSystemAccess& ServiceContext::fsAccess() const
{
    return client().fsAccess();
}

MountInfoPtr ServiceContext::get(const std::string& name) const
{
    return mMountDB.get(name);
}

MountInfoVector ServiceContext::get(bool onlyEnabled) const
{
    return mMountDB.get(onlyEnabled);
}

NormalizedPath ServiceContext::path(const std::string& name) const
{
    return mMountDB.path(name);
}

MountResult ServiceContext::remove(const std::string& name)
{
    return mMountDB.remove(name);
}

void ServiceContext::serviceFlags(const ServiceFlags& flags)
{
    // Update the inode cache's flags.
    mInodeCache.flags(flags.mInodeCacheFlags);

    // Update executor flags for existing mounts.
    mMountDB.executorFlags(flags.mMountExecutorFlags);

    // Update file explorer view flags
    mMountDB.fileExplorerView(flags.mFileExplorerView);
}

bool ServiceContext::syncable(const NormalizedPath& path) const
{
    return mMountDB.syncable(path);
}

void ServiceContext::updated(NodeEventQueue& events)
{
    mInodeDB.updated(events);
}

MountResult ServiceContext::upgrade(const LocalPath& path,
                                    std::size_t target)
{
    return dbOperation(&DatabaseBuilder::upgrade,
                       path,
                       target);
}

Database dbInit(const Client& client)
{
    Database database(logger(), dbPath(client));

    DatabaseBuilder(database).build();

    return database;
}

MountResult dbOperation(void (DatabaseBuilder::*op)(std::size_t),
                        const LocalPath& path,
                        const std::size_t target)
try
{
    assert(op);

    Database database(logger(), path);
    DatabaseBuilder builder(database);

    (builder.*op)(target);

    return MOUNT_SUCCESS;
}
catch (...)
{
    return MOUNT_UNEXPECTED;
}

LocalPath dbPath(const Client& client)
{
    return client.dbPath(format("fuse00_%s", client.sessionID().c_str()));
}

} // platform
} // fuse
} // mega

