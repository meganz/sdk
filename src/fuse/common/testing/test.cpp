#include <mega/fuse/common/error_or.h>
#include <mega/fuse/common/logging.h>
#include <mega/fuse/common/mount_info.h>
#include <mega/fuse/common/node_info.h>
#include <mega/fuse/common/testing/cloud_path.h>
#include <mega/fuse/common/testing/directory.h>
#include <mega/fuse/common/testing/model.h>
#include <mega/fuse/common/testing/parameters.h>
#include <mega/fuse/common/testing/real_client.h>
#include <mega/fuse/common/testing/test.h>
#include <mega/fuse/common/testing/utility.h>
#include <mega/fuse/platform/platform.h>

#include "tests/integration/test.h"

namespace mega
{
namespace fuse
{
namespace testing
{

Error Test::regenerate(Client& client,
                       Client& sharee,
                       const Model& model,
                       accesslevel_t permissions)
{
    // Convenience.
    using ::testing::AnyOf;

    // Make sure our two clients are friends.
    auto befriended = befriend(client, sharee);
    EXPECT_EQ(befriended, API_OK);

    // Clients don't want to be friends.
    if (befriended != API_OK)
        return befriended;

    // Check if the test root is present.
    auto handle = client.handle("/x");

    // Create the test root if necessary.
    if (handle.isUndef())
    {
        // Try and create the test root.
        auto created = client.makeDirectory("x", "/");

        // Make sure the test root was created.
        EXPECT_EQ(created.error(), API_OK);

        // Couldn't create test root.
        if (created.error() != API_OK)
            return created.error();

        // Latch the test root's handle.
        handle = *created;
    }

    // Try and share the test root with our friend.
    auto shared = client.share(sharee.email(), handle, permissions);

    // Make sure the test root was shared.
    EXPECT_EQ(shared, API_OK);

    // Couldn't share the test root with our friend.
    if (shared != API_OK)
        return shared;

    // Cloud hasn't changed state.
    if (Model::from(client, "/x/s").match(model))
        return API_OK;

    // Clear current cloud content.
    auto removed = client.removeAll("/x");

    // Make sure cloud content was removed.
    EXPECT_THAT(removed, AnyOf(API_ENOENT, API_OK));
    
    // Couldn't clear cloud content.
    if (removed != API_ENOENT && removed != API_OK)
        return removed;

    // Create scratch directory.
    Directory directory(randomName(), mScratchPath);

    // Populate scratch directory.
    model.populate(directory.path());

    // Upload new cloud content.
    auto uploaded = client.upload("/x", directory.path() / "s");

    // Make sure content was uploaded.
    EXPECT_EQ(uploaded.error(), API_OK);

    // Couldn't upload cloud content.
    if (uploaded.error() != API_OK)
        return uploaded.error();

    // Wait until our friend sees our new content.
    EXPECT_TRUE(waitFor([&]() {
        return Model::from(sharee, *uploaded).match(model);
    }, mDefaultTimeout));

    // Friend never saw our uploaded content.
    if (HasFailure())
        return LOCAL_ETIMEOUT;

    // Let the caller know that everything's set up.
    return API_OK;
}

Path Test::mDatabasePath;

Model Test::mModel;

Path Test::mStoragePath;

Watchdog Test::mWatchdog;

Test::ClientPtrArray Test::mClients;

const std::chrono::seconds Test::mDefaultTimeout(8);

Test::PathArray Test::mMountPaths;

Path Test::mScratchPath;

Test::PathArray Test::mSentinelPaths;

// Most tests run for less than 10 seconds so these limits should be fine.
static constexpr auto MaxTestCleanupTime = std::chrono::minutes(5);
static constexpr auto MaxTestRunTime     = std::chrono::minutes(5);
static constexpr auto MaxTestSetupTime   = std::chrono::minutes(10);

bool Test::DoSetUp(const Parameters& parameters)
{
    // Arm the watchdog.
    ScopedWatch watch(mWatchdog, MaxTestRunTime);

    // Make sure the clients are set up.
    allOf(mClients, [&](const ClientPtr& client) {
        auto exists = false;

        // The client should exist.
        EXPECT_TRUE((exists = !!client));

        if (!exists)
            return false;

        auto loggedIn = false;

        // The client should be logged in.
        EXPECT_TRUE((loggedIn = client->loggedIn() == FULLACCOUNT));

        if (!loggedIn)
            return false;

        auto idle = false;

        // The client should contain no mounts.
        EXPECT_TRUE((idle = client->mounts(false).empty()));

        // Specify whether files should be versioned.
        client->useVersioning(parameters.mUseVersioning);

        return idle;
    });

    // One or more clients isn't set up.
    if (HasFailure())
        return false;

    // Verify that the sentinels are no longer visible.
    allOf(mSentinelPaths, [](const Path& path) {
        auto error  = std::error_code();
        auto result = false;

        // Sentinel shouldn't exist.
        EXPECT_TRUE((result = !fs::exists(path, error) && !error));

        return result;
    });

    // One or more sentinels exist.
    if (HasFailure())
        return false;

    using ClientEntry =
      std::pair<Client*, accesslevel_t>;

    ClientEntry entries[] = {
        {ClientR().get(), RDONLY},
        {ClientW().get(), FULL}
    }; // entries

    // Regenerate clients as necessary.
    allOf(entries, [](ClientEntry& entry) {
        auto populated = API_OK;
        auto sharee = ClientS().get();

        auto client = entry.first;
        auto permissions = entry.second;

        // Regenerate state if necessary.
        EXPECT_EQ((populated = regenerate(*client,
                                          *sharee,
                                          mModel,
                                          permissions)),
                  API_OK);

        return populated == API_OK;
    });

    // Couldn't regenerate read-write state.
    if (HasFailure())
        return false;

    // Don't disarm the watchdog.
    watch.release();

    // We're done.
    return true;
}

bool Test::DoTearDown()
{
    // Make sure all mounts have been removed.
    auto result = allOf(mClients, [](ClientPtr& client) {
        // Client doesn't exist.
        if (!client)
            return false;

        auto emptied = MOUNT_SUCCESS;

        // Make sure all mounts have been removed.
        EXPECT_EQ((emptied = client->removeMounts(true)), MOUNT_SUCCESS);

        // Couldn't remove all mounts.
        if (emptied != MOUNT_SUCCESS)
            return false;

        auto empty = false;

        // Verify that all mounts have been removed.
        EXPECT_TRUE((empty = client->mounts(false).empty()));

        return empty;
    });

    // Disarm the watchdog.
    mWatchdog.disarm();

    return result;
}

ClientPtr Test::CreateClient(const std::string& name)
{
    // Where should this client store its databases?
    auto databasePath = mDatabasePath.path() / name;

    // Where should this client store its local state?
    auto storagePath = mStoragePath.path() / name;

    // So we know whether the directories were created.
    std::error_code error;

    fs::create_directories(databasePath, error);

    // Couldn't create database path.
    if (error)
        return nullptr;

    fs::create_directories(storagePath, error);

    // Couldn't create storage path.
    if (error)
        return nullptr;

    // Return client to caller.
    return std::make_unique<RealClient>(databasePath, storagePath);
}

void Test::SetUp()
{
    ASSERT_TRUE(DoSetUp(STANDARD_VERSIONED));
}

void Test::SetUpTestSuite()
{
    // Arm the watchdog.
    ScopedWatch watch(mWatchdog, MaxTestSetupTime);

    // Compute paths
    {
        auto rootPath = makeNewTestRoot() / fs::u8path("fuse");

        // Where should our clients store their databases?
        mDatabasePath = rootPath / "db";

        // Where should we store temporary state?
        mScratchPath = rootPath / "scratch";

        // Where should our clients store their state?
        mStoragePath = rootPath / "storage";

        std::error_code error;

        // Make sure the database path exists.
        fs::create_directories(mDatabasePath, error);
        ASSERT_FALSE(error);

        // Make sure the scratch path exists.
        fs::create_directories(mScratchPath, error);
        ASSERT_FALSE(error);

        // Make sure the storage path exists.
        fs::create_directory(mStoragePath, error);
        ASSERT_FALSE(error);

    }

    ClientPtrArray clients;

    // Create clients and log them in.
    {
        std::vector<std::string> names = {
            "sharee",
            "read-write",
            "read-only"
        }; // names

        // Create clients and log them in.
        allOf(clients, [&names](ClientPtr& client) {
            // Try and create client.
            EXPECT_TRUE((client = CreateClient(names.back())));

            // Couldn't create client.
            if (!client)
                return false;

            // Pop name.
            names.pop_back();

            auto loggedIn = API_OK;

            // Try and log the client in.
            EXPECT_EQ((loggedIn = client->login(names.size())), API_OK);

            // Couldn't log in.
            if (loggedIn != API_OK)
                return false;

            return true;
        });

        // Couldn't create or log in a client.
        ASSERT_FALSE(HasFailure());
    }

    // Prepare model.
    auto model = Model::generate("s", 3, 2, 2);

    model.add(Model::directory("sentinel"), "s");

    // Compute mount paths
    PathArray mountPaths = {
        /*  o */ clients[CT_READ_WRITE]->storagePath() / "observer",
        /* os */     clients[CT_SHAREE]->storagePath() / "read-write-observer",
        /*  r */  clients[CT_READ_ONLY]->storagePath() / "actor",
        /* rs */     clients[CT_SHAREE]->storagePath() / "read-only-actor",
        /*  w */ clients[CT_READ_WRITE]->storagePath() / "actor",
        /* ws */     clients[CT_SHAREE]->storagePath() / "read-write-actor"
    }; // mountPaths

    // Make sure mount paths exist.
    UNIX_ONLY({
        // Make sure mount paths exist.
        allOf(mountPaths, [](const Path& path) {
            std::error_code error;
            EXPECT_TRUE(fs::create_directories(path, error) || !error);
            return !HasFailure();
        });

        // Couldn't create mount paths.
        ASSERT_FALSE(HasFailure());
    });

    PathArray sentinelPaths = mountPaths;

    // Compute sentinel paths.
    for (auto& path : sentinelPaths)
        path /= "sentinel";

    // Persist clients, model and paths.
    mClients = std::move(clients);
    mModel = std::move(model);
    mMountPaths = std::move(mountPaths);
    mSentinelPaths = std::move(sentinelPaths);
}

void Test::TearDown()
{
    ASSERT_TRUE(DoTearDown());
}

void Test::TearDownTestSuite()
{
    // Arm watchdog.
    ScopedWatch watch(mWatchdog, MaxTestCleanupTime);

    // Destroy clients.
    for (auto& client : mClients)
        client.reset();
}

} // testing
} // fuse
} // mega

