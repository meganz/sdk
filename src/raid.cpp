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
        , raidpartspos(0)
        , outputfilepos(0)
        , resumewastedbytes(0)
    {
        for (int i = RAIDPARTS; i--; )
        {
            raidrequestpartpos[i] = 0;
        }
    }

    TransferBufferManager::~TransferBufferManager()
    {
        for (int i = RAIDPARTS; i--; )
        {
            while (!raidinputparts[i].empty())
            {
                delete raidinputparts[i].front();
                raidinputparts[i].pop_front();
            }
        }
        for (std::map<unsigned, FilePiece*>::iterator i = asyncoutputbuffers.begin(); i != asyncoutputbuffers.end(); ++i)
        {
            delete i->second;
        }
    }

    void TransferBufferManager::setIsRaid(bool b, Transfer* t, std::vector<std::string>& tempUrls, m_off_t resumepos)
    {
        is_raid = b;
        raidKnown = true;
        transfer = t;
        tempurls.swap(tempUrls);  // later change this to std::move
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
    void TransferBufferManager::submitBuffer(unsigned connectionNum, m_off_t partpos, HttpReq::http_buf_t* buf)
    {
        if (isRaid())
        {
            assert(connectionNum < RAIDPARTS);

            raidinputparts[connectionNum].push_back(new FilePiece(partpos, buf));
            combineRaidParts(connectionNum);
        }
        else
        {
            FilePiece* newpiece = new FilePiece(partpos, buf);
            finalize(*newpiece);
            assert(asyncoutputbuffers[connectionNum] == NULL);
            asyncoutputbuffers[connectionNum] = newpiece;
        }
    }

    TransferBufferManager::FilePiece* TransferBufferManager::getAsyncOutputBufferPointer(unsigned connectionNum)
    {
        std::map<unsigned, FilePiece*>::iterator i = asyncoutputbuffers.find(connectionNum);
        return (i == asyncoutputbuffers.end()) ? NULL : i->second;
    }

    TransferBufferManager::FilePiece* TransferBufferManager::getAnySubsequentAsyncOutputBufferPointer(unsigned connectionNum)
    {
        if (isRaid() && getAsyncOutputBufferPointer(connectionNum) == NULL)
        {
            combineRaidParts(connectionNum);
            return getAsyncOutputBufferPointer(connectionNum);
        }
        return NULL;
    }


    void TransferBufferManager::bufferWriteCompleted(unsigned connectionNum)
    {
        assert(asyncoutputbuffers[connectionNum]);
        FilePiece* rp = asyncoutputbuffers[connectionNum];
        if (rp)
        {
            FilePiece& r = *rp;
            for (chunkmac_map::iterator it = r.chunkmacs.begin(); it != r.chunkmacs.end(); it++)    // this code moved from TransferSlot::~TransferSlot (one async, one sync).  Also from TransferSlot::doio for REQ_SUCCESS GET sync write, and from REQ_ASYNCIO async write
            {
                transfer->chunkmacs[it->first] = it->second;
            }
            r.chunkmacs.clear();
            transfer->progresscompleted += r.buf.datalen();
            LOG_debug << "Cached data at: " << r.pos << "   Size: " << r.buf.datalen();
            delete asyncoutputbuffers[connectionNum];
            asyncoutputbuffers[connectionNum] = NULL;
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

    m_off_t TransferBufferManager::nextTransferPos(unsigned connectionNum)
    {
        if (!isRaid())
        {
            // original logic for non-raid: since several connections can be downloading pieces of the file, skip ahead if any of those have finished downloading (based on chunkmacs
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
        else
        {
            // for raid, we don't skip ahead on any particular connection (yet).  pos[i] is already updated when the chunk was downloaded with pos.CurrentPosUpdateMinimum
            // todo: for max download speed, use all six channels with round-robin parts skipped 
            return transferPos(connectionNum);
        }
    }

    m_off_t TransferBufferManager::nextNPosForConnection(unsigned connectionNum, m_off_t maxDownloadRequestSize, unsigned connectionCount, bool& skipForRaid)
    {
        skipForRaid = false;
        if (!isRaid())
        {
            m_off_t npos = ChunkedHash::chunkceil(nextTransferPos(connectionNum), transferSize(connectionNum));

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
                m_off_t reqSize = npos - transferPos(connectionNum);
                while (npos < transferSize(connectionNum)
                    && reqSize <= maxReqSize
                    && (it == transfer->chunkmacs.end()
                        || (!it->second.finished && !it->second.offset)))
                {
                    npos = ChunkedHash::chunkceil(npos, transferSize(connectionNum));
                    reqSize = npos - transferPos(connectionNum);
                    it = transfer->chunkmacs.find(npos);
                }
                LOG_debug << "Downloading chunk of size " << reqSize << " on connection " << connectionNum;
            }
            return npos;
        }
        else  // raid
        {
            m_off_t curpos = transferPos(connectionNum);
            m_off_t skipPointBlockPos = curpos - (curpos % (RaidLinesPerChunk * RAIDSECTOR * RAIDPARTS));
            m_off_t skipPoint = skipPointBlockPos + connectionNum * RaidLinesPerChunk * RAIDSECTOR;
            m_off_t npos;
            if (curpos < skipPoint)
            {
                npos = skipPoint;
            }
            else if (curpos < skipPoint + RaidLinesPerChunk * RAIDSECTOR)
            {
                skipForRaid = true;
                npos = skipPoint + RaidLinesPerChunk * RAIDSECTOR;
            }
            else
            {
                npos = skipPoint + RaidLinesPerChunk * RAIDSECTOR * RAIDPARTS;
            }
            return std::min<m_off_t>(npos, transferSize(connectionNum));
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
                assert(r.pos + r.buf.start == raidpartspos);  // check all are in sync at the front 
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
            raidpartspos += partslen;
            LOG_debug << "raidpartspos: " << raidpartspos;
            sumdatalen -= partslen * (RAIDPARTS - 1);
            outputfilepos += partslen * (RAIDPARTS - 1) + leftoverchunk.buf.datalen();
            byte* dest = outputrec->buf.datastart() + partslen * (RAIDPARTS - 1) + leftoverchunk.buf.datalen();
            leftoverchunk.swap(FilePiece());  // this data is entirely included in the outputrec now, so discard and reset

            if (processToEndOfFile && sumdatalen > 0)
            {
                // fill in the last of the buffer with non-full sectors from the end of the file
                assert(outputfilepos + sumdatalen == transfer->size);
                byte const* inputbufs[RAIDPARTS];
                for (unsigned j = RAIDPARTS; j--; )
                {
                    inputbufs[j] = raidinputparts[j].empty() || raidinputparts[j].front()->buf.isNull() ? NULL : raidinputparts[j].front()->buf.datastart();
                }
                for (unsigned j = 1; j < RAIDPARTS && sumdatalen > 0; ++j)
                {
                    unsigned n = std::min<unsigned>(RAIDSECTOR, sumdatalen);
                    if (inputbufs[j])
                        memcpy(dest, inputbufs[j], n);
                    else
                        recoverSectorFromParity(dest, inputbufs, 0);  // this one may write RAIDSECTOR bytes but the buffer is big enough
                    dest += n;
                    sumdatalen -= n;
                }
            }
            else if (!processToEndOfFile && outputfilepos > macchunkpos)
            {
                // mac processing must be done in chunks, delimited by chunkfloor and chunkceil.  If we don't have the right amount then hold the remainder over for next time.
                size_t excessdata = static_cast<size_t>(outputfilepos - macchunkpos);
                leftoverchunk.swap(FilePiece(outputfilepos - excessdata, excessdata));
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
                delete outputrec;  // this would happen if we got some data to process on all channels, but not enough to reach the next chunk boundary yet (and combined data is in leftoverchunk)
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
            byte const* inputbufs[RAIDPARTS];
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

            // remove finished input buffers
            for (unsigned i = RAIDPARTS; i--; )
            {
                FilePiece& ip = *raidinputparts[i].front();
                ip.buf.start += partslen;
                if (ip.buf.start >= ip.buf.end)
                {
                    delete raidinputparts[i].front();
                    raidinputparts[i].pop_front();
                } 
            }
        }
        return result;
    }

    void TransferBufferManager::recoverSectorFromParity(byte* dest, byte const* inputbufs[], unsigned offset)
    {
        assert(sizeof(__int64)*2 == RAIDSECTOR);
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
                    *(__int64*)dest ^= *(__int64*)(inputbufs[i] + offset);
                    *(__int64*)(dest + sizeof(__int64)) ^= *(__int64*)(inputbufs[i] + offset + sizeof(__int64));
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


}; // namespace
