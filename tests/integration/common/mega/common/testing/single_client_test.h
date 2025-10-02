#pragma once

#include <gtest/gtest.h>
#include <mega/common/testing/test.h>

namespace mega
{
namespace common
{
namespace testing
{

template<typename Traits>
class SingleClientTest: public Test<Traits>
{
public:
    using Client = typename Test<Traits>::Client;
    using ClientPtr = typename Test<Traits>::ClientPtr;

    // Perform fixture-wide setup.
    static void SetUpTestSuite()
    {
        Test<Traits>::SetUpTestSuite();

        // Create our test client.
        mClient = SingleClientTest::CreateClient("read-write");
        ASSERT_TRUE(mClient);

        // Log in the client.
        ASSERT_EQ(mClient->login(0), API_OK);
    }

    // Perform instance-specific setup.
    void SetUp() override
    {
        // Make sure our client is sane.
        ASSERT_TRUE(mClient);
    }

    // Perform fixture-wide teardown.
    static void TearDownTestSuite()
    {
        // Clean up our client.
        mClient.reset();
    }

    // The client we're using to interact with the cloud.
    static ClientPtr mClient;
}; // SingleClientTest<Traits>

template<typename Traits>
typename SingleClientTest<Traits>::ClientPtr SingleClientTest<Traits>::mClient;

} // testing
} // common
} // mega
