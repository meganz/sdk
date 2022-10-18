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

#include "db.h"

#ifdef ENABLE_SYNC


namespace mega {

class HeartBeatSyncInfo;
class BackupInfoSync;
class BackupMonitor;
class MegaClient;

// Searching from the back, this function compares path1 and path2 character by character and
// returns the number of consecutive character matches (excluding separators) but only including whole node names.
// It's assumed that the paths are normalized (e.g. not contain ..) and separated with `LocalPath::localPathSeparator`.
int computeReversePathMatchScore(const LocalPath& path1, const LocalPath& path2, const FileSystemAccess&);

// Recursively iterates through the filesystem tree starting at the sync root and assigns
// fs IDs to those local nodes that match the fingerprint retrieved from disk.
bool assignFilesystemIds(Sync& sync, MegaApp& app, FileSystemAccess& fsaccess, handlelocalnode_map& fsidnodes,
                         LocalPath& localdebris);

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

    // the local path of the sync root folder
    const LocalPath& getLocalPath() const;

    // returns the type of the sync
    Type getType() const;

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

    // uniquely identifies the filesystem, we check this is unchanged.
    fsfp_t mFilesystemFingerprint;

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

    // If the database exists then its running/paused/suspended.  Not serialized.
    bool mDatabaseExists = false;

    // Name of this sync's state cache.
    string getSyncDbStateCacheName(handle fsid, NodeHandle nh, handle userId) const;

    // Current running state.  This one is not serialized, it just makes it convenient to deliver thread-safe sync state data back to client apps.
    syncstate_t mRunningState = SYNC_CANCELED;    // cancelled indicates there is no assoicated mSync

    // enum to string conversion
    static const char* syncstatename(const syncstate_t state);
    static const char* synctypename(const Type type);
    static bool synctypefromname(const string& name, Type& type);

    SyncError knownError() const;

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

    // Try to create and start the Sync
    void changeState(syncstate_t newstate, SyncError newSyncError, bool newEnableFlag, bool notifyApp);

    // Update remote location
    bool updateSyncRemoteLocation(Node* n, bool forceCallback);
private:
    friend class Sync;
    friend struct Syncs;
    void changedConfigState(bool notifyApp);
};

class SyncThreadsafeState
{
    // This class contains things that are read/written from either the Syncs thread,
    // or the MegaClient thread.  A mutex is used to keep the data consistent.
    // Referred to by shared_ptr so transfers etc don't have awkward lifetime issues.
    mutable mutex mMutex;

    // Transfers update these from the client thread
    void adjustTransferCounts(bool upload, int32_t adjustQueued, int32_t adjustCompleted, m_off_t adjustQueuedBytes, m_off_t adjustCompletedBytes);

    // track uploads/downloads
    SyncTransferCounts mTransferCounts;

    // know where the sync's tmp folder is
    LocalPath mSyncTmpFolder;

    MegaClient* mClient = nullptr;
    handle mBackupId = 0;

public:
    void transferBegin(direction_t direction, m_off_t numBytes);
    void transferComplete(direction_t direction, m_off_t numBytes);
    void transferFailed(direction_t direction, m_off_t numBytes);

    // Return a snapshot of this sync's current transfer counts.
    SyncTransferCounts transferCounts() const;

    std::atomic<unsigned> neverScannedFolderCount{};

    LocalPath syncTmpFolder() const;
    void setSyncTmpFolder(const LocalPath&);

    SyncThreadsafeState(handle backupId, MegaClient* client) : mClient(client), mBackupId(backupId)  {}
    handle backupId() const { return mBackupId; }
    MegaClient* client() const { return mClient; }
};

class MEGA_API Sync
{
public:

    // returns the sync config
    SyncConfig& getConfig();
    const SyncConfig& getConfig() const;

    MegaClient* client = nullptr;
    Syncs& syncs;

    // for logging
    string syncname;

    // sync-wide directory notification provider
    std::unique_ptr<DirNotify> dirnotify;

    // root of local filesystem tree, holding the sync's root folder.  Never null except briefly in the destructor (to ensure efficient db usage)
    unique_ptr<LocalNode> localroot;

    FileSystemType mFilesystemType = FS_UNKNOWN;

    // current state
    syncstate_t& state() { return getConfig().mRunningState; }

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
    void changestate(syncstate_t, SyncError newSyncError, bool newEnableFlag, bool notifyApp);

    // skip duplicates and self-caused
    bool checkValidNotification(int q, Notification& notification);

    // process and remove one directory notification queue item from *notify
    dstime procscanq(int);

    // recursively look for vanished child nodes and delete them
    void deletemissing(LocalNode*);

    // scan specific path
    LocalNode* checkpath(LocalNode*, LocalPath*, string* const, dstime*, bool wejustcreatedthisfolder, DirAccess* iteratingDir);

    m_off_t localbytes = 0;
    unsigned localnodes[2]{};

    // look up LocalNode relative to localroot
    LocalNode* localnodebypath(LocalNode*, const LocalPath&, LocalNode** = nullptr, LocalPath* outpath = nullptr);

    // Assigns fs IDs to those local nodes that match the fingerprint retrieved from disk.
    // The fs IDs of unmatched nodes are invalidated.
    bool assignfsids();

    // scan items in specified path and add as children of the specified
    // LocalNode
    bool scan(LocalPath*, FileAccess*);

    // rescan sequence number (incremented when a full rescan or a new
    // notification batch starts)
    int scanseqno = 0;

    // debris path component relative to the base path
    string debris;
    LocalPath localdebris;
    LocalPath localdebrisname;

    // permanent lock on the debris/tmp folder
    unique_ptr<FileAccess> tmpfa;

    // state cache table
    unique_ptr<DbTable> statecachetable;

    // move file or folder to localdebris
    bool movetolocaldebris(const LocalPath& localpath);

    // get progress for heartbeats
    m_off_t getInflightProgress();

    // original filesystem fingerprint
    fsfp_t fsfp = 0;

    // does the filesystem have stable IDs? (FAT does not)
    bool fsstableids = false;

    // true if the sync hasn't loaded cached LocalNodes yet
    bool initializing = true;

    // true if the local synced folder is a network folder
    bool isnetwork = false;

    // values related to possible files being updated
    m_off_t updatedfilesize = ~0;
    m_time_t updatedfilets = 0;
    m_time_t updatedfileinitialts = 0;

    bool updateSyncRemoteLocation(Node* n, bool forceCallback);

    // flag to optimize destruction by skipping calls to treestate()
    bool mDestructorRunning = false;
    Sync(UnifiedSync&, const string& cdebris, const LocalPath& clocaldebris, Node*, bool, const string& logname);
    ~Sync();

    // Should we synchronize this sync?
    bool active() const;

    static const int SCANNING_DELAY_DS;
    static const int EXTRA_SCANNING_DELAY_DS;
    static const int FILE_UPDATE_DELAY_DS;
    static const int FILE_UPDATE_MAX_DELAY_SECS;
    static const dstime RECENT_VERSION_INTERVAL_SECS;

    // Change state to (DISABLED, BACKUP_MODIFIED).
    void backupModified();

    // Whether this is a backup sync.
    bool isBackup() const;

    // Whether this is a backup sync and it is mirroring.
    bool isBackupAndMirroring() const;

    // Whether this is a backup sync and it is monitoring.
    bool isBackupMonitoring() const;

    // Move the sync into the monitoring state.
    void setBackupMonitoring();

    // True if this sync should have a state cache database.
    bool shouldHaveDatabase() const;

    UnifiedSync& mUnifiedSync;

    shared_ptr<SyncThreadsafeState> threadSafeState;

protected :
    void readstatecache();

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

    // Retrieve a drive's unique backup ID.
    handle driveID(const LocalPath& drivePath) const;

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
        // Path to the drive itself.
        LocalPath drivePath;

        // The drive's unique backup ID.
        // Meaningful only for external backups.
        handle driveID = UNDEF;

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

    // Retrieve a drive's unique backup ID.
    virtual handle driveID(const LocalPath& drivePath) const;

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

struct Syncs
{
    // Retrieve a copy of configured sync settings (thread safe)
    SyncConfigVector getConfigs(bool onlyActive) const;
    bool configById(handle backupId, SyncConfig&) const;
    SyncConfigVector configsForDrive(const LocalPath& drive) const;
    SyncConfigVector selectedSyncConfigs(std::function<bool(SyncConfig&, Sync*)> selector) const;

    // Add new sync setups
    void appendNewSync(const SyncConfig&, bool startSync, bool notifyApp, std::function<void(error, SyncError, handle)> completion, bool completionInClient, const string& logname, const string& excludedPath = string());

    bool hasRunningSyncs();
    Sync* firstRunningSync();

    // only for use in tests; not really thread safe
    Sync* runningSyncByBackupIdForTests(handle backupId) const;

    void transferPauseFlagsUpdated(bool downloadsPaused, bool uploadsPaused);

    // returns a copy of the config, for thread safety
    bool syncConfigByBackupId(handle backupId, SyncConfig&) const;

    void forEachUnifiedSync(std::function<void(UnifiedSync&)> f);
    void forEachRunningSync(std::function<void(Sync* s)>);
    bool forEachRunningSync_shortcircuit(std::function<bool(Sync* s)>);
    void forEachRunningSyncContainingNode(Node* node, std::function<void(Sync* s)> f);
    void forEachSyncConfig(std::function<void(const SyncConfig&)>);

    void purgeRunningSyncs();
    void stopCancelledFailedDisabled();
    void resumeResumableSyncsOnStartup();
    void enableResumeableSyncs();
    void enableSyncByBackupId(handle backupId, bool paused, bool resetFingerprint, bool notifyApp, bool setOriginalPath, std::function<void(error, SyncError, handle)> completion, bool completionInClient, const string& logname);
    void disableSyncByBackupId(handle backupId, bool disableIsFail, SyncError syncError, bool newEnabledFlag, std::function<void()> completion);

    // disable all active syncs.  Cache is kept
    void disableSyncs(bool disableIsFail, SyncError syncError, bool newEnabledFlag, std::function<void(size_t)> completion);

    void removeSync(handle backupId, std::function<void(Error)> completion);

    // removes the sync from RAM; the config will be flushed to disk
    void unloadSelectedSyncs(std::function<bool(SyncConfig&, Sync*)> selector);

    // async, callback on client thread
    void renameSync(handle backupId, const string& newname, std::function<void(Error e)> result);

    void prepareForLogout(bool keepSyncsConfigFile, std::function<void()> clientCompletion);

    void locallogout(bool removecaches, bool keepSyncsConfigFile, bool reopenStoreAfter);

    void resetSyncConfigStore();


    Syncs(MegaClient& mc, unique_ptr<FileSystemAccess>& fsa);
    ~Syncs();


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
     */
    void backupCloseDrive(const LocalPath& drivePath, std::function<void(Error)> clientCallback);

    /**
     * @brief
     * Restores backups from an external drive.
     */
    void backupOpenDrive(const LocalPath& drivePath, std::function<void(Error)> clientCallback);


    // Add a config directly to the internal sync config DB.
    //
    // Note that configs added in this way bypass the usual sync mechanism.
    // That is, they are added directly to the JSON DB on disk.
    error syncConfigStoreAdd(const SyncConfig& config);

//private:  // anything to do with loading/saving/storing configs etc is done on the sync thread

    // Returns a reference to this user's internal configuration database.
    SyncConfigStore* syncConfigStore();

    // Whether the internal database has changes that need to be written to disk.
    bool syncConfigStoreDirty();

    // Attempts to flush the internal configuration database to disk.
    bool syncConfigStoreFlush();

    // Load internal sync configs from disk.
    error syncConfigStoreLoad(SyncConfigVector& configs);

    // updates in state & error
    void saveSyncConfig(const SyncConfig& config);


public:

    string exportSyncConfigs(const SyncConfigVector configs) const;
    string exportSyncConfigs() const;

    void importSyncConfigs(const char* data, std::function<void(error)> completion);


    typedef std::function<void(MegaClient&, TransferDbCommitter&)> QueuedClientFunc;

    void syncRun(std::function<void()>);
    void queueSync(std::function<void()>&&);
    void queueClient(QueuedClientFunc&&);

    bool onSyncThread() const {
        // when sync rework is merged, there really will be a sync thread
        // we supply this function for now to make the diffs with SRW branch easier
        return true;
    }

    // for quick lock free reference by MegaApiImpl::syncPathState (don't slow down windows explorer)
    bool mSyncVecIsEmpty = true;

    // directly accessed flag that makes sync-related logging a lot more detailed
    bool mDetailedSyncLogging = false;

    // backup rework implies certain restrictions that can be skipped
    // by setting this flag
    bool mBackupRestrictionsEnabled = true;

private:
    friend class Sync;
    friend struct UnifiedSync;
    friend class BackupInfoSync;
    friend class BackupMonitor;
    void exportSyncConfig(JSONWriter& writer, const SyncConfig& config) const;

    bool importSyncConfig(JSON& reader, SyncConfig& config);
    bool importSyncConfigs(const string& data, SyncConfigVector& configs);

    // Returns a reference to this user's sync config IO context.
    SyncConfigIOContext* syncConfigIOContext();

    void startSync_inThread(UnifiedSync& us, const string& debris, const LocalPath& localdebris,
        bool inshare, bool isNetwork, const LocalPath& rootpath,
        std::function<void(error, SyncError, handle)> completion, std::unique_ptr<FileAccess>& openedLocalFolder, const string& logname, bool notifyApp);
    void prepareForLogout_inThread(bool keepSyncsConfigFile, std::function<void()> clientCompletion);
    void locallogout_inThread(bool removecaches, bool keepSyncsConfigFile, bool reopenStoreAfter);
    void enableSyncByBackupId_inThread(handle backupId, bool paused, bool resetFingerprint, bool notifyApp, bool setOriginalPath, std::function<void(error, SyncError, handle)> completion, const string& logname, const string& excludedPath = string());
    void disableSyncByBackupId_inThread(handle backupId, bool disableIsFail, SyncError syncError, bool newEnabledFlag, std::function<void()> completion);
    void appendNewSync_inThread(const SyncConfig&, bool startSync, bool notifyApp, std::function<void(error, SyncError, handle)> completion, const string& logname, const string& excludedPath = string());
    void syncConfigStoreAdd_inThread(const SyncConfig& config, std::function<void(error)> completion);
    void clear_inThread();
    error backupOpenDrive_inThread(const LocalPath& drivePath);
    error backupCloseDrive_inThread(LocalPath drivePath);

    // ------ private data members

    MegaClient& mClient;

    // Syncs should have a separate fsaccess for thread safety
    unique_ptr<FileSystemAccess>& fsaccess;

    // pseudo-random number generator
    PrnGen rng;

    // This user's internal sync configuration store.
    unique_ptr<SyncConfigStore> mSyncConfigStore;

    // Responsible for securely writing config databases to disk.
    unique_ptr<SyncConfigIOContext> mSyncConfigIOContext;

    mutable mutex mSyncVecMutex;  // will be relevant for sync rework
    vector<unique_ptr<UnifiedSync>> mSyncVec;

    // unload the Sync (remove from RAM and data structures), its config will be flushed to disk
    bool unloadSyncByBackupID(handle id, SyncConfig&);
    void unloadSyncByIndex(size_t index);

    // shutdown safety
    bool mExecutingLocallogout = false;

    // local record of client's state for thread safety
    bool mDownloadsPaused = false;
    bool mUploadsPaused = false;

    // Responsible for tracking when to send sync/backup heartbeats
public:  // only public until SRW is merged
    unique_ptr<BackupMonitor> mHeartBeatMonitor;
private:

    // structs and classes that are private to the thread, and need access to some internals that should not be generally public
    friend struct LocalNode;
    friend class Sync;
    friend struct UnifiedSync;
};

} // namespace

#endif
#endif
