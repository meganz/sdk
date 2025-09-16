#pragma once

#include "integration/test.h"

#include <gtest/gtest.h>
#include <mega/common/testing/path.h>

#include <chrono>
#include <filesystem>
#include <memory>

namespace mega
{
namespace common
{
namespace testing
{

template<typename Traits>
class Test: public ::testing::Test
{
    // Where will our clients put their databases?
    static Path mDatabasePath;

    // Where will out clients put their local state?
    static Path mStoragePath;

public:
    // Convenience.
    using Client = typename Traits::AbstractClient;
    using ClientPtr = std::unique_ptr<Client>;

    // Create a client.
    static ClientPtr CreateClient(const std::string& name)
    {
        // Convenience.
        using std::filesystem::create_directories;

        // Where should this client store its databases?
        auto databasePath = mDatabasePath.path() / name;

        // Where should this client store its local state?
        auto storagePath = mStoragePath.path() / name;

        // So we know whether the directories were created.
        std::error_code error;

        create_directories(databasePath, error);

        // Couldn't create database path.
        if (error)
            return nullptr;

        create_directories(storagePath, error);

        // Couldn't create storage path.
        if (error)
            return nullptr;

        // Convenience.
        using ConcreteClient = typename Traits::ConcreteClient;

        // Return client to caller.
        return std::make_unique<ConcreteClient>(name, databasePath, storagePath);
    }

    // Perform fixture-wide setup.
    static void SetUpTestSuite()
    {
        // Compute paths.
        auto rootPath = makeNewTestRoot() / fs::u8path(Traits::mName);

        // Where should our clients store their databases?
        mDatabasePath = rootPath / "db";

        // Where can our tests store temporary state?
        mScratchPath = rootPath / "scratch";

        // Where should our clients store their state?
        mStoragePath = rootPath / "storage";

        std::error_code error;

        // Make sure our paths exist.
        auto paths = {&mDatabasePath, &mScratchPath, &mStoragePath};

        for (const auto* path: paths)
        {
            std::filesystem::create_directories(*path, error);
            ASSERT_FALSE(error);
        }
    }

    // How long should we wait for something to happen?
    static const std::chrono::seconds mDefaultTimeout;

    // Where should we store temporary state?
    static common::testing::Path mScratchPath;
}; // Test<Traits>

template<typename Traits>
Path Test<Traits>::mDatabasePath;

template<typename Traits>
const std::chrono::seconds Test<Traits>::mDefaultTimeout(8);

template<typename Traits>
Path Test<Traits>::mScratchPath;

template<typename Traits>
Path Test<Traits>::mStoragePath;

} // testing
} // common
} // mega
