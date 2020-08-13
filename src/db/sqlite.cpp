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

DbTable* SqliteDbAccess::open(PrnGen &rng, FileSystemAccess* fsaccess, string* name, bool recycleLegacyDB, bool checkAlwaysTransacted)
{
    //Each table will use its own database object and its own file
    sqlite3* db;
    string dbfile;
    ostringstream legacyoss;
    legacyoss << dbpath;
    legacyoss << "megaclient_statecache";
    legacyoss << LEGACY_DB_VERSION;
    legacyoss << "_" << *name << ".db";
    string legacydbpath = legacyoss.str();

    ostringstream newoss;
    newoss << dbpath;
    newoss << "megaclient_statecache";
    newoss << DB_VERSION;
    newoss << "_" << *name << ".db";
    string currentdbpath = newoss.str();

    
    auto fa = fsaccess->newfileaccess();
    auto locallegacydbpath = LocalPath::fromPath(legacydbpath, *fsaccess);
    bool legacydbavailable = fa->fopen(locallegacydbpath);
    fa.reset();

    if (legacydbavailable)
    {
        if (currentDbVersion == LEGACY_DB_VERSION)
        {
            LOG_debug << "Using a legacy DB";
            dbfile = legacydbpath;
        }
        else
        {
            if (!recycleLegacyDB)
            {
                LOG_debug << "Legacy DB is outdated. Deleting.";
                fsaccess->unlinklocal(locallegacydbpath);
            }
            else
            {
                LOG_debug << "Trying to recycle a legacy DB";
                auto localcurrentdbpath = LocalPath::fromPath(currentdbpath, *fsaccess);
                if (fsaccess->renamelocal(locallegacydbpath, localcurrentdbpath, false))
                {
                    auto localsuffix = LocalPath::fromPath("-shm", *fsaccess);

                    auto oldfile = locallegacydbpath + localsuffix;
                    auto newfile = localcurrentdbpath + localsuffix;
                    fsaccess->renamelocal(oldfile, newfile, true);

                    localsuffix = LocalPath::fromPath("-wal", *fsaccess);
                    oldfile = locallegacydbpath + localsuffix;
                    newfile = localcurrentdbpath + localsuffix;
                    fsaccess->renamelocal(oldfile, newfile, true);
                    LOG_debug << "Legacy DB recycled";
                }
                else
                {
                    LOG_debug << "Unable to recycle legacy DB. Deleting.";
                    fsaccess->unlinklocal(locallegacydbpath);
                }
            }
        }
    }

    if (!dbfile.size())
    {
        LOG_debug << "Using an upgraded DB";
        dbfile = currentdbpath;
        currentDbVersion = DB_VERSION;
    }

    int rc;

    rc = sqlite3_open(dbfile.c_str(), &db);

    if (rc)
    {
        return NULL;
    }

#if !(TARGET_OS_IPHONE)
    sqlite3_exec(db, "PRAGMA journal_mode=WAL;", NULL, NULL, NULL);
#endif

    const char *sql = "CREATE TABLE IF NOT EXISTS statecache (id INTEGER PRIMARY KEY ASC NOT NULL, content BLOB NOT NULL)";

    rc = sqlite3_exec(db, sql, NULL, NULL, NULL);

    if (rc)
    {
        return NULL;
    }

    return new SqliteDbTable(rng, db, fsaccess, &dbfile, checkAlwaysTransacted);
}

SqliteDbTable::SqliteDbTable(PrnGen &rng, sqlite3* cdb, FileSystemAccess *fs, string *filepath, bool checkAlwaysTransacted)
    : DbTable(rng, checkAlwaysTransacted)
{
    db = cdb;
    pStmt = NULL;
    fsaccess = fs;
    dbfile = *filepath;
}

SqliteDbTable::~SqliteDbTable()
{
    resetCommitter();

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
    LOG_debug << "Database closed " << dbfile;
}

// set cursor to first record
void SqliteDbTable::rewind()
{
    if (!db)
    {
        return;
    }

    if (pStmt)
    {
        sqlite3_reset(pStmt);
    }
    else
    {
        sqlite3_prepare(db, "SELECT id, content FROM statecache", -1, &pStmt, NULL);
    }
}

// retrieve next record through cursor
bool SqliteDbTable::next(uint32_t* index, string* data)
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

    *index = sqlite3_column_int(pStmt, 0);

    data->assign((char*)sqlite3_column_blob(pStmt, 1), sqlite3_column_bytes(pStmt, 1));

    return true;
}

// retrieve record by index
bool SqliteDbTable::get(uint32_t index, string* data)
{
    if (!db)
    {
        return false;
    }

    checkTransaction();

    sqlite3_stmt *stmt;
    bool result = false;

    if (sqlite3_prepare(db, "SELECT content FROM statecache WHERE id = ?", -1, &stmt, NULL) == SQLITE_OK)
    {
        if (sqlite3_bind_int(stmt, 1, index) == SQLITE_OK)
        {
            if (sqlite3_step(stmt) == SQLITE_ROW)
            {
                data->assign((char*)sqlite3_column_blob(stmt, 0), sqlite3_column_bytes(stmt, 0));

                result = true;
            }
        }
    }

    sqlite3_finalize(stmt);
    return result;
}

// add/update record by index
bool SqliteDbTable::put(uint32_t index, char* data, unsigned len)
{
    if (!db)
    {
        return false;
    }

    checkTransaction();

    sqlite3_stmt *stmt;
    bool result = false;

    if (sqlite3_prepare(db, "INSERT OR REPLACE INTO statecache (id, content) VALUES (?, ?)", -1, &stmt, NULL) == SQLITE_OK)
    {
        if (sqlite3_bind_int(stmt, 1, index) == SQLITE_OK)
        {
            if (sqlite3_bind_blob(stmt, 2, data, len, SQLITE_STATIC) == SQLITE_OK)
            {
                if (sqlite3_step(stmt) == SQLITE_DONE)
                {
                    result = true;
                }
            }
        }
    }

    sqlite3_finalize(stmt);
    return result;
}

// delete record by index
bool SqliteDbTable::del(uint32_t index)
{
    if (!db)
    {
        return false;
    }

    checkTransaction();

    char buf[64];

    sprintf(buf, "DELETE FROM statecache WHERE id = %" PRIu32, index);

    return !sqlite3_exec(db, buf, 0, 0, NULL);
}

// truncate table
void SqliteDbTable::truncate()
{
    if (!db)
    {
        return;
    }

    checkTransaction();

    sqlite3_exec(db, "DELETE FROM statecache", 0, 0, NULL);
}

// begin transaction
void SqliteDbTable::begin()
{
    if (!db)
    {
        return;
    }

    LOG_debug << "DB transaction BEGIN " << dbfile;
    sqlite3_exec(db, "BEGIN", 0, 0, NULL);
}

// commit transaction
void SqliteDbTable::commit()
{
    if (!db)
    {
        return;
    }

    LOG_debug << "DB transaction COMMIT " << dbfile;
    sqlite3_exec(db, "COMMIT", 0, 0, NULL);
}

// abort transaction
void SqliteDbTable::abort()
{
    if (!db)
    {
        return;
    }

    LOG_debug << "DB transaction ROLLBACK " << dbfile;
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

    auto localpath = LocalPath::fromPath(dbfile, *fsaccess);
    fsaccess->unlinklocal(localpath);
}
} // namespace

#endif
