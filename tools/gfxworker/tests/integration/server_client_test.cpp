#include "executable_dir.h"

#include "server.h"
#include "processor.h"

#include "mega/gfx.h"

#if defined(WIN32)
#include "mega/win32/gfx/worker/comms_client.h"
#else
#include "mega/posix/gfx/worker/comms_client.h"
#endif

#include "mega/gfx/worker/client.h"
#include "mega/utils.h"

#include <gtest/gtest.h>

#include <memory>
#include <thread>
#include <chrono>
#include <vector>
#include <cstdlib>

#if defined(WIN32)
using GfxCommunicationsClient = mega::gfx::WinGfxCommunicationsClient;
#else
using GfxCommunicationsClient = mega::gfx::PosixGfxCommunicationsClient;
#endif

using mega::gfx::Server;
using mega::gfx::RequestProcessor;
using mega::gfx::GfxProcessor;
using mega::gfx::GfxClient;
using mega::gfx::IEndpoint;
using mega::GfxDimension;
using mega::LocalPath;
using mega::getCurrentPid;
using mega_test::ExecutableDir;
using namespace std::chrono_literals;


class ServerClientTest : public testing::Test
{
protected:
    void SetUp() override
    {
        std::ostringstream oss;
        oss << "MEGA_GFXWOKER_UNIT_TEST_" << getCurrentPid();
        mName = oss.str();
    }

    std::string mName; // Used as pipe name on Windows, domain socket name on Linux
};

#if defined(WIN32)

TEST_F(ServerClientTest, RunGfxTaskSuccessfully)
{
    Server server(
        ::mega::make_unique<RequestProcessor>(),
        mName
    );

    std::thread serverThread(std::ref(server));

    auto dimensions = std::vector<GfxDimension> {
        { 200, 0 },     // THUMBNAIL: square thumbnail, cropped from near center
        { 1000, 1000 }  // PREVIEW: scaled version inside 1000x1000 bounding square
    };

    // one png
    std::vector<std::string> images;
    LocalPath jpgImage = LocalPath::fromAbsolutePath(ExecutableDir::get());
    jpgImage.appendWithSeparator(LocalPath::fromRelativePath("logo.png"), false);

    EXPECT_TRUE(
        GfxClient(
            mega::make_unique<GfxCommunicationsClient>(mName)
        ).runGfxTask(jpgImage.toPath(false), dimensions, images)
    );
    EXPECT_EQ(images.size(), 2);
    EXPECT_EQ(images[0].size(), 4539);
    EXPECT_EQ(images[1].size(), 826);

    // shutdown
    EXPECT_TRUE(
        GfxClient(
            mega::make_unique<GfxCommunicationsClient>(mName)
        ).runShutDown()
    );

    if (serverThread.joinable())
    {
        serverThread.join();
    }
}

#endif

TEST_F(ServerClientTest, RunHelloRequestResponseSuccessfully)
{
    Server server(
        ::mega::make_unique<RequestProcessor>(),
        mName
    );

    std::thread serverThread(std::ref(server));

    // allow server starting up as runHello doesn't have connect retry
    std::this_thread::sleep_for(1000ms);

    EXPECT_TRUE(
        GfxClient(
            mega::make_unique<GfxCommunicationsClient>(mName)
        ).runHello("")
    );

    EXPECT_TRUE(
        GfxClient(
            mega::make_unique<GfxCommunicationsClient>(mName)
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
        ::mega::make_unique<RequestProcessor>(),
        mName
    );

    std::thread serverThread(std::ref(server));

    // Get from isolated process
    std::string formats, videoformats;
    EXPECT_TRUE(
        GfxClient(
            mega::make_unique<GfxCommunicationsClient>(mName)
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
            mega::make_unique<GfxCommunicationsClient>(mName)
        ).runShutDown()
    );

    if (serverThread.joinable())
    {
        serverThread.join();
    }
}

#if defined(WIN32)


TEST_F(ServerClientTest, RunCommandsReturnFalseWhileServerIsNotRunning)
{
    EXPECT_FALSE(
        GfxClient(
            mega::make_unique<GfxCommunicationsClient>(mName)
        ).runShutDown()
    );

    // could be any dimensions
    std::vector<GfxDimension> dimensions;
    std::vector<std::string> images;

    EXPECT_FALSE(
        GfxClient(
            mega::make_unique<GfxCommunicationsClient>(mName)
        ).runGfxTask("anyimagename.jpg", dimensions, images)
    );
}

#endif