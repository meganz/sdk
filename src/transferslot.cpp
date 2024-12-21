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
#include "mega/raid.h"
#include "mega/testhooks.h"

namespace mega {

TransferSlotFileAccess::TransferSlotFileAccess(std::unique_ptr<FileAccess>&& p, Transfer* t)
    : transfer(t)
{
    reset(std::move(p));
}

TransferSlotFileAccess::~TransferSlotFileAccess()
{
    reset();
}

void TransferSlotFileAccess::reset(std::unique_ptr<FileAccess>&& p)
{
    fa = std::move(p);

    // transfer has no slot or slot has no fa: timer is enabled
    transfer->bt.enable(!!p);
}

double stats::TransferSlotStats::failedRequestRatio() const
{
    assert(mNumFailedRequests <= mNumTotalRequests);
    if (!mNumFailedRequests || !mNumTotalRequests)
    {
        return 0;
    }
    return static_cast<double>(mNumFailedRequests) / static_cast<double>(mNumTotalRequests);
}

double stats::TransferSlotStats::averageLatency() const
{
    assert(mTotalConnectTime != -1 || !mNumRequestsWithCalculatedLatency);
    if (mTotalConnectTime <= 0 || !mNumRequestsWithCalculatedLatency)
    {
        return 0;
    }
    return std::round(mTotalConnectTime / static_cast<double>(mNumRequestsWithCalculatedLatency));
}

m_off_t stats::TransferSlotStats::averageStartTransferTime() const
{
    assert(mTotalStartTransferTime != -1 || !mNumRequestsWithCalculatedLatency);
    if (mTotalStartTransferTime <= 0 || !mNumRequestsWithCalculatedLatency)
    {
        return 0;
    }
    return static_cast<m_off_t>(std::round(mTotalStartTransferTime /
                                           static_cast<double>(mNumRequestsWithCalculatedLatency)));
}

// transfer attempts are considered failed after XFERTIMEOUT deciseconds
// without data flow
const dstime TransferSlot::XFERTIMEOUT = 600;

// max time without progress callbacks
const dstime TransferSlot::PROGRESSTIMEOUT = 10;

#if defined(__ANDROID__) || defined(USE_IOS)
    const m_off_t TransferSlot::MAX_REQ_SIZE = 2097152; // 2 MB
#elif defined (_WIN32) || defined(HAVE_AIO_RT)
    const m_off_t TransferSlot::MAX_REQ_SIZE = 41943040; // 40 MB [Previous value: 16777216 -> 16 MB]
#else
    const m_off_t TransferSlot::MAX_REQ_SIZE = 16777216; // 16 MB [Previous value: 4194304 -> 4 MB]
#endif

const m_off_t TransferSlot::MAX_REQ_SIZE_NEW_RAID = 2 * 1024 * 1024; // 2 MB for each raidpart
const m_off_t TransferSlot::UPPER_FILESIZE_LIMIT_FOR_SMALLER_CHUNKS = 25 * 1024 * 1024; // 25 MB
const m_off_t TransferSlot::MIN_FILESIZE_FOR_MULTIPLE_CONNECTIONS = 131072 + 1; // 128 KB + 1 -> legacy value
const m_off_t TransferSlot::MAX_GAP_SIZE = 256 * 1024 * 1024; // 256 MB

TransferSlot::TransferSlot(Transfer* ctransfer)
    : fa(ctransfer->client->fsaccess->newfileaccess(), ctransfer)
    , retrybt(ctransfer->client->rng, ctransfer->client->transferSlotsBackoff)
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

    connections = 0;
    asyncIO = NULL;
    pendingcmd = NULL;

    transfer = ctransfer;
    transfer->slot = this;
    transfer->state = TRANSFERSTATE_ACTIVE;

    slots_it = transfer->client->tslots.end();

    maxRequestSize = MAX_REQ_SIZE;
#if defined(_WIN32)
    MEMORYSTATUSEX statex;
    memset(&statex, 0, sizeof (statex));
    statex.dwLength = sizeof (statex);
    if (GlobalMemoryStatusEx(&statex))
    {
        LOG_debug << "[Windows] RAM stats. Free physical: " << statex.ullAvailPhys << "   Free virtual: " << statex.ullAvailVirtual;
        if (statex.ullAvailPhys < 1073741824 // 1024 MB
                || statex.ullAvailVirtual < 1073741824)
        {
            if (statex.ullAvailPhys < 536870912 // 512 MB
                    || statex.ullAvailVirtual < 536870912)
            {
                if (statex.ullAvailPhys < 268435456 // 256 MB
                        || statex.ullAvailVirtual < 268435456)
                {
                    maxRequestSize = 2097152; // 2 MB
                }
                else
                {
                    maxRequestSize = 4194304; // 4 MB
                }
            }
            else
            {
                maxRequestSize = 8388608; // 8 MB
            }
        }
    }
    else
    {
        LOG_warn << "[Windows] Error getting RAM usage info";
    }
#endif
}

bool TransferSlot::createconnectionsonce()
{
    // delay creating these until we know if it's raid or non-raid
    if (!(connections || reqs.size() || asyncIO))
    {
        if (transferbuf.tempUrlVector().empty())
        {
            return false;   // too soon, we don't know raid / non-raid yet
        }

        connections = transferbuf.isRaid() ? RAIDPARTS : transfer->size >= MIN_FILESIZE_FOR_MULTIPLE_CONNECTIONS ? transfer->client->connections[transfer->type] : 1;
#ifdef MEGASDK_DEBUG_TEST_HOOKS_ENABLED
        if (transfer->size >= MIN_FILESIZE_FOR_MULTIPLE_CONNECTIONS && transferbuf.isNewRaid())
        {
            DEBUG_TEST_HOOK_NUMBER_OF_CONNECTIONS(connections, transfer->client->connections[transfer->type])
        }
#endif
        LOG_debug << "Populating transfer slot with " << connections << " connections, max request size of " << maxRequestSize << " bytes [transferbuf.isNewRaid() = " << transferbuf.isNewRaid() << "] [isDownload = " << (transfer->type == GET) << "]";
        reqs.resize(connections);
        mReqSpeeds.resize(connections);
        asyncIO = new AsyncIOContext*[connections]();

        if (transferbuf.isNewRaid())
        {
            transfer->slot->initCloudRaid(transfer->client);
        }
    }
    return true;
}

// delete slot and associated resources, but keep transfer intact (can be
// reused on a new slot)
TransferSlot::~TransferSlot()
{
    LOG_verbose << "[TransferSlot::~TransferSlot] BEGIN [cloudRaid = " << (void*)(cloudRaid.get()) << "]";
    LOG_verbose << "Deleting TransferSlot";
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
                        LOG_verbose << "[TransferSlot::~TransferSlot] Conn " << i << " : Async write succeeded (size: " << asyncIO[i]->dataBufferLen << ")";
                        transferbuf.bufferWriteCompleted(i, true);
                        cachetransfer = true;
                    }
                    else
                    {
                        LOG_verbose << "[TransferSlot::~TransferSlot] Conn " << i << " : Async write failed (size: " << asyncIO[i]->dataBufferLen << ")";
                        transferbuf.bufferWriteCompleted(i, false);
                    }
                    reqs[i]->status = REQ_READY;
                }
                delete asyncIO[i];
                asyncIO[i] = NULL;
            }

            // Open the file in synchonous mode
            fa.reset(transfer->client->fsaccess->newfileaccess());
            if (!fa->fopen(transfer->localfilename, false, true, FSLogging::logOnError))
            {
                fa.reset();
            }
        }

        for (int i = 0; i < connections; i++)
        {
            if (HttpReqDL *downloadRequest = static_cast<HttpReqDL*>(reqs[i].get()))
            {
                switch (static_cast<reqstatus_t>(downloadRequest->status))
                {
                    case REQ_INFLIGHT:
                        if (fa && downloadRequest && downloadRequest->status == REQ_INFLIGHT
                            && downloadRequest->contentlength == downloadRequest->size
                            && downloadRequest->bufpos >= SymmCipher::BLOCKSIZE)
                        {
                            HttpReq::http_buf_t* buf = downloadRequest->release_buf();
                            buf->end -= buf->datalen() % RAIDSECTOR;
                            transferbuf.submitBuffer(i, new TransferBufferManager::FilePiece(downloadRequest->dlpos, buf)); // resets size & bufpos of downloadrequest.
                        }
                        break;

                    case REQ_DECRYPTING:
                    {
                        LOG_info << "[TransferSlot::~TransferSlot] Conn " << i << " : Waiting for block decryption";
                        std::mutex finalizedMutex;
                        std::unique_lock<std::mutex> guard(finalizedMutex);
                        auto outputPiece = transferbuf.getAsyncOutputBufferPointer(i);
                        outputPiece->finalizedCV.wait(guard, [&](){ return outputPiece->finalized; });
                        downloadRequest->status = REQ_DECRYPTED;
                        break;
                    }

                    default:
                        break;
                }
            }
        }

        bool anyData = true;
        while (anyData)
        {
            anyData = false;
            for (int i = 0; i < connections; ++i)
            {
                // synchronous writes for all remaining outstanding data (for raid, there can be a sequence of output pieces.  for non-raid, one piece per connection)
                // check each connection first and then all that were not yet on a connection
                auto outputPiece = transferbuf.getAsyncOutputBufferPointer(i);
                if (outputPiece)
                {
                    if (!outputPiece->finalized)
                    {
                        SymmCipher *cipher = transfer->client->getRecycledTemporaryTransferCipher(transfer->transferkey.data());
                        outputPiece->finalize(true, transfer->size, transfer->ctriv, cipher, &transfer->chunkmacs);
                    }
                    anyData = true;
                    if (fa && fa->fwrite(outputPiece->buf.datastart(), static_cast<unsigned>(outputPiece->buf.datalen()), outputPiece->pos))
                    {
                        LOG_verbose << "[TransferSlot::~TransferSlot] Conn " << i << " : Sync write succeeded (size: " << outputPiece->buf.datalen() << ")";
                        transferbuf.bufferWriteCompleted(i, true);
                        cachetransfer = true;
                    }
                    else
                    {
                        LOG_err << "[TransferSlot::~TransferSlot] Conn " << i << " : Error caching data at: " << outputPiece->pos << " (size: " << outputPiece->buf.datalen() << ")";
                        transferbuf.bufferWriteCompleted(i, false);  // throws the data away so we can move on to the next one
                    }
                }
            }
        }

        if (cloudRaid)
        {
            LOG_debug << "[TransferSlot::~TransferSlot] Stop cloudRaid";
            cloudRaid->stop();
        }

        if (cachetransfer)
        {
            transfer->client->transfercacheadd(transfer, nullptr);
            LOG_debug << "[TransferSlot::~TransferSlot] Cachetransfer. Completed: " << transfer->progresscompleted;
        }
    }

    // Manage transfer stats before setting the slot to NULL
    LOG_verbose << "[TransferSlot::~TransferSlot] Stats: FailedRequestRatio = "
                << tsStats.failedRequestRatio()
                << " ms. Average latency: " << tsStats.averageLatency()
                << " ms. Average start transfer time: " << tsStats.averageStartTransferTime()
                << " [totalRequests = " << tsStats.mNumTotalRequests
                << ", failedRequests = " << tsStats.mNumFailedRequests
                << ", totalRequestsWithLatency = " << tsStats.mNumRequestsWithCalculatedLatency
                << "] [cloudRaid = " << (void*)(cloudRaid.get()) << "]";
    if (transfer->addTransferStats())
    {
        LOG_verbose << "[TransferSlot::~TransferSlot] Stats for this transfer have been added to "
                       "the global transfer stats";
    }
    else
    {
        LOG_debug << "[TransferSlot::~TransferSlot] Stats for this transfer have NOT been added to "
                     "the global transfer stats";
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
        transfer->client->performanceStats.transferFinishes += 1;
    }

    if (pendingcmd)
    {
        pendingcmd->cancel();
    }

    if (transfer->asyncopencontext)
    {
        transfer->asyncopencontext.reset();
        transfer->client->asyncfopens--;
    }

    while (connections--)
    {
        delete asyncIO[connections];
    }

    delete[] asyncIO;

    LOG_verbose << "[TransferSlot::~TransferSlot] END [cloudRaid = " << (void*)(cloudRaid.get()) << "]";
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
        disconnect(i);
    }
}

// abort one HTTP connection
void TransferSlot::disconnect(unsigned connectionNum)
{
    if (reqs[connectionNum])
    {
        disconnect(reqs[connectionNum]);
    }
}

// abort one HTTP req
void TransferSlot::disconnect(const std::shared_ptr<HttpReqXfer>& req)
{
    req->disconnect();
}

int64_t TransferSlot::macsmac(chunkmac_map* m)
{
    return m->macsmac(transfer->transfercipher());
}

int64_t TransferSlot::macsmac_gaps(chunkmac_map* m, size_t g1, size_t g2, size_t g3, size_t g4)
{
    return m->macsmac_gaps(transfer->transfercipher(), g1, g2, g3, g4);
}

bool TransferSlot::checkMetaMacWithMissingLateEntries()
{
    // Due to an old bug, some uploads attached a MAC to the node that was missing some MAC entries
    // (even though the data was uploaded) - this occurred when a ultoken arrived but one other
    // final upload connection had not completed at the local end (even though it must have
    // completed at the server end).  So the file's data is still complete in the cloud.
    // Here we check if the MAC is one of those with a missing entry (or a few if the connection had multiple chunks)

    // last 3 connections, up to 32MB (ie chunks) each, up to two completing after the one that delivered the ultoken
    size_t end = transfer->chunkmacs.size();
    size_t finalN = std::min<size_t>(32 * 3, end);

    // first check for the most likely - a single connection gap (or two but completely consecutive making a single gap)
    for (size_t countBack = 1; countBack <= finalN; ++countBack)
    {
        size_t start1 = end - countBack;
        for (size_t len1 = 1; len1 <= 64 && start1 + len1 <= end; ++len1)
        {
            if (transfer->metamac == macsmac_gaps(&transfer->chunkmacs, start1, start1 + len1, end, end))
            {
                LOG_warn << "Found mac gaps were at " << start1 << " " << len1 << " from " << end;
                auto correctMac = macsmac(&transfer->chunkmacs);
                transfer->metamac = correctMac;
                // TODO: update the Node's key to be correct (needs some API additions before enabling)
                return true;
            }
        }
    }

    // now check for two separate pieces missing (much less likely)
    // limit to checking up to 16Mb pieces wtih up to 8Mb between to avoid excessive CPU
    // takes about 1 second on a fairly modest laptop for a 100Mb file (in a release build)
    finalN = std::min<size_t>(16 * 2 + 8, transfer->chunkmacs.size());
    for (size_t start1 = end - finalN; start1 < end; ++start1)
    {
        for (size_t len1 = 1; len1 <= 16 && start1 + len1 <= end; ++len1)
        {
            for (size_t start2 = start1 + len1 + 1; start2 < transfer->chunkmacs.size(); ++start2)
            {
                for (size_t len2 = 1; len2 <= 16 && start2 + len2 <= end; ++len2)
                {
                    if (transfer->metamac == macsmac_gaps(&transfer->chunkmacs, start1, start1 + len1, start2, start2 + len2))
                    {
                        LOG_warn << "Found mac gaps were at " << start1 << " " << len1 << " " << start2 << " " << len2 << " from " << end;
                        auto correctMac = macsmac(&transfer->chunkmacs);
                        transfer->metamac = correctMac;
                        // TODO: update the Node's key to be correct (needs some API additions before enabling)
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

bool TransferSlot::checkDownloadTransferFinished(TransferDbCommitter& committer, MegaClient* client)
{
    if (transfer->progresscompleted == transfer->size)
    {
        // verify meta MAC
        if (!transfer->size
            || (macsmac(&transfer->chunkmacs) == transfer->metamac)
            || checkMetaMacWithMissingLateEntries())
        {
            client->transfercacheadd(transfer, &committer);
            if (transfer->progresscompleted != progressreported)
            {
                progressreported = transfer->progresscompleted;
                lastdata = Waiter::ds;

                progress();
            }

            transfer->complete(committer);
        }
        else
        {
            client->sendevent(99431, "MAC verification failed", 0);
            transfer->chunkmacs.clear();
            transfer->failed(API_EKEY, committer);
        }
        return true;
    }
    return false;
}

bool TransferSlot::testForSlowRaidConnection(unsigned connectionNum, bool& incrementErrors)
{
    if (transfer->type == GET && transferbuf.isRaid())
    {
        // quick early check - if we were getting data but haven't for a while
        // then switch channels before we time out entirely (at the halfway-to-timeout mark)
        if ((Waiter::ds - reqs[connectionNum]->lastdata) > (XFERTIMEOUT / 2))
        {
            LOG_warn << "Raid connection " << connectionNum << " has not received data for " << (XFERTIMEOUT / 2) << " deciseconds";
            incrementErrors = true;
            return true;
        }

        if (!transferbuf.isUnusedRaidConection(connectionNum)           // connection in use
                && mReqSpeeds[connectionNum].requestElapsedDs() > 50    // enough elapsed time to be considered
                && mRaidChannelSwapsForSlowness < 2)                    // no more than 2 swaps due to slown connections
        {
            m_off_t averageOtherRate = 0;
            unsigned otherCount = 0;
            for (unsigned j = RAIDPARTS; j--; )
            {
                if (j != connectionNum && !transferbuf.isUnusedRaidConection(j))
                {
                    if (transferbuf.isRaidConnectionProgressBlocked(j)   // this one can't continue because it would get too far ahead
                            || (reqs[j] && reqs[j]->status == REQ_DONE)) // this one reached end of file
                    {
                        ++otherCount;
                        averageOtherRate += mReqSpeeds[j].lastRequestMeanSpeed();
                    }
                    else
                    {
                        return false;
                    }
                }
            }

            averageOtherRate /=  otherCount ? otherCount : 1;
            m_off_t thisRate = mReqSpeeds[connectionNum].lastRequestMeanSpeed();

            if (thisRate < averageOtherRate / 2     // this is less than half of avg of other connections
                    && averageOtherRate > 50 * 1024 // avg is more than 50KB/s
                    && thisRate < 1024 * 1024)      // this is less than 1MB/s
            {
                LOG_warn << "Raid connection " << connectionNum
                         << " is much slower than its peers, with speed " << thisRate
                         << " while they are managing " << averageOtherRate;

                mRaidChannelSwapsForSlowness += 1;
                incrementErrors = false;
                return true;
            }
        }
    }
    return false;
}

// file transfer state machine
void TransferSlot::doio(MegaClient* client, TransferDbCommitter& committer)
{
    CodeCounter::ScopeTimer pbt(client->performanceStats.transferslotDoio);

    if (!fa || (transfer->size && transfer->progresscompleted == transfer->size)
            || (transfer->type == PUT && transfer->ultoken))
    {
        if (transfer->type == GET || transfer->ultoken)
        {
            if (fa && transfer->type == GET)
            {
                LOG_debug << "Verifying cached download";
                // verify meta MAC
                if (macsmac(&transfer->chunkmacs) == transfer->metamac)
                {
                    return transfer->complete(committer);
                }
                else
                {
                    client->sendevent(99432, "MAC verification failed for cached download", 0);

                    transfer->chunkmacs.clear();
                    return transfer->failed(API_EKEY, committer);
                }
            }

            // this is a pending completion, retry every 200 ms by default
            retrybt.backoff(2);
            retrying = true;

            return transfer->complete(committer);
        }
        else
        {
            client->sendevent(99410, "No upload token available", 0);

            return transfer->failed(API_EINTERNAL, committer);
        }
    }

    retrying = false;
    retrybt.reset();  // in case we don't delete the slot, and in case retrybt.next=1
    transfer->state = TRANSFERSTATE_ACTIVE;

    if (!createconnectionsonce()) // don't use connections, reqs, or asyncIO before this point.
    {
        return;
    }

    dstime backoff = 0;
    m_off_t p = 0;
    bool earliestUploadCompleted = false;

    if (errorcount > 4)
    {
        LOG_warn << "Failed transfer: too many errors";
        return transfer->failed(lasterror, committer);
    }

    // main loop over connections
    for (int i = connections; i--; )
    {
        if (reqs[i])
        {
            unsigned slowestStartConnection;
            if (transfer->type == GET && reqs[i]->contentlength == reqs[i]->size && !transferbuf.isNewRaid() && transferbuf.detectSlowestRaidConnection(i, slowestStartConnection))
            {
                LOG_debug << "Connection " << slowestStartConnection << " is the slowest to reply, using the other 5.";
                reqs[slowestStartConnection].reset();
                transferbuf.resetPart(slowestStartConnection);
                i = connections;
                continue;
            }

            if (reqs[i]->status == REQ_FAILURE && reqs[i]->httpstatus == 200 && transfer->type == GET && transferbuf.isRaid())  // the request started out successfully, hence status==200 in the reply headers
            {
                // check if we got some data and the failure occured partway through the part chunk.  If so, best not to waste it, convert to success case with less data
                HttpReqDL *downloadRequest = static_cast<HttpReqDL*>(reqs[i].get());
                LOG_debug << "Connection " << i << " received " << downloadRequest->bufpos << " before failing, processing data.";
                if (downloadRequest->contentlength == downloadRequest->size && downloadRequest->bufpos >= RAIDSECTOR)
                {
                    downloadRequest->bufpos -= downloadRequest->bufpos % RAIDSECTOR;  // always on a raidline boundary
                    downloadRequest->size = unsigned(downloadRequest->bufpos);
                    transferbuf.transferPos(i) = downloadRequest->bufpos;
                    downloadRequest->status = REQ_SUCCESS;
                }
            }

            switch (static_cast<reqstatus_t>(reqs[i]->status))
            {
                case REQ_INFLIGHT:
                {
                    processRequestLatency(reqs[i]);
                    mReqSpeeds[i].requestProgressed(reqs[i]->transferred(client));

                    p += reqs[i]->transferred(client);

                    assert(reqs[i]->lastdata != NEVER);
                    bool incrementErrors = false;
                    if (transfer->type == GET && transferbuf.isRaid()
                            && testForSlowRaidConnection(i, incrementErrors))
                    {
                        // switch to 5 channel raid to avoid the slow/delayed connection. (or if already switched, try a different 5).  If we already tried too many times then let the usual timeout occur
                        if (tryRaidRecoveryFromHttpGetError(i, incrementErrors))
                        {
                            LOG_warn << "Connection " << i << " is slow or stalled, trying the other 5 cloudraid connections";
                            reqs[i]->disconnect();
                            reqs[i]->status = REQ_READY;
                        }
                    }

                    if (EVER(reqs[i]->lastdata) && reqs[i]->lastdata > lastdata)
                    {
                        // prevent overall timeout if all channels are busy with big chunks for a while
                        lastdata = reqs[i]->lastdata;
                    }
                    break;
                }

                case REQ_SUCCESS:
                {
                    processRequestLatency(reqs[i]);
                    mReqSpeeds[i].requestProgressed(reqs[i]->size);

                    if (client->orderdownloadedchunks && transfer->type == GET && !transferbuf.isRaid() && transfer->progresscompleted != static_cast<HttpReqDL*>(reqs[i].get())->dlpos)
                    {
                        LOG_debug << "Conn " << i << " : POSTPONING UNSORTED CHUNK";
                        // postponing unsorted chunk
                        p += reqs[i]->size;
                        break;
                    }

                    lastdata = Waiter::ds;
                    transfer->lastaccesstime = m_time();

                    LOG_debug << "Conn " << i << " : Transfer request finished (" << connDirectionToStr(transfer->type) << ")"
                            << " " << reqs[i]->pos << " - " << (reqs[i]->pos + reqs[i]->size)
                            << "   Size: " << reqs[i]->size
                            << (transferbuf.isRaid() ? string("   Part progress: " + std::to_string(transferbuf.transferPos(i)) + "/" + std::to_string(transferbuf.raidPartSize(i, transfer->size))) : "")
                            << "   (" << (mReqSpeeds[i].lastRequestMeanSpeed() / 1024) << " KB/s)";

                    if (transfer->type == PUT)
                    {
                        // completed put transfers are signalled through the
                        // return of the upload token
                        if (reqs[i]->in.size() == UPLOADTOKENLEN)
                        {
                            LOG_debug << "Conn " << i << " : Upload token received";
                            transfer->ultoken.reset(new UploadToken);
                            memcpy(transfer->ultoken.get(), reqs[i]->in.data(), UPLOADTOKENLEN);

                            errorcount = 0;
                            transfer->failcount = 0;

                            // Before we perform the next loop, check we are not still thread-encrypting some other chunk.
                            // The case where this is relevant is quite obscure: a chunk A was uploaded and processed server
                            // side, but the program exited before receiving and committing those chunk macs.  Now we are
                            // running again and processing the resumed transfer.  There isn't much of the file left to upload
                            // and we have uploaded a small chunk and received the result before even finishing (re-)encrypting
                            // chunk A.  But we do need to include its chunkmacs in the mac-of-macs or we'll assign a wrong mac
                            // to the Node, and think the file is corrupt on download.
                            for (int j = connections; j--; )
                            {
                                if (j != i && reqs[j] &&
                                    reqs[j]->status == REQ_ENCRYPTING)
                                {
                                    LOG_debug << "Conn " << i << " : Connection " << j << " is in REQ_ENCRYPTING status. Waiting for encryption of chunk so we know all chunk macs";
                                    while (reqs[j]->status == REQ_ENCRYPTING)
                                    {
                                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                                    }
                                }
                            }

                            // any other connections that have not reported back yet, or we haven't processed yet,
                            // must have completed also - make sure to include their chunk MACs in the mac-of-macs
                            for (int j = connections; j--; )
                            {
                                if (j != i && reqs[j] &&
                                    (reqs[j]->status == REQ_INFLIGHT
                                    || reqs[j]->status == REQ_SUCCESS
                                    || reqs[j]->status == REQ_FAILURE  // could be a network error getting the result, even though it succeeded server side
                                    || reqs[j]->status == REQ_PREPARED
                                    || reqs[j]->status == REQ_UPLOAD_PREPARED_BUT_WAIT))
                                {
                                    LOG_debug << "Conn " << i << " : Including chunk MACs from incomplete/unprocessed (at this end) from connection " << j;
                                    transfer->progresscompleted += reqs[j]->size;
                                    transfer->chunkmacs.finishedUploadChunks(static_cast<HttpReqUL*>(reqs[j].get())->mChunkmacs);
                                }
                            }

                            transfer->chunkmacs.finishedUploadChunks(static_cast<HttpReqUL*>(reqs[i].get())->mChunkmacs);
                            transfer->progresscompleted += reqs[i]->size;
                            LOG_debug << "Conn " << i << " : Progress completed: " << transfer->progresscompleted << "/" << transfer->size;
                            assert(transfer->progresscompleted == transfer->size);

                            updatecontiguousprogress();

                            memcpy(&transfer->filekey.key, transfer->transferkey.data(), sizeof transfer->transferkey);
                            transfer->filekey.iv_u64 = transfer->ctriv;
                            transfer->filekey.crc_u64 = macsmac(&transfer->chunkmacs);
                            SymmCipher::xorblock(transfer->filekey.iv_bytes.data(), transfer->filekey.key.data());

                            client->transfercacheadd(transfer, &committer);

                            if (transfer->progresscompleted != progressreported)
                            {
                                m_off_t diff = transfer->progresscompleted - progressreported;
                                m_off_t naturalDiff = std::max<m_off_t>(diff, 0);
                                speed = mTransferSpeed.calculateSpeed(naturalDiff);
                                meanSpeed = mTransferSpeed.getMeanSpeed();
                                LOG_verbose
                                    << "[TransferSlot::doio] [Upload] COMPLETED (with a diff)"
                                    << " Speed: " << (speed / 1)
                                    << " KB/s. Mean speed: " << (meanSpeed / 1)
                                    << " KB/s [diff = " << diff << "]"
                                    << " [progressreported = " << progressreported
                                    << ", transfer->progresscompleted = "
                                    << transfer->progresscompleted
                                    << "] [transfer->size = " << transfer->size
                                    << "] [transfer->name = " << transfer->localfilename << "]";
                                client->httpio->updateuploadspeed(naturalDiff);

                                progressreported = transfer->progresscompleted;
                                lastdata = Waiter::ds;

                                progress();
                            }

                            return transfer->complete(committer);
                        }

                        if (reqs[i]->in.size() != 1 || reqs[i]->in[0] != '0')
                        {
                            LOG_debug << "Conn " << i << " : Error uploading chunk: " << reqs[i]->in;
                            error e = (error)atoi(reqs[i]->in.c_str());

                            DEBUG_TEST_HOOK_UPLOADCHUNK_FAILED(e);

                            if (e == API_EKEY)
                            {
                                client->sendevent(99429, "Integrity check failed in upload", 0);

                                lasterror = e;
                                errorcount++;
                                reqs[i]->status = REQ_PREPARED;
                                break;
                            }

                            if (e == DAEMON_EFAILED || (reqs[i]->contenttype.find("text/html") != string::npos
                                    && !memcmp(reqs[i]->posturl.c_str(), "http:", 5)))
                            {
                                client->usehttps = true;
                                client->app->notify_change_to_https();

                                if (e == DAEMON_EFAILED)
                                {
                                    // megad returning -4 should result in restarting the transfer
                                    LOG_warn << "Conn " << i << " : Upload piece failed with -4, the upload cannot be continued on that server";
                                    string event = "Unexpected upload chunk confirmation length: " + std::to_string(reqs[i]->in.size());
                                    client->sendevent(99441, event.c_str(), 0);  // old-style -4 (from requests with c= instead of d=) were/are reported as 99440
                                }
                                else
                                {
                                    LOG_warn << "Conn " << i << " : Invalid Content-Type detected during upload: " << reqs[i]->contenttype;
                                }
                                client->sendevent(99436, "Automatic change to HTTPS", 0);

                                return transfer->failed(API_EAGAIN, committer);
                            }

                            // fail with returned error
                            return transfer->failed(e, committer);
                        }

                        transfer->chunkmacs.finishedUploadChunks(static_cast<HttpReqUL*>(reqs[i].get())->mChunkmacs);
                        transfer->progresscompleted += reqs[i]->size;
                        LOG_debug << "Conn " << i << " : Progress completed: " << transfer->progresscompleted << "/" << transfer->size;

                        updatecontiguousprogress();

                        if (transfer->progresscompleted == transfer->size)
                        {
                            LOG_err << "Conn " << i << " : Upload is completed, but no upload token was received";
                            client->sendevent(99409, "No upload token received", 0);

                            return transfer->failed(API_EINTERNAL, committer);
                        }

                        errorcount = 0;
                        transfer->failcount = 0;
                        client->transfercacheadd(transfer, &committer);
                        reqs[i]->status = REQ_READY;

                        DEBUG_TEST_HOOK_UPLOADCHUNK_SUCCEEDED(transfer, committer);  // this will return if the hook returns false

                        // If this upload chunk is the earliest (lowest pos), then release the ones that were waiting
                        // This scheme prevents us from too big a gap between earlierst and latest (which could cause a -4 reply)
                        bool earliestInFlight = true;
                        for (int j = connections; j--; )
                        {
                            if (j != i && reqs[j] &&
                               (reqs[j]->status == REQ_INFLIGHT || reqs[j]->status == REQ_SUCCESS) &&
                               (reqs[j]->pos < reqs[i]->pos))
                            {
                                earliestInFlight = false;
                            }
                        }
                        if (earliestInFlight)
                        {
                            LOG_debug << "Connection " << i << " was the earliest upload piece and has now completed at " << reqs[i]->pos;
                            earliestUploadCompleted = true;
                        }

                    }
                    else   // GET
                    {
                        HttpReqDL *downloadRequest = static_cast<HttpReqDL*>(reqs[i].get());
                        if (reqs[i]->size == reqs[i]->bufpos || downloadRequest->buffer_released)   // downloadRequest->buffer_released being true indicates we're retrying this asyncIO
                        {
                            if (!downloadRequest->buffer_released)
                            {
                                LOG_debug << "Conn " << i << " : Submit buffer";
                                transferbuf.submitBuffer(i, new TransferBufferManager::FilePiece(downloadRequest->dlpos, downloadRequest->release_buf())); // resets size & bufpos.  finalize() is taken care of in the transferbuf
                                downloadRequest->buffer_released = true;
                            }

                            auto outputPiece = transferbuf.getAsyncOutputBufferPointer(i);
                            if (outputPiece)
                            {
                                p += outputPiece->buf.datalen(); // p (and progressreported) needs to be updated with this value. If raid, it will also be increased with the data waiting to be recombined
                                mRaidChannelSwapsForSlowness = 0;
                                bool parallelNeeded = outputPiece->finalize(false, transfer->size, transfer->ctriv, transfer->transfercipher(), &transfer->chunkmacs);

                                if (parallelNeeded)
                                {
                                    // do full chunk (and chunk-remainder) decryption on a thread for throughput and to minimize mutex lock times.
                                    auto req = reqs[i];   // shared_ptr for shutdown safety
                                    auto transferkey = transfer->transferkey;
                                    auto ctriv = transfer->ctriv;
                                    auto filesize = transfer->size;
                                    req->status = REQ_DECRYPTING;

                                    client->mAsyncQueue.push([req, i, outputPiece, transferkey, ctriv, filesize](SymmCipher& sc)
                                    {
                                        sc.setkey(transferkey.data());
                                        outputPiece->finalize(true, filesize, ctriv, &sc, nullptr);
                                        LOG_debug << "Conn " << i << " : REQ_DECRYPTED [parallel]";
                                        req->status = REQ_DECRYPTED;
                                    }, false);  // not discardable:  if we downloaded the data, don't waste it - decrypt and write as much as we can to file
                                }
                                else
                                {
                                    reqs[i]->status = REQ_DECRYPTED;
                                    LOG_debug << "Conn " << i << " : REQ_DECRYPTED";
                                }
                            }
                            else if (transferbuf.isRaid())
                            {
                                reqs[i]->status = REQ_READY;  // this connection has retrieved a part of the file, but we don't have enough to combine yet for full file output.   This connection can start fetching the next piece of that part.
                            }
                            else
                            {
                                LOG_err << "Conn " << i << " : non-raid, if the request succeeded then we must have a piece to write to file, but there is no piece!!";
                                assert(false);  // non-raid, if the request succeeded then we must have a piece to write to file.
                            }
                        }
                        else
                        {
                            if (reqs[i]->contenttype.find("text/html") != string::npos
                                    && !memcmp(reqs[i]->posturl.c_str(), "http:", 5))
                            {
                                LOG_warn << "Conn " << i << " : Invalid Content-Type detected during download: " << reqs[i]->contenttype;
                                client->usehttps = true;
                                client->app->notify_change_to_https();

                                client->sendevent(99436, "Automatic change to HTTPS", 0);

                                return transfer->failed(API_EAGAIN, committer);
                            }

                            client->sendevent(99430, "Invalid chunk size", 0);

                            LOG_warn << "Conn " << i << " : Invalid chunk size: " << reqs[i]->size << " - " << reqs[i]->bufpos;
                            lasterror = API_EREAD;
                            errorcount++;
                            reqs[i]->status = REQ_PREPARED;
                            break;
                        }
                    }
                    break;
                }
                case REQ_UPLOAD_PREPARED_BUT_WAIT:
                {
                    assert(transfer->type == PUT);
                    break;
                }
                case REQ_DECRYPTING:
                {
                    assert(transfer->type == GET);
                    // this must return the same piece we are decrypting, since we have not asked the transferbuf to discard it yet.
                    auto outputPiece = transferbuf.getAsyncOutputBufferPointer(i);
                    assert(outputPiece && outputPiece->buf.datalen());
                    p += outputPiece->buf.datalen(); // This is part of the transferred bytes
                    break;
                }
                case REQ_DECRYPTED:
                {
                        assert(transfer->type == GET);

                        // this must return the same piece we just decrypted, since we have not asked the transferbuf to discard it yet.
                        auto outputPiece = transferbuf.getAsyncOutputBufferPointer(i);

                        if (fa->asyncavailable())
                        {
                            if (asyncIO[i])
                            {
                                LOG_warn << "Conn " << i << " : Retrying failed async write";
                                delete asyncIO[i];
                                asyncIO[i] = NULL;
                            }

                            p += outputPiece->buf.datalen();

                            LOG_debug << "Conn " << i << " : Writing data asynchronously at " << outputPiece->pos << " to " << (outputPiece->pos + outputPiece->buf.datalen()) << " (size: " << outputPiece->buf.datalen() << ")";
                            asyncIO[i] = fa->asyncfwrite(outputPiece->buf.datastart(), static_cast<unsigned>(outputPiece->buf.datalen()), outputPiece->pos);
                            reqs[i]->status = REQ_ASYNCIO;
                        }
                        else
                        {
                            if (fa->fwrite(outputPiece->buf.datastart(), static_cast<unsigned>(outputPiece->buf.datalen()), outputPiece->pos))
                            {
                                LOG_verbose << "Conn " << i << " : Sync write succeeded (size: " << outputPiece->buf.datalen() << ")";
                                transferbuf.bufferWriteCompleted(i, true);
                                errorcount = 0;
                                transfer->failcount = 0;
                                updatecontiguousprogress();
                            }
                            else
                            {
                                LOG_err << "Conn " << i << " : Error saving finished chunk (size: " << outputPiece->buf.datalen() << ")";
                                if (!fa->retry)
                                {
                                    transferbuf.bufferWriteCompleted(i, false);  // discard failed data so we don't retry on slot deletion
                                    return transfer->failed(API_EWRITE, committer);
                                }
                                lasterror = API_EWRITE;
                                backoff = 2;
                                break;
                            }

                            if (checkDownloadTransferFinished(committer, client))
                            {
                                return;
                            }

                            client->transfercacheadd(transfer, &committer);
                            reqs[i]->status = REQ_READY;
                        }
                    }
                    break;

                case REQ_ASYNCIO:
                    if (asyncIO[i]->finished)
                    {
                        LOG_verbose << "Conn " << i << " : Processing finished async fs operation";
                        if (!asyncIO[i]->failed)
                        {
                            if (transfer->type == PUT)
                            {
                                LOG_verbose << "Conn " << i << " : Async read succeeded (size: " << asyncIO[i]->dataBufferLen << ")";
                                m_off_t npos = asyncIO[i]->posOfBuffer + asyncIO[i]->dataBufferLen;
                                string finaltempurl = transferbuf.tempURL(i);
                                if (client->usealtupport && !memcmp(finaltempurl.c_str(), "http:", 5))
                                {
                                    size_t index = finaltempurl.find("/", 8);
                                    if(index != string::npos && finaltempurl.find(":", 8) == string::npos)
                                    {
                                        finaltempurl.insert(index, ":8080");
                                    }
                                }

                                auto pos = asyncIO[i]->posOfBuffer;
                                auto req = reqs[i];    // shared_ptr so no object is deleted out from under the worker
                                auto transferkey = transfer->transferkey;
                                auto ctriv = transfer->ctriv;
                                req->pos = pos;
                                req->status = REQ_ENCRYPTING;

                                client->mAsyncQueue.push([req, transferkey, ctriv, finaltempurl, pos, npos](SymmCipher& sc)
                                    {
                                        sc.setkey(transferkey.data());
                                        req->prepare(finaltempurl.c_str(), &sc, ctriv, pos, npos);
                                        req->status = REQ_PREPARED;
                                    }, true);   // discardable - if the transfer or client are being destroyed, we won't be sending that data.
                            }
                            else
                            {
                                LOG_verbose << "Conn " << i << " : Async write succeeded (size: " << asyncIO[i]->dataBufferLen << ")";
                                transferbuf.bufferWriteCompleted(i, true);
                                errorcount = 0;
                                transfer->failcount = 0;

                                updatecontiguousprogress();

                                if (checkDownloadTransferFinished(committer, client))
                                {
                                    return;
                                }

                                client->transfercacheadd(transfer, &committer);
                                reqs[i]->status = REQ_READY;

                                if (client->orderdownloadedchunks && !transferbuf.isRaid())
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
                            LOG_warn << "Conn " << i << " : Async operation failed  (size: " << asyncIO[i]->dataBufferLen << "). Retry: " << asyncIO[i]->retry;
                            if (!asyncIO[i]->retry)
                            {
                                transferbuf.bufferWriteCompleted(i, false);  // discard failed data so we don't retry on slot deletion
                                delete asyncIO[i];
                                asyncIO[i] = NULL;
                                return transfer->failed(transfer->type == PUT ? API_EREAD : API_EWRITE, committer);
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
                        p += asyncIO[i]->dataBufferLen;
                    }
                    break;

                case REQ_FAILURE:
                    {
                        auto failValue = processRequestFailure(client, reqs[i], backoff, i);
                        if (failValue.first != API_OK)
                        {
                            LOG_warn << "Conn " << i << " : Request Failed with code " << failValue.first << ". Transfer failed (backoff: " << failValue.second << ")";
                            // Error: transfer fail
                            return transfer->failed(failValue.first, committer, failValue.second);
                        }
                    }
                    break;

                default:
                    ;
            }
        }

        if (!failure)
        {
            if (!reqs[i] || (reqs[i]->status == REQ_READY))
            {
                bool newInputBufferSupplied = false;
                bool pauseConnectionInputForRaid = false;
                std::pair<m_off_t, m_off_t> posrange = transferbuf.nextNPosForConnection(i, maxRequestSize, connections, newInputBufferSupplied, pauseConnectionInputForRaid, client->httpio->uploadSpeed);

                // we might have a raid-reassembled block to write, or a previously loaded block, or a skip block to process.
                bool newOutputBufferSupplied = false;
                auto outputPiece = transferbuf.getAsyncOutputBufferPointer(i);
                if (outputPiece && reqs[i])
                {
                    LOG_verbose << "Conn " << i << " : outputPiece && reqs[ " << i << "] -> REQ_SUCCESS (set up to do the actual write on the next loop, as if it was a retry)";
                    // set up to do the actual write on the next loop, as if it was a retry
                    reqs[i]->status = REQ_SUCCESS;
                    static_cast<HttpReqDL*>(reqs[i].get())->buffer_released = true;
                    newOutputBufferSupplied = true;
                }

                if (newOutputBufferSupplied || newInputBufferSupplied || pauseConnectionInputForRaid)
                {
                    LOG_verbose << "Conn " << i << " : process supplied block, or just wait until other connections catch up a bit";
                    // process supplied block, or just wait until other connections catch up a bit
                }
                else if (posrange.second > posrange.first || !transfer->size || (transfer->type == PUT && asyncIO[i]))
                {
                    LOG_verbose << "Conn " << i << " : download/upload specified range";
                    // download/upload specified range

                    if (!reqs[i])
                    {
                        reqs[i].reset(transfer->type == PUT ? (HttpReqXfer*)new HttpReqUL() : (HttpReqXfer*)new HttpReqDL());
                        reqs[i]->logname = client->clientname + (transfer->type == PUT ? "U" : "D") + std::to_string(++client->transferHttpCounter) + " ";
                    }

                    bool prepare = true;
                    if (transfer->type == PUT)
                    {
                        m_off_t pos = posrange.first;
                        unsigned size = (unsigned)(posrange.second - pos);

                        // No need to keep recopying already processed macs from prior uploads on this req[i]
                        // For uploads, these are always on chunk boundaries so no need to worry about partials.
                        static_cast<HttpReqUL*>(reqs[i].get())->mChunkmacs.clear();

                        if (fa->asyncavailable())
                        {
                            if (asyncIO[i])
                            {
                                LOG_warn << "Conn " << i << " : Retrying a failed read";
                                pos = asyncIO[i]->posOfBuffer;
                                size = asyncIO[i]->dataBufferLen;
                                posrange.second = pos + size;
                                delete asyncIO[i];
                                asyncIO[i] = NULL;
                            }

                            asyncIO[i] = fa->asyncfread(reqs[i]->out, size, (-(int)size) & (SymmCipher::BLOCKSIZE - 1), pos, FSLogging::logOnError);
                            reqs[i]->status = REQ_ASYNCIO;
                            prepare = false;
                        }
                        else
                        {
                            if (!fa->fread(reqs[i]->out, size, (-(int)size) & (SymmCipher::BLOCKSIZE - 1), transfer->pos, FSLogging::logOnError))
                            {
                                LOG_warn << "Conn " << i << " : Error preparing transfer: " << fa->retry;
                                if (!fa->retry)
                                {
                                    return transfer->failed(API_EREAD, committer);
                                }

                                // retry the read shortly
                                backoff = 2;
                                posrange.second = transfer->pos;
                                prepare = false;
                            }
                        }
                    }

                    if (prepare)
                    {
                        prepareRequest(reqs[i], transferbuf.isNewRaid() ? std::string() : transferbuf.tempURL(i), posrange.first, posrange.second);
                    }

                    LOG_verbose << "Conn " << i << " : Request prepared. Pos: " << posrange.first << " to npos: " << posrange.second << ". Size: " << (posrange.second - posrange.first)
                                << ". Current " << (transferbuf.isRaid() ? "raid part" : "transfer") << " pos: " << transferbuf.transferPos(i) << ". New " << (transferbuf.isRaid() ? "raid part" : "transfer") << " pos: " << (std::max<m_off_t>(transferbuf.transferPos(i), posrange.second))
                                << (transferbuf.isRaid() ? string(". Part size: " + std::to_string(transferbuf.raidPartSize(i, transfer->size))) : "")
                                << ". Transfer size: " << transfer->size;
                    transferbuf.transferPos(i) = std::max<m_off_t>(transferbuf.transferPos(i), posrange.second);
                }
                else if (reqs[i])
                {
                    LOG_verbose << "Conn " << i << " : REQUEST DONE -> REQ_DONE";
                    reqs[i]->status = REQ_DONE;

                    if (transfer->type == GET)
                    {
                        // raid reassembly can have several chunks to complete at the end of the file - keep processing till they are all done
                        auto outputPiece = transferbuf.getAsyncOutputBufferPointer(i);
                        if (outputPiece)
                        {
                            LOG_verbose << "Conn " << i << " : raid reassembly still has chunks to complete -> change REQ_DONE to REQ_SUCCESS (set up to do the actual write on the next loop, as if it was a retry)";
                            // set up to do the actual write on the next loop, as if it was a retry
                            reqs[i]->status = REQ_SUCCESS;
                            static_cast<HttpReqDL*>(reqs[i].get())->buffer_released = true;
                        }
                    }
                }
            }
        }
    }

    if (transfer->type == PUT)
    {
        // Get the number of reqs in flight and the position of the earliest for...
        int numInflight = 0;
        m_off_t earliestPosInFlight = 0;
        for (int i = connections; i--; )
        {
            if (reqs[i] && reqs[i]->status == REQ_INFLIGHT)
            {
                if (!numInflight || earliestPosInFlight > reqs[i]->pos)
                {
                    earliestPosInFlight = reqs[i]->pos;
                }
                ++numInflight;
            }
        }
        // ...avoid a gap greater than 256MB between start-pos of the earliest and the end-pos of the latest request
        // (the request should wait, so the gap doesn't grow over that limit)
        for (int i = connections; i--; )
        {
            if (reqs[i])
            {
                if (reqs[i]->status == REQ_PREPARED &&
                    (numInflight && !earliestUploadCompleted &&
                    earliestPosInFlight + MAX_GAP_SIZE < (reqs[i]->pos + reqs[i]->size)))
                {
                    LOG_debug << "Connection " << i << " delaying until earliest completes. pos=" << reqs[i]->pos;
                    reqs[i]->status = REQ_UPLOAD_PREPARED_BUT_WAIT;
                }
                else if (reqs[i]->status == REQ_UPLOAD_PREPARED_BUT_WAIT &&
                    (!numInflight || earliestUploadCompleted))
                {
                    LOG_debug << "Connection " << i << " resumes. pos=" << reqs[i]->pos;
                    reqs[i]->status = REQ_PREPARED;
                }
            }
        }
    }

    // Finally see if any requests are now fit to post
    for (int i = connections; i--; )
    {
        if (reqs[i] && !failure)
        {
            if (!backoff)
            {
                if (reqs[i]->status == REQ_PREPARED)
                {
                    if (transferbuf.isNewRaid())
                    {
                        assert(cloudRaid != nullptr);
                        LOG_verbose << "Conn " << i << " : balancedRequest Size: " << reqs[i]->size << ". Pos: " << reqs[i]->pos << ". Progressreported = " << progressreported << ", progresscompleted = " << transfer->progresscompleted << " [req = " << (void*)reqs[i].get() << "]";
                        if (!cloudRaid->balancedRequest(i, transferbuf.tempUrlVector(), reqs[i]->size, reqs[i]->pos, reqs[i]->size))
                        {
                            reqs[i]->status = REQ_FAILURE;
                        }
                    }
                    else // if (!transferbuf.isNewRaid())
                    {
                        processRequestPost(client, reqs[i]);
                    }

                    mReqSpeeds[i].requestStarted();
                    reqs[i]->minspeed = true;
                }
                if (transferbuf.isNewRaid())
                {
                    assert(cloudRaid != nullptr);
                    if (reqs[i]->status == REQ_PREPARED || reqs[i]->status == REQ_INFLIGHT)
                    {
                        m_off_t raidReqProgress = 0;
                        auto failValues = processRaidReq(i, raidReqProgress);
                        if (failValues.first == API_OK)
                        {
                            if (raidReqProgress > 0)
                                p += raidReqProgress;
                        }
                        else
                        {
                            LOG_debug << "[TransferSlot::doio] Conn " << i << " : Transfer failure after processing RaidReq. Error: " << failValues.first << ". Backoff: " << failValues.second;
                            return transfer->failed(failValues.first, committer, failValues.second);
                        }
                    }
                }
            }
        }
    }

    m_off_t cloudRaidProgress = 0;
    if (transfer->type == GET)
    {
        // for Raid, additionally we need the raid data that's waiting to be recombined
        if (transferbuf.isRaid())
        {
            cloudRaidProgress = transferbuf.progress();
        }
        else if (transferbuf.isNewRaid())
        {
            assert(cloudRaid != nullptr);
            cloudRaidProgress = cloudRaid->progress();
        }
        p += cloudRaidProgress;
    }
    p += transfer->progresscompleted;

    if (p != progressreported || (Waiter::ds - lastprogressreport) > PROGRESSTIMEOUT)
    {
        if (p != progressreported)
        {
            m_off_t diff = p - progressreported;
            m_off_t naturalDiff = std::max<m_off_t>(diff, 0);
            speed = mTransferSpeed.calculateSpeed(naturalDiff);
            meanSpeed = mTransferSpeed.getMeanSpeed();
            if ((Waiter::ds % 50 == 0) || (diff < 0) || (p > transfer->size)) // every 5s
            {
                if (transferbuf.isRaid() || transferbuf.isNewRaid())
                {
                    LOG_verbose << "[TransferSlot::doio] [CloudRaid] Speed: " << (speed / 1024) << " KB/s. Mean speed: " << (meanSpeed / 1024) << " KB/s [diff = " << diff << "]" << " [cloudRaidProgress = " << cloudRaidProgress << ", new progressreported = " << p << ", last progressreported = " << progressreported << ", transfer->progresscompleted = " << transfer->progresscompleted << "] [transfer->size = " << transfer->size << "] [transfer->name = " << transfer->localfilename << "]";
                }
                else
                {
                    LOG_verbose << "[TransferSlot::doio] " << ((transfer->type == PUT) ? "[Upload]" : "[Non CloudRaid]")
                                << " Speed: " << (speed / 1024) << " KB/s. Mean speed: " << (meanSpeed / 1024) << " KB/s [diff = " << diff << "]" << " [new progressreported = " << p << ", last progressreported = " << progressreported << ", transfer->progresscompleted = " << transfer->progresscompleted << "] [transfer->size = " << transfer->size << "] [transfer->name = " << transfer->localfilename << "]";
                }
                assert(p <= transfer->size);
            }
            if (transfer->type == PUT)
            {
                client->httpio->updateuploadspeed(naturalDiff);
            }
            else
            {
                client->httpio->updatedownloadspeed(naturalDiff);
            }

            progressreported = p;
            lastdata = Waiter::ds;
        }
        lastprogressreport = Waiter::ds;

        progress();
    }

    assert(lastdata != NEVER);
    if (Waiter::ds - lastdata >= XFERTIMEOUT && !failure)
    {
        LOG_warn << "Failed chunk(s) due to a timeout: no data moved for " << (XFERTIMEOUT/10) << " seconds" ;
        failure = true;
        bool changeport = false;

        if (transfer->type == GET && client->autodownport && !memcmp(transferbuf.tempURL(0).c_str(), "http:", 5))
        {
            LOG_debug << "Automatically changing download port due to a timeout";
            client->usealtdownport = !client->usealtdownport;
            changeport = true;
        }
        else if (transfer->type == PUT && client->autoupport && !memcmp(transferbuf.tempURL(0).c_str(), "http:", 5))
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
                    toggleport(reqs[i].get());
                }

                reqs[i]->status = REQ_PREPARED;
            }
        }

        if (!chunkfailed)
        {
            LOG_warn << "Transfer failed due to a timeout";
            return transfer->failed(API_EAGAIN, committer);  // either the (this) slot has been deleted, or the whole transfer including slot has been deleted
        }
        else
        {
            LOG_warn << "Chunk failed due to a timeout";
            client->app->transfer_failed(transfer, API_EFAILED);
            ++client->performanceStats.transferTempErrors;
        }
    }

    if (!failure && backoff > 0)
    {
        retrybt.backoff(backoff);
        retrying = true;  // we don't bother checking the `retrybt` before calling `doio` unless `retrying` is set.
    }
}


bool TransferSlot::tryRaidRecoveryFromHttpGetError(unsigned connectionNum, bool incrementErrors)
{
    // If we are downloding a cloudraid file then we may be able to ignore one connection and download from the other 5.
    if (transferbuf.isRaid())
    {
        if (transferbuf.tryRaidHttpGetErrorRecovery(connectionNum, incrementErrors))
        {
            // transferbuf is now set up to try a new connection
            reqs[connectionNum]->status = REQ_READY;

            // if the file is nearly complete then some connections might have stopped, but need restarting as they could have skipped portions
            for (unsigned j = connections; j--; )
            {
                if (reqs[j] && reqs[j]->status == REQ_DONE)
                {
                    reqs[j]->status = REQ_READY;
                }
            }
            return true;
        }
        LOG_warn << "Cloudraid transfer failed, too many connection errors";
    }
    return false;
}


// transfer progress notification to app and related files
void TransferSlot::progress()
{
    transfer->client->app->transfer_update(transfer);

    for (file_list::iterator it = transfer->files.begin(); it != transfer->files.end(); ++it)
    {
        (*it)->progress();
    }
}


m_off_t TransferSlot::updatecontiguousprogress()
{
    m_off_t contiguousProgress = transfer->chunkmacs.updateContiguousProgress(transfer->size);

    // Since that is updated, we may have a chance to consolidate the macsmac calculation so far also
    transfer->chunkmacs.updateMacsmacProgress(transfer->transfercipher());

    if (!transferbuf.tempUrlVector().empty() && transferbuf.isRaid())
    {
        LOG_debug << "Contiguous progress: " << contiguousProgress;
    }
    else
    {
        LOG_debug << "Contiguous progress: " << contiguousProgress << " (" << (transfer->pos - contiguousProgress) << ")";
    }

    return contiguousProgress;
}


void TransferSlot::prepareRequest(const std::shared_ptr<HttpReqXfer>& httpReq, const string& tempURL, m_off_t pos, m_off_t npos)
{
    string finaltempURL = tempURL;
    if (!finaltempURL.empty() &&
        ((transfer->type == GET && transfer->client->usealtdownport) ||
        (transfer->type == PUT && transfer->client->usealtupport)) &&
            !memcmp(finaltempURL.c_str(), "http:", 5))
    {
        size_t index = finaltempURL.find("/", 8);
        if (index != string::npos && finaltempURL.find(":", 8) == string::npos)
        {
            finaltempURL.insert(index, ":8080");
        }
    }

    LOG_debug << "[TransferSlot::prepareRequest] pos = " << pos << ", npos = " << npos << ", tempURL = '" << finaltempURL << "'";
    httpReq->prepare(finaltempURL.c_str(),
                     transfer->transfercipher(),
                     transfer->ctriv,
                     pos, npos);
    httpReq->pos = pos;
    httpReq->status = REQ_PREPARED;
}

void TransferSlot::processRequestPost(MegaClient* client,
                                      const std::shared_ptr<HttpReqXfer>& httpReq)
{
    ++tsStats.mNumTotalRequests;
    httpReq->post(client); // status becomes either REQ_INFLIGHT or REQ_FAILED
}

std::pair<error, dstime> TransferSlot::processRequestFailure(MegaClient* client, const std::shared_ptr<HttpReqXfer>& httpReq, dstime& backoff, int channel)
{
    processRequestLatency(httpReq);
    ++tsStats.mNumFailedRequests;
    LOG_warn << "Conn " << channel << " : Failed chunk. HTTP status: " << httpReq->httpstatus
             << " [httpReq = " << (void*)httpReq.get()
             << "] [totalFailedRequests = " << tsStats.mNumFailedRequests << "]";

    if (httpReq->httpstatus && httpReq->contenttype.find("text/html") != string::npos && !memcmp(httpReq->posturl.c_str(), "http:", 5))
    {
        LOG_warn << "Conn " << channel << " : Invalid Content-Type detected on failed chunk: " << httpReq->contenttype << " [httpReq = " << (void*)httpReq.get() << "]";
        client->usehttps = true;
        client->app->notify_change_to_https();

        client->sendevent(99436, "Automatic change to HTTPS", 0);

        return std::make_pair(API_EAGAIN, 0);
    }

    if (httpReq->httpstatus == 509)
    {
        LOG_warn << "Conn " << channel << " : Bandwidth overquota from storage server" << " [httpReq = " << (void*)httpReq.get() << "]";

        dstime new_backoff = client->overTransferQuotaBackoff(httpReq.get());

        return std::make_pair(API_EOVERQUOTA, new_backoff);
    }
    else if (httpReq->httpstatus == 429)
    {
        LOG_warn << "Conn " << channel << " : 429 - too many requests - backoff 5, REQ_PREPARED" << " [httpReq = " << (void*)httpReq.get() << "]";
        // too many requests - back off a bit (may be added serverside at some point.  Added here 202020623)
        backoff = 5;
        httpReq->status = REQ_PREPARED;
    }
    else if (httpReq->httpstatus == 503 && !transferbuf.isRaid() && !transferbuf.isNewRaid())
    {
        LOG_warn << "Conn " << channel << " : 503 dor non raid... backoff and REQ_PREPARED" << " [httpReq = " << (void*)httpReq.get() << "]";
        // for non-raid, if a file gets a 503 then back off as it may become available shortly
        backoff = 50;
        httpReq->status = REQ_PREPARED;
    }
    else if (httpReq->httpstatus == 403 || httpReq->httpstatus == 404 || (httpReq->httpstatus == 503 && (transferbuf.isRaid() || transferbuf.isNewRaid())))
    {
        LOG_warn << "Conn " << channel << " : 403, 404 or 503... (if is new raid -> REQ_READY)" << " [httpReq = " << (void*)httpReq.get() << "]";
        if (transferbuf.isNewRaid())
        {
            httpReq->status = REQ_READY;
        }
        // - 404 means "malformed or expired URL" - can be immediately fixed by getting a fresh one from the API
        // - 503 means "the API gave you good information, but I don't have the file" - cannot be fixed (at least not immediately) by getting a fresh URL
        // for raid parts and 503, it's appropriate to try another raid source
        else if (!tryRaidRecoveryFromHttpGetError(channel, true))
        {
            return std::make_pair(API_EAGAIN, 0);
        }
    }
    else if (httpReq->httpstatus == 0 && (transferbuf.isNewRaid() || tryRaidRecoveryFromHttpGetError(channel, true)))
    {
        LOG_warn << "Conn " << channel << " : status 0 NETWORK ERROR OR TIMEOUT -> (if is new raid -> REQ_READY)" << " [httpReq = " << (void*)httpReq.get() << "]";
        if (transferbuf.isNewRaid())
        {
            httpReq->status = REQ_READY;
        }
        // status 0 indicates network error or timeout; no headers received.
        // tryRaidRecoveryFromHttpGetError has switched to loading a different part instead of this one.
    }
    else
    {
        if (!failure)
        {
            LOG_verbose << "Conn " << channel << " : else if failure" << " [httpReq = " << (void*)httpReq.get() << "]";
            failure = true;
            bool changeport = false;

            if (transfer->type == GET && client->autodownport && !memcmp(httpReq->posturl.c_str(), "http:", 5))
            {
                LOG_debug << "Conn " << channel << " : Automatically changing download port";
                client->usealtdownport = !client->usealtdownport;
                changeport = true;
            }
            else if (transfer->type == PUT && client->autoupport && !memcmp(httpReq->posturl.c_str(), "http:", 5))
            {
                LOG_debug << "Conn " << channel << " : Automatically changing upload port";
                client->usealtupport = !client->usealtupport;
                changeport = true;
            }

            client->app->transfer_failed(transfer, API_EFAILED);
            client->setchunkfailed(&httpReq->posturl);
            ++client->performanceStats.transferTempErrors;

            if (changeport)
            {
                toggleport(httpReq.get());
            }
        }
        LOG_verbose << "Conn " << channel << " : from req_failure to REQ_PREPARED" << " [httpReq = " << (void*)httpReq.get() << "]";
        httpReq->status = REQ_PREPARED;
    }
    return std::make_pair(API_OK, 0);
}


bool TransferSlot::initCloudRaid(MegaClient* client)
{
    assert(transferbuf.isNewRaid());
    if (!transferbuf.isNewRaid())
    {
        return false;
    }

    if (cloudRaid == nullptr)
    {
        cloudRaid = std::make_shared<CloudRaid>(this, client, static_cast<int>(reqs.size()));
        return cloudRaid->isShown();
    }
    return cloudRaid->init(this, client, static_cast<int>(reqs.size()));
}


std::pair<error, dstime> TransferSlot::processRaidReq(size_t connection, m_off_t& raidReqProgress)
{
    assert(transfer->type == GET);
    assert(cloudRaid && cloudRaid->isShown());
    assert(!reqs.empty() && connection <= reqs.size());
    const std::shared_ptr<HttpReqXfer>& httpReq = reqs[connection];
    assert(httpReq != nullptr);

    // Process internal RaidReq IO
    cloudRaid->raidReqDoio(static_cast<int>(connection));
    auto failValues = cloudRaid->checkTransferFailure();
    if (failValues.first != API_OK)
    {
        return failValues;
    }

    if (httpReq->status == REQ_PREPARED)
    {
        httpReq->bufpos = 0;
        httpReq->status = REQ_INFLIGHT;
    }
    byte* buf = httpReq->buf + httpReq->bufpos;
    m_off_t len = httpReq->size - httpReq->bufpos;
    assert((len > 0) && "len is 0 in processRaidReq");

    // Get raid-assembled data
    raidReqProgress = static_cast<m_off_t>(cloudRaid->readData(static_cast<int>(connection), buf, len));
    if (raidReqProgress > 0)
    {
        httpReq->bufpos += raidReqProgress;
        if (httpReq->bufpos == httpReq->size)
        {
            httpReq->status = REQ_SUCCESS;
        }
    }
    else if (raidReqProgress < 0)
    {
        LOG_debug << "[TransferSlot::processRaidReq] Conn " << connection << " : RaidReq FAILURE. [httpReq = " << (void*)httpReq.get() << "]";
        httpReq->status = REQ_FAILURE;
    }
    httpReq->lastdata = Waiter::ds;

    if (httpReq->status == REQ_SUCCESS)
    {
        LOG_verbose << "[TransferSlot::processRaidReq] Conn " << connection << " : RaidReq SUCCESS. Progress: " << raidReqProgress << ". Removing RaidReq... [httpReq = " << (void*)httpReq.get() << "]";
        cloudRaid->removeRaidReq(static_cast<int>(connection));
    }
    return cloudRaid->checkTransferFailure();
}

void TransferSlot::processRequestLatency(const std::shared_ptr<HttpReqXfer>& req)
{
    if (!req->isLatencyProcessed)
    {
        if (req->mConnectTime > -1 && req->mStartTransferTime > -1)
        {
            if (req->mConnectTime > 0)
            {
                // mConnectTime, populated from CURLINFO_CONNECT_TIME, can be 0
                // for reused connections. We want to calculate the network performance,
                // so we exclude reused connections.
                // For coherence, we only include CURLINFO_STARTTRANSFER_TIME when
                // we have included CURLINFO_CONNECT_TIME, because we only have one
                // variable for "numRequestsWithCalculatedLatency" to calculate both averages.
                tsStats.mTotalConnectTime += req->mConnectTime;
                tsStats.mTotalStartTransferTime += req->mStartTransferTime;
                ++tsStats.mNumRequestsWithCalculatedLatency;
            }
            req->isLatencyProcessed = true;
        }
    }
}

} // namespace
