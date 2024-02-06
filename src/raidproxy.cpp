#include <algorithm>
#include <map>
#include <bitset>
#include <cstdint>
#include <climits>

#include "mega/raidproxy.h"
#include "mega.h"

using namespace ::mega::RaidProxy;

#define MAX_DELAY_IN_SECONDS 30
#define OWNREADAHEAD 64
#define ISOWNREADAHEAD(X) ((X) & OWNREADAHEAD)
#define VALIDPARTS(X) ((X) & (OWNREADAHEAD-1))
#define SECTORFLOOR(X) ((X) & -RAIDSECTOR)


/* -------------- PartFetcher --------------*/

PartFetcher::PartFetcher()
    : lastdata(Waiter::ds)
{
}

PartFetcher::~PartFetcher()
{
    if (rr)
    {
        closesocket();

        while (!readahead.empty())
        {
            free(readahead.begin()->second.first);
            readahead.erase(readahead.begin());
        }
    }
}

// sets the next read position (pos) and the remaining read length (rem/remfeed),
// taking into account all readahead data and ongoing reads on other connected fetchers.
void PartFetcher::setposrem()
{
    // we want to continue reading at the 2nd lowest position:
    // take the two lowest positions and use the higher one.
    static thread_local map<m_off_t, char> chunks;
    m_off_t basepos = rr->dataline * RAIDSECTOR;
    m_off_t curpos = basepos + rr->partpos[part];

    // determine the next suitable read range to ensure the availability
    // of RAIDPARTS-1 sources based on ongoing reads and stored readahead data.
    for (m_off_t i = RAIDPARTS; i--; )
    {
        // compile boundaries of data chunks that have been or are being fetched
        // (we do not record the beginning, as position 0 is implicitly valid)

        // a) already read data in the PartReq buffer
        chunks[SECTORFLOOR(basepos + rr->partpos[i])]--;

        // b) ongoing fetches on *other* channels
        if (i != part)
        {
            if (rr->fetcher[i].rem)
            {
                chunks[SECTORFLOOR(rr->fetcher[i].pos)]++;
                chunks[SECTORFLOOR(rr->fetcher[i].pos + rr->fetcher[i].rem)]--;
            }
        }

        // c) existing readahead data
        auto it = rr->fetcher[i].readahead.begin();
        auto end = rr->fetcher[i].readahead.end();

        while (it != end)
        {
            m_off_t t = it->first;

            // we mark our own readahead as always valid to prevent double reads
            char delta = 1;
            if (i == part) delta += OWNREADAHEAD;

            chunks[SECTORFLOOR(t)] += delta;

            // (concatenate contiguous readahead chunks to reduce chunks inserts)
            do {
                t += it->second.second;
            }  while (++it != end && t == it->first);

            chunks[SECTORFLOOR(t)] -= delta;
        }
    }

    // find range from the first position after the current read position
    // with less than RAIDPARTS-1 valid sources
    // (where we need to start fetching) to the first position thereafter with
    // RAIDPARTS-1 valid sources, if any (where we would need to stop fetching)
    char valid = RAIDPARTS;
    m_off_t startpos = -1, endpos = -1;

    for (auto it = chunks.begin(); it != chunks.end(); )
    {
        auto next_it = it;
        next_it++;
        valid += it->second;

        assert(valid >= 0 && VALIDPARTS(valid) < RAIDPARTS);

        if (startpos == -1)
        {
            // no startpos yet+ (our own readahead is excluded by valid being bumped by OWNREADAHEAD)
            if (valid < RAIDPARTS - 1)
            {
                if (curpos < it->first)
                {
                    startpos = it->first;
                }
                else if (next_it == chunks.end() || curpos < next_it->first)
                {
                    startpos = curpos;
                }
            }
        }
        else
        {

            // startpos valid, look for suitable endpos
            // (must not cross own readahead data or any already sufficient raidparts)
            if (valid >= (RAIDPARTS - 1))
            {
                endpos = it->first;
                break;
            }
        }

        it = next_it;
    }

    // clear early, hoping that much of chunks is still in the L1/L2 cache
    chunks.clear();

    // there is always a startpos, even though it may be at sourcesize
    assert(startpos != -1);

    // we always resume fetching at a raidsector boundary.
    // (this may result in the unnecessary retransmission of up to 15 bytes
    // in case we resume on the same part.)
    startpos &= -RAIDSECTOR;

    if (endpos == -1)
    {
        // no sufficient number of sources past startpos, we read to the end
        rem = rr->paddedpartsize - startpos;

    }
    else
    {
        assert(endpos >= startpos);
        rem = endpos - startpos;
    }

    pos = startpos;

    setremfeed();
}

bool PartFetcher::setremfeed(m_off_t numBytes)
{
    // request 1 less RAIDSECTOR as we will add up to 1 sector of 0 bytes at the end of the file - this leaves enough buffer space in the buffer pased to procdata for us to write past the reported length
    remfeed = numBytes ? std::min(rem, numBytes) : rem;
    if (sourcesize - pos < remfeed)
    {
        if (sourcesize - pos >= 0)
        {
            remfeed = sourcesize - pos; // we only read to the physical end of the part
        }
        else
        {
            rem = 0;
            remfeed = 0;
        }
    }
    return remfeed != 0;
}

int PartFetcher::onFailure()
{
    auto& httpReq = rr->httpReqs[part];
    assert(httpReq != nullptr);
    if (httpReq->status == REQ_FAILURE)
    {
        raidTime backoff = 0;
        {
            if (rr->cloudRaid->onRequestFailure(httpReq, part, backoff))
            {
                if (httpReq->status == REQ_FAILURE)
                {
                    auto failValues = rr->cloudRaid->checkTransferFailure();
                    if (!failValues.first)
                    {
                        LOG_warn << "[PartFetcher::onFailure] Request failure on part " << (int)part << " with no transfer fail values" << " [this = " << this << "]";
                        assert(false);
                    }
                    rr->setNewUnusedRaidConnection(part);
                    closesocket();
                    return -1;
                }
                if (httpReq->status == REQ_PREPARED)
                {
                    return trigger(backoff);
                }
            }
        }

        LOG_warn << "CloudRAID connection to part " << (int)part << " failed. Http status: " << httpReq->httpstatus << " [this = " << this << "]";
        errors++;

        if (consecutive_errors > MAXRETRIES || httpReq->status == REQ_READY)
        {
            rr->setNewUnusedRaidConnection(part);
            closesocket();
            for (uint8_t i = RAIDPARTS; i--;)
            {
                if (i != part && !rr->fetcher[i].connected)
                {
                    rr->fetcher[i].trigger();
                }
            }
            return -1;
        }
        else
        {
            consecutive_errors++;
            for (uint8_t i = RAIDPARTS; i--;)
            {
                if (i != part && !rr->fetcher[i].connected)
                {
                    rr->fetcher[i].trigger();
                }
            }

            if (httpReq->status == REQ_FAILURE) // shouldn't be
            {
                httpReq->status = REQ_READY;
            }
            return trigger(backoff);
        }
    }
    else
    {
        httpReq->status = REQ_READY;
        resume();
    }
    return -1;
}

m_off_t PartFetcher::getSocketSpeed() const
{
    if (!timeInflight)
    {
        return 0;
    }
    // In Bytes per millisec
    return reqBytesReceived / timeInflight;
}

bool PartFetcher::setsource(const std::string& partUrl, RaidReq* crr, uint8_t cpart)
{
    url = partUrl;
    part = cpart;
    rr = crr;
    partStartPos = rr->reqStartPos / (RAIDPARTS - 1);
    assert(partStartPos % RAIDSECTOR == 0);

    sourcesize = RaidReq::raidPartSize(part, rr->filesize);
    LOG_debug << "[PartFetcher::setsource] part = " << (int)cpart << ", partStartPos = " << partStartPos << ", sourcesize = " << sourcesize << " [this = " << this << "]";
    return true;
}

// (re)create, set up socket and start (optionally delayed) io on it
int PartFetcher::trigger(raidTime delay, bool disconnect)
{
    if (delay == MAX_DELAY_IN_SECONDS)
    {
        rr->cloudRaid->setTransferFailure();
        return -1;
    }
    assert(!url.empty());

    if (finished)
    {
        closesocket();
        return -1;
    }

    if (disconnect)
    {
        if (rr->httpReqs[part]->status == REQ_SUCCESS)
        {
            rem = 0;
            remfeed = 0;
        }
        else closesocket(true);
    }

    if (!rem)
    {
        assert(pos <= rr->paddedpartsize);
        if (pos == rr->paddedpartsize)
        {
            closesocket();
            return -1;
        }
    }

    directTrigger(!delay);

    if (delay) delayuntil = Waiter::ds + delay;

    return delay;
}

bool PartFetcher::directTrigger(bool addDirectio)
{
    auto httpReq = rr->httpReqs[part];

    assert(httpReq != nullptr);
    if (addDirectio && rr->pool.addDirectio(httpReq))
    {
        return true;
    }
    return !addDirectio;
}

// close socket
void PartFetcher::closesocket(bool reuseSocket)
{
    rem = 0;
    remfeed = 0; // need to clear remfeed so that the disconnected channel does not corrupt feedlag

    postCompleted = false;
    if (inbuf) inbuf.reset(nullptr);

    auto& httpReq = rr->httpReqs[part];
    if (httpReq)
    {
        if (connected)
        {
            if (!reuseSocket || httpReq->status == REQ_INFLIGHT)
            {
                rr->cloudRaid->disconnect(httpReq);
            }
            httpReq->status = REQ_READY;
        }
        rr->pool.removeio(httpReq);
    }
    connected = false;
}

// perform I/O on socket (which is assumed to exist)
int PartFetcher::io()
{
    // prevent spurious epoll events from triggering a delayed reconnect early
    if (finished && rr->completed < NUMLINES && (rr->rem > rr->completed*RAIDLINE-rr->skip))
    {
        rr->procreadahead();
    }
    if ((Waiter::ds < delayuntil) || finished) return -1;

    auto httpReq = rr->httpReqs[part];
    assert(httpReq != nullptr);


    if (httpReq->status == REQ_FAILURE)
    {
        return onFailure();
    }
    else if (rr->allconnected(part))
    {
        LOG_warn << "There are 6 connected channels, and there shouldn't be" << " [this = " << this << "]";
        assert(false && "There shouldn't be 6 connected channels");
        // we only need RAIDPARTS-1 connections, so shut down the slowest one
        closesocket(true);
        return -1;
    }
    else if (httpReq->status == REQ_INFLIGHT)
    {
        directTrigger();
        return -1;
    }
    // unless the fetch position/length for the connection has been computed
    // before, we do so *after* the connection so that the order in which
    // the connections are established are the first criterion for slow source heuristics
    else if (!rem && httpReq->status != REQ_SUCCESS)
    {
        setposrem();
        if (httpReq->status == REQ_PREPARED)
        {
            httpReq->status = REQ_READY;
        }
    }

    if (httpReq->status == REQ_READY)
    {
        if (rem <= 0)
        {
            closesocket();
            rr->resumeall(part);
            return -1;
        }

        if (postCompleted && (rr->processFeedLag() == part))
        {
            return -1;
        }

        if (inbuf)
        {
            inbuf.reset(nullptr);
        }

        m_off_t npos = pos + rem;
        assert(npos <= rr->paddedpartsize);
        LOG_verbose << "[PartFetcher::io] [part " << (int)part << "] prepareRequest -> pos = " << (pos + partStartPos) << ", npos = " << (npos + partStartPos) << " [partStartPos = " << partStartPos << ", rem = " << rem << "]" << " [this = " << this << "]";
        rr->cloudRaid->prepareRequest(httpReq, url, pos + partStartPos, npos + partStartPos);
        assert(httpReq->status == REQ_PREPARED);
        connected = true;
    }

    if (connected)
    {
        if (httpReq->status == REQ_PREPARED)
        {
            bool postDone = rr->cloudRaid->post(httpReq);
            if (postDone)
            {
                lastdata = Waiter::ds;
                postStartTime = std::chrono::system_clock::now();
            }
            else
            {
                return onFailure();
            }
        }

        if (httpReq->status == REQ_SUCCESS)
        {
            LOG_verbose << "[PartFetcher::io] [part " << (int)part << "] REQ_SUCCESS [pos = " << pos << ", partStartPos = " << partStartPos << "] [httpReq->dlpos = " << httpReq->dlpos << ", httpReq->size = " << httpReq->size << ", inbuf = " << (inbuf ? static_cast<int>(inbuf->datalen()) : -1) << "]" << " [this = " << this << "]";
            assert(!inbuf || httpReq->buffer_released);
            if (!inbuf || !httpReq->buffer_released)
            {
                assert((pos + partStartPos) == httpReq->dlpos);
                assert(postStartTime.time_since_epoch() != std::chrono::milliseconds(0));
                const auto& postEndTime = std::chrono::system_clock::now();
                auto reqTime = std::chrono::duration_cast<std::chrono::milliseconds>(postEndTime - postStartTime).count();
                timeInflight += reqTime;
                inbuf.reset(httpReq->release_buf());
                httpReq->buffer_released = true;
                reqBytesReceived += inbuf->datalen();
                postCompleted = true;
                rr->feedlag[part] += static_cast<unsigned>(inbuf->datalen() ? (((inbuf->datalen() / static_cast<unsigned>(reqTime)) * 1000) / 1024) : 0); // KB/s
                rr->resumeall(part);
            }


            while (remfeed && inbuf->datalen())
            {
                size_t bufSize = inbuf->datalen();
                if (remfeed < static_cast<m_off_t>(bufSize)) bufSize = remfeed;

                remfeed -= bufSize;
                rem -= bufSize;

                // completed a read: reset consecutive_errors
                if (!rem && consecutive_errors) consecutive_errors = 0;

                rr->procdata(part, inbuf->datastart(), pos, bufSize);

                if (!connected)
                {
                    break;
                }

                pos += bufSize;
                inbuf->start += bufSize;
            }

            lastdata = Waiter::ds;

            if (!remfeed && pos == sourcesize && sourcesize < rr->paddedpartsize)
            {
                // we have reached the end of a part requires padding
                static byte nulpad[RAIDSECTOR];

                rr->procdata(part, nulpad, pos, rr->paddedpartsize - sourcesize);
                rem = 0;
                pos = rr->paddedpartsize;
            }

            rr->procreadahead();

            if (!rem)
            {
                if (pos == rr->paddedpartsize)
                {
                    closesocket();
                    finished = true;
                    return -1;
                }
            }
            else if (inbuf->datalen() == 0)
            {
                inbuf.reset(nullptr);
                httpReq->status = REQ_READY;
            }
            else
            {
                setremfeed(std::min(static_cast<m_off_t>(NUMLINES * RAIDSECTOR), static_cast<m_off_t>(inbuf->datalen())));
            }
        }
        assert(httpReq->status == REQ_READY || httpReq->status == REQ_INFLIGHT || httpReq->status == REQ_SUCCESS || httpReq->status == REQ_FAILURE || httpReq->status == REQ_PREPARED);
        if (httpReq->status == REQ_FAILURE)
        {
            return onFailure();
        }

        if (httpReq->status == REQ_INFLIGHT)
        {
            directTrigger();
        }
        else
        {
            trigger();
        }
    }
    return -1;
}

// request a further chunk of data from the open connection
// (we cannot call io() directly due procdata() being non-reentrant)
void PartFetcher::cont(m_off_t numbytes)
{
    if (connected && pos < rr->paddedpartsize)
    {
        assert(!finished);
        setremfeed(numbytes);
        trigger();
    }
}

// feed suitable readahead data
bool PartFetcher::feedreadahead()
{
    if (readahead.empty()) return false;

    auto total = readahead.size();
    auto remaining = total;

    while (remaining)
    {
        auto it = readahead.begin();

        // make sure that we feed gaplessly
        if ((it->first < ((rr->dataline + rr->completed) * RAIDSECTOR)))
        {
            LOG_warn << "Gaps found on read ahead feed" << " [this = " << this << "]";
        }
        assert(it->first >= ((rr->dataline + rr->completed) * RAIDSECTOR));

        // we only take over from any source if we match the completed boundary precisely
        if (it->first == ((rr->dataline + rr->completed) * RAIDSECTOR)) rr->partpos[part] = it->first - (rr->dataline * RAIDSECTOR);

        // always continue at any position on our own source
        if (it->first != ((rr->dataline * RAIDSECTOR) + rr->partpos[part])) break;

        // we do not feed chunks that cannot even be processed in part (i.e. that start at or past the end of the buffer)
        if ((it->first - (rr->dataline * RAIDSECTOR)) >= (NUMLINES * RAIDSECTOR)) break;

        m_off_t p = it->first;
        byte* d = it->second.first;
        unsigned l = it->second.second;
        readahead.erase(it);

        rr->procdata(part, d, p, l);
        free(d);

        remaining--;
    }

    return total && total != remaining;
}

// resume fetching on a parked source that has become eligible again
void PartFetcher::resume(bool forceSetPosRem)
{
    bool resumeCondition = finished ? false : true;

    if (resumeCondition)
    {
        if (forceSetPosRem || ((!connected || !rem) && (pos < rr->paddedpartsize)))
        {
            setposrem();
        }

        if (rem || (pos < rr->paddedpartsize) || rr->httpReqs[part]->status == REQ_SUCCESS)
        {
            trigger();
        }
    }
}

m_off_t PartFetcher::progress() const
{
    m_off_t progressCount = 0;

    m_off_t totalReadAhead = 0;
    for (auto& it : readahead)
    {
        totalReadAhead += static_cast<m_off_t>(it.second.second);
    }
    auto& httpReq = rr->httpReqs[part];
    assert(httpReq != nullptr);
    m_off_t reqsProgress;
    switch (httpReq->status.load())
    {
        case REQ_INFLIGHT:
        {
            reqsProgress = rr->cloudRaid->transferred(httpReq);
            break;
        }
        case REQ_SUCCESS:
        {
            if (inbuf)
            {
                assert(httpReq->buffer_released);
                reqsProgress = inbuf->datalen();
            }
            else
            {
                reqsProgress = httpReq->size;
            }
            break;
        }
        default:
        {
            reqsProgress = 0;
            break;
        }
    }
    progressCount = totalReadAhead +
                    reqsProgress +
                    rr->partpos[part]; // Consecutive part data for not completed raidlines (i.e., other parts still not finished)
    if (progressCount &&
        (pos + reqsProgress) == rr->paddedpartsize)
    {
        // Parts are padded to paddedpartsize, so we can only count as much as sourcesize (which is the real data size for that part).
        assert(part != rr->unusedPart()); // If there is progress, it shouldn't be the unused part
        auto partIndex = part == 0 ? rr->unusedPart() : part; // Parity must get the sourcesize of the unused part (for example, if part 5 is the unused, it can be smaller than part 0, so we must adjust the progress)
        m_off_t paddedOffsetToSubstract = rr->paddedpartsize - rr->fetcher[partIndex].sourcesize;
        progressCount -= paddedOffsetToSubstract;
        assert ((progressCount + (rr->dataline * RAIDSECTOR)) == rr->fetcher[partIndex].sourcesize);
    }
    assert(progressCount >= 0);
    return progressCount;
}

/* -------------- PartFetcher END --------------*/


/* -------------- RaidReq --------------*/

RaidReq::RaidReq(const Params& p, RaidReqPool& rrp, const std::shared_ptr<CloudRaid>& cloudRaid)
    : pool(rrp)
    , cloudRaid(cloudRaid),
    rem(p.reqlen),
    filesize(p.filesize),
    reqStartPos(p.reqStartPos),
    paddedpartsize((raidPartSize(0, filesize)+RAIDSECTOR-1) & -RAIDSECTOR),
    maxRequestSize(p.maxRequestSize),
    lastdata(Waiter::ds)
{
    assert(p.tempUrls.size() > 0);
    assert((reqStartPos >= 0) && (rem <= filesize));
    assert(reqStartPos % RAIDSECTOR == 0);
    assert(reqStartPos % RAIDLINE == 0);

    httpReqs.resize(p.tempUrls.size());
    for(auto& httpReq : httpReqs)
    {
        httpReq = std::make_shared<HttpReqType>();
    }
    data.reset(new byte[DATA_SIZE]);
    parity.reset(new byte[PARITY_SIZE]);
    invalid.reset(new char[NUMLINES]);
    std::fill(data.get(), data.get() + DATA_SIZE, 0);
    std::fill(parity.get(), parity.get() + PARITY_SIZE, 0);
    std::fill(invalid.get(), invalid.get() + NUMLINES, (1 << RAIDPARTS) - 1);

    mUnusedRaidConnection = cloudRaid->getUnusedRaidConnection();
    if (mUnusedRaidConnection == RAIDPARTS)
    {
        LOG_verbose << "[RaidReq::RaidReq] No previous unused raid connection: set initial unused raid connection to 0" << " [this = " << this << "]";
        setNewUnusedRaidConnection(0, false);
    }

    LOG_verbose << "[RaidReq::RaidReq] filesize = " << filesize << ", paddedpartsize = " << paddedpartsize << ", maxRequestSize = " << maxRequestSize << ", unusedRaidConnection = " << (int)mUnusedRaidConnection << " [this = " << this << "]";


    for (uint8_t i = RAIDPARTS; i--; )
    {
        if (!p.tempUrls[i].empty())
        {
            // we don't trigger I/O on unknown source servers (which shouldn't exist in normal ops anyway)
            if (fetcher[i].setsource(p.tempUrls[i], this, i))
            {
                // this kicks off I/O on that source
                if (i != mUnusedRaidConnection) fetcher[i].trigger();
            }
        }
        else
        {
            missingsource = true;
        }
    }
}

RaidReq::~RaidReq()
{
    LOG_verbose << "[RaidReq::~RaidReq] DESTRUCTOR [this = " << this << "]";

    // Use feedlag to set next unused source
    uint8_t slowest, fastest;
    if (!faultysourceadded &&
        getSlowestAndFastestParts(slowest, fastest, false /* no need for the parts to be connected, just have feedlag */) &&
        differenceBetweenPartsSpeedIsSignificant(fastest, slowest))
    {
        LOG_verbose << "[RaidReq::~RaidReq] Detected slowest part for this RaidReq: " << (int)slowest << ". There's no sources with errors reported, so we will use this one as the unused connection for next RaidReq" << " [this = " << this << "]";
        cloudRaid->setUnusedRaidConnection(slowest, true);
    }
}

void RaidReq::shiftdata(m_off_t len)
{
    skip += len;
    rem -= len;

    if (rem)
    {
        auto shiftby = skip / RAIDLINE;

        completed -= shiftby;

        skip %= RAIDLINE;

        // we remove completed sectors/lines from the beginning of all state buffers
        m_off_t eobData = 0, eobAll = 0;
        for (uint8_t i = RAIDPARTS; i--; )
        {
            if (i > 0 && partpos[i] > eobData) eobData = partpos[i];
            if (partpos[i] > eobAll) eobAll = partpos[i];
        }
        eobData = (eobData + RAIDSECTOR - 1) / RAIDSECTOR;
        eobAll = (eobAll + RAIDSECTOR - 1) / RAIDSECTOR;

        if (eobData > shiftby)
        {
            memmove(data.get(), data.get() + (shiftby * RAIDLINE), (eobData - shiftby) * RAIDLINE);
        }

        if (eobAll > shiftby)
        {
            memmove(invalid.get(), invalid.get() + shiftby, eobAll - shiftby);
            std::fill(invalid.get() + eobAll - shiftby, invalid.get() + eobAll, (1 << RAIDPARTS) - 1);
        }
        else
        {
            std::fill(invalid.get(), invalid.get() + shiftby, (1 << RAIDPARTS) - 1);
        }

        dataline += shiftby;
        shiftby *= RAIDSECTOR;

        if (partpos[0] > shiftby)
        {
            memmove(parity.get(), parity.get() + shiftby, partpos[0] - shiftby);
            //std::copy(parity.get() + shiftby, parity.get() + partpos[0], parity.get());
        }

        // shift partpos[] by the dataline increment and retrigger data flow
        for (uint8_t i = RAIDPARTS; i--; )
        {
            partpos[i] -= shiftby;

            if (partpos[i] < 0) partpos[i] = 0;
            else
            {
                if (!fetcher[i].finished)
                {
                    // request 1 less RAIDSECTOR as we will add up to 1 sector of 0 bytes at the end of the file - this leaves enough buffer space in the buffer passed to procdata for us to write past the reported length
                    fetcher[i].cont(static_cast<m_off_t>((NUMLINES * RAIDSECTOR) - partpos[i]));
                }
            }
        }

        haddata = true;
        lastdata = Waiter::ds;
    }
}

bool RaidReq::allconnected(uint8_t excludedPart) const
{
    for (uint8_t i = RAIDPARTS; i--; )
    {
        if (i != excludedPart && !fetcher[i].connected)
        {
            return false;
        }
    }

    return true;
}

uint8_t RaidReq::numPartsUnfinished() const
{
    uint8_t count = 0;
    for (uint8_t i = RAIDPARTS; i--;)
    {
            if (!fetcher[i].finished)
            {
                count++;
            }
    }
    return count;
}

uint8_t RaidReq::hangingSources(uint8_t* hangingSource = nullptr, uint8_t* idleGoodSource = nullptr)
{
    uint8_t numHangingSources = 0;
    if (hangingSource) *hangingSource = -1;
    if (idleGoodSource) *idleGoodSource = -1;

    for (uint8_t i = RAIDPARTS; i--; )
    {
        if (fetcher[i].connected)
        {
            if (httpReqs[i]->status == REQ_INFLIGHT)
            {
                fetcher[i].lastdata = httpReqs[i]->lastdata;
                if (fetcher[i].remfeed && ((Waiter::ds - fetcher[i].lastdata) > PartFetcher::LASTDATA_DSTIME_FOR_HANGING_SOURCE))
                {
                    LOG_verbose << "[RaidReq::hangingSources] Part " << (int)i << " last data time reached time boundary: considering it as a hanging source [lastDataTime = " << ((Waiter::ds - fetcher[i].lastdata)/10) << " secs, limit = " << (PartFetcher::LASTDATA_DSTIME_FOR_HANGING_SOURCE/10) << " secs] [this = " << this << "]";
                    numHangingSources++;
                    if (hangingSource) *hangingSource = i;
                }
            }
        }
        else if (idleGoodSource && !fetcher[i].finished && !fetcher[i].errors) *idleGoodSource = i;
    }
    return numHangingSources;
}

// watchdog: resolve stuck connections
void RaidReq::watchdog()
{
    if (missingsource) return;

    // check for a single fast source hanging
    uint8_t hangingsource;
    uint8_t idlegoodsource;
    uint8_t hanging = hangingSources(&hangingsource, &idlegoodsource);

    if (hanging)
    {
        if (idlegoodsource < 0)
        {
            // Try a source with less errors:
            for (uint8_t i = RAIDPARTS; i--; )
            {
                if (!fetcher[i].connected &&
                    !fetcher[i].finished &&
                    (fetcher[i].errors <= MAX_ERRORS_FOR_IDLE_GOOD_SOURCE ||
                            (idlegoodsource >= 0 && fetcher[i].errors < fetcher[idlegoodsource].errors)))
                    idlegoodsource = i;
            }
        }
        if (idlegoodsource >= 0)
        {
            LOG_verbose << "Hanging source!! hangingsource = " << (int)hangingsource << " (HttpReq: " << (void*)httpReqs[hangingsource].get() << "), idlegoodsource = " << (int)idlegoodsource << " (HttpReq: " << (void*)httpReqs[idlegoodsource].get() << ") [fetcher[hangingsource].lastdata = " << fetcher[hangingsource].lastdata << ", Waiter::ds = " << Waiter::ds << "] [this = " << this << "]";
            fetcher[hangingsource].errors++;
            setNewUnusedRaidConnection(hangingsource);
            fetcher[hangingsource].closesocket();
            if (fetcher[idlegoodsource].trigger() == -1)
            {
                fetcher[hangingsource].trigger();
            }
            return;
        }
        else
        {
            LOG_verbose << "Hanging source and no idle good source to switch!! hangingsource = " << (int)hangingsource << " (HttpReq: " << (void*)httpReqs[hangingsource].get() << ") [fetcher[hangingsource].lastdata = " << fetcher[hangingsource].lastdata << ", Waiter::ds = " << Waiter::ds << "] [this = " << this << "]";
        }
    }
}

bool RaidReq::differenceBetweenPartsSpeedIsSignificant(uint8_t part1, uint8_t part2) const
{
    return feedlag[part1] * 4 > feedlag[part2] * 5;
}

bool RaidReq::getSlowestAndFastestParts(uint8_t& slowest, uint8_t& fastest, bool mustBeConnected) const
{
    slowest = 0;
    fastest = 0;

    uint8_t i = RAIDPARTS;
    while (i --> 0 &&
        ((mustBeConnected && !fetcher[slowest].connected) || !feedlag[slowest]))
    {
        slowest++;
        fastest++;
    }
    if (slowest == RAIDPARTS) // Cannot compare yet
    {
        return false;
    }
    for (i = RAIDPARTS; --i; )
    {
        if ((fetcher[i].connected || !mustBeConnected) &&
            feedlag[i])
        {
            if (feedlag[i] < feedlag[slowest])
                slowest = i;
            else if (feedlag[i] > feedlag[fastest])
                fastest = i;
        }
    }
    return true;
}

// procdata() handles input in any order/size and will push excess data to readahead
// data is assumed to be 0-padded to paddedpartsize at EOF
void RaidReq::procdata(uint8_t part, byte* ptr, m_off_t pos, m_off_t len)
{
    m_off_t basepos = dataline * RAIDSECTOR;

    // we never read backwards
    assert((pos & -RAIDSECTOR) >= (basepos + (partpos[part] & -RAIDSECTOR)));

    bool consecutive = pos == basepos + partpos[part];

    // is the data non-consecutive (i.e. a readahead), OR is it extending past the end of our buffer?
    if (!consecutive || ((pos + len) > (basepos + (NUMLINES * RAIDSECTOR))))
    {
        auto ahead_ptr = ptr;
        auto ahead_pos = pos;
        auto ahead_len = len;

        // if this is a consecutive feed, we store the overflowing part as readahead data
        // and process the non-overflowing part normally
        if (consecutive)
        {
            ahead_pos = basepos + (NUMLINES * RAIDSECTOR);
            ahead_ptr = ptr + (ahead_pos - pos);
            ahead_len = len - (ahead_pos - pos);
        }

        // enqueue for future processing
        auto itReadAhead = fetcher[part].readahead.find(ahead_pos);
        if (itReadAhead == fetcher[part].readahead.end() || itReadAhead->second.second < ahead_len)
        {
            byte* p = itReadAhead != fetcher[part].readahead.end() ?
                            static_cast<byte*>(std::realloc(itReadAhead->second.first, ahead_len)) :
                            static_cast<byte*>(malloc(ahead_len));
            std::copy(ahead_ptr, ahead_ptr + ahead_len, p);
            fetcher[part].readahead[ahead_pos] = pair<byte*, unsigned>(p, static_cast<unsigned>(ahead_len));
        }
        // if this is a pure readahead, we're done
        if (!consecutive) return;

        len = ahead_pos - pos;
    }

    // non-readahead data must flow contiguously
    assert(pos == partpos[part] + (dataline * RAIDSECTOR));

    partpos[part] += len;

    auto t = pos - (dataline * RAIDSECTOR);

    // ascertain absence of overflow (also covers parity part)
    assert((t + len) <= static_cast<m_off_t>(DATA_SIZE / (RAIDPARTS - 1)));

    // set valid bit for every block that's been received in full
    char partmask = 1 << part;
    m_off_t until = (t + len) / RAIDSECTOR; // Number of sectors - corresponding to the number of lines for this part (for each part, N_RAIDSECTORS == N_RAIDLINES)
    for (m_off_t i = t / RAIDSECTOR; i < until; i++)
    {
        assert(invalid[i] & partmask);
        invalid[i] -= partmask;
    }

    // copy (partial) blocks to data or parity buf
    if (part)
    {
        part--;

        byte* ptr2 = ptr;
        m_off_t len2 = len;
        byte* target = data.get() + part * RAIDSECTOR + (t / RAIDSECTOR) * RAIDLINE;
        auto partialSector = t % RAIDSECTOR;
        if (partialSector != 0)
        {
            target += partialSector;
            auto sectorBytes = std::min<m_off_t>(len2, RAIDSECTOR - partialSector);
            std::copy(ptr2, ptr2 + sectorBytes, target);
            target += sectorBytes + RAIDSECTOR * (RAIDPARTS - 2);
            len2 -= sectorBytes;
            ptr2 += sectorBytes;
        }
        while (len2 >= RAIDSECTOR)
        {
            *reinterpret_cast<raidsector_t*>(target) = *reinterpret_cast<raidsector_t*>(ptr2);
            target += RAIDSECTOR * (RAIDPARTS - 1);
            ptr2 += RAIDSECTOR;
            len2 -= RAIDSECTOR;
        }
        partialSector = len2;
        if (partialSector != 0)
        {
            std::copy(ptr2, ptr2 + partialSector, target);
        }
    }
    else
    {
        // store parity data for subsequent merging
        std::copy(ptr, ptr + len, parity.get() + t);
    }

    // merge new consecutive completed RAID lines so they are ready to be sent, direct from the data[] array
    auto old_completed = completed;
    for (; completed < until; completed++)
    {
        unsigned char mask = invalid[completed];
        std::bitset<CHAR_BIT * sizeof(unsigned char)> bits(mask);
        auto bitsSet = bits.count();

        assert(bitsSet);
        if (bitsSet > 1)
        {
            break;
        }
        else
        {
            if (!(mask & 1))
            {
                // parity involved in this line

                int index = -1;
#ifdef _MSC_VER
                unsigned long bitIndex;
                if (_BitScanForward(&bitIndex, mask))
                {
                    index = static_cast<int>(bitIndex);
                }
#else
                // __GNUC__ is defined for both GCC and Clang
#if defined(__GNUC__)
                index = __builtin_ctz(mask); // counts least significant consecutive 0 bits (ie 0-based index of least significant 1 bit).  Windows equivalent is _bitScanForward
#else
                // Fallback to a loop for other compilers
                for (uint8_t i = 0; i < RAIDLINE; ++i)
                {
                    if (mask & (1 << i))
                    {
                        index = i;
                        break;
                    }
                }
#endif
#endif
                if (index != -1) // index > 0 && index < RAIDLINE
                {
                    auto sectors = reinterpret_cast<raidsector_t*>(data.get() + (RAIDLINE * completed));
                    raidsector_t& target = sectors[index - 1];

                    target = reinterpret_cast<raidsector_t*>(parity.get())[completed];
                    if (!(mask & (1 << 1))) target ^= sectors[0]; // this method requires source and target are both aligned to their size
                    if (!(mask & (1 << 2))) target ^= sectors[1];
                    if (!(mask & (1 << 3))) target ^= sectors[2];
                    if (!(mask & (1 << 4))) target ^= sectors[3];
                    if (!(mask & (1 << 5))) target ^= sectors[4];
                }
            }
        }
    }

    if (completed > old_completed)
    {
        lastdata = Waiter::ds;
    }
}

m_off_t RaidReq::readdata(byte* buf, m_off_t len)
{
    watchdog();

    m_off_t lenCompleted = 0;
    m_off_t old_completed, new_completed;
    m_off_t t;
    do
    {
        if (completed < NUMLINES)
        {
            old_completed = completed;
            procreadahead();
        }
        else
        {
            old_completed = 0;
        }
        new_completed = completed;
        t = (completed * RAIDLINE) - skip;


        if (t > 0)
        {
            if ((t + lenCompleted) > len) t = len - lenCompleted;
            memmove(buf + lenCompleted, data.get() + skip, t);
            //std::copy(data.get() + skip, data.get() + skip + t, buf.get() + lenCompleted);
            lenCompleted += t;

            shiftdata(t);
        }
        else
        {
            auto lastDataOffset = Waiter::ds - lastdata;
            if (lastDataOffset > LASTDATA_DSTIME_FOR_REPORTING_FEED_STUCK)
            {
                bool feedStuck = true;
                uint8_t hanging = hangingSources();
                feedStuck = hanging ? true :
                            (lastDataOffset > LASTDATA_DSTIME_FOR_REPORTING_FEED_STUCK_WITH_NO_HANGING_SOURCES);

                if (feedStuck)
                {
                    const auto lastDataDsTimeForTimeout = hanging ? LASTDATA_DSTIME_FOR_TIMEOUT : LASTDATA_DSTIME_FOR_TIMEOUT_WITH_NO_HANGING_SOURCES;
                    if (lastDataOffset > lastDataDsTimeForTimeout)
                    {
                        LOG_warn << "CloudRAID feed timed out [lastDataTime = " << (lastDataOffset/10) << " secs, haddata = " << haddata << "] [hangingSources = " << (int)hanging << "] [this = " << this << "]";
                        return -1;
                    }
                    if (!reported)
                    {
                        reported = true;
                        LOG_warn << "CloudRAID feed stuck [lastDataTime = " << (lastDataOffset/10) << " secs, haddata = " << haddata << "] [hangingSources = " << (int)hanging << "] [this = " << this << "]";
                    }
                }
                else
                {
                    LOG_verbose << "CloudRAID reached soft limit for reporting feed stuck, but there are no hanging sources, so limit is increased to hard limit [lastDataTime = " << (lastDataOffset/10) << " secs, haddata = " << haddata << "] [soft limit = " << (LASTDATA_DSTIME_FOR_REPORTING_FEED_STUCK/10) << " secs, hard limit = " << (LASTDATA_DSTIME_FOR_REPORTING_FEED_STUCK_WITH_NO_HANGING_SOURCES/10) << " secs] [this = " << this << "]";
                }
            }
        }
    } while (new_completed > old_completed && t > 0 && lenCompleted < len);

    if (lenCompleted)
    {
        resumeall();
    }

    return lenCompleted;
}

// try to resume fetching on all sources
void RaidReq::resumeall(uint8_t excludedPart)
{
    if (rem)
    {
        {
            for (uint8_t i = RAIDPARTS; i--; )
            {
                if (i != excludedPart)
                {
                    if (fetcher[i].finished)
                    {
                        fetcher[i].directTrigger();
                    }
                    else if (fetcher[i].connected)
                    {
                        fetcher[i].resume();
                    }
                }
            }
        }
    }
}

void RaidReq::dispatchio(const HttpReqPtr& httpReq)
{
    // fast lookup of which PartFetcher to call from a single cache line
    // we don't check for httpReq not being found since we know sometimes it won't be when we closed a socket to a slower/nonresponding server
    for (uint8_t i = RAIDPARTS; i--; )
    {
        if (httpReqs[i] == httpReq)
        {
            int t = fetcher[i].io();

            if (t > 0)
            {
                // this is a relatively infrequent ocurrence, so we tolerate the overhead of a std::set insertion/erasure
                pool.addScheduledio(Waiter::ds + t, httpReqs[i]);
            }
            break;
        }
    }
}

// feed relevant read-ahead data to procdata
// returns true if any data was processed
void RaidReq::procreadahead()
{
    bool fed;

    do {
        fed = false;

        for (uint8_t i = RAIDPARTS; i--; )
        {
            if (fetcher[i].feedreadahead()) fed = true;
        }
    } while (fed);
}

void RaidReq::disconnect()
{
    for (auto& httpReq : httpReqs)
    {
        httpReq->disconnect();
    }
}

uint8_t RaidReq::processFeedLag()
{
    uint8_t laggedPart = RAIDPARTS;
    if (++lagrounds >= numPartsUnfinished())
    {
        // dominance is defined as the ratio between fastest and slowest
        uint8_t slowest, fastest;
        if (!getSlowestAndFastestParts(slowest, fastest)) // Cannot compare yet
        {
            return RAIDPARTS;
        }

        if (!missingsource && fetcher[slowest].connected && !fetcher[slowest].finished && httpReqs[slowest]->status != REQ_SUCCESS &&
            ((fetcher[slowest].rem - fetcher[fastest].rem) > ((NUMLINES * RAIDSECTOR * LAGINTERVAL * 3) / 4) || differenceBetweenPartsSpeedIsSignificant(fastest, slowest)))
        {
            // slow channel detected
            {
                LOG_debug << "Slow channel " << (int)slowest << " (" << (void*)httpReqs[slowest].get() << ")" << " detected!" << " [this = " << this << "]";
                fetcher[slowest].errors++;

                // check if we have a fresh and idle channel left to try
                uint8_t fresh = RAIDPARTS - 1;
                while (fresh >= 0 && (fetcher[fresh].connected || fetcher[fresh].errors || fetcher[fresh].finished))
                {
                    fresh--;
                }
                if (fresh >= 0)
                {
                    LOG_verbose << "New fresh channel: " << (int)fresh << " (" << (void*)httpReqs[fresh].get() << ")" << " [this = " << this << "]";
                    setNewUnusedRaidConnection(slowest);
                    fetcher[slowest].closesocket();
                    fetcher[fresh].resume(true);
                    laggedPart = slowest;
                }
                else { LOG_verbose << "No fresh channel to switch with the slow channel" << " [this = " << this << "]"; }
            }
        }

        if (laggedPart != RAIDPARTS)
        {
            std::fill(feedlag.begin(), feedlag.end(), 0);
        }
        lagrounds = 0;
    }
    return laggedPart;
}

m_off_t RaidReq::progress() const
{
    m_off_t progressCount = 0;
    for (uint8_t i = RAIDPARTS; i--; )
    {
        m_off_t partProgress = fetcher[i].progress();
        progressCount += partProgress;
    }
    assert((completed * RAIDLINE) - skip >= 0);
    progressCount += ((completed * RAIDLINE) - skip);
    assert(progressCount >= 0);
    assert(progressCount <= filesize);
    return progressCount;
}

uint8_t RaidReq::unusedPart() const
{
    uint8_t partIndex = RAIDPARTS;
    for (uint8_t i = RAIDPARTS; i--;)
    {
        if (!fetcher[i].connected && !fetcher[i].finished && !pool.lookupHttpReq(httpReqs[i]))
        {
            if (partIndex != RAIDPARTS)
            {
                LOG_warn << "[RaidReq::unusedPart] More than one unused part detected!!! [unusedPart = " << (int)partIndex << ", otherUnusedPart = " << (int)i << "]" << " [this = " << this << "]";
                assert(false && "More than one unused part detected!!!!!");
            }
            partIndex = i;
        }
    }
    if (partIndex != mUnusedRaidConnection)
    {
        LOG_warn << "[RaidReq::unusedPart] Unused part (" << (int)partIndex << ") does not match with unused raid connection (" << (int)mUnusedRaidConnection << ") !!!!" << " [this = " << this << "]";
        assert(false && "Unused part does not match with unused raid connection");
    }
    assert(partIndex != RAIDPARTS);
    return partIndex;
}

std::pair<::mega::error, raidTime> RaidReq::checkTransferFailure()
{
    return cloudRaid->checkTransferFailure();
}

bool RaidReq::setNewUnusedRaidConnection(uint8_t part, bool addToFaultyServers)
{
    if (!cloudRaid->setUnusedRaidConnection(part, addToFaultyServers))
    {
        LOG_warn << "[RaidReq::setNewUnusedRaidConnection] Could not set unused raid connection, setting it to 0" << " [this = " << this << "]";
        assert(false && "Unused raid connection couldn't be set");
        mUnusedRaidConnection = 0;
        return false;
    }

    LOG_verbose << "[RaidReq::setNewUnusedRaidConnection] Set unused raid connection to " << (int)part << " (clear previous unused connection: " << (int)mUnusedRaidConnection << ") [addToFaultyServers = " << addToFaultyServers << "]" << " [this = " << this << "]";
    mUnusedRaidConnection = part;
    if (addToFaultyServers) faultysourceadded = true;
    return true;
}

size_t RaidReq::raidPartSize(uint8_t part, size_t fullfilesize)
{
    // compute the size of this raid part based on the original file size len
    m_off_t r = static_cast<m_off_t>(fullfilesize) % RAIDLINE; // residual part

    // parts 0 (parity) & 1 (largest data) are the same size
    m_off_t t = r - ((part - !!part) * RAIDSECTOR);

    // (excess length will be found in the following sectors,
    // negative length in the preceding sectors)
    if (t < 0) t = 0;
    else if (t > RAIDSECTOR) t = RAIDSECTOR;

    LOG_debug << "[RaidReq::raidPartSize] return (fullfilesize - r) / (RAIDPARTS - 1) + t = (" << fullfilesize << " -  " << r << ") / (" << (RAIDPARTS - 1) << ") + " << t << " = " << ((fullfilesize - r) / (RAIDPARTS - 1)) << " + " << t << " = " << ((fullfilesize - r) / (RAIDPARTS - 1) + t);

    return (fullfilesize - r) / (RAIDPARTS - 1) + t;
}

/* -------------- RaidReq END --------------*/


/* -------------- RaidReqPool --------------*/

bool RaidReqPool::addScheduledio(raidTime scheduledFor, const HttpReqPtr& req)
{
    auto it = setHttpReqs.insert(req);
    if (it.second)
    {
        auto itScheduled = scheduledio.insert(std::make_pair(scheduledFor, req));
        return itScheduled.second;
    }
    return false;
}

bool RaidReqPool::addDirectio(const HttpReqPtr& req)
{
    return addScheduledio(0, req);
}

bool RaidReqPool::removeio(const HttpReqPtr& req)
{
    return setHttpReqs.erase(req);
}

void RaidReqPool::raidproxyio()
{
    if (isRunning)
    {
        if (!scheduledio.empty())
        {
            auto itScheduled = scheduledio.begin();
            bool transferFailed = false;
            while (isRunning && !transferFailed && (itScheduled != scheduledio.end() && (itScheduled->first <= Waiter::ds)))
            {
                const HttpReqPtr& httpReq = itScheduled->second;
                if (httpReq->status != REQ_INFLIGHT)
                {
                    if ((setHttpReqs.find(httpReq) != setHttpReqs.end()))
                    {
                        raidReq->dispatchio(httpReq);
                        itScheduled++;
                    }
                    else
                    {
                        itScheduled = scheduledio.erase(itScheduled);
                    }
                }
                else
                {
                    itScheduled++;
                }
                if (raidReq->checkTransferFailure().first)
                {
                    LOG_debug << "[RaidReqPool::raidproxyio] Found transfer failed flag. Stop" << " [this = " << this << "]";
                    transferFailed = true;
                }
            }
        }
    }
}

RaidReqPool::RaidReqPool()
{
    LOG_verbose << "[RaidReqPool::RaidReqPool] CONSTRUCTOR CALL [this = " << this << "]";
}

RaidReqPool::~RaidReqPool()
{
    LOG_verbose << "[RaidReqPool::~RaidReqPool] DESTRUCTOR CALL [this = " << this << "]";
    isRunning = false;
    raidReq.reset();
}

void RaidReqPool::request(const RaidReq::Params& p, const std::shared_ptr<CloudRaid>& cloudRaid)
{
    RaidReq* rr = new RaidReq(p, *this, cloudRaid);
    raidReq.reset(rr);
}

/* -------------- RaidReqPool END --------------*/