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

#include "filesystem.h"

namespace mega {

// file to be transferred
struct MEGA_API File: public FileFingerprint
{
    // set localfilename in attached transfer
    virtual void prepare(FileSystemAccess&);

    // file transfer dispatched, expect updates/completion/failure
    virtual void start();

    // progress update
    virtual void progress();

    // transfer completion
    virtual void completed(Transfer*, putsource_t source);

    // transfer terminated before completion (cancelled, failed too many times)
    virtual void terminated(error e);

    // return true if the transfer should keep trying (limited to 16)
    // return false to delete the transfer
    virtual bool failed(error, MegaClient*);

    // update localname
    virtual void updatelocalname() { }

    void sendPutnodes(MegaClient* client, UploadHandle fileAttrMatchHandle, const UploadToken& ultoken,
                      const FileNodeKey& filekey, putsource_t source, NodeHandle ovHandle,
                      std::function<void(const Error&, targettype_t, vector<NewNode>&, bool targetOverride)>&& completion,
                      LocalNode* l, const m_time_t* overrideMtime, bool canChangeVault);

    // generic filename for this transfer
    void displayname(string*);
    string displayname();

    // normalized name (UTF-8 with unescaped special chars)
    string name;

    // local filename (must be set upon injection for uploads, can be set in start() for downloads)
    LocalPath localname;

    // source/target node handle
    NodeHandle h;

    // previous node, if any
    Node *previousNode = nullptr;

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

    VersioningOption mVersioningOption = NoVersioning;

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
    bool serialize(string*) override;

    static File* unserialize(string*);

    // tag of the file transfer
    int tag;

    // set the token true to cause cancellation of this transfer (this file of the transfer)
    CancelToken cancelToken;
};

struct MEGA_API SyncFileGet: public File
{
    Sync* sync;
    Node* n;

    // set sync-specific temp filename, update treestate
    void prepare(FileSystemAccess&) override;
    bool failed(error, MegaClient*) override;
    void progress() override;

    // update localname (may have changed due to renames/moves of the synced files)
    void updatelocalname() override;

    // self-destruct after completion
    void completed(Transfer*, putsource_t source) override;

    void terminated(error e) override;

    SyncFileGet(Sync*, Node*, const LocalPath&);
    ~SyncFileGet();
};

} // namespace

#endif
