/**
 * @file mega/treeproc.h
 * @brief pnode_t tree processor
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

#ifndef MEGA_TREEPROC_H
#define MEGA_TREEPROC_H 1

#include "sharenodekeys.h"
#include "node.h"
#include "transfer.h"
#include "sync.h"

namespace mega {
// node tree processor
class MEGA_API TreeProc
{
public:
    virtual void proc(MegaClient*, pnode_t) = 0;

    virtual ~TreeProc() { }
};

class MEGA_API TreeProcDel : public TreeProc
{
public:
    void proc(MegaClient*, pnode_t);
};

class MEGA_API TreeProcApplyKey : public TreeProc
{
public:
    void proc(MegaClient*, pnode_t);
};

class MEGA_API TreeProcListOutShares : public TreeProc
{
public:
    void proc(MegaClient*, pnode_t);
};

class MEGA_API TreeProcCopy : public TreeProc
{
public:
    NewNode* nn;
    unsigned nc;

    void allocnodes(void);

    void proc(MegaClient*, pnode_t);
    TreeProcCopy();
    ~TreeProcCopy();
};

class MEGA_API TreeProcDU : public TreeProc
{
public:
    m_off_t numbytes;
    int numfiles;
    int numfolders;

    void proc(MegaClient*, pnode_t);
    TreeProcDU();
};

class MEGA_API TreeProcShareKeys : public TreeProc
{
    ShareNodeKeys snk;
    pnode_t sn;

public:
    void proc(MegaClient*, pnode_t);
    void get(Command*);

    TreeProcShareKeys(pnode_t = NULL);
};

class MEGA_API TreeProcForeignKeys : public TreeProc
{
public:
    void proc(MegaClient*, pnode_t);
};

#ifdef ENABLE_SYNC
class MEGA_API TreeProcDelSyncGet : public TreeProc
{
public:
    void proc(MegaClient*, pnode_t);
};

class MEGA_API LocalTreeProc
{
public:
    virtual void proc(MegaClient*, LocalNode*) = 0;

    virtual ~LocalTreeProc() { }
};

class MEGA_API LocalTreeProcMove : public LocalTreeProc
{
    Sync *newsync;
    bool recreate;

public:
    LocalTreeProcMove(Sync*, bool);
    void proc(MegaClient*, LocalNode*);
    int nc;
};

class MEGA_API LocalTreeProcUpdateTransfers : public LocalTreeProc
{
public:
    void proc(MegaClient*, LocalNode*);
};

#endif
} // namespace

#endif
