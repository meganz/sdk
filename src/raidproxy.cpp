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

// Macro to calculate the number of sectors per part based on NUMLINES (NL)
#define SECTORSPERPART(NL) (NL * RAIDSECTOR)


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

        while (!mReadahead.empty())
        {
            free(mReadahead.begin()->second.first);
            mReadahead.erase(mReadahead.begin());
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
    m_off_t basepos = rr->mDataline * RAIDSECTOR;
    m_off_t curpos = basepos + rr->mPartpos[part];

    // determine the next suitable read range to ensure the availability
    // of EFFECTIVE_RAIDPARTS sources based on ongoing reads and stored readahead data.
    for (m_off_t i = RAIDPARTS; i--; )
    {
        // compile boundaries of data chunks that have been or are being fetched
        // (we do not record the beginning, as position 0 is implicitly valid)

        // a) already read data in the PartReq buffer
        chunks[SECTORFLOOR(basepos + rr->mPartpos[i])]--;

        // b) ongoing fetches on *other* channels
        if (i != part)
        {
            if (rr->mFetcher[i].mRem)
            {
                chunks[SECTORFLOOR(rr->mFetcher[i].mPos)]++;
                chunks[SECTORFLOOR(rr->mFetcher[i].mPos + rr->mFetcher[i].mRem)]--;
            }
        }

        // c) existing readahead data
        auto it = rr->mFetcher[i].mReadahead.begin();
        auto end = rr->mFetcher[i].mReadahead.end();

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
    // with less than EFFECTIVE_RAIDPARTS valid sources
    // (where we need to start fetching) to the first position thereafter with
    // EFFECTIVE_RAIDPARTS valid sources, if any (where we would need to stop fetching)
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
            if (valid < EFFECTIVE_RAIDPARTS)
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
            if (valid >= EFFECTIVE_RAIDPARTS)
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
        mRem = rr->mPaddedpartsize - startpos;

    }
    else
    {
        assert(endpos >= startpos);
        mRem = endpos - startpos;
    }

    mPos = startpos;

    setremfeed(mRem);
}

bool PartFetcher::setremfeed(m_off_t numBytes)
{
    // request 1 less RAIDSECTOR as we will add up to 1 sector of 0 bytes at the end of the file - this leaves enough buffer space in the buffer pased to procdata for us to write past the reported length
    mRemfeed = numBytes ? std::min(mRem, numBytes) : mRem;
    if (mSourcesize - mPos < mRemfeed)
    {
        if (mSourcesize - mPos >= 0)
        {
            mRemfeed = mSourcesize - mPos; // we only read to the physical end of the part
        }
        else
        {
            mRem = 0;
            mRemfeed = 0;
        }
    }
    return mRemfeed != 0;
}

int64_t PartFetcher::onFailure()
{
    auto& httpReq = rr->mHttpReqs[part];
    assert(httpReq != nullptr);
    if (httpReq->status == REQ_FAILURE)
    {
        raidTime backoff = 0;
        {
            if (rr->mCloudRaid->onRequestFailure(httpReq, part, backoff))
            {
                if (httpReq->status == REQ_FAILURE)
                {
                    auto failValues = rr->mCloudRaid->checkTransferFailure();
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
        mErrors++;

        if (mConsecutiveErrors > MAXRETRIES || httpReq->status == REQ_READY)
        {
            rr->setNewUnusedRaidConnection(part);
            closesocket();
            for (uint8_t i = RAIDPARTS; i--;)
            {
                if (i != part && !rr->mFetcher[i].mConnected)
                {
                    rr->mFetcher[i].trigger();
                }
            }
            return -1;
        }
        else
        {
            mConsecutiveErrors++;
            for (uint8_t i = RAIDPARTS; i--;)
            {
                if (i != part && !rr->mFetcher[i].mConnected)
                {
                    rr->mFetcher[i].trigger();
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
    if (!mTimeInflight)
    {
        return 0;
    }
    // In Bytes per millisec
    return mReqBytesReceived / mTimeInflight;
}

bool PartFetcher::setsource(const std::string& partUrl, RaidReq* crr, uint8_t cpart)
{
    mUrl = partUrl;
    part = cpart;
    rr = crr;
    mPartStartPos = rr->mReqStartPos / EFFECTIVE_RAIDPARTS;
    assert(mPartStartPos % RAIDSECTOR == 0);

    mSourcesize = RaidReq::raidPartSize(part, rr->mFilesize);
    LOG_debug << "[PartFetcher::setsource] part = " << (int)cpart << ", partStartPos = " << mPartStartPos << ", sourcesize = " << mSourcesize << " [this = " << this << "]";
    return true;
}

// (re)create, set up socket and start (optionally delayed) io on it
int64_t PartFetcher::trigger(raidTime delay, bool disconnect)
{
    if (delay == MAX_DELAY_IN_SECONDS)
    {
        rr->mCloudRaid->setTransferFailure();
        return -1;
    }
    assert(!mUrl.empty());

    if (mFinished)
    {
        closesocket();
        return -1;
    }

    if (disconnect)
    {
        if (rr->mHttpReqs[part]->status == REQ_SUCCESS)
        {
            mRem = 0;
            mRemfeed = 0;
        }
        else closesocket(true);
    }

    if (!mRem)
    {
        assert(mPos <= rr->mPaddedpartsize);
        if (mPos == rr->mPaddedpartsize)
        {
            closesocket();
            return -1;
        }
    }

    directTrigger(!delay);

    if (delay) mDelayuntil = Waiter::ds + delay;

    return delay;
}

bool PartFetcher::directTrigger(bool addDirectio)
{
    auto httpReq = rr->mHttpReqs[part];

    assert(httpReq != nullptr);
    if (addDirectio && rr->mPool.addDirectio(httpReq))
    {
        return true;
    }
    return !addDirectio;
}

// close socket
void PartFetcher::closesocket(bool reuseSocket)
{
    mRem = 0;
    mRemfeed = 0; // need to clear remfeed so that the disconnected channel does not corrupt feedlag

    mPostCompleted = false;
    if (mInbuf) mInbuf.reset(nullptr);

    auto& httpReq = rr->mHttpReqs[part];
    if (httpReq)
    {
        if (mConnected)
        {
            if (!reuseSocket || httpReq->status == REQ_INFLIGHT)
            {
                rr->mCloudRaid->disconnect(httpReq);
            }
            httpReq->status = REQ_READY;
        }
        rr->mPool.removeio(httpReq);
    }
    mConnected = false;
}

// perform I/O on socket (which is assumed to exist)
int64_t PartFetcher::io()
{
    // prevent spurious epoll events from triggering a delayed reconnect early
    if (mFinished && rr->mCompleted < rr->mNumLines && (rr->mRem > ((rr->mCompleted * RAIDLINE) - rr->mSkip)))
    {
        rr->procreadahead();
    }
    if ((Waiter::ds < mDelayuntil) || mFinished) return -1;

    auto httpReq = rr->mHttpReqs[part];
    assert(httpReq != nullptr);


    if (httpReq->status == REQ_FAILURE)
    {
        return onFailure();
    }
    else if (rr->allconnected(part))
    {
        LOG_warn << "There are 6 connected channels, and there shouldn't be" << " [this = " << this << "]";
        assert(false && "There shouldn't be 6 connected channels");
        // we only need EFFECTIVE_RAIDPARTS connections, so shut down the slowest one
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
    else if (!mRem && httpReq->status != REQ_SUCCESS)
    {
        setposrem();
        if (httpReq->status == REQ_PREPARED)
        {
            httpReq->status = REQ_READY;
        }
    }

    if (httpReq->status == REQ_READY)
    {
        if (mRem <= 0)
        {
            closesocket();
            rr->resumeall(part);
            return -1;
        }

        if (mPostCompleted && (rr->processFeedLag() == part))
        {
            return -1;
        }

        if (mInbuf)
        {
            mInbuf.reset(nullptr);
        }

        m_off_t npos = mPos + mRem;
        assert(npos <= rr->mPaddedpartsize);
        LOG_verbose << "[PartFetcher::io] [part " << (int)part << "] prepareRequest -> pos = " << (mPos + mPartStartPos) << ", npos = " << (npos + mPartStartPos) << " [partStartPos = " << mPartStartPos << ", mRem = " << mRem << "]" << " [this = " << this << "]";
        rr->mCloudRaid->prepareRequest(httpReq, mUrl, mPos + mPartStartPos, npos + mPartStartPos);
        assert(httpReq->status == REQ_PREPARED);
        mConnected = true;
    }

    if (mConnected)
    {
        if (httpReq->status == REQ_PREPARED)
        {
            bool postDone = rr->mCloudRaid->post(httpReq);
            if (postDone)
            {
                lastdata = Waiter::ds;
                mPostStartTime = std::chrono::system_clock::now();
            }
            else
            {
                return onFailure();
            }
        }

        if (httpReq->status == REQ_SUCCESS)
        {
            rr->mCloudRaid->processRequestLatency(httpReq);
            LOG_verbose << "[PartFetcher::io] [part " << (int)part << "] REQ_SUCCESS [pos = " << mPos << ", partStartPos = " << mPartStartPos << "] [httpReq->dlpos = " << httpReq->dlpos << ", httpReq->size = " << httpReq->size << ", inbuf = " << (mInbuf ? static_cast<int>(mInbuf->datalen()) : -1) << "]" << " [this = " << this << "]";
            assert(!mInbuf || httpReq->buffer_released);
            if (!mInbuf || !httpReq->buffer_released)
            {
                assert((mPos + mPartStartPos) == httpReq->dlpos);
                assert(mPostStartTime.time_since_epoch() != std::chrono::milliseconds(0));
                const auto& postEndTime = std::chrono::system_clock::now();
                auto reqTime = std::chrono::duration_cast<std::chrono::milliseconds>(postEndTime - mPostStartTime).count();
                mTimeInflight += reqTime;
                mInbuf.reset(httpReq->release_buf());
                httpReq->buffer_released = true;
                mReqBytesReceived += mInbuf->datalen();
                mPostCompleted = true;
                rr->mFeedlag[part] += static_cast<unsigned>(mInbuf->datalen() ? (((mInbuf->datalen() / static_cast<unsigned>(reqTime)) * 1000) / 1024) : 0); // KB/s
                rr->resumeall(part);
            }

            while (mRemfeed && mInbuf->datalen())
            {
                size_t bufSize = mInbuf->datalen();
                if (mRemfeed < static_cast<m_off_t>(bufSize)) bufSize = mRemfeed;

                mRemfeed -= bufSize;
                mRem -= bufSize;

                // completed a read: reset consecutive_errors
                if (!mRem && mConsecutiveErrors) mConsecutiveErrors = 0;

                rr->procdata(part, mInbuf->datastart(), mPos, bufSize);

                if (!mConnected)
                {
                    break;
                }

                mPos += bufSize;
                mInbuf->start += bufSize;
            }

            lastdata = Waiter::ds;

            if (!mRemfeed && mPos == mSourcesize && mSourcesize < rr->mPaddedpartsize)
            {
                // we have reached the end of a part requires padding
                static byte nulpad[RAIDSECTOR];

                rr->procdata(part, nulpad, mPos, rr->mPaddedpartsize - mSourcesize);
                mRem = 0;
                mPos = rr->mPaddedpartsize;
            }

            rr->procreadahead();

            if (!mRem)
            {
                if (mPos == rr->mPaddedpartsize)
                {
                    closesocket();
                    mFinished = true;
                    return -1;
                }
            }
            else if (mInbuf->datalen() == 0)
            {
                mInbuf.reset(nullptr);
                httpReq->status = REQ_READY;
            }
            else
            {
                setremfeed(mInbuf->datalen());
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
void PartFetcher::cont(m_off_t numbytes)
{
    if (mConnected && mPos < rr->mPaddedpartsize)
    {
        assert(!mFinished);
        setremfeed(numbytes);
        trigger();
    }
}

// feed suitable readahead data
bool PartFetcher::feedreadahead()
{
    if (mReadahead.empty()) return false;

    auto total = mReadahead.size();
    auto remaining = total;

    while (remaining)
    {
        auto it = mReadahead.begin();

        // make sure that we feed gaplessly
        if ((it->first < ((rr->mDataline + rr->mCompleted) * RAIDSECTOR)))
        {
            LOG_warn << "Gaps found on read ahead feed" << " [this = " << this << "]";
        }
        assert(it->first >= ((rr->mDataline + rr->mCompleted) * RAIDSECTOR));

        // we only take over from any source if we match the completed boundary precisely
        if (it->first == ((rr->mDataline + rr->mCompleted) * RAIDSECTOR)) rr->mPartpos[part] = it->first - (rr->mDataline * RAIDSECTOR);

        // always continue at any position on our own source
        if (it->first != ((rr->mDataline * RAIDSECTOR) + rr->mPartpos[part])) break;

        // we do not feed chunks that cannot even be processed in part (i.e. that start at or past the end of the buffer)
        if ((it->first - (rr->mDataline * RAIDSECTOR)) >= SECTORSPERPART(rr->mNumLines)) break;

        m_off_t p = it->first;
        byte* d = it->second.first;
        unsigned l = it->second.second;
        mReadahead.erase(it);

        rr->procdata(part, d, p, l);
        free(d);

        remaining--;
    }

    return total && total != remaining;
}

// resume fetching on a parked source that has become eligible again
void PartFetcher::resume(bool forceSetPosRem)
{
    bool resumeCondition = mFinished ? false : true;

    if (resumeCondition)
    {
        if (forceSetPosRem || ((!mConnected || !mRem) && (mPos < rr->mPaddedpartsize)))
        {
            setposrem();
        }

        if (mRem || (mPos < rr->mPaddedpartsize) || rr->mHttpReqs[part]->status == REQ_SUCCESS)
        {
            trigger();
        }
    }
}

m_off_t PartFetcher::progress() const
{
    m_off_t progressCount = 0;

    m_off_t totalReadAhead = 0;
    for (auto& it : mReadahead)
    {
        totalReadAhead += static_cast<m_off_t>(it.second.second);
    }
    auto& httpReq = rr->mHttpReqs[part];
    assert(httpReq != nullptr);
    m_off_t reqsProgress;
    switch (httpReq->status.load())
    {
        case REQ_INFLIGHT:
        {
            reqsProgress = rr->mCloudRaid->transferred(httpReq);
            break;
        }
        case REQ_SUCCESS:
        {
            if (mInbuf)
            {
                assert(httpReq->buffer_released);
                reqsProgress = mInbuf->datalen();
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
                    rr->mPartpos[part]; // Consecutive part data for not completed raidlines (i.e., other parts still not finished)
    if (progressCount)
    {
        // Parts are padded to paddedpartsize, so we can only count as much as sourcesize (which is the real data size for that part).
        assert(part != rr->unusedPart()); // If there is progress, this part shouldn't be the unused one.
        auto partIndex = part == 0 ? rr->unusedPart() : part; // Parity must get the sourcesize of the unused part (for example, if part 5 is the unused one, it can be smaller than part 0, so we must adjust the progress)
        m_off_t paddedOffsetToSubtract = (mPos + reqsProgress) - rr->mFetcher[partIndex].mSourcesize;
        if (paddedOffsetToSubtract > 0) // The current position is above the source size, subtract it.
        {
            assert((mPos + reqsProgress) <= rr->mPaddedpartsize);
            progressCount -= paddedOffsetToSubtract;
            assert ((progressCount + (rr->mDataline * RAIDSECTOR)) == rr->mFetcher[partIndex].mSourcesize);
        }
    }
    assert(progressCount >= 0);
    return progressCount;
}

/* -------------- PartFetcher END --------------*/


/* -------------- RaidReq --------------*/

RaidReq::RaidReq(const Params& p, RaidReqPool& rrp, const std::shared_ptr<CloudRaid>& cloudRaid)
    : mPool(rrp),
    mCloudRaid(cloudRaid),
    mRem(p.reqlen),
    mFilesize(p.filesize),
    mReqStartPos(p.reqStartPos),
    mPaddedpartsize((raidPartSize(0, mFilesize) + RAIDSECTOR - 1) & -RAIDSECTOR),
    lastdata(Waiter::ds)
{
    assert(p.tempUrls.size() > 0);
    assert((mReqStartPos >= 0) && (mRem <= static_cast<m_off_t>(mFilesize)));
    assert(mReqStartPos % RAIDSECTOR == 0);
    assert(mReqStartPos % RAIDLINE == 0);

    mHttpReqs.resize(p.tempUrls.size());
    for(auto& httpReq : mHttpReqs)
    {
        httpReq = std::make_shared<HttpReqType>();
    }
    calculateNumLinesAndBufferSizes();
    mData.reset(new byte[mDataSize]);
    mParity.reset(new byte[mParitySize]);
    mInvalid.reset(new char[mNumLines]);
    std::fill(mData.get(), mData.get() + mDataSize, 0);
    std::fill(mParity.get(), mParity.get() + mParitySize, 0);
    std::fill(mInvalid.get(), mInvalid.get() + mNumLines, (1 << RAIDPARTS) - 1);

    mUnusedRaidConnection = mCloudRaid->getUnusedRaidConnection();
    if (mUnusedRaidConnection == RAIDPARTS)
    {
        LOG_verbose << "[RaidReq::RaidReq] No previous unused raid connection: set initial unused raid connection to 0" << " [this = " << this << "]";
        setNewUnusedRaidConnection(0, false);
    }

    LOG_verbose << "[RaidReq::RaidReq] filesize = " << mFilesize << ", paddedpartsize = " << mPaddedpartsize << ", unusedRaidConnection = " << (int)mUnusedRaidConnection << " [mNumLines = " << mNumLines << ", mDataSize = " << mDataSize << ", mParitySize = " << mParitySize << ", MAX_NUMLINES = " << MAX_NUMLINES << "] [this = " << this << "]";


    for (uint8_t i = RAIDPARTS; i--; )
    {
        if (!p.tempUrls[i].empty())
        {
            // we don't trigger I/O on unknown source servers (which shouldn't exist in normal ops anyway)
            if (mFetcher[i].setsource(p.tempUrls[i], this, i))
            {
                // this kicks off I/O on that source
                if (i != mUnusedRaidConnection) mFetcher[i].trigger();
            }
        }
        else
        {
            mMissingSource = true;
        }
    }
}

RaidReq::~RaidReq()
{
    LOG_verbose << "[RaidReq::~RaidReq] DESTRUCTOR [this = " << this << "]";

    // Use feedlag to set next unused source
    uint8_t slowest, fastest;
    if (!mFaultysourceadded &&
        getSlowestAndFastestParts(slowest, fastest, false /* no need for the parts to be connected, just have feedlag */) &&
        differenceBetweenPartsSpeedIsSignificant(fastest, slowest))
    {
        LOG_verbose << "[RaidReq::~RaidReq] Detected slowest part for this RaidReq: " << (int)slowest << ". There's no sources with errors reported, so we will use this one as the unused connection for next RaidReq" << " [this = " << this << "]";
        mCloudRaid->setUnusedRaidConnection(slowest, true);
    }
}

void RaidReq::calculateNumLinesAndBufferSizes()
{
    // Calculate the number of lines based on the file size and RAIDLINE
    mNumLines = std::min<m_off_t>((mFilesize + RAIDLINE - 1) / RAIDLINE, MAX_NUMLINES);

    // Calculate the required size for data buffer (padded to RAIDLINE)
    mDataSize = mNumLines * RAIDLINE;

    // Calculate the required size for parity buffer (padded to RAIDSECTOR)
    mParitySize = static_cast<size_t>(SECTORSPERPART(mNumLines));

    assert(mNumLines >= 1);
    assert(mDataSize >= RAIDLINE);
    assert(mParitySize >= RAIDSECTOR);
}

void RaidReq::shiftdata(m_off_t len)
{
    mSkip += len;
    mRem -= len;

    if (mRem)
    {
        auto shiftby = mSkip / RAIDLINE;

        mCompleted -= shiftby;

        mSkip %= RAIDLINE;

        // we remove completed sectors/lines from the beginning of all state buffers
        m_off_t eobData = 0, eobAll = 0;
        for (uint8_t i = RAIDPARTS; i--; )
        {
            if (i > 0 && mPartpos[i] > eobData) eobData = mPartpos[i];
            if (mPartpos[i] > eobAll) eobAll = mPartpos[i];
        }
        eobData = (eobData + RAIDSECTOR - 1) / RAIDSECTOR;
        eobAll = (eobAll + RAIDSECTOR - 1) / RAIDSECTOR;

        if (eobData > shiftby)
        {
            memmove(mData.get(), mData.get() + (shiftby * RAIDLINE), (eobData - shiftby) * RAIDLINE);
        }

        if (eobAll > shiftby)
        {
            memmove(mInvalid.get(), mInvalid.get() + shiftby, eobAll - shiftby);
            std::fill(mInvalid.get() + eobAll - shiftby, mInvalid.get() + eobAll, (1 << RAIDPARTS) - 1);
        }
        else
        {
            std::fill(mInvalid.get(), mInvalid.get() + shiftby, (1 << RAIDPARTS) - 1);
        }

        mDataline += shiftby;
        shiftby *= RAIDSECTOR;

        if (mPartpos[0] > shiftby)
        {
            memmove(mParity.get(), mParity.get() + shiftby, mPartpos[0] - shiftby);
        }

        // shift mPartpos[] by the dataline increment and retrigger data flow
        for (uint8_t i = RAIDPARTS; i--; )
        {
            mPartpos[i] -= shiftby;

            if (mPartpos[i] < 0) mPartpos[i] = 0;
            else
            {
                if (!mFetcher[i].mFinished)
                {
                    // request 1 less RAIDSECTOR as we will add up to 1 sector of 0 bytes at the end of the file - this leaves enough buffer space in the buffer passed to procdata for us to write past the reported length
                    mFetcher[i].cont(SECTORSPERPART(mNumLines) - mPartpos[i]);
                }
            }
        }

        mHaddata = true;
        lastdata = Waiter::ds;
    }
}

bool RaidReq::allconnected(uint8_t excludedPart) const
{
    for (uint8_t i = RAIDPARTS; i--; )
    {
        if (i != excludedPart && !mFetcher[i].mConnected)
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
            if (!mFetcher[i].mFinished)
            {
                count++;
            }
    }
    return count;
}

uint8_t RaidReq::hangingSources(uint8_t* hangingSource = nullptr, uint8_t* idleGoodSource = nullptr)
{
    uint8_t numHangingSources = 0;
    if (hangingSource) *hangingSource = RAIDPARTS;
    if (idleGoodSource) *idleGoodSource = RAIDPARTS;

    for (uint8_t i = RAIDPARTS; i--; )
    {
        if (mFetcher[i].mConnected)
        {
            if (mHttpReqs[i]->status == REQ_INFLIGHT)
            {
                mFetcher[i].lastdata = mHttpReqs[i]->lastdata;
                if (mFetcher[i].mRemfeed && ((Waiter::ds - mFetcher[i].lastdata) > PartFetcher::LASTDATA_DSTIME_FOR_HANGING_SOURCE))
                {
                    LOG_verbose << "[RaidReq::hangingSources] Part " << (int)i << " last data time reached time boundary: considering it as a hanging source [lastDataTime = " << ((Waiter::ds - mFetcher[i].lastdata)/10) << " secs, limit = " << (PartFetcher::LASTDATA_DSTIME_FOR_HANGING_SOURCE/10) << " secs] [this = " << this << "]";
                    numHangingSources++;
                    if (hangingSource) *hangingSource = i;
                }
            }
        }
        else if (idleGoodSource && !mFetcher[i].mFinished && !mFetcher[i].mErrors) *idleGoodSource = i;
    }
    return numHangingSources;
}

// watchdog: resolve stuck connections
void RaidReq::watchdog()
{
    if (mMissingSource) return;

    // check for a single fast source hanging
    uint8_t hangingsource;
    uint8_t idlegoodsource;
    uint8_t hanging = hangingSources(&hangingsource, &idlegoodsource);

    if (hanging)
    {
        assert(hangingsource != RAIDPARTS);
        if (idlegoodsource == RAIDPARTS)
        {
            // Try a source with less errors:
            for (uint8_t i = RAIDPARTS; i--; )
            {
                if (!mFetcher[i].mConnected &&
                    !mFetcher[i].mFinished &&
                    (mFetcher[i].mErrors <= MAX_ERRORS_FOR_IDLE_GOOD_SOURCE ||
                            (idlegoodsource != RAIDPARTS && mFetcher[i].mErrors < mFetcher[idlegoodsource].mErrors)))
                    idlegoodsource = i;
            }
        }
        if (idlegoodsource != RAIDPARTS)
        {
            LOG_verbose << "Hanging source!! hangingsource = " << (int)hangingsource << " (HttpReq: " << (void*)mHttpReqs[hangingsource].get() << "), idlegoodsource = " << (int)idlegoodsource << " (HttpReq: " << (void*)mHttpReqs[idlegoodsource].get() << ") [mFetcher[hangingsource].lastdata = " << mFetcher[hangingsource].lastdata << ", Waiter::ds = " << Waiter::ds << "] [this = " << this << "]";
            mFetcher[hangingsource].mErrors++;
            setNewUnusedRaidConnection(hangingsource);
            mFetcher[hangingsource].closesocket();
            if (mFetcher[idlegoodsource].trigger() == -1)
            {
                mFetcher[hangingsource].trigger();
            }
            return;
        }
        else
        {
            LOG_verbose << "Hanging source and no idle good source to switch!! hangingsource = " << (int)hangingsource << " (HttpReq: " << (void*)mHttpReqs[hangingsource].get() << ") [mFetcher[hangingsource].lastdata = " << mFetcher[hangingsource].lastdata << ", Waiter::ds = " << Waiter::ds << "] [this = " << this << "]";
        }
    }
}

bool RaidReq::differenceBetweenPartsSpeedIsSignificant(uint8_t part1, uint8_t part2) const
{
    return mFeedlag[part1] * 4 > mFeedlag[part2] * 5;
}

bool RaidReq::getSlowestAndFastestParts(uint8_t& slowest, uint8_t& fastest, bool mustBeConnected) const
{
    slowest = 0;
    fastest = 0;

    uint8_t i = RAIDPARTS;
    while (i-- > 0 &&
        ((mustBeConnected && !mFetcher[slowest].mConnected) || !mFeedlag[slowest]))
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
        if ((mFetcher[i].mConnected || !mustBeConnected) &&
            mFeedlag[i])
        {
            if (mFeedlag[i] < mFeedlag[slowest])
                slowest = i;
            else if (mFeedlag[i] > mFeedlag[fastest])
                fastest = i;
        }
    }
    return true;
}

// procdata() handles input in any order/size and will push excess data to readahead
// data is assumed to be 0-padded to paddedpartsize at EOF
void RaidReq::procdata(uint8_t part, byte* ptr, m_off_t pos, m_off_t len)
{
    m_off_t basepos = mDataline * RAIDSECTOR;

    // we never read backwards
    assert((pos & -RAIDSECTOR) >= (basepos + (mPartpos[part] & -RAIDSECTOR)));

    bool consecutive = pos == basepos + mPartpos[part];

    // is the data non-consecutive (i.e. a readahead), OR is it extending past the end of our buffer?
    if (!consecutive || ((pos + len) > (basepos + SECTORSPERPART(mNumLines))))
    {
        auto ahead_ptr = ptr;
        auto ahead_pos = pos;
        auto ahead_len = len;

        // if this is a consecutive feed, we store the overflowing part as readahead data
        // and process the non-overflowing part normally
        if (consecutive)
        {
            ahead_pos = basepos + SECTORSPERPART(mNumLines);
            ahead_ptr = ptr + (ahead_pos - pos);
            ahead_len = len - (ahead_pos - pos);
        }

        // enqueue for future processing
        auto itReadAhead = mFetcher[part].mReadahead.find(ahead_pos);
        if (itReadAhead == mFetcher[part].mReadahead.end() || itReadAhead->second.second < ahead_len)
        {
            byte* p = itReadAhead != mFetcher[part].mReadahead.end() ?
                            static_cast<byte*>(std::realloc(itReadAhead->second.first, ahead_len)) :
                            static_cast<byte*>(malloc(ahead_len));
            std::copy(ahead_ptr, ahead_ptr + ahead_len, p);
            mFetcher[part].mReadahead[ahead_pos] = pair<byte*, unsigned>(p, static_cast<unsigned>(ahead_len));
        }
        // if this is a pure readahead, we're done
        if (!consecutive) return;

        len = ahead_pos - pos;
    }

    // non-readahead data must flow contiguously
    assert(pos == mPartpos[part] + (mDataline * RAIDSECTOR));

    mPartpos[part] += len;

    auto t = pos - (mDataline * RAIDSECTOR);

    // ascertain absence of overflow (also covers parity part)
    assert((t + len) <= static_cast<m_off_t>(mDataSize / EFFECTIVE_RAIDPARTS));

    // set valid bit for every block that's been received in full
    char partmask = static_cast<char>(1 << part);
    m_off_t until = (t + len) / RAIDSECTOR; // Number of sectors - corresponding to the number of lines for this part (for each part, N_RAIDSECTORS == N_RAIDLINES)
    for (m_off_t i = t / RAIDSECTOR; i < until; i++)
    {
        assert(mInvalid[i] & partmask);
        mInvalid[i] -= partmask;
    }

    // copy (partial) blocks to data or parity buf
    if (part)
    {
        part--;

        byte* ptr2 = ptr;
        m_off_t len2 = len;
        byte* target = mData.get() + part * RAIDSECTOR + (t / RAIDSECTOR) * RAIDLINE;
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
            target += RAIDSECTOR * EFFECTIVE_RAIDPARTS;
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
        std::copy(ptr, ptr + len, mParity.get() + t);
    }

    // merge new consecutive completed RAID lines so they are ready to be sent, direct from the data[] array
    auto old_completed = mCompleted;
    for (; mCompleted < until; mCompleted++)
    {
        unsigned char mask = mInvalid[mCompleted];
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
                    auto sectors = reinterpret_cast<raidsector_t*>(mData.get() + (RAIDLINE * mCompleted));
                    raidsector_t& target = sectors[index - 1];

                    target = reinterpret_cast<raidsector_t*>(mParity.get())[mCompleted];
                    if (!(mask & (1 << 1))) target ^= sectors[0]; // this method requires source and target are both aligned to their size
                    if (!(mask & (1 << 2))) target ^= sectors[1];
                    if (!(mask & (1 << 3))) target ^= sectors[2];
                    if (!(mask & (1 << 4))) target ^= sectors[3];
                    if (!(mask & (1 << 5))) target ^= sectors[4];
                }
            }
        }
    }

    if (mCompleted > old_completed)
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
        if (mCompleted < mNumLines)
        {
            old_completed = mCompleted;
            procreadahead();
        }
        else
        {
            old_completed = 0;
        }
        new_completed = mCompleted;
        t = (mCompleted * RAIDLINE) - mSkip;


        if (t > 0)
        {
            if ((t + lenCompleted) > len) t = len - lenCompleted;
            memmove(buf + lenCompleted, mData.get() + mSkip, t);
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
                        LOG_warn << "CloudRAID feed timed out [lastDataTime = " << (lastDataOffset/10) << " secs, haddata = " << mHaddata << "] [hangingSources = " << (int)hanging << "] [this = " << this << "]";
                        return -1;
                    }
                    if (!mReported)
                    {
                        mReported = true;
                        LOG_warn << "CloudRAID feed stuck [lastDataTime = " << (lastDataOffset/10) << " secs, haddata = " << mHaddata << "] [hangingSources = " << (int)hanging << "] [this = " << this << "]";
                    }
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
    if (mRem)
    {
        for (uint8_t i = RAIDPARTS; i--; )
        {
            if (i != excludedPart)
            {
                if (mFetcher[i].mFinished)
                {
                    mFetcher[i].directTrigger();
                }
                else if (mFetcher[i].mConnected)
                {
                    mFetcher[i].resume();
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
        if (mHttpReqs[i] == httpReq)
        {
            int64_t t = mFetcher[i].io();

            if (t > 0)
            {
                // this is a relatively infrequent ocurrence, so we tolerate the overhead of a std::set insertion/erasure
                mPool.addScheduledio(Waiter::ds + t, mHttpReqs[i]);
            }
            break;
        }
    }
}

// feed relevant read-ahead data to procdata
void RaidReq::procreadahead()
{
    bool fed;

    do {
        fed = false;

        for (uint8_t i = RAIDPARTS; i--; )
        {
            if (mFetcher[i].feedreadahead()) fed = true;
        }
    } while (fed);
}

void RaidReq::disconnect()
{
    for (auto& httpReq : mHttpReqs)
    {
        httpReq->disconnect();
    }
}

uint8_t RaidReq::processFeedLag()
{
    uint8_t laggedPart = RAIDPARTS;
    if (++mLagrounds >= numPartsUnfinished())
    {
        // dominance is defined as the ratio between fastest and slowest
        uint8_t slowest, fastest;
        if (!getSlowestAndFastestParts(slowest, fastest)) // Cannot compare yet
        {
            return RAIDPARTS;
        }

        if (!mMissingSource && mFetcher[slowest].mConnected && !mFetcher[slowest].mFinished && mHttpReqs[slowest]->status != REQ_SUCCESS &&
            ((mFetcher[slowest].mRem - mFetcher[fastest].mRem) > ((SECTORSPERPART(mNumLines) * LAGINTERVAL * 3) / 4) || differenceBetweenPartsSpeedIsSignificant(fastest, slowest)))
        {
            // slow channel detected
            {
                LOG_debug << "Slow channel " << (int)slowest << " (" << (void*)mHttpReqs[slowest].get() << ")" << " detected!" << " [this = " << this << "]";
                mFetcher[slowest].mErrors++;

                // check if we have a fresh and idle channel left to try
                uint8_t fresh = 0;
                while (fresh <= EFFECTIVE_RAIDPARTS && (mFetcher[fresh].mConnected || mFetcher[fresh].mErrors || mFetcher[fresh].mFinished))
                {
                    fresh++;
                }
                if (fresh <= EFFECTIVE_RAIDPARTS)
                {
                    LOG_verbose << "New fresh channel: " << (int)fresh << " (" << (void*)mHttpReqs[fresh].get() << ")" << " [this = " << this << "]";
                    setNewUnusedRaidConnection(slowest);
                    mFetcher[slowest].closesocket();
                    mFetcher[fresh].resume(true);
                    laggedPart = slowest;
                }
                else { LOG_verbose << "No fresh channel to switch with the slow channel" << " [this = " << this << "]"; }
            }
        }

        if (laggedPart != RAIDPARTS)
        {
            std::fill(mFeedlag.begin(), mFeedlag.end(), 0);
        }
        mLagrounds = 0;
    }
    return laggedPart;
}

m_off_t RaidReq::progress() const
{
    m_off_t progressCount = 0;
    for (uint8_t i = RAIDPARTS; i--; )
    {
        m_off_t partProgress = mFetcher[i].progress();
        progressCount += partProgress;
    }
    assert((mCompleted * RAIDLINE) - mSkip >= 0);
    progressCount += ((mCompleted * RAIDLINE) - mSkip);
    assert(progressCount >= 0);
    assert(progressCount <= static_cast<m_off_t>(mFilesize));
    return progressCount;
}

uint8_t RaidReq::unusedPart() const
{
    uint8_t partIndex = RAIDPARTS;
    for (uint8_t i = RAIDPARTS; i--;)
    {
        if (!mFetcher[i].mConnected && !mFetcher[i].mFinished && !mPool.lookupHttpReq(mHttpReqs[i]))
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
    return mCloudRaid->checkTransferFailure();
}

bool RaidReq::setNewUnusedRaidConnection(uint8_t part, bool addToFaultyServers)
{
    if (!mCloudRaid->setUnusedRaidConnection(part, addToFaultyServers))
    {
        LOG_warn << "[RaidReq::setNewUnusedRaidConnection] Could not set unused raid connection, setting it to 0" << " [this = " << this << "]";
        assert(false && "Unused raid connection couldn't be set");
        mUnusedRaidConnection = 0;
        return false;
    }

    LOG_verbose << "[RaidReq::setNewUnusedRaidConnection] Set unused raid connection to " << (int)part << " (clear previous unused connection: " << (int)mUnusedRaidConnection << ") [addToFaultyServers = " << addToFaultyServers << "]" << " [this = " << this << "]";
    mUnusedRaidConnection = part;
    if (addToFaultyServers) mFaultysourceadded = true;
    return true;
}

void RaidReq::processRequestLatency(const HttpReqPtr& httpReq)
{
    if (httpReq)
    {
        mCloudRaid->processRequestLatency(httpReq);
    }
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

    LOG_debug << "[RaidReq::raidPartSize] return (fullfilesize - r) / EFFECTIVE_RAIDPARTS + t = (" << fullfilesize << " -  " << r << ") / (" << EFFECTIVE_RAIDPARTS << ") + " << t << " = " << ((fullfilesize - r) / EFFECTIVE_RAIDPARTS) << " + " << t << " = " << ((fullfilesize - r) / EFFECTIVE_RAIDPARTS + t);

    return (fullfilesize - r) / EFFECTIVE_RAIDPARTS + t;
}

/* -------------- RaidReq END --------------*/


/* -------------- RaidReqPool --------------*/

bool RaidReqPool::addScheduledio(raidTime scheduledFor, const HttpReqPtr& req)
{
    auto it = mSetHttpReqs.insert(req);
    if (it.second)
    {
        auto itScheduled = mScheduledio.insert(std::make_pair(scheduledFor, req));
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
    return mSetHttpReqs.erase(req) > 0;
}

void RaidReqPool::raidproxyio()
{
    if (mIsRunning)
    {
        if (!mScheduledio.empty())
        {
            auto itScheduled = mScheduledio.begin();
            bool transferFailed = false;
            while (mIsRunning && !transferFailed && (itScheduled != mScheduledio.end() && (itScheduled->first <= Waiter::ds)))
            {
                const HttpReqPtr& httpReq = itScheduled->second;
                if (httpReq->status != REQ_INFLIGHT)
                {
                    if ((mSetHttpReqs.find(httpReq) != mSetHttpReqs.end()))
                    {
                        mRaidReq->dispatchio(httpReq);
                        itScheduled++;
                    }
                    else
                    {
                        itScheduled = mScheduledio.erase(itScheduled);
                    }
                }
                else
                {
                    mRaidReq->processRequestLatency(httpReq);
                    itScheduled++;
                }
                if (mRaidReq->checkTransferFailure().first)
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
    mIsRunning = false;
    mRaidReq.reset();
}

void RaidReqPool::request(const RaidReq::Params& p, const std::shared_ptr<CloudRaid>& cloudRaid)
{
    RaidReq* rr = new RaidReq(p, *this, cloudRaid);
    mRaidReq.reset(rr);
}

/* -------------- RaidReqPool END --------------*/
