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

#include "mega/filesystem.h"
#include "mega/node.h"
#include "mega/user.h"
#include "mega/pendingcontactrequest.h"

#if defined(_WIN32) && !defined(WINDOWS_PHONE)
#include "mega/win32/megawaiter.h"
#elif defined(_WIN32) && defined(WINDOWS_PHONE)
#include "mega/wp8/megawaiter.h"
#else
#include "mega/posix/megawaiter.h"
#endif

namespace mega {

// generic host transactional database access interface
class MEGA_API DbTable
{
    static const int IDSPACING = 16;

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
    virtual void rewindnode() = 0;
    virtual void rewinduser() = 0;
    virtual void rewindpcr() = 0;
    bool getrootnodes(handle*);
    bool getuser(string*);
    bool getpcr(string*);
    bool getnode(string*);

    // get records for `scsn`
    virtual bool getscsn(string*) = 0;

    bool getnumchildren(handle, int*);
    bool getnumchildfiles(handle, int*);
    bool getnumchildfolders(handle, int*);
    bool gettotalnodes(long long *);

    handle_vector *gethandleschildren(handle);
    handle_vector *gethandlesencryptednodes();
    handle_vector *gethandlesoutshares(handle);
    handle_vector *gethandlespendingshares(handle);
    handle_vector *gethandlesfingerprint(string*);

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
    virtual void rewindhandlesfingerprint(string*) = 0;

    virtual bool getnumchildrenquery(handle, int*) = 0;
    virtual bool getnumchildfilesquery(handle, int*) = 0;
    virtual bool getnumchildfoldersquery(handle, int*) = 0;
    virtual bool getnumtotalnodes(long long*) = 0;

public:
    // update or add specific record
    virtual bool putscsn(char *, unsigned) = 0;
    bool putrootnodes(handle *);
    bool putnode(pnode_t);
    bool putuser(User *);
    bool putpcr(PendingContactRequest *);
    bool putchat(TextChat *);

protected:
    // update or add specific record
    virtual bool putnode(handle, handle, string*, string*, int, string*) = 0;
    virtual bool putuser(handle, string*) = 0;
    virtual bool putpcr(handle, string*) = 0;
    virtual bool putchat(handle, string*) = 0;
    virtual bool putrootnode(int, string*) = 0;

public:
    // delete specific record
    bool delnode(pnode_t);
    bool delpcr(PendingContactRequest *);
    bool deluser(User *);
    bool delchat(TextChat *);

//protected:
    virtual bool delnode(handle) = 0;
    virtual bool delpcr(handle) = 0;
    virtual bool deluser(handle) = 0;
    virtual bool delchat(handle) = 0;

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

    // return ASCII versions to export DB
    string gethkey();
    string getphkey();

private:
    // handle encryption to masterkey (AES with padded CBC mode)
    void encrypthandle(handle h, string *hstring);
    void decrypthandle(handle *h, string *hstring);

public:
    // legacy methods for LocalNode's cache

    // autoincrement
    uint32_t nextid;

    // for a full sequential get: rewind to first record
    virtual void rewind() = 0;

    // get next record in sequence
    virtual bool next(uint32_t*, string*) = 0;
    bool next(uint32_t*, string*, SymmCipher*);

    // get specific record by key
    virtual bool get(uint32_t, string*) = 0;

    // update or add specific record
    virtual bool put(uint32_t, char*, unsigned) = 0;
    bool put(uint32_t, Cachable *, SymmCipher*);

    // delete specific record
    virtual bool del(uint32_t) = 0;

};

struct MEGA_API DbAccess
{
    static const int BROKEN_DB_VERSION = 10;    // not supported. If found, it will be discarded automatically
    static const int LEGACY_DB_VERSION = 10;
    static const int DB_VERSION = LEGACY_DB_VERSION + 1;

    DbAccess();
    virtual DbTable* open(FileSystemAccess*, string*, bool = false) = 0; // for transfers & syncs only

    // if BROKEN_DB_VERSION is found, delete it and return true
    virtual bool discardbrokendb(FileSystemAccess *fsaccess, string*) = 0;

    // return true if no DB_VERSION is available with new format, but with old format
    virtual bool checkoldformat(FileSystemAccess *fsaccess, string*) = 0;
    virtual DbTable* openoldformat(FileSystemAccess*, string*) = 0;
    virtual DbTable* open(FileSystemAccess*, string*, SymmCipher *key) = 0; // main cache

    virtual ~DbAccess() { }

    int currentDbVersion;
};

class MEGA_API DbQuery
{
public:
    enum QueryType { GET_NUM_CHILD_FILES, GET_NUM_CHILD_FOLDERS, DELETE } type;

private:
    DbTable *sctable;
    error err;
    int tag;

    handle h;
    int number;

public:
    error getError()            { return err;       }
    int getTag()                { return tag;       }

    void setHandle(handle h)    { this->h = h;      }
    handle getHandle()          { return h;         }
    void setNumber(int n)       { this->number = n; }
    int getNumber()             { return number;    }

    DbQuery(QueryType type, int tag);

    void execute(DbTable *sctable);
};

/**
 * @brief The DbQuery class Provides a thread-safe queue to store database queries.
 */
class DbQueryQueue
{
protected:
    std::deque<DbQuery *> dbqueries;
    MegaMutex mutex;

public:
    DbQueryQueue();
    bool empty();
    void push(DbQuery *query);
    DbQuery * front();
    void pop();
};

class DbThread : public MegaThread
{
private:
    // application callbacks
    struct MegaApp* app;
    DbAccess *dbaccess;
    FileSystemAccess *fsaccess;
    SymmCipher *key;
    string dbname;

public:
    DbQueryQueue queryqueue;    // shared with SDK thread
    WAIT_CLASS *waiter;         // shared with SDK thread

    static void *loop(void *param);

    DbThread(struct MegaApp *app, DbAccess *dbaccess, FileSystemAccess *fsaccess, string *dbname, SymmCipher *key);
    ~DbThread();
};

} // namespace

#endif
