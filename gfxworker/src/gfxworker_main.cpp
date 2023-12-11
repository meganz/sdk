#include "server.h"
#include "comms_server_win32.h"
#include "logger.h"

#include "mega/arguments.h"

#include <algorithm>
#include <iostream>
#include <memory>
#include <unordered_map>
#include <utility>

using mega::gfx::GfxProcessor;
using mega::gfx::RequestProcessor;
using mega::gfx::WinGfxCommunicationsServer;
using mega::gfx::MegaFileLogger;
using mega::ArgumentsParser;
using mega::Arguments;
using mega::LocalPath;

namespace
{

std::string USAGE = R"--(
GFX processing server
Usage:
  gfxworker [OPTION...]

  -h                   Show help
  -l=arg               Keep alive in seconds without receiving any
                       requests, 0 is INFINITE (default: 60)
  -t=arg               Request processing thread pool size, minimum 1
                       (default: 5)
  -q=arg               The size of this queue determines the capacity for
                       pending requests when all threads in the pool are
                       busy. Minimum 1 (default: 10)
  -n=arg               Pipe name (default: mega_gfxworker)
  -d=arg               Log directory (default: .)
  -f=arg               File name (default mega.gfxworker.<pipename>.log)
)--";

struct Config
{
    unsigned short aliveSeconds;

    size_t threadCount;

    size_t queueSize;

    std::string pipename;

    std::string logdirectory;

    std::string logfilename;

    static Config fromArguments(const Arguments& arguments);
};

Config Config::fromArguments(const Arguments& arguments)
{
    Config config;
    // live
    config.aliveSeconds = static_cast<unsigned short>(std::stoi(arguments.getValue("-l", "60")));

    // thread count, minimum 1
    config.threadCount = static_cast<size_t>(std::stoi(arguments.getValue("-t", "5")));
    config.threadCount = std::max<size_t>(1, config.threadCount);

    // queue size, minimum 1
    config.queueSize = static_cast<size_t>(std::stoi(arguments.getValue("-q", "10")));
    config.queueSize = std::max<size_t>(1, config.queueSize);

    // pipe name
    config.pipename  = arguments.getValue("-n", "mega_gfxworker");

    // log directory
    config.logdirectory = arguments.getValue("-d", ".");

    // log file name
    config.logfilename = arguments.getValue("-f", "mega.gfxworker." + config.pipename + ".log");

    return config;
}

//
// We'll use a debug version and test gfx prcessing crashing in jenkins
// this prevents from presenting dialog with "Debug Error! abort()..." on
// windows
//
void set_abort_behaviour()
{
#if defined(WIN32) && defined(DEBUG)
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOALIGNMENTFAULTEXCEPT |
                 SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);

    _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_WARN, _CRTDBG_FILE_STDERR);

    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);

    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);

    _set_abort_behavior(0, _WRITE_ABORT_MSG);
    _set_error_mode(_OUT_TO_STDERR);
#endif
}

}

int main(int argc, char** argv)
{
    set_abort_behaviour();

    auto arguments = ArgumentsParser::parse(argc, argv);

    // help
    if (arguments.contains("-h"))
    {
        std::cout << USAGE << std::endl;
        return 0;
    }

    // config from arguments
    Config config;
    try
    {
        config = Config::fromArguments(arguments);
    }
    catch(...)
    {
        std::cout << USAGE << std::endl;
        return 0;
    }

    // init logger
    MegaFileLogger::get().initialize(config.logdirectory.c_str(),
                                     config.logfilename.c_str(),
                                     false);

    LOG_info << "Gfxworker server starting"
             << ", pipe name: " << config.pipename
             << ", threads: " << config.threadCount
             << ", queue size: " << config.queueSize
             << ", live in seconds: " << config.aliveSeconds;

    // start server
    WinGfxCommunicationsServer server(
        ::mega::make_unique<RequestProcessor>(GfxProcessor::create(), config.threadCount, config.queueSize),
        config.pipename,
        config.aliveSeconds
    );

    std::thread serverThread(std::ref(server));

    // run forever until server thread stops
    if (serverThread.joinable())
    {
        serverThread.join();
    }

    return 0;
}
