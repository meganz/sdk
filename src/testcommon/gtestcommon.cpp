#include "mega/testcommon/gtestcommon.h"

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
