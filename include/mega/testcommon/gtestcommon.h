#include <chrono>
#include <functional>
#include <queue>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "mega/process.h"

namespace mega
{

class ProcessWithInterceptedOutput
{
public:
    virtual ~ProcessWithInterceptedOutput() = default;
    bool run(const std::vector<std::string>& args, const std::unordered_map<std::string, std::string>& env);
    bool finishedRunning(); // false when not started or still running
    int getExitCode(); // 0 for success
    int getPid() const;

protected:
    virtual void clearBeforeRun() {} // override for member cleanup
    virtual void onOutLine(std::string&& line);
    virtual void onErrLine(std::string&& line);
    virtual void onExit() {}

private:
    void intercept(const unsigned char* data, size_t len, std::string& buffer, std::function<void(std::string&&)> onLine);

    std::unique_ptr<Process> mProc;
    std::string mOutBuffer;
    std::string mErrBuffer;
    bool mExitReported = false;
};


class GTestListProc : public ProcessWithInterceptedOutput
{
public:
    std::deque<std::string> getTestsToRun() const { return mTestsToRun; }
    size_t getTestSuiteCount() const { return mTestSuiteCount; }
    size_t getDisabledTestCount() const { return mDisabledTestCount; }

private:
    void clearBeforeRun() override { mTestsToRun.clear(); mTestSuiteCount = mDisabledTestCount = 0u; mCurrentSuite.clear(); }
    void onOutLine(std::string&& line) override;

    std::deque<std::string> mTestsToRun;
    size_t mTestSuiteCount = 0u;
    std::string mCurrentSuite;
    size_t mDisabledTestCount = 0u;
};


class GTestProc : public ProcessWithInterceptedOutput
{
public:
    bool run(const std::vector<std::string>& args, const std::unordered_map<std::string, std::string>& env, size_t workerIdx, std::string&& name);
    bool passed() const { return mStatus == TestStatus::TEST_PASSED; }
    bool crashed() const { return mStatus == TestStatus::CRASHED; }
    std::string getRelevantOutput() { return finishedRunning() ? mRelevantOutput : std::string(); }
    const std::string& getTestName() const { return mTestName; }

    void hideMemLeaks(bool hide) { mHideMemLeaks = hide; }

private:
    void clearBeforeRun() override { mRelevantOutput.clear(); mOutputIsRelevant = false; }
    void onOutLine(std::string&& line) override;
    void onErrLine(std::string&& line) override;
    void onExit() override;
    void printToScreen(std::ostream& screen, const std::string& msg) const;

    std::string mTestName;
    size_t mWorkerIdx = 0u;

    enum class TestStatus
    {
        NOT_STARTED,
        RUNNING,
        TEST_PASSED,
        TEST_FAILED,
        CRASHED,
    };
    TestStatus mStatus = TestStatus::NOT_STARTED;

    std::string mRelevantOutput;
    bool mOutputIsRelevant = false;

    bool mHideMemLeaks = false; // leave memory leaks in printouts or filter them out
    bool mIncomingMemLeaks = false;
};


class RuntimeArgValues
{
public:
    RuntimeArgValues(std::vector<std::string>&& args);

    bool isValid() const { return mRunMode != TestRunMode::INVALID; }
    bool isListOnly() const { return mRunMode == TestRunMode::LIST_ONLY; }
    bool isMainProcOnly() const { return mRunMode == TestRunMode::MAIN_PROCESS_ONLY; }
    bool isMainProcWithWorkers() const { return mRunMode == TestRunMode::MAIN_PROCESS_WITH_WORKERS; }
    bool isWorker() const { return mRunMode == TestRunMode::WORKER_PROCESS; }
    bool isHelp() const { return mRunMode == TestRunMode::HELP; }

    std::string getLog() const;
    size_t getInstanceCount() const { return mInstanceCount; }
    const std::string& getCustomApiUrl() const { return mApiUrl; }
    const std::string& getCustomUserAget() const { return mUserAgent; }
    std::vector<std::string> getArgsForWorker(const std::string& testToRun, size_t subprocIdx) const;
    std::unordered_map<std::string, std::string> getEnvVarsForWorker(size_t subprocIdx) const;
    std::string getExecutable() const { return mArgs.empty() ? std::string() : mArgs[0]; }
    std::string getFilter() const { return mGtestFilterIdx < mArgs.size() ? mArgs[mGtestFilterIdx] : std::string(); }

    static void setAccountsPerInstance(size_t count) { emailsPerInstance = count; }
    static size_t getAccountsPerInstance() { return emailsPerInstance; }

    bool hidingWorkerMemLeaks() const { return mHideWorkerMemLeaks; }

private:
    std::tuple<std::string, size_t, size_t, std::string> breakTemplate() const;

    std::vector<std::string> mArgs; // filled only in main process
    size_t mInstanceCount = 0u;
    size_t mCurrentInstance = SIZE_MAX;
    std::string mTestName;
    std::string mApiUrl;
    std::string mUserAgent;
    std::string mEmailTemplate; // "foo+bar-{1-100}@mega.co.nz"
    size_t mGtestFilterIdx = SIZE_MAX; // avoid a search
    bool mHideWorkerMemLeaks = false;

    enum class TestRunMode
    {
        INVALID,
        LIST_ONLY,
        MAIN_PROCESS_ONLY,
        MAIN_PROCESS_WITH_WORKERS, // pass --INSTANCES and use an email template
        WORKER_PROCESS, // spawned by the main process, ran with --INSTANCE
        HELP, // show Help only
    };

    TestRunMode mRunMode = TestRunMode::INVALID;

    static inline size_t emailsPerInstance = 3u; // default value at the time of writing this code
    static constexpr size_t maxWorkerCount = 256u; // reasonable limit used for validation only, not really a constraint
};


class GTestParallelRunner
{
public:
    GTestParallelRunner(RuntimeArgValues&& commonArgs) : mCommonArgs(std::move(commonArgs)) {}

    int run();

private:
    bool findTests();
    size_t getNexatAvailableInstance();
    bool runTest(size_t workerIdx, std::string&& name);
    void processFinishedTest(GTestProc& test, const std::string& logFile);
    void summary();

    RuntimeArgValues mCommonArgs;
    std::deque<std::string> mTestsToRun;
    std::map<size_t, GTestProc> mRunningGTests;
    int mFinalResult = 0;

    // summary
    std::chrono::time_point<std::chrono::system_clock> mStartTime;
    size_t mTestSuiteCount = 0u;
    size_t mTotalTestCount = 0u;
    size_t mPassedTestCount = 0u;
    std::vector<std::string> mFailedTests;
    size_t mDisabledTestCount = 0u;
    std::vector<int> mPidDumps;
};


std::string getLogFileName(size_t useIdx = SIZE_MAX, const std::string& useDescription = std::string());

std::string getCurrentTimestamp(bool includeDate = false);

} // namespace mega
