#pragma once

#include <condition_variable>
#include <map>
#include <mutex>
#include <string>
#include <utility>

#include <mega/fuse/common/any_lock_set_forward.h>
#include <mega/fuse/common/bind_handle_forward.h>
#include <mega/fuse/common/client_forward.h>
#include <mega/fuse/common/database_forward.h>
#include <mega/fuse/common/directory_inode_forward.h>
#include <mega/fuse/common/directory_inode_results.h>
#include <mega/fuse/common/error_or_forward.h>
#include <mega/fuse/common/file_cache_forward.h>
#include <mega/fuse/common/file_extension_db_forward.h>
#include <mega/fuse/common/file_inode_forward.h>
#include <mega/fuse/common/inode_cache_forward.h>
#include <mega/fuse/common/inode_db_forward.h>
#include <mega/fuse/common/inode_forward.h>
#include <mega/fuse/common/inode_id_forward.h>
#include <mega/fuse/common/lockable.h>
#include <mega/fuse/common/node_event_forward.h>
#include <mega/fuse/common/node_event_observer.h>
#include <mega/fuse/common/node_event_queue_forward.h>
#include <mega/fuse/common/node_info_forward.h>
#include <mega/fuse/common/query.h>
#include <mega/fuse/common/query_forward.h>
#include <mega/fuse/common/scoped_query_forward.h>
#include <mega/fuse/common/tags.h>
#include <mega/fuse/common/transaction_forward.h>
#include <mega/fuse/platform/mount_forward.h>
#include <mega/fuse/platform/service_context_forward.h>

#include <mega/types.h>

namespace mega
{
namespace fuse
{

template<>
struct LockableTraits<InodeDB>
  : public LockableTraitsCommon<InodeDB, std::recursive_mutex>
{
}; // LockableTraits<InodeDB>

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
class InodeDB final
  : public Lockable<InodeDB>
  , public NodeEventObserver
{
    // So they can remove themselves from the database.
    friend class DirectoryInode;
    friend class FileInode;
    friend class Inode;

    // Clarity.
    class EventObserver;

    // So we can use an inode's name and parent handle as a key.
    using NodeHandleStringPtrPair =
      std::pair<NodeHandle, const std::string*>;

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
        Queries(Database& database);

        // Add an inode to the database.
        Query mAddInode;

        // Clear every inode's bind handle.
        Query mClearBindHandles;

        // What inodes are present under the specified node handle?
        Query mGetChildrenByParentHandle;
        
        // What extension and ID is associated with the given node handle?
        Query mGetExtensionAndInodeIDByHandle;

        // Get an inode's extension and ID based on a name and parent handle.
        Query mGetExtensionAndInodeIDByNameAndParentHandle;

        // What inode is associated with the specified inode?
        Query mGetHandleByID;

        // What inode is associated with a given node handle?
        Query mGetInodeByHandle;

        // What inode is associated with a given ID?
        Query mGetInodeByID;

        // What ID is associated with the given bind handle or node handle?
        Query mGetInodeIDByBindHandleOrHandle;

        // Get an inode's ID based on name and parent handle.
        Query mGetInodeIDByNameAndParentHandle;

        // What inode are present under the specified node handle?
        Query mGetInodeIDByParentHandle;

        // Has a specific inode been modified?
        Query mGetModifiedByID;

        // What inodes have been modified?
        Query mGetModifiedInodes;

        // What is the next free inode ID?
        Query mGetNextInodeID;

        // Increment the next free inode ID.
        Query mIncrementNextInodeID;

        // Remove an inode specified by ID.
        Query mRemoveInodeByID;

        // Set an inode's bind handle.
        Query mSetBindHandleByID;

        // Set an inode's bind handle, handle, name and parent handle.
        Query mSetBindHandleHandleNameParentHandleByID;

        // Specify whether an inode has been modified.
        Query mSetModifiedByID;

        // Set an inode's name and parent handle.
        Query mSetNameParentHandleByID;
    }; // Queries

    // Add a new inode to the index.
    InodeRef add(InodePtr (InodeDB::*build)(const NodeInfo&),
                 const NodeInfo& info);

    // Add a new file to the database.
    InodeID addFile(const FileExtension& extension,
                    const std::string& name,
                    NodeHandle parentHandle,
                    Transaction& transaction);

    // Instantiate a new directory inode.
    InodePtr buildDirectory(const NodeInfo& info);

    // Instantiate a new file inode.
    InodePtr buildFile(const NodeInfo& info);

    // Try and retrieve a reference to a parent's child.
    InodeRef child(const DirectoryInode& parent,
                   const std::string& name) const;

    // Try and retrieve a reference to the specified child.
    InodeRef child(const std::string& name,
                   NodeHandle parentHandle,
                   MemoryOnlyTag) const;

    // Called with a child has been added under some parent.
    void childAdded(const Inode& indode,
                    const std::string& name,
                    NodeHandle parentHandle);

    // Called when a child has been removed from some parent.
    void childRemoved(const Inode& inode,
                      const std::string& name,
                      NodeHandle parentHandle);

    // Retrieve a reference to a directory's children.
    InodeRefVector children(const DirectoryInode& parent) const;

    // Are we discarding node events?
    bool discard() const;

    // Load an inode from the client.
    InodeRef get(Client& client, NodeHandle handle) const;

    // Load an inode from the file cache by handle.
    InodeRef get(FileCache& fileCache,
                 NodeHandle handle,
                 AnyLockSet locks) const;

    // Load an inode from the file cache by ID.
    InodeRef get(FileCache& fileCache,
                 InodeID id,
                 AnyLockSet locks) const;

    // Load an inode from the file cache.
    InodeRef get(FileCache& fileCache,
                 AnyLockSet locks,
                 ScopedQuery query,
                 Transaction transaction) const;

    // Specify what cloud node is associated with the specified file.
    void handle(FileInode& file,
                NodeHandle& oldHandle,
                NodeHandle newHandle);

    // Check if parent contains the named child.
    InodeID hasChild(const DirectoryInode& parent,
                     const std::string& name) const;

    // Check if a directory contains any children.
    ErrorOr<bool> hasChildren(const DirectoryInode& directory) const;

    // Make a new directory below parent.
    ErrorOr<MakeInodeResult> makeDirectory(const platform::Mount& mount,
                                           const std::string& name,
                                           DirectoryInodeRef parent);

    // Make a new file below parent.
    ErrorOr<MakeInodeResult> makeFile(const platform::Mount& mount,
                                      const std::string& name,
                                      DirectoryInodeRef parent);

    // Retrieve a list of all the modified inodes.
    using NodeHandleInodeIDPair       = std::pair<NodeHandle, InodeID>;
    using NodeHandleInodeIDPairVector = std::vector<NodeHandleInodeIDPair>;

    auto modified() const
     -> NodeHandleInodeIDPairVector;

    // Move (or rename) an inode.
    Error move(InodeRef source,
               const std::string& targetName,
               DirectoryInodeRef targetParent);

    // Move (or rename) a file.
    Error move(FileInodeRef source,
               const std::string& targetName,
               DirectoryInodeRef targetParent);

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

    // Tracks which inode is associated with what bind handle.
    mutable ToInodeRawPtrMap<BindHandle> mByBindHandle;

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
    bool mDiscard;

    // What queries do we perform?
    mutable Queries mQueries;

public:
    InodeDB(platform::ServiceContext& context);

    ~InodeDB();

    // Add a memory-only inode to the database.
    void add(const FileInode& inode);

    // Signal that file's content is being bound to a name in the cloud.
    auto binding(const FileInode& file, const BindHandle& handle)
      -> ToInodeRawPtrMap<BindHandle>::iterator;

    // Retrieve the inode that is being bound using the specified handle.
    FileInodeRef binding(const BindHandle& handle) const;

    // Signal that file's content has been bound to a name in the cloud.
    void bound(const FileInode& file,
               ToInodeRawPtrMap<BindHandle>::iterator iterator);

    // Retrieve the cache associated with this database.
    InodeCache& cache() const;

    // Wait for all inodes to be cleared from memory.
    void clear();

    // Retrieve the client associated with this database.
    Client& client() const;

    // Called by the client when its view of the cloud is current.
    void current();

    // Discard node events.
    void discard(bool discard);

    // Check if an inode is in the database.
    bool exists(InodeID id) const;

    // Retrieve the file cache associated with this database.
    FileCache& fileCache() const;

    // Retrieve the file extension DB associated with this database.
    FileExtensionDB& fileExtensionDB() const;

    // Retrieve an inode by handle.
    InodeRef get(NodeHandle handle, bool inMemoryOnly = false) const;

    // Retrieve an inode by ID.
    InodeRef get(InodeID id, bool inMemoryOnly = false) const;

    // Locate an inode based on a path relative to some parent.
    using LookupResult = std::pair<InodeRef, Error>;

    template<typename Path>
    auto lookup(const Path& path,
                NodeHandle parent,
                std::string* name = nullptr)
      const -> typename EnableIfPath<Path, LookupResult>::type;

    // Specify whether a file has been modified.
    void modified(InodeID id, bool modified);

    // Query whether a file has been modified.
    bool modified(InodeID id) const;

    // Return a reference to all modified inodes under the specified parent.
    FileInodeRefVector modified(NodeHandle parent) const;

    // Called when nodes have been updated in the cloud.
    void updated(NodeEventQueue& events) override;
}; // InodeDB

} // fuse
} // mega

