#pragma once

#include <functional>

#include <mega/common/activity_monitor.h>
#include <mega/common/client_forward.h>
#include <mega/common/lockable.h>
#include <mega/common/normalized_path_forward.h>
#include <mega/common/query.h>
#include <mega/common/task_executor_flags_forward.h>
#include <mega/fuse/common/file_cache_forward.h>
#include <mega/fuse/common/inode_db_forward.h>
#include <mega/fuse/common/inode_id_forward.h>
#include <mega/fuse/common/mount_db_forward.h>
#include <mega/fuse/common/mount_flags_forward.h>
#include <mega/fuse/common/mount_info_forward.h>
#include <mega/fuse/common/service_callbacks.h>
#include <mega/fuse/platform/mount_forward.h>
#include <mega/fuse/platform/service_context_forward.h>

#include <mega/types.h>

namespace mega
{
namespace common
{

template<>
struct LockableTraits<fuse::MountDB>
  : public LockableTraitsCommon<fuse::MountDB, std::mutex>
{
}; // LockableTraits<fuse::MountDB>

} // common

namespace fuse
{

// Manages mappings between the cloud and the local disk.
//
// Each mapping is like a one-way portal: They can manipulate
// entities in the cloud through that mapping's local path.
class MountDB
  : public common::Lockable<MountDB>
{
    friend class platform::Mount;

    // Bundles up all of the MountDB's queries.
    struct Queries
    {
        Queries(common::Database& database);

        // Add a mount to the database.
        common::Query mAddMount;

        // Get a mount by name.
        common::Query mGetMountByName;

        // What are a mount's flags?
        common::Query mGetMountFlagsByName;

        // What inode is the mount associated with?
        common::Query mGetMountInodeByName;

        // What path is associated with a given name?
        common::Query mGetMountPathByName;

        // Get a mount's startup state.
        common::Query mGetMountStartupStateByName;

        // Get a list of all known mounts.
        common::Query mGetMounts;

        // What mounts should be enabled at startup?
        common::Query mGetMountsEnabledAtStartup;

        // Remove a specified mount.
        common::Query mRemoveMountByName;

        // Remove transient mounts.
        common::Query mRemoveTransientMounts;

        // Set a mount's flags.
        common::Query mSetMountFlagsByName;

        // Set a mount's startup state.
        common::Query mSetMountStartupStateByName;
    }; // Queries

    // Checks whether info is a valid description of a mount.
    MountResult check(const MountInfo& info);

    // Checks whether a mount's local path is valid.
    virtual MountResult check(const common::Client& client,
                              const MountInfo& info) const = 0;

    // Perform platform-specific deinitialization.
    virtual void doDeinitialize();

    // Enable all persistent mounts.
    void enable();

    // Invalidate information cached by all mounts.
    void invalidate();

    // Query which mount is associated with a name.
    platform::MountPtr mount(const std::string& name) const;

    // Query which mount is associated with a path.
    platform::MountPtr mount(const LocalPath& path) const;

    // What cloud nodes are currently mounted?
    NodeHandleVector mounted() const;

    // Removes the specified mount from our indexes.
    platform::MountPtr remove(platform::Mount& mount);

    // Tracks which mounts are associated with what handle.
    platform::ToMountPtrSetMap<NodeHandle> mByHandle;

    // Tracks which mount is associated with what name.
    platform::ToMountPtrMap<std::string> mByName;

    // Tracks which mount is associated with what path.
    platform::ToMountPtrMap<LocalPath> mByPath;

    // How should we handle the "nodes current" event?
    void (MountDB::*mOnCurrent)();

    // What queries do we perform?
    mutable Queries mQueries;

protected:
    MountDB(platform::ServiceContext& context);

    ~MountDB();

    // Disable all enabled mounts.
    void disable();

    // Tracks whether we have any callbacks in progress.
    common::ActivityMonitor mActivities;

public:
    // Add a new mount to the database.
    MountResult add(const MountInfo& info);

    // Retrieve the client that contains this Mount DB.
    common::Client& client() const;

    // What mount contains the specified path?
    MountInfoPtr contains(const LocalPath& path,
                          bool enabled,
                          LocalPath* relativePath = nullptr) const;

    // Called by the client when its view of the cloud is current.
    void current();

    // Prepare the Mount DB for destruction.
    void deinitialize();

    // Disable an enabled mount.
    void disable(MountDisabledCallback callback,
                 const std::string& name,
                 bool remember);

    // Disable all mounts associated with the specified node.
    void disable(NodeHandle handle);

    // Execute a function on each enabled mount.
    void each(std::function<void(platform::Mount&)> function);

    // Enable a disabled mount.
    MountResult enable(const std::string& name, bool remember);

    // Query whether the specified mount is enabled.
    bool enabled(const std::string& name) const;

    // Update executor flags.
    void executorFlags(const common::TaskExecutorFlags& flags);

    // Query executor flags.
    common::TaskExecutorFlags executorFlags() const;

    // Retrieve a reference to the file cache.
    FileCache& fileCache();

    // Update an existing mount's flags.
    MountResult flags(const std::string& name,
                      const MountFlags& flags);

    // Query an existing mount's flags.
    MountFlagsPtr flags(const std::string& name) const;

    // Retrieve a description of an existing mount.
    MountInfoPtr get(const std::string& name) const;

    // Retrieve a list of known mounts.
    MountInfoVector get(bool onlyEnabled) const;

    // Retrieve a reference to the inode DB.
    InodeDB& inodeDB();

    // Query which path a named mount is associated with.
    common::NormalizedPath path(const std::string& name) const;

    // Prune stale mount entries from the database.
    MountResult prune();

    // Remove a disabled mount from the database.
    MountResult remove(const std::string& name);

    // Check whether the specified path is "syncable."
    bool syncable(const common::NormalizedPath& path) const;

    // The context this database belongs to.
    platform::ServiceContext& mContext;
}; // MountDB

} // fuse
} // mega

