#pragma once

#include <mega/common/client_forward.h>
#include <mega/common/database_forward.h>
#include <mega/common/error_or_forward.h>
#include <mega/common/lockable.h>
#include <mega/common/node_event_forward.h>
#include <mega/common/node_event_observer.h>
#include <mega/common/node_event_queue_forward.h>
#include <mega/common/node_info_forward.h>
#include <mega/common/query.h>
#include <mega/common/query_forward.h>
#include <mega/common/scoped_query_forward.h>
#include <mega/common/transaction_forward.h>
#include <mega/file_service/file_event_forward.h>
#include <mega/file_service/file_event_observer_id.h>
#include <mega/file_service/file_event_observer_result_forward.h>
#include <mega/file_service/file_info.h>
#include <mega/file_service/file_location_forward.h>
#include <mega/file_service/file_location_provider_id.h>
#include <mega/file_service/file_service_forward.h>
#include <mega/file_service/file_service_result_forward.h>
#include <mega/file_service/file_service_result_or_forward.h>
#include <mega/fuse/common/any_lock_set_forward.h>
#include <mega/fuse/common/directory_inode_forward.h>
#include <mega/fuse/common/directory_inode_results.h>
#include <mega/fuse/common/file_inode_forward.h>
#include <mega/fuse/common/inode_cache_forward.h>
#include <mega/fuse/common/inode_db_forward.h>
#include <mega/fuse/common/inode_forward.h>
#include <mega/fuse/common/inode_id_forward.h>
#include <mega/fuse/common/tags.h>
#include <mega/fuse/platform/mount_forward.h>
#include <mega/fuse/platform/service_context_forward.h>
#include <mega/types.h>

#include <atomic>
#include <condition_variable>
#include <map>
#include <mutex>
#include <string>
#include <utility>

namespace mega
{
namespace common
{

template<>
struct LockableTraits<fuse::InodeDB>:
    public LockableTraitsCommon<fuse::InodeDB, std::recursive_mutex>
{}; // LockableTraits<fuse::InodeDB>

} // common

namespace fuse
{

// Manages all inodes that are exposed to userspace.
//
// Every filesystem entity that is exposed to userspace is
// represented by an "inode."
//
// An inode can represent either a directory or a file.
//
// Every inode has a unique identifier known as its "inode ID."
// Once assigned, this identifier is never changed.
//
// The ID of a directory is the same as that directory's
// node handle in the cloud. This is reasonable as directories
// are not versioned.
//
// The ID of a file depends on whether that file existed in
// the cloud. If the file does exist in the cloud then the file's
// ID is same as that file's node handle.
//
// Files that don't exist in the cloud are assigned a unique
// identifier that is generated in some fashion.
//
// In either case, it's important to note that the ID of a file
// is not necessarily always the same as the cloud node that the
// file represents.
//
// The reason for this is that when we update a file in the cloud,
// we're not really updating that file in place. Instead, a new
// version of that file is created.
//
// When a new version of a file is uploaded, the file's node handle
// is updated but the file's ID remains unchanged.
//
// Once a file has been exposed to userspace under some ID, that file
// can continue be accessed via that ID until it has been removed.
class InodeDB final: public common::Lockable<InodeDB>, public common::NodeEventObserver
{
    // So they can remove themselves from the database.
    friend class DirectoryInode;
    friend class FileInode;
    friend class Inode;

    // Clarity.
    class EventObserver;
    class FileEventObserver;
    class NodeEventObserver;

    // So we can use an inode's name and parent handle as a key.
    using NodeHandleStringPtrPair = std::pair<NodeHandle, const std::string*>;

    // So we compare a pair's name by value rather than address.
    struct NodeHandleStringPtrPairLess
    {
        bool operator()(const NodeHandleStringPtrPair& lhs,
                        const NodeHandleStringPtrPair& rhs) const
        {
            if (lhs.first < rhs.first)
                return true;

            if (rhs.first < lhs.first)
                return false;

            // Sanity.
            assert(lhs.second);
            assert(rhs.second);

            return *lhs.second < *rhs.second;
        }
    }; // NodeHandleStringPtrPairLess

    // For convenience.
    template<typename T>
    using FromNodeHandleStringPtrPairMap =
        std::map<NodeHandleStringPtrPair, T, NodeHandleStringPtrPairLess>;

    // What queries does the InodeDB perform?
    struct Queries
    {
        Queries(common::Database& database);

        // Add an inode to the database.
        common::Query mAddInode;

        // What inodes are present under the specified node handle?
        common::Query mGetChildrenByParentHandle;

        // What inode is associated with the specified inode?
        common::Query mGetHandleByID;

        // What inode is associated with a given node handle?
        common::Query mGetInodeByHandle;

        // What inode is associated with a given ID?
        common::Query mGetInodeByID;

        // What ID is associated with the given node handle?
        common::Query mGetInodeIDByHandle;

        // Get an inode's ID based on name and parent handle.
        common::Query mGetInodeIDByNameAndParentHandle;

        // What inode are present under the specified node handle?
        common::Query mGetInodeIDByParentHandle;

        // Has a specific inode been modified?
        common::Query mGetModifiedByID;

        // What inodes have been modified?
        common::Query mGetModifiedInodes;

        // What is the next free inode ID?
        common::Query mGetNextInodeID;

        // Increment the next free inode ID.
        common::Query mIncrementNextInodeID;

        // Remove an inode specified by ID.
        common::Query mRemoveInodeByID;

        // Set an inode's handle, name and parent handle.
        common::Query mSetHandleNameParentHandleByID;

        // Specify whether an inode has been modified.
        common::Query mSetModifiedByID;

        // Set an inode's name and parent handle.
        common::Query mSetNameParentHandleByID;
    }; // Queries

    // Add a new inode to the index.
    InodeRef add(InodePtr (InodeDB::*build)(const common::NodeInfo&), const common::NodeInfo& info);

    // Add a new inode to the index based on a node's description.
    InodeRef add(const common::NodeInfo& info, const AnyLockSet& locks);

    // Add a new file to the database.
    auto addFile(const std::string& name, NodeHandle parentHandle, common::Transaction& transaction)
        -> file_service::FileServiceResultOr<file_service::FileInfo>;

    // Instantiate a new directory inode.
    InodePtr buildDirectory(const common::NodeInfo& info);

    // Instantiate a new file inode.
    InodePtr buildFile(const common::NodeInfo& info);

    // Try and retrieve a reference to a parent's child.
    InodeRef child(const DirectoryInode& parent, const std::string& name) const;

    // Try and retrieve a reference to the specified child.
    InodeRef child(const std::string& name, NodeHandle parentHandle, MemoryOnlyTag) const;

    // Called with a child has been added under some parent.
    void childAdded(const Inode& indode, const std::string& name, NodeHandle parentHandle);

    // Called when a child has been removed from some parent.
    void childRemoved(const Inode& inode, const std::string& name, NodeHandle parentHandle);

    // Retrieve a reference to a directory's children.
    InodeRefVector children(const DirectoryInode& parent) const;

    // Are we discarding node events?
    bool discard() const;

    // Load an inode from the file cache by ID.
    InodeRef get(file_service::FileService& fileService, InodeID id, const AnyLockSet& locks) const;

    // Load an inode from the file cache.
    InodeRef get(file_service::FileService& FileService,
                 const AnyLockSet& locks,
                 common::ScopedQuery query,
                 common::Transaction transaction) const;

    // Check if parent contains the named child.
    InodeID hasChild(const DirectoryInode& parent, const std::string& name) const;

    // Check if a directory contains any children.
    common::ErrorOr<bool> hasChildren(const DirectoryInode& directory) const;

    // Make a new directory below parent.
    common::ErrorOr<MakeInodeResult> makeDirectory(const platform::Mount& mount,
                                                   const std::string& name,
                                                   DirectoryInodeRef parent);

    // Make a new file below parent.
    common::ErrorOr<MakeInodeResult> makeFile(const platform::Mount& mount,
                                              const std::string& name,
                                              DirectoryInodeRef parent);

    // Retrieve a list of all the modified inodes.
    using NodeHandleInodeIDPair = std::pair<NodeHandle, InodeID>;
    using NodeHandleInodeIDPairVector = std::vector<NodeHandleInodeIDPair>;

    // What ID is associated with the specified node handle?
    InodeID map(NodeHandle handle) const;

    // Query which inodes have been modified.
    auto modified() const -> NodeHandleInodeIDPairVector;

    // Move (or rename) an inode.
    Error move(InodeRef source, const std::string& targetName, DirectoryInodeRef targetParent);

    // Move (or rename) a file.
    Error move(FileInodeRef source, const std::string& targetName, DirectoryInodeRef targetParent);

    // Called when we've received a file event from the File Service.
    auto onFileEvent(const file_service::FileEvent& event) -> file_service::FileEventObserverResult;

    // Called when the File Service wants to know where an inode is located.
    auto onFileLocation(file_service::FileID id) -> std::optional<file_service::FileLocation>;

    // Remove a directory inode from the index.
    void remove(const DirectoryInode& inode, InodeDBLock lock);

    // Remove a file inode from the index.
    void remove(const FileInode& inode, InodeDBLock lock);

    // Replace a file.
    Error replace(FileInodeRef source,
                  FileInodeRef target,
                  const std::string& targetName,
                  DirectoryInodeRef targetParent);

    // Replace an inode.
    Error replace(DirectoryInodeRef source,
                  DirectoryInodeRef target,
                  const std::string& targetName,
                  DirectoryInodeRef targetParent);

    // Unlink an inode.
    Error unlink(InodeRef inode);

    // Unlink a file.
    Error unlink(FileInodeRef file);

    // Update a file's node handle in memory.
    void updateHandleInMemory(FileInode& file, NodeHandle newHandle, NodeHandle oldHandle);

    // Update a file's node handle in the database.
    void updateHandleInDatabase(InodeID id, NodeHandle handle, common::Transaction& transaction);

    // Tracks which inode is associated with what node handle.
    mutable ToInodeRawPtrMap<NodeHandle> mByHandle;

    // Tracks which inode is associated with what ID.
    mutable ToInodePtrMap<InodeID> mByID;

    // Tracks which inode is visible under what parent with what name.
    mutable FromNodeHandleStringPtrPairMap<InodeRawPtr> mByParentHandleAndName;

    // Signalled when an inode is purged from memory.
    std::condition_variable_any mCV;

    // The context this database is associated with.
    platform::ServiceContext& mContext;

    // Whether we should discard node events.
    std::atomic<bool> mDiscard;

    // The ID of our file event observer.
    file_service::FileEventObserverID mFileEventObserverID;

    // The ID of our file location provider.
    file_service::FileLocationProviderID mFileLocationProviderID;

    // What queries do we perform?
    mutable Queries mQueries;

public:
    InodeDB(platform::ServiceContext& context);

    ~InodeDB();

    // Add a memory-only inode to the database.
    void add(const FileInode& inode);

    // Retrieve the cache associated with this database.
    InodeCache& cache() const;

    // Wait for all inodes to be cleared from memory.
    void clear();

    // Retrieve the client associated with this database.
    common::Client& client() const;

    // Called by the client when its view of the cloud is current.
    void current();

    // Retrieve the database associated with this instance.
    common::Database& database() const;

    // Discard node events.
    void discard(bool discard);

    // Check if an inode is in the database.
    bool exists(InodeID id) const;

    // Retrieve the File Service associated with this database.
    file_service::FileService& fileService() const;

    // Retrieve an inode by ID.
    InodeRef get(InodeID id, bool inMemoryOnly = false) const;

    // Locate an inode based on a path relative to some parent.
    using LookupResult = std::pair<InodeRef, Error>;

    template<typename Path>
    auto lookup(const Path& path, NodeHandle parent, std::string* name = nullptr) const ->
        typename EnableIfPath<Path, LookupResult>::type;

    // Specify whether a file has been modified.
    void modified(InodeID id, bool modified);

    // Query whether a file has been modified.
    bool modified(InodeID id) const;

    // Return a reference to all modified inodes under the specified parent.
    FileInodeRefVector modified(NodeHandle parent) const;

    // Called when nodes have been updated in the cloud.
    void updated(common::NodeEventQueue& events) override;
}; // InodeDB

} // fuse
} // mega
