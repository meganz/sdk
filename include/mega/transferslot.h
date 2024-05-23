/**
 * @file mega/transferslot.h
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

#ifndef MEGA_TRANSFERSLOT_H
#define MEGA_TRANSFERSLOT_H 1


#include "http.h"
#include "node.h"
#include "backofftimer.h"
#include "raid.h"

namespace mega {

// Helper class: Automatically manage backoff timer enablement - if the slot is in progress and has an fa, the transfer's backoff timer should not be considered
// (part of a performance upgrade, so we don't loop all the transfers, calling their bt.update() on every preparewait() )
class TransferSlotFileAccess
{
    std::unique_ptr<FileAccess> fa;
    Transfer* transfer;
public:
    TransferSlotFileAccess(std::unique_ptr<FileAccess>&& p, Transfer* t);
    ~TransferSlotFileAccess();
    void reset(std::unique_ptr<FileAccess>&& p = nullptr);
    inline operator bool() { return bool(fa); }
    inline FileAccess* operator->() { return fa.get(); }
    inline operator FileAccess* () { return fa.get(); }
};

class TransferDbCommitter;

// active transfer
struct MEGA_API TransferSlot
{
    // link to related transfer (never NULL)
    struct Transfer* transfer;

    // associated source/destination file
    TransferSlotFileAccess fa;

    // command in flight to obtain temporary URL
    Command* pendingcmd;

    // transfer attempts are considered failed after XFERTIMEOUT seconds
    // without data flow
    static const dstime XFERTIMEOUT;

    // max time without progress callbacks
    static const dstime PROGRESSTIMEOUT;

    // max request size for downloads and uploads
    static const m_off_t MAX_REQ_SIZE;

    // max request size per raid part for each connection
    static const m_off_t MAX_REQ_SIZE_NEW_RAID;

    // min file size to use new raid engine
    static const m_off_t UPPER_FILESIZE_LIMIT_FOR_SMALLER_CHUNKS;

    // min file size for multiple connections in transfer slot
    static const m_off_t MIN_FILESIZE_FOR_MULTIPLE_CONNECTIONS;

    // maximum gap between chunks for uploads
    static const m_off_t MAX_GAP_SIZE;

    m_off_t maxRequestSize;

    m_off_t progressreported;

    m_time_t lastprogressreport;

    dstime starttime, lastdata;

    // number of consecutive errors
    unsigned errorcount;

    // last error
    error lasterror;

    // maximum number of parallel connections and connection array.
    // shared_ptr for convenient coordination with the worker threads that do encrypt/decrypt on this data.
    int connections;
    vector<std::shared_ptr<HttpReqXfer>> reqs;

    // Keep track of transfer network speed per channel, and overall
    vector<SpeedController> mReqSpeeds;
    SpeedController mTransferSpeed;
    m_off_t speed, meanSpeed;

    // only swap channels twice for speed issues, to prevent endless non-progress (counter is reset if we make overall progress, ie data reassembled)
    unsigned mRaidChannelSwapsForSlowness = 0;

    // Manage download input buffers and file output buffers for file download.  Raid-aware, and automatically performs decryption and mac.
    TransferBufferManager transferbuf;

    bool initCloudRaid(MegaClient* client);
    shared_ptr<CloudRaid> getcloudRaidPtr()
    {
        return cloudRaid;
    }

    // async IO operations
    AsyncIOContext** asyncIO;

    // handle I/O for this slot
    void doio(MegaClient*, TransferDbCommitter&);

    // Prepare an HTTP request
    void prepareRequest(const std::shared_ptr<HttpReqXfer>&, const string& tempURL, m_off_t pos, m_off_t npos);

    // Process a request failure
    // Return values:
    // Error: the ErrorCode. If different from API_OK, it means that the transfer is considered as failed and transfer->failed() should be called.
    // dstime: the backoff for the transfer->failed() call. This value doesn't shadow the backoff param, as it can be different and used on different parts of the code.
    std::pair<error, dstime> processRequestFailure(MegaClient* client, const std::shared_ptr<HttpReqXfer>& httpReq, dstime& backoff, int channel);

    // Process CloudRaid Request
    std::pair<error, dstime> processRaidReq(size_t connection, m_off_t& raidReqProgress);

    // helper for doio to delay connection creation until we know if it's raid or non-raid
    bool createconnectionsonce();

    // disconnect and reconnect all open connections for this transfer
    void disconnect();

    // disconnect and reconnect one connection for this transfer
    void disconnect(unsigned connectionNum);
    void disconnect(const std::shared_ptr<HttpReqXfer>& req);

    // indicate progress
    void progress();

    // Contiguous progress means that all the chunks are finished, from the start of the file up to (but not including) the file position returned.
    m_off_t updatecontiguousprogress();

    // compute the meta MAC based on the chunk MACs
    int64_t macsmac(chunkmac_map*);
    int64_t macsmac_gaps(chunkmac_map*, size_t g1, size_t g2, size_t g3, size_t g4);

    // tslots list position
    transferslot_list::iterator slots_it;

    // slot operation retry timer
    bool retrying;
    BackoffTimerTracked retrybt;

    // transfer failure flag. MegaClient will increment the transfer->errorcount when it sees this set.
    bool failure;

    std::chrono::time_point<std::chrono::system_clock> downloadStartTime;

    TransferSlot(Transfer*);
    ~TransferSlot();

private:
    // New CloudRaid Proxy
    std::shared_ptr<CloudRaid> cloudRaid;

    void toggleport(HttpReqXfer* req);
    bool checkDownloadTransferFinished(TransferDbCommitter& committer, MegaClient* client);
    bool checkMetaMacWithMissingLateEntries();
    bool tryRaidRecoveryFromHttpGetError(unsigned i, bool incrementErrors);

    // returns true if connection haven't received data recently (set incrementErrors) or if slower than other connections (reset incrementErrors)
    bool testForSlowRaidConnection(unsigned connectionNum, bool& incrementErrors);
};

} // namespace

#endif
