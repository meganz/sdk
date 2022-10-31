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

namespace mega {

mutex File::localname_mutex;

File::File()
{
    transfer = NULL;
    chatauth = NULL;
    hprivate = true;
    hforeign = false;
    syncxfer = false;
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

bool File::serialize(string *d)
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

    FileFingerprint *fp = FileFingerprint::unserialize(d);
    if (!fp)
    {
        LOG_err << "Error unserializing File: Unable to unserialize FileFingerprint";
        return NULL;
    }

    const char* ptr = d->data();
    const char* end = ptr + d->size();

    if (ptr + sizeof(unsigned short) > end)
    {
        LOG_err << "File unserialization failed - serialized string too short";
        delete fp;
        return NULL;
    }

    // read name
    unsigned short namelen = MemAccess::get<unsigned short>(ptr);
    ptr += sizeof(namelen);
    if (ptr + namelen + sizeof(unsigned short) > end)
    {
        LOG_err << "File unserialization failed - name too long";
        delete fp;
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
        delete fp;
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
        delete fp;
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
        delete fp;
        return NULL;
    }
    const char *privauth = ptr;
    ptr += privauthlen;

    unsigned short pubauthlen = MemAccess::get<unsigned short>(ptr);
    ptr += sizeof(pubauthlen);
    if (ptr + pubauthlen + sizeof(handle) + FILENODEKEYLENGTH + sizeof(bool)
            + sizeof(bool) + sizeof(bool) + 10 > end)
    {
        LOG_err << "File unserialization failed - public auth too long";
        delete fp;
        return NULL;
    }
    const char *pubauth = ptr;
    ptr += pubauthlen;

    File *file = new File();
    *(FileFingerprint *)file = *(FileFingerprint *)fp;
    delete fp;

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
        sendPutnodes(t->client, t->uploadhandle, *t->ultoken, t->filekey, source, NodeHandle(), nullptr, nullptr, false);
    }
}


void File::sendPutnodes(MegaClient* client, UploadHandle fileAttrMatchHandle, const UploadToken& ultoken,
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
    MegaClient::honorPreviousVersionAttrs(previousNode, attrs);

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
        client->putnodes(targetuser.c_str(), move(newnodes), tag, move(completion));
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
            if (Node* ovNode = client->getovnode(client->nodeByHandle(th), &name))
            {
                newnode->ovhandle = ovNode->nodeHandle();
            }
        }

        client->reqs.add(new CommandPutNodes(client,
                                             th, NULL,
                                             mVersioningOption,
                                             move(newnodes),
                                             tag,
                                             source, nullptr, move(completion), canChangeVault));
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
        Node* n;

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

    // Blocked file?
    if (e == API_EBLOCKED)
    {
        // Still exists in the cloud?
        if (auto* n = mc->nodeByHandle(h))
            mc->movetosyncdebris(n, fromInsycShare, nullptr, syncThreadSafeState->mCanChangeVault);
    }

    return false;
}

#endif
} // namespace
