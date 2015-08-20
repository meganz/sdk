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
#include "pendingcontactrequest.h"

namespace mega {
// generic host transactional database access interface
class MEGA_API DbTable
{
protected:
    static const int IDSPACING = 16;
    SymmCipher *key;

public:
    // get specific record by key
    bool getnode(handle, string*);     // by handle
    bool getnode(string*, string*);    // by fingerprint

protected:
    virtual bool getnodebyhandle(string*, string*) = 0;          // node by handle
    virtual bool getnodebyfingerprint(string*, string*) = 0;    // node by fingerprint

public:
    // for a sequential read
    void rewindchildren(handle);
    virtual void rewinduser() = 0;
    virtual void rewindpcr() = 0;
    virtual void rewindencryptednode() = 0;
    virtual void rewindoutshares(handle = UNDEF);
    virtual void rewindpendingshares(handle = UNDEF);
    bool getrootnodes(handle*);
    bool getuser(string*);
    bool getpcr(string*);
    bool getencryptednode(string*);
    bool getoutshare(string*);
    bool getpendingshare(string*);

    // get records for `scsn`
    virtual bool getscsn(string*) = 0;

    bool getchildren(string*);
    bool getnumchildren(handle, int*);
    bool getnumchildfiles(handle, int*);
    bool getnumchildfolders(handle, int*);

protected:
    // get sequential records for Users, child nodes...
    virtual bool next(string*) = 0;
    virtual void rewindchildren(string*) = 0;
    virtual void rewindoutshares(string*) = 0;
    virtual void rewindpendingshares(string*) = 0;
    virtual bool getrootnode(int, string*) = 0;

    virtual bool getnumchildren(string*, int*) = 0;
    virtual bool getnumchildfiles(string*, int*) = 0;
    virtual bool getnumchildfolders(string*, int*) = 0;

public:
    // update or add specific record
    virtual bool putscsn(char *, unsigned) = 0;
    bool putrootnodes(handle *);
    bool putnode(pnode_t);
    bool putuser(User *);
    bool putpcr(PendingContactRequest *);

protected:
    // update or add specific record
    virtual bool putnode(string*, string*, string*, string*, int, string*) = 0;
    virtual bool putuser(string*, string*) = 0;
    virtual bool putpcr(string*, string*) = 0;
    virtual bool putrootnode(int, string*) = 0;

public:
    // delete specific record
    bool delnode(pnode_t);
    bool delpcr(PendingContactRequest *);

protected:
    virtual bool delnode(string *) = 0;
    virtual bool delpcr(string *) = 0;

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

    DbTable(SymmCipher *key);
    virtual ~DbTable() { }

private:
    void encrypthandle(handle h, string *hstring);
    void decrypthandle(handle *h, string *hstring);
};

struct MEGA_API DbAccess
{
    virtual DbTable* open(FileSystemAccess*, string*, SymmCipher *) = 0;

    virtual ~DbAccess() { }
};
} // namespace

#endif
