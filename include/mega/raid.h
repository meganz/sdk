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

#define ISNEWRAID_DEFVALUE 1

#include "http.h"
#include "utils.h"

namespace mega {

    enum { RAIDPARTS = 6 };
    enum { EFFECTIVE_RAIDPARTS = 5 }; // A file is divided by 5 parts plus parity part (to assemble the other parts if one of them is missing)
    enum { RAIDSECTOR = 16 };
    enum { RAIDLINE = (EFFECTIVE_RAIDPARTS * RAIDSECTOR) };


    // Holds the latest download data received.   Raid-aware.   Suitable for file transfers, or direct streaming.
    // For non-raid files, supplies the received buffer back to the same connection for writing to file (having decrypted and mac'd it),
    // effectively the same way it worked before raid.
    // For raid files, collects up enough input buffers until it can combine them to make a piece of the output file.
    // Once a piece of the output is reconstructed the caller can access it with getAsyncOutputBufferPointer().
    // Once that piece is no longer needed, call bufferWriteCompleted to indicate that it can be deallocated.
    class MEGA_API RaidBufferManager
    {
    public:

        struct FilePiece {
            m_off_t pos;
            HttpReq::http_buf_t buf;  // owned here
            chunkmac_map chunkmacs;

            std::condition_variable finalizedCV;
            bool finalized = false;

            FilePiece();
            FilePiece(m_off_t p, size_t len);    // makes a buffer of the specified size (with extra space for SymmCipher::ctr_crypt padding)
            FilePiece(m_off_t p, HttpReq::http_buf_t* b); // takes ownership of the buffer
            void swap(FilePiece& other);

            // decrypt & mac
            bool finalize(bool parallel, m_off_t filesize, int64_t ctriv, SymmCipher *cipher, chunkmac_map* source_chunkmacs);

        };

        // Min last request chunk (to avoid small chunks to be requested)
#if defined(__ANDROID__) || defined(USE_IOS)
        static constexpr size_t MIN_LAST_CHUNK = 512 * 1024;
#else
        static constexpr size_t MIN_LAST_CHUNK = 10 * 1024 * 1024;
#endif
        // Max last request chunk (otherwise split it in two)
#if defined(__ANDROID__) || defined(USE_IOS)
        static constexpr size_t MAX_LAST_CHUNK = 1 * 1024 * 1024;
#else
        static constexpr size_t MAX_LAST_CHUNK = 16 * 1024 * 1024;
#endif

        // To be called within CloudRaid tests when a lower speed is needed or we need to trigger 403/404/timeout errors
        void disableAvoidSmallLastRequest();

        // call this before starting a transfer. Extracts the vector content
        void setIsRaid(const std::vector<std::string>& tempUrls, m_off_t resumepos, m_off_t readtopos, m_off_t filesize, m_off_t maxDownloadRequestSize, bool isNewRaid = ISNEWRAID_DEFVALUE);

        // indicate if the file is raid or not.  Most variation due to raid/non-raid is captured in this class
        bool isRaid() const;

        // indicate if the file is new raid (CloudRaidProxy) or not
        bool isNewRaid() const;

        // Is this the connection we are not using
        bool isUnusedRaidConection(unsigned connectionNum) const;

        // Is this connection unable to continue currently because other connections are too far behind
        bool isRaidConnectionProgressBlocked(unsigned connectionNum) const;

        // in case URLs expire, use this to update them and keep downloading without wasting any data
        void updateUrlsAndResetPos(const std::vector<std::string>& tempUrls);

        // pass a downloaded buffer to the manager, pre-decryption.  Takes ownership of the FilePiece. May update the connection pos (for raid)
        void submitBuffer(unsigned connectionNum, FilePiece* piece);

        // get the file output data to write to the filesystem, on the asyncIO associated with a particular connection (or synchronously).  Buffer ownership is retained here.
        std::shared_ptr<RaidBufferManager::FilePiece> getAsyncOutputBufferPointer(unsigned connectionNum);

        // indicate that the buffer written by asyncIO (or synchronously) can now be discarded.
        void bufferWriteCompleted(unsigned connectionNum, bool succeeded);

        // temp URL to use on a given connection.  The same on all connections for a non-raid file.
        const std::string& tempURL(unsigned connectionNum);

        // reference to the tempurls.  Useful for caching raid and non-raid
        const std::vector<std::string>& tempUrlVector() const;

        // Track the progress of http requests sent.  For raid download, tracks the parts.  Otherwise, uses the position through the full file.
        virtual m_off_t& transferPos(unsigned connectionNum);

        // start this part off again (eg. after abandoning slowest connection)
        void resetPart(unsigned connectionNum);

        // Return the size of a particluar part of the file, for raid.  Or for non-raid the size of the whole wile.
        m_off_t transferSize(unsigned connectionNum);

        // Get the file position to upload/download to on the specified connection
        std::pair<m_off_t, m_off_t> nextNPosForConnection(unsigned connectionNum, bool& newBufferSupplied, bool& pauseConnectionForRaid);

        // calculate the exact size of each of the 6 parts of a raid file.  Some may not have a full last sector
        static m_off_t raidPartSize(unsigned part, m_off_t fullfilesize);

        // report a failed connection.  The function tries to switch to 5 connection raid or a different 5 connections.  Two fails without progress and we should fail the transfer as usual
        bool tryRaidHttpGetErrorRecovery(unsigned errorConnectionNum, bool incrementErrors);

        // indicate that this connection has responded with headers, and see if we now know which is the slowest connection, and make that the unused one
        bool detectSlowestRaidConnection(unsigned thisConnection, unsigned& slowestConnection);

        // Set the unused raid connection [0 - RAIDPARTS)
        bool setUnusedRaidConnection(unsigned newUnusedRaidConnection);

        // Which raid connection is not being used for downloading
        unsigned getUnusedRaidConnection() const;

        // returns how far we are through the file on average, including uncombined data
        m_off_t progress() const;

        RaidBufferManager();
        ~RaidBufferManager();

    private:

        // parameters to control raid download
        enum { RaidMaxChunksPerRead = 5 };
        enum { RaidReadAheadChunksPausePoint = 8 };
        enum { RaidReadAheadChunksUnpausePoint = 4 };

        bool is_raid;
        bool is_newRaid;
        bool raidKnown;
        m_off_t deliverlimitpos;   // end of the data that the client requested
        m_off_t acquirelimitpos;   // end of the data that we need to deliver that (can be up to the next raidline boundary)
        m_off_t fullfilesize;      // end of the file

        // controls buffer sizes used
        unsigned raidLinesPerChunk;

        // of the six raid URLs, which 5 are we downloading from
        unsigned unusedRaidConnection;

        // storage server access URLs.  It either has 6 entries for a raid file, or 1 entry for a non-raid file, or empty if we have not looked up a tempurl yet.
        std::vector<std::string> tempurls;
        std::string emptyReturnString;

        // a connection is paused if it reads too far ahead of others.  This prevents excessive buffer usage
        bool connectionPaused[RAIDPARTS];

        // for raid, how far through the raid part we are currently
        m_off_t raidrequestpartpos[RAIDPARTS];

        // for raid, the http requested data before combining
        std::deque<FilePiece*> raidinputparts[RAIDPARTS];

        // the data to output currently, per connection, raid or non-raid. Re-accessible in case retries are needed
        std::map<unsigned, std::shared_ptr<FilePiece>> asyncoutputbuffers;

        // piece to carry over to the next combine operation, when we don't get pieces that match the chunkceil boundaries
        FilePiece leftoverchunk;

        // the point we are at in the raid input parts.  raidinputparts buffers contain data from this point in their part.
        m_off_t raidpartspos;

        // the point we are at in the output file.  asyncoutputbuffers contain data from this point.
        m_off_t outputfilepos;

        // the point we started at in the output file.
        m_off_t startfilepos;

        // In the case of resuming a file, the point we got to in the output might not line up nicely with a sector in an input part.
        // This field allows us to start reading on a sector boundary but skip outputting data until we match where we got to last time.
        size_t resumewastedbytes;

        // track errors across the connections.  A successful fetch resets the error count for a connection.  Stop trying to recover if we hit 3 total.
        unsigned raidHttpGetErrorCount[RAIDPARTS];

        bool connectionStarted[RAIDPARTS];

        // For test hooks, disable avoid small requests when we need a lower speed and trigger 404/403/timeout errors
        bool mDisableAvoidSmallLastRequest;

        // take raid input part buffers and combine to form the asyncoutputbuffers
        void combineRaidParts(unsigned connectionNum);
        FilePiece* combineRaidParts(size_t partslen, size_t bufflen, m_off_t filepos, FilePiece& prevleftoverchunk);
        void recoverSectorFromParity(byte* dest, byte* inputbufs[], unsigned offset);
        void combineLastRaidLine(byte* dest, size_t nbytes);
        void rollInputBuffers(size_t dataToDiscard);
        virtual void bufferWriteCompletedAction(FilePiece& r);

        // decrypt and mac downloaded chunk.  virtual so Transfer and DirectNode derivations can be different
        // calcOutputChunkPos is used to figure out how much of the available data can be passed to it
        virtual void finalize(FilePiece& r) = 0;
        virtual m_off_t calcOutputChunkPos(m_off_t acquiredpos) = 0;

        friend class DebugTestHook;
    };


    class MEGA_API TransferBufferManager : public RaidBufferManager
    {
    public:
        // call this before starting a transfer. Extracts the vector content
        void setIsRaid(Transfer* transfer, const std::vector<std::string> &tempUrls, m_off_t resumepos, m_off_t maxDownloadRequestSize, bool isNewRaid = ISNEWRAID_DEFVALUE);

        // Track the progress of http requests sent.  For raid download, tracks the parts.  Otherwise, uses the full file position in the Transfer object, as it used to prior to raid.
        m_off_t& transferPos(unsigned connectionNum) override;

        // Get the file position to upload/download to on the specified connection
        std::pair<m_off_t, m_off_t> nextNPosForConnection(unsigned connectionNum, m_off_t maxDownloadRequestSize, unsigned connectionCount, bool& newBufferSupplied, bool& pauseConnectionForRaid, m_off_t uploadspeed);

        TransferBufferManager();

    private:

        Transfer* transfer;

        // decrypt and mac downloaded chunk
        void finalize(FilePiece& r) override;
        m_off_t calcOutputChunkPos(m_off_t acquiredpos) override;
        void bufferWriteCompletedAction(FilePiece& r) override;

        friend class DebugTestHook;
    };

    class MEGA_API DirectReadBufferManager : public RaidBufferManager
    {
    public:

        // Track the progress of http requests sent.  For raid download, tracks the parts.  Otherwise, uses the full file position in the Transfer object, as it used to prior to raid.
        m_off_t& transferPos(unsigned connectionNum) override;

        DirectReadBufferManager(DirectRead* dr);

    private:

        DirectRead* directRead;

        // decrypt and mac downloaded chunk
        void finalize(FilePiece& r) override;
        m_off_t calcOutputChunkPos(m_off_t acquiredpos) override;

        friend class DebugTestHook;
    };

    class MEGA_API CloudRaid
    {
    private:
        class CloudRaidImpl;
        const CloudRaidImpl* mPimpl() const { return m_pImpl.get(); }
        CloudRaidImpl* mPimpl() { return m_pImpl.get(); }

        std::unique_ptr<CloudRaidImpl> m_pImpl{};
        bool mShown{};

    public:
        CloudRaid();
        CloudRaid(TransferSlot* tslot, MegaClient* client, int connections);
        ~CloudRaid();

        /* Instance control functionality */
        bool isShown() const;

        /* TransferSlot functionality for RaidProxy */
        bool disconnect(const std::shared_ptr<HttpReqXfer>& req);
        bool prepareRequest(const std::shared_ptr<HttpReqXfer>& req, const string& tempURL, m_off_t pos, m_off_t npos);
        bool post(const std::shared_ptr<HttpReqXfer>& req);
        bool onRequestFailure(const std::shared_ptr<HttpReqXfer>& req, uint8_t part, dstime& backoff);
        bool setTransferFailure(::mega::error e = API_EAGAIN, dstime backoff = 0);
        std::pair<::mega::error, dstime> checkTransferFailure();
        bool setUnusedRaidConnection(uint8_t part, bool addToFaultyServers);
        uint8_t getUnusedRaidConnection() const;
        m_off_t transferred(const std::shared_ptr<HttpReqXfer>& req) const;

        /* RaidProxy functionality for TransferSlot */
        bool init(TransferSlot* tslot, MegaClient* client, int connections);
        bool balancedRequest(int connection, const std::vector<std::string>& tempUrls, size_t cfilesize, m_off_t cstart, size_t creqlen);
        bool removeRaidReq(int connection);
        bool resumeAllConnections();
        bool raidReqDoio(int connection);
        bool stop();
        m_off_t progress() const;
        m_off_t readData(int connection, byte* buf, m_off_t len);
    };

} // namespace

#endif
