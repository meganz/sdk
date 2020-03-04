#include "mega.h"
#include "gtest/gtest.h"
#include "test.h"
#include <stdio.h>
#include <fstream>

bool gRunningInCI = false;
bool gTestingInvalidArgs = false;
std::string USER_AGENT = "Integration Tests with GoogleTest framework";

namespace {

class MegaLogger : public mega::Logger
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
        os << "] " << mega::SimpleLogger::toStr(static_cast<mega::LogLevel>(loglevel)) << ": " << message;

        if (source)
        {
            os << " (" << source << ")";
        }
        os << std::endl;

        if (loglevel <= mega::SimpleLogger::logCurrentLevel)
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
                    ASSERT_NE(loglevel, mega::logError) << os.str();
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
        std::cout << "please set username and password env variables for test" << std::endl;
        return 1;
    }

    std::vector<char*> myargv1(argv, argv + argc);
    std::vector<char*> myargv2;

    for (auto it = myargv1.begin(); it != myargv1.end(); ++it)
    {
        if (std::string(*it) == "--CI")
        {
            gRunningInCI = true;
            argc -= 1;
        }
        else if (std::string(*it).substr(0, 12) == "--USERAGENT:")
        {
            USER_AGENT = std::string(*it).substr(12);
            argc -= 1;
        }
        else if (std::string(*it).substr(0, 9) == "--APIURL:")
        {
            mega::MegaClient::APIURL = std::string(*it).substr(9);
            argc -= 1;
        }
        else
        {
            myargv2.push_back(*it);
        }
    }

    MegaLogger megaLogger;

    mega::SimpleLogger::setLogLevel(mega::logMax);
    mega::SimpleLogger::setOutputClass(&megaLogger);

#if defined(_WIN32) && defined(NO_READLINE)
    using namespace mega;
    WinConsole* wc = new CONSOLE_CLASS;
    wc->setShellConsole();
#endif

    ::testing::InitGoogleTest(&argc, myargv2.data());
    return RUN_ALL_TESTS();
}
