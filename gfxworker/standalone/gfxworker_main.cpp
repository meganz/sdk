#include <iostream>
#include <memory>
#include "megaapi.h"
#include "gfxworker/server.h"
#include "gfxworker/comms_win32.h"
#include "gfxworker/logger.h"

using gfx::server::IGfxProcessorFactory;
using gfx::server::GfxProcessorFactory;
using gfx::server::RequestProcessor;
using gfx::comms::WinGfxCommunicationsServer;
using mega::LocalPath;

int main(int argc, char** argv)
{
    std::string logFileName = "gfxworker.log";

    std::unique_ptr<mega::MegaLogger> logInstance(new gfx::log::Logger());
    mega::MegaApi::addLoggerObject(logInstance.get(), false);
    mega::MegaApi::setLogLevel(mega::MegaApi::LOG_LEVEL_MAX);

    LOG_info << "Gfxworker server starting...";

    WinGfxCommunicationsServer server(
        ::mega::make_unique<RequestProcessor>(GfxProcessorFactory().processor())
    );

    std::thread serverThread(&WinGfxCommunicationsServer::initialize, &server);

    if (serverThread.joinable())
    {
        serverThread.join();
    }

    return 0;
}
