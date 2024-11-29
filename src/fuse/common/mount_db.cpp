#include <atomic>
#include <cassert>
#include <stdexcept>

#include <mega/fuse/common/any_lock.h>
#include <mega/fuse/common/any_lock_set.h>
#include <mega/fuse/common/client.h>
#include <mega/fuse/common/error_or.h>
#include <mega/fuse/common/inode.h>
#include <mega/fuse/common/inode_id.h>
#include <mega/fuse/common/logging.h>
#include <mega/fuse/common/mount_db.h>
#include <mega/fuse/common/mount_event.h>
#include <mega/fuse/common/mount_event_type.h>
#include <mega/fuse/common/mount_info.h>
#include <mega/fuse/common/mount_result.h>
#include <mega/fuse/common/node_info.h>
#include <mega/fuse/common/ref.h>
#include <mega/fuse/common/scoped_query.h>
#include <mega/fuse/common/transaction.h>
#include <mega/fuse/platform/mount.h>
#include <mega/fuse/platform/mount_db.h>
#include <mega/fuse/platform/service_context.h>

#include <mega/filesystem.h>

namespace mega
{
namespace fuse
{

MountDB::Queries::Queries(Database& mDatabase)
  : mAddMount(mDatabase.query())
  , mGetMountByPath(mDatabase.query())
  , mGetMountFlagsByPath(mDatabase.query())
  , mGetMountInodeByPath(mDatabase.query())
  , mGetMountPathsByName(mDatabase.query())
  , mGetMountStartupStateByPath(mDatabase.query())
  , mGetMounts(mDatabase.query())
  , mGetMountsEnabledAtStartup(mDatabase.query())
  , mRemoveMountByPath(mDatabase.query())
  , mRemoveTransientMounts(mDatabase.query())
  , mSetMountFlagsByPath(mDatabase.query())
  , mSetMountStartupStateByPath(mDatabase.query())
{
    mAddMount = "insert into mounts values ( "
                "  :enable_at_startup, "
                "  :id, "
                "  :name, "
                "  :path, "
                "  :persistent, "
                "  :read_only "
                ")";

    mGetMountByPath = "select * from mounts where path = :path";

    mGetMountFlagsByPath = "select enable_at_startup "
                           "     , name "
                           "     , persistent "
                           "     , read_only "
                           "  from mounts "
                           " where path = :path";

    mGetMountInodeByPath = "select id from mounts where path = :path";

    mGetMountPathsByName = "select path from mounts where name = :name";

    mGetMountStartupStateByPath = "select enable_at_startup "
                                  "     , persistent "
                                  "  from mounts "
                                  " where path = :path";

    mGetMounts = "select * from mounts";

    mGetMountsEnabledAtStartup = "select path "
                                 "  from mounts "
                                 " where enable_at_startup = true "
                                 "   and persistent = true";

    mRemoveMountByPath = "delete from mounts where path = :path";

    mRemoveTransientMounts = "delete from mounts where persistent = false";

    mSetMountFlagsByPath = "update mounts "
                           "   set enable_at_startup = :enable_at_startup "
                           "     , name = :name "
                           "     , persistent = :persistent "
                           "     , read_only = :read_only "
                           " where path = :path";

    mSetMountStartupStateByPath =
      "update mounts "
      "   set enable_at_startup = :enable_at_startup "
      "     , persistent = :persistent "
      " where path = :path";
}

MountResult MountDB::check(const MountInfo& info)
{
    // Convenience.
    auto& handle = info.mHandle;
    auto& name = info.mFlags.mName;

    // User's specified a bogus node handle.
    if (handle.isUndef())
    {
        FUSEErrorF("Invalid cloud handle specified",
                   toNodeHandle(handle).c_str());

        return MOUNT_REMOTE_UNKNOWN;
    }

    // User's specified a bogus name.
    if (name.empty())
    {
        FUSEError1("No name specified");

        return MOUNT_NO_NAME;
    }

    // Check that the specified cloud node exists.
    auto info_ = client().get(handle);

    // Cloud node doesn't exist.
    if (!info_)
    {
        FUSEErrorF("Cloud node doesn't exist: %s",
                   toNodeHandle(handle).c_str());

        return MOUNT_REMOTE_UNKNOWN;
    }

    // Cloud node isn't a directory.
    if (!info_->mIsDirectory)
    {
        FUSEErrorF("Cloud node is not a directory: %s",
                   toNodeHandle(handle).c_str());

        return MOUNT_REMOTE_FILE;
    }

    // Make sure the target path isn't claimed by a sync.
    if (!client().mountable(info.mPath))
    {
        FUSEErrorF("Local path is being synchronized: %s",
                   info.mPath.toPath(false).c_str());

        return MOUNT_LOCAL_SYNCING;
    }

    // Check local path.
    return check(client(), info);
}

void MountDB::doDeinitialize()
{
}

void MountDB::enable()
try
{
    mContext.mInodeDB.current();
    mContext.mFileCache.current();

    // What mounts should we try and enable?
    std::vector<LocalPath> mounts;

    // Compute list of mounts to enable.
    {
        auto guard = lockAll(mContext.mDatabase, *this);

        auto transaction = mContext.mDatabase.transaction();
        auto query = transaction.query(mQueries.mGetMountsEnabledAtStartup);

        // What mounts should be enabled at startup?
        query.execute();

        // Collect paths of enabled mounts.
        for ( ; query; ++query)
            mounts.emplace_back(query.field("path"));
    }

    // Try and enable each mount.
    for (auto& path : mounts)
    {
        MountEvent event;

        event.mPath = path;
        event.mType = MOUNT_ENABLED;

        // Try and enable the mount.
        event.mResult = enable(path, false);

        // Emit event.
        client().emitEvent(event);

        // Couldn't enable the mount.
        if (event.mResult != MOUNT_SUCCESS)
        {
            FUSEWarningF("Unable to enable persistent mount \"%s\" due to error: %s",
                         path.toPath(false).c_str(),
                         toString(event.mResult));

            // Try and enable the next mount.
            continue;
        }

        // Mount's been enabled.
        FUSEInfoF("Successfully enabled persistent mount \"%s\"",
                  path.toPath(false).c_str());
    }
}
catch (std::runtime_error& exception)
{
    FUSEErrorF("Unable to enable persistent mounts: %s",
               exception.what());
}

void MountDB::invalidate()
{
    // Tracks which inodes have been invalidated.
    InodeRefSet invalidated;

    // Tracks which nodes have been mounted.
    NodeHandleSet mounted;

    // Iterate over each mount, invalidating any pinned inodes.
    each([&](platform::Mount& mount) {
        // Invalidate any inodes pinned by this mount.
        mount.invalidatePins(invalidated);

        // Remember that this node was mounted.
        mounted.emplace(mount.handle());
    });

    // Mark inodes whose node has been removed.
    for (auto ref : invalidated)
    {
        auto handle = ref->handle();

        // Inode was never present in the cloud.
        if (handle.isUndef())
            continue;

        // Mark inode as removed if necessary.
        ref->removed(!client().exists(handle));
    }

    // Try and disable each mount associated with a removed node.
    for (auto handle : mounted)
    {
        if (!client().exists(handle))
            disable(handle);
    }
}

platform::MountPtr MountDB::mount(const std::string& name) const
{
    // Locate mount by name.
    auto i = mByName.find(name);

    if (i != mByName.end())
        return i->second;

    return nullptr;
}

platform::MountPtr MountDB::mount(const LocalPath& path) const
{
    // Locate mount by path.
    auto i = mByPath.find(path);

    if (i != mByPath.end())
        return i->second;

    return nullptr;
}

platform::MountPtr MountDB::remove(platform::Mount& mount)
{
    MountDBLock guard(*this);

    // Is the mount in the index?
    auto p = mByPath.find(mount.path());

    // Mount's not in the index.
    if (p == mByPath.end())
        return nullptr;

    // Latch a reference to the mount.
    auto ptr = std::move(p->second);

    // Sanity.
    assert(ptr.get() == &mount);

    // Remove the mount from the handle index.
    auto h = mByHandle.find(mount.handle());

    // Sanity.
    auto count = h->second.erase(ptr);

    assert(count);

    // No other mounts are associated with this handle.
    if (h->second.empty())
        mByHandle.erase(h);

    // Remove the mount from the path index.
    mByPath.erase(p);

    // Remove the mount from the name index.
    count = mByName.erase(mount.name());

    // Sanity.
    assert(count);

    static_cast<void>(count);

    // Return mount to caller.
    return ptr;
}

MountDB::MountDB(platform::ServiceContext& context)
  : Lockable()
  , mByHandle()
  , mByName()
  , mByPath()
  , mOnCurrent(&MountDB::enable)
  , mQueries(context.mDatabase)
  , mActivities()
  , mContext(context)
{
}

MountDB::~MountDB()
{
    assert(!mActivities.active());
    assert(mByHandle.empty());
    assert(mByName.empty());
    assert(mByPath.empty());

    FUSEDebug1("Mount DB destroyed");
}

void MountDB::disable()
{
    MountDBLock lock(*this);

    // Latch mounts.
    auto byHandle = std::move(mByHandle);
    auto byName = std::move(mByName);
    auto byPath = std::move(mByPath);

    mByHandle.clear();
    mByName.clear();
    mByPath.clear();

    // Release lock.
    lock.unlock();

    // Release mounts.
    byHandle.clear();
    byName.clear();
    byPath.clear();
}

MountResult MountDB::add(const MountInfo& info)
try
{
    // Check that the mount's description is sane.
    //
    // We're not holding the lock as check(...) calls the client.
    auto result = check(info);

    // Description isn't sane.
    if (result != MOUNT_SUCCESS)
        return result;

    auto guard = lockAll(mContext.mDatabase, *this);

    auto transaction = mContext.mDatabase.transaction();
    auto query = transaction.query(mQueries.mGetMountInodeByPath);

    // Make sure the path isn't already associated with a mount.
    query.param(":path") = info.mPath;
    query.execute();

    // A mount's already associated with this path.
    if (query)
        return MOUNT_EXISTS;

    // Add the description to the database.
    query = transaction.query(mQueries.mAddMount);

    info.serialize(query);

    query.execute();

    transaction.commit();

    // Mount's been added to the database.
    return MOUNT_SUCCESS;
}
catch (std::runtime_error& exception)
{
    // Unexpected error while adding the mount.
    FUSEErrorF("Unable to add mount: %s",
               exception.what());

    return MOUNT_UNEXPECTED;
}

Client& MountDB::client() const
{
    return mContext.client();
}

void MountDB::current()
{
    // Acquire lock.
    MountDBLock guard(*this);

    // Calls our current handler.
    auto current = [this](Activity& activity,
                          void (MountDB::*onCurrent)(),
                          const Task& task) {
        // Task hasn't been cancelled.
        if (!task.cancelled())
            (this->*onCurrent)();
    }; // current

    // Queue current handler for execution.
    mContext.mExecutor.execute(
      std::bind(std::move(current),
                mActivities.begin(),
                mOnCurrent,
                std::placeholders::_1),
      true);

    // Subsequent events cause us to invalidate all mounts.
    mOnCurrent = &MountDB::invalidate;
}

void MountDB::deinitialize()
{
    FUSEDebug1("Deinitializating Mount DB");

    // Tear down any enabled mounts.
    disable();

    // Wait for any callbacks to complete.
    mActivities.waitUntilIdle();

    // Perform platform-specific deinitialization.
    doDeinitialize();

    FUSEDebug1("Mount DB deinitialized");
}

MountInfoPtr MountDB::contains(const LocalPath& path,
                               bool enabled,
                               LocalPath* relativePath) const
{
    // Records where the mount's path ends.
    std::size_t index = 0;

    // Keeps track of the most specific match, if any.
    const MountInfo* mount = nullptr;

    // Retrieve a list of all (enabled) mounts.
    auto mounts = get(enabled);

    // Search for a matching mount.
    for (auto& m : mounts)
    {
        // Convenience.
        auto& path_ = m.mPath;

        // Path isn't within this mount.
        if (!path_.isContainingPathOf(path, &index))
            continue;

        // This mount is a better match.
        if (!mount || path_.isContainingPathOf(mount->mPath, &index))
            mount = &m;
    }

    // No mount contains this path.
    if (!mount)
        return nullptr;

    // Latch relative path, if requested.
    if (relativePath)
        *relativePath = path.subpathFrom(index);

    // Return description to caller.
    return std::make_unique<MountInfo>(std::move(*mount));
}

void MountDB::disable(MountDisabledCallback callback,
                      const LocalPath& path,
                      bool remember)
{
    auto guard = lockAll(mContext.mDatabase, *this);

    MountEvent event;

    event.mPath = path;
    event.mType = MOUNT_DISABLED;

    // Emits the event and queues callback for execution.
    auto emitEvent = [&](MountResult result) {
        // Latch result.
        event.mResult = result;

        // Emit the event.
        client().emitEvent(event);

        // Forward result to callback.
        auto wrapper = [result](MountDisabledCallback& callback,
                                const Task&) {
            callback(result);
        }; // wrapper

        // Queue callback for execution.
        client().execute(
          std::bind(std::move(wrapper),
                    std::move(callback),
                    std::placeholders::_1));
    }; // emitEvent

    try
    {
        auto transaction = mContext.mDatabase.transaction();
        auto query = transaction.query(mQueries.mGetMountStartupStateByPath);

        // Query the mount's startup state.
        query.param(":path") = path;
        query.execute();

        // No mount associated with specified path.
        if (!query)
        {
            FUSEErrorF("No mount associated with path: %s",
                       path.toPath(false).c_str());

            return emitEvent(MOUNT_UNKNOWN);
        }

        // Latch startup state.
        bool enableAtStartup = query.field("enable_at_startup");
        bool persistent = query.field("persistent");

        enableAtStartup = enableAtStartup && !remember;
        persistent = persistent || remember;

        // Update the mount's startup state.
        query = transaction.query(mQueries.mSetMountStartupStateByPath);

        query.param(":enable_at_startup") = enableAtStartup;
        query.param(":path") = path;
        query.param(":persistent") = persistent;

        query.execute();

        transaction.commit();

        // Is the mount currently enabled?
        auto mount = this->mount(path);

        // Mount's not enabled: We're done.
        if (!mount)
            return emitEvent(MOUNT_SUCCESS);

        auto flags = mount->flags();

        // Keep mount consistent with the database.
        flags.mEnableAtStartup = enableAtStartup;
        flags.mPersistent = persistent;

        // Update the mount's flags.
        mount->flags(flags);

        // Unmount the mount.
        mContext.mUnmounter.unmount(std::move(callback),
                                    std::move(mount));
    }
    catch (std::runtime_error& exception)
    {
        // Unexpected error while disabling mount.
        FUSEErrorF("Unable to disable mount %s: %s",
                   path.toPath(false).c_str(),
                   exception.what());

        emitEvent(MOUNT_UNEXPECTED);
    }
}

void MountDB::disable(NodeHandle handle)
{
    // Acquire lock.
    MountDBLock guard(*this);

    // Are any mounts associated with this handle?
    auto h = mByHandle.find(handle);

    // No mounts are associated with this handle.
    if (h == mByHandle.end())
        return;

    // Sanity.
    assert(!h->second.empty());

    // Called when a mount has been disabled.
    auto disabled = [](const LocalPath& path, MountResult result) {
        // Couldn't disable the mount.
        if (result != MOUNT_SUCCESS)
        {
            FUSEWarningF("Unable to disable mount \"%s\": %s",
                         path.toPath(false).c_str(),
                         toString(result));
            return;
        }

        // Mount's been disabled.
        FUSEInfoF("Successfully disabled mount \"%s\"",
                  path.toPath(false).c_str());
    }; // disabled

    FUSEInfoF("Attempting to disable mounts associated with %s",
              toNodeHandle(handle).c_str());

    // Disable mounts associated with handle.
    for (auto mount : h->second)
    {
        // Try and disable the mount.
        mContext.mUnmounter.unmount(std::bind(disabled,
                                              mount->path(),
                                              std::placeholders::_1),
                                    mount);
    }
}

void MountDB::each(std::function<void(platform::Mount&)> function)
{
    // Sanity.
    assert(function);

    // Acquire vector of active mounts.
    auto mounts = ([this]() {
        // Acquire lock.
        MountDBLock guard(*this);

        // Instantiate vector.
        platform::MountPtrVector mounts;

        // Reserve necessary space.
        mounts.reserve(mByPath.size());

        // Populate vector with active mounts.
        for (auto& m : mByPath)
            mounts.emplace_back(m.second);

        // Return vector to caller.
        return mounts;
    })();

    // Execute the function on each mount.
    for (auto& mount : mounts)
        function(*mount);
}

MountResult MountDB::enable(const LocalPath& path, bool remember)
try
{
    auto lock = lockAll(mContext.mDatabase, *this);

    // The mount associated with this path is already enabled.
    if (mount(path))
        return MOUNT_SUCCESS;

    auto transaction = mContext.mDatabase.transaction();
    auto query = transaction.query(mQueries.mGetMountByPath);

    // Check if the mount's present in the database.
    query.param(":path") = path;
    query.execute();

    // Mount's not in the database.
    if (!query)
    {
        FUSEErrorF("No mount associated with path: %s",
                   path.toPath(false).c_str());

        return MOUNT_UNKNOWN;
    }

    // Deserialize the mount's description.
    auto info = MountInfo::deserialize(query);

    auto& flags = info.mFlags;

    // Compute the mount's new startup state.
    flags.mEnableAtStartup |= remember;
    flags.mPersistent |= remember;

    // Update the mount's startup state.
    query = transaction.query(mQueries.mSetMountStartupStateByPath);

    query.param(":enable_at_startup") = flags.mEnableAtStartup;
    query.param(":path") = path;
    query.param(":persistent") = flags.mPersistent;

    query.execute();

    transaction.commit();

    // Make sure the transaction's truly dead.
    query.reset();

    // Release the lock so we can call the client.
    lock.unlock();

    // Check that the mount's description is still sane.
    auto result = check(info);

    // Description's no longer sane.
    if (result != MOUNT_SUCCESS)
        return result;

    // Reacquire the lock.
    lock.lock();

    // Another thread's enabled the mount.
    if (auto mount = this->mount(info.mPath))
        return MOUNT_SUCCESS;

    // Another thread's enabled a mount with the same name.
    if (auto mount = this->mount(info.mFlags.mName))
    {
        FUSEErrorF("Name %s already taken by mount: %s",
                   info.mFlags.mName.c_str(),
                   mount->path().toPath(false).c_str());

        return MOUNT_NAME_TAKEN;
    }

    // Convenience.
    auto& self = static_cast<platform::MountDB&>(*this);

    // Instantiate the mount.
    auto mount = std::make_shared<platform::Mount>(info, self);

    // Add the mount to the index.
    mByHandle[info.mHandle].emplace(mount);
    mByName[flags.mName] = mount;
    mByPath[path] = mount;

    // Release lock.
    lock.unlock();

    // Let the mount know it's been enabled.
    mount->enabled();

    // Mount's enabled.
    return MOUNT_SUCCESS;
}
catch (std::runtime_error& exception)
{
    // Unexpected error while enabling mount.
    FUSEErrorF("Unable to enable mount %s: %s",
               path.toPath(false).c_str(),
               exception.what());

    return MOUNT_UNEXPECTED;
}

bool MountDB::enabled(const LocalPath& path) const
{
    // Acquire lock.
    MountDBLock guard(*this);

    // Is a mount with this path enabled?
    return mByPath.count(path) > 0;
}

void MountDB::executorFlags(const TaskExecutorFlags& flags)
{
    each([&](platform::Mount& mount) {
        mount.executorFlags(flags);
    });
}

TaskExecutorFlags MountDB::executorFlags() const
{
    return mContext.serviceFlags().mMountExecutorFlags;
}

MountResult MountDB::flags(const LocalPath& path,
                           const MountFlags& flags)
try
{
    auto guard = lockAll(mContext.mDatabase, *this);

    // User's specified an invalid name.
    if (flags.mName.empty())
    {
        FUSEError1("No name specified");

        return MOUNT_NO_NAME;
    }

    // Is the mount enabled?
    auto mount = this->mount(path);

    // Mount's enabled.
    if (mount)
    {
        // Is this name already taken by another mount?
        auto other = this->mount(flags.mName);

        // Name's taken by another enabled mount.
        if (other && other != mount)
        {
            FUSEErrorF("Name %s already taken by mount: %s",
                       flags.mName.c_str(),
                       other->path().toPath(false).c_str());

            return MOUNT_NAME_TAKEN;
        }
    }

    // Update the mount's flags.
    auto transaction = mContext.mDatabase.transaction();
    auto query = transaction.query(mQueries.mSetMountFlagsByPath);

    flags.serialize(query);

    query.param(":path") = path;
    query.execute();

    // No mount is associated with this path.
    if (!query.changed())
        return MOUNT_UNKNOWN;

    // Mount's enabled.
    if (mount)
    {
        auto name = mount->name();

        // Keep mount consistent with the database.
        mount->flags(flags);

        // Keep the by-name index up to date.
        mByName.erase(name);
        mByName.emplace(flags.mName, mount);
    }

    transaction.commit();

    // Mount's flags have been updated.
    return MOUNT_SUCCESS;
}
catch (std::runtime_error& exception)
{
    // Unexpected error while updating the mount's flags.
    FUSEErrorF("Unable to update flags for mount %s: %s",
               path.toPath(false).c_str(),
               exception.what());

    return MOUNT_UNEXPECTED;
}

MountFlagsPtr MountDB::flags(const LocalPath& path) const
try
{
    auto guard = lockAll(mContext.mDatabase, *this);

    // Fast path: Mount's enabled and in memory.
    if (auto mount = this->mount(path))
        return std::make_unique<MountFlags>(mount->flags());

    // Retrieve mount's flags from the database.
    auto transaction = mContext.mDatabase.transaction();
    auto query = transaction.query(mQueries.mGetMountFlagsByPath);

    query.param(":path") = path;
    query.execute();

    // No mount's associated with the specified path.
    if (!query)
        return nullptr;

    // Deserialize the mount's flags.
    auto flags = MountFlags::deserialize(query);

    // And return them to the caller.
    return std::make_unique<MountFlags>(std::move(flags));
}
catch (std::runtime_error& exception)
{
    // Unexpected error while retrieving the mount's flags.
    FUSEErrorF("Unable to retrieve flags for mount %s: %s",
               path.toPath(false).c_str(),
               exception.what());

    return nullptr;
}

MountInfoPtr MountDB::get(const LocalPath& path) const
try
{
    auto guard = lockAll(mContext.mDatabase, *this);

    // Fast path: Mount's enabled and in memory.
    if (auto mount = this->mount(path))
        return std::make_unique<MountInfo>(mount->info());

    // Retrieve the mount's description from the database.
    auto transaction = mContext.mDatabase.transaction();
    auto query = transaction.query(mQueries.mGetMountByPath);

    query.param(":path") = path;
    query.execute();

    // No mount associated with the specified path.
    if (!query)
        return nullptr;

    // Deserialize the mount's description.
    auto info = MountInfo::deserialize(query);

    // And return it to the caller.
    return std::make_unique<MountInfo>(std::move(info));
}
catch (std::runtime_error& exception)
{
    // Unexpected error while retrieving the mount's description.
    FUSEErrorF("Unable to retrieve information for mount %s: %s",
               path.toPath(false).c_str(),
               exception.what());

    return nullptr;
}

MountInfoVector MountDB::get(bool enabled) const
try
{
    auto guard = lockAll(mContext.mDatabase, *this);

    MountInfoVector mounts;

    // Caller's interested only in mounts that are enabled.
    if (enabled)
    {
        // Copy descriptions of enabled mounts.
        for (auto& i : mByPath)
            mounts.emplace_back(i.second->info());

        // And return them to the caller.
        return mounts;
    }

    // Retrieve descriptions of all known mounts from the database.
    auto transaction = mContext.mDatabase.transaction();
    auto query = transaction.query(mQueries.mGetMounts);

    query.execute();

    // Deserialize each description.
    for ( ; query; ++query)
        mounts.emplace_back(MountInfo::deserialize(query));

    // And return them all to the caller.
    return mounts;
}
catch (std::runtime_error& exception)
{
    // Unepected error while retrieving mount descriptions.
    FUSEErrorF("Unable to retrieve mount information: %s",
               exception.what());

    return MountInfoVector();
}

NormalizedPathVector MountDB::paths(const std::string& name) const
try
{
    auto guard = lockAll(mContext.mDatabase, *this);

    auto paths = NormalizedPathVector();
    auto transaction = mContext.mDatabase.transaction();
    auto query = transaction.query(mQueries.mGetMountPathsByName);

    query.param(":name") = name;
    query.execute();

    for ( ; query; ++query)
        paths.emplace_back(query.field("path"));

    return paths;
}
catch (std::runtime_error& exception)
{
    FUSEErrorF("Unable to retrieve paths of mounts with name %s: %s",
               name.c_str(),
               exception.what());

    return NormalizedPathVector();
}

MountResult MountDB::prune()
try
{
    auto guard = lockAll(mContext.mDatabase, *this);
    auto transaction = mContext.mDatabase.transaction();
    auto query = transaction.query(mQueries.mRemoveTransientMounts);

    query.execute();
    transaction.commit();

    return MOUNT_SUCCESS;
}
catch (std::runtime_error& exception)
{
    FUSEErrorF("Unable to prune transient mounts: %s",
               exception.what());

    return MOUNT_UNEXPECTED;
}

MountResult MountDB::remove(const LocalPath& path)
try
{
    auto guard = lockAll(mContext.mDatabase, *this);

    if (mByPath.count(path))
    {
        FUSEErrorF("Can't remove an enabled mount: %s",
                   path.toPath(false).c_str());

        return MOUNT_BUSY;
    }

    auto transaction = mContext.mDatabase.transaction();
    auto query = transaction.query(mQueries.mRemoveMountByPath);

    query.param(":path") = path;
    query.execute();

    transaction.commit();

    return MOUNT_SUCCESS;
}
catch (std::runtime_error& exception)
{
    FUSEErrorF("Unable to remove mount %s: %s",
               path.toPath(false).c_str(),
               exception.what());

    return MOUNT_UNEXPECTED;
}

bool MountDB::syncable(const NormalizedPath& path) const
{
    // Acquire lock.
    MountDBLock guard(*this);

    // Check if any active mount is related to path.
    for (auto& m : mByPath)
    {
        if (path.related(m.second->path()))
            return false;
    }

    // Path isn't related to any mount.
    return true;
}

} // fuse
} // mega

