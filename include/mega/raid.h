/**
 * @file mega/raid.h
 * @brief helper classes for managing cloudraid downloads
 *
 * (c) 2013-2017 by Mega Limited, Auckland, New Zealand
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

#ifndef MEGA_RAID_H
#define MEGA_RAID_H 1

#include "http.h"

namespace mega {

    enum { RAIDPARTS = 6 };
    enum { RAIDSECTOR = 16 };
    enum { RAIDLINE = ((RAIDPARTS - 1)*RAIDSECTOR) };


    // Holds the latest download data received.   Raid-aware.
    // For non-raid files, supplies the received buffer back to the same connection for writing to file (having decrypted and mac'd it),
    // effectively the same way it worked before raid.
    // For raid files, collects up enough input buffers until it can combine them to make a piece of the output file. Once
    // a piece of the output is reconstructed, decrypted and mac'd, the caller can retrieve it for output in the same way as non-raid.
    class MEGA_API TransferBufferManager
    {
    public:

        struct FilePiece {
            m_off_t pos;
            HttpReq::http_buf_t buf;  // owned here
            chunkmac_map chunkmacs;

            FilePiece();
            FilePiece(m_off_t p, size_t len);    // makes a buffer of the specified size (with extra space for SymmCipher::ctr_crypt padding)
            FilePiece(m_off_t p, HttpReq::http_buf_t* b); // takes ownership of the buffer
            void swap(FilePiece& other);
        };

        // call this before starting a transfer. Extracts the vector content
        void setIsRaid(bool b, Transfer* transfer, std::vector<std::string>& tempUrls, m_off_t resumepos, unsigned maxDownloadRequestSize);

        // indicate if the file is raid or not.  Most variation due to raid/non-raid is captured in this class
        bool isRaid();

        // pass a downloaded buffer to the manager, pre-decryption.  Takes ownership of the FilePiece. May update the connection pos (for raid)
        void submitBuffer(unsigned connectionNum, FilePiece* piece);

        // get the file output data to write to the filesystem, on the asyncIO associated with a particular connection (or synchronously).  Buffer ownership is retained here.
        FilePiece* getAsyncOutputBufferPointer(unsigned connectionNum);

        // indicate that the buffer written by asyncIO (or synchronously) can now be discarded.
        void bufferWriteCompleted(unsigned connectionNum);

        // temp URL to use on a given connection.  The same on all connections for a non-raid file.
        const std::string& tempURL(unsigned connectionNum);

        // reference to the tempurls.  Useful for caching raid and non-raid
        const std::vector<std::string>& tempUrlVector() const;

        // Track the progress of http requests sent.  For raid download, tracks the parts.  Otherwise, uses the full file position in the Transfer object, as it used to prior to raid.
        m_off_t transferPos(unsigned connectionNum);

        // Return the size of a particluar part of the file, for raid.  Or for non-raid the size of the whole wile.
        m_off_t transferSize(unsigned connectionNum);

        // Increment the file position in a way that works for raid and non-raid
        void transferPosUpdateMinimum(m_off_t minpos, unsigned connectionNum);

        // Get the file position to upload/download to on the specified connection
        std::pair<m_off_t, m_off_t> nextNPosForConnection(unsigned connectionNum, m_off_t maxDownloadRequestSize, unsigned connectionCount, bool& newBufferSupplied, bool& pauseConnectionForRaid);

        // calculate the exact size of each of the 6 parts of a raid file.  Some may not have a full last sector
        static m_off_t raidPartSize(unsigned part, m_off_t fullfilesize);

        // report a failed connection.  The function tries to switch to 5 connection raid or a different 5 connections.  Two fails without progress and we should fail the transfer as usual
        bool tryRaidHttpGetErrorRecovery(unsigned errorConnectionNum);

        TransferBufferManager();
        ~TransferBufferManager();

    private:

        // parameters to control raid download
        enum { RaidMaxChunksPerRead = 5 };
        enum { RaidReadAheadChunksPausePoint = 8 };
        enum { RaidReadAheadChunksUnpausePoint = 4 };

        Transfer* transfer;
        bool is_raid;
        bool raidKnown;

        // controls buffer sizes used
        unsigned raidLinesPerChunk;

        // If one connection has an error then we can continue with just 5, or if a file is small then 5 connections can be quicker due sending fewer requests per connection
        bool useOnlyFiveRaidConnections;
        
        // only if useOnlyFiveRaidConnections==true
        unsigned unusedRaidConnection;

        // storage server access URLs.  It either has 6 entries for a raid file, or 1 entry for a non-raid file, or empty if we have not looked up a tempurl yet.
        std::vector<std::string> tempurls;
        static std::string emptyReturnString;

        // a connection is paused if it reads too far ahead of others.  This prevents excessive buffer usage
        bool connectionPaused[RAIDPARTS];
        
        // for raid, how far through the raid part we are currently
        m_off_t raidrequestpartpos[RAIDPARTS];                  
        
        // for raid, the http requested data before combining
        std::deque<FilePiece*> raidinputparts[RAIDPARTS];

        // for raid, contains previously downloaded pieces that are beyond where raidinputparts is at. Only used when failing over from 6 Connection.
        std::map<m_off_t, FilePiece*> raidinputparts_recovery[RAIDPARTS];

        // the data to output currently, per connection, raid or non-raid.  re-accessible in case retries are needed
        std::map<unsigned, FilePiece*> asyncoutputbuffers;      
        
        // piece to carry over to the next combine operation, when we don't get pieces that match the chunkceil boundaries
        FilePiece leftoverchunk;                                

        // the point we are at in the raid input parts.  raidinputparts buffers contain data from this point in their part.
        m_off_t raidpartspos;
        
        // the point we are at in the output file.  asyncoutputbuffers contain data from this point.
        m_off_t outputfilepos;

        // In the case of resuming a file, the point we got to in the output might not line up nicely with a sector in an input part.  
        // This field allows us to start reading on a sector boundary but skip outputting data until we match where we got to last time.
        size_t resumewastedbytes;

        // track errors across the connections.  A successful fetch resets the error count for a connection.  Stop trying to recover if we hit 3 total.
        unsigned raidHttpGetErrorCount[RAIDPARTS];

        // take raid input part buffers and combine to form the asyncoutputbuffers
        void combineRaidParts(unsigned connectionNum);
        FilePiece* combineRaidParts(size_t partslen, size_t bufflen, m_off_t filepos, FilePiece& prevleftoverchunk);
        void recoverSectorFromParity(byte* dest, byte* inputbufs[], unsigned offset);
        void combineLastRaidLine(byte* dest, unsigned nbytes);
        void rollInputBuffers(unsigned dataToDiscard);

        // decrypt and mac downloaded chunk
        void finalize(FilePiece& r);

        // get the next pos to start transferring from/to on this connection, for non-raid
        m_off_t nextTransferPos();

    };



} // namespace

#endif
