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

// Searching from the back, this function compares path1 and path2 character by character and
// returns the number of consecutive character matches (excluding separators) but only including whole node names.
// It's assumed that the paths are normalized (e.g. not contain ..) and separated with `LocalPath::localPathSeparator`.
int computeReversePathMatchScore(const LocalPath& path1, const LocalPath& path2, const FileSystemAccess&);

// Recursively iterates through the filesystem tree starting at the sync root and assigns
// fs IDs to those local nodes that match the fingerprint retrieved from disk.
bool assignFilesystemIds(Sync& sync, MegaApp& app, FileSystemAccess& fsaccess, handlelocalnode_map& fsidnodes,
                         LocalPath& localdebris);

// A collection of sync configs backed by a database table
class MEGA_API SyncConfigBag
{
public:
    SyncConfigBag(DbAccess& dbaccess, FileSystemAccess& fsaccess, PrnGen& rng, const std::string& id);

    MEGA_DISABLE_COPY_MOVE(SyncConfigBag)

    // Adds a new sync config or updates if exists already
    error insert(const SyncConfig& syncConfig);

    // Removes a sync config with a given tag
    error removeByTag(const int tag);

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


class MEGA_API Sync
{
public:

    // returns the sync config
    SyncConfig& getConfig();

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
    Sync(UnifiedSync&, const char*, LocalPath*, Node*, bool, int);
    ~Sync();

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
    bool isBackupMirroring() const;

    // Whether this is a backup sync and it is monitoring.
    bool isBackupMonitoring() const;

    // Move the sync into the monitoring state.
    void backupMonitor();

    UnifiedSync& mUnifiedSync;

protected :
    bool readstatecache();

private:
    std::string mLocalPath;
    SyncBackupState mBackupState;
};

class MEGA_API JSONSyncConfig
{
public:
    JSONSyncConfig();

    MEGA_DEFAULT_COPY_MOVE(JSONSyncConfig);

    bool external() const;

    bool valid() const;

    bool operator==(const JSONSyncConfig& rhs) const;

    bool operator!=(const JSONSyncConfig& rhs) const;

    // Absolute path to containing drive.
    LocalPath drivePath;

    // Relative path to sync root.
    LocalPath sourcePath;

    // Absolute path to remote sync target.
    string targetPath;
    
    // Local fingerprint.
    fsfp_t fingerprint;

    // ID for backup heartbeating.
    handle heartbeatID;

    // Handle of remote sync target.
    handle targetHandle;

    // The last error that occured on this sync.
    SyncError lastError;

    // The last warning that occured on this sync.
    SyncWarning lastWarning;

    // Type of sync.
    SyncType type;

    // Identity of sync.
    int tag;

    // Whether the sync has been enabled by the user.
    bool enabled;
}; // JSONSyncConfig

// Translates an SyncConfig into a JSONSyncConfig.
JSONSyncConfig translate(const MegaClient& client, const SyncConfig &config);

// Translates an JSONSyncConfig into a SyncConfig.
SyncConfig translate(const MegaClient& client, const JSONSyncConfig& config);

// For convenience.
using JSONSyncConfigMap = map<int, JSONSyncConfig>;

class JSONSyncConfigDBObserver;
class JSONSyncConfigIOContext;

class MEGA_API JSONSyncConfigDB
{
public:
    JSONSyncConfigDB(const LocalPath& dbPath,
                     const LocalPath& drivePath,
                     JSONSyncConfigDBObserver& observer);

    explicit
    JSONSyncConfigDB(const LocalPath& dbPath);

    ~JSONSyncConfigDB();

    MEGA_DISABLE_COPY(JSONSyncConfigDB);

    MEGA_DEFAULT_MOVE(JSONSyncConfigDB);

    // Add a new (or update an existing) config.
    const JSONSyncConfig* add(const JSONSyncConfig& config);

    // Remove all configs.
    void clear();

    // Get current configs.
    const JSONSyncConfigMap& configs() const;

    // Path to the directory containing this database.
    const LocalPath& dbPath() const;

    // Whether this database needs to be written to disk.
    bool dirty() const;

    // Path to the drive containing this database.
    const LocalPath& drivePath() const;

    // Get config by backup tag.
    const JSONSyncConfig* get(const int tag) const;

    // Get config by backup target handle.
    const JSONSyncConfig* get(const handle targetHandle) const;

    // Read this database from disk.
    error read(JSONSyncConfigIOContext& ioContext);

    // Remove config by backup tag.
    error remove(const int tag);

    // Remove config by backup target handle.
    error remove(const handle targetHandle);

    // Write this database to disk.
    error write(JSONSyncConfigIOContext& ioContext);

private:
    // Adds a new (or updates an existing) config.
    const JSONSyncConfig* add(const JSONSyncConfig& config,
                              const bool flush);

    // Removes all configs.
    void clear(const bool flush);

    // Reads this database from the specified slot on disk.
    error read(JSONSyncConfigIOContext& ioContext,
               const unsigned int slot);

    // Remove config by backup tag.
    error remove(const int tag, const bool flush);

    // How many times we should be able to write the database before
    // overwriting earlier versions.
    static const unsigned int NUM_SLOTS;

    // Path to the directory containing this database.
    LocalPath mDBPath;

    // Path to the drive containing this database.
    LocalPath mDrivePath;

    // Who we tell about config changes.
    JSONSyncConfigDBObserver* mObserver;

    // Maps backup tag to config.
    JSONSyncConfigMap mTagToConfig;

    // Maps backup target handle to config.
    map<handle, JSONSyncConfig*> mTargetToConfig;

    // Tracks which 'slot' we're writing to.
    unsigned int mSlot;

    // Whether this database needs to be written to disk.
    bool mDirty;
}; // JSONSyncConfigDB

// Convenience.
using JSONSyncConfigDBPtr = unique_ptr<JSONSyncConfigDB>;

class MEGA_API JSONSyncConfigIOContext
{
public:
    JSONSyncConfigIOContext(SymmCipher& cipher,
                            FileSystemAccess& fsAccess,
                            const string& key,
                            const string& name,
                            PrnGen& rng);

    virtual ~JSONSyncConfigIOContext();

    MEGA_DISABLE_COPY_MOVE(JSONSyncConfigIOContext);

    // Deserialize configs from JSON.
    bool deserialize(JSONSyncConfigMap& configs,
                     JSON& reader) const;

    // Determine which slots are present.
    virtual error get(const LocalPath& dbPath,
                      vector<unsigned int>& slots);

    // Read data from the specified slot.
    virtual error read(const LocalPath& dbPath,
                       string& data,
                       const unsigned int slot);

    // Serialize configs to JSON.
    void serialize(const JSONSyncConfigMap& configs,
                   JSONWriter& writer) const;

    // Write data to the specified slot.
    virtual error write(const LocalPath& dbPath,
                        const string& data,
                        const unsigned int slot);

private:
    // Decrypt data.
    bool decrypt(const string& in, string& out);

    // Deserialize a config from JSON.
    bool deserialize(JSONSyncConfig& config, JSON& reader) const;

    // Encrypt data.
    string encrypt(const string& data);

    // Serialize a config to JSON.
    void serialize(const JSONSyncConfig& config, JSONWriter& writer) const;

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
}; // JSONSyncConfigIOContext

class MEGA_API JSONSyncConfigDBObserver
{
public:
    // Invoked when a backup config is being added.
    virtual void onAdd(JSONSyncConfigDB& db,
                       const JSONSyncConfig& config) = 0;

    // Invoked when a backup config is being changed.
    virtual void onChange(JSONSyncConfigDB& db,
                          const JSONSyncConfig& from,
                          const JSONSyncConfig& to) = 0;

    // Invoked when a database needs to be written.
    virtual void onDirty(JSONSyncConfigDB& db) = 0;

    // Invoked when a backup config is being removed.
    virtual void onRemove(JSONSyncConfigDB& db,
                          const JSONSyncConfig& config) = 0;

protected:
    JSONSyncConfigDBObserver();

    ~JSONSyncConfigDBObserver();
}; // JSONSyncConfigDBObserver

class MEGA_API JSONSyncConfigStore
  : private JSONSyncConfigDBObserver
{
public:
    explicit
    JSONSyncConfigStore(JSONSyncConfigIOContext& ioContext);

    virtual ~JSONSyncConfigStore();

    MEGA_DISABLE_COPY_MOVE(JSONSyncConfigStore);

    // Add a new (or update an existing) config.
    const JSONSyncConfig* add(JSONSyncConfig config);

    // Close a database.
    error close(const LocalPath& drivePath);

    // Close all databases.
    error close();

    // Get configs from a database.
    const JSONSyncConfigMap* configs(const LocalPath& drivePath) const;

    // Get all configs.
    JSONSyncConfigMap configs() const;

    // Create a new (or open an existing) database.
    const JSONSyncConfigMap* create(const LocalPath& drivePath);

    // Whether any databases need flushing.
    bool dirty() const;
    
    // Flush a database to disk.
    error flush(const LocalPath& drivePath);

    // Flush all databases to disk.
    error flush(vector<LocalPath>& drivePaths);
    error flush();

    // Get config by backup tag.
    const JSONSyncConfig* get(const int tag) const;

    // Get config by backup target handle.
    const JSONSyncConfig* get(const handle targetHandle) const;

    // Open (and add) an existing database.
    const JSONSyncConfigMap* open(const LocalPath& drivePath);

    // Check whether a database is open.
    bool opened(const LocalPath& drivePath) const;

    // Remove config by backup tag.
    error remove(const int tag);

    // Remove config by backup target handle.
    error remove(const handle targetHandle);

    // Name of the backup configuration directory.
    static const LocalPath BACKUP_CONFIG_DIR;

protected:
    // Invoked when a backup config is being added.
    virtual void onAdd(JSONSyncConfigDB& db,
                       const JSONSyncConfig& config) override;

    // Invoked when a backup config is being changed.
    virtual void onChange(JSONSyncConfigDB& db,
                          const JSONSyncConfig& from,
                          const JSONSyncConfig& to) override;

    // Invoked when a database needs to be written.
    virtual void onDirty(JSONSyncConfigDB& db) override;

    // Invoked when a backup config is being removed.
    virtual void onRemove(JSONSyncConfigDB& db,
                          const JSONSyncConfig& config) override;

private:
    // How we compare drive paths.
    struct DrivePathComparator
    {
        bool operator()(const LocalPath& lhs, const LocalPath& rhs) const
        {
            return platformCompareUtf(lhs, false, rhs, false) < 0;
        }
    }; // DrivePathComparator

    // Convenience.
    using DrivePathToDBMap =
      std::map<LocalPath, JSONSyncConfigDBPtr, DrivePathComparator>;

    // Close a database.
    error close(JSONSyncConfigDB& db);

    // Flush a database to disk.
    error flush(JSONSyncConfigDB& db);

    // Tracks which databases need to be written.
    set<JSONSyncConfigDB*> mDirtyDB;

    // Maps drive path to database.
    DrivePathToDBMap mDriveToDB;

    // IO context used to read and write from disk.
    JSONSyncConfigIOContext& mIOContext;

    // Maps backup tag to database.
    map<int, JSONSyncConfigDB*> mTagToDB;

    // Maps backup target handle to database.
    map<handle, JSONSyncConfigDB*> mTargetToDB;
}; // JSONSyncConfigStore

struct Syncs
{
    UnifiedSync* appendNewSync(const SyncConfig&, MegaClient& mc);

    bool hasRunningSyncs();
    unsigned numRunningSyncs();
    Sync* firstRunningSync();
    Sync* runningSyncByTag(int tag) const;
    SyncConfig* syncConfigByTag(const int tag) const;

    void forEachUnifiedSync(std::function<void(UnifiedSync&)> f);
    void forEachRunningSync(std::function<void(Sync* s)>);
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

    // Clears (and flushes) internal config database.
    error truncate();

    // updates in state & error
    error saveSyncConfig(const SyncConfig& config);

    Syncs(MegaClient& mc);

    // for quick lock free reference by MegaApiImpl::syncPathState (don't slow down windows explorer)
    bool isEmpty = true;

    unique_ptr<BackupMonitor> mHeartBeatMonitor;

    // use this existing class for maintaining the db
    unique_ptr<SyncConfigBag> mSyncConfigDb;

    /**
     * @brief
     * Add an external backup sync.
     *
     * @param config
     * Config describing the sync to be added.
     *
     * @param delayInitialScan
     * Whether we should delay the inital scan.
     *
     * @return
     * The result of adding the sync.
     */
    pair<error, SyncError> backupAdd(const JSONSyncConfig& config,
                                     const bool delayInitialScan = false);

    /**
     * @brief
     * Removes a previously opened backup database from memory.
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
    error backupRemove(const LocalPath& drivePath);

    /**
     * @brief
     * Restores backups loaded from an external drive.
     *
     * @param configs
     * A map describing the backups to restore.
     *
     * @param delayInitialScan
     * Whether we should delay the inital scan.
     *
     * @return
     * The result of restoring the external backups.
     */
    pair<error, SyncError> backupRestore(const LocalPath& drivePath,
                                         const JSONSyncConfigMap& configs);
    /**
     * @brief
     * Restores backups from an external drive.
     *
     * @param drivePath
     * The drive to restore external backups from.
     *
     * @param delayInitialScan
     * Whether we should delay the inital scan.
     *
     * @return
     * The result of restoring the external backups.
     */
    pair<error, SyncError> backupRestore(const LocalPath& drivePath);

    // Returns a reference to this user's backup configuration store.
    JSONSyncConfigStore* backupConfigStore();

    // Whether the store has any changes that need to be written to disk.
    bool backupConfigStoreDirty();

    // Attempts to flush database changes to disk.
    error backupConfigStoreFlush();

    // Returns a reference to this user's internal configuration database.
    JSONSyncConfigDB* syncConfigDB();

    // Whether the internal database has changes that need to be written to disk.
    bool syncConfigDBDirty();

    // Attempts to flush the internal configuration database to disk.
    error syncConfigDBFlush();

    // Load internal sync configs from disk.
    error syncConfigDBLoad();

private:
    // Returns a reference to this user's sync config IO context.
    JSONSyncConfigIOContext* syncConfigIOContext();

    // Manages this user's external backup configuration databases.
    unique_ptr<JSONSyncConfigStore> mBackupConfigStore;

    // This user's internal sync configuration datbase.
    unique_ptr<JSONSyncConfigDB> mSyncConfigDB;

    // Responsible for securely writing config databases to disk.
    unique_ptr<JSONSyncConfigIOContext> mSyncConfigIOContext;

    vector<unique_ptr<UnifiedSync>> mSyncVec;

    // remove the Sync and its config.  The sync's Localnode cache is removed
    void removeSyncByIndex(size_t index);

    // Removes a sync config.
    error removeSyncConfig(const int tag);

    MegaClient& mClient;
};

} // namespace

#endif
#endif
