/**
 * @file mega/raid.cpp
 * @brief helper classes for managing cloudraid downloads
 *
 * (c) 2013-2019 by Mega Limited, Auckland, New Zealand
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

#include "mega/raid.h"

#include "mega/transfer.h"
#include "mega/testhooks.h"
#include "mega.h" // for thread definitions
#include "mega/raidproxy.h"

#undef min //avoids issues with std::min

namespace mega
{

const unsigned RAID_ACTIVE_CHANNEL_FAIL_THRESHOLD = 5;

struct FaultyServers
{
    // Records URLs that had recent problems, so we can start the next raid download with URLs that can work first try.
    // In particular this is useful when one server in a raid set is unavailable for an extended period
    // This class may be shared amongst many megaclients, so thread safety is needed
    typedef map<string, m_time_t> Map;
    Map recentFails;
    std::mutex m_mutex;

    string server(const string& url)
    {
        size_t n = url.find("://");
        if (n != string::npos)
        {
            n += 3;
            size_t m = url.find("/", n);
            if (m != string::npos)
            {
                return url.substr(n, m - n);
            }
        }
        return "";
    }

    void add(const string& url)
    {
        std::lock_guard<std::mutex> g(m_mutex);
        recentFails[server(url)] = m_time();
    }

    /**
     * @brief Select the worst server based on records of recent failures
     * @param urls The set of URLs to check against previosly failing servers
     * @return The index from 0 to 5, or 6 (RAIDPARTS) if none of the URLs have failed recently.
     */
    unsigned selectWorstServer(vector<string> urls)
    {
        // start with 6 connections and drop the slowest to respond, build the file from the other 5.
        // (unless we recently had problems with the server of one of the 6 URLs, in which case start with the other 5 right away)
        unsigned worstindex = RAIDPARTS;

        std::lock_guard<std::mutex> g(m_mutex);
        if (!recentFails.empty())
        {
            m_time_t now = m_time();
            m_time_t worsttime = now - 10 * 3600;   // 10 hours
            for (auto i = unsigned(urls.size()); i--; )
            {
                Map::iterator j = recentFails.find(server(urls[i]));
                if (j != recentFails.end() && j->second > worsttime)
                {
                    // select URL that failed less than 10 hours ago
                    worstindex = i;
                    worsttime = j->second;
                }
            }

            // cleanup recentFails from URLs older than 10 hours
            bool cleanup = false;
            Map::iterator jj;
            for (Map::iterator j = recentFails.begin(); j != recentFails.end(); cleanup ? (jj = j, ++j, (void)recentFails.erase(jj)) : (void)++j)
            {
                cleanup = j->second < (now - 10 * 3600);
            }
        }

        return worstindex;
    }

};

FaultyServers g_faultyServers;


RaidBufferManager::FilePiece::FilePiece()
    : pos(0)
    , buf(NULL, 0, 0)
{
}

RaidBufferManager::FilePiece::FilePiece(m_off_t p, size_t len)
    : pos(p)
    , buf(new byte[len + std::min<size_t>(SymmCipher::BLOCKSIZE, RAIDSECTOR)], 0, len)   // SymmCipher::ctr_crypt requirement: decryption: data must be padded to BLOCKSIZE.  Also make sure we can xor up to RAIDSECTOR more for convenience
{
}


RaidBufferManager::FilePiece::FilePiece(m_off_t p, HttpReq::http_buf_t* b) // taking ownership
    : pos(p)
    , buf(NULL, 0, 0)
{
    buf.swap(*b);  // take its buffer and copy other members
    delete b;  // client no longer owns it so we must delete.  Similar to move semantics where we would just assign
}

void RaidBufferManager::FilePiece::swap(FilePiece& other)
{
    m_off_t tp = pos; pos = other.pos; other.pos = tp;
    chunkmacs.swap(other.chunkmacs);
    buf.swap(other.buf);
}

RaidBufferManager::RaidBufferManager()
    : is_raid(false)
    , is_newRaid(false)
    , raidKnown(false)
    , raidLinesPerChunk(16 * 1024)
    , unusedRaidConnection(0)
    , raidpartspos(0)
    , outputfilepos(0)
    , startfilepos(0)
    , resumewastedbytes(0)
    , mDisableAvoidSmallLastRequest(false)
{
    for (int i = RAIDPARTS; i--; )
    {
        raidrequestpartpos[i] = 0;
        connectionPaused[i] = false;
        raidHttpGetErrorCount[i] = 0;
        connectionStarted[i] = false;
    }
}

static void clearOwningFilePieces(std::deque<RaidBufferManager::FilePiece*>& q)
{
    for (std::deque<RaidBufferManager::FilePiece*>::iterator i = q.begin(); i != q.end(); ++i)
    {
        delete *i;
    }
    q.clear();
}

RaidBufferManager::~RaidBufferManager()
{
    for (int i = RAIDPARTS; i--; )
    {
        clearOwningFilePieces(raidinputparts[i]);
    }
}

void RaidBufferManager::setIsRaid(const std::vector<std::string>& tempUrls, m_off_t resumepos, m_off_t readtopos, m_off_t filesize, m_off_t maxRequestSize, bool isNewRaid)
{
    assert(tempUrls.size() == RAIDPARTS || tempUrls.size() == 1);
    assert(0 <= resumepos && resumepos <= readtopos && readtopos <= filesize);
    assert(!raidKnown);

    tempurls = tempUrls;

    if (tempurls.size() == RAIDPARTS)
    {
        if (isNewRaid)
        {
            is_newRaid = true;
        }
        else
        {
            is_raid = true;
        }
    }
    raidKnown = true;
    fullfilesize = filesize;
    deliverlimitpos = readtopos;
    acquirelimitpos = deliverlimitpos + RAIDLINE - 1;
    acquirelimitpos -= acquirelimitpos % RAIDLINE;
    acquirelimitpos = std::min<m_off_t>(acquirelimitpos, fullfilesize);
    outputfilepos = resumepos;
    startfilepos = resumepos;
    if (is_raid)
    {
        raidpartspos = resumepos / EFFECTIVE_RAIDPARTS;
        raidpartspos -= raidpartspos % RAIDSECTOR;
        resumewastedbytes = size_t(outputfilepos - raidpartspos * EFFECTIVE_RAIDPARTS);
        outputfilepos -= resumewastedbytes; // we'll skip over these bytes on the first output
        for (int i = RAIDPARTS; i--;)
        {
            raidrequestpartpos[i] = raidpartspos;
        }

        // How much buffer space can we use.  Assuming two chunk sets incoming, one outgoing
        raidLinesPerChunk = static_cast<unsigned>(maxRequestSize / (RAIDPARTS * 3 * RAIDSECTOR));
        raidLinesPerChunk -= raidLinesPerChunk % 1024;
        raidLinesPerChunk = std::min<unsigned>(raidLinesPerChunk, 256 * 1024); // max 256K * RAIDSECTOR * 5
        raidLinesPerChunk = std::max<unsigned>(raidLinesPerChunk, 64 * 1024); // min 64K * RAIDSECTOR * 5

        unusedRaidConnection = g_faultyServers.selectWorstServer(tempurls);
        LOG_debug << "[RaidBufferManager::setIsRaid] unusedRaidConnection = " << unusedRaidConnection;
    }

    DEBUG_TEST_HOOK_RAIDBUFFERMANAGER_SETISRAID(this)
}

void RaidBufferManager::updateUrlsAndResetPos(const std::vector<std::string>& tempUrls)
{
    // A request to restart from whereever we got to, with new URLs.
    // the old requested-to pos is not valid anymore, as one or more Http requests failed or were abandoned
    assert(tempurls.size() == tempUrls.size());
    if (tempurls.size() == tempUrls.size())
    {
        tempurls = tempUrls;
        if (isRaid())
        {
            for (unsigned i = RAIDPARTS; i--; )
            {
                std::deque<FilePiece*>& connectionpieces = raidinputparts[i];
                transferPos(i) =
                    connectionpieces.empty() ?
                        raidpartspos :
                        connectionpieces.back()->pos +
                            static_cast<m_off_t>(connectionpieces.back()->buf.datalen());
            }
        }
        else
        {
            transferPos(0) = outputfilepos;  // if there is any data waiting in asyncoutputbuffers this value is alreday ahead of it
        }
    }
}

void RaidBufferManager::disableAvoidSmallLastRequest()
{
    mDisableAvoidSmallLastRequest = true;
}

bool RaidBufferManager::isRaid() const
{
    assert(raidKnown);
    return is_raid;
}

bool RaidBufferManager::isNewRaid() const
{
    assert(raidKnown);
    return is_newRaid;
}

bool RaidBufferManager::isUnusedRaidConection(unsigned connectionNum) const
{
    return connectionNum == unusedRaidConnection;
}

bool RaidBufferManager::isRaidConnectionProgressBlocked(unsigned connectionNum) const
{
    return connectionPaused[connectionNum];
}

const std::string& RaidBufferManager::tempURL(unsigned connectionNum)
{
    if (isRaid())
    {
        assert(connectionNum < tempurls.size());
        return tempurls[connectionNum];
    }
    else if (!tempurls.empty())
    {
        return tempurls[0];
    }
    else
    {
        assert(false); // this class shouldn't be used until we have the URLs, but don't crash
        return emptyReturnString;
    }
}

const std::vector<std::string>& RaidBufferManager::tempUrlVector() const
{
    return tempurls;
}

// takes ownership of the buffer
void RaidBufferManager::submitBuffer(unsigned connectionNum, FilePiece* piece)
{
    if (isRaid())
    {
        assert(connectionNum < RAIDPARTS);
        assert(piece->buf.datalen() % RAIDSECTOR == 0 || piece->pos + m_off_t(piece->buf.datalen()) == raidPartSize(connectionNum, acquirelimitpos));
        if (!piece->buf.isNull())
        {
            raidHttpGetErrorCount[connectionNum] = 0;
        }

        std::deque<FilePiece*>& connectionpieces = raidinputparts[connectionNum];
        m_off_t contiguouspos =
            connectionpieces.empty() ?
                raidpartspos :
                connectionpieces.back()->pos +
                    static_cast<m_off_t>(connectionpieces.back()->buf.datalen());

        assert(piece->pos == contiguouspos);
        if (piece->pos == contiguouspos)
        {
            // in case of download piece arriving after connection failure recovery
            transferPos(connectionNum) = piece->pos + static_cast<m_off_t>(piece->buf.datalen());
            raidinputparts[connectionNum].push_back(piece);
        }
    }
    else
    {
        finalize(*piece);
        assert(asyncoutputbuffers.find(connectionNum) == asyncoutputbuffers.end() || !asyncoutputbuffers[connectionNum]);
        asyncoutputbuffers[connectionNum].reset(piece);
    }
}

std::shared_ptr<RaidBufferManager::FilePiece> RaidBufferManager::getAsyncOutputBufferPointer(unsigned connectionNum)
{
    auto i = asyncoutputbuffers.find(connectionNum);
    if (isRaid() && (i == asyncoutputbuffers.end() || !i->second))
    {
        combineRaidParts(connectionNum);
        i = asyncoutputbuffers.find(connectionNum);
    }
    return (i == asyncoutputbuffers.end()) ? NULL : i->second;
}


void RaidBufferManager::bufferWriteCompleted(unsigned connectionNum, bool success)
{
    auto aob = asyncoutputbuffers.find(connectionNum);
    if (aob != asyncoutputbuffers.end())
    {
        assert(aob->second);
        if (aob->second)
        {
            if (success)
            {
                bufferWriteCompletedAction(*aob->second);
            }

            aob->second.reset();
        }
    }
}

void RaidBufferManager::bufferWriteCompletedAction(FilePiece&)
{
    // overridden for Transfers
}

m_off_t& RaidBufferManager::transferPos(unsigned connectionNum)
{
    assert(isRaid());
    return raidrequestpartpos[connectionNum];
}


std::pair<m_off_t, m_off_t> RaidBufferManager::nextNPosForConnection(unsigned connectionNum, bool& newInputBufferSupplied, bool& pauseConnectionForRaid)
{
    // returning a pair for clarity - specifying the beginning and end position of the next data block, as the 'current pos' may be updated during this function
    newInputBufferSupplied = false;
    pauseConnectionForRaid = false;

    if (!isRaid())
    {
        return std::make_pair(transferPos(connectionNum), deliverlimitpos);  // simple case for non-raid direct streaming, get the entire portion of the file requested in one http get
    }
    else  // raid
    {
        m_off_t curpos = transferPos(connectionNum);  // if we use submitBuffer, transferPos() may be updated to protect against single connection failure recovery
        m_off_t maxpos = transferSize(connectionNum);

        // if this connection gets too far ahead of the others, pause it until the others catch up a bit
        if ((curpos >= raidpartspos + RaidReadAheadChunksPausePoint * raidLinesPerChunk * RAIDSECTOR) ||
            (curpos > raidpartspos + RaidReadAheadChunksUnpausePoint * raidLinesPerChunk * RAIDSECTOR && connectionPaused[connectionNum]))
        {
            connectionPaused[connectionNum] = true;
            pauseConnectionForRaid = true;
            return std::make_pair(curpos, curpos);
        }
        else
        {
            connectionPaused[connectionNum] = false;
        }

        m_off_t npos = std::min<m_off_t>(curpos + raidLinesPerChunk * RAIDSECTOR * RaidMaxChunksPerRead, maxpos);
        size_t nextChunkSize = (curpos < npos) ?
                                static_cast<size_t>(npos - curpos) :
                                0;
        LOG_debug << "Raid lines per chunk = " << raidLinesPerChunk << ", curpos = " << curpos << ", npos = " << npos << ", maxpos = " << maxpos << ", acquirelimitpos = " << acquirelimitpos << ", nextChunkSize = " << nextChunkSize;
        if (!mDisableAvoidSmallLastRequest)
        {
            size_t lastChunkSize = (npos < maxpos) ?             // Last chunk left apart from the current chunk
                                    static_cast<size_t>(maxpos - npos) :
                                    0;
            if (lastChunkSize && (lastChunkSize < MIN_LAST_CHUNK)) // Dont leave a last chunk smaller than MIN_LAST_CHUNK for the last request
            {
                // If this chunk and the last one are greater or equal than +16 MB (or the corresponding value for MAX_CHUNK_SIZE), we'll ask for two chunks of +8 MB.
                // Otherwise, we'll request the remaining: -15 MB
                size_t remainingSize = static_cast<size_t>(
                    maxpos - curpos); // Remaining size (current chunk + last chunk)
                npos = (remainingSize >=
                        MAX_LAST_CHUNK) ? // If the remaining size (current chunk + last chunk) is
                                          // greater than MAX_LAST_CHUNK
                           (curpos +
                            static_cast<m_off_t>((remainingSize / 2) &
                                                 ~(static_cast<size_t>(RAIDSECTOR) -
                                                   1))) : // Npos moved to half of the remaining
                                                          // size (truncated to RAIDSECTOR)
                                                          maxpos; // Npos moved to the end
                assert(npos <= maxpos);
                LOG_debug << "Avoiding small last request (" << lastChunkSize << "), change npos to " << npos << ", new nextChunkSize = " << (npos - curpos);
            }
        }
        if (unusedRaidConnection == connectionNum && npos > curpos)
        {
            submitBuffer(connectionNum, new RaidBufferManager::FilePiece(curpos, new HttpReq::http_buf_t(NULL, 0, size_t(npos - curpos))));
            transferPos(connectionNum) = npos;
            newInputBufferSupplied = true;
        }
        return std::make_pair(curpos, npos);
    }
}

void RaidBufferManager::resetPart(unsigned connectionNum)
{
    assert(isRaid());
    transferPos(connectionNum) = raidpartspos;

    // if we are downloading many files at once, eg. initial sync, or large manual folder, it's better to just use 5 connections immediately after the first
    g_faultyServers.add(tempurls[connectionNum]);
}


m_off_t RaidBufferManager::transferSize(unsigned connectionNum)
{
    if (isRaid())
    {
        return raidPartSize(connectionNum, acquirelimitpos);
    }
    else
    {
        return fullfilesize;
    }
}

m_off_t RaidBufferManager::raidPartSize(unsigned part, m_off_t filesize)
{
    // compute the size of this raid part based on the original file size len
    m_off_t r = filesize % RAIDLINE;   // residual part

    m_off_t t = r - (part - !!part)*RAIDSECTOR; // parts 0 (parity) & 1 (largest data) are the same size

    // (excess length will be found in the following sectors,
    // negative length in the preceding sectors)
    if (t < 0)
    {
        t = 0;
    }
    else if (t > RAIDSECTOR)
    {
        t = RAIDSECTOR;
    }

    return (filesize - r) / EFFECTIVE_RAIDPARTS + t;
}

m_off_t RaidBufferManager::offsetToRaidLine(const m_off_t offset)
{
    return offset % RAIDLINE;
}

void RaidBufferManager::combineRaidParts(unsigned connectionNum)
{
    assert(asyncoutputbuffers.find(connectionNum) == asyncoutputbuffers.end() || !asyncoutputbuffers[connectionNum]);
    assert(raidpartspos * EFFECTIVE_RAIDPARTS == outputfilepos + m_off_t(leftoverchunk.buf.datalen()));

    size_t partslen = 0x10000000, sumdatalen = 0, xorlen = 0;
    for (unsigned i = RAIDPARTS; i--; )
    {
        if (raidinputparts[i].empty())
        {
            partslen = 0;
        }
        else
        {
            FilePiece& r = *raidinputparts[i].front();
            assert(r.pos == raidpartspos);  // check all are in sync at the front
            partslen = std::min<size_t>(partslen, r.buf.datalen());
            (i > 0 ? sumdatalen : xorlen) += r.buf.datalen();
        }
    }
    partslen -= partslen % RAIDSECTOR; // restrict to raidline boundary

    // for correct mac processing, we need to process the output file in pieces delimited by the chunkfloor / chunkceil algorithm
    m_off_t newdatafilepos = outputfilepos + static_cast<m_off_t>(leftoverchunk.buf.datalen());
    assert(newdatafilepos + m_off_t(sumdatalen) <= acquirelimitpos);
    bool processToEnd =  (newdatafilepos + m_off_t(sumdatalen) == acquirelimitpos)   // data to the end
              &&  (newdatafilepos / EFFECTIVE_RAIDPARTS + m_off_t(xorlen) == raidPartSize(0, acquirelimitpos));  // parity to the end

    assert(!partslen || !processToEnd || sumdatalen - partslen * EFFECTIVE_RAIDPARTS <= RAIDLINE);

    if (partslen > 0 || processToEnd)
    {
        m_off_t macchunkpos = calcOutputChunkPos(newdatafilepos + static_cast<m_off_t>(partslen) *
                                                                      EFFECTIVE_RAIDPARTS);

        size_t buflen = static_cast<size_t>(processToEnd ? sumdatalen : partslen * EFFECTIVE_RAIDPARTS);
        LOG_debug << "Combining raid parts -> partslen = " << partslen << ", buflen = " << buflen << ", outputfilepos = " << outputfilepos << ", leftoverchunk = " << leftoverchunk.buf.datalen();
        FilePiece* outputrec = combineRaidParts(partslen, buflen, outputfilepos, leftoverchunk);  // includes a bit of extra space for non-full sectors if we are at the end of the file
        rollInputBuffers(partslen);
        raidpartspos += partslen;
        sumdatalen -= partslen * EFFECTIVE_RAIDPARTS;
        outputfilepos += partslen * EFFECTIVE_RAIDPARTS + leftoverchunk.buf.datalen();
        byte* dest = outputrec->buf.datastart() + partslen * EFFECTIVE_RAIDPARTS + leftoverchunk.buf.datalen();
        FilePiece emptyFilePiece;
        leftoverchunk.swap(emptyFilePiece);  // this data is entirely included in the outputrec now, so discard and reset

        if (processToEnd && sumdatalen > 0)
        {
            // fill in the last of the buffer with non-full sectors from the end of the file
            assert(outputfilepos + m_off_t(sumdatalen) == acquirelimitpos);
            combineLastRaidLine(dest, sumdatalen);
            rollInputBuffers(RAIDSECTOR);
        }
        else if (!processToEnd && outputfilepos > macchunkpos)
        {
            // for transfers we do mac processing which must be done in chunks, delimited by chunkfloor and chunkceil.  If we don't have the right amount then hold the remainder over for next time.
            size_t excessdata = static_cast<size_t>(outputfilepos - macchunkpos);
            FilePiece newleftover(outputfilepos - static_cast<m_off_t>(excessdata), excessdata);
            leftoverchunk.swap(newleftover);
            memcpy(leftoverchunk.buf.datastart(), outputrec->buf.datastart() + outputrec->buf.datalen() - excessdata, excessdata);
            outputrec->buf.end -= excessdata;
            outputfilepos -= excessdata;
            assert(raidpartspos * EFFECTIVE_RAIDPARTS == outputfilepos + m_off_t(leftoverchunk.buf.datalen()));
        }

        // discard any excess data that we had to fetch when resuming a file (to align the parts appropriately)
        size_t n = std::min<size_t>(outputrec->buf.datalen(), resumewastedbytes);
        if (n > 0)
        {
            outputrec->pos += n;
            outputrec->buf.start += n;
            resumewastedbytes -= n;
        }

        // don't deliver any excess data that we needed for parity calculations in the last raid line
        if (outputrec->pos + m_off_t(outputrec->buf.datalen()) > deliverlimitpos)
        {
            size_t excess = static_cast<size_t>(outputrec->pos) + outputrec->buf.datalen() -
                            static_cast<size_t>(deliverlimitpos);
            excess = std::min<size_t>(excess, outputrec->buf.datalen());
            outputrec->buf.end -= excess;
        }

        // store the result in a place that can be read out async
        if (outputrec->buf.datalen() > 0)
        {
            finalize(*outputrec);
            asyncoutputbuffers[connectionNum].reset(outputrec);
        }
        else
        {
            delete outputrec;  // this would happen if we got some data to process on all connections, but not enough to reach the next chunk boundary yet (and combined data is in leftoverchunk)
        }
    }
}

RaidBufferManager::FilePiece* RaidBufferManager::combineRaidParts(size_t partslen, size_t bufflen, m_off_t filepos, FilePiece& prevleftoverchunk)
{
    assert(prevleftoverchunk.buf.datalen() == 0 || prevleftoverchunk.pos == filepos);

    // add a bit of extra space and copy prev chunk to the front
    FilePiece* result = new FilePiece(filepos, bufflen + prevleftoverchunk.buf.datalen());
    if (prevleftoverchunk.buf.datalen() > 0)
    {
        memcpy(result->buf.datastart(), prevleftoverchunk.buf.datastart(), prevleftoverchunk.buf.datalen());
    }

    // usual case, for simple and fast processing: all input buffers are the same size, and aligned, and a multiple of raidsector
    if (partslen > 0)
    {
        byte* inputbufs[RAIDPARTS];
        for (unsigned i = RAIDPARTS; i--; )
        {
            FilePiece* inputPiece = raidinputparts[i].front();
            inputbufs[i] = inputPiece->buf.isNull() ? NULL : inputPiece->buf.datastart();
        }

        byte* b = result->buf.datastart() + prevleftoverchunk.buf.datalen();
        byte* endpos = b + partslen * EFFECTIVE_RAIDPARTS;

        for (unsigned i = 0; b < endpos; i += RAIDSECTOR)
        {
            for (unsigned j = 1; j < RAIDPARTS; ++j)
            {
                assert(b + RAIDSECTOR <= result->buf.datastart() + result->buf.datalen());
                if (inputbufs[j])
                {
                    memcpy(b, inputbufs[j] + i, RAIDSECTOR);
                }
                else
                {
                    recoverSectorFromParity(b, inputbufs, i);
                }
                b += RAIDSECTOR;
            }
        }
        assert(b == endpos);
    }
    return result;
}

void RaidBufferManager::recoverSectorFromParity(byte* dest, byte* inputbufs[], unsigned offset)
{
    assert(sizeof(m_off_t)*2 == RAIDSECTOR);
    bool set = false;
    for (unsigned i = RAIDPARTS; i--; )
    {
        if (inputbufs[i])
        {
            if (!set)
            {
                memcpy(dest, inputbufs[i] + offset, RAIDSECTOR);
                set = true;
            }
            else
            {
                *(m_off_t*)dest ^= *(m_off_t*)(inputbufs[i] + offset);
                *(m_off_t*)(dest + sizeof(m_off_t)) ^= *(m_off_t*)(inputbufs[i] + offset + sizeof(m_off_t));
            }
        }
    }
}

void RaidBufferManager::combineLastRaidLine(byte* dest, size_t remainingbytes)
{
    // we have to be careful to use the right number of bytes from each sector
    for (unsigned i = 1; i < RAIDPARTS && remainingbytes > 0; ++i)
    {
        if (!raidinputparts[i].empty())
        {
            FilePiece* sector = raidinputparts[i].front();
            size_t n = std::min(remainingbytes, sector->buf.datalen());
            if (!sector->buf.isNull())
            {
                memcpy(dest, sector->buf.datastart(), n);
            }
            else
            {
                memset(dest, 0, n);
                for (unsigned j = RAIDPARTS; j--; )
                {
                    if (!raidinputparts[j].empty() && !raidinputparts[j].front()->buf.isNull())
                    {
                        FilePiece* xs = raidinputparts[j].front();
                        for (size_t x = std::min(n, xs->buf.datalen()); x--; )
                        { // Integer promotion with bitwise operators
                            dest[x] = static_cast<byte>(dest[x] ^ xs->buf.datastart()[x]);
                        }
                    }
                }
            }
            dest += n;
            remainingbytes -= n;
        }
    }
}

void RaidBufferManager::rollInputBuffers(size_t dataToDiscard)
{
    // remove finished input buffers
    for (unsigned i = RAIDPARTS; i--; )
    {
        if (!raidinputparts[i].empty())
        {
            FilePiece& ip = *raidinputparts[i].front();
            ip.buf.start += dataToDiscard;
            ip.pos += dataToDiscard;
            if (ip.buf.start >= ip.buf.end)
            {
                delete raidinputparts[i].front();
                raidinputparts[i].pop_front();
            }
        }
    }
}

m_off_t TransferBufferManager::calcOutputChunkPos(m_off_t acquiredpos)
{
    return ChunkedHash::chunkfloor(acquiredpos);  // we can only mac to the chunk boundary, hold the rest over
}

// decrypt, mac downloaded chunk
bool RaidBufferManager::FilePiece::finalize(bool parallel, m_off_t filesize, int64_t ctriv, SymmCipher *cipher, chunkmac_map* source_chunkmacs)
{
    assert(!finalized);
    bool queueParallel = false;

    byte *chunkstart = buf.datastart();
    m_off_t startpos = pos;
    m_off_t finalpos = startpos + static_cast<m_off_t>(buf.datalen());
    assert(finalpos <= filesize);
    if (finalpos != filesize)
    {
        finalpos &= -SymmCipher::BLOCKSIZE;
    }

    m_off_t endpos = ChunkedHash::chunkceil(startpos, finalpos);
    unsigned chunksize = static_cast<unsigned>(endpos - startpos);

    while (chunksize)
    {
        m_off_t chunkid = ChunkedHash::chunkfloor(startpos);
        if (!chunkmacs.finishedAt(chunkid))
        {
            if (source_chunkmacs)
            {
                source_chunkmacs->copyEntryTo(chunkid, chunkmacs);
            }
            if (endpos == ChunkedHash::chunkceil(chunkid, filesize))
            {
                if (parallel)
                {
                    // executing on a worker thread (or synchronously on transferslot destruction)
                    // these are independent chunks, or the earlier part of the chunk is already done.
                    chunkmacs.ctr_decrypt(chunkid, cipher, chunkstart, chunksize, startpos, ctriv, true);
                    LOG_debug << "Finished chunk: " << startpos << " - " << endpos << "   Size: " << chunksize;
                }
                else
                {
                    queueParallel = true;
                }
            }
            else if (!parallel)
            {
                // these part chunks must be done serially (and first), since later parts of a chunk need the mac of earlier parts as input.
                chunkmacs.ctr_decrypt(chunkid, cipher, chunkstart, chunksize, startpos, ctriv, false);
                LOG_debug << "Decrypted partial chunk: " << startpos << " - " << endpos << "   Size: " << chunksize;
            }
        }
        chunkstart += chunksize;
        startpos = endpos;
        endpos = ChunkedHash::chunkceil(startpos, finalpos);
        chunksize = static_cast<unsigned>(endpos - startpos);
    }

    finalized = !queueParallel;
    if (finalized)
        finalizedCV.notify_one();

    return queueParallel;
}

void TransferBufferManager::finalize(FilePiece&)
{
    // for transfers (as opposed to DirectRead), decrypt/mac is now done on threads
}


bool RaidBufferManager::tryRaidHttpGetErrorRecovery(unsigned errorConnectionNum, bool incrementErrors)
{
    assert(isRaid());

    if (incrementErrors)
    {
        raidHttpGetErrorCount[errorConnectionNum] += 1;
    }

    g_faultyServers.add(tempurls[errorConnectionNum]);

    unsigned errorSum = 0;
    unsigned highestErrors = 0;
    for (unsigned i = RAIDPARTS; i--; )
    {
        errorSum += raidHttpGetErrorCount[i];
        highestErrors = std::max<unsigned>(highestErrors, raidHttpGetErrorCount[i]);
    }

    // Allow for one nonfunctional channel and one glitchy channel.  We can still make progress swapping back and forth
    if ((errorSum - highestErrors) < RAID_ACTIVE_CHANNEL_FAIL_THRESHOLD)
    {
        if (unusedRaidConnection < RAIDPARTS)
        {
            LOG_warn << "5 connection cloudraid shutting down connection " << errorConnectionNum << " due to error, and starting " << unusedRaidConnection << " instead";

            // start up the old unused connection, and cancel this one.  Other connections all have real data since we were already in 5 connection mode
            clearOwningFilePieces(raidinputparts[unusedRaidConnection]);
            clearOwningFilePieces(raidinputparts[errorConnectionNum]);
            raidrequestpartpos[unusedRaidConnection] = raidpartspos;
            raidrequestpartpos[errorConnectionNum] = raidpartspos;
        }
        else
        {
            LOG_warn << "6 connection cloudraid shutting down connection " << errorConnectionNum << " due to error";
            clearOwningFilePieces(raidinputparts[errorConnectionNum]);
            raidrequestpartpos[errorConnectionNum] = raidpartspos;
        }

        unusedRaidConnection = errorConnectionNum;
        return true;
    }
    else
    {
        return false;
    }
}

bool RaidBufferManager::detectSlowestRaidConnection(unsigned thisConnection, unsigned& slowestConnection)
{
    if (isRaid() && unusedRaidConnection == RAIDPARTS)
    {
        connectionStarted[thisConnection] = true;
        int count = 0;
        for (unsigned j = RAIDPARTS; j--; )
        {
            if (!connectionStarted[j])
            {
                slowestConnection = j;
                ++count;
            }
        }
        if (count == 1)
        {
            unusedRaidConnection = slowestConnection;
            raidrequestpartpos[unusedRaidConnection] = raidpartspos;
            return true;
        }
    }
    return false;
}

bool RaidBufferManager::setUnusedRaidConnection(unsigned newUnusedRaidConnection)
{
    if (isRaid() && newUnusedRaidConnection < RAIDPARTS)
    {
        LOG_debug << "Set unused raid connection to " << newUnusedRaidConnection << " (clear previous unused connection: " << unusedRaidConnection << ")";
        if (unusedRaidConnection < RAIDPARTS) clearOwningFilePieces(raidinputparts[unusedRaidConnection]);
        clearOwningFilePieces(raidinputparts[newUnusedRaidConnection]);
        if (unusedRaidConnection < RAIDPARTS) raidrequestpartpos[unusedRaidConnection] = raidpartspos;
        raidrequestpartpos[newUnusedRaidConnection] = raidpartspos;
        unusedRaidConnection = newUnusedRaidConnection;
        return true;
    }
    return false;
}

unsigned RaidBufferManager::getUnusedRaidConnection() const
{
    return unusedRaidConnection;
}

m_off_t RaidBufferManager::progress() const
{
    assert(isRaid());
    m_off_t reportPos = 0;

    for (unsigned j = RAIDPARTS; j--; )
    {
        for (FilePiece* p : raidinputparts[j])
        {
            if (!p->buf.isNull())
            {
                reportPos += p->buf.datalen();
            }
        }
    }

    if (!leftoverchunk.buf.isNull())
    {
        // Need to count the valid data on the leftover chunk, if any.
        reportPos += leftoverchunk.buf.datalen();
    }

    return reportPos;
}

TransferBufferManager::TransferBufferManager()
    : transfer(NULL)
{
}

void TransferBufferManager::setIsRaid(Transfer* t, const std::vector<std::string>& tempUrls, m_off_t resumepos, m_off_t maxRequestSize, bool isNewRaid)
{
    transfer = t;
    RaidBufferManager::setIsRaid(tempUrls, resumepos, t->size, t->size, maxRequestSize, isNewRaid && t->type == GET);
}

m_off_t& TransferBufferManager::transferPos(unsigned connectionNum)
{
    return isRaid() ? RaidBufferManager::transferPos(connectionNum) : transfer->pos;
}
std::pair<m_off_t, m_off_t> TransferBufferManager::nextNPosForConnection(unsigned connectionNum, m_off_t maxRequestSize, unsigned connectionCount, bool& newInputBufferSupplied, bool& pauseConnectionForRaid, m_off_t uploadSpeed)
{
    // returning a pair for clarity - specifying the beginning and end position of the next data block, as the 'current pos' may be updated during this function
    newInputBufferSupplied = false;
    pauseConnectionForRaid = false;

    if (isRaid())
    {
        return RaidBufferManager::nextNPosForConnection(connectionNum, newInputBufferSupplied, pauseConnectionForRaid);
    }

    transfer->pos = transfer->size ?
                        transfer->chunkmacs.nextUnprocessedPosFrom(transfer->pos) :
                        0;
    m_off_t npos = transfer->size ?
                        ChunkedHash::chunkceil(transfer->pos, transfer->size) :
                        0;
    if(npos > transfer->pos)
    {
        // Calc limit for request size value depending on connection/transfer/progress heuristics.
        m_off_t maxReqSize = 0;
        if (transfer->type == PUT)
        {
            // choose upload chunks that are big enough to saturate the connection, so we don't start HTTP PUT request too frequently
            // make them smaller at the end of the file so we still have the last parts delivered in parallel
            m_off_t largeSize = 32 * 1024 * 1024;
            m_off_t maxsize = largeSize;
            if (npos + 2 * maxsize > transfer->size)
                maxsize /= 2;
            if (npos + maxsize > transfer->size)
                maxsize /= 2;
            if (npos + maxsize > transfer->size)
                maxsize /= 2;
            m_off_t speedsize = std::min<m_off_t>(maxsize, uploadSpeed * 2 / 3);        // two seconds of data over 3 connections
            m_off_t sizesize = transfer->size > largeSize ? 8 * 1024 * 1024 : 0; // start with large-ish portions for large files.
            m_off_t targetsize = std::max<m_off_t>(sizesize, speedsize);
            maxReqSize = targetsize;
        }
        else if (transfer->type == GET)
        {
            if (isNewRaid())
            {
                if (const auto posOffsetToRaidLine = offsetToRaidLine(transfer->pos);
                    posOffsetToRaidLine)
                {
                    LOG_err << "Wrong transfer->pos for new raid, not padded to RAIDLINE: "
                               "transfer->pos = "
                            << transfer->pos << ", RAIDLINE = " << RAIDLINE
                            << ", mod = " << posOffsetToRaidLine;
                    return std::make_pair(-1, -1);
                }

                // We need to adjust the size taking into account that our RaidReqs will be split into 5 requests (one for each part)
                // Besides, we need that the RaidReqs (except for the last one) are padded to a RAIDLINE
                m_off_t defaultMaxReqSize = static_cast<m_off_t>((TransferSlot::MAX_REQ_SIZE_NEW_RAID) * EFFECTIVE_RAIDPARTS);

                const m_off_t AVERAGE_NUMBER_OF_TRANSFERSLOT_CONNECTIONS = 4; // based on the average number of connections per download (and default value in MEGASync)
                m_off_t maxReqsSize = defaultMaxReqSize * AVERAGE_NUMBER_OF_TRANSFERSLOT_CONNECTIONS;

                maxReqSize = static_cast<m_off_t>(maxReqsSize / transfer->slot->connections); // divided by the real number of connections
                maxReqSize = std::max<m_off_t>(maxReqSize, (1 * 1024 * 1024) * EFFECTIVE_RAIDPARTS); // min 1MB for each raidpart
                DEBUG_TEST_HOOK_LIMIT_MAX_REQ_SIZE(maxReqSize) // Limit max request size if needed
                maxReqSize = std::min<m_off_t>(maxReqSize, transfer->size); // Not greater than the transfer itself
                if (transfer->size <= TransferSlot::UPPER_FILESIZE_LIMIT_FOR_SMALLER_CHUNKS)
                {
                    maxReqSize = transfer->size / AVERAGE_NUMBER_OF_TRANSFERSLOT_CONNECTIONS;
                }

                m_off_t nextChunk = ChunkedHash::chunkceil(transfer->pos + maxReqSize, transfer->size);
                while ((nextChunk < transfer->size) && (((nextChunk - transfer->pos) % RAIDLINE) != 0))
                {
                    // Needed for expandUnprocessedPiece to return a chunk padded to raid-line (for chunks which are not the last one)
                    maxReqSize = ChunkedHash::chunkceil(maxReqSize, transfer->size);
                    nextChunk = ChunkedHash::chunkceil(transfer->pos + maxReqSize, transfer->size);
                }
                maxReqSize += 1; // Same as above, needed for expandUnProcessedPiece to return a chunk padded to raid-line
            }
            else
            {
                // Non raid
                maxReqSize = (transfer->size - transfer->progresscompleted) / connectionCount / 2;
                if (maxReqSize > maxRequestSize)
                {
                    maxReqSize = maxRequestSize;
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
            }
        }
        // Calc npos limit depending on the maxReqSize, the next processed piece and the transfer size.
        npos = transfer->chunkmacs.expandUnprocessedPiece(transfer->pos, npos, transfer->size, maxReqSize);
        if (isNewRaid() && (npos < transfer->size))
        {
            if (const auto chunkSize = (npos - transfer->pos),
                chunkOffsetToRaidLine = offsetToRaidLine(chunkSize);
                chunkOffsetToRaidLine)
            {
                LOG_err << "Wrong chunk size for new raid, not padded to RAIDLINE: pos = "
                        << transfer->pos << ", npos = " << npos << ", chunk size = " << chunkSize
                        << ", RAIDLINE = " << RAIDLINE << ", mod = " << chunkOffsetToRaidLine;
                return std::make_pair(-1, -1);
            }
        }
        LOG_debug << std::string(transfer->type == PUT ? "Uploading" :
                                transfer->type == GET ? "Downloading" : "?")
                  << " chunk of size " << npos - transfer->pos;
        assert(npos > transfer->pos);
    }
    return std::make_pair(transfer->pos, npos);
}

void TransferBufferManager::bufferWriteCompletedAction(FilePiece& r)
{
    r.chunkmacs.copyEntriesTo(transfer->chunkmacs);
    r.chunkmacs.clear();
    transfer->setProgresscompleted(static_cast<m_off_t>(r.buf.datalen()), true /*append*/);
    LOG_debug << "On write completed -> Cached data at: " << r.pos << "   Size: " << r.buf.datalen() << "   Progress completed: " << transfer->progresscompleted << "/" << transfer->size;
}


DirectReadBufferManager::DirectReadBufferManager(DirectRead* dr)
{
    directRead = dr;
}

m_off_t& DirectReadBufferManager::transferPos(unsigned connectionNum)
{
    return isRaid() ? RaidBufferManager::transferPos(connectionNum) : directRead->nextrequestpos;
}

m_off_t DirectReadBufferManager::calcOutputChunkPos(m_off_t acquiredpos)
{
    return acquiredpos;  // give all the data straight away for streaming, no need to hold any over for mac boundaries
}

void DirectReadBufferManager::finalize(FilePiece& fp)
{
    int r, l, t;

    // decrypt, pass to app and erase
    r = fp.pos & (SymmCipher::BLOCKSIZE - 1);
    t = int(fp.buf.datalen());

    if (r)
    {
        byte buf[SymmCipher::BLOCKSIZE];
        l = static_cast<int>(sizeof(buf)) - r;

        if (l > t)
        {
            l = t;
        }

        memcpy(buf + r, fp.buf.datastart(), static_cast<size_t>(l));
        directRead->drn->symmcipher.ctr_crypt(buf,
                                              sizeof buf,
                                              fp.pos - r,
                                              static_cast<uint64_t>(directRead->drn->ctriv),
                                              NULL,
                                              false);
        memcpy(fp.buf.datastart(), buf + r, static_cast<size_t>(l));
    }
    else
    {
        l = 0;
    }

    if (t > l)
    {
        // the buffer has some extra at the end to allow full blocksize decrypt at the end
        directRead->drn->symmcipher.ctr_crypt(fp.buf.datastart() + l,
                                              static_cast<unsigned>(t - l),
                                              fp.pos + l,
                                              static_cast<uint64_t>(directRead->drn->ctriv),
                                              NULL,
                                              false);
    }
}


class CloudRaid::CloudRaidImpl
{
private:
    std::vector<std::unique_ptr<RaidProxy::RaidReqPool>> mRaidReqPoolArray;
    int mConnections;
    TransferSlot* mTSlot;
    MegaClient* mClient;
    bool mStarted;
    uint8_t mUnusedRaidConnection;
    std::pair<::mega::error, dstime> mTransferFailed; // Error and backoff to call transfer->failed()

public:
    CloudRaidImpl(TransferSlot* tslot, MegaClient* client, int connections)
    : mConnections(connections)
    , mTSlot(tslot)
    , mClient(client)
    , mStarted(false)
    , mUnusedRaidConnection(RAIDPARTS)
    {
        LOG_verbose << "[CloudRaidImpl::CloudRaidImpl] CONSTRUCTOR CALL [this = " << this << "]";
        assert(mTSlot != nullptr);
        assert(mClient != nullptr);
        mTransferFailed = std::make_pair(API_OK, 0);
        start();
    }

    ~CloudRaidImpl()
    {
        LOG_verbose << "[CloudRaidImpl::~CloudRaidImpl] DESTRUCTOR CALL [this = " << this << "]";
        stop();
    }

    /* TransferSlot functionality */
    bool disconnect(const std::shared_ptr<HttpReqXfer>& req)
    {
        if (!mStarted) return false;
        mTSlot->disconnect(req);
        return true;
    }

    bool prepareRequest(const std::shared_ptr<HttpReqXfer>& req, const string& tempURL, m_off_t pos, m_off_t npos)
    {
        if (!mStarted) return false;
        mTSlot->prepareRequest(req, tempURL, pos, npos);
        return req->status == REQ_PREPARED;
    }

    bool post(const std::shared_ptr<HttpReqXfer>& req)
    {
        if (!mStarted) return false;
        mTSlot->processRequestPost(mClient, req);
        return req->status == REQ_INFLIGHT;
    }

    bool onRequestFailure(const std::shared_ptr<HttpReqXfer>& req, uint8_t part, dstime& backoff)
    {
        if (!mStarted) return false;
        dstime tslot_backoff = 0;
        auto failValues = mTSlot->processRequestFailure(mClient, req, tslot_backoff, static_cast<int>(part));
        backoff = tslot_backoff;
        if (failValues.first != API_OK)
        {
            setTransferFailure(failValues.first, failValues.second);
        }
        return true;
    }

    bool setTransferFailure(::mega::error e, dstime backoff)
    {
        if (!mStarted) return false;
        if (mTransferFailed.first)
        {
            LOG_warn << "[CloudRaid::setTransferFailure] Transfer failed values are already set. Previous values: error = " << e << ", backoff = " << backoff;
        }
        LOG_debug << "[CloudRaid::setTransferFailure] Transfer failed values set to: error = " << e << ", backoff = " << backoff;
        mTransferFailed.first = e;
        mTransferFailed.second = backoff;
        return true;
    }

    std::pair<::mega::error, dstime> checkTransferFailure()
    {
        if (!mStarted) return std::make_pair(API_OK, 0);
        return std::make_pair(mTransferFailed.first, mTransferFailed.second);
    }

    bool setUnusedRaidConnection(uint8_t part, bool addToFaultyServers)
    {
        if (!mStarted) return false;

        if (part >= RAIDPARTS)
        {
            LOG_warn << "[CloudRaid::setUnusedRaidConnection] Invalid connection index, setting it to 0";
            assert(false && "Unused raid must be within RAIDPARTS");
            mUnusedRaidConnection = 0;
            return false;
        }

        LOG_debug << "[CloudRaid::setUnusedRaidConnection] Set unused raid connection to " << (int)part << " (clear previous unused connection: " << (int)mUnusedRaidConnection << ") [addToFaultyServers = " << addToFaultyServers << "]";
        mUnusedRaidConnection = part;

        if (addToFaultyServers)
        {
            g_faultyServers.add(mTSlot->transferbuf.tempUrlVector()[part]);
        }
        return true;
    }

    uint8_t getUnusedRaidConnection() const
    {
        return mUnusedRaidConnection; // No need to check if mStarted, there is always a default value, and if we stop it with a previous set value it is ok to retrieve it
    }

    m_off_t transferred(const std::shared_ptr<HttpReqXfer>& req) const
    {
        if (!mStarted)
            return 0;
        return req->transferred(mClient);
    }

    bool processRequestLatency(const std::shared_ptr<HttpReqXfer>& req)
    {
        if (!mStarted)
            return false;
        mTSlot->processRequestLatency(req);
        return true;
    }

    /* CloudRaid functionality */
    bool balancedRequest(int connection, const std::vector<std::string> &tempUrls, size_t cfilesize, m_off_t cstart, size_t creqlen)
    {
        if (!mStarted)
        {
            start();
        }
        RaidProxy::RaidReq::Params raidReqParams(tempUrls, cfilesize, cstart, creqlen);
        mRaidReqPoolArray[static_cast<size_t>(connection)].reset(new RaidProxy::RaidReqPool());
        mRaidReqPoolArray[static_cast<size_t>(connection)]->request(raidReqParams,
                                                                    mTSlot->getcloudRaidPtr());
        return mRaidReqPoolArray[static_cast<size_t>(connection)]->rr() != nullptr;
    }

    bool start()
    {
        if (mStarted)
        {
            return false;
        }
        mRaidReqPoolArray.resize(static_cast<size_t>(mConnections));
        mStarted = true;
        if (mUnusedRaidConnection == RAIDPARTS)
        {
            mUnusedRaidConnection = static_cast<uint8_t>(g_faultyServers.selectWorstServer(mTSlot->transferbuf.tempUrlVector()));
        }
        LOG_debug << "[CloudRaid::start] CloudRAID started. Initial unused raid connection: " << (int)mUnusedRaidConnection;
        return true;
    }

    bool stop()
    {
        LOG_verbose << "[CloudRaid::stop] stop CALL [started = " << mStarted << "] [this = " << this << "]";
        if (!mStarted)
        {
            return false;
        }
        mRaidReqPoolArray.clear();
        mStarted = false;
        return true;
    }

    bool removeRaidReq(int connection)
    {
        LOG_verbose << "[CloudRaid::removeRaidReq] connection = " << connection << " [started = " << mStarted << "] [this = " << this << "]";
        if (mStarted && mRaidReqPoolArray[static_cast<size_t>(connection)])
        {
            mRaidReqPoolArray[static_cast<size_t>(connection)].reset();
            return true;
        }
        return false;
    }

    bool resumeAllConnections()
    {
        if (mStarted)
        {
            int i = mConnections;
            while (i-- > 0)
            {
                if (mRaidReqPoolArray[static_cast<size_t>(i)])
                {
                    mRaidReqPoolArray[static_cast<size_t>(i)]->rr()->resumeall();
                }
            }
            return true;
        }
        return false;
    }

    m_off_t readData(int connection, byte* buf, m_off_t len)
    {
        m_off_t readData = -1;
        if (mStarted && mRaidReqPoolArray[static_cast<size_t>(connection)])
        {
            readData = static_cast<m_off_t>(
                mRaidReqPoolArray[static_cast<size_t>(connection)]->rr()->readdata(buf, len));
        }
        return readData;
    }

    bool raidReqDoio(int connection)
    {
        if (mStarted && mRaidReqPoolArray[static_cast<size_t>(connection)])
        {
            mRaidReqPoolArray[static_cast<size_t>(connection)]->raidproxyio();
            return true;
        }
        return false;
    }

    m_off_t progress() const
    {
        m_off_t progressCount = 0;
        if (mStarted)
        {
            int i = mConnections;
            while (i-- > 0)
            {
                if (mRaidReqPoolArray[static_cast<size_t>(i)])
                {
                    progressCount += mRaidReqPoolArray[static_cast<size_t>(i)]->rr()->progress();
                }
            }
        }
        return progressCount;
    }
};

CloudRaid::CloudRaid()
{
}

CloudRaid::CloudRaid(TransferSlot* tslot, MegaClient* client, int connections)
{
    LOG_verbose << "[CloudRaid::CloudRaid] CONSTRUCTOR CALL [this = " << this << "]";
    init(tslot, client, connections);
}

CloudRaid::~CloudRaid() { LOG_verbose << "[CloudRaid::~CloudRaid] DESTRUCTOR CALL [this = " << this << "]"; }

bool CloudRaid::isShown() const
{
    return mShown;
}

/* TransferSlot functionality */
bool CloudRaid::disconnect(const std::shared_ptr<HttpReqXfer>& req)
{
    if (!mShown)
        return false;
    return mPimpl()->disconnect(req);
}

bool CloudRaid::prepareRequest(const std::shared_ptr<HttpReqXfer>& req, const string& tempURL, m_off_t pos, m_off_t npos)
{
    if (!mShown)
        return false;
    return mPimpl()->prepareRequest(req, tempURL, pos, npos);
}

bool CloudRaid::post(const std::shared_ptr<HttpReqXfer>& req)
{
    if (!mShown)
        return false;
    return mPimpl()->post(req);
}

bool CloudRaid::onRequestFailure(const std::shared_ptr<HttpReqXfer>& req, uint8_t part, dstime& backoff)
{
    if (!mShown)
        return false;
    return mPimpl()->onRequestFailure(req, part, backoff);
}

bool CloudRaid::setTransferFailure(::mega::error e, dstime backoff)
{
    if (!mShown)
        return false;
    return mPimpl()->setTransferFailure(e, backoff);
}

std::pair<::mega::error, dstime> CloudRaid::checkTransferFailure()
{
    if (!mShown)
        return std::make_pair(API_OK, 0);
    return mPimpl()->checkTransferFailure();
}

bool CloudRaid::setUnusedRaidConnection(uint8_t part, bool addToFaultyServers)
{
    if (!mShown)
        return false;
    return mPimpl()->setUnusedRaidConnection(part, addToFaultyServers);
}

uint8_t CloudRaid::getUnusedRaidConnection() const
{
    return mPimpl()->getUnusedRaidConnection(); // No need to check if mShown, there is always a default value, and if we stop it with a previous set value it is ok to retrieve it
}

m_off_t CloudRaid::transferred(const std::shared_ptr<HttpReqXfer>& req) const
{
    if (!mShown)
        return 0;
    return mPimpl()->transferred(req);
}

bool CloudRaid::processRequestLatency(const std::shared_ptr<HttpReqXfer>& req)
{
    if (!mShown)
        return false;
    return mPimpl()->processRequestLatency(req);
}

bool CloudRaid::init(TransferSlot* tslot, MegaClient* client, int connections)
{
    m_pImpl = std::make_unique<CloudRaidImpl>(tslot, client, static_cast<uint8_t>(connections));
    mShown = m_pImpl != nullptr;
    return mShown;
}

bool CloudRaid::balancedRequest(int connection, const std::vector<std::string>& tempUrls, size_t cfilesize, m_off_t cstart, size_t creqlen)
{
    if (!mShown)
        return false;
    return mPimpl()->balancedRequest(connection, tempUrls, cfilesize, cstart, creqlen);
}

bool CloudRaid::removeRaidReq(int connection)
{
    if (!mShown)
        return false;
    return mPimpl()->removeRaidReq(connection);
}

m_off_t CloudRaid::readData(int connection, byte* buf, m_off_t len)
{
    if (!mShown)
        return -1;
    return mPimpl()->readData(connection, buf, len);
}

bool CloudRaid::resumeAllConnections()
{
    if (!mShown)
        return false;
    return mPimpl()->resumeAllConnections();
}

bool CloudRaid::raidReqDoio(int connection)
{
    if (!mShown)
        return false;
    return mPimpl()->raidReqDoio(connection);
}

bool CloudRaid::stop()
{
    if (!mShown)
        return false;
    return mPimpl()->stop();
}

m_off_t CloudRaid::progress() const
{
    if (!mShown)
        return 0;
    return mPimpl()->progress();
}

}; // namespace
