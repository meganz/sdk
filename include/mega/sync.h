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

class SyncConfig
{
public:

    enum Type
    {
        TYPE_UP = 0x01, // sync up from local to remote
        TYPE_DOWN = 0x02, // sync down from remote to local
        TYPE_TWOWAY = TYPE_UP | TYPE_DOWN, // Two-way sync
        TYPE_BACKUP, // special sync up from local to remote, automatically disabled when remote changed
    };
    SyncConfig() = default;

    SyncConfig(LocalPath localPath,
        string syncName,
        NodeHandle remoteNode,
        const string& remotePath,
        const fsfp_t localFingerprint,
        const LocalPath& externalDrivePath,
        const bool enabled = true,
        const Type syncType = TYPE_TWOWAY,
        const SyncError error = NO_SYNC_ERROR,
        const SyncWarning warning = NO_SYNC_WARNING,
        handle hearBeatID = UNDEF
    );

    bool operator==(const SyncConfig &rhs) const;
    bool operator!=(const SyncConfig &rhs) const;

    // Id for the sync, also used in sync heartbeats
    handle getBackupId() const;
    void setBackupId(const handle& backupId);

    // the local path of the sync root folder
    const LocalPath& getLocalPath() const;

    // the remote path of the sync
    NodeHandle getRemoteNode() const;
    void setRemoteNode(NodeHandle remoteNode);

    // the fingerprint of the local sync root folder
    fsfp_t getLocalFingerprint() const;
    void setLocalFingerprint(fsfp_t fingerprint);

    // returns the type of the sync
    Type getType() const;

    // This is where the remote root node was, last time we checked

    // error code or warning (errors mean the sync was stopped)
    SyncError getError() const;
    void setError(SyncError value);

    //SyncWarning getWarning() const;
    //void setWarning(SyncWarning value);

    // If the sync is enabled, we will auto-start it
    bool getEnabled() const;
    void setEnabled(bool enabled);

    // Whether this is a backup sync.
    bool isBackup() const;

    // Whether this sync is backed by an external device.
    bool isExternal() const;

    // check if we need to notify the App about error/enable flag changes
    bool errorOrEnabledChanged();

    string syncErrorToStr();
    static string syncErrorToStr(SyncError errorCode);

    void setBackupState(SyncBackupState state);
    SyncBackupState getBackupState() const;

    // enabled/disabled by the user
    bool mEnabled = true;

    // the local path of the sync
    LocalPath mLocalPath;

    // name of the sync (if localpath is not adequate)
    string mName;

    // the remote handle of the sync
    NodeHandle mRemoteNode;

    // the path to the remote node, as last known (not definitive)
    string mOriginalPathOfRemoteRootNode;

    // the local fingerprint
    fsfp_t mLocalFingerprint;

    // type of the sync, defaults to bidirectional
    Type mSyncType;

    // failure cause (disable/failure cause).
    SyncError mError;

    // Warning if creation was successful but the user should know something
    SyncWarning mWarning;

    // Unique identifier. any other field can change (even remote handle),
    // and we want to keep disabled configurations saved: e.g: remote handle changed
    // id for heartbeating
    handle mBackupId;

    // Path to the volume containing this backup (only for external backups).
    // This one is not serialized
    LocalPath mExternalDrivePath;

    // Whether this backup is monitoring or mirroring.
    SyncBackupState mBackupState;

    // enum to string conversion
    static const char* syncstatename(const syncstate_t state);
    static const char* synctypename(const Type type);
    static bool synctypefromname(const string& name, Type& type);

private:
    // If mError or mEnabled have changed from these values, we need to notify the app.
    SyncError mKnownError = NO_SYNC_ERROR;
    bool mKnownEnabled = false;
};

// Convenience.
using SyncConfigVector = vector<SyncConfig>;

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

    // Update remote location
    bool updateSyncRemoteLocation(Node* n, bool forceCallback);
private:
    friend class Sync;
    friend struct Syncs;
    error startSync(MegaClient* client, const char* debris, LocalPath* localdebris, Node* remotenode, bool inshare, bool isNetwork, LocalPath& rootpath, std::unique_ptr<FileAccess>& openedLocalFolder);
    void changedConfigState(bool notifyApp);
};

using SyncCompletionFunction =
  std::function<void(UnifiedSync*, const SyncError&, error)>;


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
    bool itemProcessed = false;

    bool recurseToScanforNewLocalNodesOnly = false;

    // Sometimes when eg. creating a local a folder, we need to add to this list
    // Note that it might be the cached version or a temporary regenerated list
    vector<FSNode>* fsSiblings = nullptr;
    vector<syncRow>* rowSiblings = nullptr;
    const LocalPath& comparisonLocalname() const;
};

struct SyncPath
{
    // Tracks both local and remote absolute paths (whether they really exist or not) as we recurse the sync nodes
    LocalPath localPath;
    string cloudPath;

    // this one purely from the sync root (using cloud name, to avoid escaped names)
    string syncPath;

    // convenience, performs the conversion
    string localPath_utf8();

    bool appendRowNames(const syncRow& row, FileSystemType filesystemType);

    SyncPath(MegaClient* c, const LocalPath& fs, const string& cloud) : client(c), localPath(fs), cloudPath(cloud) {}
private:
    MegaClient* client;
};


class ScopedSyncPathRestore {
    SyncPath& path;
    size_t length1, length2, length3;
public:
    // On destruction, puts the LocalPath length back to what it was on construction of this class
    ScopedSyncPathRestore(SyncPath&);
    ~ScopedSyncPathRestore();
};


class MEGA_API Sync
{
public:

    // returns the sync config
    SyncConfig& getConfig();
    const SyncConfig& getConfig() const;

    MegaClient* client = nullptr;

    // for logging
    string syncname;

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

    vector<syncRow> computeSyncTriplets(Node* cloudNode,
        const LocalNode& root,
        vector<FSNode>& fsNodes) const;
    bool inferAlreadySyncedTriplets(Node* cloudNode,
        const LocalNode& root,
        vector<FSNode>& fsNodes,
        vector<syncRow>& inferredRows) const;

    bool recursiveSync(syncRow& row, SyncPath& fullPath, DBTableTransactionCommitter& committer);
    bool recursiveSync_localScanForNewOnly(syncRow& row, SyncPath& fullPath, DBTableTransactionCommitter& committer);
    bool syncItem_checkMoves(syncRow& row, syncRow& parentRow, SyncPath& fullPath, DBTableTransactionCommitter& committer);
    bool syncItem(syncRow& row, syncRow& parentRow, SyncPath& fullPath, DBTableTransactionCommitter& committer);
    string logTriplet(syncRow& row, SyncPath& fullPath);

    bool resolve_userIntervention(syncRow& row, syncRow& parentRow, SyncPath& fullPath);
    bool resolve_makeSyncNode_fromFS(syncRow& row, syncRow& parentRow, SyncPath& fullPath, bool considerSynced);
    bool resolve_makeSyncNode_fromCloud(syncRow& row, syncRow& parentRow, SyncPath& fullPath, bool considerSynced);
    bool resolve_delSyncNode(syncRow& row, syncRow& parentRow, SyncPath& fullPath);
    bool resolve_upsync(syncRow& row, syncRow& parentRow, SyncPath& fullPath, DBTableTransactionCommitter& committer);
    bool resolve_downsync(syncRow& row, syncRow& parentRow, SyncPath& fullPath, DBTableTransactionCommitter& committer, bool alreadyExists);
    bool resolve_pickWinner(syncRow& row, syncRow& parentRow, SyncPath& fullPath);
    bool resolve_cloudNodeGone(syncRow& row, syncRow& parentRow, SyncPath& fullPath);
    bool resolve_fsNodeGone(syncRow& row, syncRow& parentRow, SyncPath& fullPath);

    bool syncEqual(const Node&, const FSNode&);
    bool syncEqual(const Node&, const LocalNode&);
    bool syncEqual(const FSNode&, const LocalNode&);

    bool checkLocalPathForMovesRenames(syncRow& row, syncRow& parentRow, SyncPath& fullPath, bool& rowResult);
    bool checkCloudPathForMovesRenames(syncRow& row, syncRow& parentRow, SyncPath& fullPath, bool& rowResult);

    void recursiveCollectNameConflicts(syncRow& row, list<NameConflict>& nc);
    bool recursiveCollectNameConflicts(list<NameConflict>& nc);

    //// rescan sequence number (incremented when a full rescan or a new
    //// notification batch starts)
    //int scanseqno = 0;

    // debris path component relative to the base path
    string debris;
    LocalPath localdebris;
    LocalPath localdebrisname;

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
    Sync(UnifiedSync&, const char*, LocalPath*, Node*, bool);
    ~Sync();

    // Should we synchronize this sync?
    bool active() const;

    // Is this sync paused?
    bool paused() const;

    // Should we remove this sync?
    bool purgeable() const;

    // Asynchronous scan request / result.
    std::shared_ptr<ScanService::Request> mActiveScanRequest;

    static const int SCANNING_DELAY_DS;
    static const int EXTRA_SCANNING_DELAY_DS;
    static const int FILE_UPDATE_DELAY_DS;
    static const int FILE_UPDATE_MAX_DELAY_SECS;
    static const dstime RECENT_VERSION_INTERVAL_SECS;

    // Change state to (DISABLED, BACKUP_MODIFIED).
    // Always returns false.
    bool backupModified();

    // Whether this is a backup sync.
    bool isBackup() const;

    // Whether this is a backup sync and it is mirroring.
    bool isBackupAndMirroring() const;

    // Whether this is a backup sync and it is monitoring.
    bool isBackupMonitoring() const;

    // Move the sync into the monitoring state.
    void setBackupMonitoring();

    UnifiedSync& mUnifiedSync;

    // timer for whole-sync rescan in case of notifications failing or not being available
    BackoffTimer syncscanbt;

protected :
    bool readstatecache();

private:
    LocalPath mLocalPath;
};

class SyncConfigIOContext;

class SyncConfigStore {
public:
    // How we compare drive paths.
    struct DrivePathComparator
    {
        bool operator()(const LocalPath& lhs, const LocalPath& rhs) const
        {
            return platformCompareUtf(lhs, false, rhs, false) < 0;
        }
    }; // DrivePathComparator

    using DriveSet = set<LocalPath, DrivePathComparator>;

    SyncConfigStore(const LocalPath& dbPath, SyncConfigIOContext& ioContext);
    ~SyncConfigStore();

    // Remember whether we need to update the file containing configs on this drive.
    void markDriveDirty(const LocalPath& drivePath);

    // Whether any config data has changed and needs to be written to disk
    bool dirty() const;

    // Reads a database from disk.
    error read(const LocalPath& drivePath, SyncConfigVector& configs);

    // Write the configs with this drivepath to disk.
    error write(const LocalPath& drivePath, const SyncConfigVector& configs);

    // Check whether we read configs from a particular drive
    bool driveKnown(const LocalPath& drivePath) const;

    // What drives do we know about?
    vector<LocalPath> knownDrives() const;

    // Remove a known drive.
    bool removeDrive(const LocalPath& drivePath);

    // update configs on disk for any drive marked as dirty
    auto writeDirtyDrives(const SyncConfigVector& configs) -> DriveSet;

private:
    // Metadata regarding a given drive.
    struct DriveInfo
    {
        // Directory on the drive containing the database.
        LocalPath dbPath;

        // Path to the drive itself.
        LocalPath drivePath;

        // Tracks which 'slot' we're writing to.
        unsigned int slot = 0;

        bool dirty = false;
    }; // DriveInfo

    using DriveInfoMap = map<LocalPath, DriveInfo, DrivePathComparator>;

    // Checks whether two paths are equal.
    bool equal(const LocalPath& lhs, const LocalPath& rhs) const;

    // Computes a suitable DB path for a given drive.
    LocalPath dbPath(const LocalPath& drivePath) const;

    // Reads a database from the specified slot on disk.
    error read(DriveInfo& driveInfo, SyncConfigVector& configs, unsigned int slot);

    // Where we store databases for internal syncs.
    const LocalPath mInternalSyncStorePath;

    // What drives are known to the store.
    DriveInfoMap mKnownDrives;

    // IO context used to read and write from disk.
    SyncConfigIOContext& mIOContext;
}; // SyncConfigStore


class MEGA_API SyncConfigIOContext
{
public:
    SyncConfigIOContext(FileSystemAccess& fsAccess,
                            const string& authKey,
                            const string& cipherKey,
                            const string& name,
                            PrnGen& rng);

    virtual ~SyncConfigIOContext();

    MEGA_DISABLE_COPY_MOVE(SyncConfigIOContext);

    // Deserialize configs from JSON (with logging.)
    bool deserialize(const LocalPath& dbPath,
                     SyncConfigVector& configs,
                     JSON& reader,
                     unsigned int slot) const;

    bool deserialize(SyncConfigVector& configs,
                     JSON& reader) const;

    // Return a reference to this context's filesystem access.
    FileSystemAccess& fsAccess() const;

    // Determine which slots are present.
    virtual error getSlotsInOrder(const LocalPath& dbPath,
                                  vector<unsigned int>& confSlots);

    // Read data from the specified slot.
    virtual error read(const LocalPath& dbPath,
                       string& data,
                       unsigned int slot);

    // Remove an existing slot from disk.
    virtual error remove(const LocalPath& dbPath,
                         unsigned int slot);

    // Remove all existing slots from disk.
    virtual error remove(const LocalPath& dbPath);

    // Serialize configs to JSON.
    void serialize(const SyncConfigVector &configs,
                   JSONWriter& writer) const;

    // Write data to the specified slot.
    virtual error write(const LocalPath& dbPath,
                        const string& data,
                        unsigned int slot);

    // Prefix applied to configuration database names.
    static const string NAME_PREFIX;

private:
    // Generate complete database path.
    LocalPath dbFilePath(const LocalPath& dbPath,
                         unsigned int slot) const;

    // Decrypt data.
    bool decrypt(const string& in, string& out);

    // Deserialize a config from JSON.
    bool deserialize(SyncConfig& config, JSON& reader) const;

    // Encrypt data.
    string encrypt(const string& data);

    // Serialize a config to JSON.
    void serialize(const SyncConfig& config, JSONWriter& writer) const;

    // The cipher protecting the user's configuration databases.
    SymmCipher mCipher;

    // How we access the filesystem.
    FileSystemAccess& mFsAccess;

    // Name of this user's configuration databases.
    LocalPath mName;

    // Pseudo-random number generator.
    PrnGen& mRNG;

    // Hash used to authenticate configuration databases.
    HMACSHA256 mSigner;
}; // SyncConfigIOContext

struct SyncFlags
{
    // whether the target of an asynchronous scan request is reachable.
    //bool scanTargetReachable = false;

    // we can only perform moves after scanning is complete
    bool scanningWasComplete = false;

    // track whether all our reachable nodes have been scanned
    bool reachableNodesAllScannedThisPass = true;
    bool reachableNodesAllScannedLastPass = true;

    // we can only delete/upload/download after moves are complete
    bool movesWereComplete = false;

    // stall detection (for incompatible local and remote changes, eg file added locally in a folder removed remotely)
    bool noProgress = true;
    int noProgressCount = 0;

    struct CloudStallInfo { SyncWaitReason reason = SyncWaitReason::NoReason; string involvedPath; };
    struct LocalStallInfo { SyncWaitReason reason = SyncWaitReason::NoReason; LocalPath involvedPath; };
    typedef map<string, CloudStallInfo> CloudStallInfoMap;
    typedef map<LocalPath, LocalStallInfo> LocalStallInfoMap;
    CloudStallInfoMap stalledNodePaths;
    LocalStallInfoMap stalledLocalPaths;
};


struct Syncs
{
    UnifiedSync* appendNewSync(const SyncConfig&, MegaClient& mc);

    bool hasRunningSyncs();
    size_t numRunningSyncs();
    unsigned numSyncs();    // includes non-running syncs, but configured
    Sync* firstRunningSync();
    Sync* runningSyncByBackupId(handle backupId) const;
    SyncConfig* syncConfigByBackupId(handle backupId) const;

    void forEachUnifiedSync(std::function<void(UnifiedSync&)> f);
    void forEachRunningSync(std::function<void(Sync* s)>) const;
    bool forEachRunningSync_shortcircuit(std::function<bool(Sync* s)>);
    void forEachRunningSyncContainingNode(Node* node, std::function<void(Sync* s)> f);
    void forEachSyncConfig(std::function<void(const SyncConfig&)>);

    void purgeRunningSyncs();
    void stopCancelledFailedDisabled();
    void resumeResumableSyncsOnStartup();
    void enableResumeableSyncs();
    error enableSyncByBackupId(handle backupId, bool resetFingerprint, UnifiedSync*&);

    // disable all active syncs.  Cache is kept
    void disableSyncs(SyncError syncError, bool newEnabledFlag);

    // Called via MegaApi::disableSync - cache files are retained, as is the config, but the Sync is deleted
    void disableSelectedSyncs(std::function<bool(SyncConfig&, Sync*)> selector, SyncError syncError, bool newEnabledFlag);

    // Called via MegaApi::removeSync - cache files are deleted and syncs unregistered
    void removeSelectedSyncs(std::function<bool(SyncConfig&, Sync*)> selector);

    // removes the sync from RAM; the config will be flushed to disk
    void unloadSelectedSyncs(std::function<bool(SyncConfig&, Sync*)> selector);

    // removes all configured backups from cache, API (BackupCenter) and user's attribute (*!bn = backup-names)
    void purgeSyncs();

    void resetSyncConfigStore();
    void clear();

    SyncConfigVector configsForDrive(const LocalPath& drive) const;
    SyncConfigVector allConfigs() const;

    // updates in state & error
    void saveSyncConfig(const SyncConfig& config);

    Syncs(MegaClient& mc);

    // for quick lock free reference by MegaApiImpl::syncPathState (don't slow down windows explorer)
    bool isEmpty = true;

    unique_ptr<BackupMonitor> mHeartBeatMonitor;

    /**
     * @brief
     * Removes previously opened backup databases from that drive from memory.
     *
     * Note that this function will:
     * - Flush any pending database changes.
     * - Remove all contained backup configs from memory.
     * - Remove the database itself from memory.
     *
     * @param drivePath
     * The drive containing the database to remove.
     *
     * @return
     * The result of removing the backup database.
     *
     * API_EARGS
     * The path is invalid.
     *
     * API_EFAILED
     * There is an active sync on this device.
     *
     * API_EINTERNAL
     * Encountered an internal error.
     *
     * API_ENOENT
     * No such database exists in memory.
     *
     * API_EWRITE
     * The database has been removed from memory but it could not
     * be successfully flushed.
     *
     * API_OK
     * The database was removed from memory.
     */
    error backupCloseDrive(LocalPath drivePath);

    /**
     * @brief
     * Restores backups from an external drive.
     *
     * @param drivePath
     * The drive to restore external backups from.
     *
     * @return
     * The result of restoring the external backups.
     */
    error backupOpenDrive(LocalPath drivePath);

    // Returns a reference to this user's internal configuration database.
    SyncConfigStore* syncConfigStore();

    // Add a config directly to the internal sync config DB.
    //
    // Note that configs added in this way bypass the usual sync mechanism.
    // That is, they are added directly to the JSON DB on disk.
    error syncConfigStoreAdd(const SyncConfig& config);

    // Whether the internal database has changes that need to be written to disk.
    bool syncConfigStoreDirty();

    // Attempts to flush the internal configuration database to disk.
    bool syncConfigStoreFlush();

    // Load internal sync configs from disk.
    error syncConfigStoreLoad(SyncConfigVector& configs);

    string exportSyncConfigs(const SyncConfigVector configs) const;
    string exportSyncConfigs() const;

    void importSyncConfigs(const char* data, std::function<void(error)> completion);

private:
    void exportSyncConfig(JSONWriter& writer, const SyncConfig& config) const;

    bool importSyncConfig(JSON& reader, SyncConfig& config);
    bool importSyncConfigs(const char* data, SyncConfigVector& configs);

    // Returns a reference to this user's sync config IO context.
    SyncConfigIOContext* syncConfigIOContext();

    // This user's internal sync configuration store.
    unique_ptr<SyncConfigStore> mSyncConfigStore;

    // Responsible for securely writing config databases to disk.
    unique_ptr<SyncConfigIOContext> mSyncConfigIOContext;

    vector<unique_ptr<UnifiedSync>> mSyncVec;

    // remove the Sync and its config (also unregister in API). The sync's Localnode cache is removed
    void removeSyncByIndex(size_t index);

    // unload the Sync (remove from RAM and data structures), its config will be flushed to disk
    void unloadSyncByIndex(size_t index);

    MegaClient& mClient;
};

} // namespace

#endif
#endif
