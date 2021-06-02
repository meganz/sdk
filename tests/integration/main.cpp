#include "mega.h"
#include "gtest/gtest.h"
#include "test.h"
#include <stdio.h>
#include <fstream>

// If running in Jenkins, we use its working folder.  But for local manual testing, use a convenient location
#ifdef WIN32
    #define LOCAL_TEST_FOLDER "c:\\tmp\\synctests"
#else
    #define LOCAL_TEST_FOLDER (string(getenv("HOME"))+"/synctests_mega_auto")
#endif

bool gRunningInCI = false;
bool gResumeSessions = false;
bool gTestingInvalidArgs = false;
bool gOutputToCout = false;
int gFseventsFd = -1;
std::string USER_AGENT = "Integration Tests with GoogleTest framework";

std::ofstream gUnopenedOfstream;

std::string getCurrentTimestamp()
{
    using std::chrono::system_clock;
    auto currentTime = std::chrono::system_clock::now();
    char buffer[80];

    auto transformed = currentTime.time_since_epoch().count() / 1000000;

    auto millis = transformed % 1000;

    std::time_t tt;
    tt = system_clock::to_time_t ( currentTime );
    auto timeinfo = localtime (&tt);
    strftime (buffer,80,"%H:%M:%S",timeinfo);
    sprintf(buffer, "%s:%03d",buffer,(int)millis);

    return std::string(buffer);
}

std::string logTime()
{
    return getCurrentTimestamp();
}


std::ostream& out(bool withTime)
{
    if (withTime && gOutputToCout)
    {
        std::cout << getCurrentTimestamp() << " ";
    }
    if (gOutputToCout) return std::cout;
    else return gUnopenedOfstream;
}

namespace {

class MegaLogger : public mega::Logger
{
public:
    void log(const char* time, int loglevel, const char* source, const char* message
#ifdef ENABLE_LOG_PERFORMANCE
          , const char **directMessages = nullptr, size_t *directMessagesSizes = nullptr, unsigned numberMessages = 0
#endif
    ) override
    {
        std::ostringstream os;

        os << "[";
        os << getCurrentTimestamp();
#ifdef ENABLE_LOG_PERFORMANCE
        os << "] " << mega::SimpleLogger::toStr(static_cast<mega::LogLevel>(loglevel)) << ": ";
        if (message)
        {
            os << message;
        }
        // we can have the message AND the direct messages
        for (unsigned i = 0; i < numberMessages; ++i) os.write(directMessages[i], directMessagesSizes[i]);
#else
        os << "] " << mega::SimpleLogger::toStr(static_cast<mega::LogLevel>(loglevel)) << ": " << message;
#endif
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
                    if (loglevel <= mega::logError)
                    {
                        ASSERT_GT(loglevel, mega::logError) << os.str();
                    }
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

    // delete old test folders, created during previous runs
    TestFS testFS;
    testFS.DeleteTestFolder();
    testFS.DeleteTrashFolder();

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
        else if (std::string(*it) == "--COUT")
        {
            gOutputToCout = true;
            argc -= 1;
        }
        else if (std::string(*it).substr(0, 9) == "--APIURL:")
        {
            mega::MegaClient::APIURL = std::string(*it).substr(9);
            argc -= 1;
        }
        else if (std::string(*it) == "--RESUMESESSIONS")
        {
            gResumeSessions = true;
            argc -= 1;
        }
        else if (std::string(*it).substr(0, 13) == "--FSEVENTSFD:")
        {
            int fseventsFd = std::stoi(std::string(*it).substr(13));
            if (fcntl(fseventsFd, F_GETFD) == -1 || errno == EBADF) {
                std::cout << "Received bad fsevents fd " << fseventsFd << "\n";
                return 1;
            }

            gFseventsFd = fseventsFd;
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




/*
**  TestFS implementation
*/

using namespace std;

fs::path TestFS::GetTestBaseFolder()
{
    const char* jenkins_folder = getenv("WORKSPACE");
    return jenkins_folder ? fs::path(jenkins_folder) : fs::path(LOCAL_TEST_FOLDER);
}

fs::path TestFS::GetTestFolder()
{
#ifdef WIN32
    auto pid = GetCurrentProcessId();
#else
    auto pid = getpid();
#endif

    fs::path testpath = GetTestBaseFolder() / ("pid_" + std::to_string(pid));
    out() << "Local Test folder: " << testpath << endl;
    return testpath;
}


fs::path TestFS::GetTrashFolder()
{
    return GetTestBaseFolder() / "trash";
}


void TestFS::DeleteFolder(fs::path folder)
{
    // rename folder, so that tests can still create one and add to it
    error_code ec;
    fs::path oldpath(folder);
    fs::path newpath(folder);

    for (int i = 10; i--; )
    {
        newpath += "_del"; // this can be improved later if needed
        fs::rename(oldpath, newpath, ec);
        if (!ec) break;
    }

    // if renaming failed, then there's nothing to delete
    if (ec)
    {
        // report failures, other than the case when it didn't exist
        if (ec != errc::no_such_file_or_directory)
        {
            out() << "Renaming " << oldpath << " to " << newpath << " failed." << endl
                 << ec.message() << endl;
        }

        return;
    }

    // delete folder in a separate thread
    m_cleaners.emplace_back(thread([=]() mutable // ...mostly for fun, to avoid declaring another ec
        {
            fs::remove_all(newpath, ec);

            if (ec)
            {
                out() << "Deleting " << folder << " failed." << endl
                     << ec.message() << endl;
            }
        }));
}


TestFS::~TestFS()
{
    for_each(m_cleaners.begin(), m_cleaners.end(), [](thread& t) { t.join(); });
}

void moveToTrash(const fs::path& p)
{
    fs::path trashpath(TestFS::GetTrashFolder());
    fs::create_directory(trashpath);
    fs::path newpath = trashpath / p.filename();
    for (int i = 2; fs::exists(newpath); ++i)
    {
        newpath = trashpath / fs::u8path(p.filename().stem().u8string() + "_" + to_string(i) + p.extension().u8string());
    }
    fs::rename(p, newpath);
}

fs::path makeNewTestRoot()
{
    fs::path p = TestFS::GetTestFolder();

    if (fs::exists(p))
    {
        moveToTrash(p);
    }
    #ifndef NDEBUG
    bool b =
    #endif
    fs::create_directories(p);
    assert(b);
    return p;
}

::mega::FSACCESS_CLASS makeFsAccess()
{
#ifndef __APPLE__
    return ::mega::FSACCESS_CLASS();
#else
    return ::mega::FSACCESS_CLASS(gFseventsFd);
#endif
}
