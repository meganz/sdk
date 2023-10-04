#include <iostream>
#include <memory>
#include <cxxopts.hpp>
#include "megaapi.h"
#include "gfxworker/server.h"
#include "gfxworker/comms_server_win32.h"
#include "gfxworker/logger.h"

using mega::gfx::GfxProcessor;
using mega::gfx::RequestProcessor;
using mega::gfx::WinGfxCommunicationsServer;
using mega::LocalPath;
using cxxopts::Options;

int main(int argc, char** argv)
{
    std::string pipename;
    Options options("gfxworker", "GFX processing server");
    options.add_options()
        ("h,help", "Show help")
        ("n,name", "Pipe name", cxxopts::value(pipename)->default_value("MEGA_GFXWORKER"));

    try {
        auto result = options.parse(argc, argv);

        if (result["help"].as<bool>()) {
            std::cout << options.help() << std::endl;
            return 0;
        }

    } catch (cxxopts::exceptions::exception& e) {
        std::cerr << e.what() << std::endl;
        return -1;
    }

    std::unique_ptr<mega::MegaLogger> logInstance(new mega::gfx::Logger());
    mega::MegaApi::addLoggerObject(logInstance.get(), false);
    mega::MegaApi::setLogLevel(mega::MegaApi::LOG_LEVEL_MAX);

    LOG_info << "Gfxworker server starting, pipe name: " << pipename;

    WinGfxCommunicationsServer server(
        ::mega::make_unique<RequestProcessor>(GfxProcessor::create())
    );

    std::thread serverThread(&WinGfxCommunicationsServer::initialize, &server);

    if (serverThread.joinable())
    {
        serverThread.join();
    }

    return 0;
}
