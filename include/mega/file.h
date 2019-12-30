/**
 * @file mega/file.h
 * @brief Classes for transferring files
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

#ifndef MEGA_FILE_H
#define MEGA_FILE_H 1

#include "filefingerprint.h"

namespace mega {

// file to be transferred
struct MEGA_API File: public FileFingerprint
{
    // set localfilename in attached transfer
    virtual void prepare();

    // file transfer dispatched, expect updates/completion/failure
    virtual void start();

    // progress update
    virtual void progress();

    // transfer completion
    virtual void completed(Transfer*, LocalNode*);

    // transfer terminated before completion (cancelled, failed too many times)
    virtual void terminated();

    // transfer failed
    virtual bool failed(error);

    // update localname
    virtual void updatelocalname() { }

    // generic filename for this transfer
    void displayname(string*);

    // normalized name (UTF-8 with unescaped special chars)
    string name;

    // local filename (must be set upon injection for uploads, can be set in start() for downloads)
    string localname;

    // source/target node handle
    handle h;

    struct
    {
        // source handle private?
        bool hprivate : 1;

        // source handle foreign?
        bool hforeign : 1;

        // is this part of a sync transfer?
        bool syncxfer : 1;

        // is the source file temporary?
        bool temporaryfile : 1;
    };

    // private auth to access the node
    string privauth;

    // public auth to access the node
    string pubauth;

    // chat auth to access the node
    char *chatauth;

    // if !hprivate, filekey and size must be valid
    byte filekey[FILENODEKEYLENGTH]{};

    // for remote file drops: uid or e-mail address of recipient
    string targetuser;

    // transfer linkage
    Transfer* transfer;
    file_list::iterator file_it{};

    File();
    virtual ~File();

    // serialize the File object
    virtual bool serialize(string*);

    static File* unserialize(string*);

    // tag of the file
    int tag;
};

struct MEGA_API SyncFileGet: public File
{
    Sync* sync;
    Node* n;

    // set sync-specific temp filename, update treestate
    void prepare();
    bool failed(error);
    void progress();

    // update localname (may have changed due to renames/moves of the synced files)
    void updatelocalname();

    // self-destruct after completion
    void completed(Transfer*, LocalNode*);

    void terminated();

    SyncFileGet(Sync*, Node*, string*);
    ~SyncFileGet();
};

} // namespace

#endif
