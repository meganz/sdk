#include "mega/testcommon/gtestcommon.h"

#include <fstream>
#include <regex>

using namespace std;

namespace mega
{

/// class ProcessWithInterceptedOutput
///
/// run a process and intercept its stdout and stderr before simply outputting them to console line-by-line

bool ProcessWithInterceptedOutput::run(const vector<string>& args, const unordered_map<string, string>& env)
{
    // only run if not already running or if it finished
    assert(!mProc || mProc->getPid() == -1 || mProc->hasStatus());
    if (mProc && mProc->getPid() != -1 && !mProc->hasStatus())
    {
        return false;
    }

    // clean-up for previous run
    mProc = make_unique<Process>();
    mOutBuffer.clear();
    mErrBuffer.clear();
    mExitReported = false;
    clearBeforeRun(); // other clean-up for derived classes

    Process::DataReaderFunc interceptOut = [this](const unsigned char* data, size_t len)
    {
        intercept(data, len, mOutBuffer,
                  [this](string&& line) { onOutLine(std::move(line)); });
    };

    Process::DataReaderFunc interceptErr = [this](const unsigned char* data, size_t len)
    {
        intercept(data, len, mErrBuffer,
                  [this](string&& line) { onErrLine(std::move(line)); });
    };

    return mProc->run(args, env, interceptOut, interceptErr);
}

bool ProcessWithInterceptedOutput::finishedRunning()
{
    if (!mProc || mProc->getPid() == -1)
    {
        return false;
    }

    // "flushing" child process is mandatory; otherwise it might never report having exited;
    // should probably be part of Process::isAlive()
    mProc->poll();
    mProc->flush();

    return !mProc->isAlive();
}

int ProcessWithInterceptedOutput::getExitCode()
{
    if (!mProc || mProc->getPid() == -1)
    {
        return -1;
    }

    if (!mProc->hasStatus())
    {
        mProc->poll();
        mProc->flush();
        mProc->wait(); // not relevant if it did not start or failed or succeeded
    }

    // dump any remaining output
    if (!mOutBuffer.empty())
    {
        onOutLine(std::move(mOutBuffer));
        mOutBuffer.clear();
    }
    if (!mErrBuffer.empty())
    {
        onErrLine(std::move(mErrBuffer));
        mErrBuffer.clear();
    }

    // react to the way it exited
    if (!mExitReported)
    {
        mExitReported = true;
        onExit();
    }
    return mProc->hasExited() ? mProc->getExitCode() : mProc->getTerminatingSignal();
}

void ProcessWithInterceptedOutput::onOutLine(string&& line)
{
    std::cout << line << std::endl;
}
void ProcessWithInterceptedOutput::onErrLine(string&& line)
{
    std::cerr << line << std::endl;
}

void ProcessWithInterceptedOutput::intercept(const unsigned char* data, size_t len, std::string& buffer, std::function<void(std::string&&)> onLine)
{
    if (!data || !len)
    {
        return;
    }

    buffer.append(reinterpret_cast<const char*>(data), len);

    for (string::size_type lineStart = 0, lineEnd = buffer.find('\n', lineStart); ;
         lineStart = lineEnd + 1, lineEnd = buffer.find('\n', lineStart))
    {
        // process only lines ending in separator
        if (lineEnd == string::npos)
        {
            // leave the rest of the line in the buffer, just in case it doesn't finish with '\n'
            buffer.erase(0, lineStart);
            break;
        }

        string line = buffer.substr(lineStart, lineEnd - lineStart);
        if (!line.empty() && line.back() == '\r') // Windows: \r\n
        {
            line.pop_back();
        }

        onLine(std::move(line));
    }
}


/// class GTestListProc
///
/// extend ProcessWithInterceptedOutput to build a list of GTest-s (a.k.a. googletests) to run

void GTestListProc::onOutLine(string&& line)
{
    // react to lines like:
    //
    // SuiteFoo.
    //   TestBar
    //   DISABLED_TestBazz

    if (Utils::startswith(line, '[')) return; // skip lines with other info

    // test suite
    if (!Utils::startswith(line, ' '))
    {
        // name of test suite
        if (!line.empty() && !isalpha(line.front()))
        {
            std::cerr << "ERROR: Test suite name was invalid: " << line << std::endl;
            return;
        }
        mCurrentSuite = line;
        ++mTestSuiteCount;
        return;
    }

    if (mCurrentSuite.empty())
    {
        std::cerr << "ERROR: Test suite name should have been present until now" << std::endl;
        return;
    }

    string testCase = Utils::trim(line);

    // count of disabled tests
    if (Utils::startswith(testCase, "DISABLED_"))
    {
        ++mDisabledTestCount;
        return;
    }

    mTestsToRun.push_back(mCurrentSuite + testCase);
}


/// class GTestProc
///
/// extend ProcessWithInterceptedOutput to interpret the output of a GTest (a.k.a. googletest)

bool GTestProc::run(const vector<string>& args, const unordered_map<string, string>& env, size_t workerIdx, string&& name)
{
    mWorkerIdx = workerIdx;
    mTestName = std::move(name);
    mStatus = TestStatus::NOT_STARTED;

    if (ProcessWithInterceptedOutput::run(args, env))
    {
        mStatus = TestStatus::RUNNING;
        return true;
    }

    return false;
}

void GTestProc::onOutLine(std::string&& line)
{
    // show lines between
    // [ RUN      ]
    // and
    // [       OK ] or [  FAILED  ]

    // completely ignore some lines as it's supposed to run only a single test
    if (line.empty() ||
        line.find("[LOGGER] ========== Application startup ===========") != string::npos ||
        Utils::startswith(line, "[========]") ||
        Utils::startswith(line, "Note: Google Test filter = ") ||
        Utils::startswith(line, "[----------]") ||
        Utils::startswith(line, "[==========]") ||
        Utils::startswith(line, "[  PASSED  ]") ||
        (Utils::startswith(line, "[  FAILED  ]") && !mOutputIsRelevant) ||
        line == " 1 FAILED TEST")
    {
        return;
    }

    printToScreen(std::cout, line);

    if (Utils::startswith(line, "[ RUN      ]"))
    {
        mOutputIsRelevant = true;
        mRelevantOutput += line + '\n';
    }

    else if (mOutputIsRelevant)
    {
        mRelevantOutput += line + '\n';

        if (Utils::startswith(line, "[       OK ]"))
        {
            mStatus = TestStatus::TEST_PASSED;
            mOutputIsRelevant = false;
        }
        else if (Utils::startswith(line, "[  FAILED  ]"))
        {
            mStatus = TestStatus::TEST_FAILED;
            mOutputIsRelevant = false;
        }
    }
}

void GTestProc::onErrLine(std::string&& line)
{
    if (Utils::startswith(line, "================") ||
        line.find("W: rops_pt_init_destroy_netlink: netlink bind failed") != string::npos) // skip annoying but harmless LWS warning
    {
        return;
    }

    if (mSkipUnwantedTestOutput)
    {
        // attempt to hide [false-positive] memory leaks, as they make the output unusable
        if (line.find("==ERROR: LeakSanitizer: detected memory leaks") != string::npos)
        {
            mPrintingMemLeaks = true;
            return;
        }

        if (Utils::startswith(line, "SUMMARY: AddressSanitizer:"))
        {
            mPrintingMemLeaks = false;
            return;
        }

        if (mPrintingMemLeaks || line.empty())
        {
            return;
        }
    }

    mRelevantOutput += line + '\n';
    printToScreen(std::cerr, line);
}

void GTestProc::onExit()
{
    if (mStatus != TestStatus::RUNNING)
    {
        // test reported when it finished, as it should have, no need to augument the log for possible crashes
        return;
    }

    mStatus = TestStatus::CRASHED;

    string msg("[  FAILED  ] " + mTestName + " CRASHED");
    printToScreen(std::cout, msg);

    ofstream testLog(getLogFileName(mWorkerIdx, mTestName), ios_base::app);
    if (testLog.is_open())
    {
        testLog << mRelevantOutput << msg << std::endl;
    }
    else
    {
        printToScreen(std::cout, "Could not open " + getLogFileName(mWorkerIdx, mTestName) + " to append relevant output after crash.");
    }
}

void GTestProc::printToScreen(std::ostream& screen, const std::string& msg) const
{
    screen << getCurrentTimestamp(true) << " #" << mWorkerIdx << ' ' << msg << std::endl;
}


/// class RuntimeArgValues
///
/// parse and normalize runtime arguments

RuntimeArgValues::RuntimeArgValues(vector<string>&& args)
{
    for (auto it = args.begin(); it != args.end();)
    {
        string arg = Utils::toUpperUtf8(*it);

        if (Utils::startswith(arg, "--EMAIL-POOL:"))
        {
            mEmailTemplate = it->substr(13); // keep original string, not in CAPS
            it = args.erase(it); // not passed to subprocesses
            continue;
        }

        else if (Utils::startswith(arg, "--INSTANCES:"))
        {
            assert(mRunMode == TestRunMode::INVALID);

            mInstanceCount = atoi(arg.substr(12).c_str()); // valid interval: [0, maxWorkerCount]
            if (mInstanceCount > maxWorkerCount || (!mInstanceCount && arg != "--INSTANCES:0"))
            {
                std::cerr << "Invalid runtime parameter: " << *it << "\nMaximum allowed value: " << maxWorkerCount << std::endl;
                return; // leave current instance as invalid
            }

            mRunMode = mInstanceCount ? TestRunMode::MAIN_PROCESS_WITH_WORKERS : TestRunMode::MAIN_PROCESS_ONLY;
            it = args.erase(it); // not passed to subprocesses
            continue;
        }

        else if (Utils::startswith(arg, "--INSTANCE:"))  // used only internally by subprocesses
        {
            assert(mRunMode == TestRunMode::INVALID);

            mCurrentInstance = atoi(arg.substr(11).c_str()); // valid interval: [0, maxWorkerCount)
            if (mCurrentInstance >= maxWorkerCount || (!mCurrentInstance && arg != "--INSTANCE:0"))
            {
                std::cerr << "Invalid runtime parameter: " << *it << std::endl;
                return; // leave current instance as invalid
            }
            mRunMode = TestRunMode::WORKER_PROCESS;
        }

        else if (Utils::startswith(arg, "--APIURL:"))
        {
            mApiUrl = it->substr(9); // keep original string, not in CAPS
            if (!mApiUrl.empty() && mApiUrl.back() != '/')
                mApiUrl += '/';
        }

        else if (Utils::startswith(arg, "--USERAGENT:"))
        {
            mUserAgent = it->substr(12); // keep original string, not in CAPS
        }

        else if (Utils::startswith(arg, "--GTEST_FILTER="))
        {
            mGtestFilterIdx = it - args.begin();
        }

        else if (arg == "--GTEST_LIST_TESTS")
        {
            assert(mRunMode == TestRunMode::INVALID);

            mRunMode = TestRunMode::LIST_ONLY;
            return;
        }

        ++it;
    }

    // finish set up for main process
    if (isMainProcWithWorkers())
    {
        if (mEmailTemplate.empty())
        {
            const char* teplt = getenv("MEGA_PWD0");
            if (!teplt)
            {
                std::cerr << "Missing both --EMAIL-POOL runtime parameter and MEGA_PWD0 env var" << std::endl;
                mRunMode = TestRunMode::INVALID;
                return;
            }
            mEmailTemplate = teplt;
        }

        // if it received --INSTANCES but not an email template, then it will run tests in a single worker process
        if (mEmailTemplate.find('{') == string::npos)
        {
            mEmailTemplate.clear();
            mInstanceCount = 1u;
        }

        // save args that will be passed to subprocesses
        mArgs = std::move(args);
    }

    // finish set up for worker process
    else if (isWorker())
    {
        if (mGtestFilterIdx == SIZE_MAX)
        {
            std::cerr << "Missing --gtest_filter runtime parameter for instance " << mCurrentInstance << std::endl;
            mRunMode = TestRunMode::INVALID;
            return;
        }

        mTestName = args[mGtestFilterIdx].substr(15);
    }

    else
    {
        mRunMode = TestRunMode::MAIN_PROCESS_ONLY;
    }
}

vector<string> RuntimeArgValues::getArgsForWorker(const string& test, size_t spIdx) const
{
    assert(isMainProcWithWorkers());
    if (!isMainProcWithWorkers()) return {};

    if (test.empty()) return mArgs; // remove?

    auto args = mArgs;

    string gtestFilter = "--gtest_filter=" + test;
    if (mGtestFilterIdx < args.size())
    {
        args[mGtestFilterIdx] = std::move(gtestFilter);
    }
    else
    {
        args.emplace_back(std::move(gtestFilter));
    }

    args.emplace_back("--INSTANCE:" + std::to_string(spIdx));

    return args;
}

unordered_map<string, string> RuntimeArgValues::getEnvVarsForWorker(size_t idx) const
{
    assert(isMainProcWithWorkers());
    // when it did not receive an email template don't overwrite env vars
    if (mEmailTemplate.empty() || !isMainProcWithWorkers()) return {};

    tuple<string, size_t, size_t, string> templateValues = breakTemplate();
    size_t first = std::get<1>(templateValues) + emailsPerInstance * idx;
    size_t last = first + emailsPerInstance - 1u;
    if (last > std::get<2>(templateValues)) return {};

    unordered_map<string, string> envVars(emailsPerInstance);
    for (size_t i = 0; i < emailsPerInstance; ++i)
    {
        static const char* pswd = nullptr;
        if (!i)
        {
            pswd = getenv("MEGA_PWD0");
            if (!pswd) return envVars;
        }
        else
        {
            envVars["MEGA_PWD" + std::to_string(i)] = pswd;
        }

        envVars["MEGA_EMAIL" + std::to_string(i)] = std::get<0>(templateValues) + std::to_string(first + i) + std::get<3>(templateValues);
    }

    return envVars;
}

tuple<string, size_t, size_t, string> RuntimeArgValues::breakTemplate() const
{
    static regex emailRegex("(.*)[{](\\d+)-(\\d+)[}](.*)"); // "(prefix){(first)-(last)}(suffix)"
    smatch matches;
    if (regex_search(mEmailTemplate, matches, emailRegex) && matches.size() >= 5)
    {
        size_t first = atoi(matches[2].str().c_str()); // only digits, e.g. 1
        size_t last = atoi(matches[3].str().c_str());  // only digits, e.g. 100

        if (first > 0u && last >= first)
        {
            return tuple<string, size_t, size_t, string>(matches[1].str(), first, last, matches[4].str());
        }
    }

    return tuple<string, size_t, size_t, string>();
}

string RuntimeArgValues::getLog() const
{
    return getLogFileName(mCurrentInstance, mTestName);
}


std::string getLogFileName(size_t useIdx, const std::string& useDescription)
{
    static string defaultName("test.log");

    return useDescription.empty() ? defaultName :
           Utils::replace(defaultName, ".", '.' + std::to_string(useIdx) + '.' + useDescription + '.');
}

std::string getCurrentTimestamp(bool includeDate)
{
    using std::chrono::system_clock;
    auto currentTime = system_clock::now();
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

} // namespace mega
