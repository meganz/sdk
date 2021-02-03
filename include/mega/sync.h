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
        const handle remoteNode,
        const string& remotePath,
        const fsfp_t localFingerprint,
        vector<string> regExps = {},
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
    handle getRemoteNode() const;
    void setRemoteNode(const handle& remoteNode);

    // the fingerprint of the local sync root folder
    fsfp_t getLocalFingerprint() const;
    void setLocalFingerprint(fsfp_t fingerprint);

    // returns the exclusion matching strings
    const vector<string>& getRegExps() const;
    void setRegExps(vector<string>&&);

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

    // Whether this sync is backed by an external device.
    bool isExternal() const;

    // check if we need to notify the App about error/enable flag changes
    bool errorOrEnabledChanged();


    // enabled/disabled by the user
    bool mEnabled = true;

    // the local path of the sync
    LocalPath mLocalPath;

    // name of the sync (if localpath is not adecuate)
    string mName;

    // the remote handle of the sync
    handle mRemoteNode;

    // the path to the remote node, as last known (not definitive)
    string mOrigninalPathOfRemoteRootNode;

    // the local fingerprint
    fsfp_t mLocalFingerprint;

    // list of regular expressions
    vector<string> mRegExps; //TODO: rename this to wildcardExclusions?: they are not regexps AFAIK

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

private:
    // If mError or mEnabled have changed from these values, we need to notify the app.
    SyncError mKnownError = NO_SYNC_ERROR;
    bool mKnownEnabled = false;
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

    // Update remote location
    bool updateSyncRemoteLocation(Node* n, bool forceCallback);
private:
    friend class Sync;
    friend struct Syncs;
    error startSync(MegaClient* client, const char* debris, LocalPath* localdebris, Node* remotenode, bool inshare, bool isNetwork, bool delayInitialScan, LocalPath& rootpath, std::unique_ptr<FileAccess>& openedLocalFolder);
    void changedConfigState(bool notifyApp);
};

using SyncCompletionFunction =
  std::function<void(UnifiedSync*, const SyncError&, error)>;

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
    Sync(UnifiedSync&, const char*, LocalPath*, Node*, bool);
    ~Sync();

    static const int SCANNING_DELAY_DS;
    static const int EXTRA_SCANNING_DELAY_DS;
    static const int FILE_UPDATE_DELAY_DS;
    static const int FILE_UPDATE_MAX_DELAY_SECS;
    static const dstime RECENT_VERSION_INTERVAL_SECS;

    UnifiedSync& mUnifiedSync;

protected :
    bool readstatecache();

private:
    LocalPath mLocalPath;
};


// For convenience.
using JSONSyncConfigMap = map<handle, SyncConfig>;

class JSONSyncConfigIOContext;

class MEGA_API JSONSyncConfigDB
{
public:
    JSONSyncConfigDB(const LocalPath& dbPath,
                     const LocalPath& drivePath);

    explicit
    JSONSyncConfigDB(const LocalPath& dbPath);

    ~JSONSyncConfigDB();

    MEGA_DISABLE_COPY(JSONSyncConfigDB);

    MEGA_DEFAULT_MOVE(JSONSyncConfigDB);

    // Adds a new (or updates an existing) config.
    const SyncConfig* add(const SyncConfig& config,
                          const bool flush = true);

    // Remove all configs.
    void clear(const bool flush = true);

    // Get current configs.
    const JSONSyncConfigMap& configs() const;

    // Path to the directory containing this database.
    const LocalPath& dbPath() const;

    // Whether this database needs to be written to disk.
    bool dirty() const;

    // Path to the drive containing this database.
    const LocalPath& drivePath() const;

    // Get config by backup tag.
    const SyncConfig* getByBackupId(handle backupId) const;

    // Get config by backup target handle.
    const SyncConfig* getByRootHandle(handle targetHandle) const;

    // Read this database from disk.
    error read(JSONSyncConfigIOContext& ioContext);

    // Remove config by backup tag.
    error removeByBackupId(handle backupId);

    // Remove config by backup target handle.
    error removeByRootHandle(handle targetHandle);

    // Clears database and removes slots from disk.
    error truncate(JSONSyncConfigIOContext& ioContext);

    // Write this database to disk.
    error write(JSONSyncConfigIOContext& ioContext);

private:
    // Reads this database from the specified slot on disk.
    error read(JSONSyncConfigIOContext& ioContext,
               const unsigned int slot);

    // Remove config by backup tag.
    error removeByBackupId(handle backupId, bool flush);

    // How many times we should be able to write the database before
    // overwriting earlier versions.
    static const unsigned int NUM_SLOTS;

    // Path to the directory containing this database.
    LocalPath mDBPath;

    // Path to the drive containing this database.
    LocalPath mDrivePath;

    // Maps backup tag to config.
    JSONSyncConfigMap mBackupIdToConfig;

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
    JSONSyncConfigIOContext(FileSystemAccess& fsAccess,
                            const string& authKey,
                            const string& cipherKey,
                            const string& name,
                            PrnGen& rng);

    virtual ~JSONSyncConfigIOContext();

    MEGA_DISABLE_COPY_MOVE(JSONSyncConfigIOContext);

    // Deserialize configs from JSON (with logging.)
    bool deserialize(const LocalPath& dbPath,
                     JSONSyncConfigMap& configs,
                     JSON& reader,
                     const unsigned int slot) const;

    // Deserialize configs from JSON.
    bool deserialize(JSONSyncConfigMap& configs,
                     JSON& reader) const;

    // Determine which slots are present.
    virtual error getSlotsInOrder(const LocalPath& dbPath,
                                  vector<unsigned int>& confSlots);

    // Read data from the specified slot.
    virtual error read(const LocalPath& dbPath,
                       string& data,
                       const unsigned int slot);

    // Remove an existing slot from disk.
    virtual error remove(const LocalPath& dbPath,
                         const unsigned int slot);

    // Remove all existing slots from disk.
    virtual error remove(const LocalPath& dbPath);

    // Serialize configs to JSON.
    void serialize(const JSONSyncConfigMap& configs,
                   JSONWriter& writer) const;

    // Write data to the specified slot.
    virtual error write(const LocalPath& dbPath,
                        const string& data,
                        const unsigned int slot);

    // Prefix applied to configuration database names.
    static const string NAME_PREFIX;

private:
    // Generate complete database path.
    LocalPath dbFilePath(const LocalPath& dbPath,
                         const unsigned int slot) const;

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
}; // JSONSyncConfigIOContext

struct Syncs
{
    UnifiedSync* appendNewSync(const SyncConfig&, MegaClient& mc);

    bool hasRunningSyncs();
    unsigned numRunningSyncs();
    Sync* firstRunningSync();
    Sync* runningSyncByBackupId(handle backupId) const;
    SyncConfig* syncConfigByBackupId(handle backupId) const;

    void forEachUnifiedSync(std::function<void(UnifiedSync&)> f);
    void forEachRunningSync(std::function<void(Sync* s)>);
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

    // This user's internal sync configuration datbase.
    unique_ptr<JSONSyncConfigDB> mSyncConfigDB;

    // Responsible for securely writing config databases to disk.
    unique_ptr<JSONSyncConfigIOContext> mSyncConfigIOContext;

    vector<unique_ptr<UnifiedSync>> mSyncVec;

    // remove the Sync and its config.  The sync's Localnode cache is removed
    void removeSyncByIndex(size_t index);

    // Removes a sync config.
    error removeSyncConfigByBackupId(handle bid);

    MegaClient& mClient;
};

} // namespace

#endif
#endif
