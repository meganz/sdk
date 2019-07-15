#include "mega.h"
#include "gtest/gtest.h"
#include "test.h"
#include <stdio.h>

bool g_runningInCI = false;

using namespace mega;
using namespace std;

namespace {

class MegaLogger : public ::mega::Logger {
public:
    virtual void log(const char *time, int loglevel, const char *source, const char *message)
    {
#ifdef _WIN32
        OutputDebugStringA(message);
        OutputDebugStringA("\r\n");
#else
        if (loglevel >= SimpleLogger::logCurrentLevel)
        {
            std::cout << "[" << time << "] " << SimpleLogger::toStr(static_cast<LogLevel>(loglevel)) << ": " << message << " (" << source << ")" << std::endl;
        }
#endif
    }
};

MegaLogger gMegaLogger;

} // anonymous

int main (int argc, char *argv[])
{
    if (!getenv("MEGA_EMAIL") || !getenv("MEGA_PWD") || !getenv("MEGA_EMAIL_AUX") || !getenv("MEGA_PWD_AUX"))
    {
        cout << "please set username and password env variables for test" << endl;
        return 1;
    }

    vector<char*> myargv(argv, argv + argc);

    for (auto it = myargv.begin(); it != myargv.end(); ++it)
    {
        if (string(*it) == "--CI")
        {
            g_runningInCI = true;
            myargv.erase(it);
            argc -= 1;
            break;
        }
    }

    remove("SDK.log");
    remove("synctests.log");

#ifdef _WIN32
    SimpleLogger::setLogLevel(logDebug);  // warning and stronger to console; info and weaker to VS output window
#endif
    SimpleLogger::setOutputClass(&gMegaLogger);

#if defined(WIN32) && defined(NO_READLINE)
    WinConsole* wc = new CONSOLE_CLASS;
    wc->setShellConsole();
#endif

    ::testing::InitGoogleTest(&argc, myargv.data());
    return RUN_ALL_TESTS();
}
