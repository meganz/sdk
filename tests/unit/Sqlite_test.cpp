/* @brief Unit tests for the Sqlite functionalities
 *
 * This test suite validates sqlite functionalites
 */

#include <gtest/gtest.h>
#include <mega/db/sqlite.h>
#include <mega/localpath.h>

#include <filesystem>
#include <mega.h>
#include <string>

using namespace mega;

/**
 * @brief Validate renameDBFiles method
 *
 * Steps:
 *  - Create new data base
 *  - Call to renameDBFiles
 *  - Check if all files has been renamed
 */
#ifdef WIN32
// As DB is opened when files are renamed, windows doesn't allow rename DB files
TEST(Sqlite, DISABLED_renameDB)
{
#else
TEST(Sqlite, renameDB)
{
#endif
    auto pathString{std::filesystem::current_path() / "folder"};

    const MrProper cleanUp(
        [pathString]()
        {
            std::filesystem::remove_all(pathString);
        });

    std::filesystem::create_directory(pathString);
    LocalPath folderPath = LocalPath::fromAbsolutePath(pathString.u8string());
    SqliteDbAccess dbAccess{folderPath};

    // Create and open DB
    std::unique_ptr<FileSystemAccess> fsaccess{new FSACCESS_CLASS};
    const std::string dbName{"dbName"};
    LocalPath currentDataBasePath{dbAccess.databasePath(*fsaccess, dbName, DbAccess::DB_VERSION)};
    PrnGen rng;
    constexpr int flags = 0;
    std::unique_ptr<SqliteDbTable> db{dbAccess.open(rng, *fsaccess, dbName, flags, nullptr)};
    if (!db)
    {
        ASSERT_TRUE(false) << "Failure opening DB";
    }

    // Insert elements
    for (int i = 1; i < 10; ++i)
    {
        std::string content = "content " + std::to_string(i);
        db->put(static_cast<uint32_t>((i += DbTable::IDSPACING) | MegaClient::CACHEDUSER),
                static_cast<char*>(content.data()),
                static_cast<unsigned>(content.length()));
    }

    // check if auxiliar files exist
    LocalPath shmPath = currentDataBasePath;
    shmPath.append(LocalPath::fromRelativePath("-shm"));
    bool shmExists = std::filesystem::exists(shmPath.toPath(false));
    EXPECT_TRUE(shmExists) << "Unexpected behavior, -shm file doesn't exist";

    LocalPath walPath = currentDataBasePath;
    walPath.append(LocalPath::fromRelativePath("-wal"));
    bool walExists = std::filesystem::exists(walPath.toPath(false));
    EXPECT_TRUE(walExists) << "Unexpected behavior, -wal file doesn't exist";

    // Determine new path
    const std::string dbNewName{"dbNewName"};
    LocalPath newDataBasePath{dbAccess.databasePath(*fsaccess, dbNewName, DbAccess::DB_VERSION)};

    // Rename DB
    EXPECT_TRUE(dbAccess.renameDBFiles(*fsaccess, currentDataBasePath, newDataBasePath))
        << "Failure to rename files (maybe they are in use)";

    // Verify if auxiliar files exist
    if (shmExists)
    {
        shmPath = newDataBasePath;
        shmPath.append(LocalPath::fromRelativePath("-shm"));
        std::string aux = shmPath.toPath(false);
        EXPECT_TRUE(std::filesystem::exists(aux))
            << "File " << aux << "doesn't exit when it should";
    }

    if (walExists)
    {
        walPath = newDataBasePath;
        walPath.append(LocalPath::fromRelativePath("-wal"));
        std::string aux = walPath.toPath(false);
        EXPECT_TRUE(std::filesystem::exists(aux))
            << "File " << aux << "doesn't exit when it should";
    }
}
