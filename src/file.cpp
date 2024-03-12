/**
 * @file file.cpp
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

#include "mega/file.h"
#include "mega/transfer.h"
#include "mega/transferslot.h"
#include "mega/megaclient.h"
#include "mega/sync.h"
#include "mega/command.h"
#include "mega/logging.h"
#include "mega/heartbeats.h"
#include "mega/megaapp.h"

namespace mega {

mutex File::localname_mutex;

File::File()
    :mCollisionResolution(CollisionResolution::RenameNewWithN)
{
    transfer = NULL;
    chatauth = NULL;
    hprivate = true;
    hforeign = false;
    syncxfer = false;
    fromInsycShare = false;
    temporaryfile = false;
    tag = 0;
}

File::~File()
{
    // if transfer currently running, stop
    if (transfer)
    {
        transfer->client->stopxfer(this, nullptr);
    }
    delete [] chatauth;
}

LocalPath File::getLocalname() const
{
    lock_guard<mutex> g(localname_mutex);
    return localname_multithreaded;
}

void File::setLocalname(const LocalPath& ln)
{
    lock_guard<mutex> g(localname_mutex);
    localname_multithreaded = ln;
}

bool File::serialize(string *d) const
{
    char type = char(transfer->type);
    d->append((const char*)&type, sizeof(type));

    if (!FileFingerprint::serialize(d))
    {
        LOG_err << "Error serializing File: Unable to serialize FileFingerprint";
        return false;
    }

    unsigned short ll;
    bool flag;

    ll = (unsigned short)name.size();
    d->append((char*)&ll, sizeof(ll));
    d->append(name.data(), ll);

    auto tmpstr = getLocalname().platformEncoded();
    ll = (unsigned short)tmpstr.size();
    d->append((char*)&ll, sizeof(ll));
    d->append(tmpstr.data(), ll);

    ll = (unsigned short)targetuser.size();
    d->append((char*)&ll, sizeof(ll));
    d->append(targetuser.data(), ll);

    ll = (unsigned short)privauth.size();
    d->append((char*)&ll, sizeof(ll));
    d->append(privauth.data(), ll);

    ll = (unsigned short)pubauth.size();
    d->append((char*)&ll, sizeof(ll));
    d->append(pubauth.data(), ll);

    d->append((const char*)&h, sizeof(h));
    d->append((const char*)filekey, sizeof(filekey));

    flag = hprivate;
    d->append((const char*)&flag, sizeof(flag));

    flag = hforeign;
    d->append((const char*)&flag, sizeof(flag));

    flag = syncxfer;
    d->append((const char*)&flag, sizeof(flag));

    flag = temporaryfile;
    d->append((const char*)&flag, sizeof(flag));

    char hasChatAuth = (chatauth && chatauth[0]) ? 1 : 0;
    d->append((char *)&hasChatAuth, 1);

    d->append((char*)&mCollisionResolution, 1);

    d->append("\0\0\0\0\0\0\0", 8);

    if (hasChatAuth)
    {
        ll = (unsigned short) strlen(chatauth);
        d->append((char*)&ll, sizeof(ll));
        d->append(chatauth, ll);
    }

    return true;
}

File *File::unserialize(string *d)
{
    if (!d->size())
    {
        LOG_err << "Error unserializing File: Empty string";
        return NULL;
    }

    d->erase(0, 1);

    const char* ptr = d->data();
    const char* end = ptr + d->size();

    auto fp = FileFingerprint::unserialize(ptr, end);
    if (!fp)
    {
        LOG_err << "Error unserializing File: Unable to unserialize FileFingerprint";
        return NULL;
    }

    if (ptr + sizeof(unsigned short) > end)
    {
        LOG_err << "File unserialization failed - serialized string too short";
        return NULL;
    }

    // read name
    unsigned short namelen = MemAccess::get<unsigned short>(ptr);
    ptr += sizeof(namelen);
    if (ptr + namelen + sizeof(unsigned short) > end)
    {
        LOG_err << "File unserialization failed - name too long";
        return NULL;
    }
    const char *name = ptr;
    ptr += namelen;

    // read localname
    unsigned short localnamelen = MemAccess::get<unsigned short>(ptr);
    ptr += sizeof(localnamelen);
    if (ptr + localnamelen + sizeof(unsigned short) > end)
    {
        LOG_err << "File unserialization failed - localname too long";
        return NULL;
    }
    const char *localname = ptr;
    ptr += localnamelen;

    // read targetuser
    unsigned short targetuserlen = MemAccess::get<unsigned short>(ptr);
    ptr += sizeof(targetuserlen);
    if (ptr + targetuserlen + sizeof(unsigned short) > end)
    {
        LOG_err << "File unserialization failed - targetuser too long";
        return NULL;
    }
    const char *targetuser = ptr;
    ptr += targetuserlen;

    // read private auth
    unsigned short privauthlen = MemAccess::get<unsigned short>(ptr);
    ptr += sizeof(privauthlen);
    if (ptr + privauthlen + sizeof(unsigned short) > end)
    {
        LOG_err << "File unserialization failed - private auth too long";
        return NULL;
    }
    const char *privauth = ptr;
    ptr += privauthlen;

    unsigned short pubauthlen = MemAccess::get<unsigned short>(ptr);
    ptr += sizeof(pubauthlen);
    if (ptr + pubauthlen 
            + sizeof(handle) 
            + FILENODEKEYLENGTH 
            + sizeof(bool)      //hprivate
            + sizeof(bool)      //hforeign
            + sizeof(bool)      //syncxfer
            + sizeof(bool)      //temporaryfile
            + sizeof(char)      //hasChatAuth
            + sizeof(uint8_t)   //collisionResolution
            + 8                 //8 '0'
            > end)
    {
        LOG_err << "File unserialization failed - public auth too long";
        return NULL;
    }
    const char *pubauth = ptr;
    ptr += pubauthlen;

    File *file = new File();
    *(FileFingerprint *)file = *fp;
    fp.reset();

    file->name.assign(name, namelen);
    file->setLocalname(LocalPath::fromPlatformEncodedAbsolute(std::string(localname, localnamelen)));
    file->targetuser.assign(targetuser, targetuserlen);
    file->privauth.assign(privauth, privauthlen);
    file->pubauth.assign(pubauth, pubauthlen);

    file->h.set6byte(MemAccess::get<handle>(ptr));
    ptr += sizeof(handle);

    memcpy(file->filekey, ptr, FILENODEKEYLENGTH);
    ptr += FILENODEKEYLENGTH;

    file->hprivate = MemAccess::get<bool>(ptr);
    ptr += sizeof(bool);

    file->hforeign = MemAccess::get<bool>(ptr);
    ptr += sizeof(bool);

    file->syncxfer = MemAccess::get<bool>(ptr);
    ptr += sizeof(bool);

    file->temporaryfile = MemAccess::get<bool>(ptr);
    ptr += sizeof(bool);

    char hasChatAuth = MemAccess::get<char>(ptr);
    ptr += sizeof(char);

    uint8_t collisionResolutionUint8 = MemAccess::get<uint8_t>(ptr);
    ptr += sizeof(uint8_t);
    if (collisionResolutionUint8 < static_cast<uint8_t>(CollisionResolution::Begin) || collisionResolutionUint8 >= static_cast<uint8_t>(CollisionResolution::End))
    {
        LOG_err << "File unserialization failed - collision resolution " << collisionResolutionUint8 << " not valid";
        delete file;
        return NULL;
    }
    file->setCollisionResolution(static_cast<CollisionResolution>(collisionResolutionUint8));

    if (memcmp(ptr, "\0\0\0\0\0\0\0", 8))
    {
        LOG_err << "File unserialization failed - invalid version";
        delete file;
        return NULL;
    }
    ptr += 8;

    if (hasChatAuth)
    {
        if (ptr + sizeof(unsigned short) <= end)
        {
            unsigned short chatauthlen = MemAccess::get<unsigned short>(ptr);
            ptr += sizeof(chatauthlen);

            if (!chatauthlen || ptr + chatauthlen > end)
            {
                LOG_err << "File unserialization failed - incorrect size of chat auth";
                delete file;
                return NULL;
            }

            file->chatauth = new char[chatauthlen + 1];
            memcpy(file->chatauth, ptr, chatauthlen);
            file->chatauth[chatauthlen] = '\0';
            ptr += chatauthlen;
        }
        else
        {
            LOG_err << "File unserialization failed - chat auth not found";
            delete file;
            return NULL;
        }
    }

    d->erase(0, ptr - d->data());
    return file;
}

void File::prepare(FileSystemAccess&)
{
    transfer->localfilename = getLocalname();
    assert(transfer->localfilename.isAbsolute());
}

void File::start()
{
}

void File::progress()
{
}

void File::completed(Transfer* t, putsource_t source)
{
    assert(!transfer || t == transfer);
    assert(source == PUTNODES_APP);  // derived class for sync doesn't use this code path

    if (t->type == PUT)
    {
        sendPutnodesOfUpload(t->client, t->uploadhandle, *t->ultoken, t->filekey, source, NodeHandle(), nullptr, nullptr, false);
    }
}


void File::sendPutnodesOfUpload(MegaClient* client, UploadHandle fileAttrMatchHandle, const UploadToken& ultoken,
                        const FileNodeKey& filekey, putsource_t source, NodeHandle ovHandle,
                        CommandPutNodes::Completion&& completion,
                        const m_time_t* overrideMtime, bool canChangeVault)
{
    vector<NewNode> newnodes(1);
    NewNode* newnode = &newnodes[0];

    // build new node
    newnode->source = NEW_UPLOAD;
    newnode->canChangeVault = canChangeVault;

    // upload handle required to retrieve/include pending file attributes
    newnode->uploadhandle = fileAttrMatchHandle;

    // reference to uploaded file
    newnode->uploadtoken = ultoken;

    // file's crypto key
    static_assert(sizeof(filekey) == FILENODEKEYLENGTH, "File completed: filekey size doesn't match with FILENODEKEYLENGTH");
    newnode->nodekey.assign((char*)&filekey, FILENODEKEYLENGTH);
    newnode->type = FILENODE;
    newnode->parenthandle = UNDEF;

    AttrMap attrs;
    MegaClient::honorPreviousVersionAttrs(previousNode.get(), attrs);

    // store filename
    attrs.map['n'] = name;

    // store fingerprint
    auto oldMtime = mtime;
    if (overrideMtime) mtime = *overrideMtime;
    serializefingerprint(&attrs.map['c']);
    if (overrideMtime) mtime = oldMtime;

    string tattrstring;

    attrs.getjson(&tattrstring);

    newnode->attrstring.reset(new string);
    MegaClient::makeattr(client->getRecycledTemporaryTransferCipher(filekey.bytes.data(), FILENODE),
                         newnode->attrstring, tattrstring.c_str());

    if (targetuser.size())
    {
        // drop file into targetuser's inbox (obsolete feature, kept for sending logs to helpdesk)
        client->putnodes(targetuser.c_str(), std::move(newnodes), tag, std::move(completion));
    }
    else
    {
        NodeHandle th = h;


        if (syncxfer)
        {
            newnode->ovhandle = ovHandle;
        }
        else if (mVersioningOption != NoVersioning)  // todo: resolve clash with mVersioningOption vs fixNameConflicts
        {
            // for manual upload, let the API apply the `ov` according to the global versions_disabled flag.
            // with versions on, the API will make the ov the first version of this new node
            // with versions off, the API will permanently delete `ov`, replacing it with this (and attaching the ov's old versions)
            std::shared_ptr<Node> n = client->nodeByHandle(th);
            if (std::shared_ptr<Node> ovNode = client->getovnode(n.get(), &name))
            {
                newnode->ovhandle = ovNode->nodeHandle();
            }
        }

        client->reqs.add(new CommandPutNodes(client,
                                             th, NULL,
                                             mVersioningOption,
                                             std::move(newnodes),
                                             tag,
                                             source, nullptr, std::move(completion), canChangeVault));
    }
}

void File::sendPutnodesToCloneNode(MegaClient* client, Node* nodeToClone,
                    putsource_t source, NodeHandle ovHandle,
                    std::function<void(const Error&, targettype_t, vector<NewNode>&, bool targetOverride, int tag)>&& completion,
                    bool canChangeVault)
{
    vector<NewNode> newnodes(1);
    NewNode* newnode = &newnodes[0];

    // build new node
    newnode->source = NEW_NODE;
    newnode->canChangeVault = canChangeVault;
    newnode->nodehandle = nodeToClone->nodehandle;

    // file's crypto key
    newnode->nodekey = nodeToClone->nodekey();
    assert(newnode->nodekey.size() == FILENODEKEYLENGTH);

    // copy attrs
    AttrMap attrs;
    attrs.map = nodeToClone->attrs.map;
    attr_map::iterator it = attrs.map.find(AttrMap::string2nameid("rr"));
    if (it != attrs.map.end())
    {
        LOG_debug << "Removing rr attribute for clone";
        attrs.map.erase(it);
    }
    newnode->type = FILENODE;
    newnode->parenthandle = UNDEF;

    // store filename
    attrs.map['n'] = name;

    string tattrstring;
    attrs.getjson(&tattrstring);

    newnode->attrstring.reset(new string);
    MegaClient::makeattr(client->getRecycledTemporaryTransferCipher((byte*)newnode->nodekey.data(), FILENODE),
                         newnode->attrstring, tattrstring.c_str());

    if (targetuser.size())
    {
        // drop file into targetuser's inbox (obsolete feature, kept for sending logs to helpdesk)
        client->putnodes(targetuser.c_str(), std::move(newnodes), tag, std::move(completion));
    }
    else
    {
        NodeHandle th = h;
        assert(syncxfer);
        newnode->ovhandle = ovHandle;
        client->reqs.add(new CommandPutNodes(client,
                                             th, NULL,
                                             mVersioningOption,
                                             std::move(newnodes),
                                             tag,
                                             source, nullptr, std::move(completion), canChangeVault));
    }
}


void File::terminated(error)
{

}

// do not retry crypto errors or administrative takedowns; retry other types of
// failuresup to 16 times, except I/O errors (6 times)
bool File::failed(error e, MegaClient*)
{
    if (e == API_EKEY)
    {
        return false; // mac error; do not retry
    }

    return  // Non fatal errors, up to 16 retries
            ((e != API_EBLOCKED && e != API_ENOENT && e != API_EINTERNAL && e != API_EACCESS && e != API_ETOOMANY && transfer->failcount < 16)
            // I/O errors up to 6 retries
            && !((e == API_EREAD || e == API_EWRITE) && transfer->failcount > 6))
            // Retry sync transfers up to 8 times for erros that doesn't have a specific management
            // to prevent immediate retries triggered by the sync engine
            || (syncxfer && e != API_EBLOCKED && e != API_EKEY && transfer->failcount <= 8)
            // Infinite retries for storage overquota errors
            || e == API_EOVERQUOTA || e == API_EGOINGOVERQUOTA;
}

void File::displayname(string* dname)
{
    if (name.size())
    {
        *dname = name;
    }
    else
    {
        shared_ptr<Node> n;

        if ((n = transfer->client->nodeByHandle(h)))
        {
            *dname = n->displayname();
        }
        else
        {
            *dname = "DELETED/UNAVAILABLE";
        }
    }
}

string File::displayname()
{
    string result;

    displayname(&result);

    return result;
}

#ifdef ENABLE_SYNC

void SyncTransfer_inClient::terminated(error e)
{
    File::terminated(e);

    if (e == API_EOVERQUOTA)
    {
        syncThreadSafeState->client()->syncs.disableSyncByBackupId(syncThreadSafeState->backupId(), FOREIGN_TARGET_OVERSTORAGE, false, true, nullptr);
    }

    wasTerminated = true;
    selfKeepAlive.reset();  // deletes this object! (if abandoned by sync)
}

void SyncTransfer_inClient::completed(Transfer* t, putsource_t source)
{
    assert(source == PUTNODES_SYNC);

    // do not allow the base class to submit putnodes immediately
    //File::completed(t, source);

    wasCompleted = true;
    selfKeepAlive.reset();  // deletes this object! (if abandoned by sync)
}

void SyncUpload_inClient::completed(Transfer* t, putsource_t source)
{
    // Keep the info required for putnodes and wait for
    // the sync thread to validate and activate the putnodes

    uploadHandle = t->uploadhandle;
    uploadToken = *t->ultoken;
    fileNodeKey = t->filekey;

    SyncTransfer_inClient::completed(t, source);
}

void SyncUpload_inClient::sendPutnodesOfUpload(MegaClient* client, NodeHandle ovHandle)
{
    // Always called from the client thread
    weak_ptr<SyncThreadsafeState> stts = syncThreadSafeState;

    // So we know whether it's safe to update putnodesCompleted.
    weak_ptr<SyncUpload_inClient> self = shared_from_this();

    // since we are now sending putnodes, no need to remember puts to inform the client on abandonment
    syncThreadSafeState->client()->transferBackstop.forget(tag);

    File::sendPutnodesOfUpload(client,
        uploadHandle,
        uploadToken,
        fileNodeKey,
        PUTNODES_SYNC,
        ovHandle,
        [self, stts, client](const Error& e, targettype_t t, vector<NewNode>& nn, bool targetOverride, int tag){
            // Is the originating transfer still alive?
            if (auto s = self.lock())
            {
                // Then track the result of its putnodes request.
                s->putnodesFailed = e != API_OK;

                // Capture the handle if the putnodes was successful.
                if (!s->putnodesFailed)
                {
                    assert(!nn.empty());
                    s->putnodesResultHandle.set6byte(nn.front().mAddedHandle);
                }

                // Let the engine know the putnodes has completed.
                s->wasPutnodesCompleted.store(true);
            }

            if (auto s = stts.lock())
            {
                if (e == API_EACCESS)
                {
                    client->sendevent(99402, "API_EACCESS putting node in sync transfer", 0);
                }
                else if (e == API_EOVERQUOTA)
                {
                    client->syncs.disableSyncByBackupId(s->backupId(),  FOREIGN_TARGET_OVERSTORAGE, false, true, nullptr);
                }
            }

            // since we used a completion function, putnodes_result is not called.
            // but the intermediate layer still needs that in order to call the client app back:
            client->app->putnodes_result(e, t, nn, targetOverride, tag);

        }, nullptr, syncThreadSafeState->mCanChangeVault);
}

void SyncUpload_inClient::sendPutnodesToCloneNode(MegaClient* client, NodeHandle ovHandle, Node* nodeToClone)
{
    // Always called from the client thread
    weak_ptr<SyncThreadsafeState> stts = syncThreadSafeState;

    // So we know whether it's safe to update putnodesCompleted.
    weak_ptr<SyncUpload_inClient> self = shared_from_this();

    File::sendPutnodesToCloneNode(client,
        nodeToClone,
        PUTNODES_SYNC,
        ovHandle,
        [self, stts, client](const Error& e, targettype_t t, vector<NewNode>& nn, bool targetOverride, int tag){
            // Is the originating transfer still alive?
            if (auto s = self.lock())
            {
                // Then track the result of its putnodes request.
                s->putnodesFailed = e != API_OK;

                // Capture the handle if the putnodes was successful.
                if (!s->putnodesFailed)
                {
                    assert(!nn.empty());
                    s->putnodesResultHandle.set6byte(nn.front().mAddedHandle);
                }

                // Let the engine know the putnodes has completed.
                s->wasPutnodesCompleted.store(true);
            }

            if (auto s = stts.lock())
            {
                if (e == API_EACCESS)
                {
                    client->sendevent(99402, "API_EACCESS putting node in sync transfer", 0);
                }
                else if (e == API_EOVERQUOTA)
                {
                    client->syncs.disableSyncByBackupId(s->backupId(),  FOREIGN_TARGET_OVERSTORAGE, false, true, nullptr);
                }
            }
        }, syncThreadSafeState->mCanChangeVault);
}

SyncUpload_inClient::SyncUpload_inClient(NodeHandle targetFolder, const LocalPath& fullPath,
        const string& nodeName, const FileFingerprint& ff, shared_ptr<SyncThreadsafeState> stss,
        handle fsid, const LocalPath& localname, bool fromInshare)
{
    *static_cast<FileFingerprint*>(this) = ff;

    // normalized name (UTF-8 with unescaped special chars)
    // todo: we did unescape them though?
    name = nodeName;

    // setting the full path means it works like a normal non-sync transfer
    setLocalname(fullPath);

    h = targetFolder;

    hprivate = false;
    hforeign = false;
    syncxfer = true;
    fromInsycShare = fromInshare;
    temporaryfile = false;
    chatauth = nullptr;
    transfer = nullptr;
    tag = 0;

    syncThreadSafeState = move(stss);
    syncThreadSafeState->transferBegin(PUT, size);

    sourceFsid = fsid;
    sourceLocalname = localname;
}

SyncUpload_inClient::~SyncUpload_inClient()
{
    if (!wasTerminated && !wasCompleted)
    {
        assert(wasRequesterAbandoned);
        transfer = nullptr;  // don't try to remove File from Transfer from the wrong thread
    }

    if (wasCompleted && wasPutnodesCompleted)
    {
        syncThreadSafeState->transferComplete(PUT, size);
    }
    else
    {
        syncThreadSafeState->transferFailed(PUT, size);
    }

    if (!uploadHandle.isUndef())
    {
        // Remove file attributes if they weren't removed upon ~Transfer destructor
        syncThreadSafeState->client()->fileAttributesUploading.erase(uploadHandle);
    }

    if (putnodesStarted)
    {
        syncThreadSafeState->removeExpectedUpload(h, name);
    }
}

void SyncUpload_inClient::prepare(FileSystemAccess&)
{
    transfer->localfilename = getLocalname();

    // is this transfer in progress? update file's filename.
    if (transfer->slot && transfer->slot->fa && !transfer->slot->fa->nonblocking_localname.empty())
    {
        transfer->slot->fa->updatelocalname(transfer->localfilename, false);
    }

    //todo: localNode.treestate(TREESTATE_SYNCING);
}

SyncDownload_inClient::SyncDownload_inClient(CloudNode& n, const LocalPath& clocalname, bool fromInshare,
        shared_ptr<SyncThreadsafeState> stss, const FileFingerprint& overwriteFF)
{
    h = n.handle;
    *(FileFingerprint*)this = n.fingerprint;
    okToOverwriteFF = overwriteFF;

    syncxfer = true;
    fromInsycShare = fromInshare;

    setLocalname(clocalname);

    syncThreadSafeState = move(stss);
    syncThreadSafeState->transferBegin(GET, size);
}

SyncDownload_inClient::~SyncDownload_inClient()
{
    if (!wasTerminated && !wasCompleted)
    {
        assert(wasRequesterAbandoned);
        transfer = nullptr;  // don't try to remove File from Transfer from the wrong thread
    }

    if (!wasDistributed && downloadDistributor)
        downloadDistributor->removeTarget();

    if (wasCompleted)
    {
        syncThreadSafeState->transferComplete(GET, size);
    }
    else
    {
        syncThreadSafeState->transferFailed(GET, size);
    }
}

void SyncDownload_inClient::prepare(FileSystemAccess& fsaccess)
{
    if (transfer->localfilename.empty())
    {
        // set unique filename in sync-specific temp download directory
        transfer->localfilename = syncThreadSafeState->syncTmpFolder();
        transfer->localfilename.appendWithSeparator(LocalPath::tmpNameLocal(), true);

    }
}

bool SyncDownload_inClient::failed(error e, MegaClient* mc)
{
    // Squirrel away the error for later use.
    mError = e;

    // Should we retry the download?
    if (File::failed(e, mc))
        return true;

    // MAC validation error?
    if (e == API_EKEY)
        mc->sendevent(99433, "Undecryptable file", 0);

    // TODO: this seems wrong, but is probably just carried over from the old sync logic.  Surely we should stall for this case rather than auto-delete?
    // Blocked file?
    if (e == API_EBLOCKED)
    {
        // Still exists in the cloud?
        if (auto n = mc->nodeByHandle(h))
            mc->movetosyncdebris(n.get(), fromInsycShare, nullptr, syncThreadSafeState->mCanChangeVault);
    }

    return false;
}


#endif
} // namespace
