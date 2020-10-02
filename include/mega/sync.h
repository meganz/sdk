/**
 * @file mega/sync.h
 * @brief Class for synchronizing local and remote trees
 *
 * (c) 2013-2014 by Mega Limited, Auckland, New Zealand
 *
 * This file is part of the MEGA SDK - Client Access Engine.
 *
 * Applications using the MEGA API must present a valid application key
 * and comply with the the rules set forth in the Terms of Service.
 *
 * The MEGA SDK is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * @copyright Simplified (2-clause) BSD License.
 *
 * You should have received a copy of the license along with this
 * program.
 */

#ifndef MEGA_SYNC_H
#define MEGA_SYNC_H 1

#ifdef ENABLE_SYNC
#include "megaclient.h"

namespace mega {

// Searching from the back, this function compares path1 and path2 character by character and
// returns the number of consecutive character matches (excluding separators) but only including whole node names.
// It's assumed that the paths are normalized (e.g. not contain ..) and separated with the given `localseparator`.
int computeReversePathMatchScore(const LocalPath& path1, const LocalPath& path2, const FileSystemAccess&);

// Recursively iterates through the filesystem tree starting at the sync root and assigns
// fs IDs to those local nodes that match the fingerprint retrieved from disk.
bool assignFilesystemIds(Sync& sync, MegaApp& app, FileSystemAccess& fsaccess, fsid_localnode_map& fsidnodes,
                         LocalPath& localdebris);

// A collection of sync configs backed by a database table
class MEGA_API SyncConfigBag
{
public:
    SyncConfigBag(DbAccess& dbaccess, FileSystemAccess& fsaccess, PrnGen& rng, const std::string& id);

    MEGA_DISABLE_COPY_MOVE(SyncConfigBag)

    // Adds a new sync config or updates if exists already
    void insert(const SyncConfig& syncConfig);

    // Removes a sync config with a given tag
    bool removeByTag(const int tag);

    // Returns the sync config with a given tag
    const SyncConfig* get(const int tag) const;

    // Returns the first sync config found with a remote handle
    const SyncConfig* getByNodeHandle(handle nodeHandle) const;

    // Removes all sync configs
    void clear();

    // Returns all current sync configs
    std::vector<SyncConfig> all() const;

private:
    std::unique_ptr<DbTable> mTable; // table for caching the sync configs
    std::map<int, SyncConfig> mSyncConfigs; // map of tag to sync configs
};

class MEGA_API ScanService
{
public:
    // Represents an asynchronous scan request.
    class Request
    {
    public:
        virtual ~Request() = default;

        MEGA_DISABLE_COPY_MOVE(Request);

        // Whether the target of this scan is below node.
        virtual bool below(const LocalNode& node) const = 0;

        // Whether the request is complete.
        virtual bool completed() const = 0;

        // Whether this request is for the specified target.
        virtual bool matches(const LocalNode& target) const = 0;

        // Retrieves the results of the request.
        virtual std::vector<FSNode> results() = 0;

    protected:
        Request() = default;
    }; // Request

    // For convenience.
    using RequestPtr = std::shared_ptr<Request>;

    ScanService(Waiter& waiter);

    ~ScanService();

    // Issue a scan for the given target.
    RequestPtr scan(const LocalNode& target, LocalPath targetPath);
    RequestPtr scan(const LocalNode& target);

private:
    // State shared by the service and its requests.
    class Cookie
    {
    public:
        Cookie(Waiter& waiter)
          : mWaiter(waiter)
        {
        }

        MEGA_DISABLE_COPY_MOVE(Cookie);

        // Inform our waiter that an operation has completed.
        void completed()
        {
            mWaiter.notify();
        }

    private:
        // Who should be notified when an operation completes.
        Waiter& mWaiter;
    }; // Cookie

    // Concrete representation of a scan request.
    friend class Sync; // prob a tidier way to do this
    class ScanRequest
      : public Request
    {
    public:
        ScanRequest(const std::shared_ptr<Cookie>& cookie,
                    const LocalNode& target,
                    LocalPath targetPath);

        MEGA_DISABLE_COPY_MOVE(ScanRequest);

        bool below(const LocalNode& node) const
        {
            return mTarget.isBelow(node);
        }

        bool completed() const override
        {
            return mComplete;
        };

        bool matches(const LocalNode& target) const override
        {
            return &target == &mTarget;
        };

        std::vector<FSNode> results() override
        {
            return std::move(mResults);
        }

        // Cookie from the originating service.
        std::weak_ptr<Cookie> mCookie;

        // Whether the scan request is complete.
        std::atomic<bool> mComplete;

        // Debris path of the sync containing the target.
        const LocalPath mDebrisPath;

        // Whether we should follow symbolic links.
        const bool mFollowSymLinks;

        // The results of the scan.
        std::vector<FSNode> mResults;

        // Target of the scan.
        const LocalNode& mTarget;

        // Path to the target.
        const LocalPath mTargetPath;
    }; // ScanRequest

    // Convenience.
    using ScanRequestPtr = std::shared_ptr<ScanRequest>;

    // Processes scan requests.
    class Worker
    {
    public:
        Worker(size_t numThreads = 1);

        ~Worker();

        MEGA_DISABLE_COPY_MOVE(Worker);

        // Queues a scan request for processing.
        void queue(ScanRequestPtr request);

    private:
        // Thread entry point.
        void loop();

        // Learn everything we can about the specified path.
        FSNode interrogate(DirAccess& iterator,
                           const LocalPath& name,
                           LocalPath& path);

        // Processes a scan request.
        void scan(ScanRequestPtr request);

        // Filesystem access.
        std::unique_ptr<FileSystemAccess> mFsAccess;

        // Pending scan requests.
        std::deque<ScanRequestPtr> mPending;

        // Guards access to the above.
        std::mutex mPendingLock;
        std::condition_variable mPendingNotifier;

        // Worker threads.
        std::vector<std::thread> mThreads;
    }; // Worker

    // Cookie shared with requests.
    std::shared_ptr<Cookie> mCookie;

    // How many services are currently active.
    static std::atomic<size_t> mNumServices;

    // Worker shared by all services.
    static std::unique_ptr<Worker> mWorker;

    // Synchronizes access to the above.
    static std::mutex mWorkerLock;
}; // ScanService

class MEGA_API Sync
{
public:

    // returns the sync config
    const SyncConfig& getConfig() const;

    void* appData = nullptr;

    MegaClient* client = nullptr;

    // sync-wide directory notification provider
    std::unique_ptr<DirNotify> dirnotify;

    // root of local filesystem tree, holding the sync's root folder.  Never null except briefly in the destructor (to ensure efficient db usage)
    unique_ptr<LocalNode> localroot;

    FileSystemType mFilesystemType = FS_UNKNOWN;

    // Path used to normalize sync locaroot name when using prefix /System/Volumes/Data needed by fsevents, due to notification paths
    // are served with such prefix from macOS catalina +
#ifdef __APPLE__
    string mFsEventsPath;
#endif
    // current state
    syncstate_t state = SYNC_INITIALSCAN;

    // are we conducting a full tree scan? (during initialization and if event notification failed)
    bool fullscan = true;

    // syncing to an inbound share?
    bool inshare = false;

    // deletion queue
    set<uint32_t> deleteq;

    // insertion/update queue
    localnode_set insertq;

    // adds an entry to the delete queue - removes it from insertq
    void statecachedel(LocalNode*);

    // adds an entry to the insert queue - removes it from deleteq
    void statecacheadd(LocalNode*);

    // recursively add children
    void addstatecachechildren(uint32_t, idlocalnode_map*, LocalPath&, LocalNode*, int);

    // Caches all synchronized LocalNode
    void cachenodes();

    // change state, signal to application
    void changestate(syncstate_t, SyncError newSyncError = NO_SYNC_ERROR);

    //// skip duplicates and self-caused
    //bool checkValidNotification(int q, Notification& notification);

    // process all outstanding filesystem notifications (mark sections of the sync tree to visit)
    void procscanq(int);

    // recursively look for vanished child nodes and delete them
    void deletemissing(LocalNode*);

    unsigned localnodes[2]{};

    // look up LocalNode relative to localroot
    LocalNode* localnodebypath(LocalNode*, const LocalPath&, LocalNode** = nullptr, LocalPath* outpath = nullptr);

    // Assigns fs IDs to those local nodes that match the fingerprint retrieved from disk.
    // The fs IDs of unmatched nodes are invalidated.
    bool assignfsids();


    struct syncRow
    {
        syncRow(Node* node, LocalNode* syncNode, FSNode* fsNode)
          : cloudNode(node)
          , syncNode(syncNode)
          , fsNode(fsNode)
        {
        };

        Node* cloudNode;
        LocalNode* syncNode;
        FSNode* fsNode;
    };

    vector<syncRow> computeSyncTriplets(Node* cloudNode,
                                        const LocalNode& root,
                                        vector<FSNode>& fsNodes) const;

    bool recursiveSync(syncRow& row, LocalPath& fullPath);
    bool syncItem(syncRow& row, syncRow& parentRow, LocalPath& fullPath);
    string logTriplet(syncRow& row, LocalPath& fullPath);

    bool resolve_userIntervention(syncRow& row, syncRow& parentRow, LocalPath& fullPath);
    bool resolve_makeSyncNode_fromFS(syncRow& row, syncRow& parentRow, LocalPath& fullPath);
    bool resolve_makeSyncNode_fromCloud(syncRow& row, syncRow& parentRow, LocalPath& fullPath);
    bool resolve_delSyncNode(syncRow& row, syncRow& parentRow, LocalPath& fullPath);
    bool resolve_upsync(syncRow& row, syncRow& parentRow, LocalPath& fullPath);
    bool resolve_downsync(syncRow& row, syncRow& parentRow, LocalPath& fullPath, bool alreadyExists);
    bool resolve_pickWinner(syncRow& row, syncRow& parentRow, LocalPath& fullPath);
    bool resolve_cloudNodeGone(syncRow& row, syncRow& parentRow, LocalPath& fullPath);
    bool resolve_fsNodeGone(syncRow& row, syncRow& parentRow, LocalPath& fullPath);

    bool syncEqual(const Node&, const LocalNode&);
    bool syncEqual(const FSNode&, const LocalNode&);

    // scan items in specified path and add as children of the specified
    // LocalNode
    vector<FSNode> scanOne(LocalNode&, LocalPath&);

    // scan specific path
    FSNode checkpathOne(LocalPath& localPath, const LocalPath& leafname, DirAccess* iteratingDir);

    // scan specific path
    bool checkLocalPathForMovesRenames(syncRow& row, syncRow& parentRow, LocalPath& fullPath, bool& rowResult);
    bool checkCloudPathForMovesRenames(syncRow& row, syncRow& parentRow, LocalPath& fullPath, bool& rowResult);

    // own position in session sync list
    sync_list::iterator sync_it{};

    // rescan sequence number (incremented when a full rescan or a new
    // notification batch starts)
    int scanseqno = 0;

    // notified nodes originating from this sync bear this tag
    int tag = 0;

    // debris path component relative to the base path
    string debris;
    LocalPath localdebris;

    // permanent lock on the debris/tmp folder
    std::unique_ptr<FileAccess> tmpfa;

    // state cache table
    DbTable* statecachetable = nullptr;

    // move file or folder to localdebris
    bool movetolocaldebris(LocalPath& localpath);

    // original filesystem fingerprint
    fsfp_t fsfp = 0;

    // does the filesystem have stable IDs? (FAT does not)
    bool fsstableids = false;

    // Error that causes a cancellation
    SyncError errorCode = NO_SYNC_ERROR;
    Error apiErrorCode; //in case a cancellation is caused by a regular error (unused)

    // true if the sync hasn't loaded cached LocalNodes yet
    //bool initializing = true;

    // true if the local synced folder is a network folder
    bool isnetwork = false;

    // flag to optimize destruction by skipping calls to treestate()
    bool mDestructorRunning = false;
    Sync(MegaClient*, SyncConfig &, const char*, LocalPath*, Node*, bool, int, void*);
    ~Sync();

    // Asynchronous scan request / result.
    std::shared_ptr<ScanService::Request> mScanRequest;

    static const int SCANNING_DELAY_DS;
    static const int EXTRA_SCANNING_DELAY_DS;
    static const int FILE_UPDATE_DELAY_DS;
    static const int FILE_UPDATE_MAX_DELAY_SECS;
    static const dstime RECENT_VERSION_INTERVAL_SECS;

protected :
    bool readstatecache();

private:
    std::string mLocalPath;

    static Node* const NAME_CONFLICT;
};
} // namespace

#endif
#endif
