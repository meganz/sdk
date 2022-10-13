/**
 * @file mega/treeproc.h
 * @brief Node tree processor
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
    virtual void proc(MegaClient*, Node*) = 0;

    virtual ~TreeProc() { }
};

class MEGA_API TreeProcDel : public TreeProc
{
public:
    void proc(MegaClient*, Node*);
    void setOriginatingUser(const handle& handle);
private:
    handle mOriginatingUser = mega::UNDEF;
};

class MEGA_API TreeProcApplyKey : public TreeProc
{
public:
    void proc(MegaClient*, Node*);
};

class MEGA_API TreeProcCopy : public TreeProc
{
public:
    vector<NewNode> nn;
    unsigned nc = 0;
    bool allocated = false;

    void allocnodes(void);

    void proc(MegaClient*, Node*);
};

class MEGA_API TreeProcDU : public TreeProc
{
public:
    m_off_t numbytes;
    int numfiles;
    int numfolders;

    void proc(MegaClient*, Node*);
    TreeProcDU();
};

class MEGA_API TreeProcShareKeys : public TreeProc
{
    ShareNodeKeys snk;
    Node* sn;

public:
    void proc(MegaClient*, Node*);
    void get(Command*);

    TreeProcShareKeys(Node* = NULL);
};

class MEGA_API TreeProcForeignKeys : public TreeProc
{
public:
    void proc(MegaClient*, Node*);
};

#ifdef ENABLE_SYNC

class MEGA_API LocalTreeProc
{
public:
    virtual void proc(FileSystemAccess&, LocalNode*) = 0;

    virtual ~LocalTreeProc() { }
};

class MEGA_API LocalTreeProcMove : public LocalTreeProc
{
    Sync *newsync;

public:
    LocalTreeProcMove(Sync*);
    void proc(FileSystemAccess&, LocalNode*);
    int nc;
};

class MEGA_API LocalTreeProcUpdateTransfers : public LocalTreeProc
{
public:
    void proc(FileSystemAccess&, LocalNode*);
};

#endif
} // namespace

#endif
