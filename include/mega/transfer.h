/**
 * @file mega/transfer.h
 * @brief pending/active up/download ordered by file fingerprint
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

#ifndef MEGA_TRANSFER_H
#define MEGA_TRANSFER_H 1

#include "backofftimer.h"
#include "command.h"
#include "filefingerprint.h"
#include "http.h"
#include "raid.h"

#include <variant>

namespace mega
{
using namespace std::literals;

// helper class for categorizing transfers for upload/download queues
struct TransferCategory
{
    direction_t direction = NONE;
    filesizetype_t sizetype = LARGEFILE;

    TransferCategory(direction_t d, filesizetype_t s);
    TransferCategory(Transfer*);
    unsigned index();
    unsigned directionIndex();
};

class TransferDbCommitter;

#ifdef ENABLE_SYNC
class TransferBackstop
{
    // A class to help track transfers that completed but haven't had
    // putnodes sent yet, and may be abandoned by the owning sync.  If
    // that happens, we still need to inform the app about the transfer final state.

    mutex m;

    // map by transfer tag
    map<int, shared_ptr<SyncTransfer_inClient>> pendingPutnodes;

public:

    void remember(int tag, shared_ptr<SyncTransfer_inClient> wp)
    {
        lock_guard<mutex> g(m);
        pendingPutnodes[tag] = std::move(wp);
    }

    void forget(int tag)
    {
        lock_guard<mutex> g(m);
        pendingPutnodes.erase(tag);
    }

    vector<shared_ptr<SyncTransfer_inClient>> getAbandoned()
    {
        lock_guard<mutex> g(m);
        vector<shared_ptr<SyncTransfer_inClient>> v;
        v.reserve(pendingPutnodes.size());
        for (auto i = pendingPutnodes.begin(); i != pendingPutnodes.end(); )
        {
            if (i->second.use_count() == 1)
            {
                v.push_back(i->second);
                i = pendingPutnodes.erase(i);
            }
            else ++i;
       }
       return v;
    }
};
#endif

// pending/active up/download ordered by file fingerprint (size - mtime - sparse CRC)
struct MEGA_API Transfer : public FileFingerprint
{
    // PUT or GET
    direction_t type;

    // transfer slot this transfer is active in (can be NULL if still queued)
    TransferSlot* slot;

    // files belonging to this transfer - transfer terminates upon its last
    // file is removed
    file_list files;

    shared_ptr<FileDistributor> downloadDistributor;

    // failures/backoff
    unsigned failcount;
    BackoffTimerTracked bt;

    // representative local filename for this transfer
    LocalPath localfilename;

    // progress completed
    m_off_t progresscompleted;

    m_off_t pos;

    // constructed from transferkey and the file's mac data, on upload completion
    FileNodeKey filekey;

    // CTR mode IV
    int64_t ctriv;

    // meta MAC
    int64_t metamac;

    // file crypto key and shared cipher
    std::array<byte, SymmCipher::KEYLENGTH> transferkey;

    // returns a pointer to MegaClient::tmptransfercipher setting its key to the transfer
    // tmptransfercipher key will change: to be used right away: this is not a dedicated SymmCipher for this transfer!
    SymmCipher *transfercipher();

    chunkmac_map chunkmacs;

    // upload handle for file attribute attachment (only set if file attribute queued)
    UploadHandle uploadhandle;

    // When resuming on startup, we need to be sure we are downloading the same file as before (FileFingerprint match is not a guarantee)
    NodeHandle downloadFileHandle;

    // position in transfers[type]
    transfer_multimap::iterator transfers_it;

    // upload result
    unique_ptr<UploadToken> ultoken;

    // backlink to base
    MegaClient* client;
    int tag;

    // signal failure.  Either the transfer's slot or the transfer itself (including slot) will be deleted.
    void failed(const Error&, TransferDbCommitter&, dstime = 0);

    // signal completion
    void complete(TransferDbCommitter&);

    // execute completion
    void completefiles();

    // remove file from transfer including in cache
    void removeTransferFile(error, File* f, TransferDbCommitter* committer);

    void removeCancelledTransferFiles(TransferDbCommitter* committer);

    void removeAndDeleteSelf(transferstate_t finalState);

    // previous wrong fingerprint
    FileFingerprint badfp;

    // transfer state
    bool finished;

    // temp URLs for upload/download data.  They can be cached.  For uploads, a new url means any previously uploaded data is abandoned.
    // downloads can have 6 for raid, 1 for non-raid.  Uploads always have 1
    std::vector<string> tempurls;
    uint8_t discardedTempUrlsSize{};
    static constexpr m_time_t TEMPURL_TIMEOUT_TS{172500};

    // context of the async fopen operation
    unique_ptr<AsyncIOContext> asyncopencontext;

    // timestamp of the start of the transfer
    m_time_t lastaccesstime;

    // priority of the transfer
    uint64_t priority;

    // state of the transfer
    transferstate_t state;

    bool skipserialization;

    Transfer(MegaClient*, direction_t);
    virtual ~Transfer();

    // serialize the Transfer object
    bool serialize(string*) const override;

    // unserialize a Transfer and add it to the transfer map
    static Transfer* unserialize(MegaClient *, string*, transfer_multimap *);

    // examine a file on disk for video/audio attributes to attach to the file, on upload/download
    void addAnyMissingMediaFileAttributes(Node* node, LocalPath& localpath);

    // whether the Transfer needs to remove itself from the list it's in (for quick shutdown we can skip)
    bool mOptimizedDelete = false;

    // whether it is a Transfer for support (i.e., an upload for the Support team)
    bool isForSupport() const;

    // whether the transfer is a Sync upload transfer
    bool mIsSyncUpload = false;

    // Add stats for this transfer to the MEGAclient. The client must be valid at this point.
    bool addTransferStats();

    void collectAndPrintTransferStatsIfLimitReached();

    void discardTempUrlsIfNoDataDownloadedOrTimeoutReached(const direction_t transferDirection,
                                                           const m_time_t currentTime);

    void adjustNonRaidedProgressIfNowIsRaided();

private:
    FileDistributor::TargetNameExistsResolution toTargetNameExistsResolution(CollisionResolution resolution);
};


struct LazyEraseTransferPtr
{
    // This class enables us to relatively quickly and efficiently delete many items from the middle of std::deque
    // By being the class actualy stored in a mega::deque_with_lazy_bulk_erase.
    // Such builk deletion is done by marking the ones to delete, and finally performing those as a single remove_if.
    Transfer* transfer;
    uint64_t preErasurePriority = 0;
    bool erased = false;

    explicit LazyEraseTransferPtr(Transfer* t) : transfer(t) {}
    operator Transfer*&() { return transfer; }
    void erase() { preErasurePriority = transfer->priority; transfer = nullptr; erased = true; }
    bool isErased() const { return erased; }
    bool operator==(const LazyEraseTransferPtr& e) { return transfer && transfer == e.transfer; }
};

class MEGA_API TransferList
{
public:
    static const uint64_t PRIORITY_START = 0x0000800000000000ull;
    static const uint64_t PRIORITY_STEP  = 0x0000000000010000ull;

    typedef deque_with_lazy_bulk_erase<Transfer*, LazyEraseTransferPtr> transfer_list;

    TransferList();
    void addtransfer(Transfer* transfer, TransferDbCommitter&, bool startFirst = false);
    void removetransfer(Transfer *transfer);
    void movetransfer(Transfer *transfer, Transfer *prevTransfer, TransferDbCommitter& committer);
    void movetransfer(Transfer *transfer, unsigned int position, TransferDbCommitter& committer);
    void movetransfer(Transfer *transfer, transfer_list::iterator dstit, TransferDbCommitter&);
    void movetransfer(transfer_list::iterator it, transfer_list::iterator dstit, TransferDbCommitter&);
    void movetofirst(Transfer *transfer, TransferDbCommitter& committer);
    void movetofirst(transfer_list::iterator it, TransferDbCommitter& committer);
    void movetolast(Transfer *transfer, TransferDbCommitter& committer);
    void movetolast(transfer_list::iterator it, TransferDbCommitter& committer);
    void moveup(Transfer *transfer, TransferDbCommitter& committer);
    void moveup(transfer_list::iterator it, TransferDbCommitter& committer);
    void movedown(Transfer *transfer, TransferDbCommitter& committer);
    void movedown(transfer_list::iterator it, TransferDbCommitter& committer);
    error pause(Transfer *transfer, bool enable, TransferDbCommitter& committer);
    transfer_list::iterator begin(direction_t direction);
    transfer_list::iterator end(direction_t direction);
    bool getIterator(Transfer *transfer, transfer_list::iterator&, bool canHandleErasedElements = false);
    std::array<vector<Transfer*>, 6> nexttransfers(std::function<bool(Transfer*)>& continuefunction,
	                                               std::function<bool(direction_t)>& directionContinuefunction,
                                                   TransferDbCommitter& committer);
    Transfer *transferat(direction_t direction, unsigned int position);

    std::array<transfer_list, 2> transfers;
    MegaClient *client;
    uint64_t currentpriority;

private:
    void prepareIncreasePriority(Transfer *transfer, transfer_list::iterator srcit, transfer_list::iterator dstit, TransferDbCommitter& committer);
    void prepareDecreasePriority(Transfer *transfer, transfer_list::iterator it, transfer_list::iterator dstit);
    bool isReady(Transfer *transfer);
};

/**
 * @brief Represents unused connection in a Raided Streaming Transfer.
 * This struct stores the connection number and the reason why is unused
 *
 * @note A bandwidth overquota error (509) cannot affect only a specific raided part, it applies to
 * the entire transfer, so it will be considered as an invalid error in this struct.
 *
 */
struct UnusedConn
{
public:
    enum unusedReason
    {
        UN_INVALID = 0, // INVALID REASON
        UN_NOT_ERR = 1, // CONNECTION CAN BE USED
        UN_DEFINITIVE_ERR = 2, // CONNECTION CANNOT BE USED ANYMORE
    };

    enum connReplacementReason
    {
        CONN_SPEED_LOW_PERFORMANCE = 0, // CONN SPEED PERF IS NOT AS GOOD AS THE REST
        CONN_SPEED_UNDER_THRESHOLD = 1, // CONN SPEED IS UNDER MIN THRESHOLD
        TRANSFER_MEAN_SPEED_UNDER_THRESHOLD = 2, // TRANSFER MEAN SPEED IS UNDER MIN THESHOLD
    };

    /**
     * @brief Returns an unusedReason given a HTTP status code.
     *
     * @return An unusedReason given a HTTP status code.
     */
    static unusedReason getReasonFromHttpStatus(const int httpstatus)
    {
        switch (httpstatus)
        {
            case 200:
                return UN_NOT_ERR;
            case 509:
                assert(false);
                return UN_INVALID;
            default:
                return UN_DEFINITIVE_ERR;
        }
    }

    /**
     * @brief Checks if mReason is an error reason
     *
     * @return true if the reason is `UN_DEFINITIVE_ERR`, otherwise returns false.
     */
    static bool isErrReason(const unusedReason& reason)
    {
        return reason == UN_DEFINITIVE_ERR;
    }

    /**
     * @brief Gets the number of the unused connection.
     *
     * @return The number of the unused connection.
     */
    size_t getNum() const;

    /**
     * @brief Checks if mReason is an error reason
     * @see isErrReason(const unusedReason& reason)
     * @return true if the reason is an error reason, otherwise returns false.
     */
    bool isErrReason() const;

    /**
     * @brief Sets the unused connection info.
     *
     * This method sets the unused state of the connection
     *
     * @param num The number of connection
     * @param reason The reason for marking the connection as unused
     *
     * @return true if the reason is valid and the connection state was updated successfully,
     *         false if the reason is invalid.
     */
    bool setUnused(const size_t num, const unusedReason reason);

    /**
     * @brief Resets the unused connection state.
     */
    void clear();
    /**
     * @brief Checks if reason provided by param is a valid unusedReason.
     *
     * @return true if the reason is valid, otherwise returns false.
     */
    static bool isValidUnusedReason(unusedReason reason)
    {
        return reason == UN_NOT_ERR || reason == UN_DEFINITIVE_ERR;
    }

private:
    unsigned int mNumBackoffRetries{};
    unusedReason mReason{UN_NOT_ERR};
    size_t mNum{};
};

/**
*   @brief Direct Read Slot: slot for DirectRead for connections i/o operations
*
*   Slot for DirectRead.
*   Holds the HttpReq objects for each connection.
*   Loops over every HttpReq to process data and send it to the client.
*
*   @see DirectRead
*   @see DirectReadNode
*   @see RaidBufferManager
*   @see HttpReq
*/
struct MEGA_API DirectReadSlot
{
public:

    /* ===================*\
     *      Constants     *
    \* ===================*/

    /**
    *   @brief Time interval to recalculate speed and mean speed values.
    *
    *   This value is used to watch over DirectRead performance in case it should be retried.
    *
    *   @see DirectReadSlot::watchOverDirectReadPerformance()
    */
    static constexpr int MEAN_SPEED_INTERVAL_DS = 100;

    /**
    *   @brief Min speed value allowed for the transfer.
    *
    *   @see DirectReadSlot::watchOverDirectReadPerformance()
    */
    static constexpr int MIN_BYTES_PER_SECOND = 1024 * 15;

    /**
    *   @brief Time interval allowed without request/connections updates before retrying DirectRead operations (from a new DirectReadSlot).
    *
    *   @see DirectReadNode::schedule()
    */
    static constexpr int TIMEOUT_DS = 100;

    /**
    *   @brief Timeout value for retrying a completed DirectRead in case it doesn't finish properly.
    *
    *   Timeout value when all the requests are done, and everything regarding DirectRead is cleaned up,
    *   before retrying DirectRead operations.
    *
    *   @see DirectReadNode::schedule()
    */
    static constexpr int TEMPURL_TIMEOUT_DS = 3000;

    /**
    *   @brief Min chunk size allowed to be sent to the server/consumer.
    *
    *   Chunk size values (allowed to be submitted to the transfer buffer) will be multiple of this value.
    *   For RAID files (or for any multi-connection approach) this value is used to calculate minChunk value,
    *   having this value divided by the number of connections an padded to RAIDSECTOR
    *
    *   @see DirectReadSlot::mMaxChunkSize
    */
#if defined(__ANDROID__) || defined(USE_IOS)
    static constexpr unsigned MAX_DELIVERY_CHUNK = 16 * 1024 * 1024;
#else
    static constexpr unsigned MAX_DELIVERY_CHUNK = 33 * 1024 * 1024;
#endif

    /**
    *   @brief Min chunk size for a given connection to be throughput-comparable to another connection.
    *
    *   @see DirectReadSlot::searchAndDisconnectSlowestConnection()
    */
    static constexpr unsigned DEFAULT_MIN_COMPARABLE_THROUGHPUT = MAX_DELIVERY_CHUNK;

    /**
     *   @brief Max times a DirectReadSlot is allowed to detect a slow connection respect the others
     * and switch it to the unused one.
     *
     *   @see DirectReadSlot::searchAndDisconnectSlowestConnection()
     */
    static constexpr unsigned MAX_PERF_CONNECTION_SWITCHES = 6;

    /**
     *   @brief Max times a DirectReadSlot is allowed to detect a slower connection than min
     * threshold and switch it to the unused one.
     *
     *   @see DirectReadSlot::searchAndDisconnectSlowestConnection()
     */
    static constexpr unsigned MAX_SLOW_CONNECTION_SWITCHES = 6;

    /**
     *   @brief Max times we can detect same raided part (connection) as slow before retrying entire
     * transfer.
     *
     *   @see DirectReadSlot::searchAndDisconnectSlowestConnection()
     */
    static constexpr unsigned MAX_SLOW_CONNECTION_DETECTED = 6;

    /**
    *   @brief Requests are sent in batch, and no connection is allowed to request the next chunk until the other connections have finished fetching their current one.
    *
    *   Flag value for waiting for all the connections to finish their current chunk requests (with status REQ_INFLIGHT)
    *   before any finished connection can be allowed again to request the next chunk.
    *   Warning: This value is needed to be true in order to gain fairness.
    *            It should only set to false under special conditions or testing purposes with a very fast link.
    *
    *   @see DirectReadSlot::waitForPartsInFlight()
    */
    static constexpr bool WAIT_FOR_PARTS_IN_FLIGHT = true;

    /**
    *   @brief Relation of X Y multiplying factor to consider connection A to be faster than connection B
    *
    *   @param first  X factor for connection A -> X * ConnectionA_throughput
    *   @param second Y factor for connection B -> Y * ConnectionY_throughput
    *
    *   @see DirectReadSlot::mThroughput
    *   @see DirectReadSlot::searchAndDisconnectSlowestConnection()
    */
    static constexpr m_off_t SLOWEST_TO_FASTEST_THROUGHPUT_RATIO[2] { 4, 5 };

    /**
     * @brief Max different failed raided parts of a DirectRead allowed
     *
     * @see DirectReadSlot::retryOnError
     */
    static constexpr int MAX_DIFFERENT_FAILED_RAIDED_CONNS = 1;

    /**
     * @brief Backoff between retries when mNumPerformanceConnectionsSwitches has reached limit
     *
     * @see DirectReadSlot::searchAndDisconnectSlowestConnection
     */
    static constexpr std::chrono::seconds SLOW_DETECTION_BACKOFF = 300s;

    /* ===================*\
     *      Methods       *
    \* ===================*/

    /**
     * @brief Retries the entire direct read transfer upon a failure.
     *
     * This function is called when a transfer has failed, and it is responsible for
     * resetting any failed parts and retrying the operation.
     *
     * @param err The error code that caused the failure
     * @param timeleft The time after which the transfer is retried
     */
    void retryEntireTransfer(const Error& e, dstime timeleft = 0);

    /**
     * @brief Identifies slow connections under minimum threshold and determines the slowest one.
     *
     * This function iterates through all active and completed connections, checks
     * their throughput against a minimum threshold, and identifies those that are
     * considered too slow. Additionally, it determines the slowest one among them.
     *
     * @return A pair containing:
     *         - A set of indices representing connections that are too slow.
     *         - The index of the slowest connection (or invalid index if no slow conns exist).
     */
    std::pair<std::set<size_t>, size_t> searchSlowConnsUnderThreshold();

    /**
     * @brief Detects and manages slow-performing RAIDED parts in a streaming transfer.
     *
     * This method identifies slow connections in a RAID streaming transfer by evaluating
     * their throughput against a minimum threshold. Depending on the situation, it
     * follows these actions:
     * - If multiple connections are slow, the entire transfer is retried.
     * - If a single slow connection cannot be replaced due to limitations or errors,
     *   the entire transfer is retried.
     * - If a single slow connection can be replaced, it switches to the unused connection.
     * - If no slow connections detected, return appropriates values to inform caller about it.
     *
     * @return A pair of booleans:
     *         - The first value indicates whether any slow connection was detected.
     *         - The second value indicates whether the entire transfer will be retried
     */
    std::pair<bool, bool> detectAndManageLowRaidedParts();

    /**
     * @brief Manages low speed raided direct read transfer
     *
     * The function finds the slowest connection, and replaces it by unused connection if posible,
     * Otherwise entire transfer is retried.
     *
     * @note This function is called from watchOverDirectReadPerformance that monitors the speed of
     * the entire raided transfer.
     */
    void onLowSpeedRaidedTransfer();

    /**
    *   @brief Main i/o loop (process every HTTP req from req vector).
    *
    *   @return True if connection must be retried, False to continue as normal.
    */
    bool doio();

    /**
     * @brief Manages a HTTP req failure filtering by httpstatus and performing required action (i.e
     * retry HTTP req)
     */
    void onFailure(std::unique_ptr<HttpReq>& req, const size_t connectionNum);

    /**
    *   @brief Flag value getter to check if a given request is allowed to request a further chunk.
    *
    *   Calling request should be in status REQ_READY.
    *   If wait value is true, it will remain in that status before being allowed to POST.
    *
    *   @return True for waiting, False otherwise.
    *   @see DirectReadSlot::WAIT_FOR_PARTS_IN_FLIGHT
    *   @see DirectReadSlot::mWaitForParts
    */
    bool waitForPartsInFlight() const;

    /**
    *   @brief Number of used connections (all conections but the unused one, if any).
    *
    *   @return Count of used connections
    *   @see mUnusedRaidConnection
    */
    unsigned usedConnections() const;

    /**
    *   @brief Disconnect and reset a connection, meant for connections with a request in REQ_INFLIGHT status.
    *
    *   This method should be called every time a HttpReq should call its disconnect() method.
    *
    *   @param connectionNum Connection index in mReq vector.
    *   @return True if connectionNum is valid and connection has been reset successfuly.
    */
    bool resetConnection(size_t connectionNum = 0);

    /**
     * @brief Retrieves the minimum speed per connection in Bytes per second.
     *
     * This method calculates the minimum allowed speed in Bytes per second for a connection,
     * taking into account whether it's a streaming RAID transfer, and the limits configured in the
     * client (check minstreamingrate).
     *
     * @return The minimum allowed speed per connection in Bytes per second, or 1 if
     * minstreamingrate is Zero.
     */
    unsigned getMinSpeedPerConnBytesPerSec() const;

    /**
     *   @brief Calculate throughput for a given connection: relation of bytes per millisecond.
     *
     *   Throughput is updated every time a new chunk is submitted to the transfer buffer.
     *   Throughput values are reset when a new request starts.
     *
     *   @param connectionNum Connection index in mReq vector.
     *   @return Connection throughput: average number of bytes fetched per millisecond.
     *
     *   @see DirectReadSlot::calcThroughput()
     *   @see DirectReadSlot::mThroughPut
     */
    m_off_t getThroughput(size_t connectionNum) const;

    /**
     * @brief Retries a DirectRead transfer, handling both RAIDED and non RAIDED transfers.
     *
     * This method attempts to retry a DirectRead transfer. If the transfer is non RAIDED,
     * it directly triggers a retry. If it's RAIDED, it replaces that part with unused RAID
     * connection (if posible), and retries only that part.
     *
     * @param connectionNum The connection number to retry.
     */
    void retryOnError(const size_t connectionNum, const int httpstatus);

    /**
     * @brief Check if there are in-flight requests
     *
     * @return True if there are in-flight requests, otherwise returns false
     */
    bool exitDueReqsOnFlight() const;

    /**
     * @brief Determines if the unused connection cannot be reused anymore.
     *
     * @return `true` if the unused connection can not be reused anymore, `false` otherwise.
     */
    bool isUnusedConnectionFailed();

    /**
     * @brief Replace connectionNum by unused connection
     * @param connectionNum The connection number
     * @return true if connection could be replaced, otherwise false
     */
    bool replaceConnectionByUnused(const size_t connectionNum,
                                   const UnusedConn::connReplacementReason reason);

    /**
     * @brief Identifies the slowest and fastest connections (ignoring unused connection)
     *
     * @param connectionNum The index of the connection to compare others against.
     * @return A `std::pair` where:
     *         - The first element is the index of the slowest connection.
     *         - The second element is the index of the fastest connection.
     *         If no valid comparison can be made, both values will be set to `mReqs.size()`.
     */
    std::pair<size_t, size_t> searchSlowestAndFastestConns(const size_t connectionNum) const;

    /**
     * @brief Determines if the slowest connection can be replaced by the unused connection.
     *
     * @param connectionNum The index of the connection being evaluated.
     * @param slowestConnection The index of the slowest connection identified.
     * @param fastestConnection The index of the fastest connection identified.
     * @return `true` if the slowest connection can be switched out for the unused connection,
     *         `false` otherwise.
     */
    bool canSwitchSlowestByUnusedConn(const size_t connectionNum,
                                      const size_t slowestConnection,
                                      const size_t fastestConnection) const;

    /**
     *   @brief Search for the slowest connection and switch it with the actual unused connection.
     *
     *   This method is called between requests:
     *   If WAIT_FOR_PARTS_IN_FLIGHT is true, this ensures to compare among all the connections
     * before they POST again. If WAIT_FOR_PARTS_IN_FLIGHT is false, any connection with a
     * REQ_INFLIGHT status will be ignored for comparison purposes.
     *
     *   @param connectionNum Connection index in mReq vector.
     *   @return True if the slowest connection has been detected and switched with the actual
     * unused connection, False otherwise.
     *   @see DirectReadSlot::DEFAULT_MIN_COMPARABLE_THROUGHPUT
     *   @see DirectReadSlot::MAX_SLOW_CONECCTION_SWITCHES
     */
    bool searchAndDisconnectSlowestConnection(const size_t connectionNum = 0);

    /**
     * @brief Checks if the minimum comparable throughput is met for a specific connection.
     *
     * @param connectionNum The index of the connection
     * @return true if the throughput for the specified connection meets the minimum comparable
     * threshold, otherwise returns false
     */
    bool isMinComparableThroughputForThisConnection(const size_t connectionNum) const
    {
        return mThroughput[connectionNum].second &&
               mThroughput[connectionNum].first >= mMinComparableThroughput;
    }

    /**
    *   @brief Decrease counter for requests with REQ_INFLIGHT status
    *
    *   Valid only for 2+ connections
    *
    *   @return True if counter was decreased, false otherwise (i.e.: if we only have one connection)
    *   @see DirectReadSlot::WAIT_FOR_PARTS_IN_FLIGHT
    *   @see DirectReadSlot:::mNumReqsInflight
    */
    bool decreaseReqsInflight();

    /**
    *   @brief Increase counter for requests with REQ_INFLIGHT status
    *
    *   Valid only for 2+ connections
    *
    *   @return True if counter was increased, false otherwise (i.e.: if we only have one connection)
    *   @see DirectReadSlot::WAIT_FOR_PARTS_IN_FLIGHT
    *   @see DirectReadSlot::mNumReqsInflight
    */
    bool increaseReqsInflight();

    /**
    *   @brief Calculate speed and mean speed for DirectRead aggregated operations.
    *
    *   Controlling progress values are updated when an output piece is delivered to the client.
    *
    *   @return true if Transfer must be retried, false otherwise
    *   @see DirectReadSlot::MEAN_SPEED_INTERVAL_DS
    */
    bool watchOverDirectReadPerformance();

    /**
     * @brief Checks if connection is done
     * @param connectionNum The index of the connection
     *
     * @return true if connection is done, otherwise returns false
     */
    bool isConnectionDone(const size_t connectionNum) const;

    /**
    *   @brief Builds a DirectReadSlot attached to a DirectRead object.
    *
    *   Insert DirectReadSlot object in MegaClient's DirectRead list to start fetching operations.
    */
    DirectReadSlot(DirectRead*);

    /**
    *   @brief Destroy DirectReadSlot and stop any pendant operation.
    *
    *   Aborts DirectRead operations and remove iterator from MegaClient's DirectRead list.
    */
    ~DirectReadSlot();


private:

    /* ===================*\
     *      Attributes    *
    \* ===================*/

    /**
    *   @brief Actual position, updated after combined data is sent to the http server / streaming buffers.
    */
    m_off_t mPos;

    /**
    *   @brief DirectReadSlot iterator from MegaClient's DirectReadSlot list.
    *
    *   @see mega::drs_list
    */
    drs_list::iterator mDrs_it;

    /**
    *   @brief Pointer to DirectRead (equivallent to Transfer for TransferSlot).
    *
    *   @see DirectRead
    */
    DirectRead* mDr;

    /**
    *   @brief Vector for requests, each one corresponding to a different connection.
    *
    *   For RAID files this value will be 6 (one for each part)
    *   For NON-RAID files the default value is 1, but conceptually it could be greater than one
    *   if a parallel-tcp-requests strategy is used or implemented.
    *
    *   @see HttpReq
    */
    std::vector<std::unique_ptr<HttpReq>> mReqs;

    /**
     *   @brief Pair of <Bytes downloaded> and <Total milliseconds> for throughput calculations.
     *
     *   Values are reset by default between different chunk requests.
     *
     *   @see DirectReadSlot::DEFAULT_MIN_COMPARABLE_THROUGHPUT
     */
    std::vector<std::pair<m_off_t, m_off_t>> mThroughput;

    /**
    *   @brief Same pair of values than above, used to calculate the delivery speed.
    *
    *   'Delivery speed' is calculated from the time interval between new output pieces (combined if RAID)
    *   are processed and ready to sent to the client.
    */
    std::pair<m_off_t, m_off_t> mSlotThroughput;

    /**
    *   @brief Timestamp for DirectReadSlot start. Defined in DirectReadSlot constructor.
    */
    std::chrono::steady_clock::time_point mSlotStartTime;

    /**
     * @brief Backoff to be applied when max of bad performance connections have been detected
     *
     * @see DirectReadSlot::searchAndDisconnectSlowestConnection
     */
    std::chrono::steady_clock::time_point mSlowDetetionBackoff;

    /**
     *   @brief Unused connection due to slowness.
     *
     *   This value is used for detecting the slowest start connection and further search and
     * disconnect new slowest connections. It must be synchronized with RaidBufferManager value,
     * which is the one to be cached (so we keep it if reseting the DirectReadSlot).
     */
    UnusedConn mUnusedConn;

    /**
     *   @brief Current total of switches (due performance) done, i.e.: the slowest connection being
     * switched with the unused connection.
     *
     *   @see DirectReadSlot::MAX_PERF_CONNECTION_SWITCHES
     */
    unsigned mNumPerformanceConnectionsSwitches;

    /**
     *   @brief Current total switches (due to slow conn/s) done, i.e.: the slowest connection being
     * switched with the unused connection.
     *
     *   @see DirectReadSlot::MAX_SLOW_CONNECTION_SWITCHES
     */
    unsigned mNumSlowSpeedSwitches;

    /**
     * @brief maps connection id (raided part id) to number of slow speed detections
     */
    std::map<size_t, unsigned> mNumSlowConnectionsDetected;

    /**
    *   @brief Current flag value for waiting for the other connects to finish their TCP requests before any other connection is allowed to request the next chunk.
    *
    *   @see DirectReadSlot::WAIT_FOR_PARTS_IN_FLIGHT
    */
    bool mWaitForParts;

    /**
    *   @brief Current requests with status REQ_INFLIGHT.
    *
    *   @see DirectReadSlot::mWaitForParts
    */
    unsigned mNumReqsInflight;

    /**
    *   @brief Speed controller instance.
    */
    SpeedController mSpeedController;

    /**
    *   @brief Calculated speed by speedController (different from the one calculated by throughput).
    */
    m_off_t mSpeed;

    /**
    *   @brief Calculated mean speed by speedController (different from the one calculated by throughput).
    */
    m_off_t mMeanSpeed;

    /**
    *   @brief Max chunk size allowed to submit the request data to the transfer buffer.
    *
    *   This value is dynamically set depending on the average throughput of each connection,
    *   so the DirectReadSlot will try to submit buffers as big as possible depending
    *   on connection(s) capacity and general limits (memory, etc.).
    *
    *   For NON-RAID files, the upper limit is defined by MAX_DELIVERY_CHUNK.
    *   For RAID files, the upper limit is calculated from MAX_DELIVERY_CHUNK divided by the number of raid parts and padding it to RAIDSECTOR value.
    *
    *   @see DirectReadSlot::MAX_DELIVERY_CHUNK
    */
    unsigned mMaxChunkSize;

    /**
     *   @brief Min submitted bytes needed for a connection to be throughput-comparable with others.
     *
     *   This value is set on global delivery throughput.
     *   Ex:
     *       1. Raid file, each connection submits 1MB.
     *       2. Delivery chunk size from combined data is 5MB -> min comparable throughtput until
     * next deliver will be 5MB.
     *
     *   @see searchAndDisconnectSlowestConnection()
     *   @see processAnyOutputPieces()
     */
    m_off_t mMinComparableThroughput;

    /**
    *   @brief Max chunk size submitted from one of the connections to the transfer buffer.
    *
    *   For NON-RAID files, this value is got from MAX_DELIVERY_CHUNK (so submitting buffer size and delivering buffer size are the same).
    *   For RAID files, this value is calculated from MAX_DELIVERY_CHUNK divided by the number of raid parts and padding it to RAIDSECTOR value.
    *
    *   @see DirectReadSlot::MAX_DELIVERY_CHUNK
    */
    unsigned mMaxChunkSubmitted;


    /* =======================*\
     *   Private aux methods  *
    \* =======================*/

    /**
     * @brief Checks if the maximum number of slow connection switches has been reached or exceeded
     * based on reason param.
     *
     * @param reason the reason for checking if we have reached max connection switched.
     *  - if CONN_SPEED_LOW_PERFORMANCE comparisson will be done against
     * mNumPerformanceConnectionsSwitches
     *  - if CONN_SPEED_UNDER_THRESHOLD or TRANSFER_MEAN_SPEED_UNDER_THRESHOLD comparisson will be
     * done against mNumSlowSpeedSwitches
     *
     * @return `true` if the maximum number of slow connection switches has been reached or
     * exceeded, `false` otherwise.
     */
    bool maxUnusedConnSwitchesReached(const UnusedConn::connReplacementReason reason) const
    {
        switch (reason)
        {
            case UnusedConn::CONN_SPEED_LOW_PERFORMANCE:
                return mNumPerformanceConnectionsSwitches >
                       DirectReadSlot::MAX_PERF_CONNECTION_SWITCHES;
            case UnusedConn::CONN_SPEED_UNDER_THRESHOLD:
            case UnusedConn::TRANSFER_MEAN_SPEED_UNDER_THRESHOLD:
                return mNumSlowSpeedSwitches > DirectReadSlot::MAX_SLOW_CONNECTION_SWITCHES;
        }
        assert(false);
        return false;
    }

    void increaseUnusedConnSwitches(const UnusedConn::connReplacementReason reason)
    {
        switch (reason)
        {
            case UnusedConn::CONN_SPEED_LOW_PERFORMANCE:
                ++mNumPerformanceConnectionsSwitches;
                if (maxUnusedConnSwitchesReached(reason))
                {
                    mSlowDetetionBackoff = std::chrono::steady_clock::now();
                }
                break;
            case UnusedConn::CONN_SPEED_UNDER_THRESHOLD:
            case UnusedConn::TRANSFER_MEAN_SPEED_UNDER_THRESHOLD:
                ++mNumSlowSpeedSwitches;
                break;
        }
        assert(false);
    }

    /**
    *   @brief Adjust URL port in URL for streaming (8080).
    *
    *   @param url Current URL.
    *   @return New URL with updated port.
    */
    std::string adjustURLPort(std::string url);

    /**
    *   @brief Try processing new output pieces (generated by submitted buffers, fed by each connection request).
    *
    *    - Combine output pieces for RAID files if needed.
    *    - Deliver final combined chunks to the client.
    *
    *   @return True if DirectReadSlot can continue, False if some delivery has failed.
    *   @see DirectReadSlot::MAX_DELIVERY_CHUNK
    *   @see MegaApiImpl::pread_data()
    */
    bool processAnyOutputPieces();

    /**
    *   @brief Aux method to calculate the throughput: numBytes for 1 unit of timeCount.
    *
    *
    *   @param numBytes Total numBytes received for timeCount period.
    *   @param timeCount Time period spent for receiving numBytes.
    *   @return Throughput: average number of bytes fetched for timeCount=1.
    *
    *   @see DirectReadSlot::mThroughPut
    */
    m_off_t calcThroughput(m_off_t numBytes, m_off_t timeCount) const;
};

struct MEGA_API DirectRead
{
    // Type for the callback when a data is recieved
    struct Data
    {
        Data(byte* buffer, m_off_t len, m_off_t offset, m_off_t speed, m_off_t meanSpeed):
            buffer{buffer},
            len{len},
            offset{offset},
            speed{speed},
            meanSpeed{meanSpeed}
        {}

        byte* buffer{nullptr};
        m_off_t len{0};
        m_off_t offset{0};
        m_off_t speed{0};
        m_off_t meanSpeed{0};
        bool ret{false}; // Callback sets and tells a success or a failure
    };

    // Type for the callback on a failure
    struct Failure
    {
        Failure(const Error& e, int retry, dstime timeLeft):
            e{e},
            retry{retry},
            timeLeft{timeLeft}
        {}

        Error e;
        int retry{0};
        dstime timeLeft{0};
        dstime ret{0}; // Callback sets and tells the interval for a retry
    };

    // Type for the callback to revoke itself
    struct Revoke
    {
        Revoke(void* appData):
            appdata{appData}
        {}

        void* appdata{nullptr}; // appdata to match the callback
        bool ret{false}; // Callback sets and tells if it is revoked or not
    };

    // Type for the callback to tell if it is still valid (not revoked)
    struct IsValid
    {
        bool ret{false}; // Callback sets
    };

    using CallbackParam = std::variant<Data, Failure, Revoke, IsValid>;

    using Callback = std::function<void(CallbackParam&)>;

    m_off_t count;
    m_off_t offset;
    m_off_t progress;
    m_off_t nextrequestpos;

    DirectReadBufferManager drbuf;

    DirectReadNode* drn;
    DirectReadSlot* drs;

    dr_list::iterator reads_it;
    dr_list::iterator drq_it;

    int reqtag;

    Callback callback;

    void abort();
    m_off_t drMaxReqSize() const;

    void revokeCallback(void* appData);

    bool onData(byte* buffer, m_off_t len, m_off_t theOffset, m_off_t speed, m_off_t meanSpeed);

    dstime onFailure(const Error& e, int retry, dstime timeLeft);

    bool hasValidCallback();

    DirectRead(DirectReadNode*, m_off_t, m_off_t, int, Callback&& callback);
    ~DirectRead();
};

struct MEGA_API DirectReadNode
{
    handle h;
    bool p;
    string publicauth;
    string privateauth;
    string chatauth;
    m_off_t partiallen;
    dstime partialstarttime;

    std::vector<std::string> tempurls;

    m_off_t size;

    class CommandDirectRead* pendingcmd;

    int retries;

    int64_t ctriv;
    SymmCipher symmcipher;

    dr_list reads;

    MegaClient* client;

    handledrn_map::iterator hdrn_it;
    dsdrn_map::iterator dsdrn_it;

    // API command result
    void cmdresult(const Error&, dstime = 0);

    // enqueue new read
    DirectRead* enqueue(m_off_t, m_off_t, int, DirectRead::Callback&& callback);

    // dispatch all reads
    void dispatch();

    // schedule next event
    void schedule(dstime);

    // report failure to app and abort or retry all reads
    void retry(const Error &, dstime = 0);

    DirectReadNode(MegaClient*, handle, bool, SymmCipher*, int64_t, const char*, const char*, const char*);
    ~DirectReadNode();
};

} // namespace mega

#endif
