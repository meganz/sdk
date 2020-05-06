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

class DBTableTransactionCommitter;

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

    m_off_t maxRequestSize;

    m_off_t progressreported;

    m_off_t progresscontiguous;

    m_time_t lastprogressreport;

    dstime starttime, lastdata;

    SpeedController speedController;
    m_off_t speed, meanSpeed;

    // number of consecutive errors
    unsigned errorcount;

    // last error
    error lasterror;

    // file attribute string
    string fileattrstring;

    // file attributes mutable
    int fileattrsmutable;

    // maximum number of parallel connections and connection array. 
    // shared_ptr for convenient coordination with the worker threads that do encrypt/decrypt on this data.
    int connections;
    vector<std::shared_ptr<HttpReqXfer>> reqs;

    // Manage download input buffers and file output buffers for file download.  Raid-aware, and automatically performs decryption and mac.
    TransferBufferManager transferbuf;

    // async IO operations
    AsyncIOContext** asyncIO;

    // handle I/O for this slot
    void doio(MegaClient*, DBTableTransactionCommitter&);

    // helper for doio to delay connection creation until we know if it's raid or non-raid
    bool createconnectionsonce();

    // disconnect and reconnect all open connections for this transfer
    void disconnect();

    // indicate progress
    void progress();

    // update the contiguous progress
    void updatecontiguousprogress();

    // compute the meta MAC based on the chunk MACs
    int64_t macsmac(chunkmac_map*);

    // tslots list position
    transferslot_list::iterator slots_it;

    // slot operation retry timer
    bool retrying;
    BackoffTimerTracked retrybt;

    // transfer failure flag. MegaClient will increment the transfer->errorcount when it sees this set.
    bool failure;
    
    TransferSlot(Transfer*);
    ~TransferSlot();

private:
    void toggleport(HttpReqXfer* req);
    bool tryRaidRecoveryFromHttpGetError(unsigned i);
    bool checkTransferFinished(DBTableTransactionCommitter& committer, MegaClient* client);
};
} // namespace

#endif
