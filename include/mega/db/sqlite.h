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
    DbTable* open(FileSystemAccess*, string*);

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
    bool getnodebyhandle(string*, string*);
    bool getnodebyfingerprint(string*, string*);

    void rewinduser();
    void rewindchildren(string*);
    void rewindpcr();
    bool next(string*);

    bool putscsn(char*, unsigned);
    bool putrootnode(int, string*);
    bool putnode(string*, string*, string*, string*);
    bool putuser(string*, string*);
    bool putpcr(string*, string*);

    bool delnode(string*);
    bool delpcr(string*);

    void truncate();
    void begin();
    void commit();
    void abort();
    void remove();

    SqliteDbTable(sqlite3*, FileSystemAccess *fs, string *filepath);
    ~SqliteDbTable();
};
} // namespace

#endif
#endif
