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

static LocalPath databasePath(const FileSystemAccess& fsAccess,
                              const LocalPath& rootPath,
                              const string& name,
                              const int version)
{
    ostringstream osstream;

    osstream << "megaclient_statecache"
             << version
             << "_"
             << name
             << ".db";

    LocalPath path = rootPath;

    path.appendWithSeparator(
      LocalPath::fromPath(osstream.str(), fsAccess),
      false);

    return path;
}

SqliteDbAccess::SqliteDbAccess(const LocalPath& rootPath)
  : mRootPath(rootPath)
{
}

SqliteDbAccess::~SqliteDbAccess()
{
}

DbTable* SqliteDbAccess::open(PrnGen &rng, FileSystemAccess& fsAccess, const string& name, const int flags)
{
    auto dbPath = databasePath(fsAccess, mRootPath, name, DB_VERSION);

    {
        auto legacyPath = databasePath(fsAccess, mRootPath, name, LEGACY_DB_VERSION);
        auto fileAccess = fsAccess.newfileaccess();

        if (fileAccess->fopen(legacyPath))
        {
            LOG_debug << "Found legacy database at: " << legacyPath.toPath(fsAccess);

            if (currentDbVersion == LEGACY_DB_VERSION)
            {
                LOG_debug << "Using a legacy database.";
                dbPath = std::move(legacyPath);
            }
            else if ((flags & DB_OPEN_FLAG_RECYCLE))
            {
                LOG_debug << "Trying to recycle a legacy database.";

                if (fsAccess.renamelocal(legacyPath, dbPath, false))
                {
                    auto suffix = LocalPath::fromPath("-shm", fsAccess);
                    auto from = legacyPath + suffix;
                    auto to = dbPath + suffix;

                    fsAccess.renamelocal(from, to);

                    suffix = LocalPath::fromPath("-shm", fsAccess);
                    from = legacyPath + suffix;
                    to = dbPath + suffix;

                    fsAccess.renamelocal(from, to);

                    LOG_debug << "Legacy database recycled.";
                }
                else
                {
                    LOG_debug << "Unable to recycle database, deleting...";
                    fsAccess.unlinklocal(legacyPath);
                }
            }
            else
            {
                LOG_debug << "Deleting outdated legacy database.";
                fsAccess.unlinklocal(legacyPath);
            }
        }
    }

    const string dbPathStr = dbPath.toPath(fsAccess);
    sqlite3* db;
    int result = sqlite3_open(dbPathStr.c_str(), &db);

    if (result)
    {
        if (db)
        {
            sqlite3_close(db);
        }

        return nullptr;
    }

#if !(TARGET_OS_IPHONE)
    result = sqlite3_exec(db, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
    if (result)
    {
        sqlite3_close(db);
        return nullptr;
    }
#endif /* ! TARGET_OS_IPHONE */

    const char* sql =
      "CREATE TABLE IF NOT EXISTS statecache ( "
      "    id INTEGER PRIMARY KEY ASC NOT NULL, "
      "    content BLOB NOT NULL "
      ");";

    result = sqlite3_exec(db, sql, nullptr, nullptr, nullptr);
    if (result)
    {
        sqlite3_close(db);
        return nullptr;
    }

    return new SqliteDbTable(rng,
                             db,
                             fsAccess,
                             dbPathStr,
                             (flags & DB_OPEN_FLAG_TRANSACTED) > 0);
}

bool SqliteDbAccess::probe(FileSystemAccess& fsAccess, const string& name) const
{
    auto fileAccess = fsAccess.newfileaccess();

    LocalPath dbPath = databasePath(fsAccess, mRootPath, name, DB_VERSION);

    if (fileAccess->isfile(dbPath))
    {
        return true;
    }

    dbPath = databasePath(fsAccess, mRootPath, name, LEGACY_DB_VERSION);

    return fileAccess->isfile(dbPath);
}

const LocalPath& SqliteDbAccess::rootPath() const
{
    return mRootPath;
}

SqliteDbTable::SqliteDbTable(PrnGen &rng, sqlite3* db, FileSystemAccess &fsAccess, const string &path, const bool checkAlwaysTransacted)
  : DbTable(rng, checkAlwaysTransacted)
  , db(db)
  , pStmt(nullptr)
  , dbfile(path)
  , fsaccess(&fsAccess)
{
}

SqliteDbTable::~SqliteDbTable()
{
    resetCommitter();

    if (!db)
    {
        return;
    }

    sqlite3_finalize(pStmt);

    if (inTransaction())
    {
        abort();
    }

    sqlite3_close(db);
    LOG_debug << "Database closed " << dbfile;
}

bool SqliteDbTable::inTransaction() const
{
    return sqlite3_get_autocommit(db) == 0;
}

// set cursor to first record
void SqliteDbTable::rewind()
{
    if (!db)
    {
        return;
    }

    int result;

    if (pStmt)
    {
        result = sqlite3_reset(pStmt);
    }
    else
    {
        result = sqlite3_prepare(db, "SELECT id, content FROM statecache", -1, &pStmt, NULL);
    }

    if (result != SQLITE_OK)
    {
        string err = string(" Error: ") + (sqlite3_errmsg(db) ? sqlite3_errmsg(db) : std::to_string(result));
        LOG_err << "Unable to rewind database: " << dbfile << err;
        assert(!"Unable to rewind database.");
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

        if (rc != SQLITE_DONE)
        {
            string err = string(" Error: ") + (sqlite3_errmsg(db) ? sqlite3_errmsg(db) : std::to_string(rc));
            LOG_err << "Unable to get next record from database: " << dbfile << err;
            assert(!"Unable to get next record from database.");
        }

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
    int rc;

    rc = sqlite3_prepare(db, "SELECT content FROM statecache WHERE id = ?", -1, &stmt, NULL);
    if (rc == SQLITE_OK)
    {
        rc = sqlite3_bind_int(stmt, 1, index);
        if (rc == SQLITE_OK)
        {
            rc = sqlite3_step(stmt);
            if (rc == SQLITE_ROW)
            {
                data->assign((char*)sqlite3_column_blob(stmt, 0), sqlite3_column_bytes(stmt, 0));
            }
        }
    }

    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE && rc != SQLITE_ROW)
    {
        string err = string(" Error: ") + (sqlite3_errmsg(db) ? sqlite3_errmsg(db) : std::to_string(rc));
        LOG_err << "Unable to get record from database: " << dbfile << err;
        assert(!"Unable to get record from database.");
    }

    return rc == SQLITE_ROW;
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

    int rc = sqlite3_prepare(db, "INSERT OR REPLACE INTO statecache (id, content) VALUES (?, ?)", -1, &stmt, NULL);
    if (rc == SQLITE_OK)
    {
        rc = sqlite3_bind_int(stmt, 1, index);
        if (rc == SQLITE_OK)
        {
            rc = sqlite3_bind_blob(stmt, 2, data, len, SQLITE_STATIC);
            if (rc == SQLITE_OK)
            {

                rc = sqlite3_step(stmt);
                if (rc == SQLITE_DONE)
                {
                    result = true;
                }
            }
        }
    }

    sqlite3_finalize(stmt);

    if (!result)
    {
        string err = string(" Error: ") + (sqlite3_errmsg(db) ? sqlite3_errmsg(db) : std::to_string(rc));
        LOG_err << "Unable to put record into database: " << dbfile << err;
        assert(!"Unable to put record into database.");
    }

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

    int rc = sqlite3_exec(db, buf, 0, 0, nullptr);
    if (rc != SQLITE_OK)
    {
        string err = string(" Error: ") + (sqlite3_errmsg(db) ? sqlite3_errmsg(db) : std::to_string(rc));
        LOG_err << "Unable to delete record from database: " << dbfile << err;
        assert(!"Unable to delete record from database.");

        return false;
    }

    return true;
}

// truncate table
void SqliteDbTable::truncate()
{
    if (!db)
    {
        return;
    }

    checkTransaction();

    int rc = sqlite3_exec(db, "DELETE FROM statecache", 0, 0, NULL);
    if (rc != API_OK)
    {
        string err = string(" Error: ") + (sqlite3_errmsg(db) ? sqlite3_errmsg(db) : std::to_string(rc));
        LOG_err << "Unable to truncate database: " << dbfile << err;
        assert(!"Unable to truncate database.");
    }
}

// begin transaction
void SqliteDbTable::begin()
{
    if (!db)
    {
        return;
    }

    LOG_debug << "DB transaction BEGIN " << dbfile;
    int rc = sqlite3_exec(db, "BEGIN", 0, 0, NULL);
    if (rc != SQLITE_OK)
    {
        string err = string(" Error: ") + (sqlite3_errmsg(db) ? sqlite3_errmsg(db) : std::to_string(rc));
        LOG_err << "Unable to begin transaction on database: " << dbfile << err;
        assert(!"Unable to begin transaction on database.");
    }
}

// commit transaction
void SqliteDbTable::commit()
{
    if (!db)
    {
        return;
    }

    LOG_debug << "DB transaction COMMIT " << dbfile;

    int rc = sqlite3_exec(db, "COMMIT", 0, 0, NULL);
    if (rc != SQLITE_OK)
    {
        string err = string(" Error: ") + (sqlite3_errmsg(db) ? sqlite3_errmsg(db) : std::to_string(rc));
        LOG_err << "Unable to commit transaction on database: " << dbfile << err;
        assert(!"Unable to commit transaction on database.");
    }
}

// abort transaction
void SqliteDbTable::abort()
{
    if (!db)
    {
        return;
    }

    LOG_debug << "DB transaction ROLLBACK " << dbfile;

    int rc = sqlite3_exec(db, "ROLLBACK", 0, 0, NULL);
    if (rc != SQLITE_OK)
    {
        string err = string(" Error: ") + (sqlite3_errmsg(db) ? sqlite3_errmsg(db) : std::to_string(rc));
        LOG_err << "Unable to rollback transaction on database: " << dbfile << err;
        assert(!"Unable to rollback transaction on database.");
    }
}

void SqliteDbTable::remove()
{
    if (!db)
    {
        return;
    }

    sqlite3_finalize(pStmt);

    if (inTransaction())
    {
        abort();
    }

    sqlite3_close(db);

    db = NULL;

    auto localpath = LocalPath::fromPath(dbfile, *fsaccess);
    fsaccess->unlinklocal(localpath);
}
} // namespace

#endif
