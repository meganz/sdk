/**
 * @file transferslot.cpp
 * @brief Class for active transfer
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

#include "mega/transferslot.h"
#include "mega/node.h"
#include "mega/transfer.h"
#include "mega/megaclient.h"
#include "mega/command.h"
#include "mega/base64.h"
#include "mega/megaapp.h"
#include "mega/utils.h"
#include "mega/logging.h"

namespace mega {
TransferSlot::TransferSlot(Transfer* ctransfer)
{
    starttime = 0;
    lastprogressreport = 0;
    progressreported = 0;
    speed = meanSpeed = 0;

    lastdata = Waiter::ds;
    errorcount = 0;
    lasterror = API_OK;

    failure = false;
    retrying = false;
    
    fileattrsmutable = 0;

    reqs = NULL;
    pendingcmd = NULL;

    transfer = ctransfer;
    transfer->slot = this;
    transfer->state = TRANSFERSTATE_ACTIVE;

    connections = transfer->size > 131072 ? transfer->client->connections[transfer->type] : 1;
    LOG_debug << "Creating transfer slot with " << connections << " connections";

    reqs = new HttpReqXfer*[connections]();

    fa = transfer->client->fsaccess->newfileaccess();

    slots_it = transfer->client->tslots.end();
}

// delete slot and associated resources, but keep transfer intact (can be
// reused on a new slot)
TransferSlot::~TransferSlot()
{
    if (transfer->type == GET && !transfer->finished
            && transfer->progresscompleted != transfer->size)
    {
        m_off_t p = 0;
        bool cachetransfer = false; // need to save in cache
        for (int i = 0; i < connections; i++)
        {
            if (fa && reqs[i] && reqs[i]->status == REQ_INFLIGHT
                    && reqs[i]->contentlength == reqs[i]->size
                    && reqs[i]->bufpos >= SymmCipher::BLOCKSIZE)
            {
                m_off_t bufpos = reqs[i]->bufpos & -SymmCipher::BLOCKSIZE;
                m_off_t dlpos = ((HttpReqDL *)reqs[i])->dlpos;

                byte *bufstart = reqs[i]->buf;
                m_off_t buflen = bufpos;
                m_off_t startpos = dlpos;   // in the file
                m_off_t endpos = ChunkedHash::chunkceil(startpos, transfer->size);  // in the file
                m_off_t finalpos = startpos + buflen;

                while (endpos <= finalpos)
                {
                    m_off_t chunksize = endpos - startpos;
                    reqs[i]->finalize(fa, &transfer->key, &transfer->chunkmacs, transfer->ctriv, startpos, endpos);
                    transfer->progresscompleted += chunksize;
                    LOG_debug << "Caching finished chunk: " << startpos << " - " << endpos << "   Size: " << chunksize;

                    startpos = endpos;
                    endpos = ChunkedHash::chunkceil(startpos, transfer->size);

                    bufstart += chunksize;
                    buflen -= chunksize;
                    cachetransfer = true;
                }

                if (buflen >= SymmCipher::BLOCKSIZE)
                {
                    ChunkMAC &chunk = transfer->chunkmacs[ChunkedHash::chunkfloor(startpos)];
                    transfer->key.ctr_crypt(bufstart, buflen, startpos, transfer->ctriv,
                                chunk.mac, false, !chunk.finished && !chunk.offset);

                    fa->fwrite((const byte*)bufstart, buflen, startpos);
                    chunk.offset += (unsigned int)buflen;
                    cachetransfer = true;
                    p += buflen;
                }
            }
        }

        if (cachetransfer)
        {
            transfer->client->transfercacheadd(transfer);
            LOG_debug << "Completed: " << (transfer->progresscompleted + p)
                      << " Partial: " << p;
        }
    }

    transfer->slot = NULL;

    if (slots_it != transfer->client->tslots.end())
    {
        // advance main loop iterator if deleting next in line
        if (transfer->client->slotit != transfer->client->tslots.end() && *transfer->client->slotit == this)
        {
            transfer->client->slotit++;
        }

        transfer->client->tslots.erase(slots_it);
    }

    if (pendingcmd)
    {
        pendingcmd->cancel();
    }

    if (fa)
    {
        delete fa;
    }

    while (connections--)
    {
        delete reqs[connections];
    }

    delete[] reqs;
}

void TransferSlot::toggleport(HttpReqXfer *req)
{
    if (!memcmp(req->posturl.c_str(), "http:", 5))
    {
       size_t portendindex = req->posturl.find("/", 8);
       size_t portstartindex = req->posturl.find(":", 8);

       if (portendindex != string::npos)
       {
           if (portstartindex == string::npos)
           {
               LOG_debug << "Enabling alternative port for chunk";
               req->posturl.insert(portendindex, ":8080");
           }
           else
           {
               LOG_debug << "Disabling alternative port for chunk";
               req->posturl.erase(portstartindex, portendindex - portstartindex);
           }
       }
    }
}

// abort all HTTP connections
void TransferSlot::disconnect()
{
    for (int i = connections; i--;)
    {
        if (reqs[i])
        {
            reqs[i]->disconnect();
        }
    }
}

// coalesce block macs into file mac
int64_t TransferSlot::macsmac(chunkmac_map* macs)
{
    byte mac[SymmCipher::BLOCKSIZE] = { 0 };

    for (chunkmac_map::iterator it = macs->begin(); it != macs->end(); it++)
    {
        SymmCipher::xorblock(it->second.mac, mac);
        transfer->key.ecb_encrypt(mac);
    }

    uint32_t* m = (uint32_t*)mac;

    m[0] ^= m[1];
    m[1] = m[2] ^ m[3];

    return MemAccess::get<int64_t>((const char*)mac);
}

// file transfer state machine
void TransferSlot::doio(MegaClient* client)
{
    if (!fa || (transfer->size && transfer->progresscompleted == transfer->size))
    {
        if (transfer->type == GET || transfer->ultoken)
        {
            if (fa && transfer->type == GET)
            {
                LOG_debug << "Verifying cached download";
                transfer->currentmetamac = macsmac(&transfer->chunkmacs);
                transfer->hascurrentmetamac = true;

                // verify meta MAC
                if (transfer->currentmetamac == transfer->metamac)
                {
                    return transfer->complete();
                }
                else
                {
                    int creqtag = client->reqtag;
                    client->reqtag = 0;
                    client->sendevent(99432, "MAC verification failed for cached download");
                    client->reqtag = creqtag;

                    transfer->chunkmacs.clear();
                    return transfer->failed(API_EKEY);
                }
            }

            // this is a pending completion, retry every 200 ms by default
            retrybt.backoff(2);
            retrying = true;

            return transfer->complete();
        }
        else
        {
            int creqtag = client->reqtag;
            client->reqtag = 0;
            client->sendevent(99410, "No upload token available");
            client->reqtag = creqtag;

            return transfer->failed(API_EINTERNAL);
        }
    }

    retrying = false;

    if (!tempurl.size())
    {
        return;
    }

    dstime backoff = 0;
    m_off_t p = 0;

    if (errorcount > 4)
    {
        return transfer->failed(lasterror);
    }

    for (int i = connections; i--; )
    {
        if (reqs[i])
        {
            switch (reqs[i]->status)
            {
                case REQ_INFLIGHT:
                    p += reqs[i]->transferred(client);
                    break;

                case REQ_SUCCESS:
                    lastdata = Waiter::ds;
                    transfer->lastaccesstime = time(NULL);

                    LOG_debug << "Chunk finished OK (" << transfer->type << ") Pos: " << transfer->pos
                              << " Completed: " << (transfer->progresscompleted + reqs[i]->size) << " of " << transfer->size;

                    if (transfer->type == PUT)
                    {
                        // completed put transfers are signalled through the
                        // return of the upload token
                        if (reqs[i]->in.size())
                        {
                            if (reqs[i]->in.size() == NewNode::UPLOADTOKENLEN)
                            {                                        
                                LOG_debug << "Upload token received";
                                if (!transfer->ultoken)
                                {
                                    transfer->ultoken = new byte[NewNode::UPLOADTOKENLEN]();
                                }

                                bool tokenOK = true;
                                if (reqs[i]->in.data()[NewNode::UPLOADTOKENLEN - 1] == 1)
                                {
                                    LOG_debug << "New style upload token";
                                    memcpy(transfer->ultoken, reqs[i]->in.data(), NewNode::UPLOADTOKENLEN);
                                }
                                else
                                {
                                    LOG_debug << "Old style upload token: " << reqs[i]->in;
                                    tokenOK = (Base64::atob(reqs[i]->in.data(), transfer->ultoken, NewNode::UPLOADTOKENLEN)
                                               == NewNode::OLDUPLOADTOKENLEN);
                                }

                                if (tokenOK)
                                {
                                    errorcount = 0;
                                    transfer->failcount = 0;
                                    transfer->chunkmacs[reqs[i]->pos].finished = true;
                                    transfer->progresscompleted += reqs[i]->size;
                                    memcpy(transfer->filekey, transfer->key.key, sizeof transfer->key.key);
                                    ((int64_t*)transfer->filekey)[2] = transfer->ctriv;
                                    ((int64_t*)transfer->filekey)[3] = macsmac(&transfer->chunkmacs);
                                    SymmCipher::xorblock(transfer->filekey + SymmCipher::KEYLENGTH, transfer->filekey);

                                    client->transfercacheadd(transfer);

                                    if (transfer->progresscompleted != progressreported)
                                    {
                                        progressreported = transfer->progresscompleted;
                                        lastdata = Waiter::ds;

                                        progress();
                                    }

                                    return transfer->complete();
                                }
                                else
                                {
                                    delete [] transfer->ultoken;
                                    transfer->ultoken = NULL;
                                }
                            }

                            LOG_debug << "Error uploading chunk: " << reqs[i]->in;
                            error e = (error)atoi(reqs[i]->in.c_str());
                            if (e == API_EKEY)
                            {
                                int creqtag = client->reqtag;
                                client->reqtag = 0;
                                client->sendevent(99429, "Integrity check failed in upload");
                                client->reqtag = creqtag;

                                lasterror = e;
                                errorcount++;
                                reqs[i]->status = REQ_PREPARED;
                                break;
                            }

                            // fail with returned error
                            return transfer->failed(e);
                        }

                        transfer->chunkmacs[reqs[i]->pos].finished = true;
                        transfer->progresscompleted += reqs[i]->size;

                        if (transfer->progresscompleted == transfer->size)
                        {
                            int creqtag = client->reqtag;
                            client->reqtag = 0;
                            client->sendevent(99409, "No upload token received");
                            client->reqtag = creqtag;

                            return transfer->failed(API_EINTERNAL);
                        }

                        errorcount = 0;
                        transfer->failcount = 0;
                    }
                    else
                    {
                        if (reqs[i]->size == reqs[i]->bufpos)
                        {
                            m_off_t startpos = ((HttpReqDL *)reqs[i])->dlpos;
                            m_off_t finalpos = startpos + reqs[i]->bufpos;
                            while (startpos < finalpos)
                            {
                                m_off_t endpos = ChunkedHash::chunkceil(startpos, transfer->size);
                                m_off_t chunksize = endpos - startpos;
                                reqs[i]->finalize(fa, &transfer->key, &transfer->chunkmacs, transfer->ctriv, startpos, endpos);
                                transfer->progresscompleted += chunksize;
                                LOG_debug << "Finished chunk: " << startpos << " - " << endpos << "   Size: " << chunksize;
                                startpos = endpos;
                            }

                            if (transfer->progresscompleted == transfer->size)
                            {
                                if (transfer->progresscompleted)
                                {
                                    transfer->currentmetamac = macsmac(&transfer->chunkmacs);
                                    transfer->hascurrentmetamac = true;
                                }

                                // verify meta MAC
                                if (!transfer->progresscompleted
                                        || (transfer->currentmetamac == transfer->metamac))
                                {
                                    errorcount = 0;
                                    transfer->failcount = 0;
                                    client->transfercacheadd(transfer);

                                    if (transfer->progresscompleted != progressreported)
                                    {
                                        progressreported = transfer->progresscompleted;
                                        lastdata = Waiter::ds;

                                        progress();
                                    }

                                    return transfer->complete();
                                }
                                else
                                {
                                    int creqtag = client->reqtag;
                                    client->reqtag = 0;
                                    client->sendevent(99431, "MAC verification failed");
                                    client->reqtag = creqtag;

                                    transfer->chunkmacs.clear();
                                    return transfer->failed(API_EKEY);
                                }
                            }
                            errorcount = 0;
                            transfer->failcount = 0;
                        }
                        else
                        {
                            int creqtag = client->reqtag;
                            client->reqtag = 0;
                            client->sendevent(99430, "Invalid chunk size");
                            client->reqtag = creqtag;

                            LOG_warn << "Invalid chunk size: " << reqs[i]->size << " - " << reqs[i]->bufpos;
                            lasterror = API_EREAD;
                            errorcount++;
                            reqs[i]->status = REQ_PREPARED;
                            break;
                        }
                    }

                    client->transfercacheadd(transfer);
                    reqs[i]->status = REQ_READY;
                    break;

                case REQ_FAILURE:
                    LOG_warn << "Failed chunk. HTTP status: " << reqs[i]->httpstatus;
                    if (reqs[i]->httpstatus == 509)
                    {
                        if (reqs[i]->timeleft < 0)
                        {
                            int creqtag = client->reqtag;
                            client->reqtag = 0;
                            client->sendevent(99408, "Overquota without timeleft");
                            client->reqtag = creqtag;
                        }

                        LOG_warn << "Bandwidth overquota from storage server";
                        if (reqs[i]->timeleft > 0)
                        {
                            backoff = reqs[i]->timeleft * 10;
                        }
                        else
                        {
                            // default retry intervals
                            backoff = MegaClient::DEFAULT_BW_OVERQUOTA_BACKOFF_SECS * 10;
                        }

                        return transfer->failed(API_EOVERQUOTA, backoff);
                    }
                    else if (reqs[i]->httpstatus == 403 || reqs[i]->httpstatus == 404)
                    {
                        return transfer->failed(API_EAGAIN);
                    }
                    else
                    {
                        if (!failure)
                        {
                            failure = true;
                            bool changeport = false;

                            if (transfer->type == GET && client->autodownport && !memcmp(tempurl.c_str(), "http:", 5))
                            {
                                LOG_debug << "Automatically changing download port";
                                client->usealtdownport = !client->usealtdownport;
                                changeport = true;
                            }
                            else if (transfer->type == PUT && client->autoupport && !memcmp(tempurl.c_str(), "http:", 5))
                            {
                                LOG_debug << "Automatically changing upload port";
                                client->usealtupport = !client->usealtupport;
                                changeport = true;
                            }

                            client->app->transfer_failed(transfer, API_EFAILED);
                            client->setchunkfailed(&reqs[i]->posturl);

                            if (changeport)
                            {
                                toggleport(reqs[i]);
                            }
                        }
                    }
                    reqs[i]->status = REQ_PREPARED;

                default:
                    ;
            }
        }

        if (!failure)
        {
            if (!reqs[i] || (reqs[i]->status == REQ_READY))
            {
                m_off_t npos = ChunkedHash::chunkceil(transfer->nextpos(), transfer->size);
                if (!transfer->size)
                {
                    transfer->pos = 0;
                }

                if ((npos > transfer->pos) || !transfer->size)
                {
                    if (transfer->type == GET && transfer->size && transfer->pos >= 3670016)
                    {
                        m_off_t maxReqSize = (transfer->size - transfer->progresscompleted) / connections / 2;
                        if (maxReqSize > MAX_DOWNLOAD_REQ_SIZE)
                        {
                            maxReqSize = MAX_DOWNLOAD_REQ_SIZE;
                        }

                        if (maxReqSize > 0x100000)
                        {
                            m_off_t val = 0x100000;
                            while (val <= maxReqSize)
                            {
                                val <<= 1;
                            }
                            maxReqSize = val >> 1;
                            maxReqSize -= 0x100000;
                        }
                        else
                        {
                            maxReqSize = 0;
                        }

                        chunkmac_map::iterator it = transfer->chunkmacs.find(npos);
                        m_off_t reqSize = npos - transfer->pos;
                        while (npos < transfer->size
                               && reqSize <= maxReqSize
                               && (it == transfer->chunkmacs.end()
                                   || (!it->second.finished && !it->second.offset)))
                        {
                            npos = ChunkedHash::chunkceil(npos, transfer->size);
                            reqSize = npos - transfer->pos;
                            it = transfer->chunkmacs.find(npos);
                        }
                        LOG_debug << "Downloading chunk of size " << reqSize;
                    }

                    if (!reqs[i])
                    {
                        reqs[i] = transfer->type == PUT ? (HttpReqXfer*)new HttpReqUL() : (HttpReqXfer*)new HttpReqDL();
                    }

                    string finaltempurl = tempurl;
                    if (transfer->type == GET && client->usealtdownport
                            && !memcmp(tempurl.c_str(), "http:", 5))
                    {
                        size_t index = tempurl.find("/", 8);
                        if(index != string::npos && tempurl.find(":", 8) == string::npos)
                        {
                            finaltempurl.insert(index, ":8080");
                        }
                    }

                    if (transfer->type == PUT && client->usealtupport
                            && !memcmp(tempurl.c_str(), "http:", 5))
                    {
                        size_t index = tempurl.find("/", 8);
                        if(index != string::npos && tempurl.find(":", 8) == string::npos)
                        {
                            finaltempurl.insert(index, ":8080");
                        }
                    }

                    if (reqs[i]->prepare(fa, finaltempurl.c_str(), &transfer->key,
                                         &transfer->chunkmacs, transfer->ctriv,
                                         transfer->pos, npos))
                    {
                        reqs[i]->pos = ChunkedHash::chunkfloor(transfer->pos);
                        reqs[i]->status = REQ_PREPARED;
                        transfer->pos = npos;
                    }
                    else
                    {
                        LOG_warn << "Error preparing transfer: " << fa->retry;
                        if (!fa->retry)
                        {
                            return transfer->failed(API_EREAD);
                        }

                        // retry the read shortly
                        backoff = 2;
                    }
                }
                else if (reqs[i])
                {
                    reqs[i]->status = REQ_DONE;
                }
            }

            if (reqs[i] && (reqs[i]->status == REQ_PREPARED))
            {
                reqs[i]->post(client);
            }
        }
    }

    p += transfer->progresscompleted;

    if (p != progressreported || (Waiter::ds - lastprogressreport) > PROGRESSTIMEOUT)
    {
        if (p != progressreported)
        {
            m_off_t diff = p - progressreported;
            speed = speedController.calculateSpeed(diff);
            meanSpeed = speedController.getMeanSpeed();
            if (transfer->type == PUT)
            {
                client->httpio->updateuploadspeed(diff);
            }
            else
            {
                client->httpio->updatedownloadspeed(diff);
            }

            progressreported = p;
            lastdata = Waiter::ds;
        }
        lastprogressreport = Waiter::ds;

        progress();
    }

    if (Waiter::ds - lastdata >= XFERTIMEOUT && !failure)
    {
        LOG_warn << "Failed chunk due to a timeout";
        failure = true;
        bool changeport = false;

        if (transfer->type == GET && client->autodownport && !memcmp(tempurl.c_str(), "http:", 5))
        {
            LOG_debug << "Automatically changing download port due to a timeout";
            client->usealtdownport = !client->usealtdownport;
            changeport = true;
        }
        else if (transfer->type == PUT && client->autoupport && !memcmp(tempurl.c_str(), "http:", 5))
        {
            LOG_debug << "Automatically changing upload port due to a timeout";
            client->usealtupport = !client->usealtupport;
            changeport = true;
        }

        bool chunkfailed = false;
        for (int i = connections; i--; )
        {
            if (reqs[i] && reqs[i]->status == REQ_INFLIGHT)
            {
                chunkfailed = true;
                client->setchunkfailed(&reqs[i]->posturl);
                reqs[i]->disconnect();

                if (changeport)
                {
                    toggleport(reqs[i]);
                }

                reqs[i]->status = REQ_PREPARED;
            }
        }

        if (!chunkfailed)
        {
            LOG_warn << "Transfer failed due to a timeout";
            transfer->failed(API_EAGAIN);
        }
        else
        {
            LOG_warn << "Chunk failed due to a timeout";
            client->app->transfer_failed(transfer, API_EFAILED);
        }
    }

    if (!failure)
    {
        if (!backoff && (Waiter::ds - lastdata) < XFERTIMEOUT)
        {
            // no other backoff: check again at XFERMAXFAIL
            backoff = XFERTIMEOUT - (Waiter::ds - lastdata);
        }

        retrybt.backoff(backoff);
    }
}

// transfer progress notification to app and related files
void TransferSlot::progress()
{
    transfer->client->app->transfer_update(transfer);

    for (file_list::iterator it = transfer->files.begin(); it != transfer->files.end(); it++)
    {
        (*it)->progress();
    }
}
} // namespace
