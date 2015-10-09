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
    
    faputcompletion_it = client->faputcompletion.end();
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
        (*it)->transfer = NULL;
        (*it)->terminated();
    }

    if (transfers_it != client->transfers[type].end())
    {
        client->transfers[type].erase(transfers_it);
    }

    if (slot)
    {
        delete slot;
    }
}

// transfer attempt failed, notify all related files, collect request on
// whether to abort the transfer, kill transfer if unanimous
void Transfer::failed(error e)
{
    bool defer = false;

    LOG_debug << "Transfer failed with error " << e;

    bt.backoff();

    client->app->transfer_failed(this, e);

    for (file_list::iterator it = files.begin(); it != files.end(); it++)
    {
        if ((*it)->failed(e) && !defer)
        {
            defer = true;
        }
    }

    if (defer)
    {
        failcount++;
        delete slot;

        LOG_debug << "Deferring transfer " << failcount;
    }
    else
    {
        LOG_debug << "Removing transfer";

        client->app->transfer_removed(this);
        delete this;
    }
}

// transfer completion: copy received file locally, set timestamp(s), verify
// fingerprint, notify app, notify files
void Transfer::complete()
{
    LOG_debug << "Transfer complete: " << (files.size() ? files.front()->name : "NO_FILES") << " " << files.size();

    if (type == GET)
    {
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

        if (!transient_error && fa->fopen(&localfilename, true, false))
        {
            fingerprint.genfingerprint(fa);

            if (isvalid && !(fingerprint == *(FileFingerprint*)this))
            {
                if (!badfp.isvalid || !(badfp == fingerprint))
                {
                    badfp = fingerprint;
                    delete fa;
                    client->fsaccess->unlinklocal(&localfilename);
                    return failed(API_EWRITE);
                }
                else
                {
                    fixfingerprint = true;
                }
            }
        }
#ifdef ENABLE_SYNC
        else
        {
            if (!transient_error)
            {
                transient_error = fa->retry;
                LOG_debug << "Unable to validate fingerprint " << transient_error;
            }
        }
#endif
        delete fa;

        int missingattr = 0;
        handle attachh;
        SymmCipher* symmcipher;

        if (!transient_error)
        {
            // set FileFingerprint on source node(s) if missing
            for (file_list::iterator it = files.begin(); it != files.end(); it++)
            {
                if ((*it)->hprivate && (n = client->nodebyhandle((*it)->h)))
                {
                    if (client->gfx && client->gfx->isgfx(&(*it)->localname))
                    {
                        // check for missing imagery
                        if (!n->hasfileattribute(GfxProc::THUMBNAIL120X120)) missingattr |= 1 << GfxProc::THUMBNAIL120X120;
                        if (!n->hasfileattribute(GfxProc::PREVIEW1000x1000)) missingattr |= 1 << GfxProc::PREVIEW1000x1000;
                        attachh = n->nodehandle;
                        symmcipher = n->nodecipher();
                    }

                    if (fingerprint.isvalid && (!n->isvalid || fixfingerprint))
                    {
                        *(FileFingerprint*)n = fingerprint;

                        n->serializefingerprint(&n->attrs.map['c']);
                        client->setattr(n);
                    }
                }
            }

            if (fingerprint.isvalid && fixfingerprint)
            {
                (*(FileFingerprint*)this) = fingerprint;
            }

            if (missingattr)
            {
                // FIXME: do this while file is still open
                client->gfx->gendimensionsputfa(NULL, &localfilename, attachh, symmcipher, missingattr);
            }

            // ...and place it in all target locations. first, update the files'
            // local target filenames, in case they have changed during the upload
            for (file_list::iterator it = files.begin(); it != files.end(); it++)
            {
                (*it)->updatelocalname();
            }

            // place file in all target locations - use up to one renames, copy
            // operations for the rest
            // remove and complete successfully completed files
            for (file_list::iterator it = files.begin(); it != files.end(); )
            {
                transient_error = false;
                success = false;
                localname = (*it)->localname;

                fa = client->fsaccess->newfileaccess();
                if (fa->fopen(&localname))
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
                        } while (fa->fopen(&localnewname));


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

                if (!tmplocalname.size())
                {
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

                if (success || !transient_error)
                {
                    if (success)
                    {
                        // prevent deletion of associated Transfer object in completed()
                        (*it)->transfer = NULL;
                        (*it)->completed(this, NULL);
                    }

                    if (success || !(*it)->failed(API_EAGAIN))
                    {
                        File* f = (*it);
                        files.erase(it++);
                        if(!success)
                        {
                            LOG_warn << "Unable to complete transfer due to a persistent error";

                            f->transfer = NULL;
                            f->terminated();
                        }
                    }
                    else
                    {
                        LOG_debug << "Persistent error completing file";
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
            localfilename = localname;
            client->app->transfer_complete(this);
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
        // files must not change during a PUT transfer
        if (genfingerprint(slot->fa, true))
        {
            return failed(API_EREAD);
        }

        // if this transfer is put on hold, do not complete
        client->checkfacompletion(uploadhandle, this);
        return;
    }
}

void Transfer::completefiles()
{
    // notify all files and give them an opportunity to self-destruct
    for (file_list::iterator it = files.begin(); it != files.end(); )
    {
        // prevent deletion of associated Transfer object in completed()
        (*it)->transfer = NULL;
        (*it)->completed(this, NULL);
        files.erase(it++);
    }
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
    schedule(NEVER);

    if (pendingcmd)
    {
        pendingcmd->cancel();
        pendingcmd = NULL;
    }
    
    if (reads.empty())
    {
        delete this;
    }
    else
    {
        for (dr_list::iterator it = reads.begin(); it != reads.end(); it++)
        {
            assert((*it)->drq_it == client->drq.end());
            assert(!(*it)->drs);
        }

        pendingcmd = new CommandDirectRead(this);

        client->reqs.add(pendingcmd);
    }
}

// abort all active reads, remove pending reads and reschedule with app-supplied backoff
void DirectReadNode::retry(error e)
{
    dstime retryds, minretryds = NEVER;

    retries++;

    // signal failure to app , obtain minimum desired retry time
    for (dr_list::iterator it = reads.begin(); it != reads.end(); it++)
    {
        (*it)->abort();

        retryds = client->app->pread_failure(e, retries, (*it)->appdata);

        if (retryds < minretryds)
        {
            minretryds = retryds;
        }
    }

    if (!minretryds)
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
            delete this;
        }
    }
}

void DirectReadNode::cmdresult(error e)
{
    pendingcmd = NULL;

    if (e == API_OK)
    {
        // feed all pending reads to the global read queue
        for (dr_list::iterator it = reads.begin(); it != reads.end(); it++)
        {
            (*it)->drq_it = client->drq.insert(client->drq.end(), *it);
        }

        schedule(NEVER);
    }
    else
    {
        retry(e);
    }
}

void DirectReadNode::schedule(dstime deltads)
{            
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

            dr->drn->schedule(1800);

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

            if (dr->drn->client->app->pread_data((byte*)req->in.data(), t, pos, dr->appdata))
            {
                pos += t;

                req->in.clear();
                req->contentlength -= t;
                req->bufpos = 0;               
            }
            else
            {
                // app-requested abort
                delete dr;
                return false;
            }
        }

        if (req->status == REQ_SUCCESS)
        {
            dr->drn->schedule(3000);

            // remove and delete completed read request, then remove slot
            delete dr;
            return true;
        }
    }
    else if (req->status == REQ_FAILURE)
    {
        // a failure triggers a complete abort and retry of all pending reads for this node
        dr->drn->retry(API_EREAD);
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
        // no tempurl yet
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

    pos = dr->offset;

    req = new HttpReq(true);

    sprintf(buf,"/%" PRIu64 "-", dr->offset);

    if (dr->count)
    {
        sprintf(strchr(buf, 0), "%" PRIu64, dr->offset + dr->count - 1);
    }

    req->posturl = dr->drn->tempurl;
    req->posturl.append(buf);
    req->type = REQ_BINARY;

    req->post(dr->drn->client);

    drs_it = dr->drn->client->drss.insert(dr->drn->client->drss.end(), this);
}

DirectReadSlot::~DirectReadSlot()
{
    dr->drn->client->drss.erase(drs_it);

    delete req;
}
} // namespace
