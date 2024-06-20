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

enum class CollisionResolution : uint8_t
{
    Begin = 1,
    Overwrite = 1,
    RenameNewWithN = 2,
    RenameExistingToOldN = 3,
    End = 4,
};

// File is the base class for an upload or download, as managed by the SDK core.
// Each Transfer consists of a list of File that all have the same content and fingerprint
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

    void sendPutnodesOfUpload(MegaClient* client, UploadHandle fileAttrMatchHandle, const UploadToken& ultoken,
                      const FileNodeKey& filekey, putsource_t source, NodeHandle ovHandle,
                      std::function<void(const Error&, targettype_t, vector<NewNode>&, bool targetOverride, int tag)>&& completion,
                      const m_time_t* overrideMtime, bool canChangeVault);

    void sendPutnodesToCloneNode(MegaClient* client, Node* nodeToClone,
                      putsource_t source, NodeHandle ovHandle,
                      std::function<void(const Error&, targettype_t, vector<NewNode>&, bool targetOverride, int tag)>&& completion,
                      bool canChangeVault);

    void setCollisionResolution(CollisionResolution collisionResolution) { mCollisionResolution = collisionResolution; }

    CollisionResolution getCollisionResolution() const { return mCollisionResolution; }

    // generic filename for this transfer
    void displayname(string*);
    string displayname();

    // normalized name (UTF-8 with unescaped special chars)
    string name;

    // local filename (must be set upon injection for uploads, can be set in start() for downloads)
    // now able to be updated from the syncs thread, should the nodes move during upload/download
    static mutex localname_mutex;
    LocalPath localname_multithreaded;
    LocalPath getLocalname() const;
    void setLocalname(const LocalPath&);

    // source/target node handle
    NodeHandle h;

    // previous node, if any
    std::shared_ptr<Node> previousNode;

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

        // remember if the sync is from an inshare
        bool fromInsycShare : 1;
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
    bool serialize(string*) const override;

    static File* unserialize(string*);

    // tag of the file transfer
    int tag;

    // set the token true to cause cancellation of this transfer (this file of the transfer)
    CancelToken cancelToken;

    // True if this is a FUSE transfer.
    virtual bool isFuseTransfer() const;

    // relevant only for downloads (GET); do not override anywhere else
    virtual bool undelete() const { return false; }

    // Set this file's logical path.
    void logicalPath(LocalPath logicalPath);

    // Retrieve this file's logical path.
    LocalPath logicalPath() const;

private:
    CollisionResolution mCollisionResolution;

    // The file's logical path.
    LocalPath mLogicalPath;
};

class SyncThreadsafeState;
struct CloudNode;

struct SyncTransfer_inClient: public File
{
    // self-destruct after completion
    void completed(Transfer*, putsource_t) override;
    void terminated(error) override;

    // We will be passing a raw pointer to this object
    // into the tranfer system on the client thread.
    // this member prevents that becoming a dangling pointer
    // should the sync no longer require it.  So we set this
    // member just before startxfer, and reset it on completed()/terminated()
    shared_ptr<SyncTransfer_inClient> selfKeepAlive;

    shared_ptr<SyncThreadsafeState> syncThreadSafeState;

    // Why was the transfer failed/terminated?
    error mError = API_OK;

    bool wasTerminated = false;
    bool wasCompleted = false;
    bool wasRequesterAbandoned = false;

    // Whether the flags above were already set in a previous call (and avoid repeating unnecessary actions)
    bool reasonAlreadyKnown = false;
};

struct SyncDownload_inClient: public SyncTransfer_inClient
{
    shared_ptr<FileDistributor> downloadDistributor;

    // set sync-specific temp filename, update treestate
    void prepare(FileSystemAccess&) override;
    bool failed(error, MegaClient*) override;

    SyncDownload_inClient(CloudNode& n, const LocalPath&, bool fromInshare,
            shared_ptr<SyncThreadsafeState> stss, const FileFingerprint& overwriteFF);

    ~SyncDownload_inClient();

    // True if we could copy (or move) the download into place.
    bool wasDistributed = false;

    FileFingerprint okToOverwriteFF;
};

struct SyncUpload_inClient : SyncTransfer_inClient, std::enable_shared_from_this<SyncUpload_inClient>
{
    // This class is part of the client's Transfer system (ie, works in the client's thread)
    // The sync system keeps a shared_ptr to it.  Whichever system finishes with it last actually deletes it
    SyncUpload_inClient(NodeHandle targetFolder, const LocalPath& fullPath,
            const string& nodeName, const FileFingerprint& ff, shared_ptr<SyncThreadsafeState> stss,
            handle fsid, const LocalPath& localname, bool fromInshare);
    ~SyncUpload_inClient();

    void prepare(FileSystemAccess&) override;
    void completed(Transfer*, putsource_t) override;

    bool putnodesStarted = false;

    // Valid when wasPutnodesCompleted is true. (putnodes might be from upload, or shortcut node clone)
    NodeHandle putnodesResultHandle;
    bool putnodesFailed = false;

    std::atomic<bool> wasPutnodesCompleted{false};

    handle sourceFsid = UNDEF;
    LocalPath sourceLocalname;

    // once the upload completes these are set.  todo: should we dynamically allocate space for these, save RAM for mass transfer cases?
    UploadHandle uploadHandle;
    UploadToken uploadToken;
    FileNodeKey fileNodeKey;

    void sendPutnodesOfUpload(MegaClient* client, NodeHandle ovHandle);
    void sendPutnodesToCloneNode(MegaClient* client, NodeHandle ovHandle, Node* nodeToClone);
};

} // namespace

#endif
