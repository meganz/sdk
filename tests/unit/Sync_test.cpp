/**
 * (c) 2019 by Mega Limited, Wellsford, New Zealand
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

#include <memory>
#include <numeric>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <mega/megaclient.h>
#include <mega/megaapp.h>
#include <mega/types.h>
#include <mega/heartbeats.h>
#include <mega/sync.h>
#include <mega/filesystem.h>

#include "constants.h"
#include "FsNode.h"
#include "DefaultedDbTable.h"
#include "DefaultedDirAccess.h"
#include "DefaultedFileAccess.h"
#include "DefaultedFileSystemAccess.h"
#include "utils.h"

#ifdef ENABLE_SYNC

namespace mega {
    enum { TYPE_TWOWAY = SyncConfig::TYPE_TWOWAY };
    enum { TYPE_UP = SyncConfig::TYPE_UP };
    enum { TYPE_DOWN = SyncConfig::TYPE_DOWN };
};

namespace SyncConfigTests
{

using namespace mega;
using namespace testing;

class Directory
{
public:
    Directory(FSACCESS_CLASS& fsAccess, const LocalPath& path)
      : mFSAccess(fsAccess)
      , mPath(path)
    {
        mFSAccess.mkdirlocal(mPath, false, true);
    }

    ~Directory()
    {
        mFSAccess.emptydirlocal(mPath);
        mFSAccess.rmdirlocal(mPath);
    }

    MEGA_DISABLE_COPY_MOVE(Directory)

    operator const LocalPath&() const
    {
        return mPath;
    }

    const LocalPath& path() const
    {
        return mPath;
    }

private:
    FSACCESS_CLASS& mFSAccess;
    LocalPath mPath;
}; // Directory

// Temporary shims so that we can easily switch to using
// NiceMock / FakeStrictMock when GMock/GTest is upgraded on Jenkins.
#if 0

template<typename MockClass>
class FakeNiceMock
    : public MockClass
{
public:
        using MockClass::MockClass;
}; // FakeNiceMock<T>

template<typename MockClass>
class FakeStrictMock
    : public MockClass
{
public:
        using MockClass::MockClass;
}; // FakeStrictMock<T>

#else

template<typename T>
using FakeNiceMock = NiceMock<T>;

template<typename T>
using FakeStrictMock = StrictMock<T>;

#endif

class Utilities
{
public:
    static string randomBase64(const size_t n = 16)
    {
        return Base64::btoa(randomBytes(n));
    }

    static string randomBytes(const size_t n)
    {
        return mRNG.genstring(n);
    }

    static bool randomFile(LocalPath path, const size_t n = 64)
    {
        auto fileAccess = mFSAccess.newfileaccess(false);

        if (!fileAccess->fopen(path, false, true, FSLogging::logOnError))
        {
            return false;
        }

        if (fileAccess->size > 0)
        {
            if (!fileAccess->ftruncate())
            {
                return false;
            }
        }

        const string data = randomBytes(n);
        const byte* bytes = reinterpret_cast<const byte*>(&data[0]);

        return fileAccess->fwrite(bytes, static_cast<unsigned>(n), 0x0);
    }

    static LocalPath randomPath(const LocalPath& prefix, const size_t n = 16)
    {
        LocalPath result = prefix;

        result.appendWithSeparator(randomPathRelative(n), false);

        return result;
    }

    static LocalPath randomPathAbsolute(const size_t n = 16)
    {
        return LocalPath::fromAbsolutePath(randomBase64(n));
    }
    static LocalPath randomPathRelative(const size_t n = 16)
    {
        return LocalPath::fromRelativePath(randomBase64(n));
    }

    static LocalPath separator()
    {
#ifdef _WIN32
        return LocalPath::fromRelativePath("\\");
#else // _WIN32
        return LocalPath::fromRelativePath("/");
#endif // ! _WIN32
    }

private:
    static FSACCESS_CLASS mFSAccess;
    static PrnGen mRNG;
}; // Utilities

FSACCESS_CLASS Utilities::mFSAccess;
PrnGen Utilities::mRNG;

class SyncConfigTest
  : public Test
{
public:
    class IOContext
      : public SyncConfigIOContext
    {
    public:
        IOContext(FileSystemAccess& fsAccess,
                  const string& authKey,
                  const string& cipherKey,
                  const string& name,
                  PrnGen& rng)
          : SyncConfigIOContext(fsAccess,
                                    authKey,
                                    cipherKey,
                                    name,
                                    rng)
        {
            // Perform real behavior by default.
            ON_CALL(*this, driveID(_))
              .WillByDefault(Invoke(this, &IOContext::driveIDConcrete));

            ON_CALL(*this, getSlotsInOrder(_, _))
              .WillByDefault(Invoke(this, &IOContext::getSlotsInOrderConcrete));

            ON_CALL(*this, read(_, _, _))
              .WillByDefault(Invoke(this, &IOContext::readConcrete));

            ON_CALL(*this, remove(_, _))
              .WillByDefault(Invoke(this, &IOContext::removeSlotConcrete));

            ON_CALL(*this, remove(_))
              .WillByDefault(Invoke(this, &IOContext::removeAllSlotsConcrete));

            ON_CALL(*this, write(_, _, _))
              .WillByDefault(Invoke(this, &IOContext::writeConcrete));
        }

        MOCK_CONST_METHOD1(driveID, handle(const LocalPath&));

        MOCK_METHOD2(getSlotsInOrder, error(const LocalPath&, vector<unsigned int>&));

        MOCK_METHOD3(read, error(const LocalPath&, string&, unsigned int));

        MOCK_METHOD2(remove, error(const LocalPath&, unsigned int));

        MOCK_METHOD1(remove, error(const LocalPath&));

        MOCK_METHOD3(write, error(const LocalPath&, const string&, unsigned int));

    private:
        // Delegate to real behavior.
        handle driveIDConcrete(const LocalPath& drivePath)
        {
            return SyncConfigIOContext::driveID(drivePath);
        }

        error getSlotsInOrderConcrete(const LocalPath& dbPath,
                                      vector<unsigned int>& slotsVec)
        {
            return SyncConfigIOContext::getSlotsInOrder(dbPath, slotsVec);
        }

        error readConcrete(const LocalPath& dbPath,
                           string& data,
                           unsigned int slot)
        {
            return SyncConfigIOContext::read(dbPath, data, slot);
        }

        error removeSlotConcrete(const LocalPath& dbPath,
                                 unsigned int slot)
        {
            return SyncConfigIOContext::remove(dbPath, slot);
        }

        error removeAllSlotsConcrete(const LocalPath& dbPath)
        {
            return SyncConfigIOContext::remove(dbPath);
        }

        error writeConcrete(const LocalPath& dbPath,
                            const string& data,
                            unsigned int slot)
        {
            return SyncConfigIOContext::write(dbPath, data, slot);
        }
    }; // IOContext

    SyncConfigTest()
      : Test()
      , mFSAccess()
      , mRNG()
      , mConfigAuthKey(Utilities::randomBytes(16))
      , mConfigCipherKey(Utilities::randomBytes(16))
      , mConfigName(Utilities::randomBase64(16))
      , mIOContext(mFSAccess,
                   mConfigAuthKey,
                   mConfigCipherKey,
                   mConfigName,
                   mRNG)
    {
    }

    string emptyDB() const
    {
        return "{\"sy\":[]}";
    }

    FSACCESS_CLASS& fsAccess()
    {
        return mFSAccess;
    }

    IOContext& ioContext()
    {
        return mIOContext;
    }

protected:
    FSACCESS_CLASS mFSAccess;
    PrnGen mRNG;
    const string mConfigAuthKey;
    const string mConfigCipherKey;
    const string mConfigName;
    FakeNiceMock<IOContext> mIOContext;
}; // SyncConfigTest

class SyncConfigIOContextTest
  : public SyncConfigTest
{
public:
    SyncConfigIOContextTest()
      : SyncConfigTest()
    {
    }

    string configName() const
    {
        return configPrefix() + mConfigName;
    }

    const string& configPrefix() const
    {
        return SyncConfigIOContext::NAME_PREFIX;
    }
}; // SyncConfigIOContextTest

TEST_F(SyncConfigIOContextTest, GetBadPath)
{
    vector<unsigned int> slotsVec;

    // Generate a bogus path.
    const auto drivePath = Utilities::randomPathAbsolute();

    // Try to read slots from an invalid path.
    EXPECT_NE(ioContext().getSlotsInOrder(drivePath, slotsVec), API_OK);

    // Slots should be empty.
    EXPECT_TRUE(slotsVec.empty());
}

TEST_F(SyncConfigIOContextTest, GetNoSlots)
{
    // Make sure the drive path exists.
    Directory drive(fsAccess(), Utilities::randomPathAbsolute());

    // Generate some malformed slots for this user.
    {
        LocalPath configPath = drive;

        // This file will be ignored as it has no slot suffix.
        configPath.appendWithSeparator(
          LocalPath::fromRelativePath(configName()), false);
        EXPECT_TRUE(Utilities::randomFile(configPath));

        // This file will be ignored as it has a malformed slot suffix.
        configPath.append(LocalPath::fromRelativePath("."));
        EXPECT_TRUE(Utilities::randomFile(configPath));

        // This file will be ignored as it has an invalid slot suffix.
        configPath.append(LocalPath::fromRelativePath("Q"));
        EXPECT_TRUE(Utilities::randomFile(configPath));
    }

    // Generate a slot for a different user.
    {
        LocalPath configPath = drive;

        configPath.appendWithSeparator(
          LocalPath::fromRelativePath(configPrefix()), false);
        configPath.append(Utilities::randomPathRelative());
        configPath.append(LocalPath::fromRelativePath(".0"));

        EXPECT_TRUE(Utilities::randomFile(configPath));
    }

    vector<unsigned int> slotsVec;

    // Try and get a list of slots.
    EXPECT_EQ(ioContext().getSlotsInOrder(drive.path(), slotsVec), API_OK);

    // Slots should be empty.
    EXPECT_TRUE(slotsVec.empty());
}

TEST_F(SyncConfigIOContextTest, GetSlotsOrderedByModificationTime)
{
    const size_t NUM_SLOTS = 3;

    // Make sure drive path exists.
    Directory drive(fsAccess(), Utilities::randomPathAbsolute());

    // Generate some slots for this user.
    {
        LocalPath configPath = drive;

        // Generate suitable config path prefix.
        configPath.appendWithSeparator(
          LocalPath::fromRelativePath(configName()), false);

        for (size_t i = 0; i < NUM_SLOTS; ++i)
        {
            using std::to_string;

            ScopedLengthRestore restorer(configPath);

            // Generate suffix.
            LocalPath suffixPath =
              LocalPath::fromRelativePath("." + to_string(i));

            // Complete config path.
            configPath.append(suffixPath);

            // Populate the file.
            EXPECT_TRUE(Utilities::randomFile(configPath));

            // Set the modification time.
            EXPECT_TRUE(fsAccess().setmtimelocal(configPath, i * 1000));
        }
    }

    vector<unsigned int> slotsVec;

    // Get the slots.
    EXPECT_EQ(ioContext().getSlotsInOrder(drive.path(), slotsVec), API_OK);

    // Did we retrieve the correct number of slots?
    ASSERT_EQ(slotsVec.size(), NUM_SLOTS);

    // Are the slots ordered by descending modification time?
    {
        vector<unsigned int> expected(NUM_SLOTS, 0);

        iota(begin(expected), end(expected), 0);

        EXPECT_TRUE(equal(begin(expected), end(expected), slotsVec.rbegin()));
    }
}

TEST_F(SyncConfigIOContextTest, GetSlotsOrderedBySlotSuffix)
{
    const size_t NUM_SLOTS = 3;

    // Make sure drive path exists.
    Directory drive(fsAccess(), Utilities::randomPathAbsolute());

    // Generate some slots for this user.
    {
        LocalPath configPath = drive;

        // Generate suitable config path prefix.
        configPath.appendWithSeparator(
          LocalPath::fromRelativePath(configName()), false);

        for (size_t i = 0; i < NUM_SLOTS; ++i)
        {
            using std::to_string;

            ScopedLengthRestore restorer(configPath);

            // Generate suffix.
            LocalPath suffixPath =
              LocalPath::fromRelativePath("." + to_string(i));

            // Complete config path.
            configPath.append(suffixPath);

            // Populate the file.
            EXPECT_TRUE(Utilities::randomFile(configPath));

            // Set the modification time.
            EXPECT_TRUE(fsAccess().setmtimelocal(configPath, 0));
        }
    }

    vector<unsigned int> slotsVec;

    // Get the slots.
    EXPECT_EQ(ioContext().getSlotsInOrder(drive.path(), slotsVec), API_OK);

    // Did we retrieve the correct number of slots?
    EXPECT_EQ(slotsVec.size(), NUM_SLOTS);

    // Are the slots ordered by descending slot number when their
    // modification time is the same?
    {
        vector<unsigned int> expected(NUM_SLOTS, 0);

        iota(begin(expected), end(expected), 0);

        EXPECT_TRUE(equal(begin(expected), end(expected), slotsVec.rbegin()));
    }
}

TEST_F(SyncConfigIOContextTest, Read)
{
    // Make sure the drive path exists.
    Directory drive(fsAccess(), Utilities::randomPathAbsolute());

    // Try writing some data out and reading it back again.
    {
        string read;
        string written = Utilities::randomBytes(64);

        EXPECT_EQ(ioContext().write(drive.path(), written, 0), API_OK);
        EXPECT_EQ(ioContext().read(drive.path(), read, 0), API_OK);
        EXPECT_EQ(read, written);
    }

    // Try a different slot to make sure it has an effect.
    {
        string read;
        string written = Utilities::randomBytes(64);

        EXPECT_EQ(ioContext().read(drive.path(), read, 1), API_EREAD);
        EXPECT_TRUE(read.empty());

        EXPECT_EQ(ioContext().write(drive.path(), written, 1), API_OK);
        EXPECT_EQ(ioContext().read(drive.path(), read, 1), API_OK);
        EXPECT_EQ(read, written);
    }
}

TEST_F(SyncConfigIOContextTest, ReadBadData)
{
    string data;

    // Make sure the drive path exists.
    Directory drive(fsAccess(), Utilities::randomPathAbsolute());

    // Generate slot path.
    LocalPath slotPath = drive;

    slotPath.appendWithSeparator(
      LocalPath::fromRelativePath(configName()), false);

    slotPath.append(LocalPath::fromRelativePath(".0"));

    // Try loading a file that's too short to be valid.
    EXPECT_TRUE(Utilities::randomFile(slotPath, 1));
    EXPECT_EQ(ioContext().read(drive.path(), data, 0), API_EREAD);
    EXPECT_TRUE(data.empty());

    // Try loading a file composed entirely of junk.
    EXPECT_TRUE(Utilities::randomFile(slotPath, 128));
    EXPECT_EQ(ioContext().read(drive.path(), data, 0), API_EREAD);
    EXPECT_TRUE(data.empty());
}

TEST_F(SyncConfigIOContextTest, ReadBadPath)
{
    const LocalPath drivePath = Utilities::randomPathAbsolute();
    string data;

    // Try and read data from an insane path.
    EXPECT_EQ(ioContext().read(drivePath, data, 0), API_EREAD);
    EXPECT_TRUE(data.empty());
}

TEST_F(SyncConfigIOContextTest, RemoveSlot)
{
    // Make sure drive path exists.
    Directory drive(fsAccess(), Utilities::randomPathAbsolute());

    // Generate a slot for this user.
    {
        LocalPath configPath = drive;

        // Generate path prefix.
        configPath.appendWithSeparator(
            LocalPath::fromRelativePath(configName()), false);

        // Generate suffix.
        configPath.append(LocalPath::fromRelativePath(".0"));

        // Populate slot.
        EXPECT_TRUE(Utilities::randomFile(configPath));
    }

    // Remove the slot.
    EXPECT_EQ(ioContext().remove(drive.path(), 0), API_OK);

    // Remove again won't fail since we don't want errors in the log that are not actually problems
    EXPECT_EQ(ioContext().remove(drive.path(), 0), API_OK);
}

TEST_F(SyncConfigIOContextTest, RemoveSlots)
{
    const auto drivePath = Utilities::randomPathAbsolute();

    // No slots to remove.
    EXPECT_CALL(ioContext(),
                getSlotsInOrder(Eq(drivePath), _))
      .WillOnce(Return(API_ENOENT));

    EXPECT_EQ(ioContext().remove(drivePath), API_ENOENT);

    // Two slots to remove.
    static const vector<unsigned int> slotsVec = {0, 1};

    EXPECT_CALL(ioContext(),
                getSlotsInOrder(Eq(drivePath), _))
      .WillRepeatedly(DoAll(SetArgReferee<1>(slotsVec),
                            Return(API_OK)));

    // All slots should be removed successfully.
    EXPECT_CALL(ioContext(),
                remove(Eq(drivePath), _))
      .WillRepeatedly(Return(API_OK));

    EXPECT_EQ(ioContext().remove(drivePath), API_OK);

    // Should only succeed if all slots can be removed.
    EXPECT_CALL(ioContext(),
                remove(Eq(drivePath), Eq(0u)))
      .WillRepeatedly(Return(API_EWRITE));

    EXPECT_EQ(ioContext().remove(drivePath), API_EWRITE);
}

TEST_F(SyncConfigIOContextTest, Serialize)
{
    SyncConfigVector read;
    SyncConfigVector written;
    JSONWriter writer;

    // Populate the database with two configs.
    {
        SyncConfig config;

        config.mBackupId = 1;
        config.mEnabled = false;
        config.mError = NO_SYNC_ERROR;
        config.mFilesystemFingerprint = fsfp_t(2, "1");
        config.mLocalPath = Utilities::randomPathAbsolute();
        config.mName = Utilities::randomBase64();
        config.mOriginalPathOfRemoteRootNode = Utilities::randomBase64();
        config.mRemoteNode = NodeHandle();
        config.mWarning = NO_SYNC_WARNING;
        config.mSyncType = SyncConfig::TYPE_TWOWAY;
        config.mBackupState = SYNC_BACKUP_NONE;

        written.emplace_back(config);

        config.mBackupId = 2;
        config.mEnabled = true;
        config.mError = UNKNOWN_ERROR;
        config.mFilesystemFingerprint = fsfp_t(2, "2");
        config.mLocalPath = Utilities::randomPathAbsolute();
        config.mName = Utilities::randomBase64();
        config.mOriginalPathOfRemoteRootNode = Utilities::randomBase64();
        config.mRemoteNode.set6byte(3);
        config.mWarning = LOCAL_IS_FAT;
        config.mSyncType = SyncConfig::TYPE_BACKUP;
        config.mBackupState = SYNC_BACKUP_MIRROR;

        written.emplace_back(config);
    }

    // Serialize the database.
    {
        ioContext().serialize(written, writer);
        EXPECT_FALSE(writer.getstring().empty());
    }

    // Deserialize the database.
    {
        JSON reader(writer.getstring());
        EXPECT_TRUE(ioContext().deserialize(read, reader, false));
    }

    // Are the databases identical?
    ASSERT_EQ(read.size(), written.size());
    for (auto i = read.size(); i--; )
    {
        auto& a = read[i];
        auto& b = written[i];
        EXPECT_EQ(a.mBackupId, b.mBackupId);
        EXPECT_EQ(a.mEnabled, b.mEnabled);
        EXPECT_EQ(a.mError, b.mError);
        EXPECT_EQ(a.mFilesystemFingerprint, b.mFilesystemFingerprint);
        EXPECT_EQ(a.mLocalPath, b.mLocalPath);
        EXPECT_EQ(a.mName, b.mName);
        EXPECT_EQ(a.mOriginalPathOfRemoteRootNode, b.mOriginalPathOfRemoteRootNode);
        EXPECT_EQ(a.mRemoteNode, b.mRemoteNode);
        EXPECT_EQ(a.mWarning, b.mWarning);
        EXPECT_EQ(a.mSyncType, b.mSyncType);
        EXPECT_EQ(a.mBackupState, b.mBackupState);
    }
}

TEST_F(SyncConfigIOContextTest, SerializeEmpty)
{
    JSONWriter writer;

    // Serialize an empty database.
    {
        static const SyncConfigVector empty;

        // Does serializing an empty database yield an empty array?
        ioContext().serialize(empty, writer);
        EXPECT_EQ(writer.getstring(), emptyDB());
    }

    // Deserialize the empty database.
    {
        SyncConfigVector configs;
        JSON reader(writer.getstring());

        // Can we deserialize an empty database?
        EXPECT_TRUE(ioContext().deserialize(configs, reader, true));
        EXPECT_TRUE(configs.empty());
    }
}

TEST_F(SyncConfigIOContextTest, WriteBadPath)
{
    const LocalPath drivePath = Utilities::randomPathAbsolute();
    const string data = Utilities::randomBytes(64);

    auto dbPath = drivePath;
    dbPath.appendWithSeparator(Utilities::randomPathRelative(), false);

    // Try and write data to an insane path.
    EXPECT_NE(ioContext().write(dbPath, data, 0), API_OK);
}

class SyncConfigStoreTest
  : public SyncConfigTest
{
public:
    SyncConfigStoreTest()
      : SyncConfigTest()
    {
    }
}; // SyncConfigStoreTest

TEST_F(SyncConfigStoreTest, Read)
{
    Directory db(fsAccess(), Utilities::randomPathAbsolute());

    SyncConfigVector written;

    // Create a database for later reading.
    {
        SyncConfigStore store(db, ioContext());

        // Read empty so that the drive is known.
        EXPECT_EQ(store.read(LocalPath(), written, false), API_ENOENT);

        // Drive should be known.
        ASSERT_TRUE(store.driveKnown(LocalPath()));

        // Create a config for writing.
        SyncConfig config;

        config.mBackupId = 1;
        config.mLocalPath = Utilities::randomPathAbsolute();
        config.mRemoteNode.set6byte(2);

        written.emplace_back(config);

        // Write database to disk.
        EXPECT_EQ(store.write(LocalPath(), written), API_OK);
    }

    SyncConfigStore store(db, ioContext());

    SyncConfigVector read;

    // Read database back.
    EXPECT_EQ(store.read(LocalPath(), read, false), API_OK);

    // Drive should now be known.
    EXPECT_TRUE(store.driveKnown(LocalPath()));

    // Configs should be precisely what we wrote.
    ASSERT_EQ(read.size(), written.size());
    for (auto i = read.size(); i--; )
    {
        auto& a = read[i];
        auto& b = written[i];
        EXPECT_EQ(a.mBackupId, b.mBackupId);
        EXPECT_EQ(a.mEnabled, b.mEnabled);
        EXPECT_EQ(a.mError, b.mError);
        EXPECT_EQ(a.mFilesystemFingerprint, b.mFilesystemFingerprint);
        EXPECT_EQ(a.mLocalPath, b.mLocalPath);
        EXPECT_EQ(a.mName, b.mName);
        EXPECT_EQ(a.mOriginalPathOfRemoteRootNode, b.mOriginalPathOfRemoteRootNode);
        EXPECT_EQ(a.mRemoteNode, b.mRemoteNode);
        EXPECT_EQ(a.mWarning, b.mWarning);
        EXPECT_EQ(a.mSyncType, b.mSyncType);
        EXPECT_EQ(a.mBackupState, b.mBackupState);
    }
}

TEST_F(SyncConfigStoreTest, ReadEmpty)
{
    Directory db(fsAccess(), Utilities::randomPathAbsolute());

    SyncConfigStore store(db, ioContext());

    // No slots available for reading.
    EXPECT_CALL(ioContext(),
                getSlotsInOrder(Eq(db.path()), _))
      .WillOnce(Return(API_ENOENT));

    // No attempts should be made to read from disk.
    EXPECT_CALL(ioContext(),
                read(Eq(db.path()), _, _))
      .Times(0);

    SyncConfigVector configs;

    // Read should inform the caller that no database is present.
    EXPECT_EQ(store.read(LocalPath(), configs, false), API_ENOENT);

    // Configs should remain empty.
    EXPECT_TRUE(configs.empty());

    // Drive should be known as read didn't signal a fatal error.
    EXPECT_TRUE(store.driveKnown(LocalPath()));

    // Store should remain clean.
    EXPECT_FALSE(store.dirty());
}

TEST_F(SyncConfigStoreTest, ReadFailNoDriveID)
{
    Directory db(fsAccess(), Utilities::randomPathAbsolute());
    Directory drive(fsAccess(), Utilities::randomPathAbsolute());

    SyncConfigStore store(db, ioContext());

    // Shouldn't try to read slots if we can't read the drive ID.
    EXPECT_CALL(ioContext(),
                getSlotsInOrder(_, _))
      .Times(0);

    // Read of drive ID should fail.
    EXPECT_CALL(ioContext(),
                driveID(Eq(drive.path())))
      .WillOnce(Return(UNDEF));

    SyncConfigVector configs;

    // Read should report a fatal read error.
    EXPECT_EQ(store.read(drive, configs, true), API_EREAD);

    // Drive shouldn't be known.
    EXPECT_FALSE(store.driveKnown(drive));

    // No slots available for reading.
    EXPECT_CALL(ioContext(),
                getSlotsInOrder(_, _))
      .WillOnce(Return(API_ENOENT));

    // Drive ID read should succeed.
    EXPECT_CALL(ioContext(),
                driveID(Eq(drive.path())))
      .WillOnce(Return(1u));

    // Read should report no entries.
    EXPECT_EQ(store.read(drive, configs, true), API_ENOENT);

    // Drive should now be known.
    EXPECT_TRUE(store.driveKnown(drive));

    // Drive ID should be cached.
    EXPECT_EQ(store.driveID(drive), 1u);
}

TEST_F(SyncConfigStoreTest, ReadFail)
{
    Directory db(fsAccess(), Utilities::randomPathAbsolute());

    SyncConfigStore store(db, ioContext());

    // Return a single slot for reading.
    Expectation get =
      EXPECT_CALL(ioContext(),
                  getSlotsInOrder(Eq(db.path()), _))
        .WillOnce(DoAll(SetArgReferee<1>(vector<unsigned>{0}),
                        Return(API_OK)));

    // Attempts to read the slot should fail.
    Expectation read =
      EXPECT_CALL(ioContext(),
                  read(Eq(db.path()), _, Eq(0u)))
        .After(get)
        .WillOnce(Return(API_EREAD));

    SyncConfigVector configs;

    // Read should report a fatal read error.
    EXPECT_EQ(store.read(LocalPath(), configs, false), API_EREAD);

    // Configs should remain empty.
    EXPECT_TRUE(configs.empty());

    // Drive should remain unknown as read failed fatally.
    EXPECT_FALSE(store.driveKnown(LocalPath()));

    // Store should remain clean.
    EXPECT_FALSE(store.dirty());
}

TEST_F(SyncConfigStoreTest, ReadFailFallback)
{
    Directory db(fsAccess(), Utilities::randomPathAbsolute());

    SyncConfigStore store(db, ioContext());

    // Return three slots for reading.
    Expectation get =
      EXPECT_CALL(ioContext(),
                  getSlotsInOrder(Eq(db.path()), _))
        .WillOnce(DoAll(SetArgReferee<1>(vector<unsigned>{2, 1, 0}),
                        Return(API_OK)));

    // Attempts to read slots 2 and 1 should fail.
    Expectation read21 =
      EXPECT_CALL(ioContext(),
                  read(Eq(db.path()), _, _))
        .Times(2)
        .After(get)
        .WillRepeatedly(Return(API_EREAD));

    // Attempts to read slot 0 should succeed.
    Expectation read0 =
      EXPECT_CALL(ioContext(),
                  read(Eq(db.path()), _, Eq(0u)))
        .After(read21)
        .WillOnce(DoAll(SetArgReferee<1>(emptyDB()),
                        Return(API_OK)));

    SyncConfigVector configs;

    // Read should succeed.
    EXPECT_EQ(store.read(LocalPath(), configs, false), API_OK);

    // Configs should remain empty.
    EXPECT_TRUE(configs.empty());

    // Drive should be known as the read eventually succeeded.
    EXPECT_TRUE(store.driveKnown(LocalPath()));

    // Should should remain clean.
    EXPECT_FALSE(store.dirty());
}

TEST_F(SyncConfigStoreTest, WriteDirty)
{
    Directory db(fsAccess(), Utilities::randomPathAbsolute());

    SyncConfigStore store(db, ioContext());

    SyncConfigVector configs;

    // Perform a read such that the drive is known.
    EXPECT_EQ(store.read(LocalPath(), configs, false), API_ENOENT);

    SyncConfigVector internal;

    // Populate configs.
    {
        SyncConfig config;

        // External
        config.mBackupId = 1;
        config.mExternalDrivePath = Utilities::randomPathAbsolute();
        config.mLocalPath = Utilities::randomPath(config.mExternalDrivePath);
        config.mRemoteNode.set6byte(2);

        configs.emplace_back(config);

        // Internal
        config.mBackupId = 2;
        config.mExternalDrivePath.clear();
        config.mLocalPath = Utilities::randomPathAbsolute();
        config.mRemoteNode.set6byte(3);

        configs.emplace_back(config);
        internal.emplace_back(config);
    }

    // Mark internal database for writing.
    store.markDriveDirty(LocalPath());

    // Serialize configs for later comparison.
    JSONWriter writer;

    ioContext().serialize(internal, writer);

    // First attempt to write the database should fail.
    // Subsequent attempts should succeed.
    EXPECT_CALL(ioContext(),
                write(Eq(db.path()), Eq(writer.getstring()), Eq(0u)))
      .Times(2)
      .WillOnce(Return(API_EWRITE))
      .WillRepeatedly(Return(API_OK));

    // First attempt to write dirty databases should fail.
    store.writeDirtyDrives(configs);

    // Store should be clean.
    EXPECT_FALSE(store.dirty());

    // Mark drive as dirty.
    store.markDriveDirty(LocalPath());

    // Second attempt to write dirty databases should succeed.
    store.writeDirtyDrives(configs);

    // Store should now be clean.
    EXPECT_FALSE(store.dirty());
}

TEST_F(SyncConfigStoreTest, WriteEmpty)
{
    Directory db(fsAccess(), Utilities::randomPathAbsolute());

    SyncConfigStore store(db, ioContext());

    SyncConfigVector configs;

    // Read empty so that the drive is known.
    EXPECT_EQ(store.read(LocalPath(), configs, false), API_ENOENT);

    // Drive should now be known.
    ASSERT_TRUE(store.driveKnown(LocalPath()));

    // Attempt to remove the database should fail.
    EXPECT_CALL(ioContext(),
                remove(Eq(db.path())))
      .WillOnce(Return(API_EWRITE));

    // Mark drive as dirty.
    store.markDriveDirty(LocalPath());

    // Write should fail.
    EXPECT_EQ(store.write(LocalPath(), configs), API_EWRITE);

    // Store should be clean.
    EXPECT_FALSE(store.dirty());

    // Mark drive as dirty.
    store.markDriveDirty(LocalPath());

    // Next attempt to remove should succeed.
    EXPECT_CALL(ioContext(),
                remove(Eq(db.path())))
      .WillOnce(Return(API_OK));

    // Write should succeed.
    EXPECT_EQ(store.write(LocalPath(), configs), API_OK);

    // Store should now be clean.
    EXPECT_FALSE(store.dirty());
}

TEST_F(SyncConfigStoreTest, Write)
{
    Directory db(fsAccess(), Utilities::randomPathAbsolute());

    SyncConfigStore store(db, ioContext());

    SyncConfigVector configs;

    // Read empty so that the drive is known.
    EXPECT_EQ(store.read(LocalPath(), configs, false), API_ENOENT);

    // Drive should be known.
    ASSERT_TRUE(store.driveKnown(LocalPath()));

    // Populate config vector.
    {
        SyncConfig config;

        config.mBackupId = 2;
        config.mLocalPath = Utilities::randomPathAbsolute();
        config.mRemoteNode.set6byte(3);

        configs.emplace_back(config);
    }

    // Serialize configs for later comparison.
    JSONWriter writer;

    ioContext().serialize(configs, writer);

    // Attempts to write the first slot should fail.
    EXPECT_CALL(ioContext(),
                write(Eq(db.path()), Eq(writer.getstring()), Eq(0u)))
      .WillOnce(Return(API_EWRITE));

    // No databases should be removed in anticipation as the write failed.
    EXPECT_CALL(ioContext(),
                remove(Eq(db.path()), Eq(1u)))
      .Times(0);

    // Mark drive as being dirty.
    store.markDriveDirty(LocalPath());

    // Write should fail.
    EXPECT_EQ(store.write(LocalPath(), configs), API_EWRITE);

    // Store should be clean.
    EXPECT_FALSE(store.dirty());

    // Mark drive as dirty.
    store.markDriveDirty(LocalPath());

    // Attempts to write the first slot should succeed.
    EXPECT_CALL(ioContext(),
                write(Eq(db.path()), Eq(writer.getstring()), Eq(0u)))
      .WillOnce(Return(API_OK));

    // Next slot should be removed in anticipation of future writes.
    EXPECT_CALL(ioContext(),
                remove(Eq(db.path()), Eq(1u)))
      .WillOnce(Return(API_OK));

    // Write should succeed.
    EXPECT_EQ(store.write(LocalPath(), configs), API_OK);

    // Store should be clean.
    EXPECT_FALSE(store.dirty());
}

} // SyncConfigTests

#endif

