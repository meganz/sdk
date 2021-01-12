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

#include "db.h"

#ifdef ENABLE_SYNC


namespace mega {

class HeartBeatSyncInfo;
class BackupInfoSync;
class BackupMonitor;
class MegaClient;

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


struct UnifiedSync
{
    // Reference to client
    MegaClient& mClient;

    // We always have a config
    SyncConfig mConfig;

    // If the config is good, the sync can be running
    unique_ptr<Sync> mSync;

    // High level info about this sync, sent to backup centre
    std::unique_ptr<BackupInfoSync> mBackupInfo;

    // The next detail heartbeat to send to the backup centre
    std::shared_ptr<HeartBeatSyncInfo> mNextHeartbeat;

    // ctor/dtor
    UnifiedSync(MegaClient&, const SyncConfig&);

    // Try to create and start the Sync
    error enableSync(bool resetFingerprint, bool notifyApp);

private:
    friend class Sync;
    friend struct Syncs;
    error startSync(MegaClient* client, const char* debris, LocalPath* localdebris, Node* remotenode, bool inshare, bool isNetwork, bool delayInitialScan, LocalPath& rootpath, std::unique_ptr<FileAccess>& openedLocalFolder);
    void changedConfigState(bool notifyApp);
    bool updateSyncRemoteLocation(Node* n, bool forceCallback);
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

        // Details the known children of mTarget.
        map<LocalPath, FSNode> mKnown;

        // Results of the scan.
        vector<FSNode> mResults;

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
                           LocalPath& path,
                           ScanRequest& request);

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
    SyncConfig& getConfig();
    const SyncConfig& getConfig() const;

    MegaClient* client = nullptr;

    // sync-wide directory notification provider
    std::unique_ptr<DirNotify> dirnotify;

    // root of local filesystem tree, holding the sync's root folder.  Never null except briefly in the destructor (to ensure efficient db usage)
    unique_ptr<LocalNode> localroot;
    Node* cloudRoot();

    FileSystemType mFilesystemType = FS_UNKNOWN;

    // Path used to normalize sync locaroot name when using prefix /System/Volumes/Data needed by fsevents, due to notification paths
    // are served with such prefix from macOS catalina +
#ifdef __APPLE__
    string mFsEventsPath;
#endif
    // current state
    syncstate_t state = SYNC_INITIALSCAN;

    //// are we conducting a full tree scan? (during initialization and if event notification failed)
    //bool fullscan = true;

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
    void changestate(syncstate_t, SyncError newSyncError, bool newEnableFlag, bool notifyApp);

    //// skip duplicates and self-caused
    //bool checkValidNotification(int q, Notification& notification);

    // process expired extra notifications.
    dstime procextraq();

    // process all outstanding filesystem notifications (mark sections of the sync tree to visit)
    dstime procscanq();

    //// recursively look for vanished child nodes and delete them
    //void deletemissing(LocalNode*);

    unsigned localnodes[2]{};

    // look up LocalNode relative to localroot
    LocalNode* localnodebypath(LocalNode*, const LocalPath&, LocalNode** = nullptr, LocalPath* outpath = nullptr);

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

        vector<Node*> cloudClashingNames;
        vector<FSNode*> fsClashingNames;

        bool suppressRecursion = false;

        // Sometimes when eg. creating a local a folder, we need to add to this list
        // Note that it might be the cached version or a temporary regenerated list
        vector<FSNode>* fsSiblings = nullptr;

        const LocalPath& comparisonLocalname() const;
    };

    vector<syncRow> computeSyncTriplets(Node* cloudNode,
                                        const LocalNode& root,
                                        vector<FSNode>& fsNodes) const;

    bool recursiveSync(syncRow& row, LocalPath& fullPath, DBTableTransactionCommitter& committer);
    bool syncItem(syncRow& row, syncRow& parentRow, LocalPath& fullPath, DBTableTransactionCommitter& committer);
    string logTriplet(syncRow& row, LocalPath& fullPath);

    bool resolve_userIntervention(syncRow& row, syncRow& parentRow, LocalPath& fullPath);
    bool resolve_makeSyncNode_fromFS(syncRow& row, syncRow& parentRow, LocalPath& fullPath, bool considerSynced);
    bool resolve_makeSyncNode_fromCloud(syncRow& row, syncRow& parentRow, LocalPath& fullPath, bool considerSynced);
    bool resolve_delSyncNode(syncRow& row, syncRow& parentRow, LocalPath& fullPath);
    bool resolve_upsync(syncRow& row, syncRow& parentRow, LocalPath& fullPath, DBTableTransactionCommitter& committer);
    bool resolve_downsync(syncRow& row, syncRow& parentRow, LocalPath& fullPath, DBTableTransactionCommitter& committer, bool alreadyExists);
    bool resolve_pickWinner(syncRow& row, syncRow& parentRow, LocalPath& fullPath);
    bool resolve_cloudNodeGone(syncRow& row, syncRow& parentRow, LocalPath& fullPath);
    bool resolve_fsNodeGone(syncRow& row, syncRow& parentRow, LocalPath& fullPath);

    bool syncEqual(const Node&, const FSNode&);
    bool syncEqual(const Node&, const LocalNode&);
    bool syncEqual(const FSNode&, const LocalNode&);

    bool checkLocalPathForMovesRenames(syncRow& row, syncRow& parentRow, LocalPath& fullPath, bool& rowResult);
    bool checkCloudPathForMovesRenames(syncRow& row, syncRow& parentRow, LocalPath& fullPath, bool& rowResult);

    void recursiveCollectNameConflicts(syncRow& row, list<NameConflict>& nc);

    //// rescan sequence number (incremented when a full rescan or a new
    //// notification batch starts)
    //int scanseqno = 0;

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

    // get progress for heartbeats
    m_off_t getInflightProgress();

    // original filesystem fingerprint
    fsfp_t fsfp = 0;

    // does the filesystem have stable IDs? (FAT does not)
    bool fsstableids = false;

    // true if the sync hasn't loaded cached LocalNodes yet
    //bool initializing = true;

    // true if the local synced folder is a network folder
    bool isnetwork = false;


    bool updateSyncRemoteLocation(Node* n, bool forceCallback);

    // flag to optimize destruction by skipping calls to treestate()
    bool mDestructorRunning = false;
    Sync(UnifiedSync&, const char*, LocalPath*, Node*, bool, int);
    ~Sync();

    // Should we synchronize this sync?
    bool active() const;

    // Is this sync paused?
    bool paused() const;

    // Should we remove this sync?
    bool purgeable() const;

    // Asynchronous scan request / result.
    std::shared_ptr<ScanService::Request> mScanRequest;

    static const int SCANNING_DELAY_DS;
    static const int EXTRA_SCANNING_DELAY_DS;
    static const int FILE_UPDATE_DELAY_DS;
    static const int FILE_UPDATE_MAX_DELAY_SECS;
    static const dstime RECENT_VERSION_INTERVAL_SECS;

    UnifiedSync& mUnifiedSync;

protected :
    bool readstatecache();

private:
    std::string mLocalPath;
};

struct SyncFlags
{
    // whether the target of an asynchronous scan request is reachable.
    bool scanTargetReachable = false;

    // we can only perform moves after scanning is complete
    bool scanningWasComplete = false;

    // we can only delete/upload/download after moves are complete
    bool movesWereComplete = false;

    // stall detection (for incompatible local and remote changes, eg file added locally in a folder removed remotely)
    bool noProgress = true;
    int noProgressCount = 0;
    map<string, SyncWaitReason> stalledNodePaths;
    map<LocalPath, SyncWaitReason> stalledLocalPaths;
};


struct Syncs
{
    UnifiedSync* appendNewSync(const SyncConfig&, MegaClient& mc);

    bool hasRunningSyncs();
    unsigned numRunningSyncs();
    Sync* firstRunningSync();
    Sync* runningSyncByTag(int tag) const;

    void forEachUnifiedSync(std::function<void(UnifiedSync&)> f);
    void forEachRunningSync(std::function<void(Sync* s)>) const;
    bool forEachRunningSync_shortcircuit(std::function<bool(Sync* s)>);
    void forEachRunningSyncContainingNode(Node* node, std::function<void(Sync* s)> f);
    void forEachSyncConfig(std::function<void(const SyncConfig&)>);

    void purgeRunningSyncs();
    void stopCancelledFailedDisabled();
    void resumeResumableSyncsOnStartup();
    void enableResumeableSyncs();
    error enableSyncByTag(int tag, bool resetFingerprint, UnifiedSync*&);

    // disable all active syncs.  Cache is kept
    void disableSyncs(SyncError syncError, bool newEnabledFlag);

    // Called via MegaApi::disableSync - cache files are retained, as is the config, but the Sync is deleted
    void disableSelectedSyncs(std::function<bool(SyncConfig&, Sync*)> selector, SyncError syncError, bool newEnabledFlag);

    // Called via MegaApi::removeSync - cache files are deleted
    void removeSelectedSyncs(std::function<bool(SyncConfig&, Sync*)> selector);

    void resetSyncConfigDb();
    void clear();

    // updates in state & error
    void saveSyncConfig(const SyncConfig& config);

    Syncs(MegaClient& mc);

    // for quick lock free reference by MegaApiImpl::syncPathState (don't slow down windows explorer)
    bool isEmpty = true;

    unique_ptr<BackupMonitor> mHeartBeatMonitor;

    // use this existing class for maintaining the db
    unique_ptr<SyncConfigBag> mSyncConfigDb;

private:

    vector<unique_ptr<UnifiedSync>> mSyncVec;

    // remove the Sync and its config.  The sync's Localnode cache is removed
    void removeSyncByIndex(size_t index);

    MegaClient& mClient;
};


} // namespace

#endif
#endif
