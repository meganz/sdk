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
// `accumulated` is a buffer that is used to avoid constant reallocations.
int computeReversePathMatchScore(string& accumulated, const LocalPath& path1, const LocalPath& path2, const FileSystemAccess&);

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

    // Removes a sync config at the given local path
    void remove(const std::string& localPath);

    // Returns the sync config at the given local path
    const SyncConfig* get(const std::string& localPath) const;

    // Removes all sync configs
    void clear();

    // Returns all current sync configs
    std::vector<SyncConfig> all() const;

private:
    std::unique_ptr<DbTable> mTable; // table for caching the sync configs
    std::map<std::string, SyncConfig> mSyncConfigs; // map of local paths to sync configs
};

class MEGA_API Sync
{
public:

    // returns the sync config
    const SyncConfig& getConfig() const;

    // sets whether this sync is resumable (default is true)
    void setResumable(bool isResumable);

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
    void changestate(syncstate_t);

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
    LocalNode* localnodebypath(LocalNode*, const LocalPath&, LocalNode** = NULL, string* = NULL);

    // Assigns fs IDs to those local nodes that match the fingerprint retrieved from disk.
    // The fs IDs of unmatched nodes are invalidated.
    bool assignfsids();

    // scan items in specified path and add as children of the specified
    // LocalNode
    bool scan(LocalPath* localpath, FileAccess* fa, LocalNode* localnode);

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
    error errorcode = API_OK;

    // true if the sync hasn't loaded cached LocalNodes yet
    bool initializing = true;

    // true if the local synced folder is a network folder
    bool isnetwork = false;

    // values related to possible files being updated
    m_off_t updatedfilesize = ~0;
    m_time_t updatedfilets = 0;
    m_time_t updatedfileinitialts = 0;

    Sync(MegaClient*, SyncConfig, const char*, string*, Node*, bool, int, void*);
    ~Sync();

    const LocalPath ignoreFileLocalName;
    static const string ignoreFileName;

    static const int IGNOREFILE_DELAY_DS;
    static const int SCANNING_DELAY_DS;
    static const int EXTRA_SCANNING_DELAY_DS;
    static const int FILE_UPDATE_DELAY_DS;
    static const int FILE_UPDATE_MAX_DELAY_SECS;
    static const dstime RECENT_VERSION_INTERVAL_SECS;

protected :
    bool readstatecache();

private:
    std::string mLocalPath;
};
} // namespace

#endif
#endif
