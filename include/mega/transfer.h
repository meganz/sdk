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

#include "filefingerprint.h"
#include "backofftimer.h"
#include "http.h"
#include "command.h"
#include "raid.h"

namespace mega {

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

    // position in transfers[type]
    transfer_map::iterator transfers_it;

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

    // context of the async fopen operation
    AsyncIOContext* asyncopencontext;

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
    bool serialize(string*) override;

    // unserialize a Transfer and add it to the transfer map
    static Transfer* unserialize(MegaClient *, string*, transfer_map *);

    // examine a file on disk for video/audio attributes to attach to the file, on upload/download
    void addAnyMissingMediaFileAttributes(Node* node, LocalPath& localpath);

    // whether the Transfer needs to remove itself from the list it's in (for quick shutdown we can skip)
    bool mOptimizedDelete = false;
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

struct MEGA_API DirectReadSlot
{
    m_off_t pos;

    // values to calculate the transfer speed
    static const int MEAN_SPEED_INTERVAL_DS = 100;
    static const int MIN_BYTES_PER_SECOND = 1024 * 15;
    static const int TIMEOUT_DS = 100;
    static const int TEMPURL_TIMEOUT_DS = 3000;

    DirectRead* dr;
    std::vector<HttpReq*> reqs;

    drs_list::iterator drs_it;
    SpeedController speedController;
    m_off_t speed;
    m_off_t meanSpeed;

    bool doio();

    DirectReadSlot(DirectRead*);
    ~DirectReadSlot();

private:
    std::string adjustURLPort(std::string url);
    bool processAnyOutputPieces();
};

struct MEGA_API DirectRead
{
    m_off_t count;
    m_off_t offset;
    m_off_t progress;
    m_off_t nextrequestpos;

    DirectReadBufferManager drbuf;

    DirectReadNode* drn;
    DirectReadSlot* drs;

    dr_list::iterator reads_it;
    dr_list::iterator drq_it;

    void* appdata;

    int reqtag;

    void abort();

    DirectRead(DirectReadNode*, m_off_t, m_off_t, int, void*);
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
    void enqueue(m_off_t, m_off_t, int, void*);

    // dispatch all reads
    void dispatch();

    // schedule next event
    void schedule(dstime);

    // report failure to app and abort or retry all reads
    void retry(const Error &, dstime = 0);

    DirectReadNode(MegaClient*, handle, bool, SymmCipher*, int64_t, const char*, const char*, const char*);
    ~DirectReadNode();
};
} // namespace

#endif
