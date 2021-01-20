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
    void insert(const SyncConfig& syncConfig);

    // Removes a sync config with a given backupId
    bool removeByBackupId(const handle backupId);

    // Returns the sync config with a given backupId
    const SyncConfig* get(const handle backupId) const;

    // Returns the first sync config found with a remote handle
    const SyncConfig* getByNodeHandle(handle nodeHandle) const;

    // Removes all sync configs
    void clear();

    // Returns all current sync configs
    std::vector<SyncConfig> all() const;

private:
    std::unique_ptr<DbTable> mTable; // table for caching the sync configs
    std::map<handle, SyncConfig> mSyncConfigs; // map of backupId to sync configs
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
    std::string mLocalPath;
};


struct Syncs
{
    UnifiedSync* appendNewSync(const SyncConfig&, MegaClient& mc);

    bool hasRunningSyncs();
    unsigned numRunningSyncs();
    Sync* firstRunningSync();
    Sync* runningSyncByBackupId(handle tag) const;

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
