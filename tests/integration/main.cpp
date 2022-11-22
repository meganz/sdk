#include "mega.h"
#include "gtest/gtest.h"
#include "test.h"
#include <stdio.h>
#include <fstream>

// If running in Jenkins, we use its working folder.  But for local manual testing, use a convenient location
#ifdef WIN32
    #include <winhttp.h>
    #define LOCAL_TEST_FOLDER "c:\\tmp\\synctests"
#else
    #define LOCAL_TEST_FOLDER (string(getenv("HOME"))+"/synctests_mega_auto")
#endif

using namespace ::mega;

bool gRunningInCI = false;
bool gResumeSessions = false;
bool gTestingInvalidArgs = false;
bool gOutputToCout = false;

std::string USER_AGENT = "Integration Tests with GoogleTest framework";

string_vector envVarAccount = {"MEGA_EMAIL", "MEGA_EMAIL_AUX", "MEGA_EMAIL_AUX2"};
string_vector envVarPass    = {"MEGA_PWD",   "MEGA_PWD_AUX",   "MEGA_PWD_AUX2"};

void WaitMillisec(unsigned n)
{
#ifdef _WIN32
    if (n > 1000)
    {
        for (int i = 0; i < 10; ++i)
        {
            // better for debugging, with breakpoints, pauses, etc
            Sleep(n/10);
        }
    }
    else
    {
        Sleep(n);
    }
#else
    usleep(n * 1000);
#endif
}

string runProgram(const string& command, PROG_OUTPUT_TYPE ot)
{
    FILE* pPipe =
#ifdef _WIN32
        _popen(command.c_str(), "rt");
#else
        popen(command.c_str(), "r");
#endif

    if (!pPipe)
    {
        LOG_err << "Failed to run command\n" << command;
        return string();
    }

    // Read pipe until file ends or error occurs.
    string output;
    char   psBuffer[128];

    while (!feof(pPipe) && !ferror(pPipe))
    {
        switch (ot)
        {
        case PROG_OUTPUT_TYPE::TEXT:
        {
            if (fgets(psBuffer, 128, pPipe))
            {
                output += psBuffer;
            }
            break;
        }

        case PROG_OUTPUT_TYPE::BINARY:
        {
            size_t lastRead = fread(psBuffer, 1, sizeof(psBuffer), pPipe);
            if (lastRead)
            {
                output.append(psBuffer, lastRead);
            }
        }
        } // end switch()
    }

    if (ferror(pPipe))
    {
        LOG_err << "Failed to read full command output.";
    }

#ifdef _WIN32
    _pclose(pPipe);
#else
    pclose(pPipe); // docs don't _guarantee_ handling null stream
#endif

    return output;
}

string loadfile(const string& filename)
{
    string filedata;
    ifstream f(filename, ios::binary);
    f.seekg(0, std::ios::end);
    filedata.resize(unsigned(f.tellg()));
    f.seekg(0, std::ios::beg);
    f.read(const_cast<char*>(filedata.data()), static_cast<std::streamsize>(filedata.size()));
    return filedata;
}

#ifdef WIN32
void synchronousHttpPOSTData(const string& url, const string& senddata, string& responsedata)
{
    LOG_info << "Sending file to " << url << ", size: " << senddata.size();

    BOOL  bResults = TRUE;
    HINTERNET hSession = NULL, hConnect = NULL, hRequest = NULL;

    // Use WinHttpOpen to obtain a session handle.
    hSession = WinHttpOpen(L"testmega/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS, 0);

    WCHAR szURL[8192];
    WCHAR szHost[256];
    URL_COMPONENTS urlComp = { sizeof urlComp };

    urlComp.lpszHostName = szHost;
    urlComp.dwHostNameLength = sizeof szHost / sizeof *szHost;
    urlComp.dwUrlPathLength = (DWORD)-1;
    urlComp.dwSchemeLength = (DWORD)-1;

    if (MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, szURL,
        sizeof szURL / sizeof *szURL)
        && WinHttpCrackUrl(szURL, 0, 0, &urlComp))
    {
        if ((hConnect = WinHttpConnect(hSession, szHost, urlComp.nPort, 0)))
        {
            hRequest = WinHttpOpenRequest(hConnect, L"POST",
                urlComp.lpszUrlPath, NULL,
                WINHTTP_NO_REFERER,
                WINHTTP_DEFAULT_ACCEPT_TYPES,
                (urlComp.nScheme == INTERNET_SCHEME_HTTPS)
                ? WINHTTP_FLAG_SECURE
                : 0);
        }
    }

    // Send a Request.
    if (hRequest)
    {
        WinHttpSetTimeouts(hRequest, 58000, 58000, 0, 0);

        LPCWSTR pwszHeaders = L"Content-Type: application/octet-stream";

        // HTTPS connection: ignore certificate errors, send no data yet
        DWORD flags = SECURITY_FLAG_IGNORE_CERT_CN_INVALID
            | SECURITY_FLAG_IGNORE_CERT_DATE_INVALID
            | SECURITY_FLAG_IGNORE_UNKNOWN_CA;

        WinHttpSetOption(hRequest, WINHTTP_OPTION_SECURITY_FLAGS, &flags, sizeof flags);

        if (WinHttpSendRequest(hRequest, pwszHeaders,
            DWORD(wcslen(pwszHeaders)),
            (LPVOID)senddata.data(),
            (DWORD)senddata.size(),
            (DWORD)senddata.size(),
            NULL))
        {
        }
    }

    DWORD dwSize = 0;

    // End the request.
    if (bResults)
        bResults = WinHttpReceiveResponse(hRequest, NULL);

    // Continue to verify data until there is nothing left.
    if (bResults)
        do
        {
            // Verify available data.
            dwSize = 0;
            if (!WinHttpQueryDataAvailable(hRequest, &dwSize))
                printf("Error %u in WinHttpQueryDataAvailable.\n",
                    GetLastError());

            size_t offset = responsedata.size();
            responsedata.resize(offset + dwSize);

            ZeroMemory(responsedata.data() + offset, dwSize);

            DWORD dwDownloaded = 0;
            if (!WinHttpReadData(hRequest, responsedata.data() + offset, dwSize, &dwDownloaded))
                printf("Error %u in WinHttpReadData.\n", GetLastError());

        } while (dwSize > 0);

    // Report errors.
    if (!bResults)
        printf("Error %d has occurred.\n", GetLastError());

    // Close open handles.
    if (hRequest) WinHttpCloseHandle(hRequest);
    if (hConnect) WinHttpCloseHandle(hConnect);
    if (hSession) WinHttpCloseHandle(hSession);
}
#endif

void synchronousHttpPOSTFile(const string& url, const string& filepath, string& responsedata)
{
#ifdef WIN32
    synchronousHttpPOSTData(url, loadfile(filepath), responsedata);
#else
#ifdef __APPLE__
    // tbd
#else
    string command = "curl -s --data-binary @";
    command.append(filepath).append(" ").append(url.c_str());
    responsedata = runProgram(command, PROG_OUTPUT_TYPE::BINARY);
#endif
#endif
}

LogStream::~LogStream()
{
    auto data = mBuffer.str();

    // Always write messages via standard logger.
    LOG_debug << data;

    if (gOutputToCout)
    {
        std::cout << logTime() << " " << data << std::endl;
    }
}

std::string getCurrentTimestamp(bool includeDate = false)
{
    using std::chrono::system_clock;
    auto currentTime = std::chrono::system_clock::now();
    constexpr const auto buffSz = 80;
    char buffer[buffSz];

    auto transformed = currentTime.time_since_epoch().count() / 1000000;

    auto millis = transformed % 1000;

    std::time_t tt;
    tt = system_clock::to_time_t ( currentTime );
    auto timeinfo = localtime (&tt);
    string fmt = "%H:%M:%S";
    if (includeDate) fmt = "%Y-%m-%d_" + fmt;
    size_t timeStrSz = strftime (buffer, buffSz, fmt.c_str(),timeinfo);
    snprintf(buffer + timeStrSz , buffSz - timeStrSz, ":%03d",(int)millis);

    return std::string(buffer);
}

std::string logTime()
{
    return getCurrentTimestamp();
}

LogStream out()
{
    return LogStream();
}

class TestMegaLogger : public Logger
{
    mutex logMutex;
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
        os << "] " << SimpleLogger::toStr(static_cast<LogLevel>(loglevel)) << ": ";
        if (message)
        {
            os << message;
        }
        // we can have the message AND the direct messages
        for (unsigned i = 0; i < numberMessages; ++i) os.write(directMessages[i], directMessagesSizes[i]);
#else
        os << "] " << SimpleLogger::toStr(static_cast<LogLevel>(loglevel)) << ": " << message;
#endif
        if (source)
        {
            os << " (" << source << ")";
        }
        os << std::endl;

        lock_guard<mutex> g(logMutex);

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
#ifdef _WIN32
                if (!IsDebuggerPresent())
#endif // _WIN32

                {
                    std::cout << os.str() << std::flush;
                }

                if (!gTestingInvalidArgs)
                {
                    if (loglevel <= logError)
                    {
                        ASSERT_GT(loglevel, logError) << os.str();
                    }
                }
            }

#ifdef _WIN32
            // Always show the logging in the output window in VS, very useful to see what's going on as the tests run
            // (with the high level --CI output visible in the app's own console window)
            OutputDebugStringA(os.str().c_str());
#endif // _WIN32
        }
    }

private:
    std::ofstream mLogFile;
};

class GTestLogger
  : public ::testing::EmptyTestEventListener
{
public:
    void OnTestEnd(const ::testing::TestInfo& info) override
    {
        std::string result = "FAILED";

        if (info.result()->Passed())
        {
            result = "PASSED";
        }

        out() << "GTEST: "
              << result
              << " "
              << info.test_case_name()
              << "."
              << info.name();
    }

    void OnTestPartResult(const ::testing::TestPartResult& result) override
    {
        using namespace ::testing;

        if (result.type() == TestPartResult::kSuccess) return;

        std::string file = "unknown";
        std::string line;

        if (result.file_name())
        {
            file = result.file_name();
        }

        if (result.line_number() >= 0)
        {
            line = std::to_string(result.line_number()) + ":";
        }

        out() << "GTEST: "
              << file
              << ":"
              << line
              << " Failure";

        std::istringstream istream(result.message());

        for (std::string s; std::getline(istream, s); )
        {
            out() << "GTEST: " << s;
        }
    }

    void OnTestStart(const ::testing::TestInfo& info) override
    {
        out() << "GTEST: RUNNING "
              << info.test_case_name()
              << "."
              << info.name();
    }
}; // GTestLogger

// Let us log even during post-test shutdown
TestMegaLogger megaLogger;

#ifdef ENABLE_SYNC
// destroy g_clientManager while the logging is still active
ClientManager* g_clientManager = nullptr;
#endif // ENABLE_SYNC

int main (int argc, char *argv[])
{
    if (!getenv("MEGA_EMAIL") || !getenv("MEGA_PWD") || !getenv("MEGA_EMAIL_AUX") || !getenv("MEGA_PWD_AUX"))
    {
        std::cout << "please set username and password env variables for test" << std::endl;
        return 1;
    }

#ifdef ENABLE_SYNC
    // destroy g_clientManager while the logging is still active, and before global destructors (for things like mutexes) run
    ClientManager clientManager;
    g_clientManager = &clientManager;
#endif // ENABLE_SYNC


    std::vector<char*> myargv1(argv, argv + argc);
    std::vector<char*> myargv2;
    bool startOneSecLogger = false;

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
            std::lock_guard<std::mutex> g(g_APIURL_default_mutex);
            string s = std::string(*it).substr(9);
            if (!s.empty())
            {
                if (s.back() != '/') s += "/";
                g_APIURL_default = s;
            }
            argc -= 1;
        }
        else if (std::string(*it) == "--RESUMESESSIONS")
        {
            gResumeSessions = true;
            argc -= 1;
        }
        else if (std::string(*it) == "--ONESECLOGGER")
        {
            startOneSecLogger = true;
            argc -= 1;
        }
        else
        {
            myargv2.push_back(*it);
        }
    }

    SimpleLogger::setLogLevel(logMax);
    SimpleLogger::setOutputClass(&megaLogger);

    // delete old test folders, created during previous runs
    TestFS testFS;
    testFS.DeleteTestFolder();
    testFS.DeleteTrashFolder();

#if defined(_WIN32) && defined(NO_READLINE)
    using namespace mega;
    WinConsole* wc = new CONSOLE_CLASS;
    wc->setShellConsole();
#endif

#if defined(__APPLE__)
    // Try and raise the file descriptor limit as high as we can.
    platformSetRLimitNumFile();
#endif // __APPLE__

    ::testing::InitGoogleTest(&argc, myargv2.data());

    if (gRunningInCI)
    {
        auto& listeners = testing::UnitTest::GetInstance()->listeners();
        listeners.Append(new GTestLogger());
    }

    bool exitFlag = false;
    std::thread one_sec_logger;
    if (startOneSecLogger)
    {
        one_sec_logger = std::thread([&](){
            int count = 0;
            while (!exitFlag)
            {
                LOG_debug << "onesec count: " << ++count;
                WaitMillisec(1000);
            }
        });
    }

    auto ret = RUN_ALL_TESTS();

    exitFlag = true;
    if (startOneSecLogger) one_sec_logger.join();

    //SimpleLogger::setOutputClass(nullptr);

    return ret;
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
    out() << "Local Test folder: " << testpath;
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
            out() << "Renaming " << oldpath << " to " << newpath << " failed."
                 << ec.message();
        }

        return;
    }

    // delete folder in a separate thread
    m_cleaners.emplace_back(thread([=]() mutable // ...mostly for fun, to avoid declaring another ec
        {
            fs::remove_all(newpath, ec);

            if (ec)
            {
                out() << "Deleting " << folder << " failed."
                     << ec.message();
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
    int errcount = 0;
    for (int i = 2; errcount < 20; ++i)
    {
        if (!fs::exists(p)) break;

        newpath = trashpath / fs::u8path(p.filename().stem().u8string() + "_" + to_string(i) + p.extension().u8string());

        if (!fs::exists(newpath))
        {
            std::error_code e;
            fs::rename(p, newpath, e);
            if (e)
            {
                LOG_err << "Failed to trash-rename " << p.u8string() << " to " << newpath.u8string() << ": " << e.message();
                WaitMillisec(500);
                errcount += 1;
            }
            else break;
        }
    }
}

fs::path makeNewTestRoot()
{
    fs::path p = TestFS::GetTestFolder();

    if (fs::exists(p))
    {
        moveToTrash(p);
    }

    std::error_code e;
    bool b = fs::create_directories(p, e);
    if (!b) { out() << "Failed to create base directory for test at: " << p << ", error: " << e.message(); }
    assert(b);
    return p;
}

std::unique_ptr<::mega::FileSystemAccess> makeFsAccess()
{
    return ::mega::make_unique<FSACCESS_CLASS>();
}

fs::path makeReusableClientFolder(const string& subfolder)
{
#ifdef WIN32
    auto pid = GetCurrentProcessId();
#else
    auto pid = getpid();
#endif

    fs::path p = TestFS::GetTestBaseFolder() / ("clients_" + std::to_string(pid)) / subfolder;

#ifndef NDEBUG
    bool b =
#endif
    fs::create_directories(p);
    assert(b);
    return p;
}
