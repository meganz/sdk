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

#include "node.h"
#include "backofftimer.h"
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

    m_off_t pos;

    byte filekey[FILENODEKEYLENGTH];

    // CTR mode IV
    int64_t ctriv;

    // meta MAC
    int64_t metamac;

    // file crypto key
    SymmCipher key;

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
    byte ultoken[NewNode::UPLOADTOKENLEN + 1];

    // backlink to base
    MegaClient* client;
    int tag;

    // signal failure
    void failed(error);

    // signal completion
    void complete();
    
    // execute completion
    void completefiles();

    // previous wrong fingerprint
    FileFingerprint badfp;
   
    Transfer(MegaClient*, direction_t);
    virtual ~Transfer();
};

struct MEGA_API DirectReadSlot
{
    m_off_t pos;

    DirectRead* dr;
    HttpReq* req;

    drs_list::iterator drs_it;

    bool doio();

    DirectReadSlot(DirectRead*);
    ~DirectReadSlot();
};

struct MEGA_API DirectRead
{
    m_off_t count;
    m_off_t offset;

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
    void cmdresult(error);
    
    // enqueue new read
    void enqueue(m_off_t, m_off_t, int, void*);

    // dispatch all reads
    void dispatch();
    
    // schedule next event
    void schedule(dstime);

    // report failure to app and abort or retry all reads
    void retry(error);

    DirectReadNode(MegaClient*, handle, bool, SymmCipher*, int64_t);
    ~DirectReadNode();
};
} // namespace

#endif
