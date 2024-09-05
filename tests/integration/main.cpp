#include "mega.h"
#include "mega/filesystem.h"
#include "gtest_common.h"
#include <cstdio>
#include <fstream>
#ifdef WIN32
#include <winhttp.h>
#endif

#include "sdk_test_utils.h"
#include "test.h"
#include "env_var_accounts.h"

#include <mega/fuse/common/mount_result.h>
#include <mega/fuse/common/service.h>

// If running in Jenkins, we use its working folder.  But for local manual testing, use a convenient location
std::string getLocalTestFolder()
{
    const std::string folderName{"mega_tests"};
#ifdef WIN32
    return std::string("c:\\tmp\\") + folderName;
#else
    // Should always find HOME on Posix, but use "." as backup
    return Utils::getenv("HOME", ".") + "/" + folderName;
#endif

}

fs::path LINK_EXTRACT_SCRIPT = "email_processor.py";

const string& getDefaultLogName()
{
    static const string LOG_NAME{ "test_integration.log" };
    return LOG_NAME;
}

bool gWriteLog = false;
string gLogName = getDefaultLogName();
bool gResumeSessions = false;
bool gScanOnly = false; // will be used in SRW
bool gManualVerification=false;

std::string USER_AGENT = "Integration Tests with GoogleTest framework";

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
    string command = "curl -s --data-binary @";
    command.append(filepath).append(" ").append(url);
    responsedata = runProgram(command, PROG_OUTPUT_TYPE::BINARY);
#endif
}

LogStream::~LogStream()
{
    auto data = mBuffer.str();

    // Always write messages via standard logger.
    LOG_debug << data;
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
    static bool writeCout;

    void log(const char* /*time*/, int loglevel, const char* source, const char* message
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

        if (loglevel <= SimpleLogger::getLogLevel())
        {
            if (gWriteLog)
            {
                if (!mLogFile.is_open())
                {
                    mLogFile.open(gLogName);
                }
                mLogFile << os.str() << std::flush;
            }
            else
            {
                bool output = writeCout;
#ifdef _WIN32
                if (IsDebuggerPresent())
                    output = false;
#endif // _WIN32

                if (output)
                    std::cout << os.str() << std::flush;
            }

#ifdef _WIN32
            // Always show the logging in the output window in VS, very useful to see what's going on as the tests run
            // (with the high level --log output visible in the app's own console window)
            OutputDebugStringA(os.str().c_str());
#endif // _WIN32
        }
    }

    void close() {
        mLogFile.close();
    }

private:
    std::ofstream mLogFile;
};
bool TestMegaLogger::writeCout = true;

class GTestLogger
  : public ::testing::EmptyTestEventListener
{
    static void toLog(const std::string& message)
    {
        out() << "GTEST: " << message;
    }

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

        RequestRetryRecorder::instance().report(toLog);
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

        RequestRetryRecorder::instance().report(toLog);
    }

    void OnTestStart(const ::testing::TestInfo& info) override
    {
        out() << "GTEST: RUNNING "
              << info.test_case_name()
              << "."
              << info.name();
    }
}; // GTestLogger

class RequestRetryReporter
  : public ::testing::EmptyTestEventListener
{
    static void toStandardOutput(const std::string& message)
    {
        std::cout << message << std::endl;
    }

public:
    void OnTestEnd(const ::testing::TestInfo& info) override
    {
        RequestRetryRecorder::instance().report(toStandardOutput);
    }

    void OnTestPartResult(const ::testing::TestPartResult& result) override
    {
        using ::testing::TestPartResult;

        // Only write report if the test failed.
        if (result.type() == TestPartResult::kSuccess)
            RequestRetryRecorder::instance().report(toStandardOutput);
    }
}; // RequestRetryReporter

// Let us log even during post-test shutdown
TestMegaLogger megaLogger;

#ifdef ENABLE_SYNC
// destroy g_clientManager while the logging is still active
ClientManager* g_clientManager = nullptr;
#endif // ENABLE_SYNC

RequestRetryRecorder* RequestRetryRecorder::mInstance = nullptr;


class SdkRuntimeArgValues : public RuntimeArgValues
{
public:
    SdkRuntimeArgValues(std::vector<std::string>&& args, std::vector<std::pair<std::string, std::string>>&& envVars) :
        RuntimeArgValues(std::move(args), std::move(envVars))
    {
        if (isHelp() || isListOnly() || !isValid())
        {
            return;
        }

        for (auto it = mArgs.begin(); it != mArgs.end();)
        {
            string arg = Utils::toUpperUtf8(*it);

            if (arg == "--LOG")
            {
                gWriteLog = true;
            }
            else if (arg == "--CI")
            {
                // options for continuous integration
                gWriteLog = true;
            }
            else if (arg == "--RESUMESESSIONS")
            {
                gResumeSessions = true;
            }
            else if (arg == "--SCANONLY")
            {
                gScanOnly = true;
            }

            ++it;
        }
    }

protected:
    void printCustomOptions() const override
    {
        cout << buildAlignedHelpString("--LOG",            {"Write output to log file"}) << '\n';
        cout << buildAlignedHelpString("--CI",             {"Include all 'Continuous Integration' options (same as --LOG)"}) << '\n';
        cout << buildAlignedHelpString("--RESUMESESSIONS", {"Resume previous account sessions, instead of full logins"}) << '\n';
        cout << buildAlignedHelpString("--SCANONLY",       {"Scan synced folders periodically instead of use file system notifications"}) << '\n';
    }

    void printCustomEnvVars() const override
    {
        cout << buildAlignedHelpString("  $MEGA_REAL_EMAIL", {"mega.co.nz email account to recevied account creation emails"}) << '\n';
        cout << buildAlignedHelpString("  $MEGA_REAL_PWD",   {"Password for Mega email account"}) << '\n';
        cout << buildAlignedHelpString("  $WORKSPACE",       {"Where to base tests, defaults to " + getLocalTestFolder() + " when not set"}) << '\n';
    }
};

// Make sure any megafs mounts are nuked before and after a run.
class ScopedAbortMounts
{
    // Nuke all megafs mounts on this machine.
    void abort()
    {
        // Convenience.
        using namespace ::mega::fuse;

        // Where would our mounts be mounted?
        auto workspace = TestFS::GetBaseFolder().u8string();

        // Nuke everything.
        auto result = Service::abort([&](const std::string& path) {
            // Make sure path is present under the workspace.
            return path.size() > workspace.size()
                   && !path.compare(0, workspace.size(), workspace);
        });

        // The mounts have been nuked.
        if (result == MOUNT_SUCCESS)
            return;

        // Couldn't nuke the mounts.
        LOG_warn << "Couldn't abort FUSE mounts: "
                 << toString(result);
    }

    // Whether we should abort any mounts.
    bool mAbort;

public:
    // Nuke the mounts when our tests start.
    ScopedAbortMounts(bool abort)
      : mAbort(abort)
    {
    }

    // And just before they end.
    ~ScopedAbortMounts()
    {
        if (mAbort)
            abort();
    }
}; // ScopedAbortMounts

int main (int argc, char *argv[])
{
    SdkRuntimeArgValues argVals(vector<string>(argv, argv + argc), getEnvVarAccounts().cloneVarNames());
    if (argVals.isHelp())
    {
        argVals.printHelp();
        return 0;
    }

    if (!argVals.isValid())
    {
        std::cout << "No tests executed (invalid arguments)." << std::endl;
        return -1;
    }

    if (argVals.isListOnly())
    {
        testing::InitGoogleTest(&argc, argv);
        return RUN_ALL_TESTS(); // returns 0 (success) or 1 (failed tests)
    }

    gLogName = argVals.getLog(); // set accordingly for worker or main process

    remove(gLogName.c_str());

    // Abort any stale mounts.
    ScopedAbortMounts abort(!argVals.isWorker());

    if (argVals.isMainProcWithWorkers())
    {
        // Don't run tests, only manage subprocesses.
        // To get here run with --INSTANCES:2 [--EMAIL-POOL:foo+bar-{1-28}@mega.nz]
        // If --EMAIL-POOL runtime arg is missing, email template will be taken from MEGA_EMAIL env var.
        // Password for all emails built from template will be taken from MEGA_PWD env var.
        // If it did not get an email template, it'll use 1 single subprocess with the existing env vars.
        GTestParallelRunner pr(std::move(argVals));
        string testBase = (TestFS::GetBaseFolder() / "pid_").u8string(); // see TestFS::GetProcessFolder()
        pr.useWorkerOutputPathForPid(std::move(testBase));
        return pr.run();
    }

    if (!argVals.getCustomApiUrl().empty())
    {
        g_APIURL_default = argVals.getCustomApiUrl();
    }
    if (!argVals.getCustomUserAget().empty())
    {
        USER_AGENT = argVals.getCustomUserAget();
    }
    if (argVals.isMainProcOnly())
    {
        // Env vars might need to be set, for example when an email template was used
        auto envVars = argVals.getEnvVarsForWorker(0);
        for (const auto& env : envVars)
        {
            Utils::setenv(env.first, env.second);
        }
    }

    // So we can track how often requests are retried.
    RequestRetryRecorder retryRecorder;

    try {

#ifdef ENABLE_SYNC
    // destroy g_clientManager while the logging is still active, and before global destructors (for things like mutexes) run
    ClientManager clientManager;
    g_clientManager = &clientManager;
#endif // ENABLE_SYNC

    sdk_test::setTestDataDir(fs::absolute(fs::path(argv[0]).parent_path()));

    SimpleLogger::setLogLevel(logMax);
    SimpleLogger::setOutputClass(&megaLogger);

    // delete old test folders, created during previous runs
    TestFS testFS;
    testFS.ClearProcessFolder();
    testFS.ChangeToProcessFolder();

#if defined(__APPLE__)
    // our waiter uses select which only supports file number <=1024.
    // by limiting max open files to 1024, we might able to know this error precisely
    platformSetRLimitNumFile(1024); 
#endif // __APPLE__

    // Add listeners.
    {
        auto& listeners = testing::UnitTest::GetInstance()->listeners();

        // Emit request retries to screen.
        listeners.Append(new RequestRetryReporter());

        // Emit test events to a log file.
        if (gWriteLog)
            listeners.Append(new GTestLogger());
    }

    ::testing::InitGoogleTest(&argc, argv);

    int gtestRet = RUN_ALL_TESTS();

#if defined(USE_OPENSSL) && !defined(OPENSSL_IS_BORINGSSL)
    if (CurlHttpIO::sslMutexes)
    {
        int numLocks = CRYPTO_num_locks();
        for (int i = 0; i < numLocks; ++i)
        {
            delete CurlHttpIO::sslMutexes[i];
        }
        delete [] CurlHttpIO::sslMutexes;
    }
#endif

#ifdef ENABLE_SYNC
    g_clientManager->clear();
#endif
    megaLogger.close();

    return gtestRet;

    }
    catch (exception& e) {
        cerr << argv[0] << ": exception: " << e.what() << endl;
        return -1;
    }
}




/*
**  TestFS implementation
*/

using namespace std;

fs::path TestFS::GetBaseFolder()
{
    return fs::path{Utils::getenv("WORKSPACE", getLocalTestFolder())};
}

fs::path TestFS::GetProcessFolder()
{
    fs::path testBase = GetBaseFolder() / ("pid_" + std::to_string(getCurrentPid()));
    return testBase;
}

fs::path TestFS::GetTestFolder()
{
    fs::path testpath = GetProcessFolder() / "test";
    return testpath;
}


fs::path TestFS::GetTrashFolder()
{
    return GetProcessFolder() / "trash";
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

void TestFS::ClearProcessFolder()
{
    fs::path base = GetProcessFolder();

    if (!fs::exists(base))
        return;

    FSACCESS_CLASS fsaccess;
    unique_ptr<DirAccess> dir(fsaccess.newdiraccess());
    
    LocalPath lbase = LocalPath::fromAbsolutePath(base.string());
    lbase.appendWithSeparator(LocalPath::fromRelativePath("*"), false);
    if (!dir->dopen(&lbase, nullptr, true))
        throw runtime_error("Can not read directory '" + lbase.toPath(false) + "'");

    LocalPath nameArg;
    nodetype_t ntype = TYPE_UNKNOWN;
    while (dir->dnext(lbase, nameArg, true, &ntype))
    {
        if (ntype == FILENODE) {
            fs::remove(nameArg.toPath(false));
        }
        else {
            fs::remove_all(nameArg.toPath(false));
        }
    }
}

void TestFS::ChangeToProcessFolder()
{
    fs::path base = GetProcessFolder();
    fs::create_directories(base);
    fs::current_path(base);
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
    if (!b) { out() << "Failed to create base directory for test at: " << p.u8string() << ", error: " << e.message(); }
    assert(b);
    return p;
}

fs::path makeReusableClientFolder(const string& subfolder)
{
    fs::path p = TestFS::GetProcessFolder() / ("clients_" + std::to_string(getCurrentPid())) / subfolder;

#ifndef NDEBUG
    bool b =
#endif
    fs::create_directories(p);
    assert(b);
    return p;
}


bool SdkTestBase::clearProcessFolderEachTest = false;

void SdkTestBase::SetUp()
{
    Test::SetUp();

    TestFS::ChangeToProcessFolder();

    if (clearProcessFolderEachTest)
    {
        // for testing that tests are independent, slow as NOD database deleted
        TestFS::ClearProcessFolder();
    }

    // Reset request retry statistics.
    RequestRetryRecorder::instance().reset();
}

fs::path getLinkExtractSrciptPath() {
    return sdk_test::getTestDataDir() / LINK_EXTRACT_SCRIPT;
}

bool isFileHidden(const LocalPath& path)
{
    return FileSystemAccess::isFileHidden(path) != 0;
}

bool isFileHidden(const fs::path& path)
{
    return isFileHidden(LocalPath::fromAbsolutePath(path.u8string()));
}

