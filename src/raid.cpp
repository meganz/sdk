/**
 * @file mega/raid.cpp
 * @brief helper classes for managing cloudraid downloads
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

#include "mega/raid.h"

#include "mega/transfer.h" 

namespace mega
{


    TransferBufferManager::FilePiece::FilePiece() 
        : pos(0)
        , buf(NULL, 0, 0) 
    {
    }
    
    TransferBufferManager::FilePiece::FilePiece(m_off_t p, size_t len) 
        : pos(p)
        , buf(new byte[len + std::min<size_t>(SymmCipher::BLOCKSIZE, RAIDSECTOR)], 0, len)   // SymmCipher::ctr_crypt requirement: decryption: data must be padded to BLOCKSIZE.  Also make sure we can xor up to RAIDSECTOR more for convenience
    {
    }   
    
    
    TransferBufferManager::FilePiece::FilePiece(m_off_t p, HttpReq::http_buf_t* b) // taking ownership
        : pos(p)
        , buf(NULL, 0, 0)
    {
        buf.swap(*b);  // take its buffer and copy other members
        delete b;  // client no longer owns it so we must delete.  Similar to move semantics where we would just assign
    }

    void TransferBufferManager::FilePiece::swap(FilePiece& other) 
    {
        m_off_t tp = pos; pos = other.pos; other.pos = tp;
        chunkmacs.swap(other.chunkmacs);
        buf.swap(other.buf);
    }

    TransferBufferManager::TransferBufferManager()
        : transfer(NULL)
        , is_raid(false)
        , raidKnown(false)
        , useOnlyFiveRaidConnections(false)
        , unusedRaidConnection(0)
        , raidpartspos(0)
        , outputfilepos(0)
        , resumewastedbytes(0)
        , raidLinesPerChunk(16 * 1024)
    {
        for (int i = RAIDPARTS; i--; )
        {
            raidrequestpartpos[i] = 0;
            connectionPaused[i] = false;
            raidHttpGetErrorCount[i] = 0;
        }
    }

    static void clearOwningFilePieces(std::deque<TransferBufferManager::FilePiece*>& q)
    {
        for (std::deque<TransferBufferManager::FilePiece*>::iterator i = q.begin(); i != q.end(); ++i)
        {
            delete *i;
        }
        q.clear();
    }
    static void clearOwningFilePieces(std::map<m_off_t, TransferBufferManager::FilePiece*>& q)
    {
        for (std::map<m_off_t, TransferBufferManager::FilePiece*>::iterator i = q.begin(); i != q.end(); ++i)
        {
            delete i->second;
        }
        q.clear();
    }

    TransferBufferManager::~TransferBufferManager()
    {
        for (int i = RAIDPARTS; i--; )
        {
            clearOwningFilePieces(raidinputparts[i]);
            clearOwningFilePieces(raidinputparts_recovery[i]);
        }
        for (std::map<unsigned, FilePiece*>::iterator i = asyncoutputbuffers.begin(); i != asyncoutputbuffers.end(); ++i)
        {
            delete i->second;
        }
    }

    void TransferBufferManager::setIsRaid(bool b, Transfer* t, std::vector<std::string>& tempUrls, m_off_t resumepos, unsigned maxDownloadRequestSize)
    {
        is_raid = b;
        raidKnown = true;
        transfer = t;
        tempurls.swap(tempUrls);  // move semantics
        outputfilepos = resumepos;
        if (is_raid)
        {
            raidpartspos = resumepos / (RAIDPARTS - 1);
            raidpartspos -= raidpartspos % RAIDSECTOR;
            resumewastedbytes = size_t(outputfilepos - raidpartspos * (RAIDPARTS - 1));
            outputfilepos -= resumewastedbytes;  // we'll skip over these bytes on the first output
            for (int i = RAIDPARTS; i--; )
            {
                raidrequestpartpos[i] = raidpartspos;
            }

            // How much buffer space can we use.  Assuming two chunk sets incoming, one outgoing
            raidLinesPerChunk = maxDownloadRequestSize / RAIDPARTS / 3 / RAIDSECTOR;
            raidLinesPerChunk -= raidLinesPerChunk % 1024;
            raidLinesPerChunk = std::min<unsigned>(raidLinesPerChunk, 64 * 1024);
            raidLinesPerChunk = std::max<unsigned>(raidLinesPerChunk, 8 * 1024);

            //If we can get the whole file with just 5 requests then do that to avoid latency of subsequent http requests
            if (transfer->size / (RAIDPARTS - 1) <= RaidMaxChunksPerRead * raidLinesPerChunk * RAIDSECTOR)
            {
                useOnlyFiveRaidConnections = true;
                unusedRaidConnection = rand() % RAIDPARTS;
            }
        }
    }

    bool TransferBufferManager::isRaid()
    {
        assert(raidKnown);
        assert(transfer);
        return is_raid;
    }

    std::string TransferBufferManager::emptyReturnString;

    const std::string& TransferBufferManager::tempURL(unsigned connectionNum)
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
            return emptyReturnString;
        }
    }

    const std::vector<std::string>& TransferBufferManager::tempUrlVector() const
    {
        return tempurls;
    }

    // takes ownership of the buffer 
    void TransferBufferManager::submitBuffer(unsigned connectionNum, FilePiece* piece)
    {
        if (isRaid())
        {
            assert(connectionNum < RAIDPARTS);
            if (!piece->buf.isNull())
            {
                raidHttpGetErrorCount[connectionNum] = 0;
            }

            std::deque<FilePiece*>& connectionpieces = raidinputparts[connectionNum];
            m_off_t contiguouspos = connectionpieces.empty() ? raidpartspos : connectionpieces.back()->pos + connectionpieces.back()->buf.datalen();

            assert(piece->pos >= contiguouspos);
            if (piece->pos == contiguouspos)
            {
                transferPosUpdateMinimum(piece->pos + piece->buf.datalen(), connectionNum);  // in case of download piece arriving after connection failure recovery
                raidinputparts[connectionNum].push_back(piece);
            }
            else
            {
                // we would have been downloading this on one connection (beyond a skip point) when another failed and we switched to 5 connection
                raidinputparts_recovery[connectionNum][piece->pos] = piece;
            }
        }
        else
        {
            finalize(*piece);
            assert(asyncoutputbuffers.find(connectionNum) == asyncoutputbuffers.end() || !asyncoutputbuffers.find(connectionNum)->second);
            asyncoutputbuffers[connectionNum] = piece;
        }
    }

    TransferBufferManager::FilePiece* TransferBufferManager::getAsyncOutputBufferPointer(unsigned connectionNum)
    {
        std::map<unsigned, FilePiece*>::iterator i = asyncoutputbuffers.find(connectionNum);
        if (isRaid() && (i == asyncoutputbuffers.end() || !i->second))
        {
            combineRaidParts(connectionNum);
            i = asyncoutputbuffers.find(connectionNum);
        }
        return (i == asyncoutputbuffers.end()) ? NULL : i->second;
    }


    void TransferBufferManager::bufferWriteCompleted(unsigned connectionNum)
    {
        std::map<unsigned, FilePiece*>::iterator aob = asyncoutputbuffers.find(connectionNum);
        if (aob != asyncoutputbuffers.end())
        {
            assert(aob->second);
            if (aob->second)
            {
                FilePiece& r = *aob->second;
                for (chunkmac_map::iterator it = r.chunkmacs.begin(); it != r.chunkmacs.end(); it++)
                {
                    transfer->chunkmacs[it->first] = it->second;
                }

                r.chunkmacs.clear();
                transfer->progresscompleted += r.buf.datalen();
                LOG_debug << "Cached data at: " << r.pos << "   Size: " << r.buf.datalen();
                delete aob->second;
                aob->second = NULL;
            }
        }
    }

    void TransferBufferManager::transferPosUpdateMinimum(m_off_t minpos, unsigned connectionNum)
    {
        if (!isRaid())
        {
            if (transfer->pos < minpos)
            {
                transfer->pos = minpos;
            }
        }
        else
        {
            if (raidrequestpartpos[connectionNum] < minpos)
            {
                raidrequestpartpos[connectionNum] = minpos;
            }
        }
    }

    m_off_t TransferBufferManager::transferPos(unsigned connectionNum)
    {
        if (!isRaid())
        {
            return transfer->pos;
        }
        else
        {
            return raidrequestpartpos[connectionNum];
        }
    }

    m_off_t TransferBufferManager::nextTransferPos()
    {
        assert(!isRaid());
        chunkmac_map& chunkmacs = transfer->chunkmacs;
        while (chunkmacs.find(ChunkedHash::chunkfloor(transfer->pos)) != chunkmacs.end())
        {
            if (chunkmacs[ChunkedHash::chunkfloor(transfer->pos)].finished)
            {
                transfer->pos = ChunkedHash::chunkceil(transfer->pos);
            }
            else
            {
                transfer->pos += chunkmacs[ChunkedHash::chunkfloor(transfer->pos)].offset;
                break;
            }
        }
        return transfer->pos;
    }

    std::pair<m_off_t, m_off_t> TransferBufferManager::nextNPosForConnection(unsigned connectionNum, m_off_t maxDownloadRequestSize, unsigned connectionCount, bool& newInputBufferSupplied, bool& pauseConnectionForRaid)
    {
        // returning a pair for clarity - specifying the beginning and end position of the next data block, as the 'current pos' may be updated during this function
        newInputBufferSupplied = false;
        pauseConnectionForRaid = false;

        if (!isRaid())
        {
            m_off_t npos = ChunkedHash::chunkceil(nextTransferPos(), transfer->size);
            if (!transfer->size)
            {
                transfer->pos = 0;
            }

            if (transfer->type == GET && transfer->size)
            {
                m_off_t maxReqSize = (transfer->size - transfer->progresscompleted) / connectionCount / 2;
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
            return std::make_pair(transfer->pos, npos);
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

            // if we were in 6 connection mode, and had to switch to 5 connection due to a failure/timeout, check if there is an already downloaded piece we can use
            std::map<m_off_t, FilePiece*>::iterator recovery_it = raidinputparts_recovery[connectionNum].begin();
            if (recovery_it != raidinputparts_recovery[connectionNum].end())
            {
                assert(recovery_it->second->pos >= curpos);
                if (recovery_it->second->pos == curpos)
                {
                    // use previously received piece that was beyond a skip point
                    newInputBufferSupplied = true;
                    m_off_t npos = recovery_it->second->pos + recovery_it->second->buf.datalen();
                    submitBuffer(connectionNum, recovery_it->second);
                    raidinputparts_recovery[connectionNum].erase(recovery_it);
                    transferPosUpdateMinimum(npos, connectionNum);
                    return std::make_pair(curpos, npos);
                }
                else if (recovery_it->second->pos > curpos)
                {
                    // only allow downloading new data up to an existing downloaded piece
                    maxpos = recovery_it->second->pos;  
                }
            }


            m_off_t npos;
            if (useOnlyFiveRaidConnections)
            {
                // 5 connection mode.  Either download the next large piece, or put a NULL buffer with similar offsets into the mechanism
                npos = std::min<m_off_t>(curpos + raidLinesPerChunk * RAIDSECTOR * RaidMaxChunksPerRead, maxpos);
                if (unusedRaidConnection == connectionNum && npos > curpos)
                {
                    submitBuffer(connectionNum, new TransferBufferManager::FilePiece(curpos, new HttpReq::http_buf_t(NULL, 0, size_t(npos - curpos))));  
                    transferPosUpdateMinimum(npos, connectionNum);
                    newInputBufferSupplied = true;
                }
            }
            else
            {
                // 6 connection mode.  Figure out where the next piece to skip for this connection is, and load up to that. 
                // If we're at a skip point, put a NULL buffer with those offsets into the mechanism
                m_off_t skipPointBlockPos = curpos - (curpos % (raidLinesPerChunk * RAIDSECTOR * RAIDPARTS));
                m_off_t skipPoint = skipPointBlockPos + connectionNum * raidLinesPerChunk * RAIDSECTOR;
                if (curpos < skipPoint)
                {
                    npos = skipPoint;
                }
                else if (curpos < skipPoint + raidLinesPerChunk * RAIDSECTOR)
                {
                    npos = std::min<m_off_t>(skipPoint + raidLinesPerChunk * RAIDSECTOR, maxpos);
                    if (npos > curpos)
                    {
                        submitBuffer(connectionNum, new TransferBufferManager::FilePiece(curpos, new HttpReq::http_buf_t(NULL, 0, size_t(npos - curpos))));
                        transferPosUpdateMinimum(npos, connectionNum);
                        newInputBufferSupplied = true;
                    }
                }
                else
                {
                    npos = skipPoint + raidLinesPerChunk * RAIDSECTOR * std::min<unsigned>(RAIDPARTS, RaidMaxChunksPerRead + 1);
                }
            }
            return std::make_pair(curpos, std::min<m_off_t>(npos, maxpos));
        }
    }

    m_off_t TransferBufferManager::transferSize(unsigned connectionNum)
    {
        if (isRaid())
        {
            return raidPartSize(connectionNum, transfer->size);
        }
        else
        {
            return transfer->size;
        }
    }
    
    m_off_t TransferBufferManager::raidPartSize(unsigned part, m_off_t fullfilesize)
    {
        // compute the size of this raid part based on the original file size len
        m_off_t r = fullfilesize % RAIDLINE;   // residual part

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

        return (fullfilesize - r) / (RAIDPARTS - 1) + t;
    }


    void TransferBufferManager::combineRaidParts(unsigned connectionNum)
    {
        assert(!asyncoutputbuffers[connectionNum]);
        assert(raidpartspos * (RAIDPARTS - 1) == outputfilepos + leftoverchunk.buf.datalen());

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

        // for correct mac processing, we need to process the output file in pieces delimited by the chunkfloor / chunkceil algorithm
        m_off_t newdatafilepos = outputfilepos + leftoverchunk.buf.datalen();
        assert(newdatafilepos + sumdatalen <= transfer->size);
        bool processToEndOfFile =  newdatafilepos + sumdatalen == transfer->size   // if we are getting all the remaining data to the end of the file then no need to break it up
                                && (xorlen - partslen) == std::min<m_off_t>(transfer->size - newdatafilepos - partslen*(RAIDPARTS - 1), RAIDSECTOR)
                                && partslen*(RAIDPARTS - 1) + RAIDLINE >= sumdatalen;  

        if (partslen > 0 || processToEndOfFile)
        {
            m_off_t macchunkpos = ChunkedHash::chunkfloor(newdatafilepos + partslen * (RAIDPARTS - 1));
            
            size_t buflen = static_cast<size_t>(processToEndOfFile ? sumdatalen : partslen * (RAIDPARTS - 1));
            FilePiece* outputrec = combineRaidParts(partslen, buflen, outputfilepos, leftoverchunk);  // includes a bit of extra space for non-full sectors if we are at the end of the file
            rollInputBuffers(partslen);
            raidpartspos += partslen;
            LOG_debug << "raidpartspos: " << raidpartspos;
            sumdatalen -= partslen * (RAIDPARTS - 1);
            outputfilepos += partslen * (RAIDPARTS - 1) + leftoverchunk.buf.datalen();
            byte* dest = outputrec->buf.datastart() + partslen * (RAIDPARTS - 1) + leftoverchunk.buf.datalen();
            FilePiece emptyFilePiece;
            leftoverchunk.swap(emptyFilePiece);  // this data is entirely included in the outputrec now, so discard and reset

            if (processToEndOfFile && sumdatalen > 0)
            {
                // fill in the last of the buffer with non-full sectors from the end of the file
                assert(outputfilepos + sumdatalen == transfer->size);
                combineLastRaidLine(dest, sumdatalen);
                rollInputBuffers(RAIDSECTOR);
            }
            else if (!processToEndOfFile && outputfilepos > macchunkpos)
            {
                // mac processing must be done in chunks, delimited by chunkfloor and chunkceil.  If we don't have the right amount then hold the remainder over for next time.
                size_t excessdata = static_cast<size_t>(outputfilepos - macchunkpos);
                FilePiece newleftover(outputfilepos - excessdata, excessdata);
                leftoverchunk.swap(newleftover);
                memcpy(leftoverchunk.buf.datastart(), outputrec->buf.datastart() + outputrec->buf.datalen() - excessdata, excessdata);
                outputrec->buf.end -= excessdata;
                outputfilepos -= excessdata;
                assert(raidpartspos * (RAIDPARTS - 1) == outputfilepos + leftoverchunk.buf.datalen());
            }

            // discard any excess data that we had to fetch when resuming a file (to align the parts appropriately)
            size_t n = std::min<size_t>(outputrec->buf.datalen(), resumewastedbytes);
            if (n > 0)
            {
                outputrec->pos += n;
                outputrec->buf.start += n;
                resumewastedbytes -= n;
            }

            // store the result in a place that can be read out async
            if (outputrec->buf.datalen() > 0)
            {
                finalize(*outputrec);
                asyncoutputbuffers[connectionNum] = outputrec;
            }
            else
            {
                delete outputrec;  // this would happen if we got some data to process on all connections, but not enough to reach the next chunk boundary yet (and combined data is in leftoverchunk)
            }
        }
    }

    TransferBufferManager::FilePiece* TransferBufferManager::combineRaidParts(size_t partslen, size_t bufflen, m_off_t filepos, FilePiece& prevleftoverchunk)
    {
        assert(prevleftoverchunk.buf.datalen() == 0 || prevleftoverchunk.pos == filepos);
        
        // add a bit of extra space and copy prev chunk to the front
        FilePiece* result = new FilePiece(filepos, bufflen + prevleftoverchunk.buf.datalen());
        if (prevleftoverchunk.buf.datalen() > 0)
        {
            memcpy(result->buf.datastart(), prevleftoverchunk.buf.datastart(), prevleftoverchunk.buf.datalen());
        }

        byte* newdatastart = result->buf.datastart() + prevleftoverchunk.buf.datalen();

        // usual case, for simple and fast processing: all input buffers are the same size, and aligned, and a multiple of raidsector
        if (partslen > 0)
        {
            byte* inputbufs[RAIDPARTS];
            for (unsigned i = RAIDPARTS; i--; )
            {
                FilePiece* inputPiece = raidinputparts[i].front();
                inputbufs[i] = inputPiece->buf.isNull() ? NULL : inputPiece->buf.datastart();
            }

            for (unsigned i = 0; i + RAIDSECTOR - 1 < partslen; i += RAIDSECTOR)
            {
                for (unsigned j = 1; j < RAIDPARTS; ++j)
                {
                    assert(i * (RAIDPARTS - 1) + (j - 1) * RAIDSECTOR + RAIDSECTOR <= result->buf.datalen());
                    if (inputbufs[j])
                        memcpy(newdatastart + i * (RAIDPARTS - 1) + (j - 1) * RAIDSECTOR, inputbufs[j] + i, RAIDSECTOR);
                    else
                        recoverSectorFromParity(newdatastart + i * (RAIDPARTS - 1) + (j - 1) * RAIDSECTOR, inputbufs, i);
                }
            }
        }
        return result;
    }

    void TransferBufferManager::recoverSectorFromParity(byte* dest, byte* inputbufs[], unsigned offset)
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

    void TransferBufferManager::combineLastRaidLine(byte* dest, unsigned remainingbytes)
    {
        // we have to be careful to use the right number of bytes from each sector
        for (unsigned i = 1; i < RAIDPARTS && remainingbytes > 0; ++i)
        {
            if (!raidinputparts[i].empty())
            {
                FilePiece* sector = raidinputparts[i].front();
                unsigned n = std::min<unsigned>(remainingbytes, sector->buf.datalen());
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
                            for (unsigned x = std::min<unsigned>(n, xs->buf.datalen()); x--; )
                            {
                                dest[x] ^= xs->buf.datastart()[x];
                            }
                        }
                    }
                }
                dest += n;
                remainingbytes -= n;
            }
        }
    }

    void TransferBufferManager::rollInputBuffers(unsigned dataToDiscard)
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

    // decrypt, mac downloaded chunk
    void TransferBufferManager::finalize(FilePiece& r)
    {
        byte *chunkstart = r.buf.datastart();
        m_off_t startpos = r.pos;
        m_off_t finalpos = startpos + r.buf.datalen();
        assert(finalpos <= transfer->size);
        if (finalpos != transfer->size)
        {
            finalpos &= -SymmCipher::BLOCKSIZE;
        }

        m_off_t endpos = ChunkedHash::chunkceil(startpos, finalpos);
        unsigned chunksize = static_cast<unsigned>(endpos - startpos);
        SymmCipher *cipher = transfer->transfercipher();
        while (chunksize)
        {
            m_off_t chunkid = ChunkedHash::chunkfloor(startpos);
            ChunkMAC &chunkmac = r.chunkmacs[chunkid];
            if (!chunkmac.finished)
            {
                chunkmac = transfer->chunkmacs[chunkid];
                cipher->ctr_crypt(chunkstart, chunksize, startpos, transfer->ctriv, chunkmac.mac, false, !chunkmac.finished && !chunkmac.offset);
                if (endpos == ChunkedHash::chunkceil(chunkid, transfer->size))
                {
                    LOG_debug << "Finished chunk: " << startpos << " - " << endpos << "   Size: " << chunksize;
                    chunkmac.finished = true;
                    chunkmac.offset = 0;
                }
                else
                {
                    LOG_debug << "Decrypted partial chunk: " << startpos << " - " << endpos << "   Size: " << chunksize;
                    chunkmac.finished = false;
                    chunkmac.offset += chunksize;
                }
            }
            chunkstart += chunksize;
            startpos = endpos;
            endpos = ChunkedHash::chunkceil(startpos, finalpos);
            chunksize = static_cast<unsigned>(endpos - startpos);
        }
    }


    bool TransferBufferManager::tryRaidHttpGetErrorRecovery(unsigned errorConnectionNum)
    {
        if (isRaid())
        {
            raidHttpGetErrorCount[errorConnectionNum] += 1;

            unsigned errorSum = 0;
            for (unsigned i = RAIDPARTS; i--; )
                errorSum += raidHttpGetErrorCount[i];

            if (errorSum >= 3)
                return false;


            // try to switch to 5 connection raid, or a different 5 connections
            if (useOnlyFiveRaidConnections)
            {
                LOG_warn << "5 connection cloudraid shutting down connection " << errorConnectionNum << " due to error, and starting " << unusedRaidConnection << " instead";

                // start up the old unused connection, and cancel this one.  Other connections all have real data since we were already in 5 connection mode
                clearOwningFilePieces(raidinputparts[unusedRaidConnection]);
                clearOwningFilePieces(raidinputparts[errorConnectionNum]);
                clearOwningFilePieces(raidinputparts_recovery[unusedRaidConnection]);
                clearOwningFilePieces(raidinputparts_recovery[errorConnectionNum]);
                raidrequestpartpos[unusedRaidConnection] = raidpartspos;
                raidrequestpartpos[errorConnectionNum] = raidpartspos;

                unusedRaidConnection = errorConnectionNum;
                return true;
            }
            else
            {
                // running in 6 connection mode. switch to 5 connection, while keeping already downloaded input buffers (including thosein progress), discarding and re-requesting skipped sections.
                LOG_warn << "6 connection cloudraid shutting down connection " << errorConnectionNum << " due to error";

                useOnlyFiveRaidConnections = true;
                unusedRaidConnection = errorConnectionNum;
                for (unsigned j = RAIDPARTS; j--; )
                {
                    raidrequestpartpos[j] = raidpartspos;
                    std::deque<FilePiece*> fixedraidinputparts;
                    for (std::deque<FilePiece*>::iterator i = raidinputparts[j].begin(); i != raidinputparts[j].end(); ++i)
                    {
                        if ((*i)->buf.isNull())
                        {
                            delete *i;
                        }
                        else if ((*i)->pos == raidrequestpartpos[j])
                        {
                            fixedraidinputparts.push_back(*i);
                            raidrequestpartpos[j] += (*i)->buf.datalen();
                        }
                        else
                        {
                            raidinputparts_recovery[j][(*i)->pos] = *i;
                        }
                    }
                    raidinputparts[j].swap(fixedraidinputparts);
                }
                return true;
            }
        }
        return false; 
    }

}; // namespace
