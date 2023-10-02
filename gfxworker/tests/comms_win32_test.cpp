#include "gtest/gtest.h"
#include "mega/win32/gfx/worker/comms_client.h"
#include "gfxworker/comms_server_win32.h"
#include "gfxworker/logger.h"
#include "gfxworker/server.h"
#include "gfxworker/client.h"
#include <memory>
#include <thread>
#include <chrono>


using mega::gfx::WinGfxCommunicationsServer;
using mega::gfx::WinGfxCommunicationsClient;
using mega::gfx::RequestProcessor;
using mega::gfx::GfxProcessor;
using mega::gfx::GfxClient;
using mega::gfx::IEndpoint;

//TEST(CommsWin32, DISABLED_init)
TEST(CommsWin32, init)
{
    std::unique_ptr<mega::MegaLogger> logInstance(new mega::gfx::Logger());
    mega::MegaApi::addLoggerObject(logInstance.get(), false);
    mega::MegaApi::setLogLevel(mega::MegaApi::LOG_LEVEL_MAX);

    WinGfxCommunicationsServer server(
        ::mega::make_unique<RequestProcessor>(GfxProcessor::create())
    );

    std::thread serverThread(&WinGfxCommunicationsServer::initialize, &server);

    using namespace std::chrono_literals;
    std::this_thread::sleep_for(100ms);

    EXPECT_TRUE(
        GfxClient(
            mega::make_unique<WinGfxCommunicationsClient>([](std::unique_ptr<IEndpoint> _) {})
        ).runGfxTask("C:\\Users\\mega-cjr\\Pictures\\Screenshot.jpg")
    );

    EXPECT_TRUE(
        GfxClient(
            mega::make_unique<WinGfxCommunicationsClient>([](std::unique_ptr<IEndpoint> _) {})
        ).runGfxTask("C:\\Users\\mega-cjr\\Pictures\\Screenshot1.png")
    );

    EXPECT_TRUE(
        GfxClient(
            mega::make_unique<WinGfxCommunicationsClient>([](std::unique_ptr<IEndpoint> _) {})
        ).runShutDown()
    );

    if (serverThread.joinable())
    {
        serverThread.join();
    }
}
