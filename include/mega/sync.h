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

struct SyncDescriptor
{
    enum
    {
        TYPE_UP = 0x01, // sync up from local to remote
        TYPE_DOWN = 0x02, // sync down from remote to local
        TYPE_DEFAULT = TYPE_UP | TYPE_DOWN,
    };

    // type of the sync, defaults to bidirectional
    int syncType = TYPE_DEFAULT;

    // whether deletions are synced (only relevant for one-way-sync)
    bool syncDeletions = false;

    // whether changes are overwritten (only relevant for one-way-sync)
    bool overwriteChanges = false;
};

class MEGA_API Sync
{
public:
    void *appData;

    MegaClient* client;

    // sync-wide directory notification provider
#if __cplusplus >= 201100L
    std::unique_ptr<DirNotify> dirnotify;
#else
    std::auto_ptr<DirNotify> dirnotify;
#endif

    SyncDescriptor syncDescriptor;

    bool isUp() const;
    bool isDown() const;

    // root of local filesystem tree, holding the sync's root folder
    LocalNode localroot;

    // current state
    syncstate_t state;

    // are we conducting a full tree scan? (during initialization and if event notification failed)
    bool fullscan;

    // syncing to an inbound share?
    bool inshare;
    
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

    m_off_t localbytes;
    unsigned localnodes[2];

    // look up LocalNode relative to localroot
    LocalNode* localnodebypath(LocalNode*, string*, LocalNode** = NULL, string* = NULL);

    // scan items in specified path and add as children of the specified
    // LocalNode
    bool scan(string*, FileAccess*);

    // own position in session sync list
    sync_list::iterator sync_it;

    // rescan sequence number (incremented when a full rescan or a new
    // notification batch starts)
    int scanseqno;

    // notified nodes originating from this sync bear this tag
    int tag;

    // debris path component relative to the base path
    string debris, localdebris;

    // permanent lock on the debris/tmp folder
    FileAccess* tmpfa;

    // state cache table
    DbTable* statecachetable;

    // move file or folder to localdebris
    bool movetolocaldebris(string* localpath);

    // original filesystem fingerprint
    fsfp_t fsfp;

    // Error that causes a cancellation
    error errorcode;

    // true if the sync hasn't loaded cached LocalNodes yet
    bool initializing;

    // true if the local synced folder is a network folder
    bool isnetwork;

    // values related to possible files being updated
    m_off_t updatedfilesize;
    m_time_t updatedfilets;
    m_time_t updatedfileinitialts;
    
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
