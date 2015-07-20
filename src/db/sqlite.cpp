/**
 * @file sqlite.cpp
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

#include "mega.h"

#ifdef USE_SQLITE
namespace mega {
SqliteDbAccess::SqliteDbAccess(string* path)
{
    if (path)
    {
        dbpath = *path;
    }
}

SqliteDbAccess::~SqliteDbAccess()
{
}

DbTable* SqliteDbAccess::open(FileSystemAccess* fsaccess, string* name)
{
    //Each table will use its own database object and its own file
    //The previous implementation was closing the first database
    //when the second one was opened.
    sqlite3* db;

    string dbdir = dbpath + "megaclient_statecache7_" + *name + ".db";

    int rc;

    rc = sqlite3_open(dbdir.c_str(), &db);

    if (rc)
    {
        return NULL;
    }

#if !(TARGET_OS_IPHONE)
    sqlite3_exec(db, "PRAGMA journal_mode=WAL;", NULL, NULL, NULL);
#endif

    string sql;

    // 1. Create table 'scsn'
    sql = "CREATE TABLE IF NOT EXISTS init (id INTEGER PRIMARY KEY NOT NULL, content BLOB NOT NULL)";
    rc = sqlite3_exec(db, sql.c_str(), NULL, NULL, NULL);
    if (rc)
    {
        return NULL;
    }

    // 2. Create table for 'nodes'
    sql = "CREATE TABLE IF NOT EXISTS nodes (nodehandle INTEGER PRIMARY KEY NOT NULL, parenthandle INTEGER, fingerprint BLOB, node BLOB NOT NULL)";
    rc = sqlite3_exec(db, sql.c_str(), NULL, NULL, NULL);
    if (rc)
    {
        return NULL;
    }

    // 3. Create table for 'users'
    sql = "CREATE TABLE IF NOT EXISTS users (email TEXT PRIMARY KEY NOT NULL, user BLOB NOT NULL)";
    rc = sqlite3_exec(db, sql.c_str(), NULL, NULL, NULL);
    if (rc)
    {
        return NULL;
    }

    return new SqliteDbTable(db, fsaccess, &dbdir);
}

SqliteDbTable::SqliteDbTable(sqlite3* cdb, FileSystemAccess *fs, string *filepath)
{
    db = cdb;
    pStmt = NULL;
    fsaccess = fs;
    dbfile = *filepath;
}

SqliteDbTable::~SqliteDbTable()
{
    if (!db)
    {
        return;
    }

    if (pStmt)
    {
        sqlite3_finalize(pStmt);
    }
    abort();
    sqlite3_close(db);
    LOG_debug << "Database closed";
}

// retrieve record by index
bool SqliteDbTable::getscsn(string* data)
{
    if (!db)
    {
        return false;
    }

    sqlite3_stmt *stmt;

    int rc = sqlite3_prepare(db, "SELECT content FROM init WHERE id = ?", -1, &stmt, NULL);

    if (rc)
    {
        return false;
    }

    rc = sqlite3_bind_int(stmt, 1, 0);

    if (rc)
    {
        return false;
    }

    rc = sqlite3_step(stmt);

    if (rc != SQLITE_ROW)
    {
        return false;
    }

    data->assign((char*)sqlite3_column_blob(stmt, 0), sqlite3_column_bytes(stmt, 0));

    sqlite3_finalize(stmt);

    return true;
}

bool SqliteDbTable::getrootnodes(handle *rootnodes)
{
    if (!db)
    {
        return false;
    }

    sqlite3_stmt *stmt = NULL;
    int rc;
    handle *h;

    // TODO: consider to encrypt rootnode handles, or simply retrieve them from "Nodes" table (parenthandle = -1)

    for (int i=0; i<3; i++)
    {
        if (stmt)
        {
            rc = sqlite3_reset(stmt);
            if (rc)
            {
                return false;
            }
        }

        rc = sqlite3_prepare(db, "SELECT content FROM init WHERE id = ?", -1, &stmt, NULL);
        if (rc)
        {
            return false;
        }

        rc = sqlite3_bind_int(stmt, 1, i+1);    // 0: scsn 1-3: rootnodes
        if (rc)
        {
            return false;
        }

        rc = sqlite3_step(stmt);
        if (rc != SQLITE_ROW)
        {
            return false;
        }

        // TODO: decrypt `nodehandle` if they were encrypted at `putrootnodes()`

        h = (handle *) sqlite3_column_blob(stmt, 0);
        rootnodes[i] = *h;
    }

    sqlite3_finalize(stmt);
    return true;
}

bool SqliteDbTable::getnodebyhandle(handle h, string *data)
{
    if (!db)
    {
        return false;
    }

    sqlite3_stmt *stmt;

    int rc = sqlite3_prepare(db, "SELECT node FROM nodes WHERE nodehandle = ?", -1, &stmt, NULL);
    if (rc)
    {
        return false;
    }

    rc = sqlite3_bind_int64(stmt, 1, h);
    if (rc)
    {
        return false;
    }

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW)
    {
        return false;
    }

    data->assign((char*)sqlite3_column_blob(stmt,0), sqlite3_column_bytes(stmt,0));

    sqlite3_finalize(stmt);

    return true;
}

bool SqliteDbTable::getnodebyfingerprint(string* fingerprint, string *data)
{
    if (!db)
    {
        return false;
    }

    sqlite3_stmt *stmt;

    int rc = sqlite3_prepare(db, "SELECT node FROM nodes WHERE fingerprint = ?", -1, &stmt, NULL);
    if (rc)
    {
        return false;
    }

    rc = sqlite3_bind_blob(stmt, 1, (char*)fingerprint->data(), fingerprint->size(), SQLITE_STATIC);
    if (rc)
    {
        return false;
    }

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW)
    {
        return false;
    }

    data->assign((char*)sqlite3_column_blob(stmt,0), sqlite3_column_bytes(stmt,0));

    sqlite3_finalize(stmt);

    return true;
}

void SqliteDbTable::rewinduser()
{
    if (!db)
    {
        return;
    }

    if (pStmt)
    {
        sqlite3_reset(pStmt);
    }

    sqlite3_prepare(db, "SELECT user FROM users", -1, &pStmt, NULL);

}

void SqliteDbTable::rewindchildren(handle h)
{
    if (!db)
    {
        return;
    }

    if (pStmt)
    {
        sqlite3_reset(pStmt);
    }

    if (sqlite3_prepare(db, "SELECT node FROM nodes WHERE parenthandle = ?", -1, &pStmt, NULL))
    {
        return;
    }

    if (sqlite3_bind_int64(pStmt, 1, h))
    {
        return;
    }

}

bool SqliteDbTable::next(string *data)
{
    if (!db)
    {
        return false;
    }

    if (!pStmt)
    {
        return false;
    }

    int rc = sqlite3_step(pStmt);

    if (rc != SQLITE_ROW)
    {
        sqlite3_finalize(pStmt);
        pStmt = NULL;
        return false;
    }

    data->assign((char*)sqlite3_column_blob(pStmt, 0), sqlite3_column_bytes(pStmt, 0));

    return true;
}

// update SCSN
bool SqliteDbTable::putscsn(char* data, unsigned len)
{
    if (!db)
    {
        return false;
    }

    sqlite3_stmt *stmt;

    int rc = sqlite3_prepare(db, "INSERT OR REPLACE INTO init (id, content) VALUES (?, ?)", -1, &stmt, NULL);
    if (rc)
    {
        return false;
    }

    // `id` for the `scsn` is always the same, only one row
    rc = sqlite3_bind_int(stmt, 1, 0);
    if (rc)
    {
        return false;
    }

    rc = sqlite3_bind_blob(stmt, 2, data, len, SQLITE_STATIC);
    if (rc)
    {
        return false;
    }

    rc = sqlite3_step(stmt);

    if (rc != SQLITE_DONE)
    {
        return false;
    }

    sqlite3_finalize(stmt);
    return true;
}

bool SqliteDbTable::putrootnodes(handle *rootnodes)
{
    if (!db)
    {
        return false;
    }

    sqlite3_stmt *stmt = NULL;
    int rc;

    // TODO: consider to encrypt rootnode handles

    for (int i=0; i<3; i++)
    {
        if (stmt)
        {
            rc = sqlite3_reset(stmt);
            if (rc)
            {
                return false;
            }
        }

        rc = sqlite3_prepare(db, "INSERT OR REPLACE INTO init (id, content) VALUES (?, ?)", -1, &stmt, NULL);
        if (rc)
        {
            return false;
        }

        rc = sqlite3_bind_int(stmt, 1, i+1);    // 0: scsn 1-3: rootnodes
        if (rc)
        {
            return false;
        }

        rc = sqlite3_bind_blob(stmt, 2, &rootnodes[i], sizeof (handle), SQLITE_STATIC);
        if (rc)
        {
            return false;
        }

        rc = sqlite3_step(stmt);
        if (rc != SQLITE_DONE)
        {
            return false;
        }
    }

    sqlite3_finalize(stmt);
    return true;
}

bool SqliteDbTable::putnode(handle h, handle ph, char *fp, unsigned fpsize, char *node, unsigned nodesize)
{
    if (!db)
    {
        return false;
    }

    sqlite3_stmt *stmt;

    int rc = sqlite3_prepare(db, "INSERT OR REPLACE INTO nodes (nodehandle, parenthandle, fingerprint, node) VALUES (?, ?, ?, ?)", -1, &stmt, NULL);
    if (rc)
    {
        return false;
    }

    rc = sqlite3_bind_int64(stmt, 1, h);
    if (rc)
    {
        return false;
    }

    rc = sqlite3_bind_int64(stmt, 2, ph);
    if (rc)
    {
        return false;
    }

    rc = sqlite3_bind_blob(stmt, 3, fp, fpsize, SQLITE_STATIC);
    if (rc)
    {
        return false;
    }

    rc = sqlite3_bind_blob(stmt, 4, node, nodesize, SQLITE_STATIC);
    if (rc)
    {
        return false;
    }

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE)
    {
        return false;
    }

    sqlite3_finalize(stmt);
    return true;
}

bool SqliteDbTable::putuser(char *email, unsigned emailsize, char *user, unsigned usersize)
{
    if (!db)
    {
        return false;
    }

    sqlite3_stmt *stmt;

    int rc = sqlite3_prepare(db, "INSERT OR REPLACE INTO users (email, user) VALUES (?, ?)", -1, &stmt, NULL);
    if (rc)
    {
        return false;
    }

    rc = sqlite3_bind_text(stmt, 1, email, emailsize, SQLITE_STATIC);
    if (rc)
    {
        return false;
    }

    rc = sqlite3_bind_blob(stmt, 2, user, usersize, SQLITE_STATIC);
    if (rc)
    {
        return false;
    }

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE)
    {
        return false;
    }

    sqlite3_finalize(stmt);
    return true;
}

bool SqliteDbTable::delnode(handle h)
{
    if (!db)
    {
        return false;
    }

    char buf[64];

    sprintf(buf, "DELETE FROM nodes WHERE nodehandle = %" PRIu64, h);

    return !sqlite3_exec(db, buf, 0, 0, NULL);
}

// truncate table
void SqliteDbTable::truncate()
{
    if (!db)
    {
        return;
    }

    sqlite3_exec(db, "DELETE FROM scsn", 0, 0, NULL);
    sqlite3_exec(db, "DELETE FROM nodes", 0, 0, NULL);
    sqlite3_exec(db, "DELETE FROM users", 0, 0, NULL);
}

// begin transaction
void SqliteDbTable::begin()
{
    if (!db)
    {
        return;
    }

    sqlite3_exec(db, "BEGIN", 0, 0, NULL);
}

// commit transaction
void SqliteDbTable::commit()
{
    if (!db)
    {
        return;
    }

    sqlite3_exec(db, "COMMIT", 0, 0, NULL);
}

// abort transaction
void SqliteDbTable::abort()
{
    if (!db)
    {
        return;
    }

    sqlite3_exec(db, "ROLLBACK", 0, 0, NULL);
}

void SqliteDbTable::remove()
{
    if (!db)
    {
        return;
    }

    if (pStmt)
    {
        sqlite3_finalize(pStmt);
    }
    abort();
    sqlite3_close(db);

    db = NULL;

    string localpath;
    fsaccess->path2local(&dbfile, &localpath);
    fsaccess->unlinklocal(&localpath);
}
} // namespace

#endif
