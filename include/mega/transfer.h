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

namespace mega {
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
    BackoffTimer bt;

    // representative local filename for this transfer
    string localfilename;

    // progress completed
    m_off_t progresscompleted;

    m_off_t pos;

    byte filekey[FILENODEKEYLENGTH];

    // CTR mode IV
    int64_t ctriv;

    // meta MAC
    int64_t metamac;

    // file crypto key and shared cipher
    byte transferkey[SymmCipher::KEYLENGTH];
    SymmCipher *transfercipher();

    chunkmac_map chunkmacs;

    // upload handle for file attribute attachment (only set if file attribute queued)
    handle uploadhandle;

    // minimum number of file attributes that need to be posted before a PUT transfer can complete
    int minfa;
    
    // position in transfers[type]
    transfer_map::iterator transfers_it;

    // position in faputcompletion[uploadhandle]
    handletransfer_map::iterator faputcompletion_it;
    
    // upload result
    byte *ultoken;

    // backlink to base
    MegaClient* client;
    int tag;

    // signal failure
    void failed(error, dstime = 0);

    // signal completion
    void complete();
    
    // execute completion
    void completefiles();

    // next position to download/upload
    m_off_t nextpos();

    // previous wrong fingerprint
    FileFingerprint badfp;

    // flag to know if prevmetamac is valid
    bool hasprevmetamac;

    // previous wrong metamac
    int64_t prevmetamac;

    // flag to know if currentmetamac is valid
    bool hascurrentmetamac;

    // current wrong metamac
    int64_t currentmetamac;

    // transfer state
    bool finished;

    // cached temp URLs for upload/download data.  6 for raid, 1 for non-raid
    std::vector<string> cachedtempurls;

    // context of the async fopen operation
    AsyncIOContext* asyncopencontext;
   
    // timestamp of the start of the transfer
    m_time_t lastaccesstime;

    // priority of the transfer
    uint64_t priority;

    // state of the transfer
    transferstate_t state;

    Transfer(MegaClient*, direction_t);
    virtual ~Transfer();

    // serialize the Transfer object
    virtual bool serialize(string*);

    // unserialize a Transfer and add it to the transfer map
    static Transfer* unserialize(MegaClient *, string*, transfer_map *);
};

class MEGA_API TransferList
{
public:
    static const uint64_t PRIORITY_START = 0x0000800000000000;
    static const uint64_t PRIORITY_STEP  = 0x0000000000010000;

    TransferList();
    void addtransfer(Transfer* transfer);
    void removetransfer(Transfer *transfer);
    void movetransfer(Transfer *transfer, Transfer *prevTransfer);
    void movetransfer(Transfer *transfer, unsigned int position);
    void movetransfer(Transfer *transfer, transfer_list::iterator dstit);
    void movetransfer(transfer_list::iterator it, transfer_list::iterator dstit);
    void movetofirst(Transfer *transfer);
    void movetofirst(transfer_list::iterator it);
    void movetolast(Transfer *transfer);
    void movetolast(transfer_list::iterator it);
    void moveup(Transfer *transfer);
    void moveup(transfer_list::iterator it);
    void movedown(Transfer *transfer);
    void movedown(transfer_list::iterator it);
    error pause(Transfer *transfer, bool enable);
    transfer_list::iterator begin(direction_t direction);
    transfer_list::iterator end(direction_t direction);
    transfer_list::iterator iterator(Transfer *transfer);
    Transfer *nexttransfer(direction_t direction);
    Transfer *transferat(direction_t direction, unsigned int position);

    transfer_list transfers[2];
    MegaClient *client;
    uint64_t currentpriority;

private:
    void prepareIncreasePriority(Transfer *transfer, transfer_list::iterator srcit, transfer_list::iterator dstit);
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
    HttpReq* req;

    drs_list::iterator drs_it;
    SpeedController speedController;
    m_off_t speed;
    m_off_t meanSpeed;

    bool doio();

    DirectReadSlot(DirectRead*);
    ~DirectReadSlot();
};

struct MEGA_API DirectRead
{
    m_off_t count;
    m_off_t offset;
    m_off_t progress;

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
    m_off_t partiallen;
    dstime partialstarttime;

    string tempurl;

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
    void cmdresult(error, dstime = 0);
    
    // enqueue new read
    void enqueue(m_off_t, m_off_t, int, void*);

    // dispatch all reads
    void dispatch();
    
    // schedule next event
    void schedule(dstime);

    // report failure to app and abort or retry all reads
    void retry(error, dstime = 0);

    DirectReadNode(MegaClient*, handle, bool, SymmCipher*, int64_t);
    ~DirectReadNode();
};
} // namespace

#endif
