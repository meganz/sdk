#include "gtest/gtest.h"
#include "mega.h"
#include "mega/gfx.h"
#include "mega/gfx/worker/tasks.h"
#include "mega/utils.h"
#include "mega/win32/gfx/worker/comms_client.h"
#include "mega/gfx/worker/client.h"
#include "comms_server_win32.h"
#include "server.h"
#include "mega/gfx/isolatedprocess.h"
#include <memory>
#include <thread>
#include <chrono>
#include <vector>
#include <cstdlib>


using mega::gfx::WinGfxCommunicationsServer;
using mega::gfx::WinGfxCommunicationsClient;
using mega::gfx::RequestProcessor;
using mega::gfx::GfxProcessor;
using mega::gfx::GfxClient;
using mega::gfx::IEndpoint;
using mega::GfxDimension;
using mega::LocalPath;
using namespace std::chrono_literals;


class ServerClientTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        auto s = std::getenv("MEGA_TESTDATA_FOLDER");
        mDataFolder = s ? LocalPath::fromAbsolutePath(s) : LocalPath::fromAbsolutePath(".");
        mPipename = "MEGA_GFXWOKER_UNIT_TEST";
    }

    LocalPath mDataFolder;
    std::string mPipename;
};

TEST_F(ServerClientTest, gfxTask)
{
    WinGfxCommunicationsServer server(
        ::mega::make_unique<RequestProcessor>(GfxProcessor::create()),
        mPipename
    );

    std::thread serverThread(std::ref(server));

    std::this_thread::sleep_for(100ms);

    auto dimensions = std::vector<GfxDimension> {
        { 200, 0 },     // THUMBNAIL: square thumbnail, cropped from near center
        { 1000, 1000 }  // PREVIEW: scaled version inside 1000x1000 bounding square
    };

    // JPG
    std::vector<std::string> images;
    LocalPath jpgImage = mDataFolder;
    jpgImage.appendWithSeparator(LocalPath::fromRelativePath("Screenshot.jpg"), false);

    EXPECT_TRUE(
        GfxClient(
            mega::make_unique<WinGfxCommunicationsClient>(mPipename)
        ).runGfxTask(jpgImage.toPath(false), dimensions, images)
    );
    EXPECT_EQ(images.size(), 2);
    EXPECT_EQ(images[0].size(), 8146);
    EXPECT_EQ(images[1].size(), 63012);

    // PNG
    LocalPath pngImage = mDataFolder;
    pngImage.appendWithSeparator(LocalPath::fromRelativePath("Screenshot.png"), false);
    EXPECT_TRUE(
        GfxClient(
            mega::make_unique<WinGfxCommunicationsClient>(mPipename)
        ).runGfxTask(jpgImage.toPath(false), dimensions, images)
    );
    EXPECT_EQ(images.size(), 2);

    EXPECT_TRUE(
        GfxClient(
            mega::make_unique<WinGfxCommunicationsClient>(mPipename)
        ).runShutDown()
    );

    if (serverThread.joinable())
    {
        serverThread.join();
    }
}

TEST_F(ServerClientTest, hello)
{
    WinGfxCommunicationsServer server(
        ::mega::make_unique<RequestProcessor>(GfxProcessor::create()),
        mPipename
    );

    std::thread serverThread(std::ref(server));

    std::this_thread::sleep_for(100ms);


    EXPECT_TRUE(
        GfxClient(
            mega::make_unique<WinGfxCommunicationsClient>(mPipename)
        ).runHello("")
    );

    EXPECT_TRUE(
        GfxClient(
            mega::make_unique<WinGfxCommunicationsClient>(mPipename)
        ).runShutDown()
    );

    if (serverThread.joinable())
    {
        serverThread.join();
    }
}

TEST_F(ServerClientTest, supportformats)
{
    WinGfxCommunicationsServer server(
        ::mega::make_unique<RequestProcessor>(GfxProcessor::create()),
        mPipename
    );

    std::thread serverThread(std::ref(server));

    std::this_thread::sleep_for(100ms);

    // Get from isolated process
    std::string formats, videoformats;
    EXPECT_TRUE(
        GfxClient(
            mega::make_unique<WinGfxCommunicationsClient>(mPipename)
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
            mega::make_unique<WinGfxCommunicationsClient>(mPipename)
        ).runShutDown()
    );

    if (serverThread.joinable())
    {
        serverThread.join();
    }
}

TEST_F(ServerClientTest, ServerIsNotRunning)
{
    auto dimensions = std::vector<GfxDimension> {
        { 200, 0 },     // THUMBNAIL: square thumbnail, cropped from near center
        { 1000, 1000 }  // PREVIEW: scaled version inside 1000x1000 bounding square
    };
    std::vector<std::string> images;

    EXPECT_FALSE(
        GfxClient(
            mega::make_unique<WinGfxCommunicationsClient>(mPipename)
        ).runShutDown()
    );

    EXPECT_FALSE(
        GfxClient(
            mega::make_unique<WinGfxCommunicationsClient>(mPipename)
        ).runGfxTask("anyimagename.jpg", dimensions, images)
    );
}
