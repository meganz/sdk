#include "gtest_common.h"

#include <fstream>
#include <numeric>
#include <regex>

using namespace std;

const string& getDefaultLogName();  // implement this separately for SDK tests, MEGAchat tests etc.


namespace mega
{


/// class ProcessWithInterceptedOutput
///

bool ProcessWithInterceptedOutput::run(const vector<string>& args, const unordered_map<string, string>& env)
{
    // only run if not already running or if it finished
    assert(!mProc || mProc->getPid() == -1 || mProc->hasStatus());
    if (mProc && mProc->getPid() != -1 && !mProc->hasStatus())
    {
        return false;
    }

    // clean-up for previous run
    mProc = std::make_unique<Process>();
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

int ProcessWithInterceptedOutput::getPid() const
{
    return mProc ? mProc->getPid() : -1;
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

    printToScreen(std::cout, "Failed to run " + mTestName);
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

    if (mHideMemLeaks)
    {
        // attempt to hide [false-positive] memory leaks, as they make the output unusable
        if (line.find("==ERROR: LeakSanitizer: detected memory leaks") != string::npos)
        {
            mIncomingMemLeaks = true;
            return;
        }

        if (Utils::startswith(line, "SUMMARY: AddressSanitizer:"))
        {
            mIncomingMemLeaks = false;
            return;
        }

        if (mIncomingMemLeaks || line.empty())
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

    const string& workerLog = getWorkerLog();
    ofstream testLog(workerLog, ios_base::app);
    if (testLog.is_open())
    {
        testLog << mRelevantOutput << msg << std::endl;
    }
    else
    {
        printToScreen(std::cout, "Could not open " + workerLog + " to append relevant output after crash.");
    }
}

void GTestProc::printToScreen(std::ostream& screen, const std::string& msg) const
{
    screen << getCurrentTimestamp(true) << " #" << mWorkerIdx << ' ' << msg << std::endl;
}

string GTestProc::getWorkerLog() const
{
    const string& log = (mStatus == TestStatus::NOT_STARTED || mCustomPathForPid.empty())
                        ? getLogFileName(mWorkerIdx, mTestName)
                        : mCustomPathForPid + std::to_string(getPid()) + '/' + getLogFileName(mWorkerIdx, mTestName);
    return log;
}


/// class RuntimeArgValues
///

RuntimeArgValues::RuntimeArgValues(vector<string>&& args, vector<pair<string, string>>&& accEnvVars)
{
    if (accEnvVars.empty())
    {
        assert(!accEnvVars.empty());
        return;
    }

    mAccEnvVars.swap(accEnvVars);
    string emailTemplate;

    for (auto it = args.begin(); it != args.end();)
    {
        if (Utils::startswith(*it, "--#"))
        {
            // commented out args, e.g. --#INSTANCES:3
            it = args.erase(it);
            continue;
        }

        string arg = Utils::toUpperUtf8(*it);

        if (arg == "--HELP")
        {
            mRunMode = TestRunMode::HELP;
            return;
        }

        if (Utils::startswith(arg, "--EMAIL-POOL:"))
        {
            emailTemplate = it->substr(13); // keep original string, not in CAPS
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
                mRunMode = TestRunMode::INVALID;
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
                mRunMode = TestRunMode::INVALID;
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

        else if (arg == "--HIDE_WORKER_MEM_LEAKS")
        {
            mHideWorkerMemLeaks = true;
            it = args.erase(it); // not passed to subprocesses
            continue;
        }

        ++it;
    }

    if (!validateRequirements(emailTemplate))
    {
        mRunMode = TestRunMode::INVALID;
        return;
    }

    if (isWorker())
    {
        mTestName = args[mGtestFilterIdx].substr(15);
    }
    else if (!isMainProcWithWorkers())
    {
        mRunMode = TestRunMode::MAIN_PROCESS_ONLY;
    }

    mArgs = std::move(args);
}

bool RuntimeArgValues::validateRequirements(const string& emailTemplate)
{
    // Env var for first password must always be set
    if (!Utils::hasenv(mAccEnvVars[0].second))
    {
        std::cerr << "Missing required $" << mAccEnvVars[0].second << " env var" << std::endl;
        return false;
    }

    // Break email template, if any
    bool hasTemplate = false;

    if (!isWorker())
    {
        if (!emailTemplate.empty())
        {
            hasTemplate = breakTemplate(emailTemplate);
            if (!hasTemplate)
            {
                std::cerr << "Invalid runtime parameter --EMAIL-POOL:" << emailTemplate << "\nMust be a template like foo+bar-{1-15}@mega.co.nz" << std::endl;
                return false;
            }
        }
        else
        {
            const string& teplt = Utils::getenv(mAccEnvVars[0].first, "");
            if (teplt.empty())
            {
                std::cerr << "Missing both $" << mAccEnvVars[0].first << " env var and --EMAIL-POOL runtime parameter" << std::endl;
                return false;
            }
            hasTemplate = breakTemplate(teplt);
        }
    }

    if (hasTemplate)
    {
        size_t instanceCountFromTemplate = (std::get<2>(mEmailTemplateValues) - std::get<1>(mEmailTemplateValues) + 1) / getAccountsPerInstance();
        if (!instanceCountFromTemplate)
        {
            const string& t = emailTemplate.empty() ? Utils::getenv(mAccEnvVars[0].first, "") : emailTemplate;
            std::cerr << "Invalid email template " << t << ", 0 instances allowed" << std::endl;
            return false;
        }
        if (mInstanceCount && instanceCountFromTemplate < mInstanceCount)
        {
            std::cerr << "WARNING: Not enough accounts in email template (" << instanceCountFromTemplate << ") for requested instances (" << mInstanceCount << ")."
                      << "\nRunning with maximum " << instanceCountFromTemplate << " instances instead.\n" << std::endl;
            mInstanceCount = instanceCountFromTemplate;
        }
        return true;
    }

    if (isWorker() && mGtestFilterIdx == SIZE_MAX)
    {
        std::cerr << "Missing --gtest_filter runtime parameter for instance " << mCurrentInstance << std::endl;
        return false;
    }

    if (isMainProcWithWorkers() && mInstanceCount > 1u)
    {
        // if it received --INSTANCES but not an email template, then it will run tests in a single worker process
        std::cerr << "WARNING: No email template found to run " << mInstanceCount << " instances,\n";
        std::cerr << "Continuing with sequential run in 1 separate instance instead." << std::endl;

        mInstanceCount = 1u;
    }

    // Validate the rest of env vars
    for (size_t i = 1; i < getAccountsPerInstance(); ++ i)
    {
        const auto& a = mAccEnvVars[i];
        if (!Utils::hasenv(a.first) || !Utils::hasenv(a.second))
        {
            std::cerr << "Both $" << a.first << " and $" << a.second << " env vars must be defined" << std::endl;
            return false;
        }
    }

    return true;
}

void RuntimeArgValues::printHelp() const
{
    string firstAccDescr = mAccEnvVars.empty() ? "env var of the first account" : ('$' + mAccEnvVars[0].first + " env var");
    static const string patternExample{ "foo+bar-{1-15}@mega.co.nz" };

    // runtime options
    cout << "Options are case insensitive.\n";
    cout << buildAlignedHelpString("--INSTANCES:<n>",         {"Run n tests in parallel, each in its own process. In order to achieve that, an email pattern",
                                                               "will be required and will be taken from --EMAIL-POOL argument or " + firstAccDescr + '.',
                                                               "If no email pattern was found, it will behave as if n==1, thus run with a single worker",
                                                               "process and use credentials from the same env vars required by running without this arg."}) << '\n';
    cout << buildAlignedHelpString("--EMAIL-POOL:<pattern>",  {"Email address pattern used to extract the required test accounts. Must be of the form",
                                                               patternExample}) << '\n';
    cout << buildAlignedHelpString("--APIURL:<url>",          {"Custom base URL to use for contacting the server; overwrites default url."}) << '\n';
    cout << buildAlignedHelpString("--USERAGENT:<uag>",       {"Custom HTTP User-Agent"}) << '\n';
    cout << buildAlignedHelpString("--#<arg>",                {"Commented out argument, ignored"}) << '\n';
    cout << buildAlignedHelpString("--GTEST_FILTER=<filter>", {"Set tests to execute; can be ':'-separated list, with * or other wildcards",
                                                               "e.g. --GTEST_FILTER=SuiteFoo.TestBar:*TestBazz"}) << '\n';
    cout << buildAlignedHelpString("--GTEST_LIST_TESTS",      {"List tests compiled with this executable; consider --GTEST_FILTER if received."}) << '\n';
    cout << buildAlignedHelpString("--HIDE_WORKER_MEM_LEAKS", {"Hide memory leaks printed by debugger while running with --INSTANCES."}) << '\n';
    printCustomOptions();

    // env vars
    cout << '\n';
    cout << "Environment variables:\n";
    for (size_t i = 0u; i < getAccountsPerInstance(); ++i)
    {
        string numeralStr = std::to_string(i);
        switch ((i + 1) % 10)
        {
        case 1: numeralStr += "st"; break;
        case 2: numeralStr += "nd"; break;
        case 3: numeralStr += "rd"; break;
        default: numeralStr += "th"; break;
        }

        const string& accVarName = mAccEnvVars[i].first;
        string defaultAccDescr = "Email address for " + numeralStr + " MEGA account";
        const vector<string>& accVarDescr = (i > 0) ?
            vector<string>{defaultAccDescr} :
            vector<string>{"[required or pass --EMAIL-POOL:<pattern>] " + defaultAccDescr + ", or pattern; can be",
                           "overwritten by the command line argument. When running concurrently using --instances, it must contain",
                           "{min - max}, e.g: " + patternExample + " to set all MEGA account email addresses"};
        cout << buildAlignedHelpString("  $" + accVarName, accVarDescr) << '\n';

        const string& pwdVarName = mAccEnvVars[i].second;
        string defaultPwdDescr = "Passsword for " + numeralStr + " MEGA account,";
        const vector<string>& pwdVarDescr = (i > 0) ?
            vector<string>{defaultPwdDescr + " defaults to the password of the first mega account when not set"} :
            vector<string>{"[required] " + defaultPwdDescr + " becomes the default for unset passwords of other accounts"};
        cout << buildAlignedHelpString("  $" + pwdVarName, pwdVarDescr) << '\n';
    }
    printCustomEnvVars();
}

string RuntimeArgValues::buildAlignedHelpString(const string& var, const std::vector<string>& descr)
{
    static size_t colW = 28u; // 28 characters from the start of row until the description
    size_t spacesAfterVar = var.size() >= colW ? 1 : colW - var.size();
    string str = std::accumulate(descr.begin(), descr.end(), string{},
        [](const string& a, const string& b)
        {
            return a + (a.size() ? '\n' + string(colW, ' ') : "") + b;
        });
    return var + string(spacesAfterVar, ' ') + str;
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
    assert(isMainProcWithWorkers() || isMainProcOnly());
    // when it did not receive an email template don't overwrite env vars
    if (!usingTemplate() || (!isMainProcWithWorkers() && !isMainProcOnly())) return {};

    size_t first = std::get<1>(mEmailTemplateValues) + getAccountsPerInstance() * idx;
    size_t last = first + getAccountsPerInstance() - 1u;
    if (last > std::get<2>(mEmailTemplateValues)) return {};

    unordered_map<string, string> envVars(getAccountsPerInstance());
    for (size_t i = 0; i < getAccountsPerInstance(); ++i)
    {
        envVars[mAccEnvVars[i].first] = std::get<0>(mEmailTemplateValues) + std::to_string(first + i) + std::get<3>(mEmailTemplateValues);

        // the first password will be duplicated for the rest of accounts
        static const string pswd{ Utils::getenv(mAccEnvVars[0].second, "")};

        if (!i && pswd.empty())
        {
            return envVars;
        }
        else
        {
            envVars[mAccEnvVars[i].second] = pswd;
        }
    }

    return envVars;
}

bool RuntimeArgValues::breakTemplate(const std::string& tplt)
{
    // Supported templates:
    //   "(prefix){(first)-(last)}(suffix)"
    //   "(prefix){(first)..(last)}(suffix)"
    static regex emailRegex("(.*)[{](\\d+)(-|\\.\\.)(\\d+)[}](.*)");
    smatch matches;
    if (regex_search(tplt, matches, emailRegex) && matches.size() >= 6)
    {
        size_t first = atoi(matches[2].str().c_str()); // only digits, e.g. 1
        size_t last = atoi(matches[4].str().c_str());  // only digits, e.g. 100

        if (first > 0u && last > first && !matches[1].str().empty() && !matches[5].str().empty())
        {
            mEmailTemplateValues = std::make_tuple(matches[1].str(), first, last, matches[5].str());
            return true;
        }
    }

    return false;
}

bool RuntimeArgValues::usingTemplate() const
{
    return std::get<2>(mEmailTemplateValues);
}

string RuntimeArgValues::getLog() const
{
    return getLogFileName(mCurrentInstance, mTestName);
}


/// class GTestParallelRunner
///

int GTestParallelRunner::run()
{
    mFinalResult = 0;
    mPassedTestCount = 0u;
    mFailedTests.clear();
    mPidDumps.clear();
    mStartTime = chrono::system_clock::now();

    assert(mCommonArgs.isMainProcWithWorkers());
    if (!mCommonArgs.isMainProcWithWorkers() || !findTests())
    {
        return 1;
    }

    std::cout << "[==========] Running " << mTestsToRun.size() << " tests from " << mTestSuiteCount << " test suites." << std::endl;

    // assign 1 test to 1 subprocess
    size_t procIdx = SIZE_MAX;
    while (!mTestsToRun.empty())
    {
        if (procIdx == SIZE_MAX)
        {
            for (procIdx = getNexatAvailableInstance(); procIdx == SIZE_MAX; procIdx = getNexatAvailableInstance())
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(500)); // not for too long, to collect output
            }
        }

        string testName(std::move(mTestsToRun.front()));
        mTestsToRun.pop_front();

        if (runTest(procIdx, std::move(testName)))
        {
            procIdx = SIZE_MAX; // get new value in next loop
        }
    }

    // wait for remaining tests to finish
    vector<size_t> stillRunning(mRunningGTests.size());
    std::iota(stillRunning.begin(), stillRunning.end(), 0); // fill it with 0,1,...

    while (!stillRunning.empty())
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100)); // collect output from running workers

        for (auto it = stillRunning.begin(); it != stillRunning.end();)
        {
            GTestProc& t = mRunningGTests[*it];
            if (t.finishedRunning())
            {
                processFinishedTest(t);
                it = stillRunning.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    summary();

    return mFinalResult;
}

bool GTestParallelRunner::findTests()
{
    vector<string> args = { mCommonArgs.getExecutable(), "--gtest_list_tests", mCommonArgs.getFilter(), "--gtest_print_time=0", "--no-log-cout" };
    GTestListProc proc;
    if (!proc.run(args, {}) || proc.getExitCode())
    {
        std::cerr << mCommonArgs.getExecutable() << " --gtest_list_tests " << mCommonArgs.getFilter() << " failed" << std::endl;
        return false;
    }

    mTestsToRun = proc.getTestsToRun();
    mTestSuiteCount = proc.getTestSuiteCount();
    mTotalTestCount = mTestsToRun.size();
    mDisabledTestCount = proc.getDisabledTestCount();

    if (!mTotalTestCount)
    {
        std::cerr << mCommonArgs.getExecutable() << " --gtest_list_tests " << mCommonArgs.getFilter() << " found 0 tests to run" << std::endl;
        summary();
        return false;
    }

    return true;
}

size_t GTestParallelRunner::getNexatAvailableInstance()
{
    if (mRunningGTests.size() < mCommonArgs.getInstanceCount())
    {
        return mRunningGTests.size();
    }

    for (auto& i : mRunningGTests)
    {
        GTestProc& testProcess = i.second;
        if (testProcess.finishedRunning())
        {
            processFinishedTest(testProcess);

            return i.first;
        }
    }

    return SIZE_MAX; // invalid
}

bool GTestParallelRunner::runTest(size_t workerIdx, string&& name)
{
    auto procArgs = mCommonArgs.getArgsForWorker(name, workerIdx);
    auto envVars = mCommonArgs.getEnvVarsForWorker(workerIdx);

    GTestProc& testProcess = mRunningGTests[workerIdx];
    testProcess.setCustomPathForPid(mWorkerOutPath);
    testProcess.hideMemLeaks(mCommonArgs.hidingWorkerMemLeaks());
    bool running = testProcess.run(procArgs, envVars, workerIdx, std::move(name));

    return running;
}

void GTestParallelRunner::processFinishedTest(GTestProc& test)
{
    int err = test.getExitCode(); // waits if not finished yet

    if (!err || test.passed())
    {
        // Unfortunately the underlying process can return 1 even if it actually ran fine.
        // That will happen when running under debugger and reporting memory leaks (LeakSanitizer
        // reports false-positive mem leaks when secondary processes are forked during execution).
        ++mPassedTestCount;
    }
    else
    {
        mFailedTests.push_back(test.getTestName());
        if (test.crashed())
        {
            mPidDumps.push_back(test.getPid());
        }
        mFinalResult = 1;
    }

    // concatenate individual log to main log
    const string& individualLog = test.getWorkerLog();
    ifstream infile(individualLog);
    if (!infile.is_open())
    {
        std::cout << "Could not open " << individualLog << " for reading" << std::endl;
        return;
    }
    const string& mainLog = getLogFileName();
    ofstream outfile(mainLog, ios_base::app);
    if (!outfile.is_open())
    {
        std::cout << "Could not open " << mainLog << " for writing" << std::endl;
        return;
    }
    outfile << infile.rdbuf();
}

void GTestParallelRunner::summary()
{
    /*
     * Example:
     *
     * [==========] 26 tests from 2 test suites ran. (5820142 ms total)
     * [  PASSED  ] 24 tests.
     * [  FAILED  ] 2 tests, listed below:
     * [  FAILED  ] SuiteFoo.TestBar
     * [  FAILED  ] SuiteBazz.TestFred
     *
     *  2 FAILED TESTS
     *
     *   YOU HAVE 3 DISABLED TESTS
     */

    using namespace std::chrono;
    auto timeSpent = duration_cast<milliseconds>(system_clock::now() - mStartTime).count();
    std::cout << "[==========] " << (mPassedTestCount + mFailedTests.size()) << " tests from "
              << mTestSuiteCount << " test suites ran. (" << timeSpent << " ms total)\n"
              << "[  PASSED  ] " << mPassedTestCount << " tests.\n";

    if (!mFailedTests.empty())
    {
        std::cout << "[  FAILED  ] " << mFailedTests.size() << " tests, listed below:\n";
        for (const auto& t : mFailedTests)
        {
            std::cout << "[  FAILED  ] " << t << '\n';
        }
        std::cout << "\n " << mFailedTests.size() << " FAILED TESTS\n";
    }

    if (mDisabledTestCount)
    {
        std::cout << '\n';
        std::cout << "  YOU HAVE " << mDisabledTestCount << " DISABLED TESTS\n";
    }

    if (!mPidDumps.empty())
    {
        std::cout << '\n';
        for_each(mPidDumps.begin(), mPidDumps.end(), [](int p) { std::cout << "<< PROCESS SIGNALED >> (PID:" << p << ")\n"; });
    }

    std::cout << std::endl;
}


std::string getLogFileName(size_t useIdx, const std::string& useDescription)
{
    return useDescription.empty() ? getDefaultLogName() :
           Utils::replace(getDefaultLogName(), ".", '.' + std::to_string(useIdx) + '.' + useDescription + '.');
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
