#include "mega.h"
#include "mega/filesystem.h"
#include "mega/process.h"
#include "mega/testcommon/gtestcommon.h"
#include <gtest/gtest.h>
#include <stdio.h>
#include <fstream>
#ifdef WIN32
#include <winhttp.h>
#endif
#include <regex>

#include "test.h"

// If running in Jenkins, we use its working folder.  But for local manual testing, use a convenient location
#define LOCAL_TEST_FOLDER_NAME "mega_tests"
#ifdef WIN32
    #define LOCAL_TEST_FOLDER (string("c:\\tmp\\") + LOCAL_TEST_FOLDER_NAME)
#else
    #define LOCAL_TEST_FOLDER (string(getenv("HOME")) + "/" + LOCAL_TEST_FOLDER_NAME)
#endif

fs::path LINK_EXTRACT_SCRIPT = "email_processor.py";

fs::path executableDir; // Path to the folder containing the test executable.

const string LOG_NAME = "test_integration.log";
const string MASTER_LOG_NAME = "test_integration.master.log";
const string LOG_NAME_AFTER_CLOSE = "test_integration.after-closed.log";
const string LOG_TEMPLATE = "test_integration.{n}.log";
const string OUTPUT_TEMPLATE = "test_integration.{n}.out";

bool gWriteLog = false;
string gLogName = LOG_NAME;
bool gResumeSessions = false;
bool gScanOnly = false; // will be used in SRW
bool gOutputToCout = false;
bool gManualVerification=false;

// max accounts used by any test
// update if a test starts using more
int gMaxAccounts = 3;
std::string USER_AGENT = "Integration Tests with GoogleTest framework";

// exit code from proess when a gtest test has failed
// assert() failure on Windows is 3
// killed on Windows is 1
// crash on Windows is -1073741819 (STATUS_ACCESS_VIOLATION, 0xc0000005)
const int EXIT_GTEST_FAILURE = 10;

// use to force Jenkins to run NOT concurrently, 
// in case you think concurrecy is breaking the tests
// see also SdkTestBase::clearProcessFolderEachTest
bool alllowMultipleProcesses = true;

string_vector envVarAccount = {"MEGA_EMAIL", "MEGA_EMAIL_AUX", "MEGA_EMAIL_AUX2"};
string_vector envVarPass    = {"MEGA_PWD",   "MEGA_PWD_AUX",   "MEGA_PWD_AUX2"};

int launchMultipleProcesses(const string& argv0, const vector<string>& subprocessArgs, int numInstances,  bool liveOutput, bool timestampOutput, bool showProgress, const string& filter);

fs::path getTestDataDir() {
    // testing files are expected to be next to the binary.
    return executableDir;
}

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

// parse email template strings such as  sdk+test-setb-{1..100}@mega.co.nz
struct EmailTemplateParser
{
    // {} is used as <> is awkward in shells
    string prefix;
    string suffix;
    int min = -1;
    int max = -1; // inclusive


    int totalEmails() const { return max - min + 1; }

    // true if the email matches the template format
    static bool isTemplate(const string& str) { return str.find('{') != string::npos; }

    bool parse(const string& str);

    // n numbered from 0
    string format(int n)
    {
        int en = min + n;
        assert(en <= max);
        return prefix + to_string(en) + suffix;
    }
};

bool EmailTemplateParser::parse(const string& str)
{
    static regex emailRegex("([^{]*)[{](\\d+)\\.\\.(\\d+)[}](.*)");
    //  my+email-{1..30}-test@mega.co.nz
    smatch matches;
    if (!regex_match(str, matches, emailRegex))
    {
        cerr << "Invalid --email email template '" << str << "' should be in form name-{min..max}@mega.co.nz, e.g. fred+test-{1..100}@mega.co.nz" << endl;
        return false;
    }

    prefix = matches[1].str();      // e.g. fred+test-
    min = atoi(matches[2].str().c_str()); // only mathes digits, e.g. 1
    max = atoi(matches[3].str().c_str()); // only mathes digits, e.g. 100
    if (min > max)
    {
        cerr << "Invalid range in email template '" << str << "': max must be greter than or equal to min" << endl;
        return false;
    }
    suffix = matches[4].str();  // e.g. @mega.co.nz

    return true;
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

// "MEGA_EMAIL" -> "--email"
string megaEnvToSwitch(const string& var)
{
    return "--" + Utils::toLowerUtf8(Utils::replace(var.substr(strlen("MEGA_")), '_', '-'));
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

int main (int argc, char *argv[])
{
    // So we can track how often requests are retried.
    RequestRetryRecorder retryRecorder;

    try { // @todo: try/catch not indented to avoid merge conflict

#ifdef ENABLE_SYNC
    // destroy g_clientManager while the logging is still active, and before global destructors (for things like mutexes) run
    ClientManager clientManager;
    g_clientManager = &clientManager;
#endif // ENABLE_SYNC

    std::vector<string> argsForGTest;
    argsForGTest.push_back(argv[0]);
    std::vector<string> subprocessArgs;

    bool startOneSecLogger = false;
    bool showProgress = true;
    int  numInstances = -1;
    bool liveOutput = true;
    bool timestampOutput = true;
    string filter; // --gtest_filter
    bool writeElapsedCout = true;

    for (char** it = argv + 1; *it != nullptr; ++it)
    {
        string arg = Utils::toUpperUtf8(*it);
        
        if (Utils::startswith(arg, "--#"))
        {
            // comment, ignore
            // use to comment out args, e.g. --#INSTANCES:3
        }
        else if (arg.substr(0, 12) == "--USERAGENT:")
        {
            USER_AGENT = string(*it).substr(12);
            subprocessArgs.push_back(*it);
        }
        else if (arg == "--COUT")
        {
            gOutputToCout = true;
            subprocessArgs.push_back(*it);
        }
        else if (arg == "--CI")
        {
            // options for continious integration
            gWriteLog = true;
            showProgress = false;
            liveOutput = true;
            subprocessArgs.push_back(*it);
        }
        else if (arg == "--LOG")
        {
            gWriteLog = true;
            subprocessArgs.push_back(*it);
        }
        else if (Utils::startswith(arg, "--LOG:"))
        {
            // used internally on sub procesasses
            gWriteLog = true;
            gLogName = string(*it).substr(strlen("--LOG:"));
        }
        else if (arg == "--SCANONLY")
        {
            gScanOnly = true;
            subprocessArgs.push_back(*it);
        }
        else if (arg.substr(0, 9) == "--APIURL:")
        {
            std::lock_guard<std::mutex> g(g_APIURL_default_mutex);
            string s = std::string(*it).substr(9);
            if (!s.empty())
            {
                if (s.back() != '/') s += "/";
                g_APIURL_default = s;
            }
            subprocessArgs.push_back(*it);
        }
        else if (std::string(*it) == "--RESUMESESSIONS")
        {
            gResumeSessions = true;
            subprocessArgs.push_back(*it);
        }
        else if (std::string(*it) == "--ONESECLOGGER")
        {
            startOneSecLogger = true;
            subprocessArgs.push_back(*it);
        }
        else if (Utils::startswith(arg, "--INSTANCES:"))
        {
            // 0 = traditional behaviour
            // 1 = 1 subprocess
            string str = string(*it).substr(strlen("--INSTANCES:"));
            numInstances = atoi(str.c_str());
            if (numInstances == 0 && str != "0") {
                cerr << "Invalid --INSTANCES: '" << *it << "'" << endl;
                return 1;
            }
            
            if (!alllowMultipleProcesses) {
                cout << "alllowMultipleProcesses false, NOT running concurrently" << endl;
                numInstances = 1;
                // run with one sub process so -MEGA_EMAIL/--email template and passsword works
            }

            gLogName = MASTER_LOG_NAME;

        }
        else if (Utils::startswith(arg, "--EMAIL:"))
        {
            string str =  string(*it).substr(strlen("--EMAIL:"));
            Utils::setenv("MEGA_EMAIL", str);
        }
        else if (arg == "--NO-LIVE")
        {
            liveOutput = false;
        }
        else if (arg == "--NO-TIMESTAMP")
        {
            timestampOutput = false;
        }
        else if (arg == "--NO-LOG-COUT")
        {
            // used internally to quieten output
            TestMegaLogger::writeCout = false;
            writeElapsedCout = false;
            subprocessArgs.push_back(*it);
        }
        else if (Utils::startswith(arg, "--EMAIL-AUX:"))
        {
            string str =  string(*it).substr(strlen("--EMAIL-AUX:"));
            Utils::setenv("MEGA_EMAIL_AUX", str);
        }
        else if (Utils::startswith(arg, "--EMAIL-AUX2:"))
        {
            string str =  string(*it).substr(strlen("--EMAIL-AUX2:"));
            Utils::setenv("MEGA_EMAIL_AUX2", str);
        }
        else if (Utils::startswith(arg, "--REAL-EMAIL:"))
        {
            string str =  string(*it).substr(strlen("--REAL-EMAIL:"));
            Utils::setenv("MEGA_REAL_EMAIL", str);
        }
        else if (Utils::startswith(arg, "--WORKSPACE:"))
        {
            string str =  string(*it).substr(strlen("--WORKSPACE:"));
            Utils::setenv("WORKSPACE", str);
        }
        else if (arg == "--NO-PROGRESS")
        {
            showProgress = false;
            subprocessArgs.push_back(*it);
        }
        else if (arg == "--ENV")
        {
            cout << "%MEGA_EMAIL: " << Utils::getenv("MEGA_EMAIL", "<not set>") << endl;
            cout << "%MEGA_PWD: " << Utils::getenv("MEGA_PWD", "<not set>") << endl;
            cout << "%MEGA_EMAIL_AUX: " << Utils::getenv("MEGA_EMAIL_AUX", "<not set>") << endl;
            cout << "%MEGA_PWD_AUX: " << Utils::getenv("MEGA_PWD_AUX", "<not set>") << endl;
            cout << "%MEGA_EMAIL_AUX2: " << Utils::getenv("MEGA_EMAIL_AUX2", "<not set>") << endl;
            cout << "%MEGA_PWD_AUX2: " << Utils::getenv("MEGA_PWD_AUX2", "<not set>") << endl;
            cout << "%MEGA_REAL_EMAIL: " << Utils::getenv("MEGA_REAL_EMAIL", "<not set>") << endl;
            cout << "%MEGA_REAL_PWD: " << Utils::getenv("MEGA_REAL_PWD", "<not set>") << endl;
            cout << "%WORKSPACE: " << Utils::getenv("WORKSPACE", "<not set>") << endl;
            return 0;
        }
        else if (arg == "--GHELP")
        {
            // get gtest to print its help
            static char help[7];
            // not const for gtest
            strncpy(help, "--help", sizeof help);
            argsForGTest.push_back(help);
            subprocessArgs.push_back(*it);
        }
        else if (Utils::startswith(arg, "--GTEST_FILTER="))
        {
            // compatible with gtest
            filter = string(*it).substr(strlen("--GTEST_FILTER="));
        }
        else if (arg == "--HELP")
        {
            cout << "Options are case insensitive, and may be commented out with --#ARG. Legacy environemnt variables may be specified" << endl;
            cout << endl;
            cout << "--EMAIL                     Email address for first MEGA account, one can set $MEGA_EMAIL" << endl;
            cout << "                            May contain a {min..max} to set all three email addresses when running non concurently." << endl;
            cout << "                            When running concurrently, using --instances, must contain the {min..max}, e.g: test+email-{1..50}@mega.co.nz" << endl;
            cout << "--EMAIL-AUX                 Email address for second MEGA account, one can set $MEGA_EMAIL_AUX" << endl;
            cout << "--EMAIL-AUX2                Email address for third MEGA account, one can set $MEGA_EMAIL_AUX2" << endl;
            cout << "--REAL-EMAIL:email          Mega.co.nz email account to recevied account creation emails, one can set $MEGA_REAL_EMAIL " << endl;
            cout << "--WORKSPACE:dir             Where to base tests, one case se t$WORKSPACE, defaults to " << LOCAL_TEST_FOLDER << endl;
            cout << "--CI                        Options for Jenkins, --log, --no-live and --no-progress" << endl;
            cout << "--LOG                       Write a log to " << LOG_NAME << endl;
            cout << "--LOG:file.log              Write to a specified log" << endl;
            cout << "--COUT                      Also log to stdout" << endl;
            cout << "--NO-LIVE                   when running concurrently do not show stdout of subprocesses as they run" << endl;
            cout << "--NO-TIMESTAMP              Do not prefix stdout and stderr from subprocesses with a timestamp" << endl;
            cout << "--INSTANCES:n               Run n processes in parallel" << endl;
            cout << "                            --email or $MEGA_EMAIL is a template with {min..max}" << endl;
            cout << "                            presently  " <<  gMaxAccounts << " accounts are required per process" << endl;
            cout << "                            --pwd or $MEGA_PWD is password for all MEGA accounts" << endl;
            cout << "--USERAGENT:agent           HTTP User-Agent to set" << LOG_NAME << endl;
            cout << "--APIURL:url                Base URL to use for contacting the server" << endl;
            cout << "--ONESECLOGGER              Write counting message to log every second" << endl;
            cout << "--RESUMESESSIONS    " << endl;
            //cout << "--SCANONLY          " << endl; // not presently used
            cout << "--NO-PROGRESS               When running concurrently with --INSTANCES do not show progress bar and ETTA" << endl;
            cout << "--GHELP                     Show gtest options help" << endl;
            cout << "--#arg                      Commented out argument, ignored" << endl;
            cout << endl;
            cout << "Useful GTest options:" << endl;
            cout << "  --gtest_filter=FILTER     set tests to execute, can be : separated list, * or wildcard" << endl;
            cout << "                            e.g. --gtest_filter=SdkTest.SdkTestShares" << endl;
            cout << "Environment variables:" << endl;
            cout << "  $MEGA_EMAIL               [required or --email] Email address for first MEGA account, can set or override with --EMAIL" << endl;
            cout << "                            May, and is required when running concurrently using --instances, contain {min..max}, e.g: test+email-{1..50}@mega.co.nz" << endl;
            cout << "                            to set all MEGA account email addresses" << endl;
            cout << "  $MEGA_PWD                 [required] Passsword for first MEGA account, becomes the default for $MEGA_PWD_AUX and $MEGA_PWD_AUX2" << endl;
            cout << "  $MEGA_EMAIL_AUX           Email address for second MEGA account, can set or override with --EMAIL-AUX" << endl;
            cout << "  $MEGA_PWD_AUX             Password for second MEGA account, defaults to MEGA_PWD" << endl;
            cout << "  $MEGA_EMAIL_AUX2          Email address for third MEGA account, can set or override with --EMAIL-AUX2" << endl;
            cout << "  $MEGA_PWD_AUX2            Password for third MEGA account, defaults to MEGA_PWD" << endl;
            cout << "  $MEGA_REAL_EMAIL          mega.co.nz email account to recevied account creation emails, can set or override with --REAL-EMAIL" << endl;
            cout << "  $MEGA_REAL_PWD            Password for Mega email account" << endl;
            cout << "  $WORKSPACE                Where to base tests, can set or override with --WORKSPACE, defaults to " << LOCAL_TEST_FOLDER << endl;
            // MEGA_LINK_EXTRACT_SCRIPT is obsolete, now looks in same folder as executable if not defined
            return 0;
        }
        else
        {
            argsForGTest.push_back(*it);
            subprocessArgs.push_back(*it);
        }
    }

    if (!Utils::hasenv("MEGA_REAL_EMAIL"))
        cerr << "Warning: Neither --real-email nor MEGA_REAL_EMAIL set" << endl;

    // convert WORKSPACE to absolute path
    if (Utils::hasenv("WORKSPACE"))
        Utils::setenv("WORKSPACE", fs::absolute(Utils::getenv("WORKSPACE", ".")).string());

    executableDir = fs::absolute(fs::path(argv[0]).parent_path());

    SimpleLogger::setLogLevel(logMax);
    SimpleLogger::setOutputClass(&megaLogger);

    // can use log from here:
    out() << "cwd: " << fs::current_path();
    out() << "executableDir: " << executableDir;

    // RUN TESTS IN PARALLEL (it requires --instances:n, n=0 to run in series)
    if (numInstances >= 1)
    {
        for (const string& var : string_vector{ "MEGA_EMAIL", "MEGA_PWD"})
        {
            if (!Utils::hasenv(var))
            {
                cerr << "Please set " << var << " or " << megaEnvToSwitch(var) << endl;
                return 1;
            }
        }

        return launchMultipleProcesses(argv[0], subprocessArgs, numInstances, liveOutput, timestampOutput, showProgress, filter);
    }
    // else --> from this point...

    // RUN IN SERIES

    if (!filter.empty())
        argsForGTest.push_back("--gtest_filter=" + filter);

    // set MEGA_PWD_AUX and MEGA_PWD_AUX2 if not already set
    bool hasPwd = false;
    string pwd = Utils::getenv("MEGA_PWD", &hasPwd);
    if (hasPwd)
    {
        if (!Utils::hasenv("MEGA_PWD_AUX"))
            Utils::setenv("MEGA_PWD_AUX", pwd);
        if (!Utils::hasenv("MEGA_PWD_AUX2"))
            Utils::setenv("MEGA_PWD_AUX2", pwd);
    }

    // if the emails is a template (but running in series), then set MEGA_EMAIL, MEGA_EMAIL_AUX and MEGA_EMAIL_AUX2
    bool hasEmail = false;
    string email = Utils::getenv("MEGA_EMAIL", &hasEmail);
    if (hasEmail && EmailTemplateParser::isTemplate(email))
    {
        EmailTemplateParser parser;
        if (!parser.parse(email))
        {
            return 1;
        }

        // do we have enough emails to run in series, but with a template email?
        if (parser.totalEmails() < (int)envVarAccount.size())
        {
            cerr << "Not enough email addresses in email template '" << email << "': provides " << parser.totalEmails() << ", " << envVarAccount.size() << " requried" << endl;
            return 1;
        }

        for (int i = 0; i < (int)envVarAccount.size(); ++i)
            Utils::setenv(envVarAccount[i], parser.format(i));
    }

    // sanity check we have all required emails/pwds
    for (const string& var : string_vector{ "MEGA_EMAIL", "MEGA_PWD", "MEGA_EMAIL_AUX", "MEGA_PWD_AUX", "MEGA_EMAIL_AUX2", "MEGA_PWD_AUX2" })
    {
        if (!Utils::hasenv(var))
        {
            cerr << "Please set " << var << " or " << megaEnvToSwitch(var) << endl;
            return 1;
        }
    }

    m_time_t start = m_time();  // to calculate elapsed time

    // delete old test folders, created during previous runs
    TestFS testFS;
    testFS.DeleteTestFolder();
    testFS.DeleteTrashFolder();
    testFS.ChangeToProcessFolder();

#if defined(_WIN32) && defined(NO_READLINE)
    using namespace mega;
    WinConsole* wc = new CONSOLE_CLASS;
    wc->setShellConsole();
#endif

#if defined(__APPLE__)
    // our waiter uses select which only supports file number <=1024.
    // by limiting max open files to 1024, we might able to know this error precisely
    platformSetRLimitNumFile(1024); 
#endif // __APPLE__

    vector<char*> argsForGTestCptrs;
    transform(argsForGTest.begin(), argsForGTest.end(), inserter(argsForGTestCptrs, argsForGTestCptrs.end()), [](const string& str) { return const_cast<char*>(str.c_str()); });
    int argsForGTestCount = (int)argsForGTestCptrs.size();
    ::testing::InitGoogleTest(&argsForGTestCount, argsForGTestCptrs.data());
    if (argsForGTestCount > 1)
    {
        cerr << "Warning unrecognised switches:";
        for (vector<char*>::const_iterator i = argsForGTestCptrs.begin() + 1; i < argsForGTestCptrs.begin() + argsForGTestCount; ++i)
            cerr << ' ' << *i;
        cerr << endl;
    }

    // Add listeners.
    {
        auto& listeners = testing::UnitTest::GetInstance()->listeners();

        // Emit request retries to screen when appropriate.
        if (!gOutputToCout)
            listeners.Append(new RequestRetryReporter());

        // Emit test events to a log file.
        if (gWriteLog)
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

    int gtestRet = RUN_ALL_TESTS();

    exitFlag = true;
    if (startOneSecLogger) one_sec_logger.join();

    //SimpleLogger::setOutputClass(nullptr);
    m_time_t end = m_time();

    m_time_t elapsed = end - start;
    out() << "elapsed: " << ((double)elapsed / 60.0) << " mins";
    if (writeElapsedCout)
        cout << "elapsed: " << ((double)elapsed / 60.0) << " mins" << endl;

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

    if (gtestRet != 0)
        return EXIT_GTEST_FAILURE;
    return 0;

    } // @todo: try/catch not indented to avoid merge conflict
    catch (exception& e) {
        cerr << argv[0] << ": fatal error: " << e.what() << endl;
        return 1;
    }
}




/*
**  TestFS implementation
*/

using namespace std;

fs::path TestFS::GetBaseFolder()
{
    const char* jenkins_folder = getenv("WORKSPACE");
    fs::path base = jenkins_folder ? fs::path(jenkins_folder) : fs::path(LOCAL_TEST_FOLDER);
    return base;
}

fs::path TestFS::GetProcessFolder()
{
#ifdef WIN32
    auto pid = GetCurrentProcessId();
#else
    auto pid = getpid();
#endif

    fs::path testBase = GetBaseFolder() / ("pid_" + std::to_string(pid));
    return testBase;
}

fs::path TestFS::GetTestFolder()
{
    fs::path testpath = GetProcessFolder() / "test";
    out() << "Local Test folder: " << testpath;
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
    
    LocalPath lbase = LocalPath::fromAbsolutePath(base.string().c_str());
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
    LOG_debug << "TestFS::ChangeToProcessFolder() " << fs::current_path();
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
#ifdef WIN32
    auto pid = GetCurrentProcessId();
#else
    auto pid = getpid();
#endif

    fs::path p = TestFS::GetProcessFolder() / ("clients_" + std::to_string(pid)) / subfolder;

#ifndef NDEBUG
    bool b =
#endif
    fs::create_directories(p);
    assert(b);
    return p;
}

regex testResultLineRegex("^\\[\\s*([A-Z]+)\\s*\\]\\s+([^ .]+)[.]([^ .]+)\\s+[(][^)]*[)]$");
// 1: OK|FAILED
// 2: suite
// 3: test-name
// [       OK ] SyncTest.RootHasFilesystemWatch (14319 ms)
// [  FAILED  ] SyncTest.BasicSync_ClientToSDKConfigMigration (29829 ms)
// [  FAILED  ] 6 tests, listed below :
// [  FAILED  ] SdkTest.SdkTestContacts

/**
 * @brief The TestProcess struct
 *
 * TODO: provide a basic description of the purpose of this class and
 * how it works.
 */
struct TestProcess
{
    // id of the (sub)process running parallel
    // (used as a prefix for output like logs files, etc. Starts from 0)
    int id = -1;

    unique_ptr<Process> process;
    // so can destroy before close log

    // filename to temporary store the output from GTest
    string outputFilename;

    // stdout and stderr / 256KiB
    ofstream out;

    // filename to temporary store the output from SDK's logger
    // only created if --log specified
    // can not contain the PID as we need to delete it before the process starts
    string logFilename;

    vector<string> failedTests;
    vector<string> successfulTests;

    // write to stderr/stdout as lines are outputted by sub-process
    bool liveOutput = true;

    // add timestamp as prefix to output lines
    bool timestampOutput = true;

private:
    string unprocessedOut;
    string unprocessedError;

    void writeData(ostream& console, ostream& file, string& buffer, const unsigned char* data, size_t len, function<void(const string& line)> processLine = nullptr) const;

    // stores output from GTest in temporary strings for summary of the process results
    // (this is the lambda passed to writeData(), only for stdout, and used by flush() too)
    void processOutputLine(const string& line);

public:
    void writeStdout(const unsigned char* data, size_t len);
    void writeStderr(const unsigned char* data, size_t len);
    void flush();

    // bool is true for success ("OK")
    function<void(bool)> processTestResult;

    string getOutputPrefix() const;
};

void TestProcess::writeStderr(const unsigned char* data, size_t len)
{
    writeData(cerr, out, unprocessedError, data, len);
}

void TestProcess::writeStdout(const unsigned char* data, size_t len)
{
    writeData(cout, out, unprocessedOut, data, len, [=](const string& line) { processOutputLine(line); });
}

void TestProcess::flush() {

    processOutputLine(unprocessedOut);

    if (!unprocessedOut.empty())
    {
        string prefix = getOutputPrefix();
        out << prefix << unprocessedOut << endl;
        if (liveOutput)
            cout << prefix << "#" << id << " "  << unprocessedOut << endl;
        unprocessedOut.clear();
    }

    if (!unprocessedError.empty())
    {
        string prefix = getOutputPrefix();
        out << prefix << unprocessedError << endl;
        if (liveOutput)
            cerr << prefix << "#" << id << " "  << unprocessedError << endl;
        unprocessedError.clear();
    }
}

string TestProcess::getOutputPrefix() const
{
    // must be constant width
    if (timestampOutput)
        return getCurrentTimestamp(true) + " ";
    return "";
}

void TestProcess::processOutputLine(const string &line) {

    smatch matches;
    if (!regex_search(line, matches, testResultLineRegex))
        return;

    if (matches[1].str() == "FAILED")
    {
        failedTests.push_back(matches[2].str() + "." + matches[3].str());
        if (processTestResult != nullptr)
            processTestResult(false);
    }
    else if (matches[1].str() == "OK")
    {
        successfulTests.push_back(matches[2].str() + "." + matches[3].str());
        if (processTestResult != nullptr)
            processTestResult(true);
    }
}

void TestProcess::writeData(ostream &console, ostream& file, string &buffer, const unsigned char* data, size_t len, function<void(const string &line)> processLine) const
{
    // @todo: change (12345 ms) to 2:24:02.23 or 23.34 mins

    // just in case the bytes turn up splitting lines
    buffer.append((const char*)data, len);
    string::size_type pos = 0;
    for (;;)
    {
        // processs line by line
        string::size_type epos = buffer.find('\n', pos);
        if (epos == string::npos)
        {
            // leave the rest of the line in the buffer, just in case it doesn't finish with '\n'
            buffer.erase(0, pos);
            break;
        }

        // --> example of text to be parsed:
        // [       OK ] SyncTest.RootHasFilesystemWatch (14319 ms)
        // [  FAILED  ] SyncTest.BasicSync_ClientToSDKConfigMigration (29829 ms)
        // [  FAILED  ] 6 tests, listed below :
        // [  FAILED  ] SdkTest.SdkTestContacts

        string line = buffer.substr(pos, epos - pos);
        if (!line.empty() && line.back() == '\r')
        {
            // WIN32: \r\n
            line.pop_back();
        }
        pos = epos + 1;

        string prefix = getOutputPrefix();
        file << prefix << line << endl;
        if (liveOutput)
            console << prefix << "#" << id << " " << line << endl;

        if (processLine != nullptr)
            processLine(line);
    }
}

/**  
 * @brief  run the current executable 'argv0' with --gtest_list_tests and populate 'names'
 *
 * @param names to append to
 * @param argv0 path to executable
 * @param filter "" for non --gtest_filter=FILTER
 * @return true if succeeds
 */
bool findTests(vector<string>& tests, const string& argv0, const string& filter, int& disabledTestsCount)
{
    Process ti; // used to list tests through GTest
    Process::StringSink output;
    Process::StringSink errorOutput;

    vector<string> args = { argv0, "--gtest_list_tests", "--no-log-cout" };
    if (!filter.empty())
        args.push_back("--gtest_filter=" + filter);
    ti.run(args, {}, output.func(), errorOutput.func());
    if (!ti.wait())
    {
        cerr << argv0 << " --gtest_list_tests failed: " << ti.getExitMessage() << ": " << errorOutput;
        return false;
    }
    // output:
    // SdkTest.
    //   SdkTestCreateAccount
    //   DISABLED_SdkTestCreateEphmeralPlusPlusAccount

    string testSuite;
    string_vector lines;
    readLines(output, lines);
    for (const string& line : lines)
    {
        // test suite
        if (!Utils::startswith(line, ' '))
        {
            // name of test suite
            if (!line.empty() && !isalpha(line.front()))
            {
                LOG_err << "Bad name for test suite" << line << "";
                return false;
            }
            testSuite = line;
            continue;
        }

        // test cases
        string testCase = Utils::trim(line);
        if (testSuite.empty())
        {
            LOG_err << "Indented test case '" << testCase << "' before any test suite";
            return false;
        }

        // count of disabled tests
        if (Utils::startswith(testCase, "DISABLED_"))
        {
            ++disabledTestsCount;
            continue;
        }

        tests.push_back(testSuite+testCase);
    }

    return true;
}

void copyStream(ostream& os, istream& is)
{
    char buf[10 * 1024];
    do
    {
        is.read(buf, sizeof buf);
        os.write(buf, is.gcount());
    } while (is.gcount() > 0);
}

/**
 * @beief Run concurrently.
 * 
 * @param argv0 main's argv[0]
 * @param subprocessArgs Arguments to pass to each subprocess
 * @param numInstances number of sub processes --instacnes
 * @param liveOutput show output from sub processes as they run !--no-live
 * @param showProgress show a progrss bar and ETTA !--no-progess
 * @paraam filter --gtest_filter value or "" for none
 * @return exit code for program
 */
int launchMultipleProcesses(const string& argv0, const vector<string>& subprocessArgs, int numInstances, bool liveOutput, bool timestampOutput, bool showProgress, const string &filter)
{
    try
    {
        m_time_t start = m_time();

        out() << "lauchMultipleProcesses cwd " << fs::current_path();

        vector<string> tests;
        int disabledTestsCount = 0;
        if (!findTests(tests, argv0, filter, disabledTestsCount))
        {
            // Google Test reports success if it runs zero tests (none has failed)
            cout << "Running 0 tests from 0 test suites" << endl;
            return 0;
        }

        // adjust the number of sub processes in case there's not enough tests to feed them
        if ((int)tests.size() < numInstances)
            numInstances = (int)tests.size();

        // get email from environment variables and initialize email's parser
        bool found = false;
        string emailTemplate = Utils::getenv("MEGA_EMAIL", &found);
        if (!found)
        {
            cerr << "No MEGA_EMAIL nor --email email template" << endl;
            return 1;
        }
        EmailTemplateParser parser;
        if (!parser.parse(emailTemplate))
        {
            return 1;
        }
        int emailAccountsRequired = gMaxAccounts * numInstances;
        if (parser.totalEmails() < emailAccountsRequired)
        {
            cerr << "Not enough email addresses in email template '" << emailTemplate << "': provides " << parser.totalEmails() << ", " << emailAccountsRequired << " requried with " << numInstances << " instances and max " << gMaxAccounts << " accounts per test" << endl;
            return 1;
        }

        string password = Utils::getenv("MEGA_PWD", &found);
        assert(found);

        // distribute tests among sub processes
        vector<string> testArgs(numInstances);
        vector<string>::iterator i = testArgs.begin();
        for (auto test : tests)
        {
            string& str = *i;

            // add separator ':' between tests
            if (!str.empty())  str.append(":");

            str.append(test);

            if (++i == testArgs.end()) i = testArgs.begin();
        }

        // remove output files if they exist
        vector<TestProcess> processes(numInstances);
        for (TestProcess& test: processes)
        {
            if (fs::exists(test.outputFilename))
                fs::remove(test.outputFilename);
            if (fs::exists(test.logFilename))
                fs::remove(test.logFilename);
        }

        ConsoleProgressBar progress(tests.size(), true);
        
        // form command lines and launch sub processes
        vector<string>::iterator tai = testArgs.begin();
        int processCount = 0;
        for (TestProcess& tproc: processes)
        {
            tproc.id = processCount;
            vector<string> args = { argv0 };
            assert(!tai->empty()); // should not be blank, could be if n
            string testsArg = "--gtest_filter=" + *tai;
            // testsArg is 7,548 chars long if all tests as listed for one instance
            // Windows limits this to 32k so we have lots of head room
            // On Linux anbd MacOS had an unpredictable but large 100-200kish limit
            // We could use gtest sharding rather than setting --gtest_filter but would still have to list tests to get the count
            args.push_back(testsArg);
            tproc.logFilename = Utils::replace(LOG_TEMPLATE, "{n}", to_string(tproc.id));
            // can not contain the PID as passed into subprocess as argument

            args.insert(args.end(), subprocessArgs.begin(), subprocessArgs.end());

            if (gWriteLog)
                args.push_back("--LOG:" + tproc.logFilename);

            // prepare pairs of "email" + "password"
            int emailNum = tproc.id * gMaxAccounts;
            unordered_map<string, string> env;
            assert(envVarAccount.size() >= (size_t)gMaxAccounts);
            assert(envVarPass.size() >= (size_t)gMaxAccounts);
            for (int ei = 0; ei < gMaxAccounts; ++ei, ++emailNum)
            {
                env[envVarAccount[ei]] = parser.format(emailNum);
                env[envVarPass[ei]] = password;
            }

            tproc.outputFilename = Utils::replace(OUTPUT_TEMPLATE, "{n}", to_string(tproc.id));
            tproc.out.open(tproc.outputFilename, ios::binary);
            if (!tproc.out)
            {
                cerr << "Can not create test sub process output, filename '" << tproc.outputFilename << "'" << endl;
                return 1;
            }

            if (showProgress)
                tproc.processTestResult = [&](bool /*result*/) { progress.inc(); };
            Process::DataReaderFunc outReader = [&](const unsigned char* data, size_t len) {tproc.writeStdout(data, len); };
            Process::DataReaderFunc errorReader = [&](const unsigned char* data, size_t len) {tproc.writeStderr(data, len); };
            out() << "Running: " << Process::formCommandLine(args);
            tproc.process.reset(new Process());
            if (!tproc.process->run(args, env, outReader, errorReader))
            {
                return 1;
            }
            tproc.liveOutput = liveOutput;
            tproc.timestampOutput = timestampOutput;
            ++processCount;
            ++tai;
        }

        if (!processes.empty())
        {
            // indent so can tell the output from progress lines
            // 1: [       OK ] SdkTest.SdkTestShareKeys (57218 ms)
            //    17/125 ETTA 00:13:07 [>>>>>                                   ]
            progress.setPrefix(string(processes.front().getOutputPrefix().length(), ' ') + "   ");
        }

        // run until all sub processes have exited
        bool anyFailed = false;
        for (;;)
        {
            bool anyAlive = false;
            bool anyRead = false;
            for (TestProcess& test : processes)
            {
                if (test.process->isAlive())
                {
                    anyAlive = true;
                    anyRead |= test.process->poll();
                }
                else
                {
                    anyFailed |= !test.process->hasExitedOk();

                    if (!test.process->poll())
                    {
                        // may still have unread pipe data if exited
                        continue;
                    }
                    anyRead |= true;
                }
            }
            if (!anyAlive && !anyRead)
                break;
            if (!anyRead)
                usleep(100000); // 0.1 sec
        }

        // process and flush any stdout and stderr partial lines
        for (TestProcess& test : processes)
            test.flush();

        // output all stdout and stderr stored in files from processes to stdout (in order)
        cout << "====================================================================================================" << endl;
        for (TestProcess& test : processes)
        {
            test.out.close();

            cout << test.outputFilename << endl;
            {
                ifstream in(test.outputFilename);
                copyStream(cout, in);
            }
            cout << "----------------------------------------------------------------------------------------------------" << endl;
        }

        // write test failures at end so they are visible
        size_t totalFailedTests = 0;
        size_t totalSuccessfulTests = 0;
        for (TestProcess& proc : processes)
        {
            totalFailedTests += proc.failedTests.size();
            totalSuccessfulTests += proc.successfulTests.size();
        }
        cout << (totalFailedTests + totalSuccessfulTests) << " executed tests out of " << tests.size() << endl;
        cout << "[  PASSED  ] " << totalSuccessfulTests << " tests." << endl;
        if (totalFailedTests)
        {
            cout << "[  FAILED  ] " << totalFailedTests << " tests, listed below:" << endl;
            for (TestProcess& proc : processes)
            {
                for (const string& test : proc.failedTests)
                {
                    cout << "[  FAILED  ] " << test << endl;
                }
            }

            cout << endl << totalFailedTests << " FAILED TESTS" << endl;
        }
        if (disabledTestsCount)
        {
            cout << endl << " YOU HAVE " << disabledTestsCount << " DISABLED TESTS" << endl;
        }


        // show abnormal sub process statuses
        for (TestProcess& test : processes)
        {
            if (test.process->isAlive())
            {
                cout << "<< PROCESS STILL ALIVE >> #" << test.id << " (PID:"<< test.process->getPid() << ") process is still running" << endl;
                assert(!test.process->isAlive() && "Already waited for processes to die");
            }
            else if (test.process->hasExited())
            {
                if (!test.process->hasExitedOk() && test.process->getExitCode() != EXIT_GTEST_FAILURE) // EXIT_GTEST_FAILURE if any google test fails. Not strictly a sub-process failure.
                {
                    cout << "<< PROCESS FAILURE >> #" << test.id << " (PID:"<< test.process->getPid() << ") process exited with " << test.process->getExitCode() << endl;
                }
            }
            else if (test.process->hasTerminateBySignal())
            {
                cout << "<< PROCESS SIGNALED >> #" << test.id << " (PID:"<< test.process->getPid() << ") process terminated with signal " << test.process->getExitSignalDescription() << endl;
            }
            else
            {
                // should never happen
                // assert() here not useful
                cout << "<< PROCESS UNKNOWN FAILURE >> #" << test.id << " (PID:"<< test.process->getPid() << ") internal error: prrocess terminated for unknown cause" << endl;
            }
        }

        // write to log before we close it
        m_time_t end = m_time();
        m_time_t elapsed = end - start;
        out() << "elapsed: " << ((double)elapsed / 60.0) << " mins";
        cout << "elapsed: " << ((double)elapsed / 60.0) << " mins" << endl;

        // destroy that that shall write to the log
        // can not write sub process log lines to the log or we shall get another set of line prefixes
        for (TestProcess& test : processes)
        {
            test.process.reset();
        }

#ifdef ENABLE_SYNC
        g_clientManager->clear();
#endif

        // join all the SDK's log files together
        // we should not write to the log after this
        {
            string logName = gLogName;
            
            // just in case the log is opened again by logging from this point, set a differnt name
            // (the file is left in startup directory and shown by Jenkins)
            gLogName = LOG_NAME_AFTER_CLOSE;

            megaLogger.close();

            // write master log (generated by parent process)
            ofstream out(LOG_NAME);
            {
                ifstream in(logName);
                out << logName << ':' << endl;
                copyStream(out, in);
            }
            fs::remove(logName);

            // append individual logs (generated by sub-processes)
            for (TestProcess& test : processes)
            {
                {
                    ifstream in(test.logFilename);
                    out << "----------------------------------------------------------------------------------------------------" << endl;
                    out << test.logFilename << ':' << endl;
                    copyStream(out, in);
                }
                fs::remove(test.logFilename);
                fs::remove(test.outputFilename); // remove this here as well, after the process instance has been destroyed
            }
        }

        if (totalFailedTests > 0)
            return 1;

        if (!anyFailed)
            return 0;

        return 1;
    }
    catch (exception& e)
    {
        cerr << "running mulitple instances failed: " << e.what() << endl;
        cerr << "catch cwd: " << fs::current_path() << endl;
        return 1;
    }
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

void copyFileFromTestData(fs::path filename, fs::path destination)
{
    fs::path dir = getTestDataDir();
    fs::path source = dir / filename;
    if (fs::is_directory(destination))
        destination = destination / filename;
    if (fs::exists(destination))
        fs::remove(destination);
    fs::copy_file(source, destination);
}

fs::path getLinkExtractSrciptPath() {
    return executableDir / LINK_EXTRACT_SCRIPT;
}

bool isFileHidden(const LocalPath& path)
{
    return FileSystemAccess::isFileHidden(path);
}

bool isFileHidden(const fs::path& path)
{
    return isFileHidden(LocalPath::fromAbsolutePath(path.u8string()));
}

