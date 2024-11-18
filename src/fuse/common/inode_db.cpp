#include <cassert>
#include <chrono>
#include <map>
#include <tuple>

#include <mega/fuse/common/any_lock.h>
#include <mega/fuse/common/any_lock_set.h>
#include <mega/fuse/common/bind_handle.h>
#include <mega/fuse/common/client.h>
#include <mega/fuse/common/database.h>
#include <mega/fuse/common/directory_inode.h>
#include <mega/fuse/common/error_or.h>
#include <mega/fuse/common/file_cache.h>
#include <mega/fuse/common/file_info.h>
#include <mega/fuse/common/file_inode.h>
#include <mega/fuse/common/file_io_context.h>
#include <mega/fuse/common/inode_db.h>
#include <mega/fuse/common/inode_info.h>
#include <mega/fuse/common/logging.h>
#include <mega/fuse/common/mount_db.h>
#include <mega/fuse/common/node_event.h>
#include <mega/fuse/common/node_event_queue.h>
#include <mega/fuse/common/node_event_type.h>
#include <mega/fuse/common/node_info.h>
#include <mega/fuse/common/query.h>
#include <mega/fuse/common/scoped_query.h>
#include <mega/fuse/common/transaction.h>
#include <mega/fuse/platform/mount.h>
#include <mega/fuse/platform/path_adapter.h>
#include <mega/fuse/platform/service_context.h>

#include <mega/utils.h>

namespace mega
{
namespace fuse
{

class InodeDB::EventObserver
{
    // Called when a node's been added.
    void added(const NodeEvent& event);

    // Retrieve a reference to the database.
    Database& database() const;

    // Retrieve a reference to the file cache.
    FileCache& fileCache() const;

    // Retrieve a reference to the file extension DB.
    FileExtensionDB& fileExtensionDB() const;

    // Called when a node's been modified.
    void modified(const NodeEvent& event);

    // Retrieve a reference to the mount DB.
    MountDB& mountDB() const;

    // Called when a node's been moved or renamed.
    void moved(const NodeEvent& event);

    // Called when a node's permissions have changed.
    void permissions(const NodeEvent& event);
    
    // Retrieve a reference to the InodeDB's queries.
    auto queries() const -> Queries&;

    // Called when a node's been removed.
    void removed(const NodeEvent& event);

    // The InodeDB we're processing events for.
    InodeDB& mInodeDB;

    // Ensures exclusive access to the database.
    DatabaseLock mDatabaseLock;

    // Ensures exclusive access to the inode DB.
    InodeDBLock mInodeDBLock;

    // So we can perform queries.
    Transaction mTransaction;

public:
    EventObserver(InodeDB& inodeDB);

    ~EventObserver();

    // Process the specified node events.
    void operator()(NodeEventQueue& events);
}; // EventObserver

InodeDB::Queries::Queries(Database& database)
  : mAddInode(database.query())
  , mClearBindHandles(database.query())
  , mGetChildrenByParentHandle(database.query())
  , mGetExtensionAndInodeIDByHandle(database.query())
  , mGetExtensionAndInodeIDByNameAndParentHandle(database.query())
  , mGetHandleByID(database.query())
  , mGetInodeByHandle(database.query())
  , mGetInodeByID(database.query())
  , mGetInodeIDByBindHandleOrHandle(database.query())
  , mGetInodeIDByNameAndParentHandle(database.query())
  , mGetInodeIDByParentHandle(database.query())
  , mGetModifiedByID(database.query())
  , mGetModifiedInodes(database.query())
  , mGetNextInodeID(database.query())
  , mIncrementNextInodeID(database.query())
  , mRemoveInodeByID(database.query())
  , mSetBindHandleByID(database.query())
  , mSetBindHandleHandleNameParentHandleByID(database.query())
  , mSetModifiedByID(database.query())
  , mSetNameParentHandleByID(database.query())
{
    mAddInode = "insert into inodes values ( "
                "  :bind_handle, "
                "  :extension, "
                "  :handle, "
                "  :id, "
                "  :modified, "
                "  :name, "
                "  :parent_handle "
                ")";

    mClearBindHandles = "update inodes set bind_handle = null";

    mGetChildrenByParentHandle = "select bind_handle "
                                 "     , extension "
                                 "     , handle "
                                 "     , id "
                                 "     , name "
                                 "  from inodes "
                                 " where parent_handle = :parent_handle";

    mGetExtensionAndInodeIDByHandle = "select extension "
                                      "     , id "
                                      "  from inodes "
                                      " where handle = :handle";

    mGetExtensionAndInodeIDByNameAndParentHandle =
      "select extension "
      "     , id "
      "  from inodes "
      " where name = :name "
      "   and parent_handle = :parent_handle";

    mGetHandleByID = "select handle from inodes where id = :id";

    mGetInodeByHandle = "select * "
                        "  from inodes "
                        " where handle = :handle";

    mGetInodeByID = "select * "
                    "  from inodes "
                    " where id = :id";

    mGetInodeIDByBindHandleOrHandle = "select id "
                                      "  from inodes "
                                      " where handle = :handle "
                                      "    or bind_handle not null "
                                      "   and bind_handle = :bind_handle";

    mGetInodeIDByNameAndParentHandle =
      "select id "
      "  from inodes "
      " where name = :name "
      "   and parent_handle = :parent_handle";

    mGetInodeIDByParentHandle = "select id "
                                "  from inodes "
                                " where parent_handle = :parent_handle";

    mGetModifiedByID = "select modified from inodes where id = :id";

    mGetModifiedInodes = "select * from inodes where modified = 1";

    mGetNextInodeID = "select next from inode_id";

    mIncrementNextInodeID = "update inode_id set next = next + 1";

    mRemoveInodeByID = "delete from inodes where id = :id";

    mSetBindHandleByID = "update inodes "
                         "   set bind_handle = :bind_handle "
                         " where id = :id";

    mSetBindHandleHandleNameParentHandleByID = "update inodes "
                                               "   set bind_handle = :bind_handle "
                                               "     , handle = :handle "
                                               "     , name = :name "
                                               "     , parent_handle = :parent_handle "
                                               " where id = :id";

    mSetModifiedByID = "update inodes "
                       "   set modified = :modified"
                       " where id = :id";

    mSetNameParentHandleByID = "update inodes "
                               "   set name = :name "
                               "     , parent_handle = :parent_handle "
                               " where id = :id";
}

InodeRef InodeDB::add(InodePtr (InodeDB::*build)(const NodeInfo&),
                      const NodeInfo& info)
{
    // Convenience.
    auto id = InodeID(info.mHandle);
    auto handle = info.mHandle;

    // Sanity.
    assert(build);
    assert(!handle.isUndef());
    assert(!mByHandle.count(handle));
    assert(!mByID.count(id));

    // Instantiate a new inode.
    auto ptr = (this->*build)(info);

    // Add the inode to the index.
    auto h = mByHandle.emplace(handle, ptr.get()).first;

    mByID.emplace(id, std::move(ptr));

    // Return inode to caller.
    return InodeRef(h->second->accessed());
}

InodeID InodeDB::addFile(const FileExtension& extension,
                         const std::string& name,
                         NodeHandle parentHandle,
                         Transaction& transaction)
{
    auto query = transaction.query(mQueries.mGetNextInodeID);

    // Allocate a new inode ID.
    query.execute();

    auto id = query.field("next").inode();

    // Make sure we haven't wrapped around.
    assert(id);

    // Mark ID as having been allocated.
    query = transaction.query(mQueries.mIncrementNextInodeID);

    query.execute();

    // Add the file to the database.
    query = transaction.query(mQueries.mAddInode);

    query.param(":bind_handle") = nullptr;
    query.param(":extension") = extension;
    query.param(":handle") = nullptr;
    query.param(":id") = id;
    query.param(":modified") = false;
    query.param(":name") = name;
    query.param(":parent_handle") = parentHandle;
    query.execute();

    // Return new ID to caller.
    return id;
}

InodePtr InodeDB::buildDirectory(const NodeInfo& info)
{
    return std::make_unique<DirectoryInode>(InodeID(info.mHandle),
                                       info,
                                       *this);
}

InodePtr InodeDB::buildFile(const NodeInfo& info)
{
    return std::make_unique<FileInode>(InodeID(info.mHandle),
                                  info,
                                  *this);
}

InodeRef InodeDB::child(const DirectoryInode& parent,
                        const std::string& name) const
{
    // Sanity.
    assert(!name.empty());

    // Check if the parent contains a child with this name.
    auto childID = hasChild(parent, name);

    // Parent has a child with this name.
    if (childID)
        return get(childID);

    // Parent doesn't have a child with this name.
    return InodeRef();
}

InodeRef InodeDB::child(const std::string& name,
                        NodeHandle parentHandle,
                        MemoryOnlyTag) const
{
    InodeDBLock guard(*this);

    // Is there an inode in memory at this location?
    auto p = std::make_pair(parentHandle, &name);
    auto i = mByParentHandleAndName.find(p);

    // An inode's in memory at this location.
    if (i != mByParentHandleAndName.end())
        return InodeRef(i->second->accessed());

    // No inode's in memory at this location.
    return InodeRef();
}

void InodeDB::childAdded(const Inode& inode,
                         const std::string& name,
                         NodeHandle parentHandle)
{
    auto pair = std::make_pair(parentHandle, &name);

    // Does a child with this name already exist under this parent?
    auto i = mByParentHandleAndName.find(pair);

    // Child already exists: consider it replaced.
    if (i != mByParentHandleAndName.end())
        i->second->removed(true);

    // Add ourselves to the map.
    auto result = mByParentHandleAndName.emplace(
                    std::piecewise_construct,
                    std::forward_as_tuple(parentHandle, &name),
                    std::forward_as_tuple(const_cast<Inode*>(&inode)));

    // Make sure the name isn't already occupied under this parent.
    assert(result.second);

    // Silence the compiler.
    static_cast<void>(result);
}

void InodeDB::childRemoved([[maybe_unused]] const Inode& inode,
                           const std::string& name,
                           NodeHandle parentHandle)
{
    // Convenience.
    auto pair = std::make_pair(parentHandle, &name);

    // What entry are we in the map?
    auto i = mByParentHandleAndName.find(pair);

    // Does this entry describe us?
    assert(i != mByParentHandleAndName.end()
           && i->second == &inode);

    // Remove ourselves from the map.
    mByParentHandleAndName.erase(i);
}

InodeRefVector InodeDB::children(const DirectoryInode& parent) const
{
    // So we can look up strings by reference.
    struct StringPtrLess {
        bool operator()(const std::string* lhs,
                        const std::string* rhs) const
        {
            return *lhs < *rhs;
        }
    }; // StringPtrLess

    // Convenience.
    using StringPtrToNodeInfoPtrMap =
      std::map<const std::string*,
               NodeInfoList::iterator,
               StringPtrLess>;

    // Stores the description of each of this node's children.
    NodeInfoList storage;

    // Maps a child's name to its description.
    StringPtrToNodeInfoPtrMap descriptions;

    // Insert dummy for purposes of duplicate detection.
    storage.emplace_back();

    // What children are present in the cloud?
    client().each([&](NodeInfo description) {
        // Have we seen a child with this name before?
        auto i = descriptions.find(&description.mName);

        // Haven't seen a child with this name before.
        if (i == descriptions.end())
        {
            auto j = storage.end();

            // Latch the child's description.
            j = storage.emplace(j, std::move(description));

            // Add child to index.
            descriptions[&j->mName] = j;

            // Process the next child.
            return;
        }

        // We've already detected a duplicate with this name.
        if (i->second == storage.begin())
            return;

        // Remove existing child's description.
        storage.erase(i->second);

        // Mark name as a duplicate.
        i->second = storage.begin();
    }, parent.handle());

    auto guard = lockAll(mContext.mDatabase, *this);

    auto transaction = mContext.mDatabase.transaction();
    auto query = transaction.query(mQueries.mGetChildrenByParentHandle);

    // What children are present on disk?
    query.param(":parent_handle") = parent.handle();
    query.execute();

    // What children need to be loaded?
    struct PendingChild
    {
        PendingChild(FileExtension extension,
                     std::string name,
                     InodeID id)
          : mExtension(std::move(extension))
          , mName(std::move(name))
          , mID(id)
        {
        }

        // The child's extension.
        FileExtension mExtension;

        // The child's name.
        std::string mName;

        // The child's ID.
        InodeID mID;
    }; // PendingChild

    using PendingChildVector = std::vector<PendingChild>;

    // Children waiting to be loaded.
    PendingChildVector pending;

    // What children have been removed or replaced?
    InodeIDVector removed;

    for ( ; query; ++query)
    {
        auto id = query.field("id").inode();
        auto name = query.field("name").string();

        // Have we already seen a cloud child with this name?
        auto i = descriptions.find(&name);

        // A child with this name is present in the cloud.
        if (i != descriptions.end())
        {
            // Child was a duplicate.
            if (i->second == storage.begin())
                continue;

            // Cloud child has replaced this local child.
            removed.emplace_back(id);

            // Process next local child.
            continue;
        }

        // Child exists only locally.
        pending.emplace_back(fileExtensionDB().get(query.field("extension")),
                             std::move(name),
                             id);
    }

    InodeRefVector children;

    // Convenience.
    auto& self = const_cast<InodeDB&>(*this);

    // Pop dummy marker.
    storage.pop_front();

    // Prepare query.
    query = transaction.query(mQueries.mGetExtensionAndInodeIDByHandle);

    // Instantiate cloud children.
    while (!storage.empty())
    {
        // Latch child's description.
        auto info = std::move(storage.front());

        // Pop description.
        storage.pop_front();

        // Instantiate child.
        children.emplace_back(([&]() {
            // Is the child already in memory?
            auto h = mByHandle.find(info.mHandle);

            // Child's already in memory.
            if (h != mByHandle.end())
                return InodeRef(h->second->accessed());

            // Child's a directory.
            if (info.mIsDirectory)
                return self.add(&InodeDB::buildDirectory, info);

            query.reset();

            // Check if child's in the file cache.
            query.param(":handle") = info.mHandle;
            query.execute();

            // Child's not in the file cache.
            if (!query)
                return self.add(&InodeDB::buildFile, info);

            // Convenience.
            auto extension = fileExtensionDB().get(query.field("extension"));
            auto id = query.field("id").inode();

            // Try and get our hands on the file's info.
            auto fileInfo = fileCache().info(extension, id);

            // File's been removed from the cache.
            if (!fileInfo)
            {
                // Remember to purge the stale record.
                removed.emplace_back(id);

                // Return new child instance.
                return self.add(&InodeDB::buildFile, info);
            }

            // Instantiate child.
            auto ptr = std::make_unique<FileInode>(id, info, self);

            // Inject file info.
            ptr->fileInfo(std::move(fileInfo));

            // Establish reference to new child.
            auto ref = InodeRef(ptr->accessed());

            // Add child to index.
            mByHandle.emplace(info.mHandle, ptr.get());
            mByID.emplace(id, std::move(ptr));

            // Return new child instance.
            return ref;
        })());
    }

    // Instantiate local children.
    while (!pending.empty())
    {
        // Convenience.
        auto& child = pending.back();

        // Pop child's name and ID.
        auto extension = std::move(child.mExtension);
        auto name = std::move(child.mName);
        auto id = child.mID;

        pending.pop_back();

        // Is the child already in memory?
        auto i = mByID.find(id);

        // Child's already in memory.
        if (i != mByID.end())
        {
            // Add child to vector.
            children.emplace_back(i->second->accessed());

            // Process next child.
            continue;
        }

        // Try and get our hands on the file's info.
        auto fileInfo = fileCache().info(extension, id);

        // File's been removed from the cache.
        if (!fileInfo)
        {
            // Remember to purge stale record.
            removed.emplace_back(id);

            // Process next child.
            continue;
        }

        NodeInfo info;

        // Populate dummy description.
        info.mName = std::move(name);
        info.mParentHandle = parent.handle();

        // Instantiate child.
        auto ptr = std::make_unique<FileInode>(id, info, self);

        // Inject file info.
        ptr->fileInfo(std::move(fileInfo));

        // Add child to vector.
        children.emplace_back(ptr.get());

        // Add child to index.
        mByID.emplace(id, std::move(ptr));
    }

    query = transaction.query(mQueries.mRemoveInodeByID);

    // Prune stale database records.
    for (auto id : removed)
    {
        query.param(":id") = id;
        query.execute();
        query.reset();
    }

    // Commit transaction.
    transaction.commit();

    // Return children to caller.
    return children;
}

void InodeDB::current()
{
    // No-op for now but left in case we need it later.
}

bool InodeDB::discard() const
{
    InodeDBLock guard(*this);

    return mDiscard;
}

InodeRef InodeDB::get(Client& client, NodeHandle handle) const
{
    // Sanity.
    assert(!handle.isUndef());

    // Does the client know anything about this node?
    auto info = client.get(handle);

    // Client knows nothing.
    if (!info)
        return InodeRef();

    // Acquire lock.
    InodeDBLock guard(*this);

    // Has another thread loaded this inode?
    auto h = mByHandle.find(handle);

    // Inode was loaded by another thread.
    if (h != mByHandle.end())
        return InodeRef(h->second->accessed());

    // Is this node associated with an inode that's being bound?
    auto b = mByBindHandle.find(info->mBindHandle);

    // Nodes associated with an inode that's being bound.
    if (b != mByBindHandle.end())
        return InodeRef(b->second->accessed());

    // Assume we're instantiating a file inode.
    auto build = &InodeDB::buildFile;

    // Actually instantiating a directory.
    if (info->mIsDirectory)
        build = &InodeDB::buildDirectory;

    // Instantiate new inode and add it to the index.
    auto ref = const_cast<InodeDB&>(*this).add(build, *info);

    // Return new inode to caller.
    return ref;
}

InodeRef InodeDB::get(FileCache& fileCache,
                      NodeHandle handle,
                      AnyLockSet locks) const
{
    // Make sure we're holding the lock.
    assert(locks.owns_lock());

    // Is this inode present in the file cache?
    auto transaction = mContext.mDatabase.transaction();
    auto query = transaction.query(mQueries.mGetInodeByHandle);

    query.param(":handle") = handle;
    query.execute();

    // Inode isn't present in the cache.
    if (!query)
        return InodeRef();

    return get(fileCache,
               std::move(locks),
               std::move(query),
               std::move(transaction));
}

InodeRef InodeDB::get(FileCache& fileCache,
                      InodeID id,
                      AnyLockSet locks) const
{
    // Make sure we're holding the lock.
    assert(locks.owns_lock());

    // Is this inode present in the file cache?
    auto transaction = mContext.mDatabase.transaction();
    auto query = transaction.query(mQueries.mGetInodeByID);

    query.param(":id") = id;
    query.execute();

    // Inode isn't present in the cache.
    if (!query)
        return InodeRef();

    return get(fileCache,
               std::move(locks),
               std::move(query),
               std::move(transaction));
}

InodeRef InodeDB::get(FileCache& fileCache,
                      AnyLockSet locks,
                      ScopedQuery query,
                      Transaction transaction) const
{
    // Make sure the lock's held.
    assert(locks.owns_lock());

    // Make sure we actually found an entry in the database.
    assert(query);

    // Convenience.
    auto extension = fileExtensionDB().get(query.field("extension"));
    auto id = query.field("id").inode();

    // If the inode's in the cache, it must be a file.
    //
    // Try and determine the file's local state.
    auto fileInfo = fileCache.info(extension, id);

    // The file's state is no longer accessible.
    if (!fileInfo)
    {
        // So prune the stale cache entry.
        query = transaction.query(mQueries.mRemoveInodeByID);

        query.param(":id") = id;
        query.execute();

        transaction.commit();

        return InodeRef();
    }

    NodeInfo info;

    // Latch information from the database.
    if (!query.field("handle").null())
        info.mHandle = query.field("handle").handle();

    if (!query.field("name").null())
        info.mName = query.field("name").string();

    if (!query.field("parent_handle").null())
        info.mParentHandle = query.field("parent_handle");

    // Release query, transaction and lock so we can call the client.
    query.reset();
    transaction.rollback();
    locks.unlock();

    // File's local state is present.
    do
    {
        // Convenience.
        auto& client = this->client();

        // File was present in the cloud.
        if (!info.mHandle.isUndef())
        {
            // Is the file still present in the cloud?
            auto info_ = client.get(info.mHandle);

            // File's no longer present in the cloud.
            if (!info_)
                break;

            // Latch current name and parent.
            info = std::move(*info_);
        }

        // File was never present in the cloud.
        if (info.mHandle.isUndef())
        {
            // And its parent no longer exists.
            if (!client.exists(info.mParentHandle))
                break;
        }

        // Reacquire lock.
        locks.lock();

        // Has another thread loaded this inode?
        auto i = mByID.find(id);

        // Another thread loaded the inode.
        if (i != mByID.end())
            return InodeRef(i->second->accessed());

        // Instantiate the inode.
        auto ptr = std::make_unique<FileInode>(id,
                                          std::move(info),
                                          const_cast<InodeDB&>(*this));

        // Inject local file state.
        ptr->fileInfo(std::move(fileInfo));

        // Add the inode to the index.
        i = mByID.emplace(id, std::move(ptr)).first;

        // File doesn't exist in the cloud.
        if (info.mHandle.isUndef())
            return InodeRef(i->second->accessed());

        // File's present in the cloud.

        // Sanity.
        assert(!mByHandle.count(info.mHandle));

        // We don't record the name or parent of a file in the cloud.
        transaction = mContext.mDatabase.transaction();
        query = transaction.query(mQueries.mSetBindHandleHandleNameParentHandleByID);

        query.param(":bind_handle") = nullptr;
        query.param(":handle") = info.mHandle;
        query.param(":id") = id;
        query.param(":name") = nullptr;
        query.param(":parent_handle") = nullptr;
        query.execute();

        transaction.commit();

        // Add handle to the index.
        mByHandle.emplace(info.mHandle, i->second.get());

        // Return inode to the caller.
        return InodeRef(i->second->accessed());
    }
    while (0);

    // Reacquire lock.
    locks.lock();

    // Prune stale entry from file cache.
    transaction = mContext.mDatabase.transaction();
    query = transaction.query(mQueries.mRemoveInodeByID);

    query.param(":id") = id;
    query.execute();

    transaction.commit();

    return InodeRef();
}

void InodeDB::handle(FileInode& file,
                     NodeHandle& oldHandle,
                     NodeHandle newHandle)
{
    // Acquire locks.
    auto guard = lockAll(mContext.mDatabase, *this);

    // Sanity.
    assert(!newHandle.isUndef());

    // Swap new handle into place.
    std::swap(oldHandle, newHandle);

    auto transaction = mContext.mDatabase.transaction();
    auto query = transaction.query(mQueries.mSetBindHandleHandleNameParentHandleByID);

    // Record the inode's new handle in the database.
    query.param(":bind_handle") = nullptr;
    query.param(":handle") = oldHandle;
    query.param(":id") = file.id();
    query.param(":name") = nullptr;
    query.param(":parent_handle") = nullptr;
    query.execute();

    // Handle hasn't changed.
    if (oldHandle == newHandle)
        return transaction.commit();

    // Sanity.
    assert(!mByHandle.count(oldHandle));

    // Add new assocation.
    mByHandle.emplace(oldHandle, &file);

    // Remove any old association.
    auto count = mByHandle.erase(newHandle);

    assert(!count == newHandle.isUndef());

    // Silence compiler.
    static_cast<void>(count);

    // Persist database changes.
    transaction.commit();
}

InodeID InodeDB::hasChild(const DirectoryInode& parent,
                          const std::string& name) const
{
    // Sanity.
    assert(!name.empty());

    // Convenience.
    auto bindHandle = BindHandle();
    auto parentHandle = parent.handle();

    // Does the child exist in the cloud?
    auto childHandle = client().handle(parentHandle, name, &bindHandle);
    
    // Acquire locks.
    auto guard = lockAll(mContext.mDatabase, *this);

    // Child exists in the cloud.
    if (!childHandle.isUndef())
    {
        // Sanity.
        assert(bindHandle);

        // Assume the child's inode ID is its node handle.
        auto id = InodeID(childHandle);

        auto transaction = mContext.mDatabase.transaction();
        auto query = transaction.query(mQueries.mGetInodeIDByBindHandleOrHandle);

        // Check if the inode is known locally.
        query.param(":bind_handle") = bindHandle;
        query.param(":handle") = childHandle;
        query.execute();

        // Child's known locally (under some parent.)
        if (query)
        {
            // Latch the child's actual inode ID.
            id = query.field("id").inode();

            // We don't track a cloud node's name or parent.
            query = transaction.query(mQueries.mSetBindHandleHandleNameParentHandleByID);

            query.param(":bind_handle") = nullptr;
            query.param(":handle") = childHandle;
            query.param(":id") = id;
            query.param(":name") = nullptr;
            query.param(":parent_handle") = nullptr;
            query.execute();
        }

        // Check if the child is known locally under *this* parent.
        query = transaction.query(mQueries.mGetInodeIDByNameAndParentHandle);

        query.param(":name") = name;
        query.param(":parent_handle") = parentHandle;
        query.execute();

        // No other child exists with this name.
        if (!query)
            return transaction.commit(), id;

        // Another child exists with this name.
        auto otherID = query.field("id").inode();

        // Consider it "replaced."
        query = transaction.query(mQueries.mSetNameParentHandleByID);

        query.param(":id") = otherID;
        query.param(":name") = nullptr;
        query.param(":parent_handle") = nullptr;
        query.execute();

        transaction.commit();

        // Return inode ID to caller.
        return id;
    }

    // Does the child exist locally?
    auto transaction = mContext.mDatabase.transaction();
    auto query = transaction.query(mQueries.mGetInodeIDByNameAndParentHandle);

    query.param(":name") = name;
    query.param(":parent_handle") = parentHandle;
    query.execute();

    // Child exists locally.
    if (query)
        return query.field("id");

    // No child exists locally.
    return InodeID();
}

ErrorOr<bool> InodeDB::hasChildren(const DirectoryInode& directory) const
{
    // Convenience.
    auto parentHandle = directory.handle();

    // Does this directory have any children in the cloud?
    auto result = client().hasChildren(parentHandle);

    // Parent no longer exists in the cloud.
    if (!result)
        return result;

    // Parent has children in the cloud.
    if (*result)
        return result;

    // Acquire lock.
    auto guard = lockAll(mContext.mDatabase, *this);

    // Does this directory contain any local children?
    auto transaction = mContext.mDatabase.transaction();
    auto query = transaction.query(mQueries.mGetInodeIDByParentHandle);

    query.param(":parent_handle") = parentHandle;
    query.execute();

    return query;
}

ErrorOr<MakeInodeResult> InodeDB::makeDirectory(const platform::Mount&,
                                                const std::string& name,
                                                DirectoryInodeRef parent)
{
    // Sanity.
    assert(parent);

    // Try and make the new directory.
    auto result = client().makeDirectory(name, parent->handle());

    // Couldn't create the directory.
    if (!result)
        return result.error();

    // Convenience.
    auto info = std::move(*result);

    // Lock the database.
    InodeDBLock guard(*this);

    // Has another thread instantiated an inode for this directory?
    auto ref = get(info.mHandle, true);

    // No inode has been instantiated yet.
    if (!ref)
        ref = add(&InodeDB::buildDirectory, info);

    // Return result to caller.
    return std::make_tuple(std::move(ref), std::move(info));
}

ErrorOr<MakeInodeResult> InodeDB::makeFile(const platform::Mount& mount,
                                           const std::string& name,
                                           DirectoryInodeRef parent)
{
    // Sanity.
    assert(parent);

    // Lock the database.
    auto lock = lockAll(mContext.mDatabase, *this);
    auto transaction = mContext.mDatabase.transaction();

    // Add the new file to the database.
    auto extension = fileExtensionDB().getFromPath(name);
    auto id = addFile(extension, name, parent->handle(), transaction);

    // Add a new file to the cache.
    auto fileInfo = fileCache().create(extension, id);

    // Couldn't add a new file to the cache.
    if (!fileInfo)
        return fileInfo.error();

    // Create a description of our new file.
    NodeInfo info;

    info.mIsDirectory = false;
    info.mName = name;
    info.mModified = (*fileInfo)->modified();
    info.mParentHandle = parent->handle();
    info.mPermissions = FULL;
    info.mSize = 0;

    // Make an inode to represent the new file.
    auto ref = ([&]() {
        // Instantiate a new inode to represent the file.
        auto ptr = std::make_unique<FileInode>(id,
                                          info,
                                          const_cast<InodeDB&>(*this));

        // Sanity.
        assert(!mByID.count(id));

        // Add the inode to our index.
        auto i = mByID.emplace(id, std::move(ptr)).first;

        // Return a reference to our new inode.
        return i->second->file();
    })();

    // Let the inode know about its attributes.
    ref->fileInfo(*fileInfo);

    // Persist database changes.
    transaction.commit();

    // Release lock.
    lock.unlock();

    // Make sure our new file is flushed to the cloud.
    fileCache().context(ref)->modified(mount);

    // Let the mounts know a new file has been created.
    mContext.mMountDB.each([&](Mount& mount) {
        mount.invalidateEntry(name, parent->id());
    });

    // Return result to caller.
    return std::make_tuple(std::move(ref),
                           InodeInfo(id, std::move(info)));
}

auto InodeDB::modified() const
  -> NodeHandleInodeIDPairVector
{
    // Acquire locks.
    auto lock = lockAll(mContext.mDatabase, *this);

    // Instantiate transaction and query.
    auto transaction = mContext.mDatabase.transaction();
    auto query = transaction.query(mQueries.mGetModifiedInodes);

    NodeHandleInodeIDPairVector modified;

    // Collect the ID of each modified inode.
    for (query.execute(); query; ++query)
    {
        // Compute the inode's effective node handle.
        auto handle = ([&]() -> NodeHandle {
            // Search for the inode's parent if it hasn't been uploaded.
            if (query.field("handle").null())
                return query.field("parent_handle");

            // Otherwise search for the inode itself.
            return query.field("handle");
        })();

        // Retrieve the inode's ID.
        auto id = query.field("id").inode();

        // Add the inode's information to our vector.
        modified.emplace_back(handle, id);
    }

    // Pass IDs to our caller.
    return modified;
}

Error InodeDB::move(InodeRef source,
                    const std::string& targetName,
                    DirectoryInodeRef targetParent)
{
    // Sanity.
    assert(source);
    assert(!targetName.empty());
    assert(targetParent);

    // Ask the client to move the child.
    return client().move(targetName,
                         source->handle(),
                         targetParent->handle());
}

Error InodeDB::move(FileInodeRef source,
                    const std::string& targetName,
                    DirectoryInodeRef targetParent)
{
    // Sanity.
    assert(source);
    assert(!targetName.empty());
    assert(targetParent);

    // Child exists in the cloud.
    if (!source->handle().isUndef())
        return move(InodeRef(std::move(source)),
                    targetName,
                    std::move(targetParent));

    // Convenience.
    auto id = source->id();
    auto sourceName = source->name();
    auto sourceParent = source->parent();
    auto targetParentHandle = targetParent->handle();

    // Child (may) exist locally.
    auto guard = lockAll(mContext.mDatabase, *this);

    // Update the database.
    auto transaction = mContext.mDatabase.transaction();
    auto query = transaction.query(mQueries.mSetNameParentHandleByID);

    query.param(":id") = id;
    query.param(":name") = targetName;
    query.param(":parent_handle") = targetParentHandle;
    query.execute();

    transaction.commit();

    // Let the mounts know the file's been moved.
    mContext.mMountDB.each([&](Mount& mount) {
        // Invalidate source directory entry.
        mount.invalidateEntry(sourceName, sourceParent->id());

        // Invalidate target directory entry.
        mount.invalidateEntry(targetName, targetParent->id());
    });

    // Let the child know that it's been moved.
    source->moved(targetName, targetParentHandle);

    // Return result to caller.
    return API_OK;
}

void InodeDB::remove(const DirectoryInode& inode, InodeDBLock lock)
{
    // Leave a record of what we've done.
    FUSEDebugF("Removing inode %s from memory",
               toString(inode.id()).c_str());

    // Directories are always present in both maps.
    auto count = mByHandle.erase(inode.handle());
    assert(count);

    count = mByID.erase(inode.id());
    assert(count);

    static_cast<void>(count);

    // Let any waiters know an inode's been removed.
    mCV.notify_all();
}

void InodeDB::remove(const FileInode& inode, InodeDBLock lock)
{
    // Convenience.
    auto id = inode.id();

    // Leave some clues for debuggers.
    FUSEDebugF("Removing inode %s from memory",
               toString(inode.id()).c_str());

    InodePtr ptr;

    // Remove the inode from the index.
    do
    {
        auto guard = std::move(lock);

        // Convenience.
        auto handle = inode.handle();

        // Get our hands on the inode's pointer.
        auto i = mByID.find(id);

        // Sanity.
        assert(i != mByID.end());

        // Latch the pointer for release.
        ptr = std::move(i->second);

        // Remove the inode from the ID index.
        mByID.erase(i);

        // Remove the inode from the other indexes.
        auto count = mByHandle.erase(handle);
        assert(!!count == !handle.isUndef());

        // Silence the compiler.
        static_cast<void>(count);
    }
    while (0);

    // Had the file been removed?
    auto removed = inode.removed();

    // File hasn't been removed.
    if (!removed)
    {
        // Let any waiters know an inode's been removed from memory.
        return mCV.notify_all();
    }

    // Purge the file from the cache if needed.
    if (auto info = inode.fileInfo())
        fileCache().remove(info->extension(), id);

    // Purge inode from the database.
    auto locks = lockAll(mContext.mDatabase, *this);
    auto transaction = mContext.mDatabase.transaction();
    auto query = transaction.query(mQueries.mRemoveInodeByID);

    query.param(":id") = id;
    query.execute();

    transaction.commit();

    // Let any waiters know an inode's been removed from memory.
    mCV.notify_all();
}

Error InodeDB::replace(FileInodeRef source,
                       FileInodeRef target,
                       const std::string& targetName,
                       DirectoryInodeRef targetParent)
{
    assert(source);
    assert(target);
    assert(targetParent);

    // Convenience.
    auto sourceHandle = source->handle();
    auto sourceName   = source->name();
    auto sourceParent = source->parent();
    auto targetHandle = target->handle();
    auto targetParentHandle = targetParent->handle();

    // Perform cloud processing if required.
    auto result = ([&]() {
        // Source exists in the cloud.
        if (!sourceHandle.isUndef())
        {
            // Target exists in the cloud.
            if (!targetHandle.isUndef())
                return client().replace(sourceHandle, targetHandle);

            // Only source exists in the cloud.
            return client().move(targetName,
                                 sourceHandle,
                                 targetParentHandle);
        }

        // Only target exists in the cloud.
        if (!targetHandle.isUndef())
            return client().remove(targetHandle);

        // Neither source nor target exist in the cloud.
        return Error(API_OK);
    })();

    // Couldn't perform move/remove/replace in the cloud.
    if (result != API_OK)
        return result;

    // Lock database.
    auto lock = lockAll(mContext.mDatabase, *this);

    auto transaction = mContext.mDatabase.transaction();
    auto query = transaction.query(mQueries.mRemoveInodeByID);

    // Remove target from the database.
    query.param(":id") = target->id();
    query.execute();

    // Source is a local file.
    if (sourceHandle.isUndef())
    {
        query = transaction.query(mQueries.mSetNameParentHandleByID);

        // Move source in the database.
        query.param(":id") = source->id();
        query.param(":name") = targetName;
        query.param(":parent_handle") = targetParentHandle;
        query.execute();
    }

    // Persist database changes.
    transaction.commit();

    // Let the mounts know the target has been replaced.
    mContext.mMountDB.each([&](Mount& mount) {
        // Source is a local file.
        if (sourceHandle.isUndef())
            mount.invalidateEntry(sourceName, sourceParent->id());

        // Target is a local file.
        if (targetHandle.isUndef())
            mount.invalidateEntry(targetName, targetParent->id());
    });

    // Let the target know it's been removed.
    if (targetHandle.isUndef())
        target->removed(true);

    // Let the source know it's been moved.
    if (sourceHandle.isUndef())
        source->moved(targetName, targetParentHandle);

    // Target's been replaced.
    return API_OK;
}

Error InodeDB::replace(DirectoryInodeRef source,
                       DirectoryInodeRef target,
                       const std::string&,
                       DirectoryInodeRef)
{
    assert(source);
    assert(target);

    // Ask the client to replace target with source.
    return client().replace(source->handle(), target->handle());
}

Error InodeDB::unlink(InodeRef inode)
{
    // Sanity.
    assert(inode);

    // Try and remove the node associated with this inode.
    auto result = client().remove(inode->handle());

    // Couldn't remove the node.
    if (result != API_OK)
        return result;

    // Mark inode as removed.
    inode->removed(true);

    // Let caller know whether the inode was removed.
    return result;
}

Error InodeDB::unlink(FileInodeRef file)
{
    assert(file);

    // Convenience.
    auto handle = file->handle();
    auto name   = file->name();
    auto parent = file->parent();
    auto result = API_OK;

    // File exists in the cloud.
    if (!handle.isUndef())
    {
        // Ask the client to remove the file.
        result = client().remove(handle);

        // Couldn't remove the file.
        if (result != API_OK)
            return result;
    }

    // Remove the file from the database.
    {
        auto guard = lockAll(mContext.mDatabase, *this);
        auto transaction = mContext.mDatabase.transaction();
        auto query = transaction.query(mQueries.mRemoveInodeByID);

        query.param(":id") = file->id();
        query.execute();

        transaction.commit();
    }

    // Mark the file as having been removed.
    file->removed(true);

    // Try and cancel any pending uploads.
    fileCache().remove(file->extension(), file->id());

    // Let the mounts know the file's been removed.
    mContext.mMountDB.each([&](Mount& mount) {
        mount.invalidateEntry(name, parent->id());
    });

    // File's been removed.
    return API_OK;
}

InodeDB::InodeDB(platform::ServiceContext& context)
  : Lockable()
  , mByBindHandle()
  , mByHandle()
  , mByID()
  , mByParentHandleAndName()
  , mCV()
  , mContext(context)
  , mDiscard(false)
  , mQueries(context.mDatabase)
{
    FUSEDebug1("Inode DB constructed");

    // For now, ensure the bind handle of all inodes is clear.
    auto transaction = mContext.mDatabase.transaction();
    auto query = transaction.query(mQueries.mClearBindHandles);

    query.execute();

    transaction.commit();
}

InodeDB::~InodeDB()
{
    FUSEDebug1("Inode DB destroyed");
}

void InodeDB::add(const FileInode& inode)
{
    // Convenience.
    auto extension = inode.extension();
    auto handle = inode.handle();
    auto id = inode.id();

    // Acquire locks.
    auto guard = lockAll(mContext.mDatabase, *this);

    // Establish transaction and query.
    auto transaction = mContext.mDatabase.transaction();
    auto query = transaction.query(mQueries.mAddInode);

    // Add the inode to the database.
    query.param(":bind_handle") = nullptr;
    query.param(":extension") = extension;
    query.param(":handle") = handle;
    query.param(":id") = id;
    query.param(":modified") = false;
    query.param(":name") = nullptr;
    query.param(":parent_handle") = nullptr;

    query.execute();

    // Persist changes.
    transaction.commit();
}

auto InodeDB::binding(const FileInode& file, const BindHandle& handle)
  -> ToInodeRawPtrMap<BindHandle>::iterator
{
    // Acquire locks.
    auto lock = lockAll(mContext.mDatabase, *this);

    // Try and associate file with handle.
    auto result = mByBindHandle.emplace(handle, const_cast<FileInode*>(&file));

    // Sanity.
    assert(result.second);

    // Persist the inode's bind handle in the database.
    auto transaction = mContext.mDatabase.transaction();
    auto query = transaction.query(mQueries.mSetBindHandleByID);

    query.param(":id") = file.id();
    query.param(":bind_handle") = handle;
    query.execute();

    transaction.commit();

    // Return iterator to caller.
    return result.first;
}

FileInodeRef InodeDB::binding(const BindHandle& handle) const
{
    // Acquire lock.
    InodeDBLock guard(*this);

    // Is an inode being bound using the specified handle?
    auto i = mByBindHandle.find(handle);

    // No inode is being bound with this handle.
    if (i == mByBindHandle.end())
        return FileInodeRef();

    // An inode is being bound with this handle.
    return i->second->file();
}

void InodeDB::bound(const FileInode& file,
                    ToInodeRawPtrMap<BindHandle>::iterator iterator)
{
    // Acquire locks.
    auto lock = lockAll(mContext.mDatabase, *this);

    // Sanity.
    assert(iterator->second == &file);

    // File is no longer being bound to a name in the cloud.
    mByBindHandle.erase(iterator);

    // Clear the inode's bind handle in the database.
    auto transaction = mContext.mDatabase.transaction();
    auto query = transaction.query(mQueries.mSetBindHandleByID);

    query.param(":id") = file.id();
    query.param(":bind_handle") = nullptr;
    query.execute();

    transaction.commit();
}

InodeCache& InodeDB::cache() const
{
    return mContext.mInodeCache;
}

void InodeDB::clear()
{
    // True when there are no inodes in memory.
    auto empty = [this]() {
        return mByID.empty();
    }; // empty

    // True when all inodes have been purged from memory.
    auto purged = false;

    FUSEDebug1("Waiting for inodes to be purged from memory");

    // Wait for the inodes to be purged from memory.
    while (!purged)
    {
        // Convenience.
        constexpr auto timeout = std::chrono::milliseconds(500);

        // Evict all inodes from the cache.
        cache().clear();

        // Acquire lock.
        InodeDBLock lock(*this);

        // Wait for the inodes to be purged from memory.
        purged = mCV.wait_for(lock, timeout, empty);
    }

    FUSEDebug1("Inodes have been purged from memory");

    // Acquire lock.
    InodeDBLock guard(*this);

    // Sanity.
    assert(mByBindHandle.empty());
    assert(mByID.empty());
    assert(mByHandle.empty());
    assert(mByParentHandleAndName.empty());
}

Client& InodeDB::client() const
{
    return mContext.client();
}

void InodeDB::discard(bool discard)
{
    InodeDBLock guard(*this);

    mDiscard = discard;
}

bool InodeDB::exists(InodeID id) const
{
    // Check if the inode's in memory.
    auto ref = get(id, true);

    // Inode exists if it hasn't been removed.
    if (ref)
        return !ref->removed();

    auto guard = lockAll(mContext.mDatabase, *this);

    // Check the database.
    auto transaction = mContext.mDatabase.transaction();
    auto query = transaction.query(mQueries.mGetHandleByID);

    query.param(":id") = id;
    query.execute();

    return !!query;
}

FileCache& InodeDB::fileCache() const
{
    return mContext.mFileCache;
}

FileExtensionDB& InodeDB::fileExtensionDB() const
{
    return mContext.mFileExtensionDB;
}

InodeRef InodeDB::get(NodeHandle handle, bool inMemoryOnly) const
{
    assert(!handle.isUndef());

    // Acquire database lock.
    auto lock = lockAll(mContext.mDatabase, *this);

    // Check if the inode's already in memory.
    auto h = mByHandle.find(handle);

    // Inode's in memory (match on current handle.)
    if (h != mByHandle.end())
        return InodeRef(h->second->accessed());

    // Inode's not in memory and we don't want to load it.
    if (inMemoryOnly)
        return InodeRef();

    // Check if the inode's in the file cache.
    if (auto ptr = get(fileCache(), handle, std::move(lock)))
        return ptr;

    // Inode's not in memory and not in the cache.
    return get(client(), handle);
}

InodeRef InodeDB::get(InodeID id, bool inMemoryOnly) const
{
    assert(id);

    // Acquire database lock.
    auto lock = lockAll(mContext.mDatabase, *this);

    // Check if the inode's already in memory.
    auto i = mByID.find(id);

    // Inode's already in memory.
    if (i != mByID.end())
        return InodeRef(i->second->accessed());

    // Inode's not in memory and we don't want to load it.
    if (inMemoryOnly)
        return InodeRef();

    // Check if the inode's in the file cache.
    if (auto ptr = get(fileCache(), id, std::move(lock)))
        return ptr;

    // A synthetic inode should've been in the file cache.
    if (id.synthetic())
        return InodeRef();

    // See if the inode exists in the cloud.
    return get(client(), NodeHandle(id));
}

template<typename Path>
auto InodeDB::lookup(const Path& path,
                     NodeHandle parent,
                     std::string* name)
  const -> typename EnableIfPath<Path, LookupResult>::type
{
    // For convenience.
    auto& fsAccess = client().fsAccess();

    // Keeps logic simple.
    std::string dummy;

    if (!name)
        name = &dummy;

    // Try and get our hands on the root node.
    auto ref = get(parent);

    // Couldn't locate the root node.
    if (!ref)
        return name->clear(), std::make_pair(ref, API_ENOENT);

    Path component;
    std::size_t index = 0;

    // Traverse tree, component by component.
    while (path.nextPathComponent(index, component))
    {
        // Is the current node a directory?
        auto directoryRef = ref->directory();

        // Current node isn't a directory.
        if (!directoryRef)
            return std::make_pair(ref, API_FUSE_ENOTDIR);

        // Extract component name.
        *name = component.toName(fsAccess);

        // Try and traverse down the tree.
        ref = directoryRef->get(*name);

        // We've found a child with this name.
        if (ref)
            continue;

        // No child exists with this name.
        auto result = API_ENOENT;

        // Indicate when the final component couldn't be found.
        if (!path.hasNextPathComponent(index))
            result = API_FUSE_ENOTFOUND;

        return std::make_pair(directoryRef, result);
    }

    // Effectively empty path, return root to caller.
    return std::make_pair(ref, API_OK);
}

void InodeDB::modified(InodeID id, bool modified)
{
    auto guard = lockAll(mContext.mDatabase, *this);
    auto transaction = mContext.mDatabase.transaction();
    auto query = transaction.query(mQueries.mSetModifiedByID);

    query.param(":id") = id;
    query.param(":modified") = modified;
    query.execute();

    transaction.commit();
}

bool InodeDB::modified(InodeID id) const
{
    auto guard = lockAll(mContext.mDatabase, *this);
    auto transaction = mContext.mDatabase.transaction();
    auto query = transaction.query(mQueries.mGetModifiedByID);

    query.param(":id") = id;
    query.execute();

    return query && query.field("modified");
}

FileInodeRefVector InodeDB::modified(NodeHandle parent) const
{
    // Convenience.
    const auto& client = this->client();

    // Stores a reference to each modified inode.
    FileInodeRefVector modified;

    // Tracks which nodes are under parent.
    NodeHandleSet related;

    // Tracks which nodes are *not* under parent.
    NodeHandleSet unrelated;

    // Makes logic simpler.
    related.emplace(parent);
    unrelated.emplace(NodeHandle());

    // Collect each modified inode that's a descendent of parent.
    for (auto& entry : this->modified())
    {
        // Keeps track of this node's ancestors;
        NodeHandleSet ancestors;

        // Convenience.
        auto handle = entry.first;
        auto id = entry.second;

        // Climb up the tree until we hit parent or the root.
        while (true)
        {
            // We've hit the root (or an unrelated node.)
            if (unrelated.count(handle))
            {
                // Add ancestors to our unrelated set.
                unrelated.insert(ancestors.begin(), ancestors.end());

                // Process the next inode.
                break;
            }

            // We've hit parent (or a related node.)
            if (related.count(handle))
            {
                // Add ancestors to our related set.
                related.insert(ancestors.begin(), ancestors.end());

                // Add the inode to our vector of modified inodes.
                if (auto ref = get(id))
                    modified.emplace_back(ref->file());

                // Process the next inode.
                break;
            }

            // Add ourselves to the ancestors map.
            ancestors.emplace(handle);

            // Climb up into our parent.
            handle = client.parentHandle(handle);
        }
    }

    // Return modified inodes to the caller.
    return modified;
}

void InodeDB::updated(NodeEventQueue& events)
{
    // Processing node events.
    if (!discard())
        return EventObserver(*this)(events);

    // Discarding node events.
    FUSEDebugF("Discarding %zu node event(s)", events.size());
}

void InodeDB::EventObserver::added(const NodeEvent& event)
{
    // Convenience.
    auto  handle = event.handle();
    auto& name = event.name();
    auto  parentHandle = event.parentHandle();

    FUSEDebugF("Node added: %s (%s) [%s]",
               name.c_str(),
               toNodeHandle(handle).c_str(),
               toNodeHandle(parentHandle).c_str());

    // Node was added because an inode was bound.
    if (auto ref = mInodeDB.binding(event.bindHandle()))
    {
        FUSEDebugF("%s (%s) [%s] is the future identity of inode %s",
                   name.c_str(),
                   toNodeHandle(handle).c_str(),
                   toNodeHandle(parentHandle).c_str(),
                   toString(ref->id()).c_str());

        // Partially complete the binding.
        ref->info(event.info());

        // Inode's no longer a candidate for removal.
        ref->removed(false);

        // FileIOContext will invalidate the bind handle as needed.
        return;
    }

    // Node replaces an in-memory inode.
    if (auto ref = mInodeDB.child(name, parentHandle, MemoryOnly))
    {
        FUSEDebugF("%s (%s) [%s] replaces warm inode %s",
                   name.c_str(),
                   toNodeHandle(handle).c_str(),
                   toNodeHandle(parentHandle).c_str(),
                   toString(ref->id()).c_str());

        // Mark the inode as removed.
        ref->removed(true);

        // Invalidate any associated directory entries.
        return mountDB().each([&](Mount& mount) {
            mount.invalidatePin(ref->id());
        });
    }

    // Does the node replace an inode that's not in memory?
    auto query = mTransaction.query(
                   queries().mGetExtensionAndInodeIDByNameAndParentHandle);

    query.param(":name") = name;
    query.param(":parent_handle") = parentHandle;
    query.execute();

    // Node replaces an inode that's not in memory.
    if (query)
    {
        // Convenience.
        auto extension = fileExtensionDB().get(query.field("extension"));
        auto id = query.field("id").inode();

        FUSEDebugF("%s (%s) [%s] replaces cold inode %s",
                   name.c_str(),
                   toNodeHandle(handle).c_str(),
                   toNodeHandle(parentHandle).c_str(),
                   toString(id).c_str());

        // Purge the inode from the database.
        query = mTransaction.query(queries().mRemoveInodeByID);

        query.param(":id") = id;
        query.execute();

        // Purge the inode's content from the cache.
        fileCache().remove(extension, id);
    }

    // Invalidate any associated directory entries.
    mountDB().each([&](Mount& mount) {
        mount.invalidateEntry(name, InodeID(parentHandle));
    });
}

Database& InodeDB::EventObserver::database() const
{
    return mInodeDB.mContext.mDatabase;
}

FileCache& InodeDB::EventObserver::fileCache() const
{
    return mInodeDB.fileCache();
}

FileExtensionDB& InodeDB::EventObserver::fileExtensionDB() const
{
    return mInodeDB.fileExtensionDB();
}

void InodeDB::EventObserver::modified(const NodeEvent& event)
{
    // Convenience.
    auto  handle = event.handle();
    auto& name = event.name();
    auto  parentHandle = event.parentHandle();

    FUSEDebugF("Node modified: %s (%s) [%s]",
               name.c_str(),
               toNodeHandle(handle).c_str(),
               toNodeHandle(parentHandle).c_str());

    // Has an inode in memory been updated in the cloud?
    auto ref = mInodeDB.get(handle, true);

    // No inode in memory has been updated in the cloud.
    if (!ref)
        return;

    // Invalidate the inode's attributes.
    mountDB().each([&](Mount& mount) {
        mount.invalidateAttributes(ref->id());
    });
}

void InodeDB::EventObserver::moved(const NodeEvent& event)
{
    // Convenience.
    auto  handle = event.handle();
    auto& name = event.name();
    auto  parentHandle = event.parentHandle();

    FUSEDebugF("Node moved: %s (%s) [%s]",
               name.c_str(),
               toNodeHandle(handle).c_str(),
               toNodeHandle(parentHandle).c_str());

    // Node replaces an in-memory inode at this location.
    if (auto ref = mInodeDB.child(name, parentHandle, MemoryOnly))
    {
        FUSEDebugF("%s (%s) [%s] replaces warm inode %s",
                   name.c_str(),
                   toNodeHandle(handle).c_str(),
                   toNodeHandle(parentHandle).c_str(),
                   toString(ref->id()).c_str());

        // Mark inode as removed.
        ref->removed(true);
    }

    // Is an inode at this location in the database?
    auto query = mTransaction.query(
                   queries().mGetExtensionAndInodeIDByNameAndParentHandle);

    query.param(":name") = name;
    query.param(":parent_handle") = parentHandle;
    query.execute();

    // Node replaces an inode that's not in memory.
    if (query)
    {
        // Convenience.
        auto extension = fileExtensionDB().get(query.field("extension"));
        auto id = query.field("id").inode();

        FUSEDebugF("%s (%s) [%s] replaces cold inode %s",
                   name.c_str(),
                   toNodeHandle(handle).c_str(),
                   toNodeHandle(parentHandle).c_str(),
                   toString(id).c_str());

        // Purge the inode from the database.
        query = mTransaction.query(queries().mRemoveInodeByID);

        query.param(":id") = id;
        query.execute();

        // Purge the inode from the cache.
        fileCache().remove(extension, id);
    }

    // Has an inode changed location?
    if (auto ref = mInodeDB.get(handle, true))
    {
        // Convenience.
        auto  id_ = ref->id();
        auto& name_ = ref->name(CachedOnly);
        auto  parentID_ = InodeID(ref->parentHandle(CachedOnly));

        FUSEDebugF("%s (%s) [%s] has moved to %s [%s]",
                   name_.c_str(),
                   toString(id_).c_str(),
                   toString(parentID_).c_str(),
                   name.c_str(),
                   toNodeHandle(parentHandle).c_str());

        // Update the inode's description.
        ref->info(event.info());

        // Invalidate associated directory entries.
        return mountDB().each([&](Mount& mount) {
            // Invalidate source.
            mount.invalidatePin(ref->id());

            // Invalidate target.
            mount.invalidateEntry(name, InodeID(parentHandle));
        });
    }

    // Invalidate any negative directory entries.
    mountDB().each([&](Mount& mount) {
        mount.invalidateEntry(name, InodeID(parentHandle));
    });
}

MountDB& InodeDB::EventObserver::mountDB() const
{
    return mInodeDB.mContext.mMountDB;
}

void InodeDB::EventObserver::permissions(const NodeEvent&)
{
    // TODO: To be improved later.
}

auto InodeDB::EventObserver::queries() const -> Queries&
{
    return mInodeDB.mQueries;
}

void InodeDB::EventObserver::removed(const NodeEvent& event)
{
    // Convenience.
    auto  handle = event.handle();
    auto& name = event.name();
    auto  parentHandle = event.parentHandle();

    FUSEDebugF("Node removed: %s (%s) [%s]",
               name.c_str(),
               toNodeHandle(handle).c_str(),
               toNodeHandle(parentHandle).c_str());

    // Disable any mounts that might be associated with this node.
    if (event.isDirectory())
        mountDB().disable(event.handle());

    // Node matches an inode that's currently in memory.
    if (auto ref = mInodeDB.get(handle, true))
    {
        // Convenience.
        auto id = ref->id();

        FUSEDebugF("%s (%s) [%s] matches warm inode %s",
                   name.c_str(),
                   toNodeHandle(handle).c_str(),
                   toNodeHandle(parentHandle).c_str(),
                   toString(id).c_str());

        // Mark inode as removed.
        ref->removed(true);

        // Invalidate any associated directory entries.
        return mountDB().each([&](Mount& mount) {
            mount.invalidateEntry(name, id, InodeID(parentHandle));
        });
    }

    auto query = mTransaction.query(queries().mGetExtensionAndInodeIDByHandle);

    query.param(":handle") = handle;
    query.execute();

    // Node matches an inode that's not in memory.
    if (query)
    {
        // Convenience.
        auto extension = fileExtensionDB().get(query.field("extension"));
        auto id = query.field("id").inode();

        FUSEDebugF("%s (%s) [%s] matches cold inode %s",
                   name.c_str(),
                   toNodeHandle(handle).c_str(),
                   toNodeHandle(parentHandle).c_str(),
                   toString(id).c_str());

        // Purge the inode from the database.
        query = mTransaction.query(queries().mRemoveInodeByID);

        query.param(":id") = id;
        query.execute();

        // Purge the inode's content from the cache.
        fileCache().remove(extension, id);
    }

    // Invalidate any associated directory entries.
    mountDB().each([&](Mount& mount) {
        mount.invalidateEntry(name, InodeID(parentHandle));
    });
}

InodeDB::EventObserver::EventObserver(InodeDB& inodeDB)
  : mInodeDB(inodeDB)
  , mDatabaseLock(database(), std::defer_lock)
  , mInodeDBLock(mInodeDB, std::defer_lock)
  , mTransaction()
{
    // Acquire necessary locks.
    std::lock(mDatabaseLock, mInodeDB);

    // Establish a transaction for future queries.
    mTransaction = database().transaction();
}

InodeDB::EventObserver::~EventObserver()
{
    mInodeDB.unlock();
    mDatabaseLock.unlock();
}

void InodeDB::EventObserver::operator()(NodeEventQueue& events)
{
    // Convenience.
    using EventHandler =
      void (EventObserver::*)(const NodeEvent&);

    using std::chrono::duration_cast;
    using std::chrono::milliseconds;
    using std::chrono::high_resolution_clock;

    // For quick dispatch.
    static EventHandler handlers[NUM_NODE_EVENT_TYPES] = {
        &EventObserver::added,
        &EventObserver::modified,
        &EventObserver::moved,
        &EventObserver::permissions,
        &EventObserver::removed
    }; // handlers

    FUSEDebugF("Processing %zu node event(s)", events.size());

    auto began = high_resolution_clock::now();

    // Process each event.
    for ( ; !events.empty(); events.pop_front())
    {
        // What's the next event in the queue?
        auto& event = events.front();

        // What is the event's type?
        auto type = event.type();

        // Sanity.
        assert(type < NUM_NODE_EVENT_TYPES);

        // Which handler should we dispatch to?
        auto handler = handlers[type];

        // Sanity.
        assert(handler);

        // Handle the event.
        (this->*handler)(event);
    }

    // Persist database changes.
    mTransaction.commit();

    // Log some helpful statistics.
    auto elapsed = high_resolution_clock::now() - began;

    FUSEDebugF("%zu node event(s) processed in %lu ms",
               events.size(),
               duration_cast<milliseconds>(elapsed).count());
}

template auto
InodeDB::lookup(const platform::PathAdapter&,
                NodeHandle,
                std::string*)
   const -> LookupResult;

template auto
InodeDB::lookup(const LocalPath&,
                NodeHandle,
                std::string*)
   const -> LookupResult;

template auto
InodeDB::lookup(const RemotePath&,
                NodeHandle,
                std::string*)
   const -> LookupResult;

} // fuse
} // mega

