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

class MEGA_API Sync
{
public:

    // returns the sync config
    const SyncConfig& getConfig() const;

    void* appData = nullptr; //DEPRECATED, do not use: sync re-enabled does not have this set.

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
    bool initializing = true;

    // true if the local synced folder is a network folder
    bool isnetwork = false;

    // values related to possible files being updated
    m_off_t updatedfilesize = ~0;
    m_time_t updatedfilets = 0;
    m_time_t updatedfileinitialts = 0;

    // flag to optimize destruction by skipping calls to treestate()
    bool mDestructorRunning = false;
    Sync(MegaClient*, SyncConfig &, const char*, LocalPath*, Node*, bool, int, void*);
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
    bool isMirroring() const;

    // Whether this is a backup sync and it is monitoring.
    bool isMonitoring() const;

    // Move the sync into the monitoring state.
    void monitor();

protected:
    bool readstatecache();

private:
    std::string mLocalPath;
    SyncBackupState mBackupState;
};

class MEGA_API XBackupConfig
{
public:
    XBackupConfig();

    MEGA_DEFAULT_COPY_MOVE(XBackupConfig);

    bool valid() const;

    bool operator==(const XBackupConfig& rhs) const;

    bool operator!=(const XBackupConfig& rhs) const;

    // Absolute path to containing drive.
    LocalPath drivePath;

    // Relative path to sync root.
    LocalPath sourcePath;
    
    // ID for backup heartbeating.
    handle heartbeatID;

    // Handle of remote sync target.
    handle targetHandle;

    // The last error that occured on this sync.
    SyncError lastError;

    // Identity of sync.
    int tag;

    // Whether the sync has been enabled by the user.
    bool enabled;
}; // XBackupConfig

// Translates an SyncConfig into a XBackupConfig.
XBackupConfig translate(const MegaClient& client, const SyncConfig &config);

// Translates an XBackupConfig into a SyncConfig.
SyncConfig translate(const MegaClient& client, const XBackupConfig& config);

// For convenience.
using XBackupConfigMap = map<int, XBackupConfig>;

class XBackupConfigDBObserver;
class XBackupConfigIOContext;

class MEGA_API XBackupConfigDB
{
public:
    XBackupConfigDB(const LocalPath& drivePath,
                    XBackupConfigDBObserver& observer);

    ~XBackupConfigDB();

    MEGA_DISABLE_COPY(XBackupConfigDB);

    MEGA_DEFAULT_MOVE(XBackupConfigDB);

    // Add a new (or update an existing) config.
    const XBackupConfig* add(const XBackupConfig& config);

    // Remove all configs.
    void clear();

    // Get current configs.
    const XBackupConfigMap& configs() const;

    // Path to the drive containing this database.
    const LocalPath& drivePath() const;

    // Get config by backup tag.
    const XBackupConfig* get(const int tag) const;

    // Get config by backup target handle.
    const XBackupConfig* get(const handle targetHandle) const;

    // Read this database from disk.
    error read(XBackupConfigIOContext& ioContext);

    // Remove config by backup tag.
    error remove(const int tag);

    // Remove config by backup target handle.
    error remove(const handle targetHandle);

    // Write this database to disk.
    error write(XBackupConfigIOContext& ioContext);

private:
    // Adds a new (or updates an existing) config.
    const XBackupConfig* add(const XBackupConfig& config,
                             const bool flush);

    // Removes all configs.
    void clear(const bool flush);

    // Reads this database from the specified slot on disk.
    error read(XBackupConfigIOContext& ioContext,
               const unsigned int slot);

    // Remove config by backup tag.
    error remove(const int tag, const bool flush);

    // How many times we should be able to write the database before
    // overwriting earlier versions.
    static const unsigned int NUM_SLOTS;

    // Path to the drive containing this database.
    LocalPath mDrivePath;

    // Who we tell about config changes.
    XBackupConfigDBObserver& mObserver;

    // Maps backup tag to config.
    XBackupConfigMap mTagToConfig;

    // Maps backup target handle to config.
    map<handle, XBackupConfig*> mTargetToConfig;

    // Tracks which 'slot' we're writing to.
    unsigned int mSlot;
}; // XBackupConfigDB

// Convenience.
using XBackupConfigDBPtr = unique_ptr<XBackupConfigDB>;

class MEGA_API XBackupConfigIOContext
{
public:
    XBackupConfigIOContext(SymmCipher& cipher,
                           FileSystemAccess& fsAccess,
                           const string& key,
                           const string& name,
                           PrnGen& rng);

    virtual ~XBackupConfigIOContext();

    MEGA_DISABLE_COPY_MOVE(XBackupConfigIOContext);

    // Deserialize configs from JSON.
    bool deserialize(XBackupConfigMap& configs,
                     JSON& reader) const;

    // Determine which slots are present.
    error get(const LocalPath& drivePath,
              vector<unsigned int>& slots);

    // Read data from the specified slot.
    error read(const LocalPath& drivePath,
               string& data,
               const unsigned int slot);

    // Serialize configs to JSON.
    void serialize(const XBackupConfigMap& configs,
                   JSONWriter& writer) const;

    // Write data to the specified slot.
    error write(const LocalPath& drivePath,
                const string& data,
                const unsigned int slot);

private:
    // Decrypt data.
    bool decrypt(const string& in, string& out);

    // Deserialize a config from JSON.
    bool deserialize(XBackupConfig& config, JSON& reader) const;

    // Encrypt data.
    string encrypt(const string& data);

    // Serialize a config to JSON.
    void serialize(const XBackupConfig& config, JSONWriter& writer) const;

    // Name of the backup configuration directory.
    static const string BACKUP_CONFIG_DIR;

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
}; // XBackupConfigIOContext

class MEGA_API XBackupConfigDBObserver
{
public:
    // Invoked when a backup config is being added.
    virtual void onAdd(XBackupConfigDB& db,
                       const XBackupConfig& config) = 0;

    // Invoked when a backup config is being changed.
    virtual void onChange(XBackupConfigDB& db,
                          const XBackupConfig& from,
                          const XBackupConfig& to) = 0;

    // Invoked when a database needs to be written.
    virtual void onDirty(XBackupConfigDB& db) = 0;

    // Invoked when a backup config is being removed.
    virtual void onRemove(XBackupConfigDB& db,
                          const XBackupConfig& config) = 0;

protected:
    XBackupConfigDBObserver();

    ~XBackupConfigDBObserver();
}; // XBackupConfigDBObserver

class MEGA_API XBackupConfigStore
  : private XBackupConfigDBObserver
  , private XBackupConfigIOContext
{
public:
    XBackupConfigStore(SymmCipher& cipher,
                       FileSystemAccess& fsAccess,
                       const string& key,
                       const string& name,
                       PrnGen& rng);

    ~XBackupConfigStore();

    MEGA_DISABLE_COPY_MOVE(XBackupConfigStore);

    // Add a new (or update an existing) config.
    const XBackupConfig* add(const XBackupConfig& config);

    // Close a database.
    error close(const LocalPath& drivePath);

    // Close all databases.
    error close();

    // Get configs from a database.
    const XBackupConfigMap* configs(const LocalPath& drivePath) const;

    // Get all configs.
    XBackupConfigMap configs() const;

    // Create a new (or open an existing) database.
    const XBackupConfigMap* create(const LocalPath& drivePath);

    // Whether any databases need flushing.
    bool dirty() const;
    
    // Flush a database to disk.
    error flush(const LocalPath& drivePath);

    // Flush all databases to disk.
    error flush(vector<LocalPath>& drivePaths);
    error flush();

    // Get config by backup tag.
    const XBackupConfig* get(const int tag) const;

    // Get config by backup target handle.
    const XBackupConfig* get(const handle targetHandle) const;

    // Open (and add) an existing database.
    const XBackupConfigMap* open(const LocalPath& drivePath);

    // Check whether a database is open.
    bool opened(const LocalPath& drivePath) const;

    // Remove config by backup tag.
    error remove(const int tag);

    // Remove config by backup target handle.
    error remove(const handle targetHandle);

private:
    // Close a database.
    error close(XBackupConfigDB& db);

    // Flush a database to disk.
    error flush(XBackupConfigDB& db);

    // Invoked when a backup config is being added.
    void onAdd(XBackupConfigDB& db,
               const XBackupConfig& config) override;

    // Invoked when a backup config is being changed.
    void onChange(XBackupConfigDB& db,
                  const XBackupConfig& from,
                  const XBackupConfig& to) override;

    // Invoked when a database needs to be written.
    void onDirty(XBackupConfigDB& db) override;

    // Invoked when a backup config is being removed.
    void onRemove(XBackupConfigDB& db,
                  const XBackupConfig& config) override;

    // Tracks which databases need to be written.
    set<XBackupConfigDB*> mDirtyDB;

    // Maps drive path to database.
    map<LocalPath, XBackupConfigDBPtr> mDriveToDB;

    // Maps backup tag to database.
    map<int, XBackupConfigDB*> mTagToDB;

    // Maps backup target handle to database.
    map<handle, XBackupConfigDB*> mTargetToDB;
}; // XBackupConfigStore

} // namespace

#endif
#endif
