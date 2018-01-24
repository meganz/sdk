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

namespace mega {
File::File()
{
    transfer = NULL;
    hprivate = true;
    hforeign = false;
    syncxfer = false;
    temporaryfile = false;
    h = UNDEF;
    tag = 0;
}

File::~File()
{
    // if transfer currently running, stop
    if (transfer)
    {
        transfer->client->stopxfer(this);
    }
}

bool File::serialize(string *d)
{
    char type = transfer->type;
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

    ll = (unsigned short)localname.size();
    d->append((char*)&ll, sizeof(ll));
    d->append(localname.data(), ll);

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

    d->append("\0\0\0\0\0\0\0\0", 9);

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
    file->localname.assign(localname, localnamelen);
    file->targetuser.assign(targetuser, targetuserlen);
    file->privauth.assign(privauth, privauthlen);
    file->pubauth.assign(pubauth, pubauthlen);

    file->h = MemAccess::get<handle>(ptr);
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

    if (memcmp(ptr, "\0\0\0\0\0\0\0\0", 9))
    {
        LOG_err << "File unserialization failed - invalid version";
        delete file;
        return NULL;
    }
    ptr += 9;

    d->erase(0, ptr - d->data());
    return file;
}

void File::prepare()
{
    transfer->localfilename = localname;
}

void File::start()
{
}

void File::progress()
{
}

void File::completed(Transfer* t, LocalNode* l)
{
    if (t->type == PUT)
    {
        NewNode* newnode = new NewNode[1];

        // build new node
        newnode->source = NEW_UPLOAD;

        // upload handle required to retrieve/include pending file attributes
        newnode->uploadhandle = t->uploadhandle;

        // reference to uploaded file
        memcpy(newnode->uploadtoken, t->ultoken, sizeof newnode->uploadtoken);

        // file's crypto key
        newnode->nodekey.assign((char*)t->filekey, FILENODEKEYLENGTH);
        newnode->type = FILENODE;
        newnode->parenthandle = UNDEF;
#ifdef ENABLE_SYNC
        if ((newnode->localnode = l))
        {
            l->newnode = newnode;
            newnode->syncid = l->syncid;
        }
#endif
        AttrMap attrs;

        // store filename
        attrs.map['n'] = name;

        // store fingerprint
        t->serializefingerprint(&attrs.map['c']);

        string tattrstring;

        attrs.getjson(&tattrstring);

        newnode->attrstring = new string;
        t->client->makeattr(t->transfercipher(), newnode->attrstring, tattrstring.c_str());

        if (targetuser.size())
        {
            // drop file into targetuser's inbox
            int creqtag = t->client->reqtag;
            t->client->reqtag = tag;
            t->client->putnodes(targetuser.c_str(), newnode, 1);
            t->client->reqtag = creqtag;
        }
        else
        {
            handle th = h;

            // inaccessible target folder - use / instead
            if (!t->client->nodebyhandle(th))
            {
                th = t->client->rootnodes[0];
            }
#ifdef ENABLE_SYNC            
            if (l)
            {
                // tag the previous version in the synced folder (if any) or move to SyncDebris
                if (l->node && l->node->parent && l->node->parent->localnode)
                {
                    if (t->client->versions_disabled)
                    {
                        t->client->movetosyncdebris(l->node, l->sync->inshare);
                        t->client->execsyncdeletions();
                    }
                    else
                    {
                        newnode->ovhandle = l->node->nodehandle;
                    }
                }

                t->client->syncadding++;
            }
#endif
            if (!t->client->versions_disabled && ISUNDEF(newnode->ovhandle))
            {
                newnode->ovhandle = t->client->getovhandle(t->client->nodebyhandle(th), &name);
            }

            t->client->reqs.add(new CommandPutNodes(t->client,
                                                                  th, NULL,
                                                                  newnode, 1,
                                                                  tag,
#ifdef ENABLE_SYNC
                                                                  l ? PUTNODES_SYNC : PUTNODES_APP));
#else
                                                                  PUTNODES_APP));
#endif
        }
    }
}

void File::terminated()
{

}

// do not retry crypto errors or administrative takedowns; retry other types of
// failuresup to 16 times, except I/O errors (6 times)
bool File::failed(error e)
{
    if (e == API_EKEY)
    {
        if (!transfer->hascurrentmetamac)
        {
            // several integrity check errors uploading chunks
            return transfer->failcount < 1;
        }

        if (transfer->hasprevmetamac && transfer->prevmetamac == transfer->currentmetamac)
        {
            // integrity check failed after download, two times with the same value
            return false;
        }

        // integrity check failed once, try again
        transfer->prevmetamac = transfer->currentmetamac;
        transfer->hasprevmetamac = true;
        return transfer->failcount < 16;
    }

    return ((e != API_EBLOCKED && e != API_ENOENT && e != API_EINTERNAL && e != API_EACCESS && transfer->failcount < 16)
            && !((e == API_EREAD || e == API_EWRITE) && transfer->failcount > 6))
            || (syncxfer && e != API_EBLOCKED && e != API_EKEY && transfer->failcount <= 8);
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

        if ((n = transfer->client->nodebyhandle(h)))
        {
            *dname = n->displayname();
        }
        else
        {
            *dname = "DELETED/UNAVAILABLE";
        }
    }
}

#ifdef ENABLE_SYNC
SyncFileGet::SyncFileGet(Sync* csync, Node* cn, string* clocalname)
{
    sync = csync;

    n = cn;
    h = n->nodehandle;
    *(FileFingerprint*)this = *n;
    localname = *clocalname;

    syncxfer = true;
    n->syncget = this;
}

SyncFileGet::~SyncFileGet()
{
    if (n)
    {
        n->syncget = NULL;
    }
}

// create sync-specific temp download directory and set unique filename
void SyncFileGet::prepare()
{
    if (!transfer->localfilename.size())
    {
        int i;
        string tmpname, lockname;

        tmpname = "tmp";
        sync->client->fsaccess->name2local(&tmpname);

        if (!sync->tmpfa)
        {
            sync->tmpfa = sync->client->fsaccess->newfileaccess();

            for (i = 3; i--;)
            {
                LOG_verbose << "Creating tmp folder";
                transfer->localfilename = sync->localdebris;
                sync->client->fsaccess->mkdirlocal(&transfer->localfilename, true);

                transfer->localfilename.append(sync->client->fsaccess->localseparator);
                transfer->localfilename.append(tmpname);
                sync->client->fsaccess->mkdirlocal(&transfer->localfilename);

                // lock it
                transfer->localfilename.append(sync->client->fsaccess->localseparator);
                lockname = "lock";
                sync->client->fsaccess->name2local(&lockname);
                transfer->localfilename.append(lockname);

                if (sync->tmpfa->fopen(&transfer->localfilename, false, true))
                {
                    break;
                }
            }

            // if we failed to create the tmp dir three times in a row, fall
            // back to the sync's root
            if (i < 0)
            {
                delete sync->tmpfa;
                sync->tmpfa = NULL;
            }
        }

        if (sync->tmpfa)
        {
            transfer->localfilename = sync->localdebris;
            transfer->localfilename.append(sync->client->fsaccess->localseparator);
            transfer->localfilename.append(tmpname);
        }
        else
        {
            transfer->localfilename = sync->localroot.localname;
        }

        sync->client->fsaccess->tmpnamelocal(&tmpname);
        transfer->localfilename.append(sync->client->fsaccess->localseparator);
        transfer->localfilename.append(tmpname);
    }

    if (n->parent && n->parent->localnode)
    {
        n->parent->localnode->treestate(TREESTATE_SYNCING);
    }
}

bool SyncFileGet::failed(error e)
{
    bool retry = File::failed(e);

    if (n->parent && n->parent->localnode)
    {
        n->parent->localnode->treestate(TREESTATE_PENDING);

        if (!retry && (e == API_EBLOCKED || e == API_EKEY))
        {
            if (e == API_EKEY)
            {
                int creqtag = n->parent->client->reqtag;
                n->parent->client->reqtag = 0;
                n->parent->client->sendevent(99433, "Undecryptable file");
                n->parent->client->reqtag = creqtag;
            }
            n->parent->client->movetosyncdebris(n, n->parent->localnode->sync->inshare);
        }
    }

    return retry;
}

void SyncFileGet::progress()
{
    File::progress();
    if (n->parent && n->parent->localnode && n->parent->localnode->ts != TREESTATE_SYNCING)
    {
        n->parent->localnode->treestate(TREESTATE_SYNCING);
    }
}

// update localname (parent's localnode)
void SyncFileGet::updatelocalname()
{
    attr_map::iterator ait;

    if ((ait = n->attrs.map.find('n')) != n->attrs.map.end())
    {
        if (n->parent && n->parent->localnode)
        {
            string tmpname = ait->second;

            sync->client->fsaccess->name2local(&tmpname);
            n->parent->localnode->getlocalpath(&localname);

            localname.append(sync->client->fsaccess->localseparator);
            localname.append(tmpname);
        }
    }
}

// add corresponding LocalNode (by path), then self-destruct
void SyncFileGet::completed(Transfer*, LocalNode*)
{
    LocalNode *ll = sync->checkpath(NULL, &localname);
    if (ll && ll != (LocalNode*)~0 && n
            && (*(FileFingerprint *)ll) == (*(FileFingerprint *)n))
    {
        LOG_debug << "LocalNode created, associating with remote Node";
        ll->setnode(n);
        ll->treestate(TREESTATE_SYNCED);
        ll->sync->statecacheadd(ll);
        ll->sync->cachenodes();
    }
    delete this;
}

void SyncFileGet::terminated()
{
    delete this;
}
#endif
} // namespace
