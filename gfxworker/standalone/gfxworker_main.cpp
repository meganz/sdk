#include <iostream>
#include <memory>
#include <cxxopts.hpp>
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
    unsigned short aliveSeconds = 0;
    Options options("gfxworker", "GFX processing server");
    options.add_options()
        ("h,help", "Show help")
        ("l,live", "Keep alive in seconds without receiving any requests, 0 is INFINITE", cxxopts::value(aliveSeconds)->default_value("60"))
        ("n,name", "Pipe name", cxxopts::value(pipename)->default_value("mega_gfxworker"));

    try {
        auto result = options.parse(argc, argv);

        if (result["help"].as<bool>()) {
            std::cout << options.help() << std::endl;
            return 0;
        }

    } catch (cxxopts::OptionException& e) {
        std::cerr << e.what() << std::endl;
        return -1;
    }

    std::string logfilename = "mega.gfxworker." + pipename + ".log";
    mega::gfx::MegaFileLogger logger;
    logger.initialize(".", logfilename.c_str(), false);

    LOG_info << "Gfxworker server starting"
             << ", pipe name: " << pipename
             << ", live in seconds: " << aliveSeconds;

    WinGfxCommunicationsServer server(
        ::mega::make_unique<RequestProcessor>(GfxProcessor::create()),
        pipename,
        aliveSeconds
    );

    std::thread serverThread(&WinGfxCommunicationsServer::initialize, &server);

    if (serverThread.joinable())
    {
        serverThread.join();
    }

    return 0;
}
