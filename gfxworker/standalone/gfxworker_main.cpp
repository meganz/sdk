#include <algorithm>
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
    // parse arguments
    std::string pipename;
    std::string logdirectory;
    std::string logfilename;
    unsigned short aliveSeconds = 0;
    size_t threadCount = 0;
    size_t queueSize = 0;
    Options options("gfxworker", "GFX processing server");
    options.add_options()
        ("h,help", "Show help")
        ("l,live", "Keep alive in seconds without receiving any requests, 0 is INFINITE", cxxopts::value(aliveSeconds)->default_value("60"))
        ("t,threads", "Request processing thread pool size, minimum 1", cxxopts::value(threadCount)->default_value("5"))
        ("q,queue", "The size of this queue determines the capacity for pending requests when all threads in the pool are busy. Minimum 1", cxxopts::value(queueSize)->default_value("10"))
        ("n,name", "Pipe name", cxxopts::value(pipename)->default_value("mega_gfxworker"))
        ("d,directory", "Log directory", cxxopts::value(logdirectory)->default_value("."))
        ("f,file", "File name (default mega.gfxworker.<pipename>.log)", cxxopts::value(logfilename));

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

    threadCount = std::max<size_t>(1, threadCount);
    queueSize = std::max<size_t>(1, queueSize);
    if (logdirectory.empty())
    {
        logdirectory = ".";
    }
    if (logfilename.empty())
    {
        logfilename = "mega.gfxworker." + pipename + ".log";
    }

    // init logger
    mega::gfx::MegaFileLogger logger;
    logger.initialize(logdirectory.c_str(), logfilename.c_str(), false);
    LOG_info << "Gfxworker server starting"
             << ", pipe name: " << pipename
             << ", threads: " << threadCount
             << ", queue size: " << queueSize
             << ", live in seconds: " << aliveSeconds;

    // start server
    WinGfxCommunicationsServer server(
        ::mega::make_unique<RequestProcessor>(GfxProcessor::create(), threadCount, queueSize),
        pipename,
        aliveSeconds
    );

    std::thread serverThread(&WinGfxCommunicationsServer::run, &server);

    // run forever until server thread stops
    if (serverThread.joinable())
    {
        serverThread.join();
    }

    return 0;
}
