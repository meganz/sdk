#include "executable_dir.h"
#include "mega/gfx.h"
#include "mega/gfx/worker/client.h"
#include "mega/logging.h"
#include "mega/utils.h"
#include "processor.h"
#include "sdk_test_data_provider.h"
#include "server.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <memory>
#include <system_error>
#include <thread>
#include <vector>

using mega::gfx::Server;
using mega::gfx::RequestProcessor;
using mega::gfx::GfxClient;
using mega::gfx::GfxCommunicationsClient;
using mega::GfxDimension;
using mega::LocalPath;
using mega::getCurrentPid;
using mega_test::ExecutableDir;
using namespace std::chrono_literals;

#if !defined(WIN32) && defined(ENABLE_ISOLATED_GFX)
#include "mega/posix/gfx/worker/socket_utils.h"
#include <filesystem>

using mega::gfx::SocketUtils;
#endif

class ServerClientTest: public SdkTestDataProvider, public testing::Test
{
protected:
    void SetUp() override
    {
        std::ostringstream oss;
        oss << "MEGA_GFXWOKER_UNIT_TEST_" << getCurrentPid();
        mEndpointName = oss.str();
    }

    void TearDown() override
    {
    #if !defined(WIN32) && defined(ENABLE_ISOLATED_GFX)
        // Clean up socket file on UNIX
        if (std::error_code errorCode = SocketUtils::removeSocketFile(mEndpointName))
        {
            LOG_err << "Failed to remove socket path " << mEndpointName << ": " << errorCode.message();
        }
    #endif
    }

    std::string mEndpointName; // Used as pipe name on Windows, domain socket name on Linux
};

TEST_F(ServerClientTest, RunGfxTaskSuccessfully)
{
    Server server(
        std::make_unique<RequestProcessor>(),
        mEndpointName
    );

    std::thread serverThread(std::ref(server));

    auto dimensions = std::vector<GfxDimension> {
        { 200, 0 },     // THUMBNAIL: square thumbnail, cropped from near center
        { 1000, 1000 }  // PREVIEW: scaled version inside 1000x1000 bounding square
    };

    // one png
    std::string testImage{"logo.png"};
    fs::path testImageLocalPath = fs::path{ExecutableDir::get()} / testImage;

    ASSERT_TRUE(getFileFromArtifactory("test-data/" + testImage, testImageLocalPath));
    std::vector<std::string> images;
    EXPECT_TRUE(GfxClient(std::make_unique<GfxCommunicationsClient>(mEndpointName))
                    .runGfxTask(testImageLocalPath.string(), dimensions, images));
    EXPECT_EQ(images.size(), 2);
    EXPECT_GT(images[0].size(), 4500); // Use > as the size is different between MacOS and other platforms
    EXPECT_GT(images[1].size(), 650); // Use > as the above

    // shutdown
    EXPECT_TRUE(
        GfxClient(
            std::make_unique<GfxCommunicationsClient>(mEndpointName)
        ).runShutDown()
    );

    if (serverThread.joinable())
    {
        serverThread.join();
    }
}

TEST_F(ServerClientTest, RunHelloRequestResponseSuccessfully)
{
    Server server(
        std::make_unique<RequestProcessor>(),
        mEndpointName
    );

    std::thread serverThread(std::ref(server));

    // allow server starting up as runHello doesn't have connect retry
    std::this_thread::sleep_for(1000ms);

    EXPECT_TRUE(
        GfxClient(
            std::make_unique<GfxCommunicationsClient>(mEndpointName)
        ).runHello("")
    );

    EXPECT_TRUE(
        GfxClient(
            std::make_unique<GfxCommunicationsClient>(mEndpointName)
        ).runShutDown()
    );

    if (serverThread.joinable())
    {
        serverThread.join();
    }
}

TEST_F(ServerClientTest, RunSupportformatsRequestResponseSuccessfully)
{
    Server server(
        std::make_unique<RequestProcessor>(),
        mEndpointName
    );

    std::thread serverThread(std::ref(server));

    // Get from isolated process
    std::string formats, videoformats;
    EXPECT_TRUE(
        GfxClient(
            std::make_unique<GfxCommunicationsClient>(mEndpointName)
        ).runSupportFormats(formats, videoformats)
    );

    // compare with local
    auto provider = mega::IGfxProvider::createInternalGfxProvider();
    auto internalFormats = provider->supportedformats();
    if (internalFormats)
    {
        EXPECT_EQ(formats.find(internalFormats), 0); // the formats starts with internalFormats, extra part are not checked here for simplicity
    }
    else
    {
        EXPECT_EQ(formats, std::string());
    }
    EXPECT_EQ(videoformats, provider->supportedvideoformats() ? std::string(provider->supportedvideoformats()) : std::string());

    EXPECT_TRUE(
        GfxClient(
            std::make_unique<GfxCommunicationsClient>(mEndpointName)
        ).runShutDown()
    );

    if (serverThread.joinable())
    {
        serverThread.join();
    }
}

TEST_F(ServerClientTest, RunCommandsReturnFalseWhileServerIsNotRunning)
{
    EXPECT_FALSE(
        GfxClient(
            std::make_unique<GfxCommunicationsClient>(mEndpointName)
        ).runShutDown()
    );

    // could be any dimensions
    std::vector<GfxDimension> dimensions;
    std::vector<std::string> images;

    EXPECT_FALSE(
        GfxClient(
            std::make_unique<GfxCommunicationsClient>(mEndpointName)
        ).runGfxTask("anyimagename.jpg", dimensions, images)
    );
}
