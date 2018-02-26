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

// transfer attempts are considered failed after XFERTIMEOUT seconds
// without data flow
const dstime TransferSlot::XFERTIMEOUT = 600;

// max time without progress callbacks
const dstime TransferSlot::PROGRESSTIMEOUT = 10;

// max request size for downloads
#if defined(__ANDROID__) || defined(USE_IOS) || defined(WINDOWS_PHONE)
    const m_off_t TransferSlot::MAX_DOWNLOAD_REQ_SIZE = 2097152; // 2 MB
#elif defined (_WIN32) || defined(HAVE_AIO_RT)
    const m_off_t TransferSlot::MAX_DOWNLOAD_REQ_SIZE = 16777216; // 16 MB
#else
    const m_off_t TransferSlot::MAX_DOWNLOAD_REQ_SIZE = 4194304; // 4 MB
#endif

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
    asyncIO = new AsyncIOContext*[connections]();

    fa = transfer->client->fsaccess->newfileaccess();

    slots_it = transfer->client->tslots.end();

    maxDownloadRequestSize = MAX_DOWNLOAD_REQ_SIZE;
#if defined(_WIN32) && !defined(WINDOWS_PHONE)
    MEMORYSTATUSEX statex;
    memset(&statex, 0, sizeof (statex));
    statex.dwLength = sizeof (statex);
    if (GlobalMemoryStatusEx(&statex))
    {
        LOG_debug << "RAM stats. Free physical: " << statex.ullAvailPhys << "   Free virtual: " << statex.ullAvailVirtual;
        if (statex.ullAvailPhys < 536870912 // 512 MB
                || statex.ullAvailVirtual < 536870912)
        {
            if (statex.ullAvailPhys < 268435456 // 256 MB
                    || statex.ullAvailVirtual < 268435456)
            {
                if (statex.ullAvailPhys < 134217728 // 128 MB
                        || statex.ullAvailVirtual < 134217728)
                {
                    maxDownloadRequestSize = 2097152; // 2 MB
                }
                else
                {
                    maxDownloadRequestSize = 4194304; // 4 MB
                }
            }
            else
            {
                maxDownloadRequestSize = 8388608; // 8 MB
            }
        }
        else
        {
            maxDownloadRequestSize = 16777216; // 16 MB
        }
    }
    else
    {
        LOG_warn << "Error getting RAM usage info";
    }
#endif
}

// delete slot and associated resources, but keep transfer intact (can be
// reused on a new slot)
TransferSlot::~TransferSlot()
{
    if (transfer->type == GET && !transfer->finished
            && transfer->progresscompleted != transfer->size
            && !transfer->asyncopencontext)
    {
        bool cachetransfer = false; // need to save in cache

        if (fa && fa->asyncavailable())
        {
            for (int i = 0; i < connections; i++)
            {
                if (reqs[i] && reqs[i]->status == REQ_ASYNCIO && asyncIO[i])
                {
                    asyncIO[i]->finish();
                    if (!asyncIO[i]->failed)
                    {
                        LOG_verbose << "Async write succeeded";
                        HttpReqDL *downloadRequest = (HttpReqDL *)reqs[i];
                        for (chunkmac_map::iterator it = downloadRequest->chunkmacs.begin(); it != downloadRequest->chunkmacs.end(); it++)
                        {
                            transfer->chunkmacs[it->first] = it->second;
                        }
                        downloadRequest->chunkmacs.clear();
                        transfer->progresscompleted += downloadRequest->bufpos;
                        LOG_debug << "Cached async data at: " << downloadRequest->dlpos << "   Size: " << downloadRequest->bufpos;
                        cachetransfer = true;
                    }
                }
                delete asyncIO[i];
                asyncIO[i] = NULL;
            }

            // Open the file in synchonous mode
            delete fa;
            fa = transfer->client->fsaccess->newfileaccess();
            if (!fa->fopen(&transfer->localfilename, false, true))
            {
                delete fa;
                fa = NULL;
            }
        }

        for (int i = 0; i < connections; i++)
        {
            HttpReqDL *downloadRequest = (HttpReqDL *)reqs[i];
            if (fa && downloadRequest && downloadRequest->status == REQ_INFLIGHT
                    && downloadRequest->contentlength == downloadRequest->size
                    && downloadRequest->bufpos >= SymmCipher::BLOCKSIZE)
            {
                downloadRequest->finalize(transfer);
                m_off_t dlpos = downloadRequest->dlpos;
                m_off_t bufsize = downloadRequest->bufpos;
                if (fa->fwrite(downloadRequest->buf, bufsize, dlpos))
                {
                    LOG_verbose << "Sync write succeeded";
                    for (chunkmac_map::iterator it = downloadRequest->chunkmacs.begin(); it != downloadRequest->chunkmacs.end(); it++)
                    {
                        transfer->chunkmacs[it->first] = it->second;
                    }
                    downloadRequest->chunkmacs.clear();
                    transfer->progresscompleted += bufsize;
                    LOG_debug << "Cached data at: " << dlpos << "   Size: " << bufsize;
                    cachetransfer = true;
                }
                else
                {
                    LOG_err << "Error caching data at: " << dlpos;
                }
            }
        }

        if (cachetransfer)
        {
            transfer->client->transfercacheadd(transfer);
            LOG_debug << "Completed: " << transfer->progresscompleted;
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

    if (transfer->asyncopencontext)
    {
        delete transfer->asyncopencontext;
        transfer->asyncopencontext = NULL;
        transfer->client->asyncfopens--;
    }

    while (connections--)
    {
        delete asyncIO[connections];
        delete reqs[connections];
    }

    delete[] asyncIO;
    delete[] reqs;

    if (fa)
    {
        delete fa;
    }
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

    SymmCipher *cipher = transfer->transfercipher();
    for (chunkmac_map::iterator it = macs->begin(); it != macs->end(); it++)
    {
        SymmCipher::xorblock(it->second.mac, mac);
        cipher->ecb_encrypt(mac);
    }

    uint32_t* m = (uint32_t*)mac;

    m[0] ^= m[1];
    m[1] = m[2] ^ m[3];

    return MemAccess::get<int64_t>((const char*)mac);
}

// file transfer state machine
void TransferSlot::doio(MegaClient* client)
{
    if (!fa || (transfer->size && transfer->progresscompleted == transfer->size)
            || (transfer->type == PUT && transfer->ultoken))
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
        LOG_warn << "Failed transfer: too many errors";
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
                    if (client->orderdownloadedchunks && transfer->type == GET && transfer->progresscompleted != ((HttpReqDL *)reqs[i])->dlpos)
                    {
                        // postponing unsorted chunk
                        p += reqs[i]->size;
                        break;
                    }

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
                                    memcpy(transfer->filekey, transfer->transferkey, sizeof transfer->transferkey);
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

                            if (reqs[i]->contenttype.find("text/html") != string::npos
                                    && !memcmp(reqs[i]->posturl.c_str(), "http:", 5))
                            {
                                LOG_warn << "Invalid Content-Type detected during upload: " << reqs[i]->contenttype;
                                client->usehttps = true;
                                client->app->notify_change_to_https();

                                int creqtag = client->reqtag;
                                client->reqtag = 0;
                                client->sendevent(99436, "Automatic change to HTTPS");
                                client->reqtag = creqtag;

                                return transfer->failed(API_EAGAIN);
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
                        client->transfercacheadd(transfer);
                        reqs[i]->status = REQ_READY;
                    }
                    else
                    {
                        if (reqs[i]->size == reqs[i]->bufpos)
                        {
                            HttpReqDL *downloadRequest = (HttpReqDL *)reqs[i];
                            if (fa->asyncavailable())
                            {
                                if (!asyncIO[i])
                                { 
                                    downloadRequest->finalize(transfer);
                                }
                                else
                                {
                                    LOG_warn << "Retrying failed async write";
                                    delete asyncIO[i];
                                    asyncIO[i] = NULL;
                                }

                                p += reqs[i]->size;

                                LOG_debug << "Writting data asynchronously at " << downloadRequest->dlpos;
                                asyncIO[i] = fa->asyncfwrite(downloadRequest->buf, downloadRequest->bufpos, downloadRequest->dlpos);
                                reqs[i]->status = REQ_ASYNCIO;
                            }
                            else
                            {
                                downloadRequest->finalize(transfer);
                                if (fa->fwrite(downloadRequest->buf, downloadRequest->bufpos, downloadRequest->dlpos))
                                {
                                    LOG_verbose << "Sync write succeeded";
                                    for (chunkmac_map::iterator it = downloadRequest->chunkmacs.begin(); it != downloadRequest->chunkmacs.end(); it++)
                                    {
                                        transfer->chunkmacs[it->first] = it->second;
                                        assert (transfer->chunkmacs[it->first].finished);
                                    }
                                    downloadRequest->chunkmacs.clear();
                                    transfer->progresscompleted += downloadRequest->bufpos;
                                    LOG_debug << "Saved data at: " << downloadRequest->dlpos << "   Size: " << downloadRequest->bufpos;
                                    errorcount = 0;
                                    transfer->failcount = 0;
                                }
                                else
                                {
                                    LOG_err << "Error saving finished chunk";
                                    if (!fa->retry)
                                    {
                                        return transfer->failed(API_EWRITE);
                                    }
                                    lasterror = API_EWRITE;
                                    backoff = 2;
                                    break;
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
                                client->transfercacheadd(transfer);
                                reqs[i]->status = REQ_READY;
                            }
                        }
                        else
                        {
                            if (reqs[i]->contenttype.find("text/html") != string::npos
                                    && !memcmp(reqs[i]->posturl.c_str(), "http:", 5))
                            {
                                LOG_warn << "Invalid Content-Type detected during download: " << reqs[i]->contenttype;
                                client->usehttps = true;
                                client->app->notify_change_to_https();

                                int creqtag = client->reqtag;
                                client->reqtag = 0;
                                client->sendevent(99436, "Automatic change to HTTPS");
                                client->reqtag = creqtag;

                                return transfer->failed(API_EAGAIN);
                            }

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
                    break;

                case REQ_ASYNCIO:
                    if (asyncIO[i]->finished)
                    {
                        LOG_verbose << "Processing finished async fs operation";
                        if (!asyncIO[i]->failed)
                        {
                            if (transfer->type == PUT)
                            {
                                LOG_verbose << "Async read succeeded";
                                m_off_t npos = ChunkedHash::chunkceil(asyncIO[i]->pos, transfer->size);

                                string finaltempurl = tempurl;
                                if (client->usealtupport && !memcmp(tempurl.c_str(), "http:", 5))
                                {
                                    size_t index = tempurl.find("/", 8);
                                    if(index != string::npos && tempurl.find(":", 8) == string::npos)
                                    {
                                        finaltempurl.insert(index, ":8080");
                                    }
                                }

                                reqs[i]->prepare(finaltempurl.c_str(), transfer->transfercipher(),
                                         &transfer->chunkmacs, transfer->ctriv,
                                         asyncIO[i]->pos, npos);

                                reqs[i]->pos = ChunkedHash::chunkfloor(asyncIO[i]->pos);
                                reqs[i]->status = REQ_PREPARED;
                            }
                            else
                            {
                                LOG_verbose << "Async write succeeded";
                                HttpReqDL *downloadRequest = (HttpReqDL *)reqs[i];
                                for (chunkmac_map::iterator it = downloadRequest->chunkmacs.begin(); it != downloadRequest->chunkmacs.end(); it++)
                                {
                                    transfer->chunkmacs[it->first] = it->second;
                                }
                                downloadRequest->chunkmacs.clear();
                                transfer->progresscompleted += downloadRequest->bufpos;
                                LOG_debug << "Saved data at: " << downloadRequest->dlpos << "   Size: " << downloadRequest->bufpos;
                                errorcount = 0;
                                transfer->failcount = 0;

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

                                client->transfercacheadd(transfer);
                                reqs[i]->status = REQ_READY;

                                if (client->orderdownloadedchunks)
                                {
                                    // Check connections again looking for postponed chunks
                                    delete asyncIO[i];
                                    asyncIO[i] = NULL;
                                    i = connections;
                                    continue;
                                }
                            }
                            delete asyncIO[i];
                            asyncIO[i] = NULL;
                        }
                        else
                        {
                            LOG_warn << "Async operation failed: " << asyncIO[i]->retry;
                            if (!asyncIO[i]->retry)
                            {
                                delete asyncIO[i];
                                asyncIO[i] = NULL;
                                return transfer->failed(transfer->type == PUT ? API_EREAD : API_EWRITE);
                            }

                            // retry shortly
                            if (transfer->type == PUT)
                            {
                                lasterror = API_EREAD;
                                reqs[i]->status = REQ_READY;
                            }
                            else
                            {
                                lasterror = API_EWRITE;
                                reqs[i]->status = REQ_SUCCESS;
                            }
                            backoff = 2;
                        }
                    }
                    else if (transfer->type == GET)
                    {
                        p += asyncIO[i]->len;
                    }
                    break;

                case REQ_FAILURE:
                    LOG_warn << "Failed chunk. HTTP status: " << reqs[i]->httpstatus;
                    if (reqs[i]->httpstatus && reqs[i]->contenttype.find("text/html") != string::npos
                            && !memcmp(reqs[i]->posturl.c_str(), "http:", 5))
                    {
                        LOG_warn << "Invalid Content-Type detected on failed chunk: " << reqs[i]->contenttype;
                        client->usehttps = true;
                        client->app->notify_change_to_https();

                        int creqtag = client->reqtag;
                        client->reqtag = 0;
                        client->sendevent(99436, "Automatic change to HTTPS");
                        client->reqtag = creqtag;

                        return transfer->failed(API_EAGAIN);
                    }

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

                if ((npos > transfer->pos) || !transfer->size || (transfer->type == PUT && asyncIO[i]))
                {
                    if (transfer->type == GET && transfer->size)
                    {
                        m_off_t maxReqSize = (transfer->size - transfer->progresscompleted) / connections / 2;
                        if (maxReqSize > maxDownloadRequestSize)
                        {
                            maxReqSize = maxDownloadRequestSize;
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

                    bool prepare = true;
                    if (transfer->type == PUT)
                    {
                        m_off_t pos = transfer->pos;
                        unsigned size = (unsigned)(npos - pos);

                        if (fa->asyncavailable())
                        {
                            if (asyncIO[i])
                            {
                                LOG_warn << "Retrying a failed read";
                                pos = asyncIO[i]->pos;
                                size = asyncIO[i]->len;
                                npos = ChunkedHash::chunkceil(pos, transfer->size);
                                delete asyncIO[i];
                                asyncIO[i] = NULL;
                            }

                            asyncIO[i] = fa->asyncfread(reqs[i]->out, size, (-(int)size) & (SymmCipher::BLOCKSIZE - 1), pos);
                            reqs[i]->status = REQ_ASYNCIO;
                            prepare = false;
                        }
                        else
                        {
                            if (!fa->fread(reqs[i]->out, size, (-(int)size) & (SymmCipher::BLOCKSIZE - 1), transfer->pos))
                            {
                                LOG_warn << "Error preparing transfer: " << fa->retry;
                                if (!fa->retry)
                                {
                                    return transfer->failed(API_EREAD);
                                }

                                // retry the read shortly
                                backoff = 2;
                                npos = transfer->pos;
                                prepare = false;
                            }
                        }
                    }

                    if (prepare)
                    {
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

                        unsigned size = (unsigned)(npos - transfer->pos);
                        if (size > 16777216)
                        {
                            int creqtag = client->reqtag;
                            client->reqtag = 0;
                            client->sendevent(99434, "Invalid request size");
                            client->reqtag = creqtag;

                            transfer->chunkmacs.clear();
                            return transfer->failed(API_EINTERNAL);
                        }

                        reqs[i]->prepare(finaltempurl.c_str(), transfer->transfercipher(),
                                                                 &transfer->chunkmacs, transfer->ctriv,
                                                                 transfer->pos, npos);
                        reqs[i]->pos = ChunkedHash::chunkfloor(transfer->pos);
                        reqs[i]->status = REQ_PREPARED;
                    }

                    if (transfer->pos < npos)
                    {
                        transfer->pos = npos;
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
