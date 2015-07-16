/**
 * @file mega/db.h
 * @brief Database access interface
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

#ifndef MEGA_DB_H
#define MEGA_DB_H 1

#include "filesystem.h"
#include "node.h"
#include "user.h"

namespace mega {
// generic host transactional database access interface
class MEGA_API DbTable
{
protected:
    static const int IDSPACING = 16;

public:
    // get specific record by key
    bool getnode(handle, string*, SymmCipher*);     // by handle
    bool getnode(string*, string*, SymmCipher*);    // by fingerprint

protected:
    virtual bool getnodebyhandle(handle, string*) = 0;          // node by handle
    virtual bool getnodebyfingerprint(string*, string*) = 0;    // node by fingerprint

public:
    // for a sequential read of Users or child nodes
    void rewindchildren(handle, SymmCipher*);
    virtual void rewinduser() = 0;

    bool getuser(string*, SymmCipher*);
    bool getchildren(string*, SymmCipher*);

    // get records for `scsn` and `rootnodes`
    virtual bool getscsn(string*) = 0;
    virtual bool getrootnodes(handle*) = 0;

protected:
    // get sequential records for Users & child nodes
    virtual bool next(string*) = 0;
    virtual void rewindchildren(handle) = 0;

public:
    // update or add specific record
    virtual bool putscsn(char *, unsigned) = 0;
    virtual bool putrootnodes(handle*) = 0;
    bool putnode(pnode_t, SymmCipher*);
    bool putuser(User *, SymmCipher*);

protected:
    // update or add specific record
    virtual bool putnode(handle, handle, char *, unsigned, char *, unsigned) = 0;
    virtual bool putuser(char *, unsigned, char *, unsigned) = 0;

public:
    // delete specific record
    bool delnode(pnode_t, SymmCipher*);

protected:
    virtual bool delnode(handle h) = 0;

public:
    // delete all records
    virtual void truncate() = 0;

    // begin transaction
    virtual void begin() = 0;

    // commit transaction
    virtual void commit() = 0;

    // abort transaction
    virtual void abort() = 0;

    // permanantly remove all database info
    virtual void remove() = 0;

    // autoincrement
    uint32_t nextid;

    DbTable();
    virtual ~DbTable() { }
};

struct MEGA_API DbAccess
{
    virtual DbTable* open(FileSystemAccess*, string*) = 0;

    virtual ~DbAccess() { }
};
} // namespace

#endif
