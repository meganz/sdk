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

// Returns true for a path that can be synced (.debris is not one of those).
bool isPathSyncable(const string& localpath, const string& localdebris, const string& localseparator);

// Invalidates the fs IDs of all local nodes below `l` and removes them from `fsidnodes`.
void invalidateFilesystemIds(handlelocalnode_map& fsidnodes, LocalNode& l, size_t& count);

// Searching from the back, this function compares path1 and path2 character by character and
// returns the number of consecutive character matches (excluding separators) but only including whole node names.
// It's assumed that the paths are normalized (e.g. not contain ..) and separated with the given `localseparator`.
int computeReversePathMatchScore(const string& path1, const string& path2, const string& localseparator);

// Recursively iterates through the filesystem tree starting at the sync root and assigns
// fs IDs to those local nodes that match the fingerprint retrieved from disk.
bool assignFilesystemIds(Sync& sync, MegaApp& app, FileSystemAccess& fsaccess, handlelocalnode_map& fsidnodes,
                         const string& localdebris, const string& localseparator, bool followsymlinks);

class MEGA_API Sync
{
public:
    void* appData = nullptr;

    MegaClient* client = nullptr;

    // sync-wide directory notification provider
    std::unique_ptr<DirNotify> dirnotify;

    // root of local filesystem tree, holding the sync's root folder
    LocalNode localroot;

    // current state
    syncstate_t state = SYNC_FAILED;

    // are we conducting a full tree scan? (during initialization and if event notification failed)
    bool fullscan = false;

    // syncing to an inbound share?
    bool inshare = false;
    
    // deletion queue
    set<int32_t> deleteq;

    // insertion/update queue
    localnode_set insertq;

    // adds an entry to the delete queue - removes it from insertq
    void statecachedel(LocalNode*);

    // adds an entry to the insert queue - removes it from deleteq
    void statecacheadd(LocalNode*);

    // recursively add children
    void addstatecachechildren(uint32_t, idlocalnode_map*, string*, LocalNode*, int);
    
    // Caches all synchronized LocalNode
    void cachenodes();

    // change state, signal to application
    void changestate(syncstate_t);

    // process and remove one directory notification queue item from *notify
    dstime procscanq(int);

    // recursively look for vanished child nodes and delete them
    void deletemissing(LocalNode*);

    // scan specific path
    LocalNode* checkpath(LocalNode*, string*, string* = NULL, dstime* = NULL, bool wejustcreatedthisfolder = false);

    m_off_t localbytes = 0;
    unsigned localnodes[2]{};

    // look up LocalNode relative to localroot
    LocalNode* localnodebypath(LocalNode*, string*, LocalNode** = NULL, string* = NULL);

    // Assigns fs IDs to those local nodes that match the fingerprint retrieved from disk.
    // The fs IDs of unmatched nodes are invalidated.
    bool assignfsids();

    // scan items in specified path and add as children of the specified
    // LocalNode
    bool scan(string*, FileAccess*);

    // own position in session sync list
    sync_list::iterator sync_it{};

    // rescan sequence number (incremented when a full rescan or a new
    // notification batch starts)
    int scanseqno = 0;

    // notified nodes originating from this sync bear this tag
    int tag = 0;

    // debris path component relative to the base path
    string debris, localdebris;

    // permanent lock on the debris/tmp folder
    FileAccess* tmpfa = nullptr;

    // state cache table
    DbTable* statecachetable = nullptr;

    // move file or folder to localdebris
    bool movetolocaldebris(string* localpath);

    // original filesystem fingerprint
    fsfp_t fsfp = 0;

    // does the filesystem have stable IDs? (FAT does not)
    bool fsstableids = false;

    // Error that causes a cancellation
    error errorcode = API_OK;

    // true if the sync hasn't loaded cached LocalNodes yet
    bool initializing = false;

    // true if the local synced folder is a network folder
    bool isnetwork = false;

    // values related to possible files being updated
    m_off_t updatedfilesize = 0;
    m_time_t updatedfilets = 0;
    m_time_t updatedfileinitialts = 0;

    Sync() = default;
    Sync(MegaClient*, string*, const char*, string*, Node*, fsfp_t, bool, int, void*);
    ~Sync();

    static const int SCANNING_DELAY_DS;
    static const int EXTRA_SCANNING_DELAY_DS;
    static const int FILE_UPDATE_DELAY_DS;
    static const int FILE_UPDATE_MAX_DELAY_SECS;
    static const dstime RECENT_VERSION_INTERVAL_SECS;

protected :
    bool readstatecache();
};
} // namespace

#endif
#endif
