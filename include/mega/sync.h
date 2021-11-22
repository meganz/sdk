/**
 * @file mega/sync.h
 * @brief Class for synchronizing local and remote trees
 *
 * (c) 2013-2014 by Mega Limited, Auckland, New Zealand
 *
 * This file is part of the MEGA SDK - Client Access Engine.
 *
 * Applications using the MEGA API must present a valid application key
 * and comply with the rules set forth in the Terms of Service.
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

#include <future>

#include "db.h"
#include "megawaiter.h"

#ifdef ENABLE_SYNC
#include "node.h"

namespace mega {

class HeartBeatSyncInfo;
class BackupInfoSync;
class BackupMonitor;
class MegaClient;

// How should the sync engine detect filesystem changes?
enum ChangeDetectionMethod
{
    // Via filesystem event notifications.
    //
    // If the filesystem notification subsystem encounters an unrecoverable
    // error then all asssociated syncs will be failed unless the user has
    // specified a scan frequency.
    CDM_NOTIFICATIONS,
    // Via periodic rescanning.
    //
    // The user must specify a scan frequency in order to use this mode.
    CDM_PERIODIC_SCANNING,
    // Unknown change detection method.
    //
    // A possible result of importing a user-edited sync config.
    CDM_UNKNOWN
}; // ChangeDetectionMethod

ChangeDetectionMethod changeDetectionMethodFromString(const string& method);

string changeDetectionMethodToString(const ChangeDetectionMethod method);

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
    bool isInternal() const;

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

    // Current running state.  This one is not serialized, it just makes it convenient to deliver thread-safe sync state data back to client apps.
    syncstate_t mRunningState = SYNC_CANCELED;    // cancelled indicates there is no assoicated mSync

    // How should the engine detect filesystem changes?
    ChangeDetectionMethod mChangeDetectionMethod = CDM_NOTIFICATIONS;

    // How often should the engine rescan the filesystem?
    //
    // Only meaningful when a sync is effectively operating in periodic-scan mode.
    unsigned mScanIntervalSec = 0;

    // enum to string conversion
    static const char* syncstatename(const syncstate_t state);
    static const char* synctypename(const Type type);
    static bool synctypefromname(const string& name, Type& type);

    SyncError knownError() const;

    // True if this sync is operating in scan-only mode.
    bool isScanOnly() const;

private:
    // If mError or mEnabled have changed from these values, we need to notify the app.
    SyncError mKnownError = NO_SYNC_ERROR;
    bool mKnownEnabled = false;
};

// Convenience.
using SyncConfigVector = vector<SyncConfig>;
struct Syncs;

struct UnifiedSync
{
    // Reference to containing Syncs object
    Syncs& syncs;

    // We always have a config
    SyncConfig mConfig;

    // If the config is good, the sync can be running
    unique_ptr<Sync> mSync;

    // High level info about this sync, sent to backup centre
    std::unique_ptr<BackupInfoSync> mBackupInfo;

    // The next detail heartbeat to send to the backup centre
    std::shared_ptr<HeartBeatSyncInfo> mNextHeartbeat;

    // ctor/dtor
    UnifiedSync(Syncs&, const SyncConfig&);

private:
    friend class Sync;
    friend struct Syncs;
    void changedConfigState(bool notifyApp);
};

enum SyncRowType : unsigned {
    SRT_XXX,
    SRT_XXF,
    SRT_XSX,
    SRT_XSF,
    SRT_CXX,
    SRT_CXF,
    SRT_CSX,
    SRT_CSF
}; // SyncRowType

struct syncRow
{
    syncRow(CloudNode* node, LocalNode* syncNode, FSNode* fsNode)
        : cloudNode(node)
        , syncNode(syncNode)
        , fsNode(fsNode)
    {
    };

    // needs to be move constructable/assignable for sorting (note std::list of non-copyable below)
    syncRow(syncRow&&) = default;
    syncRow& operator=(syncRow&&) = default;

    CloudNode* cloudNode;
    LocalNode* syncNode;
    FSNode* fsNode;

    vector<CloudNode*> cloudClashingNames;
    vector<FSNode*> fsClashingNames;

    // True if we've recorded any clashing names.
    bool hasClashes() const;

    // True if this row exists in the cloud.
    bool hasCloudPresence() const;

    // True if this row exists on disk.
    bool hasLocalPresence() const;

    bool suppressRecursion = false;
    bool itemProcessed = false;

    bool recurseBelowRemovedCloudNode = false;
    bool recurseBelowRemovedFsNode = false;

    vector<syncRow>* rowSiblings = nullptr;
    const LocalPath& comparisonLocalname() const;

    // This list stores "synthesized" FSNodes: That is, nodes that we
    // haven't scanned yet but we know must exist.
    //
    // An example would be when we download a directory from the cloud.
    // Here, we create directory locally and push an FSNode representing it
    // to this list so that we recurse into it immediately.
    list<FSNode> fsAddedSiblings;

    void inferOrCalculateChildSyncRows(bool wasSynced, vector<syncRow>& childRows, vector<FSNode>& fsInferredChildren, vector<FSNode>& fsChildren, vector<CloudNode>& cloudChildren,
            bool belowRemovedFsNode, fsid_localnode_map& localnodeByScannedFsid);

    bool empty() { return !cloudNode && !syncNode && !fsNode && cloudClashingNames.empty() && fsClashingNames.empty(); }

    // What type of sync row is this?
    SyncRowType type() const;

    // Does our ignore file require exclusive processing?
    bool ignoreFileChanged() const;

    // Signals that our ignore file is changing and requires exclusive processing.
    void ignoreFileChanging();

    // Can our ignore file be processed alongside other rows?
    bool ignoreFileStable() const;

    // Convenience specializations.
    ExclusionState exclusionState(const CloudNode& node) const;
    ExclusionState exclusionState(const FSNode& node) const;
    ExclusionState exclusionState(const LocalPath& name, nodetype_t type) const;

    // Does this row have a reserved name?
    bool hasReservedName(const FileSystemAccess& fsAccess) const;

    // Does this row represent an ignore file?
    bool isIgnoreFile() const;

private:
    // Whether our ignore file requires exclusive processing.
    bool mIgnoreFileChanged = false;
};

struct SyncPath
{
    // Tracks both local and remote absolute paths (whether they really exist or not) as we recurse the sync nodes
    LocalPath localPath;
    string cloudPath;

    // this one purely from the sync root (using cloud name, to avoid escaped names)
    string syncPath;

    // convenience, performs the conversion
    string localPath_utf8() const;

    bool appendRowNames(const syncRow& row, FileSystemType filesystemType);

    SyncPath(Syncs& s, const LocalPath& fs, const string& cloud) : localPath(fs), cloudPath(cloud), syncs(s) {}
private:
    Syncs& syncs;
};


class ScopedSyncPathRestore {
    SyncPath& path;
    size_t length1, length2, length3;
public:
    // On destruction, puts the LocalPath length back to what it was on construction of this class
    ScopedSyncPathRestore(SyncPath&);
    ~ScopedSyncPathRestore();
};


class SyncThreadsafeState
{
    // This class contains things that are read/written from either the Syncs thread,
    // or the MegaClient thread.  A mutex is used to keep the data consistent.
    // Referred to by shared_ptr so transfers etc don't have awkward lifetime issues.
    mutex mMutex;

    // If we make a LocalNode for an upload we created ourselves,
    // it's because the local file is no longer at the matching position
    // and we need to set it up to be moved correspondingly
    map<string, weak_ptr<SyncUpload_inClient>> mExpectedUploads;

    // track uploads/downloads
    struct TransferCounts
    {
        int32_t queued = 0;
        int32_t completed = 0;
        m_off_t queuedBytes = 0;
        m_off_t completedBytes = 0;
    };
    TransferCounts mUploads;
    TransferCounts mDownloads;

public:

    // Remember which Nodes we created from upload,
    // until the corresponding LocalNodes are updated.
    void addExpectedUpload(NodeHandle parentHandle, const string& name, weak_ptr<SyncUpload_inClient>);
    void removeExpectedUpload(NodeHandle parentHandle, const string& name);
    shared_ptr<SyncUpload_inClient> isNodeAnExpectedUpload(NodeHandle parentHandle, const string& name);

    // Transfers update these from the client thread
    void adjustTransferCounts(bool upload, int32_t adjustQueued, int32_t adjustCompleted, m_off_t adjustQueuedBytes, m_off_t adjustCompletedBytes);

};




class MEGA_API Sync
{
public:

    // returns the sync config
    SyncConfig& getConfig();
    const SyncConfig& getConfig() const;

    Syncs& syncs;

    // for logging
    string syncname;

    // are we calling recursiveSync for this one?
    bool syncPaused = false;

    // sync-wide directory notification provider
    std::unique_ptr<DirNotify> dirnotify;

    // track how recent the last received fs noticiation was
    dstime lastFSNotificationTime = 0;

    // root of local filesystem tree, holding the sync's root folder.  Never null except briefly in the destructor (to ensure efficient db usage)
    unique_ptr<LocalNode> localroot;

    // Cloud node to sync to
    CloudNode cloudRoot;
    string cloudRootPath;

    FileSystemType mFilesystemType = FS_UNKNOWN;

    // current state
    syncstate_t& state() { return getConfig().mRunningState; }

    // syncing to an inbound share?
    bool inshare = false;

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

    // Retrieve the name of this sync's state cache.
    //
    // Returns an empty string if this sync has no state cache.
    const string& statecachename() const;

    // change state, signal to application
    void changestate(syncstate_t, SyncError newSyncError, bool newEnableFlag, bool notifyApp);

    //// skip duplicates and self-caused
    //bool checkValidNotification(int q, Notification& notification);

    // process expired extra notifications.
    dstime procextraq();

    // process all outstanding filesystem notifications (mark sections of the sync tree to visit)
    dstime procscanq();

    // helper for checking moves etc
    bool checkIfFileIsChanging(FSNode& fsNode, const LocalPath& fullPath);

    unsigned localnodes[2]{};

    // look up LocalNode relative to localroot
    LocalNode* localnodebypath(LocalNode*, const LocalPath&, LocalNode** = nullptr, LocalPath* outpath = nullptr);

    void combineTripletSet(vector<syncRow>::iterator a, vector<syncRow>::iterator b) const;

    vector<syncRow> computeSyncTriplets(
        vector<CloudNode>& cloudNodes,
        const LocalNode& root,
        vector<FSNode>& fsNodes) const;
    bool inferRegeneratableTriplets(
        vector<CloudNode>& cloudNodes,
        const LocalNode& root,
        vector<FSNode>& fsNodes,
        vector<syncRow>& inferredRows) const;

    bool recursiveSync(syncRow& row, SyncPath& fullPath, bool belowRemovedCloudNode, bool belowRemovedFsNode, unsigned depth);
    bool syncItem_checkMoves(syncRow& row, syncRow& parentRow, SyncPath& fullPath, bool belowRemovedCloudNode, bool belowRemovedFsNode);
    bool syncItem(syncRow& row, syncRow& parentRow, SyncPath& fullPath);

    string logTriplet(syncRow& row, SyncPath& fullPath);

    bool resolve_checkMoveComplete(syncRow& row, syncRow& parentRow, SyncPath& fullPath);
    bool resolve_rowMatched(syncRow& row, syncRow& parentRow, SyncPath& fullPath);
    bool resolve_userIntervention(syncRow& row, syncRow& parentRow, SyncPath& fullPath);
    bool resolve_makeSyncNode_fromFS(syncRow& row, syncRow& parentRow, SyncPath& fullPath, bool considerSynced);
    bool resolve_makeSyncNode_fromCloud(syncRow& row, syncRow& parentRow, SyncPath& fullPath, bool considerSynced);
    bool resolve_delSyncNode(syncRow& row, syncRow& parentRow, SyncPath& fullPath, unsigned deleteCounter);
    bool resolve_upsync(syncRow& row, syncRow& parentRow, SyncPath& fullPath);
    bool resolve_downsync(syncRow& row, syncRow& parentRow, SyncPath& fullPath, bool alreadyExists);
    bool resolve_pickWinner(syncRow& row, syncRow& parentRow, SyncPath& fullPath);
    bool resolve_cloudNodeGone(syncRow& row, syncRow& parentRow, SyncPath& fullPath);
    bool resolve_fsNodeGone(syncRow& row, syncRow& parentRow, SyncPath& fullPath);

    bool syncEqual(const CloudNode&, const FSNode&);
    bool syncEqual(const CloudNode&, const LocalNode&);
    bool syncEqual(const FSNode&, const LocalNode&);

    bool checkLocalPathForMovesRenames(syncRow& row, syncRow& parentRow, SyncPath& fullPath, bool& rowResult, bool belowRemovedCloudNode);
    bool checkCloudPathForMovesRenames(syncRow& row, syncRow& parentRow, SyncPath& fullPath, bool& rowResult, bool belowRemovedFsNode);

    void recursiveCollectNameConflicts(syncRow& row, list<NameConflict>& nc, SyncPath& fullPath);
    bool recursiveCollectNameConflicts(list<NameConflict>& nc);

    // debris path component relative to the base path
    string debris;
    LocalPath localdebris;
    LocalPath localdebrisname;

    // state cache table
    unique_ptr<DbTable> statecachetable;

    // move file or folder to localdebris
    bool movetolocaldebris(const LocalPath& localpath);
    bool movetolocaldebrisSubfolder(const LocalPath& localpath, const LocalPath& targetFolder, bool logFailReason, bool& failedDueToTargetExists);

private:
    string mLastDailyDateTimeDebrisName;
    unsigned mLastDailyDateTimeDebrisCounter = 0;

public:
    // Moves a file from source to target.
    bool moveTo(LocalPath source, LocalPath target, bool overwrite);

    // original filesystem fingerprint
    fsfp_t fsfp = 0;

    // does the filesystem have stable IDs? (FAT does not)
    bool fsstableids = false;

    // true if the local synced folder is a network folder
    bool isnetwork = false;

    // flag to optimize destruction by skipping calls to treestate()
    bool mDestructorRunning = false;

    Sync(UnifiedSync&, const string&, const LocalPath&, NodeHandle rootNodeHandle, const string& rootNodeName, bool, const string& logname);
    ~Sync();

    // Should we synchronize this sync?
    bool active() const;

    // pause synchronization.  Syncs are still "active" but we don't call recursiveSync for them.
    void setSyncPaused(bool pause);
    bool isSyncPaused();

    // Asynchronous scan request / result.
    std::shared_ptr<ScanService::ScanRequest> mActiveScanRequest;

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

    // permanent lock on the debris/tmp folder
    void createDebrisTmpLockOnce();

    // permanent lock on the debris/tmp folder
    unique_ptr<FileAccess> tmpfa;
    LocalPath tmpfaPath;

    // Name of this sync's state cache.
    string mStateCacheName;

    shared_ptr<SyncThreadsafeState> threadSafeState;
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
    error read(const LocalPath& drivePath, SyncConfigVector& configs, bool isExternal);

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
    error read(DriveInfo& driveInfo, SyncConfigVector& configs, unsigned int slot, bool isExternal);

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

    MEGA_DISABLE_COPY_MOVE(SyncConfigIOContext)

    // Deserialize configs from JSON (with logging.)
    bool deserialize(const LocalPath& dbPath,
                     SyncConfigVector& configs,
                     JSON& reader,
                     unsigned int slot,
                     bool isExternal) const;

    bool deserialize(SyncConfigVector& configs,
                     JSON& reader,
                     bool isExternal) const;

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
    bool deserialize(SyncConfig& config, JSON& reader, bool isExternal) const;

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

/**
 * A Synchronization operation detected a problem and is
 * not able to continue (a stall)
 */
struct SyncStallEntry {
    SyncWaitReason reason = SyncWaitReason::NoReason;
    string involvedCloudPath;    ///<! remote path representation
    LocalPath involvedLocalPath;
};

struct SyncStallInfo
{
    using CloudStallInfoMap = map<string, SyncStallEntry>;
    using LocalStallInfoMap = map<LocalPath, SyncStallEntry>;

    /** No stalls detected */
    bool empty() const;

    bool waitingCloud(const string& cloudPath1,
                      const string& cloudPath2,
                      const LocalPath& localPath,
                      SyncWaitReason reason);

    bool waitingLocal(const LocalPath& localPath1,
                      const LocalPath& localPath2,
                      const string& cloudPath,
                      SyncWaitReason reason);

    CloudStallInfoMap cloud;
    LocalStallInfoMap local;

    /** Requires user action to resolve */
    bool hasImmediateStallReason() const;
};

struct SyncProblems
{
    list<NameConflict> mConflicts;
    SyncStallInfo mStalls;
    bool mConflictsDetected = false;
    bool mStallsDetected = false;
}; // SyncProblems

struct SyncFlags
{
    // we can only perform moves after scanning is complete
    bool scanningWasComplete = false;

    // track whether all our reachable nodes have been scanned
    bool reachableNodesAllScannedThisPass = true;
    bool reachableNodesAllScannedLastPass = true;

    // true anytime we have just added a new sync, or unpaused one
    bool isInitialPass = true;

    // we can only delete/upload/download after moves are complete
    bool movesWereComplete = false;

    // stall detection (for incompatible local and remote changes, eg file added locally in a folder removed remotely)
    bool noProgress = true;
    int noProgressCount = 0;

    bool earlyRecurseExitRequested = false;

    // to help with slowing down retries in stall state
    dstime recursiveSyncLastCompletedDs = 0;

    SyncStallInfo stall;
};


struct Syncs
{
    void appendNewSync(const SyncConfig&, bool startSync, bool notifyApp, std::function<void(error, SyncError, handle)> completion, bool completionInClient, const string& logname);


    // only for use in tests; not really thread safe
    Sync* runningSyncByBackupIdForTests(handle backupId) const;

    // Pause/unpause a sync. Returns a future for async operation.
    std::future<bool> setSyncPausedByBackupId(handle id, bool pause);

    void transferPauseFlagsUpdated(bool downloadsPaused, bool uploadsPaused);

    // returns a copy of the config, for thread safety
    bool syncConfigByBackupId(handle backupId, SyncConfig&) const;

    // This function is deprecated; very few still using it, eventually we should remove it
    void forEachUnifiedSync(std::function<void(UnifiedSync&)> f);

    // This function is deprecated; very few still using it, eventually we should remove it
    void forEachRunningSync(bool includePaused, std::function<void(Sync* s)>) const;

    // Temporary; Only to be used from MegaApiImpl::syncPathState.
    bool forEachRunningSync_shortcircuit(bool includePaused, std::function<bool(Sync* s)>);

    vector<NodeHandle> getSyncRootHandles(bool mustBeActive);

    void purgeRunningSyncs();
    void resumeResumableSyncsOnStartup(bool resetSyncConfigStore, std::function<void(error)>&& completion);

    void enableSyncByBackupId(handle backupId, bool resetFingerprint, bool notifyApp, std::function<void(error)> completion, const string& logname);

    // disable all active syncs.  Cache is kept
    void disableSyncs(SyncError syncError, bool newEnabledFlag);

    // Called via MegaApi::disableSync - cache files are retained, as is the config, but the Sync is deleted
    void disableSelectedSyncs(std::function<bool(SyncConfig&, Sync*)> selector, bool disableIsFail, SyncError syncError, bool newEnabledFlag, std::function<void(size_t)> completion);

    // Called via MegaApi::removeSync - cache files are deleted and syncs unregistered.  Synchronous (for now)
    void removeSelectedSyncs(std::function<bool(SyncConfig&, Sync*)> selector,
	     bool removeSyncDb, bool notifyApp, bool unregisterHeartbeat);

    // removes the sync from RAM; the config will be flushed to disk
    void unloadSelectedSyncs(std::function<bool(SyncConfig&, Sync*)> selector);

    // async, callback on client thread
    void renameSync(handle backupId, const string& newname, std::function<void(Error e)> result);

    void locallogout(bool removecaches, bool keepSyncsConfigFile);

    SyncConfigVector configsForDrive(const LocalPath& drive) const;
    SyncConfigVector allConfigs() const;

    // updates in state & error
    void saveSyncConfig(const SyncConfig& config);

    // synchronous for now as that's a constraint from the intermediate layer
    NodeHandle getSyncedNodeForLocalPath(const LocalPath&);

    Syncs(MegaClient& mc, unique_ptr<FileSystemAccess> notification_fsa);
    ~Syncs();

    void getSyncProblems(std::function<void(SyncProblems&)> completion,
                         bool completionInClient,
                         bool detailed);

    void getSyncProblems_inThread(SyncProblems& problems, bool detailed);

    /**
     * @brief route the request to the sync thread for fulfillment.
     */
    void getSyncStalls(std::function<void(SyncStallInfo& syncStallInfo)> completionClosure, 
            bool completionInClient);

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


    // Add a config directly to the internal sync config DB.
    //
    // Note that configs added in this way bypass the usual sync mechanism.
    // That is, they are added directly to the JSON DB on disk.
    error syncConfigStoreAdd(const SyncConfig& config);

private:  // anything to do with loading/saving/storing configs etc is done on the sync thread

    // Returns a reference to this user's internal configuration database.
    SyncConfigStore* syncConfigStore();

    // Whether the internal database has changes that need to be written to disk.
    bool syncConfigStoreDirty();

    // Attempts to flush the internal configuration database to disk.
    bool syncConfigStoreFlush();

    // Load internal sync configs from disk.
    error syncConfigStoreLoad(SyncConfigVector& configs);

public:

    string exportSyncConfigs(const SyncConfigVector configs) const;
    string exportSyncConfigs() const;

    void importSyncConfigs(const char* data, std::function<void(error)> completion);

    // maps local fsid to corresponding LocalNode* (s)
    fsid_localnode_map localnodeBySyncedFsid;
    fsid_localnode_map localnodeByScannedFsid;
    LocalNode* findLocalNodeBySyncedFsid(mega::handle fsid, nodetype_t type, const FileFingerprint& fp, Sync* filesystemSync, std::function<bool(LocalNode* ln)> extraCheck);
    LocalNode* findLocalNodeByScannedFsid(mega::handle fsid, nodetype_t type, const FileFingerprint* fp, Sync* filesystemSync, std::function<bool(LocalNode* ln)> extraCheck);

    void setSyncedFsidReused(mega::handle fsid);
    void setScannedFsidReused(mega::handle fsid);

    // maps nodehandle to corresponding LocalNode* (s)
    nodehandle_localnode_map localnodeByNodeHandle;
    LocalNode* findLocalNodeByNodeHandle(NodeHandle h);

    // manage syncdown flags inside the syncs
    void setSyncsNeedFullSync(bool andFullScan, handle backupId = UNDEF);

    // retrieves information about any detected name conflicts.
    bool conflictsDetected(list<NameConflict>& conflicts) const;

    bool syncStallDetected(SyncStallInfo& si) const;

    // Get name conficts - pass UNDEF to collect for all syncs.
    void collectSyncNameConflicts(handle backupId, std::function<void(list<NameConflict>&& nc)>, bool completionInClient);

    list<weak_ptr<LocalNode::RareFields::ScanBlocked>> scanBlockedPaths;
    list<weak_ptr<LocalNode::RareFields::BadlyFormedIgnore>> badlyFormedIgnoreFilePaths;

    typedef std::function<void(MegaClient&, DBTableTransactionCommitter&)> QueuedClientFunc;
    ThreadSafeDeque<QueuedClientFunc> clientThreadActions;
    ThreadSafeDeque<std::function<void()>> syncThreadActions;

    void syncRun(std::function<void()>);
    void queueSync(std::function<void()>&&);
    void queueClient(QueuedClientFunc&&);

    // Update remote location
    bool updateSyncRemoteLocation(UnifiedSync&, bool exists, string cloudPath);
    
    // Trigger a full scan on the specified sync.
    //
    // If backupID is UNDEF, trigger a scan on all active periodic syncs.
    std::future<size_t> triggerFullScan(handle backupID);

    // mark nodes as needing to be checked for sync actions
    void triggerSync(NodeHandle, bool recurse = false);

    // Move a file into the nearest suitable local debris.
    std::future<bool> moveToLocalDebris(LocalPath path);

    // ------ public data members (thread safe)

    // waiter for sync loop on thread
    WAIT_CLASS waiter;

    // These rules are used to generate ignore files for newly added syncs.
    DefaultFilterChain mDefaultFilterChain;

    // todo: move relevant code to this class later
    // this mutex protects the LocalNode trees while MEGAsync receives requests from the filesystem browser for icon indicators
    std::timed_mutex mLocalNodeChangeMutex;  // needs to be locked when making changes on this thread; or when accessing from another thread

    // flags matching the state we have reported to the app via callbacks
    bool syncscanstate = false;
    bool syncBusyState = false;
    bool syncStallState = false;
    bool syncConflictState = false;

    // for quick lock free reference by MegaApiImpl::syncPathState (don't slow down windows explorer)
    bool isEmpty = true;

    // directly accessed flag that makes sync-related logging a lot more detailed
    bool mDetailedSyncLogging = false;

    // total number of LocalNode objects (only updated by syncs thread)
    long long totalLocalNodes = 0;

private:

    // functions for internal use on-thread only
    void stopCancelledFailedDisabled();

    void processTriggerHandles();

    void exportSyncConfig(JSONWriter& writer, const SyncConfig& config) const;

    bool importSyncConfig(JSON& reader, SyncConfig& config);
    bool importSyncConfigs(const char* data, SyncConfigVector& configs);

    // Returns a reference to this user's sync config IO context.
    SyncConfigIOContext* syncConfigIOContext();


    // remove the Sync and its config from memory - optionally also other aspects
    void removeSyncByIndex(size_t index, bool removeSyncDb, bool notifyApp, bool unresg);

    // unload the Sync (remove from RAM and data structures), its config will be flushed to disk
    void unloadSyncByIndex(size_t index);

    void proclocaltree(LocalNode* n, LocalTreeProc* tp);

    bool mightAnySyncsHaveMoves(bool includePausedSyncs);
    bool isAnySyncSyncing(bool includePausedSyncs);
    bool isAnySyncScanning(bool includePausedSyncs);

    bool conflictsFlagged() const;


    // actually start the sync (on sync thread)
    void startSync_inThread(UnifiedSync& us, const string& debris, const LocalPath& localdebris,
        NodeHandle rootNodeHandle, const string& rootNodeName, bool inshare, bool isNetwork, const LocalPath& rootpath,
        std::function<void(error, SyncError, handle)> completion, const string& logname);
    void disableSelectedSyncs_inThread(std::function<bool(SyncConfig&, Sync*)> selector, bool disableIsFail, SyncError syncError, bool newEnabledFlag, std::function<void(size_t)> completion);
    void locallogout_inThread(bool removecaches, bool keepSyncsConfigFile);
    void resumeResumableSyncsOnStartup_inThread(bool resetSyncConfigStore, std::function<void(error)>);
    void enableSyncByBackupId_inThread(handle backupId, bool resetFingerprint, bool notifyApp, std::function<void(error, SyncError, handle)> completion, const string& logname);
    void appendNewSync_inThread(const SyncConfig&, bool startSync, bool notifyApp, std::function<void(error, SyncError, handle)> completion, const string& logname);
    void syncConfigStoreAdd_inThread(const SyncConfig& config, std::function<void(error)> completion);
    void clear_inThread();
    void removeSelectedSyncs_inThread(std::function<bool(SyncConfig&, Sync*)> selector,
	     bool removeSyncDb, bool notifyApp, bool unregisterHeartbeat);
    void purgeRunningSyncs_inThread();
    void renameSync_inThread(handle backupId, const string& newname, std::function<void(Error e)> result);

    void syncLoop();

    bool onSyncThread() const { return std::this_thread::get_id() == syncThreadId; }

    enum WhichCloudVersion { EXACT_VERSION, LATEST_VERSION, FOLDER_ONLY };
    bool lookupCloudNode(NodeHandle h, CloudNode& cn, string* cloudPath, bool* isInTrash,
            bool* nodeIsInActiveSync, bool* nodeIsDefinitelyExcluded, WhichCloudVersion);

    bool lookupCloudChildren(NodeHandle h, vector<CloudNode>& cloudChildren);

    // Query whether a specified child is definitely excluded.
    //
    // A node is definitely excluded if:
    // - The node itself is excluded.
    // - One of the node's parents are definitely excluded.
    //
    // It is the caller's responsibility to ensure that:
    // - Necessary locks are acquired such that this function has exclusive
    //   access to both the local and remote node trees.
    // - The node specified by child is below root.
    bool isDefinitelyExcluded(const pair<Node*, Sync*>& root, const Node* child);

    template<typename Predicate>
    Sync* syncMatching(Predicate&& predicate, bool includePaused)
    {
        // Sanity.
        assert(onSyncThread());

        lock_guard<mutex> guard(mSyncVecMutex);

        for (auto& i : mSyncVec)
        {
            // Skip inactive syncs.
            if (!i->mSync)
                continue;

            // Optionally skip paused syncs.
            if (!includePaused && i->mSync->syncPaused)
                continue;

            // Have we found our lucky sync?
            if (predicate(*i))
                return i->mSync.get();
        }

        // No syncs match our search criteria.
        return nullptr;
    }

    Sync* syncContainingPath(const LocalPath& path, bool includePaused);
    Sync* syncContainingPath(const string& path, bool includePaused);

    // Signal that an ignore file failed to load.
    void ignoreFileLoadFailure(const Sync& sync, const LocalPath& path);

    // Records which ignore file failed to load and under which sync.
    struct IgnoreFileFailureContext
    {
        // Clear the context if the associated sync:
        // - Is disabled (or failed.)
        // - Is paused.
        // - No longer exists.
        void reset(Syncs& syncs)
        {
            if (mBackupID == UNDEF)
                return;

            auto predicate = [&](const UnifiedSync& us) {
                return us.mConfig.mBackupId == mBackupID
                       && us.mConfig.mRunningState >= SYNC_INITIALSCAN;
            };

            if (syncs.syncMatching(std::move(predicate), false))
                return;

            reset();
        }

        // Clear the context.
        void reset()
        {
            mBackupID = UNDEF;
            mFilterChain.clear();
            mPath.clear();
        }

        // Report the load failure as a stall.
        void report(SyncStallInfo& stallInfo)
        {
            stallInfo.waitingLocal(mPath,
                                   LocalPath(),
                                   string(),
                                   SyncWaitReason::UnableToLoadIgnoreFile);
        }

        // Has the ignore file failure been resolved?
        bool resolve(FileSystemAccess& fsAccess)
        {
            // No failures to resolve so we're all good.
            if (mBackupID == UNDEF)
                return true;

            // Try and load the ignore file.
            auto result = mFilterChain.load(fsAccess, mPath);

            // Resolved if the file's been deleted or corrected.
            if (result == FLR_FAILED || result == FLR_SKIPPED)
                return false;

            // Clear the failure condition.
            reset();

            // Let the caller know the situation's resolved.
            return true;
        }

        // Has an ignore file failure been signalled?
        bool signalled() const
        {
            return mBackupID != UNDEF;
        }

        // Used to load the ignore file specified below.
        FilterChain mFilterChain;

        // What ignore file failed to load?
        LocalPath mPath;

        // What sync contained the broken ignore file?
        handle mBackupID = UNDEF;
    }; // IgnoreFileFailureContext

       // Check if the sync described by config contains an ignore file.
    bool hasIgnoreFile(const SyncConfig& config);

    // ------ private data members

    MegaClient& mClient;

    // Syncs should have a separate fsaccess for thread safety
    unique_ptr<FileSystemAccess> fsaccess;

    // Track some state during and between recursiveSync runs
    unique_ptr<SyncFlags> mSyncFlags;

    // This user's internal sync configuration store.
    unique_ptr<SyncConfigStore> mSyncConfigStore;

    // Responsible for securely writing config databases to disk.
    unique_ptr<SyncConfigIOContext> mSyncConfigIOContext;

    // Sometimes the Client needs a list of the sync configs, we provide it by copy (mutex for thread safety of course)
    mutable mutex mSyncVecMutex;
    vector<shared_ptr<UnifiedSync>> mSyncVec;

    // used to asynchronously perform scans.
    unique_ptr<ScanService> mScanService;

    // pseudo-random number generator
    PrnGen rng;

    // Separate key to avoid threading issues
    SymmCipher syncKey;

    // data structure with mutex to interchange stall info
    SyncStallInfo stallReport;
    mutable mutex stallReportMutex;

    // When the node tree changes, this structure lets the sync code know which LocalNodes need to be flagged
    map<NodeHandle, bool> triggerHandles;
    mutex triggerMutex;

    // Keep track of files that we can't move yet because they are changing
    struct FileChangingState
    {
        // values related to possible files being updated
        m_off_t updatedfilesize = ~0;
        m_time_t updatedfilets = 0;
        m_time_t updatedfileinitialts = 0;
    };
    std::map<LocalPath, FileChangingState> mFileChangingCheckState;

    // shutdown safety
    bool mExecutingLocallogout = false;

    // local record of client's state for thread safety
    bool mDownloadsPaused = false;
    bool mUploadsPaused = false;

    // Responsible for tracking when to send sync/backup heartbeats
    unique_ptr<BackupMonitor> mHeartBeatMonitor;

    // Tracks the last recorded ignore file failure.
    IgnoreFileFailureContext mIgnoreFileFailureContext;

    // for clarifying and confirming which functions run on which threads
    std::thread::id syncThreadId;

    // declared last; would be auto-destructed first.
    std::thread syncThread;


    // structs and classes that are private to the thread, and need access to some internals that should not be generally public
    friend struct LocalNode;
    friend class Sync;
    friend struct SyncPath;
    friend struct UnifiedSync;
    friend class BackupInfoSync;
    friend class BackupMonitor;
    friend struct ProgressingMonitor;
};

} // namespace

#endif
#endif
