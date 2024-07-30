#pragma once

#include <chrono>

#include <mega/fuse/common/testing/client_forward.h>
#include <mega/fuse/common/testing/model.h>
#include <mega/fuse/common/testing/parameters_forward.h>
#include <mega/fuse/common/testing/path.h>
#include <mega/fuse/common/testing/watchdog.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace mega
{
namespace fuse
{
namespace testing
{

class Test
  : public ::testing::Test
{
    // For clarity.
    enum ClientType : std::size_t {
        // Client for read-only access.
        CT_READ_ONLY,
        // Client for read-write access.
        CT_READ_WRITE,
        // Client for share access.
        CT_SHAREE,
        // How many client types there are.
        NUM_CLIENT_TYPES
    }; // ClientType

    enum PathType : std::size_t {
        // Path for observing changes.
        PT_OBSERVER,
        // Path for observing share changes.
        PT_OBSERVER_SHARE,
        // Path for read-only accesses.
        PT_READ_ONLY,
        // Path for read-only share accesses.
        PT_READ_ONLY_SHARE,
        // Path for read-write accesses.
        PT_READ_WRITE,
        // Path for read-write share accesses.
        PT_READ_WRITE_SHARE,
        // Number of path types.
        NUM_PATH_TYPES
    }; // PathType

    // Convenience.
    using ClientPtrArray =
      std::array<ClientPtr, NUM_CLIENT_TYPES>;
    
    using PathArray =
      std::array<Path, NUM_PATH_TYPES>;

    // Regenerate cloud content if necessary.
    static Error regenerate(Client& client,
                            Client& sharee,
                            const Model& model,
                            accesslevel_t permissions);

    // What clients should we use for testing?
    static ClientPtrArray mClients;

    // Where will our clients put their databases?
    static Path mDatabasePath;

    // Expected contents of the cloud.
    static Model mModel;

    // Where should we mount cloud entities?
    static PathArray mMountPaths;

    // Where are our sentinels?
    static PathArray mSentinelPaths;

    // Where will out clients put their local state?
    static Path mStoragePath;

    // Makes sure our tests don't run forever.
    static Watchdog mWatchdog;

protected:
    // Perform fixture-specific setup.
    virtual bool DoSetUp(const Parameters& parameters);

    // Prform fixture-specific teardown.
    virtual bool DoTearDown();

public:
    // Defines a client accessor method.
    #define DEFINE_CLIENT_ACCESSOR(name, slot) \
        static ClientPtr& Client##name() \
        { \
            return mClients[(slot)]; \
        }

    // Define client accessor methods.
    DEFINE_CLIENT_ACCESSOR(R, CT_READ_ONLY);
    DEFINE_CLIENT_ACCESSOR(W, CT_READ_WRITE);
    DEFINE_CLIENT_ACCESSOR(S, CT_SHAREE);

    // Remove macro from scope.
    #undef DEFINE_CLIENT_ACCESSOR

    // Create a client.
    static ClientPtr CreateClient(const std::string& name);

    // Defines a path accesor method.
    #define DEFINE_PATH_ACCESSOR(name, scope, slot) \
        static const Path& scope##name() \
        { \
            return m##scope##s[(slot)]; \
        }

    // Define mount path accessors.
    DEFINE_PATH_ACCESSOR(O,  MountPath, PT_OBSERVER);
    DEFINE_PATH_ACCESSOR(OS, MountPath, PT_OBSERVER_SHARE);
    DEFINE_PATH_ACCESSOR(R,  MountPath, PT_READ_ONLY);
    DEFINE_PATH_ACCESSOR(RS, MountPath, PT_READ_ONLY_SHARE);
    DEFINE_PATH_ACCESSOR(W,  MountPath, PT_READ_WRITE);
    DEFINE_PATH_ACCESSOR(WS, MountPath, PT_READ_WRITE_SHARE);

    // Define sentinel path accessors.
    DEFINE_PATH_ACCESSOR(O,  SentinelPath, PT_OBSERVER);
    DEFINE_PATH_ACCESSOR(OS, SentinelPath, PT_OBSERVER_SHARE);
    DEFINE_PATH_ACCESSOR(R,  SentinelPath, PT_READ_ONLY);
    DEFINE_PATH_ACCESSOR(RS, SentinelPath, PT_READ_ONLY_SHARE);
    DEFINE_PATH_ACCESSOR(W,  SentinelPath, PT_READ_WRITE);
    DEFINE_PATH_ACCESSOR(WS, SentinelPath, PT_READ_WRITE_SHARE);

    // Remove macro from scope.
    #undef DEFINE_PATH_ACCESSOR

    // Perform instance-specific setup.
    void SetUp() override;

    // Perform fixture-wide setup.
    static void SetUpTestSuite();

    // Perform instance-specific teardown.
    void TearDown() override;

    // Perform fixture-wide teardown.
    static void TearDownTestSuite();

    // How long should we wait for something to happen?
    static const std::chrono::seconds mDefaultTimeout;

    // Where should we store temporary state?
    static Path mScratchPath;
}; // Test

} // testing
} // fuse
} // mega

