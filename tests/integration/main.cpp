#include "mega.h"
#include "gtest/gtest.h"
#include "test.h"
#include <stdio.h>
#include <fstream>

bool gRunningInCI = false;
bool gTestingInvalidArgs = false;

using namespace mega;
using namespace std;

namespace {

class MegaLogger : public ::mega::Logger
{
public:
    void log(const char* time, int loglevel, const char* source, const char* message)
    {
        std::ostringstream os;

        os << "[";
        if (time)
        {
            os << time;
        }
        else
        {
            auto t = std::time(NULL);
            char ts[50];
            if (!std::strftime(ts, sizeof(ts), "%H:%M:%S", std::gmtime(&t)))
            {
                ts[0] = '\0';
            }
            os << ts;
        }
        os << "] " << SimpleLogger::toStr(static_cast<LogLevel>(loglevel)) << ": " << message;

        if (source)
        {
            os << " (" << source << ")";
        }
        os << std::endl;

        if (loglevel <= SimpleLogger::logCurrentLevel)
        {
            if (gRunningInCI)
            {
                if (!mLogFile.is_open())
                {
                    mLogFile.open("test_integration.log");
                }
                mLogFile << os.str() << std::flush;
            }
            else
            {
#ifndef _WIN32
                std::cout << os.str() << std::flush;
#endif
                if (!gTestingInvalidArgs)
                {
                    ASSERT_NE(loglevel, logError) << os.str();
                }
            }
#ifdef _WIN32
            OutputDebugStringA(os.str().c_str());
#endif

        }
    }

private:
    std::ofstream mLogFile;
};

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
            gRunningInCI = true;
            myargv.erase(it);
            argc -= 1;
            break;
        }
    }

    MegaLogger megaLogger;

    SimpleLogger::setLogLevel(logDebug);
    SimpleLogger::setOutputClass(&megaLogger);

#if defined(_WIN32) && defined(NO_READLINE)
    WinConsole* wc = new CONSOLE_CLASS;
    wc->setShellConsole();
#endif

    ::testing::InitGoogleTest(&argc, myargv.data());
    return RUN_ALL_TESTS();
}
