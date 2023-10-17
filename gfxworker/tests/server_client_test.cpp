#include "gtest/gtest.h"
#include "mega.h"
#include "mega/gfx.h"
#include "mega/gfx/worker/tasks.h"
#include "mega/utils.h"
#include "mega/win32/gfx/worker/comms_client.h"
#include "mega/gfx/worker/client.h"
#include "gfxworker/comms_server_win32.h"
#include "gfxworker/server.h"
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
using mega::gfx::GfxSize;
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

//TEST(ServerClientTest, DISABLED_init)
TEST_F(ServerClientTest, gfxTask)
{
    WinGfxCommunicationsServer server(
        ::mega::make_unique<RequestProcessor>(GfxProcessor::create()),
        mPipename
    );

    std::thread serverThread(&WinGfxCommunicationsServer::initialize, &server);

    std::this_thread::sleep_for(100ms);

    auto sizes = std::vector<GfxSize> {
        { 200, 0 },     // THUMBNAIL: square thumbnail, cropped from near center
        { 1000, 1000 }  // PREVIEW: scaled version inside 1000x1000 bounding square
    };
    std::vector<std::string> images;
    LocalPath jpgImage = mDataFolder;
    jpgImage.appendWithSeparator(LocalPath::fromRelativePath("Screenshot.jpg"), false);

    EXPECT_TRUE(
        GfxClient(
            mega::make_unique<WinGfxCommunicationsClient>(mPipename, [](std::unique_ptr<IEndpoint> _) {})
        ).runGfxTask(jpgImage.toPath(false), sizes, images)
    );
    EXPECT_EQ(images.size(), 2);
    EXPECT_EQ(images[0].size(), 8146);
    EXPECT_EQ(images[1].size(), 63012);

    LocalPath pngImage = mDataFolder;
    pngImage.appendWithSeparator(LocalPath::fromRelativePath("Screenshot.png"), false);
    EXPECT_TRUE(
        GfxClient(
            mega::make_unique<WinGfxCommunicationsClient>(mPipename, [](std::unique_ptr<IEndpoint> _) {})
        ).runGfxTask(jpgImage.toPath(false), sizes, images)
    );

    EXPECT_TRUE(
        GfxClient(
            mega::make_unique<WinGfxCommunicationsClient>(mPipename, [](std::unique_ptr<IEndpoint> _) {})
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

    std::thread serverThread(&WinGfxCommunicationsServer::initialize, &server);

    std::this_thread::sleep_for(100ms);


    EXPECT_TRUE(
        GfxClient(
            mega::make_unique<WinGfxCommunicationsClient>(mPipename, [](std::unique_ptr<IEndpoint> _) {})
        ).runHello("")
    );

    EXPECT_TRUE(
        GfxClient(
            mega::make_unique<WinGfxCommunicationsClient>(mPipename, [](std::unique_ptr<IEndpoint> _) {})
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

    std::thread serverThread(&WinGfxCommunicationsServer::initialize, &server);

    std::this_thread::sleep_for(100ms);

    std::string formats, videoformats;
    EXPECT_TRUE(
        GfxClient(
            mega::make_unique<WinGfxCommunicationsClient>(mPipename, [](std::unique_ptr<IEndpoint> _) {})
        ).runSupportFormats(formats, videoformats)
    );

    auto provider = mega::IGfxProvider::createInternalGfxProvider();
    EXPECT_EQ(formats, provider->supportedformats() ? std::string(provider->supportedformats()) : std::string());
    EXPECT_EQ(videoformats, provider->supportedvideoformats() ? std::string(provider->supportedvideoformats()) : std::string());

    EXPECT_TRUE(
        GfxClient(
            mega::make_unique<WinGfxCommunicationsClient>(mPipename, [](std::unique_ptr<IEndpoint> _) {})
        ).runShutDown()
    );

    if (serverThread.joinable())
    {
        serverThread.join();
    }
}

TEST_F(ServerClientTest, ServerIsNotRunning)
{
    auto sizes = std::vector<GfxSize> {
        { 200, 0 },     // THUMBNAIL: square thumbnail, cropped from near center
        { 1000, 1000 }  // PREVIEW: scaled version inside 1000x1000 bounding square
    };
    std::vector<std::string> images;

    EXPECT_FALSE(
        GfxClient(
            mega::make_unique<WinGfxCommunicationsClient>(mPipename, [](std::unique_ptr<IEndpoint> _) {})
        ).runGfxTask("anyimagename.jpg", sizes, images)
    );
}

TEST_F(ServerClientTest, KeepLaunch)
{
    std::vector<std::string> argv {
        "C:\\Users\\mega-cjr\\dev\\gitlab\\sdk\\build-x64-windows-mega\\Debug\\gfxworker.exe",
        "-l10",             // keep alive for 10 seconds
        "-n" + mPipename,   // pipe name
    };

    mega::AutoStartLauncher runner(
        std::move(argv),
        [this](){
            ::mega::gfx::GfxClient::create(this->mPipename).runShutDown();
        }
    );

    mega::GfxWorkerHelloBeater beater(std::chrono::seconds(5), mPipename);

    //std::this_thread::sleep_for(std::chrono::seconds(100000));
    runner.shutDownOnce();
}
