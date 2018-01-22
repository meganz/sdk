/**
 * @file transfer.cpp
 * @brief Pending/active up/download ordered by file fingerprint
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

#include "mega/transfer.h"
#include "mega/megaclient.h"
#include "mega/transferslot.h"
#include "mega/megaapp.h"
#include "mega/sync.h"
#include "mega/logging.h"
#include "mega/base64.h"
#include "mega/mediafileattribute.h"
#include "megawaiter.h"

namespace mega {
Transfer::Transfer(MegaClient* cclient, direction_t ctype)
{
    type = ctype;
    client = cclient;
    size = 0;
    failcount = 0;
    uploadhandle = 0;
    minfa = 0;
    pos = 0;
    ctriv = 0;
    metamac = 0;
    tag = 0;
    slot = NULL;
    asyncopencontext = NULL;
    progresscompleted = 0;
    hasprevmetamac = false;
    hascurrentmetamac = false;
    finished = false;
    lastaccesstime = 0;
    ultoken = NULL;

    priority = 0;
    state = TRANSFERSTATE_NONE;

    faputcompletion_it = client->faputcompletion.end();
    transfers_it = client->transfers[type].end();
}

// delete transfer with underlying slot, notify files
Transfer::~Transfer()
{
    if (faputcompletion_it != client->faputcompletion.end())
    {
        client->faputcompletion.erase(faputcompletion_it);
    }

    for (file_list::iterator it = files.begin(); it != files.end(); it++)
    {
        if (finished)
        {
            client->filecachedel(*it);
        }

        (*it)->transfer = NULL;
        (*it)->terminated();
    }

    if (transfers_it != client->transfers[type].end())
    {
        client->transfers[type].erase(transfers_it);
    }
    client->transferlist.removetransfer(this);

    if (slot)
    {
        delete slot;
    }

    if (asyncopencontext)
    {
        delete asyncopencontext;
        asyncopencontext = NULL;
        client->asyncfopens--;
    }

    if (ultoken)
    {
        delete [] ultoken;
    }

    if (finished)
    {
        if (type == GET && localfilename.size())
        {
            client->fsaccess->unlinklocal(&localfilename);
        }
        client->transfercachedel(this);
    }
}

bool Transfer::serialize(string *d)
{
    unsigned short ll;

    d->append((const char*)&type, sizeof(type));

    ll = (unsigned short)localfilename.size();
    d->append((char*)&ll, sizeof(ll));
    d->append(localfilename.data(), ll);

    d->append((const char*)filekey, sizeof(filekey));
    d->append((const char*)&ctriv, sizeof(ctriv));
    d->append((const char*)&metamac, sizeof(metamac));
    d->append((const char*)transferkey, sizeof (transferkey));

    ll = (unsigned short)chunkmacs.size();
    d->append((char*)&ll, sizeof(ll));
    for (chunkmac_map::iterator it = chunkmacs.begin(); it != chunkmacs.end(); it++)
    {
        d->append((char*)&it->first, sizeof(it->first));
        d->append((char*)&it->second, sizeof(it->second));
    }

    if (!FileFingerprint::serialize(d))
    {
        LOG_err << "Error serializing Transfer: Unable to serialize FileFingerprint";
        return false;
    }

    if (!badfp.serialize(d))
    {
        LOG_err << "Error serializing Transfer: Unable to serialize badfp";
        return false;
    }

    d->append((const char*)&lastaccesstime, sizeof(lastaccesstime));

    char hasUltoken;
    if (ultoken)
    {
        hasUltoken = 2;
        d->append((const char*)&hasUltoken, sizeof(char));
        d->append((const char*)ultoken, NewNode::UPLOADTOKENLEN);
    }
    else
    {
        hasUltoken = 0;
        d->append((const char*)&hasUltoken, sizeof(char));
    }

    if (slot)
    {
        ll = (unsigned short)slot->tempurl.size();
        d->append((char*)&ll, sizeof(ll));
        d->append(slot->tempurl.data(), ll);
    }
    else
    {
        ll = (unsigned short)cachedtempurl.size();
        d->append((char*)&ll, sizeof(ll));
        d->append(cachedtempurl.data(), ll);
    }

    char s = state;
    d->append((const char*)&s, sizeof(s));
    d->append((const char*)&priority, sizeof(priority));
    d->append("", 1);
    return true;
}

Transfer *Transfer::unserialize(MegaClient *client, string *d, transfer_map* transfers)
{
    unsigned short ll;
    const char* ptr = d->data();
    const char* end = ptr + d->size();

    if (ptr + sizeof(direction_t) + sizeof(ll) > end)
    {
        LOG_err << "Transfer unserialization failed - serialized string too short (direction)";
        return NULL;
    }

    direction_t type;
    type = MemAccess::get<direction_t>(ptr);
    ptr += sizeof(direction_t);

    ll = MemAccess::get<unsigned short>(ptr);
    ptr += sizeof(ll);

    if (ptr + ll + FILENODEKEYLENGTH + sizeof(int64_t)
            + sizeof(int64_t) + SymmCipher::KEYLENGTH
            + sizeof(ll) > end)
    {
        LOG_err << "Transfer unserialization failed - serialized string too short (filepath)";
        return NULL;
    }

    const char *filepath = ptr;
    ptr += ll;

    Transfer *t = new Transfer(client, type);

    memcpy(t->filekey, ptr, sizeof t->filekey);
    ptr += sizeof(t->filekey);

    t->ctriv = MemAccess::get<int64_t>(ptr);
    ptr += sizeof(int64_t);

    t->metamac = MemAccess::get<int64_t>(ptr);
    ptr += sizeof(int64_t);

    memcpy(t->transferkey, ptr, SymmCipher::KEYLENGTH);
    ptr += SymmCipher::KEYLENGTH;

    t->localfilename.assign(filepath, ll);

    ll = MemAccess::get<unsigned short>(ptr);
    ptr += sizeof(ll);

    if (ptr + ll * (sizeof(m_off_t) + sizeof(ChunkMAC)) + sizeof(ll) > end)
    {
        LOG_err << "Transfer unserialization failed - chunkmacs too long";
        delete t;
        return NULL;
    }

    for (int i = 0; i < ll; i++)
    {
        m_off_t pos = MemAccess::get<m_off_t>(ptr);
        ptr += sizeof(m_off_t);

        memcpy(&(t->chunkmacs[pos]), ptr, sizeof(ChunkMAC));
        ptr += sizeof(ChunkMAC);
    }

    d->erase(0, ptr - d->data());

    FileFingerprint *fp = FileFingerprint::unserialize(d);
    if (!fp)
    {
        LOG_err << "Error unserializing Transfer: Unable to unserialize FileFingerprint";
        delete t;
        return NULL;
    }

    *(FileFingerprint *)t = *(FileFingerprint *)fp;
    delete fp;

    fp = FileFingerprint::unserialize(d);
    t->badfp = *fp;
    delete fp;

    ptr = d->data();
    end = ptr + d->size();

    if (ptr + sizeof(m_time_t) + sizeof(char) > end)
    {
        LOG_err << "Transfer unserialization failed - fingerprint too long";
        delete t;
        return NULL;
    }

    t->lastaccesstime = MemAccess::get<m_time_t>(ptr);
    ptr += sizeof(m_time_t);


    char hasUltoken = MemAccess::get<char>(ptr);
    ptr += sizeof(char);

    ll = hasUltoken ? ((hasUltoken == 1) ? NewNode::OLDUPLOADTOKENLEN + 1 : NewNode::UPLOADTOKENLEN) : 0;
    if (hasUltoken < 0 || hasUltoken > 2
            || (ptr + ll + sizeof(unsigned short) > end))
    {
        LOG_err << "Transfer unserialization failed - invalid ultoken";
        delete t;
        return NULL;
    }

    if (hasUltoken)
    {
        t->ultoken = new byte[NewNode::UPLOADTOKENLEN]();
        memcpy(t->ultoken, ptr, ll);
        ptr += ll;
    }

    ll = MemAccess::get<unsigned short>(ptr);
    ptr += sizeof(ll);

    if (ptr + ll + 10 > end)
    {
        LOG_err << "Transfer unserialization failed - temp URL too long";
        delete t;
        return NULL;
    }

    t->cachedtempurl.assign(ptr, ll);
    ptr += ll;

    char state = MemAccess::get<char>(ptr);
    ptr += sizeof(char);
    if (state == TRANSFERSTATE_PAUSED)
    {
        LOG_debug << "Unserializing paused transfer";
        t->state = TRANSFERSTATE_PAUSED;
    }

    t->priority =  MemAccess::get<uint64_t>(ptr);
    ptr += sizeof(uint64_t);

    if (*ptr)
    {
        LOG_err << "Transfer unserialization failed - invalid version";
        delete t;
        return NULL;
    }
    ptr++;

    for (chunkmac_map::iterator it = t->chunkmacs.begin(); it != t->chunkmacs.end(); it++)
    {
        m_off_t chunkceil = ChunkedHash::chunkceil(it->first, t->size);

        if (t->pos == it->first && it->second.finished)
        {
            t->pos = chunkceil;
            t->progresscompleted = chunkceil;
        }
        else if (it->second.finished)
        {
            m_off_t chunksize = chunkceil - ChunkedHash::chunkfloor(it->first);
            t->progresscompleted += chunksize;
        }
        else
        {
            t->progresscompleted += it->second.offset;
        }
    }

    transfers[type].insert(pair<FileFingerprint*, Transfer*>(t, t));
    return t;
}

SymmCipher *Transfer::transfercipher()
{
    client->tmptransfercipher.setkey(transferkey);
    return &client->tmptransfercipher;
}

// transfer attempt failed, notify all related files, collect request on
// whether to abort the transfer, kill transfer if unanimous
void Transfer::failed(error e, dstime timeleft)
{
    bool defer = false;

    LOG_debug << "Transfer failed with error " << e;

    if (!timeleft || e != API_EOVERQUOTA)
    {
        bt.backoff();
    }
    else
    {
        bt.backoff(timeleft);
        LOG_debug << "backoff: " << timeleft;
        client->overquotauntil = Waiter::ds + timeleft;
    }

    state = TRANSFERSTATE_RETRYING;
    client->looprequested = true;
    client->app->transfer_failed(this, e, timeleft);

    for (file_list::iterator it = files.begin(); it != files.end(); it++)
    {
        if ( (*it)->failed(e)
                || (e == API_ENOENT // putnodes returned -9, file-storage server unavailable
                    && type == PUT
                    && slot && slot->tempurl.empty()
                    && failcount < 16) )
        {
            defer = true;
        }
    }

    cachedtempurl.clear();
    if (type == PUT)
    {
        chunkmacs.clear();
        progresscompleted = 0;
        delete [] ultoken;
        ultoken = NULL;
        pos = 0;

        if (slot && slot->fa && (slot->fa->mtime != mtime || slot->fa->size != size))
        {
            LOG_warn << "Modification detected during active upload";
            defer = false;
        }
    }

    if (defer && !(e == API_EOVERQUOTA && !timeleft))
    {        
        failcount++;
        delete slot;
        slot = NULL;
        client->transfercacheadd(this);

        LOG_debug << "Deferring transfer " << failcount << " during " << (bt.retryin() * 100) << " ms";
    }
    else
    {
        LOG_debug << "Removing transfer";
        state = TRANSFERSTATE_FAILED;
        finished = true;

        for (file_list::iterator it = files.begin(); it != files.end(); it++)
        {
#ifdef ENABLE_SYNC
            if((*it)->syncxfer)
            {
                client->syncdownrequired = true;
            }
#endif
            client->app->file_removed(*it, e);
        }
        client->app->transfer_removed(this);
        delete this;
    }
}

static uint32_t* fileAttributeKeyPtr(byte filekey[FILENODEKEYLENGTH])
{
    // returns the last half, beyond the actual key, ie the nonce+crc
    return (uint32_t*)(filekey + FILENODEKEYLENGTH / 2);
}

void Transfer::addAnyMissingMediaFileAttributes(Node* node, /*const*/ std::string& localpath)
{
    assert(type == PUT || node && node->type == FILENODE);

#ifdef USE_MEDIAINFO
    char ext[8];
    if (((type == PUT && size >= 16) || (node && node->nodekey.size() == FILENODEKEYLENGTH && node->size >= 16)) &&
        client->fsaccess->getextension(&localpath, ext, sizeof(ext)) &&
        MediaProperties::isMediaFilenameExt(ext) &&
        !client->mediaFileInfo.mediaCodecsFailed)
    {
        // for upload, the key is in the transfer.  for download, the key is in the node.
        uint32_t* attrKey = fileAttributeKeyPtr((type == PUT) ? filekey : (byte*)node->nodekey.data());

        if (type == PUT || !node->hasfileattribute(fa_media) || client->mediaFileInfo.timeToRetryMediaPropertyExtraction(node->fileattrstring, attrKey))
        {
            // if we don't have the codec id mappings yet, send the request
            client->mediaFileInfo.requestCodecMappingsOneTime(client, NULL);

            // always get the attribute string; it may indicate this version of the mediaInfo library was unable to interpret the file
            MediaProperties vp;
            vp.extractMediaPropertyFileAttributes(localpath, client->fsaccess);

            if (type == PUT)
            {
                minfa += client->mediaFileInfo.queueMediaPropertiesFileAttributesForUpload(vp, attrKey, client, uploadhandle);
            }
            else
            {
                client->mediaFileInfo.sendOrQueueMediaPropertiesFileAttributesForExistingFile(vp, attrKey, client, node->nodehandle);
            }
        }
    }
#else
    node;
    localpath;
#endif
}

// transfer completion: copy received file locally, set timestamp(s), verify
// fingerprint, notify app, notify files
void Transfer::complete()
{
    state = TRANSFERSTATE_COMPLETING;
    client->app->transfer_update(this);

    if (type == GET)
    {
        LOG_debug << "Download complete: " << (files.size() ? LOG_NODEHANDLE(files.front()->h) : "NO_FILES") << " " << files.size();

        bool transient_error = false;
        string tmplocalname;
        string localname;
        bool success;

        // disconnect temp file from slot...
        delete slot->fa;
        slot->fa = NULL;

        // FIXME: multiple overwrite race conditions below (make copies
        // from open file instead of closing/reopening!)

        // set timestamp (subsequent moves & copies are assumed not to alter mtime)
        success = client->fsaccess->setmtimelocal(&localfilename, mtime);

#ifdef ENABLE_SYNC
        if (!success)
        {
            transient_error = client->fsaccess->transient_error;
            LOG_debug << "setmtimelocal failed " << transient_error;
        }
#endif

        // verify integrity of file
        FileAccess* fa = client->fsaccess->newfileaccess();
        FileFingerprint fingerprint;
        Node* n;
        bool fixfingerprint = false;
        bool fixedfingerprint = false;
        bool syncxfer = false;

        for (file_list::iterator it = files.begin(); it != files.end(); it++)
        {
            if ((*it)->syncxfer)
            {
                syncxfer = true;
            }

            if (!fixedfingerprint && (n = client->nodebyhandle((*it)->h))
                 && !(*(FileFingerprint*)this == *(FileFingerprint*)n))
            {
                LOG_debug << "Wrong fingerprint already fixed";
                fixedfingerprint = true;
            }

            if (syncxfer && fixedfingerprint)
            {
                break;
            }
        }

        if (!fixedfingerprint && success && fa->fopen(&localfilename, true, false))
        {
            fingerprint.genfingerprint(fa);
            if (isvalid && !(fingerprint == *(FileFingerprint*)this))
            {
                LOG_err << "Fingerprint mismatch";

                // enforce the verification of the fingerprint for sync transfers only
                if (syncxfer && (!badfp.isvalid || !(badfp == fingerprint)))
                {
                    badfp = fingerprint;
                    delete fa;
                    chunkmacs.clear();
                    client->fsaccess->unlinklocal(&localfilename);
                    return failed(API_EWRITE);
                }
                else
                {
                    // We consider that mtime is different if the difference is >2
                    // due to the resolution of mtime in some filesystems (like FAT).
                    // This check prevents changes in the fingerprint due to silent
                    // errors in setmtimelocal (returning success but not setting the
                    // modification time) that seem to happen in some Android devices.
                    if (abs(mtime - fingerprint.mtime) <= 2)
                    {
                        fixfingerprint = true;
                    }
                    else
                    {
                        LOG_warn << "Silent failure in setmtimelocal";
                    }
                }
            }
        }
#ifdef ENABLE_SYNC
        else
        {
            if (syncxfer && !fixedfingerprint && success)
            {
                transient_error = fa->retry;
                LOG_debug << "Unable to validate fingerprint " << transient_error;
            }
        }
#endif
        delete fa;

        char me64[12];
        Base64::btoa((const byte*)&client->me, MegaClient::USERHANDLE, me64);

        if (!transient_error)
        {
            if (fingerprint.isvalid)
            {
                // set FileFingerprint on source node(s) if missing
                set<handle> nodes;
                for (file_list::iterator it = files.begin(); it != files.end(); it++)
                {
                    if ((*it)->hprivate && !(*it)->hforeign && (n = client->nodebyhandle((*it)->h))
                            && nodes.find(n->nodehandle) == nodes.end())
                    {
                        nodes.insert(n->nodehandle);

                        if ((!n->isvalid || fixfingerprint)
                                && !(fingerprint == *(FileFingerprint*)n)
                                && fingerprint.size == this->size)
                        {
                            LOG_debug << "Fixing fingerprint";
                            *(FileFingerprint*)n = fingerprint;

                            n->serializefingerprint(&n->attrs.map['c']);
                            client->setattr(n);
                        }
                    }
                }
            }

            // ...and place it in all target locations. first, update the files'
            // local target filenames, in case they have changed during the upload
            for (file_list::iterator it = files.begin(); it != files.end(); it++)
            {
                (*it)->updatelocalname();
            }

            set<string> keys;
            // place file in all target locations - use up to one renames, copy
            // operations for the rest
            // remove and complete successfully completed files
            for (file_list::iterator it = files.begin(); it != files.end(); )
            {
                transient_error = false;
                success = false;
                localname = (*it)->localname;

                if (localname != localfilename)
                {
                    fa = client->fsaccess->newfileaccess();
                    if (fa->fopen(&localname) || fa->type == FOLDERNODE)
                    {
                        // the destination path already exists
        #ifdef ENABLE_SYNC
                        if((*it)->syncxfer)
                        {
                            sync_list::iterator it2;
                            for (it2 = client->syncs.begin(); it2 != client->syncs.end(); it2++)
                            {
                                Sync *sync = (*it2);
                                LocalNode *localNode = sync->localnodebypath(NULL, &localname);
                                if (localNode)
                                {
                                    LOG_debug << "Overwritting a local synced file. Moving the previous one to debris";

                                    // try to move to local debris
                                    if(!sync->movetolocaldebris(&localname))
                                    {
                                        transient_error = client->fsaccess->transient_error;
                                    }

                                    break;
                                }
                            }

                            if(it2 == client->syncs.end())
                            {
                                LOG_err << "LocalNode for destination file not found";

                                if(client->syncs.size())
                                {
                                    // try to move to debris in the first sync
                                    if(!client->syncs.front()->movetolocaldebris(&localname))
                                    {
                                        transient_error = client->fsaccess->transient_error;
                                    }
                                }
                            }
                        }
                        else
        #endif
                        {
                            LOG_debug << "The destination file exist (not synced). Saving with a different name";

                            // the destination path isn't synced, save with a (x) suffix
                            string utf8fullname;
                            client->fsaccess->local2path(&localname, &utf8fullname);
                            size_t dotindex = utf8fullname.find_last_of('.');
                            string name;
                            string extension;
                            if (dotindex == string::npos)
                            {
                                name = utf8fullname;
                            }
                            else
                            {
                                string separator;
                                client->fsaccess->local2path(&client->fsaccess->localseparator, &separator);
                                size_t sepindex = utf8fullname.find_last_of(separator);
                                if(sepindex == string::npos || sepindex < dotindex)
                                {
                                    name = utf8fullname.substr(0, dotindex);
                                    extension = utf8fullname.substr(dotindex);
                                }
                                else
                                {
                                    name = utf8fullname;
                                }
                            }

                            string suffix;
                            string newname;
                            string localnewname;
                            int num = 0;
                            do
                            {
                                num++;
                                ostringstream oss;
                                oss << " (" << num << ")";
                                suffix = oss.str();
                                newname = name + suffix + extension;
                                client->fsaccess->path2local(&newname, &localnewname);
                            } while (fa->fopen(&localnewname) || fa->type == FOLDERNODE);


                            (*it)->localname = localnewname;
                            localname = localnewname;
                        }
                    }
                    else
                    {
                        transient_error = fa->retry;
                    }

                    delete fa;

                    if (transient_error)
                    {
                        LOG_warn << "Transient error checking if the destination file exist";
                        it++;
                        continue;
                    }
                }

                if (files.size() == 1 && !tmplocalname.size())
                {
                    if (localfilename != localname)
                    {
                        LOG_debug << "Renaming temporary file to target path";
                        if (client->fsaccess->renamelocal(&localfilename, &localname))
                        {
                            tmplocalname = localname;
                            success = true;
                        }
                        else if (client->fsaccess->transient_error)
                        {
                            transient_error = true;
                        }
                    }
                    else
                    {
                        tmplocalname = localname;
                        success = true;
                    }
                }

                if (!success)
                {
                    if((tmplocalname.size() ? tmplocalname : localfilename) == localname)
                    {
                        LOG_debug << "Identical node downloaded to the same folder";
                        success = true;
                    }
                    else if (client->fsaccess->copylocal(tmplocalname.size() ? &tmplocalname : &localfilename,
                                                   &localname, mtime))
                    {
                        success = true;
                    }
                    else if (client->fsaccess->transient_error)
                    {
                        transient_error = true;
                    }
                }

                if (success)
                {
                    // set missing node attributes
                    if ((*it)->hprivate && !(*it)->hforeign && (n = client->nodebyhandle((*it)->h)))
                    {
                        if (client->gfx && client->gfx->isgfx(&localname) &&
                                keys.find(n->nodekey) == keys.end() &&    // this file hasn't been processed yet
                                client->checkaccess(n, OWNER))
                        {
                            keys.insert(n->nodekey);

                            // check if restoration of missing attributes failed in the past (no access)
                            if (n->attrs.map.find('f') == n->attrs.map.end() || n->attrs.map['f'] != me64)
                            {
                                // check for missing imagery
                                int missingattr = 0;
                                if (!n->hasfileattribute(GfxProc::THUMBNAIL)) missingattr |= 1 << GfxProc::THUMBNAIL;
                                if (!n->hasfileattribute(GfxProc::PREVIEW)) missingattr |= 1 << GfxProc::PREVIEW;

                                if (missingattr)
                                {
                                    client->gfx->gendimensionsputfa(NULL, &localname, n->nodehandle, n->nodecipher(), missingattr);
                                }

                                addAnyMissingMediaFileAttributes(n, localname);
                            }
                        }
                    }
                }

                if (success || !transient_error)
                {
                    if (success)
                    {
                        // prevent deletion of associated Transfer object in completed()
                        client->filecachedel(*it);
                        client->app->file_complete(*it);
                        (*it)->transfer = NULL;
                        (*it)->completed(this, NULL);
                    }

                    if (success || !(*it)->failed(API_EAGAIN))
                    {
                        File* f = (*it);
                        files.erase(it++);
                        if (!success)
                        {
                            LOG_warn << "Unable to complete transfer due to a persistent error";
                            client->filecachedel(f);
#ifdef ENABLE_SYNC
                            if (f->syncxfer)
                            {
                                client->syncdownrequired = true;
                            }
#endif
                            client->app->file_removed(f, API_EWRITE);
                            f->transfer = NULL;
                            f->terminated();
                        }
                    }
                    else
                    {
                        failcount++;
                        LOG_debug << "Persistent error completing file. Failcount: " << failcount;
                        it++;
                    }
                }
                else
                {
                    LOG_debug << "Transient error completing file";
                    it++;
                }
            }

            if (!tmplocalname.size() && !files.size())
            {
                client->fsaccess->unlinklocal(&localfilename);
            }
        }

        if (!files.size())
        {
            state = TRANSFERSTATE_COMPLETED;
            localfilename = localname;
            finished = true;
            client->looprequested = true;
            client->app->transfer_complete(this);
            localfilename.clear();
            delete this;
        }
        else
        {
            // some files are still pending completion, close fa and set retry timer
            delete slot->fa;
            slot->fa = NULL;

            LOG_debug << "Files pending completion: " << files.size() << ". Waiting for a retry.";
            LOG_debug << "First pending file: " << files.front()->name;

            slot->retrying = true;
            slot->retrybt.backoff(11);
        }
    }
    else
    {
        LOG_debug << "Upload complete: " << (files.size() ? files.front()->name : "NO_FILES") << " " << files.size();
        delete slot->fa;
        slot->fa = NULL;

        // files must not change during a PUT transfer
        for (file_list::iterator it = files.begin(); it != files.end(); )
        {
            File *f = (*it);
            bool isOpen = true;
            FileAccess *fa = client->fsaccess->newfileaccess();
            string *localpath = &f->localname;

#ifdef ENABLE_SYNC
            string synclocalpath;
            LocalNode *ll = dynamic_cast<LocalNode *>(f);
            if (ll)
            {
                LOG_debug << "Verifying sync upload";
                ll->getlocalpath(&synclocalpath, true);
                localpath = &synclocalpath;
            }
            else
            {
                LOG_debug << "Verifying regular upload";
            }
#endif

            if (!fa->fopen(localpath))
            {
                isOpen = false;
                if (client->fsaccess->transient_error)
                {
                    LOG_warn << "Retrying upload completion due to a transient error";
                    slot->retrying = true;
                    slot->retrybt.backoff(11);
                    delete fa;
                    return;
                }
            }

            if (!isOpen || f->genfingerprint(fa))
            {
                if (!isOpen)
                {
                    LOG_warn << "Deletion detected after upload";
                }
                else
                {
                    LOG_warn << "Modification detected after upload";
                }

#ifdef ENABLE_SYNC
                if (f->syncxfer)
                {
                    client->syncdownrequired = true;
                }
#endif
                client->filecachedel(f);
                files.erase(it++);
                client->app->file_removed(f, API_EREAD);
                f->transfer = NULL;
                f->terminated();
            }
            else
            {
                it++;
            }
            delete fa;
        }

        if (!files.size())
        {
            return failed(API_EREAD);
        }


        // prepare file attributes for video/audio files if the file is suitable
        addAnyMissingMediaFileAttributes(NULL, localfilename);

        // if this transfer is put on hold, do not complete
        client->checkfacompletion(uploadhandle, this);
        return;
    }
}

void Transfer::completefiles()
{
    // notify all files and give them an opportunity to self-destruct
    vector<uint32_t> &ids = client->pendingtcids[tag];
    vector<string> *pfs = NULL;

    for (file_list::iterator it = files.begin(); it != files.end(); )
    {
        File *f = (*it);
        ids.push_back(f->dbid);
        if (f->temporaryfile)
        {
            if (!pfs)
            {
                pfs = &client->pendingfiles[tag];
            }
            pfs->push_back(f->localname);
        }

        client->app->file_complete(f);
        f->transfer = NULL;
        f->completed(this, NULL);
        files.erase(it++);
    }
    ids.push_back(dbid);
}

m_off_t Transfer::nextpos()
{
    while (chunkmacs.find(ChunkedHash::chunkfloor(pos)) != chunkmacs.end())
    {    
        if (chunkmacs[ChunkedHash::chunkfloor(pos)].finished)
        {
            pos = ChunkedHash::chunkceil(pos);
        }
        else
        {
            pos += chunkmacs[ChunkedHash::chunkfloor(pos)].offset;
            break;
        }
    }

    return pos;
}

DirectReadNode::DirectReadNode(MegaClient* cclient, handle ch, bool cp, SymmCipher* csymmcipher, int64_t cctriv)
{
    client = cclient;

    p = cp;
    h = ch;

    symmcipher = *csymmcipher;
    ctriv = cctriv;

    retries = 0;
    size = 0;
    
    pendingcmd = NULL;
    
    dsdrn_it = client->dsdrns.end();
}

DirectReadNode::~DirectReadNode()
{
    schedule(NEVER);

    if (pendingcmd)
    {
        pendingcmd->cancel();
    }

    for (dr_list::iterator it = reads.begin(); it != reads.end(); )
    {
        delete *(it++);
    }
    
    client->hdrns.erase(hdrn_it);
}

void DirectReadNode::dispatch()
{    
    if (reads.empty())
    {
        LOG_debug << "Removing DirectReadNode";
        delete this;
    }
    else
    {
        for (dr_list::iterator it = reads.begin(); it != reads.end(); it++)
        {
            assert((*it)->drq_it == client->drq.end());
            assert(!(*it)->drs);
        }

        schedule(DirectReadSlot::TIMEOUT_DS);
        if (!pendingcmd)
        {
            pendingcmd = new CommandDirectRead(client, this);
            client->reqs.add(pendingcmd);
        }
    }
}

// abort all active reads, remove pending reads and reschedule with app-supplied backoff
void DirectReadNode::retry(error e, dstime timeleft)
{
    if (reads.empty())
    {
        LOG_warn << "Removing DirectReadNode. No reads to retry.";
        delete this;
        return;
    }

    dstime minretryds = NEVER;

    retries++;

    LOG_warn << "Streaming transfer retry due to error " << e;
    if (client->autodownport)
    {
        client->usealtdownport = !client->usealtdownport;
    }

    // signal failure to app , obtain minimum desired retry time
    for (dr_list::iterator it = reads.begin(); it != reads.end(); it++)
    {
        (*it)->abort();

        if (e)
        {
            dstime retryds = client->app->pread_failure(e, retries, (*it)->appdata, timeleft);

            if (retryds < minretryds)
            {
                minretryds = retryds;
            }
        }
    }

    if (e == API_EOVERQUOTA && timeleft)
    {
        // don't retry at least until the end of the overquota state
        client->overquotauntil = Waiter::ds + timeleft;
        if (minretryds < timeleft)
        {
            minretryds = timeleft;
        }
    }

    tempurl.clear();

    if (!e || !minretryds)
    {
        // immediate retry desired
        dispatch();        
    }
    else
    {
        if (EVER(minretryds))
        {
            // delayed retry desired
            schedule(minretryds);
        }
        else
        {
            // cancellation desired
            LOG_debug << "Removing DirectReadNode. Too many errors.";
            delete this;
        }
    }
}

void DirectReadNode::cmdresult(error e, dstime timeleft)
{
    pendingcmd = NULL;

    if (e == API_OK)
    {
        // feed all pending reads to the global read queue
        for (dr_list::iterator it = reads.begin(); it != reads.end(); it++)
        {
            assert((*it)->drq_it == client->drq.end());
            (*it)->drq_it = client->drq.insert(client->drq.end(), *it);
        }

        schedule(DirectReadSlot::TIMEOUT_DS);
    }
    else
    {
        retry(e, timeleft);
    }
}

void DirectReadNode::schedule(dstime deltads)
{            
    WAIT_CLASS::bumpds();
    if (dsdrn_it != client->dsdrns.end())
    {
        client->dsdrns.erase(dsdrn_it);
    }

    if (EVER(deltads))
    {
        dsdrn_it = client->dsdrns.insert(pair<dstime, DirectReadNode*>(Waiter::ds + deltads, this));
    }
    else
    {
        dsdrn_it = client->dsdrns.end();
    }
}

void DirectReadNode::enqueue(m_off_t offset, m_off_t count, int reqtag, void* appdata)
{
    new DirectRead(this, count, offset, reqtag, appdata);
}

bool DirectReadSlot::doio()
{
    if (req->status == REQ_INFLIGHT || req->status == REQ_SUCCESS)
    {
        if (req->in.size())
        {
            int r, l, t;
            byte buf[SymmCipher::BLOCKSIZE];

            // decrypt, pass to app and erase
            r = pos & (sizeof buf - 1);
            t = req->in.size();

            dr->drn->schedule(DirectReadSlot::TIMEOUT_DS);

            if (r)
            {
                l = sizeof buf - r;

                if (l > t)
                {
                    l = t;
                }

                memcpy(buf + r, req->in.data(), l);
                dr->drn->symmcipher.ctr_crypt(buf, sizeof buf, pos - r, dr->drn->ctriv, NULL, false);
                memcpy((char*)req->in.data(), buf + r, l);
            }
            else
            {
                l = 0;
            }

            if (t > l)
            {
                r = (l - t) & (sizeof buf - 1);
                req->in.resize(t + r);
                dr->drn->symmcipher.ctr_crypt((byte*)req->in.data() + l, req->in.size() - l, pos + l, dr->drn->ctriv, NULL, false);
            }

            if (req->httpio)
            {
                req->httpio->lastdata = Waiter::ds;
                req->lastdata = Waiter::ds;
            }

            speed = speedController.calculateSpeed(t);
            meanSpeed = speedController.getMeanSpeed();
            dr->drn->client->httpio->updatedownloadspeed(t);
            if (dr->drn->client->app->pread_data((byte*)req->in.data(), t, pos, speed, meanSpeed, dr->appdata))
            {
                pos += t;
                dr->drn->partiallen += t;
                dr->progress += t;

                req->in.clear();
                req->contentlength -= t;
                req->bufpos = 0;               
            }
            else
            {
                // app-requested abort
                delete dr;
                return true;
            }
        }

        if (req->status == REQ_SUCCESS)
        {
            dr->drn->schedule(DirectReadSlot::TEMPURL_TIMEOUT_DS);

            // remove and delete completed read request, then remove slot
            delete dr;
            return true;
        }
    }
    else if (req->status == REQ_FAILURE)
    {
        if (req->httpstatus == 509)
        {
            if (req->timeleft < 0)
            {
                int creqtag = dr->drn->client->reqtag;
                dr->drn->client->reqtag = 0;
                dr->drn->client->sendevent(99408, "Overquota without timeleft");
                dr->drn->client->reqtag = creqtag;
            }

            dstime backoff;

            LOG_warn << "Bandwidth overquota from storage server for streaming transfer";
            if (req->timeleft > 0)
            {
                backoff = dstime(req->timeleft * 10);
            }
            else
            {
                // default retry interval
                backoff = MegaClient::DEFAULT_BW_OVERQUOTA_BACKOFF_SECS * 10;
            }

            dr->drn->retry(API_EOVERQUOTA, backoff);
        }
        else
        {
            // a failure triggers a complete abort and retry of all pending reads for this node
            dr->drn->retry(API_EREAD);
        }
        return true;
    }

    if (Waiter::ds - dr->drn->partialstarttime > MEAN_SPEED_INTERVAL_DS)
    {
        m_off_t meanspeed = (10 * dr->drn->partiallen) / (Waiter::ds - dr->drn->partialstarttime);

        LOG_debug << "Mean speed (B/s): " << meanspeed;
        if (meanspeed < MIN_BYTES_PER_SECOND)
        {
            LOG_warn << "Transfer speed too low for streaming. Retrying";
            dr->drn->retry(API_EAGAIN);
            return true;
        }
        else
        {
            dr->drn->partiallen = 0;
            dr->drn->partialstarttime = Waiter::ds;
        }
    }

    return false;
}

// abort active read, remove from pending queue
void DirectRead::abort()
{
    delete drs;
    drs = NULL;

    if (drq_it != drn->client->drq.end())
    {
        drn->client->drq.erase(drq_it);
        drq_it = drn->client->drq.end();
    }
}

DirectRead::DirectRead(DirectReadNode* cdrn, m_off_t ccount, m_off_t coffset, int creqtag, void* cappdata)
{
    drn = cdrn;

    count = ccount;
    offset = coffset;
    progress = 0;
    reqtag = creqtag;
    appdata = cappdata;

    drs = NULL;

    reads_it = drn->reads.insert(drn->reads.end(), this);
    
    if (drn->tempurl.size())
    {
        // we already have a tempurl: queue for immediate fetching
        drq_it = drn->client->drq.insert(drn->client->drq.end(), this);
    }
    else
    {
        // no tempurl yet or waiting for a retry
        drq_it = drn->client->drq.end();
    }
}

DirectRead::~DirectRead()
{
    abort();

    if (reads_it != drn->reads.end())
    {
        drn->reads.erase(reads_it);
    }
}

// request DirectRead's range via tempurl
DirectReadSlot::DirectReadSlot(DirectRead* cdr)
{
    char buf[128];

    dr = cdr;

    pos = dr->offset + dr->progress;

    speed = meanSpeed = 0;

    req = new HttpReq(true);

    sprintf(buf,"/%" PRIu64 "-", pos);

    if (dr->count)
    {
        sprintf(strchr(buf, 0), "%" PRIu64, dr->offset + dr->count - 1);
    }

    dr->drn->partiallen = 0;
    dr->drn->partialstarttime = Waiter::ds;
    req->posturl = dr->drn->tempurl;
    if (!memcmp(req->posturl.c_str(), "http:", 5))
    {
        size_t portendindex = req->posturl.find("/", 8);
        size_t portstartindex = req->posturl.find(":", 8);

        if (portendindex != string::npos)
        {
            if (portstartindex == string::npos)
            {
                if (dr->drn->client->usealtdownport)
                {
                    LOG_debug << "Enabling alternative port for streaming transfer";
                    req->posturl.insert(portendindex, ":8080");
                }
            }
            else
            {
                if (!dr->drn->client->usealtdownport)
                {
                    LOG_debug << "Disabling alternative port for streaming transfer";
                    req->posturl.erase(portstartindex, portendindex - portstartindex);
                }
            }
        }
    }

    req->posturl.append(buf);
    req->type = REQ_BINARY;

    LOG_debug << "POST URL: " << req->posturl;
    req->post(dr->drn->client);

    drs_it = dr->drn->client->drss.insert(dr->drn->client->drss.end(), this);
}

DirectReadSlot::~DirectReadSlot()
{
    dr->drn->client->drss.erase(drs_it);

    LOG_debug << "Deleting DirectReadSlot";
    delete req;
}

bool priority_comparator(Transfer* i, Transfer *j)
{
    return (i->priority < j->priority);
}

TransferList::TransferList()
{
    currentpriority = PRIORITY_START;
}

void TransferList::addtransfer(Transfer *transfer)
{
    if (transfer->state != TRANSFERSTATE_PAUSED)
    {
        transfer->state = TRANSFERSTATE_QUEUED;
    }

    if (!transfer->priority)
    {
        currentpriority += PRIORITY_STEP;
        transfer->priority = currentpriority;
        assert(!transfers[transfer->type].size() || transfers[transfer->type][transfers[transfer->type].size() - 1]->priority < transfer->priority);
        transfers[transfer->type].push_back(transfer);
        client->transfercacheadd(transfer);
    }
    else
    {
        transfer_list::iterator it = std::lower_bound(transfers[transfer->type].begin(), transfers[transfer->type].end(), transfer, priority_comparator);
        assert(it == transfers[transfer->type].end() || (*it)->priority != transfer->priority);
        transfers[transfer->type].insert(it, transfer);
    }
}

void TransferList::removetransfer(Transfer *transfer)
{
    transfer_list::iterator it = iterator(transfer);
    if (it != transfers[transfer->type].end())
    {
        transfers[transfer->type].erase(it);
    }
}

void TransferList::movetransfer(Transfer *transfer, Transfer *prevTransfer)
{
    transfer_list::iterator dstit = iterator(prevTransfer);
    if (dstit == transfers[prevTransfer->type].end())
    {
        return;
    }
    movetransfer(transfer, dstit);
}

void TransferList::movetransfer(Transfer *transfer, unsigned int position)
{
    transfer_list::iterator it = iterator(transfer);
    if (it == transfers[transfer->type].end())
    {
        return;
    }

    transfer_list::iterator dstit;
    if (position >= transfers[transfer->type].size())
    {
        dstit = transfers[transfer->type].end();
    }
    else
    {
        dstit = transfers[transfer->type].begin() + position;
    }

    movetransfer(it, dstit);
}

void TransferList::movetransfer(Transfer *transfer, transfer_list::iterator dstit)
{
    transfer_list::iterator it = iterator(transfer);
    if (it == transfers[transfer->type].end())
    {
        return;
    }
    movetransfer(it, dstit);
}

void TransferList::movetransfer(transfer_list::iterator it, transfer_list::iterator dstit)
{
    if (it == dstit)
    {
        LOG_warn << "Trying to move before the same transfer";
        return;
    }

    if ((it + 1) == dstit)
    {
        LOG_warn << "Trying to move to the same position";
        return;
    }

    Transfer *transfer = (*it);
    if (dstit == transfers[transfer->type].end())
    {
        LOG_debug << "Moving transfer to the last position";
        prepareDecreasePriority(transfer, it, dstit);

        transfers[transfer->type].erase(it);
        currentpriority += PRIORITY_STEP;
        transfer->priority = currentpriority;
        assert(!transfers[transfer->type].size() || transfers[transfer->type][transfers[transfer->type].size() - 1]->priority < transfer->priority);
        transfers[transfer->type].push_back(transfer);
        client->transfercacheadd(transfer);
        client->app->transfer_update(transfer);
        return;
    }

    int srcindex = std::distance(transfers[transfer->type].begin(), it);
    int dstindex = std::distance(transfers[transfer->type].begin(), dstit);
    LOG_debug << "Moving transfer from " << srcindex << " to " << dstindex;

    uint64_t prevpriority = 0;
    uint64_t nextpriority = 0;

    nextpriority = (*dstit)->priority;
    if (dstit != transfers[transfer->type].begin())
    {
        transfer_list::iterator previt = dstit - 1;
        prevpriority = (*previt)->priority;
    }
    else
    {
        prevpriority = nextpriority - 2 * PRIORITY_STEP;
    }

    uint64_t newpriority = (prevpriority + nextpriority) / 2;
    LOG_debug << "Moving transfer between priority " << prevpriority << " and " << nextpriority << ". New: " << newpriority;
    if (prevpriority == newpriority)
    {
        LOG_warn << "There is no space for the move. Adjusting priorities.";
        int positions = dstindex;
        uint64_t fixedPriority = transfers[transfer->type][0]->priority - PRIORITY_STEP * (positions + 1);
        for (int i = 0; i < positions; i++)
        {
            Transfer *t = transfers[transfer->type][i];
            LOG_debug << "Adjusting priority of transfer " << i << " to " << fixedPriority;
            t->priority = fixedPriority;
            client->transfercacheadd(t);
            client->app->transfer_update(t);
            fixedPriority += PRIORITY_STEP;
        }
        newpriority = fixedPriority;
        LOG_debug << "Fixed priority: " << fixedPriority;
    }

    transfer->priority = newpriority;
    if (srcindex > dstindex)
    {
        prepareIncreasePriority(transfer, it, dstit);
    }
    else
    {
        prepareDecreasePriority(transfer, it, dstit);
        dstindex--;
    }

    transfers[transfer->type].erase(it);
    transfer_list::iterator fit = transfers[transfer->type].begin() + dstindex;
    assert(fit == transfers[transfer->type].end() || (*fit)->priority != transfer->priority);
    transfers[transfer->type].insert(fit, transfer);
    client->transfercacheadd(transfer);
    client->app->transfer_update(transfer);
}

void TransferList::movetofirst(Transfer *transfer)
{
    movetransfer(transfer, transfers[transfer->type].begin());
}

void TransferList::movetofirst(transfer_list::iterator it)
{
    Transfer *transfer = (*it);
    movetransfer(it, transfers[transfer->type].begin());
}

void TransferList::movetolast(Transfer *transfer)
{
    movetransfer(transfer, transfers[transfer->type].end());
}

void TransferList::movetolast(transfer_list::iterator it)
{
    Transfer *transfer = (*it);
    movetransfer(it, transfers[transfer->type].end());
}

void TransferList::moveup(Transfer *transfer)
{
    transfer_list::iterator it = iterator(transfer);
    if (it == transfers[transfer->type].begin())
    {
        return;
    }
    transfer_list::iterator dstit = it - 1;
    movetransfer(it, dstit);
}

void TransferList::moveup(transfer_list::iterator it)
{
    if (it == transfers[(*it)->type].begin())
    {
        return;
    }

    transfer_list::iterator dstit = it - 1;
    movetransfer(it, dstit);
}

void TransferList::movedown(Transfer *transfer)
{
    transfer_list::iterator it = iterator(transfer);
    if (it == transfers[transfer->type].end())
    {
        return;
    }

    transfer_list::iterator dstit = it + 1;
    if (dstit == transfers[transfer->type].end())
    {
        return;
    }

    dstit++;
    movetransfer(it, dstit);
}

void TransferList::movedown(transfer_list::iterator it)
{
    if (it == transfers[(*it)->type].end())
    {
        return;
    }

    transfer_list::iterator dstit = it + 1;
    movetransfer(it, dstit);
}

error TransferList::pause(Transfer *transfer, bool enable)
{
    if (!transfer)
    {
        return API_ENOENT;
    }

    if ((enable && transfer->state == TRANSFERSTATE_PAUSED) ||
            (!enable && transfer->state != TRANSFERSTATE_PAUSED))
    {
        return API_OK;
    }

    if (!enable)
    {
        transfer_list::iterator it = iterator(transfer);
        transfer->state = TRANSFERSTATE_QUEUED;
        prepareIncreasePriority(transfer, it, it);
        client->transfercacheadd(transfer);
        client->app->transfer_update(transfer);
        return API_OK;
    }

    if (transfer->state == TRANSFERSTATE_ACTIVE
            || transfer->state == TRANSFERSTATE_QUEUED
            || transfer->state == TRANSFERSTATE_RETRYING)
    {
        if (transfer->slot)
        {
            transfer->bt.arm();
            transfer->cachedtempurl = transfer->slot->tempurl;
            delete transfer->slot;
        }
        transfer->state = TRANSFERSTATE_PAUSED;
        client->transfercacheadd(transfer);
        client->app->transfer_update(transfer);
        return API_OK;
    }

    return API_EFAILED;
}

transfer_list::iterator TransferList::begin(direction_t direction)
{
    return transfers[direction].begin();
}

transfer_list::iterator TransferList::end(direction_t direction)
{
    return transfers[direction].end();
}

transfer_list::iterator TransferList::iterator(Transfer *transfer)
{
    if (!transfer)
    {
        LOG_err << "Getting iterator of a NULL transfer";
        return transfer_list::iterator();
    }

    transfer_list::iterator it = std::lower_bound(transfers[transfer->type].begin(), transfers[transfer->type].end(), transfer, priority_comparator);
    if (it != transfers[transfer->type].end() && (*it) == transfer)
    {
        return it;
    }
    LOG_debug << "Transfer not found";
    return transfers[transfer->type].end();
}

Transfer *TransferList::nexttransfer(direction_t direction)
{
    for (transfer_list::iterator it = transfers[direction].begin(); it != transfers[direction].end(); it++)
    {
        Transfer *transfer = (*it);
        if ((!transfer->slot && isReady(transfer))
                || (transfer->asyncopencontext
                    && transfer->asyncopencontext->finished))
        {
            return transfer;
        }
    }
    return NULL;
}

Transfer *TransferList::transferat(direction_t direction, unsigned int position)
{
    if (transfers[direction].size() > position)
    {
        return transfers[direction][position];
    }
    return NULL;
}

void TransferList::prepareIncreasePriority(Transfer *transfer, transfer_list::iterator srcit, transfer_list::iterator dstit)
{
    if (dstit == transfers[transfer->type].end())
    {
        return;
    }

    if (!transfer->slot && transfer->state != TRANSFERSTATE_PAUSED)
    {
        Transfer *lastActiveTransfer = NULL;
        for (transferslot_list::iterator it = client->tslots.begin(); it != client->tslots.end(); it++)
        {
            Transfer *t = (*it)->transfer;
            if (t && t->type == transfer->type && t->slot
                    && t->state == TRANSFERSTATE_ACTIVE
                    && t->priority > transfer->priority
                    && (!lastActiveTransfer || t->priority > lastActiveTransfer->priority))
            {
                lastActiveTransfer = t;
            }
        }

        if (lastActiveTransfer)
        {
            lastActiveTransfer->bt.arm();
            lastActiveTransfer->cachedtempurl = lastActiveTransfer->slot->tempurl;
            delete lastActiveTransfer->slot;
            lastActiveTransfer->state = TRANSFERSTATE_QUEUED;
            client->transfercacheadd(lastActiveTransfer);
            client->app->transfer_update(lastActiveTransfer);
        }
    }
}

void TransferList::prepareDecreasePriority(Transfer *transfer, transfer_list::iterator it, transfer_list::iterator dstit)
{
    if (transfer->slot && transfer->state == TRANSFERSTATE_ACTIVE)
    {
        transfer_list::iterator cit = it + 1;
        while (cit != transfers[transfer->type].end())
        {
            if (!(*cit)->slot && isReady(*cit))
            {
                transfer->bt.arm();
                transfer->cachedtempurl = (*it)->slot->tempurl;
                delete transfer->slot;
                transfer->state = TRANSFERSTATE_QUEUED;
                break;
            }

            if (cit == dstit)
            {
                break;
            }

            cit++;
        }
    }
}

bool TransferList::isReady(Transfer *transfer)
{
    return ((transfer->state == TRANSFERSTATE_QUEUED || transfer->state == TRANSFERSTATE_RETRYING)
            && transfer->bt.armed());
}

} // namespace
