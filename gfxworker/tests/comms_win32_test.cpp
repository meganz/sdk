#include "gtest/gtest.h"
#include "gfxworker/comms_win32.h"
#include "gfxworker/tasks.h"
#include "gfxworker/megaproxytasks.h"
#include "gfxworker/logger.h"
#include "gfxworker/command_serializer.h"
#include "gfxworker/commands.h"
#include "gfxworker/server.h"
#include "gfxworker/client.h"
#include <memory>
#include <thread>
#include <chrono>


using gfx::comms::WinGfxCommunicationsServer;
using gfx::comms::WinGfxCommunicationsClient;
using gfx::server::RequestProcessor;
using gfx::server::GfxProcessorFactory;
using gfx::client::GfxClient;
using gfx::comms::IEndpoint;

//TEST(CommsWin32, DISABLED_init)
TEST(CommsWin32, init)
{
    std::unique_ptr<mega::MegaLogger> logInstance(new gfx::log::Logger());
    mega::MegaApi::addLoggerObject(logInstance.get(), false);
    mega::MegaApi::setLogLevel(mega::MegaApi::LOG_LEVEL_MAX);

    WinGfxCommunicationsServer server(
        ::mega::make_unique<RequestProcessor>(GfxProcessorFactory().processor())
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
