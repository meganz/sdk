/**
 * @file sqlite.h
 * @brief SQLite DB access layer
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

#ifdef USE_SQLITE
#ifndef DBACCESS_CLASS
#define DBACCESS_CLASS SqliteDbAccess

#include <sqlite3.h>

namespace mega {
class MEGA_API SqliteDbAccess : public DbAccess
{
    string dbpath;

public:
    DbTable* open(FileSystemAccess*, string*, SymmCipher *key);

    SqliteDbAccess(string* = NULL);
    ~SqliteDbAccess();
};

class MEGA_API SqliteDbTable : public DbTable
{
    sqlite3* db;
    sqlite3_stmt* pStmt;
    string dbfile;
    FileSystemAccess *fsaccess;

public:
    bool getscsn(string*);
    bool getrootnode(int, string*);
//    bool getnodebyhandle(string*, string*);
    bool getnodebyhandle(handle, string*);
    bool getnodebyfingerprint(string*, string*);

//    bool getnumchildren(string*, int*);
//    bool getnumchildfiles(string*, int*);
//    bool getnumchildfolders(string*, int*);
    bool getnumchildren(handle, int*);
    bool getnumchildfiles(handle, int*);
    bool getnumchildfolders(handle, int*);

    void rewinduser();
    void rewindpcr();
    void rewindencryptednode();
    void rewindoutshares(string*);
    void rewindpendingshares(string*);
//    void rewindhandleschildren(string *);
    void rewindhandleschildren(handle);
    void rewindhandlesencryptednodes();
//    void rewindhandlesoutshares(string *);
    void rewindhandlesoutshares(handle);
//    void rewindhandlespendingshares(string *);
    void rewindhandlespendingshares(handle);
    bool next(string*);
    bool nexthandle(handle*);

    bool putscsn(char*, unsigned);
    bool putrootnode(int, string*);
//    bool putnode(string*, string*, string*, string*, int, string*);
    bool putnode(handle, handle, string*, string*, int, string*);
    bool putuser(string*, string*);
//    bool putpcr(string*, string*);
    bool putpcr(handle, string*);

//    bool delnode(string*);
    bool delnode(handle);
//    bool delpcr(string*);
    bool delpcr(handle);

    void truncate();
    void begin();
    void commit();
    void abort();
    void remove();

    SqliteDbTable(sqlite3*, FileSystemAccess *fs, string *filepath, SymmCipher *key);
    ~SqliteDbTable();
};
} // namespace

#endif
#endif
