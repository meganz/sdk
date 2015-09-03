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
    SymmCipher *key;
    byte *hkey;
    byte *phkey;

public:
    // get specific record by key
    bool getnode(handle, string*);     // by handle
    bool getnode(string*, string*);    // by fingerprint

protected:
    virtual bool getnodebyhandle(handle, string*) = 0;          // node by handle
    virtual bool getnodebyfingerprint(string*, string*) = 0;    // node by fingerprint

public:
    // for a sequential read
    virtual void rewinduser() = 0;
    virtual void rewindpcr() = 0;
    bool getrootnodes(handle*);
    bool getuser(string*);
    bool getpcr(string*);

    // get records for `scsn`
    virtual bool getscsn(string*) = 0;

    bool getnumchildren(handle, int*);
    bool getnumchildfiles(handle, int*);
    bool getnumchildfolders(handle, int*);

    handle_vector *gethandleschildren(handle);
    handle_vector *gethandlesencryptednodes();
    handle_vector *gethandlesoutshares(handle);
    handle_vector *gethandlespendingshares(handle);

protected:
    // get sequential records for Users, child nodes...
    virtual bool next(string*) = 0;
    virtual bool nexthandle(handle*) = 0;
    virtual bool getrootnode(int, string*) = 0;

    virtual void rewindhandleschildren(handle) = 0;
    virtual void rewindhandlesencryptednodes() = 0;
    virtual void rewindhandlesoutshares(handle) = 0;
    virtual void rewindhandlesoutshares() = 0;
    virtual void rewindhandlespendingshares(handle) = 0;
    virtual void rewindhandlespendingshares() = 0;

    virtual bool getnumchildrenquery(handle, int*) = 0;
    virtual bool getnumchildfilesquery(handle, int*) = 0;
    virtual bool getnumchildfoldersquery(handle, int*) = 0;

public:
    // update or add specific record
    virtual bool putscsn(char *, unsigned) = 0;
    bool putrootnodes(handle *);
    bool putnode(pnode_t);
    bool putuser(User *);
    bool putpcr(PendingContactRequest *);

protected:
    // update or add specific record
    virtual bool putnode(handle, handle, string*, string*, int, string*) = 0;
    virtual bool putuser(handle, string*) = 0;
    virtual bool putpcr(handle, string*) = 0;
    virtual bool putrootnode(int, string*) = 0;

public:
    // delete specific record
    bool delnode(pnode_t);
    bool delpcr(PendingContactRequest *);

//protected:
    virtual bool delnode(handle) = 0;
    virtual bool delpcr(handle) = 0;

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
    virtual ~DbTable();

    // read key to encrypt handles from DB
    virtual bool readhkey() = 0;

private:
    // handle encryption to masterkey (AES with padded CBC mode)
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
