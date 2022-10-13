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
#include "mega/utils.h"
#include "mega/testhooks.h"

namespace mega {

TransferCategory::TransferCategory(direction_t d, filesizetype_t s)
    : direction(d)
    , sizetype(s)
{
}

TransferCategory::TransferCategory(Transfer* t)
    : direction(t->type)
    , sizetype(t->size > 131072 ? LARGEFILE : SMALLFILE)  // Conservative starting point: 131072 is the smallest chunk, we will certainly only use one socket to upload/download
{
}

unsigned TransferCategory::index()
{
    assert(direction == GET || direction == PUT);
    assert(sizetype == LARGEFILE || sizetype == SMALLFILE);
    return 2 + direction * 2 + sizetype;
}

unsigned TransferCategory::directionIndex()
{
    assert(direction == GET || direction == PUT);
    return direction;
}

Transfer::Transfer(MegaClient* cclient, direction_t ctype)
    : bt(cclient->rng, cclient->transferRetryBackoffs[ctype])
{
    type = ctype;
    client = cclient;
    size = 0;
    failcount = 0;
    pos = 0;
    ctriv = 0;
    metamac = 0;
    tag = 0;
    slot = NULL;
    asyncopencontext = NULL;
    progresscompleted = 0;
    finished = false;
    lastaccesstime = 0;
    ultoken = NULL;

    priority = 0;
    state = TRANSFERSTATE_NONE;

    skipserialization = false;

    transfers_it = client->transfers[type].end();
}

// delete transfer with underlying slot, notify files
Transfer::~Transfer()
{
    TransferDbCommitter* committer = client->tctable ?
                         static_cast<TransferDbCommitter*>(client->tctable->getTransactionCommitter()) :
                         nullptr;

    if (!uploadhandle.isUndef())
    {
        client->fileAttributesUploading.erase(uploadhandle);
    }

    for (file_list::iterator it = files.begin(); it != files.end(); it++)
    {
        if (finished)
        {
            client->filecachedel(*it, nullptr);
        }

        (*it)->transfer = NULL;
        (*it)->terminated(API_OK);
    }

    if (!mOptimizedDelete)
    {
        if (transfers_it != client->transfers[type].end())
        {
            client->transfers[type].erase(transfers_it);
        }

        client->transferlist.removetransfer(this);
    }

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

    if (finished)
    {
        if (type == GET && !localfilename.empty())
        {
            client->fsaccess->unlinklocal(localfilename);
        }
        client->transfercachedel(this, committer);
    }
}

bool Transfer::serialize(string *d)
{
    unsigned short ll;

    d->append((const char*)&type, sizeof(type));

    const auto& tmpstr = localfilename.platformEncoded();
    ll = (unsigned short)tmpstr.size();
    d->append((char*)&ll, sizeof(ll));
    d->append(tmpstr.data(), ll);

    d->append((const char*)&filekey.bytes, sizeof(filekey.bytes));
    d->append((const char*)&ctriv, sizeof(ctriv));
    d->append((const char*)&metamac, sizeof(metamac));
    d->append((const char*)transferkey.data(), sizeof (transferkey));

    chunkmacs.serialize(*d);

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
        d->append((const char*)ultoken.get(), UPLOADTOKENLEN);
    }
    else
    {
        hasUltoken = 0;
        d->append((const char*)&hasUltoken, sizeof(char));
    }

    // store raid URL string(s) in the same record as non-raid, 0-delimited in the case of raid
    std::string combinedUrls;
    for (std::vector<std::string>::const_iterator i = tempurls.begin(); i != tempurls.end(); ++i)
    {
        combinedUrls.append("", i == tempurls.begin() ? 0 : 1); // '\0' separator
        combinedUrls.append(*i);
    }
    ll = (unsigned short)combinedUrls.size();
    d->append((char*)&ll, sizeof(ll));
    d->append(combinedUrls.data(), ll);

    char s = static_cast<char>(state);
    d->append((const char*)&s, sizeof(s));
    d->append((const char*)&priority, sizeof(priority));
    d->append("", 1);

#ifdef DEBUG
    // very quick debug only double check
    string tempstr = *d;
    transfer_map tempmap[2];
    unique_ptr<Transfer> t(unserialize(client, &tempstr, tempmap));
    assert(t);
    assert(t->localfilename == localfilename);
    assert(t->state == (state == TRANSFERSTATE_PAUSED ? TRANSFERSTATE_PAUSED : TRANSFERSTATE_NONE));
    assert(t->priority == priority);
    assert(t->fingerprint() == fingerprint());
#endif


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

    if (type != GET && type != PUT)
    {
        assert(false);
        LOG_err << "Transfer unserialization failed - neither get nor put";
        return NULL;
    }

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

    unique_ptr<Transfer> t(new Transfer(client, type));

    memcpy(&t->filekey, ptr, sizeof t->filekey);
    ptr += sizeof(t->filekey);

    t->ctriv = MemAccess::get<int64_t>(ptr);
    ptr += sizeof(int64_t);

    t->metamac = MemAccess::get<int64_t>(ptr);
    ptr += sizeof(int64_t);

    memcpy(t->transferkey.data(), ptr, SymmCipher::KEYLENGTH);
    ptr += SymmCipher::KEYLENGTH;

    if (ll > 0)
    {
        t->localfilename = LocalPath::fromPlatformEncodedAbsolute(std::string(filepath, ll));
    }

    if (!t->chunkmacs.unserialize(ptr, end))
    {
        LOG_err << "Transfer unserialization failed - chunkmacs too long";
        return NULL;
    }

    d->erase(0, ptr - d->data());

    FileFingerprint *fp = FileFingerprint::unserialize(d);
    if (!fp)
    {
        LOG_err << "Error unserializing Transfer: Unable to unserialize FileFingerprint";
        return NULL;
    }

    *(FileFingerprint *)t.get() = *(FileFingerprint *)fp;
    delete fp;

    fp = FileFingerprint::unserialize(d);
    t->badfp = *fp;
    delete fp;

    ptr = d->data();
    end = ptr + d->size();

    if (ptr + sizeof(m_time_t) + sizeof(char) > end)
    {
        LOG_err << "Transfer unserialization failed - fingerprint too long";
        return NULL;
    }

    t->lastaccesstime = MemAccess::get<m_time_t>(ptr);
    ptr += sizeof(m_time_t);


    char hasUltoken = MemAccess::get<char>(ptr);
    ptr += sizeof(char);

    ll = hasUltoken ? ((hasUltoken == 1) ? NewNode::OLDUPLOADTOKENLEN + 1 : UPLOADTOKENLEN) : 0;
    if (hasUltoken < 0 || hasUltoken > 2
            || (ptr + ll + sizeof(unsigned short) > end))
    {
        LOG_err << "Transfer unserialization failed - invalid ultoken";
        return NULL;
    }

    if (hasUltoken)
    {
        t->ultoken.reset(new UploadToken);
        memcpy(t->ultoken.get(), ptr, ll);
        ptr += ll;
    }

    ll = MemAccess::get<unsigned short>(ptr);
    ptr += sizeof(ll);

    if (ptr + ll + 10 > end)
    {
        LOG_err << "Transfer unserialization failed - temp URL too long";
        return NULL;
    }

    std::string combinedUrls;
    combinedUrls.assign(ptr, ll);
    for (size_t p = 0; p < ll; )
    {
        size_t n = combinedUrls.find('\0');
        t->tempurls.push_back(combinedUrls.substr(p, n));
        assert(!t->tempurls.back().empty());
        p += (n == std::string::npos) ? ll : (n + 1);
    }
    if (!t->tempurls.empty() && t->tempurls.size() != 1 && t->tempurls.size() != RAIDPARTS)
    {
        LOG_err << "Transfer unserialization failed - temp URL incorrect components";
        return NULL;
    }
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
        return NULL;
    }
    ptr++;

    t->chunkmacs.calcprogress(t->size, t->pos, t->progresscompleted);

    auto it_bool = transfers[type].insert(pair<FileFingerprint*, Transfer*>(t.get(), t.get()));
    if (!it_bool.second)
    {
        // duplicate transfer
        t.reset();
    }
    return t.release();
}

SymmCipher *Transfer::transfercipher()
{
    return client->getRecycledTemporaryTransferCipher(transferkey.data());
}

void Transfer::removeCancelledTransferFiles(TransferDbCommitter* committer)
{
    // remove transfer files whose MegaTransfer associated has been cancelled (via cancel token)
    for (file_list::iterator it = files.begin(); it != files.end();)
    {
        file_list::iterator auxit = it++;
        if ((*auxit)->cancelToken.isCancelled())
        {
            removeTransferFile(API_EINCOMPLETE, *auxit, committer);
        }
    }
}

void Transfer::removeTransferFile(error e, File* f, TransferDbCommitter* committer)
{
    Transfer *transfer = f->transfer;
    client->filecachedel(f, committer);
    assert(*f->file_it == f);
    transfer->files.erase(f->file_it);
    client->app->file_removed(f, e);
    f->transfer = NULL;
    f->terminated(e);
}

void Transfer::removeAndDeleteSelf(transferstate_t finalState)
{
    finished = true;
    state = finalState;
    client->app->transfer_removed(this);

    // this will also remove the transfer from internal lists etc.
    // those use a lazy delete (ie mark for later deletion) so that we don't invalidate iterators.
    delete this;
}

// transfer attempt failed, notify all related files, collect request on
// whether to abort the transfer, kill transfer if unanimous
void Transfer::failed(const Error& e, TransferDbCommitter& committer, dstime timeleft)
{
    bool defer = false;

    LOG_debug << "Transfer failed with error " << e;

    DEBUG_TEST_HOOK_DOWNLOAD_FAILED(e);

    if (e == API_EOVERQUOTA || e == API_EPAYWALL)
    {
        assert((e == API_EPAYWALL && !timeleft) || (type == PUT && !timeleft) || (type == GET && timeleft)); // overstorage only possible for uploads, overbandwidth for downloads
        if (!slot)
        {
            bt.backoff(timeleft ? timeleft : NEVER);
            client->activateoverquota(timeleft, (e == API_EPAYWALL));
            client->app->transfer_failed(this, e, timeleft);
            ++client->performanceStats.transferTempErrors;
        }
        else
        {
            bool allForeignTargets = true;
            for (auto &file : files)
            {
                if (client->isPrivateNode(file->h))
                {
                    allForeignTargets = false;
                    break;
                }
            }

            /* If all targets are foreign and there's not a bandwidth overquota, transfer must fail.
             * Otherwise we need to activate overquota.
             */
            if (!timeleft && allForeignTargets)
            {
                client->app->transfer_failed(this, e);
            }
            else
            {
                bt.backoff(timeleft ? timeleft : NEVER);
                client->activateoverquota(timeleft, (e == API_EPAYWALL));
            }
        }
    }
    else if (e == API_EARGS || (e == API_EBLOCKED && type == GET) || (e == API_ETOOMANY && type == GET && e.hasExtraInfo()))
    {
        client->app->transfer_failed(this, e);
    }
    else if (e != API_EBUSINESSPASTDUE)
    {
        bt.backoff();
        state = TRANSFERSTATE_RETRYING;
        client->app->transfer_failed(this, e, timeleft);
        client->looprequested = true;
        ++client->performanceStats.transferTempErrors;
    }

    for (file_list::iterator it = files.begin(); it != files.end();)
    {
        // Remove files with foreign targets, if transfer failed with a (foreign) storage overquota
        if (e == API_EOVERQUOTA
                && !timeleft
                && client->isForeignNode((*it)->h))
        {
            File *f = (*it++);

#ifdef ENABLE_SYNC
            if (f->syncxfer)
            {
                client->disableSyncContainingNode(f->h, FOREIGN_TARGET_OVERSTORAGE, false);
            }
#endif
            removeTransferFile(API_EOVERQUOTA, f, &committer);
            continue;
        }

        /*
         * If the transfer failed with API_EARGS, the target handle is invalid. For a sync-transfer,
         * the actionpacket will eventually remove the target and the sync-engine will force to
         * disable the synchronization of the folder. For non-sync-transfers, remove the file directly.
         */
        if (e == API_EARGS || (e == API_EBLOCKED && type == GET) || (e == API_ETOOMANY && type == GET && e.hasExtraInfo()))
        {
             File *f = (*it++);
             if (f->syncxfer && e == API_EARGS)
             {
                defer = true;
             }
             else
             {
                removeTransferFile(e, f, &committer);
             }
             continue;
        }

        if (((*it)->failed(e, client) && (e != API_EBUSINESSPASTDUE))
                || (e == API_ENOENT // putnodes returned -9, file-storage server unavailable
                    && type == PUT
                    && tempurls.empty()
                    && failcount < 16) )
        {
            defer = true;
        }

        it++;
    }

    tempurls.clear();
    if (type == PUT)
    {
        chunkmacs.clear();
        progresscompleted = 0;
        ultoken.reset();
        pos = 0;

        if (slot && slot->fa && (slot->fa->mtime != mtime || slot->fa->size != size))
        {
            LOG_warn << "Modification detected during active upload. Size: " << size << "  Mtime: " << mtime
                     << "    FaSize: " << slot->fa->size << "  FaMtime: " << slot->fa->mtime;
            defer = false;
        }
    }

    if (defer)
    {
        failcount++;
        delete slot;
        slot = NULL;
        client->transfercacheadd(this, &committer);

        LOG_debug << "Deferring transfer " << failcount << " during " << (bt.retryin() * 100) << " ms";
    }
    else
    {
        LOG_debug << "Removing transfer";
        state = TRANSFERSTATE_FAILED;
        finished = true;

#ifdef ENABLE_SYNC
        bool alreadyDisabled = false;
#endif

        for (file_list::iterator it = files.begin(); it != files.end(); it++)
        {
#ifdef ENABLE_SYNC
            if((*it)->syncxfer
                && e != API_EBUSINESSPASTDUE
                && e != API_EOVERQUOTA
                && e != API_EPAYWALL)
            {
                client->syncdownrequired = true;
            }

            if (e == API_EBUSINESSPASTDUE && !alreadyDisabled)
            {
                client->syncs.disableSyncs(false, BUSINESS_EXPIRED, false, nullptr);
                alreadyDisabled = true;
            }
#endif

            client->app->file_removed(*it, e);
        }
        client->app->transfer_removed(this);
        ++client->performanceStats.transferFails;
        delete this;
    }
}

#ifdef USE_MEDIAINFO
static uint32_t* fileAttributeKeyPtr(byte filekey[FILENODEKEYLENGTH])
{
    // returns the last half, beyond the actual key, ie the nonce+crc
    return (uint32_t*)(filekey + FILENODEKEYLENGTH / 2);
}
#endif

void Transfer::addAnyMissingMediaFileAttributes(Node* node, /*const*/ LocalPath& localpath)
{
    assert(type == PUT || (node && node->type == FILENODE));

#ifdef USE_MEDIAINFO
    string ext;
    if (((type == PUT && size >= 16) || (node && node->nodekey().size() == FILENODEKEYLENGTH && node->size >= 16)) &&
        client->fsaccess->getextension(localpath, ext) &&
        MediaProperties::isMediaFilenameExt(ext) &&
        !client->mediaFileInfo.mediaCodecsFailed)
    {
        // for upload, the key is in the transfer.  for download, the key is in the node.
        uint32_t* attrKey = fileAttributeKeyPtr((type == PUT) ? filekey.bytes.data() : (byte*)node->nodekey().data());

        if (type == PUT || !node->hasfileattribute(fa_media) || client->mediaFileInfo.timeToRetryMediaPropertyExtraction(node->fileattrstring, attrKey))
        {
            // if we don't have the codec id mappings yet, send the request
            client->mediaFileInfo.requestCodecMappingsOneTime(client, LocalPath());

            // always get the attribute string; it may indicate this version of the mediaInfo library was unable to interpret the file
            MediaProperties vp;
            vp.extractMediaPropertyFileAttributes(localpath, client->fsaccess.get());

            if (type == PUT)
            {
                client->mediaFileInfo.queueMediaPropertiesFileAttributesForUpload(vp, attrKey, client, uploadhandle, this);
            }
            else
            {
                client->mediaFileInfo.sendOrQueueMediaPropertiesFileAttributesForExistingFile(vp, attrKey, client, node->nodeHandle());
            }
        }
    }
#endif
}

// transfer completion: copy received file locally, set timestamp(s), verify
// fingerprint, notify app, notify files
void Transfer::complete(TransferDbCommitter& committer)
{
    CodeCounter::ScopeTimer ccst(client->performanceStats.transferComplete);

    state = TRANSFERSTATE_COMPLETING;
    client->app->transfer_update(this);

    if (type == GET)
    {

        LOG_debug << client->clientname << "Download complete: " << (files.size() ? LOG_NODEHANDLE(files.front()->h) : "NO_FILES") << " " << files.size() << (files.size() ? files.front()->name : "");

        bool transient_error = false;
        LocalPath tmplocalname;
        LocalPath localname;
        bool success;

        // disconnect temp file from slot...
        slot->fa.reset();

        // FIXME: multiple overwrite race conditions below (make copies
        // from open file instead of closing/reopening!)

        // set timestamp (subsequent moves & copies are assumed not to alter mtime)
        success = client->fsaccess->setmtimelocal(localfilename, mtime);

        if (!success)
        {
            transient_error = client->fsaccess->transient_error;
            LOG_debug << "setmtimelocal failed " << transient_error;
        }

        // verify integrity of file
        auto fa = client->fsaccess->newfileaccess();
        FileFingerprint fingerprint;
        Node* n = nullptr;
        bool fixfingerprint = false;
        bool fixedfingerprint = false;
        bool syncxfer = false;

        for (file_list::iterator it = files.begin(); it != files.end(); it++)
        {
            if ((*it)->syncxfer)
            {
                syncxfer = true;
            }

            if (!fixedfingerprint && (n = client->nodeByHandle((*it)->h))
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

        if (!fixedfingerprint && success && fa->fopen(localfilename, true, false))
        {
            fingerprint.genfingerprint(fa.get());
            if (isvalid && !(fingerprint == *(FileFingerprint*)this))
            {
                LOG_err << "Fingerprint mismatch";

                // enforce the verification of the fingerprint for sync transfers only
                if (syncxfer && (!badfp.isvalid || !(badfp == fingerprint)))
                {
                    badfp = fingerprint;
                    fa.reset();
                    chunkmacs.clear();
                    client->fsaccess->unlinklocal(localfilename);
                    return failed(API_EWRITE, committer);
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
        else
        {
            if (syncxfer && !fixedfingerprint && success)
            {
                transient_error = fa->retry;
                LOG_debug << "Unable to validate fingerprint " << transient_error;
            }
        }
        fa.reset();

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
                    if ((*it)->hprivate && !(*it)->hforeign && (n = client->nodeByHandle((*it)->h))
                            && nodes.find(n->nodehandle) == nodes.end())
                    {
                        nodes.insert(n->nodehandle);

                        if ((!n->isvalid || fixfingerprint)
                                && !(fingerprint == *(FileFingerprint*)n)
                                && fingerprint.size == this->size)
                        {
                            LOG_debug << "Fixing fingerprint";
                            *(FileFingerprint*)n = fingerprint;

                            attr_map attrUpdate;
                            n->serializefingerprint(&attrUpdate['c']);
                            client->setattr(n, std::move(attrUpdate), client->reqtag, nullptr, nullptr, false);
                            // canChangeVault = false -> this is a download being completed. Backups only upload data, and
                            // even if the FileFingerprint was missing, setting it is not an action coming from a backup
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
                    if (fa->fopen(localname) || fa->type == FOLDERNODE)
                    {
                        // the destination path already exists
        #ifdef ENABLE_SYNC
                        if((*it)->syncxfer)
                        {
                            bool foundOne = false;
                            client->syncs.forEachRunningSync([&](Sync* sync){

                                LocalNode *localNode = sync->localnodebypath(NULL, localname);
                                if (localNode && !foundOne)
                                {
                                    LOG_debug << "Overwriting a local synced file. Moving the previous one to debris";

                                    // try to move to local debris
                                    if(!sync->movetolocaldebris(localname))
                                    {
                                        transient_error = client->fsaccess->transient_error;
                                    }

                                    foundOne = true;
                                }
                            });

                            if (!foundOne)
                            {
                                LOG_err << "LocalNode for destination file not found";

                                if(client->syncs.hasRunningSyncs())
                                {
                                    // try to move to debris in the first sync
                                    if(!client->syncs.firstRunningSync()->movetolocaldebris(localname))
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
                            LocalPath localnewname;
                            unsigned num = 0;
                            do
                            {
                                num++;
                                localnewname = localname.insertFilenameCounter(num);
                            } while (fa->fopen(localnewname) || fa->type == FOLDERNODE);


                            (*it)->localname = localnewname;
                            localname = localnewname;
                        }
                    }
                    else
                    {
                        transient_error = fa->retry;
                    }

                    if (transient_error)
                    {
                        LOG_warn << "Transient error checking if the destination file exist";
                        it++;
                        continue;
                    }
                }

                if (files.size() == 1 && tmplocalname.empty())
                {
                    if (localfilename != localname)
                    {
                        LOG_debug << "Renaming temporary file to target path";
                        if (client->fsaccess->renamelocal(localfilename, localname))
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
                    if((!tmplocalname.empty() ? tmplocalname : localfilename) == localname)
                    {
                        LOG_debug << "Identical node downloaded to the same folder";
                        success = true;
                    }
                    else if (client->fsaccess->copylocal(!tmplocalname.empty() ? tmplocalname : localfilename,
                                                   localname, mtime))
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
                    if ((*it)->hprivate && !(*it)->hforeign && (n = client->nodeByHandle((*it)->h)))
                    {
                        if (!client->gfxdisabled && client->gfx && client->gfx->isgfx(localname) &&
                            keys.find(n->nodekey()) == keys.end() &&    // this file hasn't been processed yet
                            client->checkaccess(n, OWNER))
                        {
                            keys.insert(n->nodekey());

                            // check if restoration of missing attributes failed in the past (no access)
                            if (n->attrs.map.find('f') == n->attrs.map.end() || n->attrs.map['f'] != me64)
                            {
                                // check for missing imagery
                                int missingattr = 0;
                                if (!n->hasfileattribute(GfxProc::THUMBNAIL)) missingattr |= 1 << GfxProc::THUMBNAIL;
                                if (!n->hasfileattribute(GfxProc::PREVIEW)) missingattr |= 1 << GfxProc::PREVIEW;

                                if (missingattr)
                                {
                                    client->gfx->gendimensionsputfa(NULL, localname, NodeOrUploadHandle(n->nodeHandle()), n->nodecipher(), missingattr);
                                }

                                addAnyMissingMediaFileAttributes(n, localname);
                            }
                        }
                    }
                }

                if (success || !transient_error)
                {
                    if (auto node = client->nodeByHandle((*it)->h))
                    {
                        auto path = (*it)->localname;
                        auto type = isFilenameAnomaly(path, node);

                        if (type != FILENAME_ANOMALY_NONE)
                        {
                            client->filenameAnomalyDetected(type, path, node->displaypath());
                        }
                    }

                    if (success)
                    {
                        // prevent deletion of associated Transfer object in completed()
                        client->filecachedel(*it, &committer);
                        client->app->file_complete(*it);
                        (*it)->transfer = NULL;
                        (*it)->completed(this, (*it)->syncxfer ? PUTNODES_SYNC : PUTNODES_APP);
                    }

                    if (success || !(*it)->failed(API_EAGAIN, client))
                    {
                        File* f = (*it);
                        files.erase(it++);
                        if (!success)
                        {
                            LOG_warn << "Unable to complete transfer due to a persistent error";
                            client->filecachedel(f, &committer);
#ifdef ENABLE_SYNC
                            if (f->syncxfer)
                            {
                                client->syncdownrequired = true;
                            }
#endif
                            client->app->file_removed(f, API_EWRITE);
                            f->transfer = NULL;
                            f->terminated(API_EWRITE);
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

            if (tmplocalname.empty() && !files.size())
            {
                client->fsaccess->unlinklocal(localfilename);
            }
        }

        if (!files.size())
        {
            state = TRANSFERSTATE_COMPLETED;
            localfilename = localname;
            assert(localfilename.isAbsolute());
            finished = true;
            client->looprequested = true;
            client->app->transfer_complete(this);
            localfilename.clear();
            delete this;
        }
        else
        {
            // some files are still pending completion, close fa and set retry timer
            slot->fa.reset();

            LOG_debug << "Files pending completion: " << files.size() << ". Waiting for a retry.";
            LOG_debug << "First pending file: " << files.front()->name;

            slot->retrying = true;
            slot->retrybt.backoff(11);
        }
    }
    else // type == PUT
    {
        LOG_debug << client->clientname << "Upload complete: " << (files.size() ? files.front()->name : "NO_FILES") << " " << files.size();

        if (slot->fa)
        {
            slot->fa.reset();
        }

        // files must not change during a PUT transfer
        for (file_list::iterator it = files.begin(); it != files.end(); )
        {
            File *f = (*it);
            LocalPath *localpath = &f->localname;

#ifdef ENABLE_SYNC
            LocalPath synclocalpath;
            LocalNode *ll = dynamic_cast<LocalNode *>(f);
            if (ll)
            {
                LOG_debug << "Verifying sync upload";
                synclocalpath = ll->getLocalPath();
                localpath = &synclocalpath;
            }
#endif
            if (auto node = client->nodeByHandle(f->h))
            {
                auto type = isFilenameAnomaly(*localpath, f->name);

                if (type != FILENAME_ANOMALY_NONE)
                {
                    // Construct remote path for reporting.
                    ostringstream remotepath;

                    remotepath << node->displaypath()
                               << (node->parent ? "/" : "")
                               << f->name;

                    client->filenameAnomalyDetected(type, *localpath, remotepath.str());
                }
            }

            if (localpath == &f->localname)
            {
                LOG_debug << "Verifying regular upload";
            }

            auto fa = client->fsaccess->newfileaccess();
            bool isOpen = fa->fopen(*localpath);
            if (!isOpen)
            {
                if (client->fsaccess->transient_error)
                {
                    LOG_warn << "Retrying upload completion due to a transient error";
                    slot->retrying = true;
                    slot->retrybt.backoff(11);
                    return;
                }
            }

            if (!isOpen || f->genfingerprint(fa.get()))
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
                it++; // the next line will remove the current item and invalidate that iterator
                removeTransferFile(API_EREAD, f, &committer);
            }
            else
            {
                it++;
            }
        }

        if (!files.size())
        {
            return failed(API_EREAD, committer);
        }


        if (!client->gfxdisabled)
        {
            // prepare file attributes for video/audio files if the file is suitable
            addAnyMissingMediaFileAttributes(NULL, localfilename);
        }

        // if this transfer is put on hold, do not complete
        client->checkfacompletion(uploadhandle, this, true);
        return;
    }
}

void Transfer::completefiles()
{
    // notify all files and give them an opportunity to self-destruct
    vector<uint32_t> &ids = client->pendingtcids[tag];
    vector<LocalPath> *pfs = NULL;

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
        f->completed(this, f->syncxfer ? PUTNODES_SYNC : PUTNODES_APP);
        files.erase(it++);
    }
    ids.push_back(dbid);
}

DirectReadNode::DirectReadNode(MegaClient* cclient, handle ch, bool cp, SymmCipher* csymmcipher, int64_t cctriv, const char *privauth, const char *pubauth, const char *cauth)
{
    client = cclient;

    p = cp;
    h = ch;

    if (privauth)
    {
        privateauth = privauth;
    }

    if (pubauth)
    {
        publicauth = pubauth;
    }

    if (cauth)
    {
        chatauth = cauth;
    }

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
void DirectReadNode::retry(const Error& e, dstime timeleft)
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

            if (retryds < minretryds && !(e == API_ETOOMANY && e.hasExtraInfo()))
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
    else if (e == API_EPAYWALL)
    {
        minretryds = NEVER;
    }

    tempurls.clear();

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

void DirectReadNode::cmdresult(const Error &e, dstime timeleft)
{
    pendingcmd = NULL;

    if (e == API_OK)
    {
        // feed all pending reads to the global read queue
        for (dr_list::iterator it = reads.begin(); it != reads.end(); it++)
        {
            DirectRead* dr = *it;
            assert(dr->drq_it == client->drq.end());

            if (dr->drbuf.tempUrlVector().empty())
            {
                // DirectRead starting
                m_off_t streamingMaxReqSize = dr->drMaxReqSize();
                LOG_debug << "Direct read node size = " << dr->drn->size << ", streaming max request size: " << streamingMaxReqSize;
                dr->drbuf.setIsRaid(dr->drn->tempurls, dr->offset, dr->offset + dr->count, dr->drn->size, streamingMaxReqSize);
            }
            else
            {
                // URLs have been re-requested, eg. due to temp URL expiry.  Keep any parts downloaded already
                dr->drbuf.updateUrlsAndResetPos(dr->drn->tempurls);
            }

            dr->drq_it = client->drq.insert(client->drq.end(), *it);
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

bool DirectReadSlot::processAnyOutputPieces()
{
    bool continueDirectRead = true;

    std::shared_ptr<TransferBufferManager::FilePiece> outputPiece;
    while (continueDirectRead && (outputPiece = mDr->drbuf.getAsyncOutputBufferPointer(0)))
    {
        size_t len = outputPiece->buf.datalen();
        mSpeed = mSpeedController.calculateSpeed(len);
        mMeanSpeed = mSpeedController.getMeanSpeed();
        mDr->drn->client->httpio->updatedownloadspeed(len);

        mSlotThroughput.first += static_cast<m_off_t>(len);
        auto lastDataTime = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - mSlotStartTime).count();
        mSlotThroughput.second = static_cast<m_off_t>(lastDataTime);
        LOG_verbose << "DirectReadSlot -> Delivering assembled part ->"
                    << "len = " << len << ", speed = " << mSpeed << ", meanSpeed = " << (mMeanSpeed / 1024) << " KB/s"
                    << ", slotThroughput = " << ((calcThroughput(mSlotThroughput.first, mSlotThroughput.second) * 1000) / 1024) << " KB/s]";
        continueDirectRead = mDr->drn->client->app->pread_data(outputPiece->buf.datastart(), len, mPos, mSpeed, mMeanSpeed, mDr->appdata);

        mDr->drbuf.bufferWriteCompleted(0, true);

        if (continueDirectRead)
        {
            mPos += len;
            mDr->drn->partiallen += len;
            mDr->progress += len;
            mMinComparableThroughput = static_cast<m_off_t>(len);
            mDr->drn->schedule(DirectReadSlot::TIMEOUT_DS); // If we have all the req data already submitted we need to re-schedule this or the transfer will be retried before TIMEOUT_DS dseconds
        }
    }
    return continueDirectRead;
}

bool DirectReadSlot::waitForPartsInFlight() const
{
    return DirectReadSlot::WAIT_FOR_PARTS_IN_FLIGHT &&
            (mDr->drbuf.isRaid() || (mReqs.size() > 1)) &&
            mWaitForParts;
}

unsigned DirectReadSlot::usedConnections() const
{
    assert(mDr->drbuf.isRaid() || (mReqs.size() > 1));
    if (!mDr->drbuf.isRaid() || mReqs.size() == 0)
    {
        LOG_warn << "DirectReadSlot -> usedConnections() being used when it shouldn't";
    }
    return static_cast<unsigned>(mReqs.size()) - ((mUnusedRaidConnection != static_cast<unsigned>(mReqs.size())) ? 1 : 0);
}

bool DirectReadSlot::resetConnection(size_t connectionNum)
{
    LOG_debug << "DirectReadSlot [conn " << connectionNum << "] -> resetConnection";
    assert(connectionNum < mReqs.size());
    if (connectionNum >= mReqs.size())
    {
        return false;
    }
    if (mReqs[connectionNum])
    {
        mReqs[connectionNum]->disconnect();
        mReqs[connectionNum]->status = REQ_READY;
        mThroughput[connectionNum].first = 0;
        mThroughput[connectionNum].second = 0;
    }
    mDr->drbuf.resetPart(static_cast<unsigned>(connectionNum));
    return true;
}

m_off_t DirectReadSlot::getThroughput(size_t connectionNum) const
{
    assert(connectionNum < mReqs.size());
    m_off_t connectionThroughPut = calcThroughput(mThroughput[connectionNum].first, mThroughput[connectionNum].second);
    return connectionThroughPut;
}

m_off_t DirectReadSlot::calcThroughput(m_off_t numBytes, m_off_t timeCount) const
{
    m_off_t throughput = (numBytes && timeCount) ?
                            numBytes / timeCount :
                            0;
    return throughput;
}

bool DirectReadSlot::searchAndDisconnectSlowestConnection(size_t connectionNum)
{
    assert(connectionNum < mReqs.size());
    std::unique_ptr<HttpReq>& req = mReqs[connectionNum];
    if (!req || !mDr->drbuf.isRaid())
    {
        return false;
    }
    if ((mNumSlowConnectionsSwitches < DirectReadSlot::MAX_SLOW_CONNECTION_SWITCHES) ||
        (connectionNum == mUnusedRaidConnection))
    {
        return false;
    }
    bool minComparableThroughputForThisConnection = mThroughput[connectionNum].second &&
                                                    mThroughput[connectionNum].first >= mMinComparableThroughput;
    if (minComparableThroughputForThisConnection)
    {
        size_t slowestConnection = connectionNum;
        size_t fastestConnection = connectionNum;
        size_t numReqs = mReqs.size();
        bool minComparableThroughputForOtherConnection = true;
        for (size_t otherConnection = numReqs - 1; otherConnection > 0 && minComparableThroughputForOtherConnection; --otherConnection)
        {
            if ((otherConnection != connectionNum) &&
                (otherConnection != mUnusedRaidConnection))
            {
                if (mThroughput[otherConnection].second && mThroughput[otherConnection].first >= mMinComparableThroughput)
                {
                    m_off_t otherConnectionThroughput = getThroughput(otherConnection);
                    m_off_t slowestConnectionThroughput = getThroughput(slowestConnection);
                    m_off_t fastestConnectionThroughput = getThroughput(fastestConnection);
                    if (otherConnectionThroughput < slowestConnectionThroughput)
                    {
                        slowestConnection = otherConnection;
                    }
                    if (otherConnectionThroughput > fastestConnectionThroughput)
                    {
                        fastestConnection = otherConnection;
                    }
                }
                else // If we don't have enough throughput data for mThroughput to compare with (maybe it was disconnected right from the beggining), then we just skip this
                {
                    // Cannot compare... will need to wait
                    slowestConnection = numReqs;
                    fastestConnection = numReqs;
                    minComparableThroughputForOtherConnection = false;
                }
            }
        }
        LOG_verbose << "DirectReadSlot [conn " << connectionNum << "]"
                    << " Test slow connection -> slowest connection = " << slowestConnection
                    << ", fastest connection = " << fastestConnection
                    << ", unused raid connection = " << mUnusedRaidConnection
                    << ", mMinComparableThroughput = " << (mMinComparableThroughput / 1024) << " KB/s";
        if (((slowestConnection == connectionNum) ||
             ((slowestConnection != numReqs) && (mReqs[slowestConnection]->status == REQ_READY))) &&
            (fastestConnection != slowestConnection))
        {
            m_off_t slowestConnectionThroughput = getThroughput(slowestConnection);
            m_off_t fastestConnectionThroughput = getThroughput(fastestConnection);
            if (fastestConnectionThroughput * SLOWEST_TO_FASTEST_THROUGHPUT_RATIO[0]
                        >
                slowestConnectionThroughput * SLOWEST_TO_FASTEST_THROUGHPUT_RATIO[1])
            {
                LOG_warn << "DirectReadSlot [conn " << connectionNum << "]"
                        << " Connection " << slowestConnection << " is slow, trying the other 5 cloudraid connections"
                        << " [slowest speed = " << ((slowestConnectionThroughput * 1000 / 1024)) << " KB/s"
                        << ", fastest speed = " << ((fastestConnectionThroughput * 1000 / 1024)) << " KB/s"
                        << ", mMinComparableThroughput = " << (mMinComparableThroughput / 1024) << " KB/s]"
                        << " [total slow connections switches = " << mNumSlowConnectionsSwitches << "]"
                        << " [current unused raid connection = " << mUnusedRaidConnection << "]";
                if (mDr->drbuf.setUnusedRaidConnection(static_cast<unsigned>(slowestConnection)))
                {
                    if (mUnusedRaidConnection != mReqs.size())
                    {
                        resetConnection(mUnusedRaidConnection);
                    }
                    mUnusedRaidConnection = slowestConnection;
                    ++mNumSlowConnectionsSwitches;
                    LOG_verbose << "DirectReadSlot [conn " << connectionNum << "]"
                                << " Continuing after setting slow connection"
                                << " [total slow connections switches = " << mNumSlowConnectionsSwitches << "]";
                    return resetConnection(mUnusedRaidConnection);
                }
            }
        }
    }
    return false;
}

bool DirectReadSlot::detectSlowestStartConnection(size_t connectionNum)
{
    assert(connectionNum < mReqs.size());
    if (mDr->drbuf.isRaid() && mUnusedRaidConnection == static_cast<unsigned>(mReqs.size()))
    {
        std::unique_ptr<HttpReq>& req = mReqs[connectionNum];
        unsigned slowestStartConnection;
        if (req && req->httpio &&
            ((static_cast<m_off_t>(req->in.size()) + mThroughput[connectionNum].first) >= mMinComparableThroughput) &&
            mDr->drbuf.detectSlowestRaidConnection(static_cast<unsigned>(connectionNum), slowestStartConnection))
        {
            LOG_warn << "DirectReadSlot [conn " << connectionNum << "] ->"
                    << " Connection " << slowestStartConnection << " is the slowest to reply, using the other 5";
            mUnusedRaidConnection = static_cast<size_t>(slowestStartConnection);
            if (mReqs[slowestStartConnection])
            {
                LOG_debug << "DirectReadSlot [conn " << connectionNum << "] -> "
                            << "New unused raid connection: " << mUnusedRaidConnection
                            << ", mMinComparableThroughput = " << (mMinComparableThroughput / 1024) << " KB/s"
                            << " [request status of new unused raid connection = " << mReqs[slowestStartConnection]->status << "]";
                switch(mReqs[slowestStartConnection]->status)
                {
                    case REQ_INFLIGHT:
                    case REQ_SUCCESS:
                    case REQ_FAILURE:
                        if (decreaseReqsInflight() && (mNumReqsInflight == static_cast<unsigned>(mReqs.size() - 1)))
                        {
                            // If this happens, numReqsInflight was mReqs.size() (RAIDPARTS likely) before this.
                            // Hence, waitForParts should be true, but we need the disconnected part to process the new buffer it will have after it's been reset (so it can be combined with the others by using parity)
                            assert(!DirectReadSlot::WAIT_FOR_PARTS_IN_FLIGHT || mWaitForParts == true);
                            LOG_debug << "DirectReadSlot [conn " << connectionNum << "] -> new unused connection " << mUnusedRaidConnection << " was in flight and waitForParts flag was true, setting waitForParts flag to false";
                            mWaitForParts = false;
                        }
                    default:
                        break;
                }
            }
            return resetConnection(mUnusedRaidConnection);
        }
    }
    return false;
}

bool DirectReadSlot::decreaseReqsInflight()
{
    if (mReqs.size() > 1)
    {
        LOG_verbose << "Decreasing counter of total requests inflight: " << mNumReqsInflight << " - 1";
        assert(mNumReqsInflight > 0);
        --mNumReqsInflight;
        if ((mUnusedRaidConnection < mReqs.size()) &&
            (mReqs[mUnusedRaidConnection]->status != REQ_DONE) &&
            (mNumReqsInflight == (static_cast<unsigned>(mReqs.size()) - usedConnections())))
        {
            mNumReqsInflight = 0;
        }
        if (mNumReqsInflight == 0)
        {
            // waitForParts could be true at this point if there were connections with REQ_DONE status which didn't increase the inflight counter
            mWaitForParts = false;
            mMaxChunkSubmitted = 0;
        }
        return true;
    }
    return false;
}

bool DirectReadSlot::increaseReqsInflight()
{
    if (mReqs.size() > 1)
    {
        LOG_verbose << "Increasing counter of total requests inflight: " << mNumReqsInflight << " + 1";
        assert(mNumReqsInflight < mReqs.size());
        ++mNumReqsInflight;
        if (mNumReqsInflight == static_cast<unsigned>(mReqs.size()))
        {
            assert(!mWaitForParts);
            mWaitForParts = true;
        }
        return true;
    }
    return false;
}

bool DirectReadSlot::watchOverDirectReadPerformance()
{
    if (Waiter::ds - mDr->drn->partialstarttime > MEAN_SPEED_INTERVAL_DS)
    {
        m_off_t meanspeed = (10 * mDr->drn->partiallen) / (Waiter::ds - mDr->drn->partialstarttime);

        LOG_debug << "DirectReadSlot: Calculating Mean speed (KB/s): " << (meanspeed / 1024);
        int minspeed = mDr->drn->client->minstreamingrate;
        if (minspeed < 0)
        {
            LOG_warn << "DirectReadSlot: Set min speed as MIN_BYTES_PER_SECOND";
            minspeed = MIN_BYTES_PER_SECOND;
        }
        if (minspeed != 0 && meanspeed < minspeed)
        {
            LOG_warn << "DirectReadSlot: Transfer speed too low for streaming. Retrying";
            mDr->drn->retry(API_EAGAIN);
            return true;
        }
        else
        {
            mDr->drn->partiallen = 0;
            mDr->drn->partialstarttime = Waiter::ds;
        }
    }
    return false;
}

bool DirectReadSlot::doio()
{
    for (int connectionNum = static_cast<int>(mReqs.size()); connectionNum--; )
    {
        if (detectSlowestStartConnection(connectionNum))
        {
            LOG_verbose << "DirectReadSlot [conn " << connectionNum << "] Continue DirectReadSlot loop after disconnecting slowest initial connection " << mUnusedRaidConnection;
        }

        std::unique_ptr<HttpReq>& req = mReqs[connectionNum];
        if (req && (static_cast<unsigned>(connectionNum) != mUnusedRaidConnection))
        {
            if (req->status == REQ_INFLIGHT || req->status == REQ_SUCCESS)
            {
                if (req->in.size())
                {
                    unsigned n = static_cast<unsigned>(req->in.size());
                    auto lastDataTime = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - req->postStartTime).count();
                    m_off_t chunkTime = static_cast<m_off_t>(lastDataTime) - mThroughput[connectionNum].second;

                    unsigned minmin = 16 * 1024;
                    m_off_t updatedThroughput = calcThroughput(mThroughput[connectionNum].first + n, mThroughput[connectionNum].second + chunkTime) * 1000;
                    m_off_t chunkThroughput = calcThroughput(static_cast<m_off_t>(n), chunkTime) * 1000;
                    m_off_t aggregatedThroughput = (chunkThroughput + updatedThroughput) / 2;
                    if (mMaxChunkSubmitted && aggregatedThroughput && ((std::max(static_cast<unsigned>(aggregatedThroughput), mMaxChunkSubmitted) / std::min(static_cast<unsigned>(aggregatedThroughput), mMaxChunkSubmitted)) == 1))
                    {
                        // Avoid small chunks due to fragmentation caused by similar (but different) chunk sizes (compared to max submitted chunk size)
                        aggregatedThroughput = mMaxChunkSubmitted;
                    }
                    unsigned minChunkSize = std::max(static_cast<unsigned>(aggregatedThroughput), minmin);
                    unsigned bytesLeftForMaxChunkSize = mMaxChunkSize - static_cast<unsigned>(mThroughput[connectionNum].first % mMaxChunkSize);
                    if (req->status == REQ_INFLIGHT)
                    {
                        if (n >= bytesLeftForMaxChunkSize)
                        {
                            n = bytesLeftForMaxChunkSize;
                        }
                        else if (n >= minChunkSize)
                        {
                            n = (n / minmin) * minmin;
                        }
                        else
                        {
                            n = 0;
                        }
                    }

                    assert(!mDr->drbuf.isRaid() || (req->status == REQ_SUCCESS) || ((n % RAIDSECTOR) == 0));
                    if ((mDr->drbuf.isRaid() && (req->status != REQ_SUCCESS) && ((n % RAIDSECTOR) != 0)))
                    {
                        LOG_err << "DirectReadSlot [conn " << connectionNum << "] ERROR: (mDr->drbuf.isRaid() && (req->status != REQ_SUCCESS) && ((n % RAIDSECTOR) != 0)"
                                << " n = " << n
                                << ", req->in.size = " << req->in.size()
                                << ", req->status = " << req->status
                                << ", bytesLeftForMaxChunkSize = " << bytesLeftForMaxChunkSize
                                << ", mMaxChunkSize = " << mMaxChunkSize
                                << ", submitted = " << mThroughput[connectionNum].first;
                    }

                    if (n)
                    {
                        mThroughput[connectionNum].first += static_cast<m_off_t>(n);
                        mThroughput[connectionNum].second += chunkTime;
                        LOG_verbose << "DirectReadSlot [conn " << connectionNum << "] ->"
                                    << " FilePiece's going to be submitted: n = " << n << ", req->in.size = " << req->in.size() << ", req->in.capacity = " << req->in.capacity()
                                    << " [minChunkSize = " << minChunkSize
                                    << ", bytesLeftForMaxChunkSize = " << bytesLeftForMaxChunkSize
                                    << ", mMaxChunkSize = " << mMaxChunkSize
                                    << ", reqs.size = " << mReqs.size()
                                    << ", req->status = " << std::string(req->status == REQ_READY ? "REQ_READY" : req->status == REQ_INFLIGHT ? "REQ_INFLIGHT" : req->status == REQ_SUCCESS ? "REQ_SUCCESS" : "REQ_SOMETHING")
                                    << ", req->httpstatus = " << req->httpstatus << ", req->contentlength = " << req->contentlength
                                    << ", numReqsInflight = " << mNumReqsInflight << ", unusedRaidConnection = " << mUnusedRaidConnection << "]"
                                    << " [chunk throughput = " << ((calcThroughput(static_cast<m_off_t>(n), chunkTime) * 1000) / 1024) << " KB/s"
                                    << ", average throughput = " << (getThroughput(connectionNum) * 1000 / 1024) << " KB/s"
                                    << ", aggregated throughput = " << (aggregatedThroughput / 1024) << " KB/s]"
                                    << ", [req->pos_pre = " << (req->pos) << ", req->pos_now = " << (req->pos + n) << "]";
                        RaidBufferManager::FilePiece *np = new RaidBufferManager::FilePiece(req->pos, n);
                        memcpy(np->buf.datastart(), req->in.c_str(), n);

                        req->in.erase(0, n);
                        req->contentlength -= n;
                        req->bufpos = 0;
                        req->pos += n;

                        mDr->drbuf.submitBuffer(connectionNum, np);

                        if (n > mMaxChunkSubmitted)
                        {
                            mMaxChunkSubmitted = n;
                        }
                    }

                    if (req->httpio)
                    {
                        req->httpio->lastdata = Waiter::ds;
                    }
                    req->lastdata = Waiter::ds;

                    mDr->drn->schedule(DirectReadSlot::TIMEOUT_DS);

                    // we might have a raid-reassembled block to write now, or this very block in non-raid
                    if (n && !processAnyOutputPieces())
                    {
                        // app-requested abort
                        delete mDr;
                        return true;
                    }
                }

                if (req->status == REQ_SUCCESS && !req->in.size())
                {
                    decreaseReqsInflight();
                    req->status = REQ_READY;
                }
            }
        }

        if (!req || req->status == REQ_READY)
        {
            if (!waitForPartsInFlight())
            {
                if (searchAndDisconnectSlowestConnection(connectionNum))
                {
                    LOG_verbose << "DirectReadSlot [conn " << connectionNum << "] Continue DirectReadSlot loop after disconnecting slow connection " << mUnusedRaidConnection;
                }
                bool newBufferSupplied = false, pauseForRaid = false;
                std::pair<m_off_t, m_off_t> posrange = mDr->drbuf.nextNPosForConnection(connectionNum, newBufferSupplied, pauseForRaid);

                if (newBufferSupplied)
                {
                    if (static_cast<unsigned>(connectionNum) == mUnusedRaidConnection)
                    {
                        // Count the "unused connection" (restored by parity) as a req inflight, so we avoid to exec this piece of code needlessly
                        increaseReqsInflight();
                    }
                    // we might have a raid-reassembled block to write, or a previously loaded block, or a skip block to process.
                    processAnyOutputPieces();
                }
                else if (!pauseForRaid)
                {
                    if (posrange.first >= posrange.second)
                    {
                        if (req)
                        {
                            LOG_verbose << "DirectReadSlot [conn " << connectionNum << "] Request status set to DONE";
                            req->status = REQ_DONE;
                        }
                        bool allDone = true;
                        for (size_t i = mReqs.size(); i--;)
                        {
                            if (mReqs[i] && mReqs[i]->status != REQ_DONE)
                            {
                                allDone = false;
                            }
                        }
                        if (allDone)
                        {
                            LOG_debug << "DirectReadSlot [conn " << connectionNum << "] All requests are DONE: Delete read request and direct read slot";
                            mDr->drn->schedule(DirectReadSlot::TEMPURL_TIMEOUT_DS);

                            // remove and delete completed read request, then remove slot
                            delete mDr;
                            return true;
                        }
                    }
                    else
                    {
                        if (!req)
                        {
                            mReqs[connectionNum] = make_unique<HttpReq>(true);
                        }

                        char buf[128];
                        sprintf(buf, "/%" PRIu64 "-", posrange.first);
                        if (mDr->count)
                        {
                            sprintf(strchr(buf, 0), "%" PRIu64, posrange.second - 1);
                        }

                        req->pos = posrange.first;
                        req->posturl = adjustURLPort(mDr->drbuf.tempURL(connectionNum));
                        req->posturl.append(buf);
                        LOG_debug << "DirectReadSlot [conn " << connectionNum << "] Request chunk of size " << (posrange.second - posrange.first) << " (request status = " << req->status << ")";
                        LOG_debug << "POST URL: " << req->posturl;

                        mThroughput[connectionNum].first = 0;
                        mThroughput[connectionNum].second = 0;
                        req->in.reserve(mMaxChunkSize + (mMaxChunkSize/2));
                        req->post(mDr->drn->client); // status will go to inflight or fail
                        LOG_verbose << "DirectReadSlot [conn " << connectionNum << "] POST done (new request status = " << req->status << ")";

                        mDr->drbuf.transferPos(connectionNum) = posrange.second;
                        increaseReqsInflight();
                    }
                }
            }
        }

        if (req && req->status == REQ_FAILURE)
        {
            LOG_warn << "DirectReadSlot [conn " << connectionNum << "] Request status is FAILURE [Request status = " << req->status << ", HTTP status = " << req->httpstatus << "]";
            decreaseReqsInflight();
            if (req->httpstatus == 509)
            {
                LOG_warn << "Bandwidth overquota from storage server for streaming transfer";

                dstime backoff = mDr->drn->client->overTransferQuotaBackoff(req.get());
                mDr->drn->retry(API_EOVERQUOTA, backoff);
            }
            else
            {
                // a failure triggers a complete abort and retry of all pending reads for this node, including getting updated URL(s)
                mDr->drn->retry(API_EREAD);
            }
            return true;
        }

        if (watchOverDirectReadPerformance())
        {
            LOG_debug << "DirectReadSlot [conn " << connectionNum << "] DirectReadSlot will be retried";
            return true;
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

m_off_t DirectRead::drMaxReqSize() const
{
    m_off_t numParts = drn->tempurls.size() == RAIDPARTS ?
                                    (RAIDPARTS - 1) :
                                    drn->tempurls.size();
    return std::max(drn->size / numParts, TransferSlot::MAX_REQ_SIZE);
}

DirectRead::DirectRead(DirectReadNode* cdrn, m_off_t ccount, m_off_t coffset, int creqtag, void* cappdata)
    : drbuf(this)
{
    drn = cdrn;

    count = ccount;
    offset = coffset;
    progress = 0;
    reqtag = creqtag;
    appdata = cappdata;

    drs = NULL;

    reads_it = drn->reads.insert(drn->reads.end(), this);

    if (!drn->tempurls.empty())
    {
        // we already have tempurl(s): queue for immediate fetching
        m_off_t streamingMaxReqSize = drMaxReqSize();
        LOG_debug << "Direct read start -> direct read node size = " << drn->size << ", streaming max request size: " << streamingMaxReqSize;
        drbuf.setIsRaid(drn->tempurls, offset, offset + count, drn->size, streamingMaxReqSize);
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

std::string DirectReadSlot::adjustURLPort(std::string url)
{
    if (!memcmp(url.c_str(), "http:", 5))
    {
        size_t portendindex = url.find("/", 8);
        size_t portstartindex = url.find(":", 8);

        if (portendindex != string::npos)
        {
            if (portstartindex == string::npos)
            {
                if (mDr->drn->client->usealtdownport)
                {
                    LOG_debug << "Enabling alternative port for streaming transfer";
                    url.insert(portendindex, ":8080");
                }
            }
            else
            {
                if (!mDr->drn->client->usealtdownport)
                {
                    LOG_debug << "Disabling alternative port for streaming transfer";
                    url.erase(portstartindex, portendindex - portstartindex);
                }
            }
        }
    }
    return url;
}

// request DirectRead's range via tempurl
DirectReadSlot::DirectReadSlot(DirectRead* cdr)
{
    mDr = cdr;

    mPos = mDr->offset + mDr->progress;
    mDr->nextrequestpos = mPos;

    mSpeed = mMeanSpeed = 0;

    assert(mReqs.empty());
    for (int i = static_cast<int>(mDr->drbuf.tempUrlVector().size()); i--; )
    {
        mReqs.push_back(make_unique<HttpReq>(true));
        mReqs.back()->status = REQ_READY;
        mReqs.back()->type = REQ_BINARY;
    }
    mThroughput.resize(mReqs.size());
    mUnusedRaidConnection = mDr->drbuf.isRaid() ? mDr->drbuf.getUnusedRaidConnection() : mReqs.size();
    mNumSlowConnectionsSwitches = 0;
    mNumReqsInflight = 0;
    mWaitForParts = false;
    mMaxChunkSubmitted = 0;

    mDrs_it = mDr->drn->client->drss.insert(mDr->drn->client->drss.end(), this);

    mDr->drn->partiallen = 0;
    mDr->drn->partialstarttime = Waiter::ds;
    mMaxChunkSize = static_cast<unsigned>(static_cast<unsigned>(DirectReadSlot::MAX_DELIVERY_CHUNK) / (mReqs.size() == static_cast<unsigned>(RAIDPARTS) ? (static_cast<unsigned>(RAIDPARTS-1)) : mReqs.size()));
    if (mDr->drbuf.isRaid())
    {
        mMaxChunkSize -= mMaxChunkSize % RAIDSECTOR;
    }
    mMinComparableThroughput = DirectReadSlot::DEFAULT_MIN_COMPARABLE_THROUGHPUT;
    mSlotStartTime = std::chrono::system_clock::now();
}

DirectReadSlot::~DirectReadSlot()
{
    mDr->drn->client->drss.erase(mDrs_it);
    LOG_debug << "Deleting DirectReadSlot";
}

bool priority_comparator(const LazyEraseTransferPtr& i, const LazyEraseTransferPtr& j)
{
    return (i.transfer ? i.transfer->priority : i.preErasurePriority) < (j.transfer ? j.transfer->priority : j.preErasurePriority);
}

TransferList::TransferList()
{
    currentpriority = PRIORITY_START;
}

void TransferList::addtransfer(Transfer *transfer, TransferDbCommitter& committer, bool startFirst)
{
    if (transfer->state != TRANSFERSTATE_PAUSED)
    {
        transfer->state = TRANSFERSTATE_QUEUED;
    }

    assert(transfer->type == PUT || transfer->type == GET);

    if (!transfer->priority)
    {
        if (startFirst && transfers[transfer->type].size())
        {
            transfer_list::iterator dstit = transfers[transfer->type].begin();
            transfer->priority = dstit->transfer->priority - PRIORITY_STEP;
            prepareIncreasePriority(transfer, transfers[transfer->type].end(), dstit, committer);
            transfers[transfer->type].push_front(transfer);
        }
        else
        {
            currentpriority += PRIORITY_STEP;
            transfer->priority = currentpriority;
            assert(!transfers[transfer->type].size() || transfers[transfer->type][transfers[transfer->type].size() - 1]->priority < transfer->priority);
            transfers[transfer->type].push_back(transfer);
        }

        client->transfercacheadd(transfer, &committer);
    }
    else
    {
        transfer_list::iterator it = std::lower_bound(transfers[transfer->type].begin(), transfers[transfer->type].end(), LazyEraseTransferPtr(transfer), priority_comparator);
        assert(it == transfers[transfer->type].end() || it->transfer->priority != transfer->priority);
        transfers[transfer->type].insert(it, transfer);
    }
}

void TransferList::removetransfer(Transfer *transfer)
{
    transfer_list::iterator it;
    if (getIterator(transfer, it, true))
    {
        transfers[transfer->type].erase(it);
    }
}

void TransferList::movetransfer(Transfer *transfer, Transfer *prevTransfer, TransferDbCommitter& committer)
{
    transfer_list::iterator dstit;
    if (getIterator(prevTransfer, dstit))
    {
        movetransfer(transfer, dstit, committer);
    }
}

void TransferList::movetransfer(Transfer *transfer, unsigned int position, TransferDbCommitter& committer)
{
    transfer_list::iterator dstit;
    if (position >= transfers[transfer->type].size())
    {
        dstit = transfers[transfer->type].end();
    }
    else
    {
        dstit = transfers[transfer->type].begin() + position;
    }

    transfer_list::iterator it;
    if (getIterator(transfer, it))
    {
        movetransfer(it, dstit, committer);
    }
}

void TransferList::movetransfer(Transfer *transfer, transfer_list::iterator dstit, TransferDbCommitter& committer)
{
    transfer_list::iterator it;
    if (getIterator(transfer, it))
    {
        movetransfer(it, dstit, committer);
    }
}

void TransferList::movetransfer(transfer_list::iterator it, transfer_list::iterator dstit, TransferDbCommitter& committer)
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
    assert(transfer->type == PUT || transfer->type == GET);
    if (dstit == transfers[transfer->type].end())
    {
        LOG_debug << "Moving transfer to the last position";
        prepareDecreasePriority(transfer, it, dstit);

        transfers[transfer->type].erase(it);
        currentpriority += PRIORITY_STEP;
        transfer->priority = currentpriority;
        assert(!transfers[transfer->type].size() || transfers[transfer->type][transfers[transfer->type].size() - 1]->priority < transfer->priority);
        transfers[transfer->type].push_back(transfer);
        client->transfercacheadd(transfer, &committer);
        client->app->transfer_update(transfer);
        return;
    }

    int srcindex = int(std::distance(transfers[transfer->type].begin(), it));
    int dstindex = int(std::distance(transfers[transfer->type].begin(), dstit));
    LOG_debug << "Moving transfer from " << srcindex << " to " << dstindex;

    uint64_t prevpriority = 0;
    uint64_t nextpriority = 0;

    nextpriority = dstit->transfer->priority;
    if (dstit != transfers[transfer->type].begin())
    {
        transfer_list::iterator previt = dstit - 1;
        prevpriority = previt->transfer->priority;
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
            client->transfercacheadd(t, &committer);
            client->app->transfer_update(t);
            fixedPriority += PRIORITY_STEP;
        }
        newpriority = fixedPriority;
        LOG_debug << "Fixed priority: " << fixedPriority;
    }

    transfer->priority = newpriority;
    if (srcindex > dstindex)
    {
        prepareIncreasePriority(transfer, it, dstit, committer);
    }
    else
    {
        prepareDecreasePriority(transfer, it, dstit);
        dstindex--;
    }

    transfers[transfer->type].erase(it);
    transfer_list::iterator fit = transfers[transfer->type].begin() + dstindex;
    assert(fit == transfers[transfer->type].end() || fit->transfer->priority != transfer->priority);
    transfers[transfer->type].insert(fit, transfer);
    client->transfercacheadd(transfer, &committer);
    client->app->transfer_update(transfer);
}

void TransferList::movetofirst(Transfer *transfer, TransferDbCommitter& committer)
{
    movetransfer(transfer, transfers[transfer->type].begin(), committer);
}

void TransferList::movetofirst(transfer_list::iterator it, TransferDbCommitter& committer)
{
    Transfer *transfer = (*it);
    movetransfer(it, transfers[transfer->type].begin(), committer);
}

void TransferList::movetolast(Transfer *transfer, TransferDbCommitter& committer)
{
    movetransfer(transfer, transfers[transfer->type].end(), committer);
}

void TransferList::movetolast(transfer_list::iterator it, TransferDbCommitter& committer)
{
    Transfer *transfer = (*it);
    movetransfer(it, transfers[transfer->type].end(), committer);
}

void TransferList::moveup(Transfer *transfer, TransferDbCommitter& committer)
{
    transfer_list::iterator it;
    if (getIterator(transfer, it))
    {
        if (it == transfers[transfer->type].begin())
        {
            return;
        }
        transfer_list::iterator dstit = it - 1;
        movetransfer(it, dstit, committer);
    }
}

void TransferList::moveup(transfer_list::iterator it, TransferDbCommitter& committer)
{
    if (it == transfers[it->transfer->type].begin())
    {
        return;
    }

    transfer_list::iterator dstit = it - 1;
    movetransfer(it, dstit, committer);
}

void TransferList::movedown(Transfer *transfer, TransferDbCommitter& committer)
{
    transfer_list::iterator it;
    if (getIterator(transfer, it))
    {

        transfer_list::iterator dstit = it + 1;
        if (dstit == transfers[transfer->type].end())
        {
            return;
        }

        dstit++;
        movetransfer(it, dstit, committer);
    }
}

void TransferList::movedown(transfer_list::iterator it, TransferDbCommitter& committer)
{
    if (it == transfers[it->transfer->type].end())
    {
        return;
    }

    transfer_list::iterator dstit = it + 1;
    movetransfer(it, dstit, committer);
}

error TransferList::pause(Transfer *transfer, bool enable, TransferDbCommitter& committer)
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
        transfer->state = TRANSFERSTATE_QUEUED;

        transfer_list::iterator it;
        if (getIterator(transfer, it))
        {
            prepareIncreasePriority(transfer, it, it, committer);
        }

        client->transfercacheadd(transfer, &committer);
        client->app->transfer_update(transfer);
        return API_OK;
    }

    if (transfer->state == TRANSFERSTATE_ACTIVE
            || transfer->state == TRANSFERSTATE_QUEUED
            || transfer->state == TRANSFERSTATE_RETRYING)
    {
        if (transfer->slot)
        {
            if (transfer->client->ststatus != STORAGE_RED || transfer->type == GET)
            {
                transfer->bt.arm();
            }
            delete transfer->slot;
            transfer->slot = NULL;
        }
        transfer->state = TRANSFERSTATE_PAUSED;
        client->transfercacheadd(transfer, &committer);
        client->app->transfer_update(transfer);
        return API_OK;
    }

    return API_EFAILED;
}

auto TransferList::begin(direction_t direction) -> transfer_list::iterator
{
    return transfers[direction].begin();
}

auto TransferList::end(direction_t direction) -> transfer_list::iterator
{
    return transfers[direction].end();
}

bool TransferList::getIterator(Transfer *transfer, transfer_list::iterator& it, bool canHandleErasedElements)
{
    assert(transfer);
    if (!transfer)
    {
        LOG_err << "Getting iterator of a NULL transfer";
        return false;
    }

    assert(transfer->type == GET || transfer->type == PUT);
    if (transfer->type != GET && transfer->type != PUT)
    {
        LOG_err << "Getting iterator of wrong transfer type " << transfer->type;
        return false;
    }

    it = std::lower_bound(transfers[transfer->type].begin(canHandleErasedElements), transfers[transfer->type].end(canHandleErasedElements), LazyEraseTransferPtr(transfer), priority_comparator);
    if (it != transfers[transfer->type].end(canHandleErasedElements) && it->transfer == transfer)
    {
        return true;
    }
    LOG_debug << "Transfer not found";
    return false;
}

std::array<vector<Transfer*>, 6> TransferList::nexttransfers(std::function<bool(Transfer*)>& continuefunction,
                                                             std::function<bool(direction_t)>& directionContinuefunction,
                                                             TransferDbCommitter& committer)
{
    std::array<vector<Transfer*>, 6> chosenTransfers;

    static direction_t putget[] = { PUT, GET };

    for (direction_t direction : putget)
    {
        for (Transfer *transfer : transfers[direction])
        {
            if (!transfer->slot)
            {
                // check for cancellation here before we go to the trouble of requesting a download/upload URL
                transfer->removeCancelledTransferFiles(&committer);
                if (transfer->files.empty())
                {
                    transfer->removeAndDeleteSelf(TRANSFERSTATE_CANCELLED);
                    continue;
                }
            }

            // don't traverse the whole list if we already have as many as we are going to get
            if (!directionContinuefunction(direction)) break;

            bool continueLarge = true;
            bool continueSmall = true;

            if ((!transfer->slot && isReady(transfer))
                || (transfer->asyncopencontext
                    && transfer->asyncopencontext->finished))
            {
                TransferCategory tc(transfer);

                if (tc.sizetype == LARGEFILE && continueLarge)
                {
                    continueLarge = continuefunction(transfer);
                    if (continueLarge)
                    {
                        chosenTransfers[tc.index()].push_back(transfer);
                    }
                }
                else if (tc.sizetype == SMALLFILE && continueSmall)
                {
                    continueSmall = continuefunction(transfer);
                    if (continueSmall)
                    {
                        chosenTransfers[tc.index()].push_back(transfer);
                    }
                }
                if (!continueLarge && !continueSmall)
                {
                    break;
                }
            }
        }
    }
    return chosenTransfers;
}

Transfer *TransferList::transferat(direction_t direction, unsigned int position)
{
    if (transfers[direction].size() > position)
    {
        return transfers[direction][position];
    }
    return NULL;
}

void TransferList::prepareIncreasePriority(Transfer *transfer, transfer_list::iterator /*srcit*/, transfer_list::iterator dstit, TransferDbCommitter& committer)
{
    assert(transfer->type == PUT || transfer->type == GET);
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
            if (lastActiveTransfer->client->ststatus != STORAGE_RED || lastActiveTransfer->type == GET)
            {
                lastActiveTransfer->bt.arm();
            }
            delete lastActiveTransfer->slot;
            lastActiveTransfer->slot = NULL;
            lastActiveTransfer->state = TRANSFERSTATE_QUEUED;
            client->transfercacheadd(lastActiveTransfer, &committer);
            client->app->transfer_update(lastActiveTransfer);
        }
    }
}

void TransferList::prepareDecreasePriority(Transfer *transfer, transfer_list::iterator it, transfer_list::iterator dstit)
{
    assert(transfer->type == PUT || transfer->type == GET);
    if (transfer->slot && transfer->state == TRANSFERSTATE_ACTIVE)
    {
        transfer_list::iterator cit = it + 1;
        while (cit != transfers[transfer->type].end())
        {
            if (!cit->transfer->slot && isReady(*cit))
            {
                if (transfer->client->ststatus != STORAGE_RED || transfer->type == GET)
                {
                    transfer->bt.arm();
                }
                delete transfer->slot;
                transfer->slot = NULL;
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
