#include "mega/gfx/isolatedprocess.h"
#include "mega/filesystem.h"
#include "mega/gfx/worker/client.h"
#include "mega/logging.h"
#include "mega/process.h"

#include <algorithm>
#include <iterator>
#include <mutex>
#include <ratio>
#include <string>
#include <system_error>
#include <thread>
#include <vector>
#include <chrono>

using mega::gfx::GfxClient;
using std::chrono::milliseconds;
using std::chrono::seconds;
using std::chrono::duration_cast;
using std::chrono::time_point;
using std::chrono::steady_clock;

namespace mega {

const milliseconds AutoStartLauncher::MAX_BACKOFF(15 * 1000);
const milliseconds AutoStartLauncher::START_BACKOFF(100);
const unsigned int GfxIsolatedProcess::MIN_ALIVE_SECONDS = 3;

void GfxWorkerHelloBeater::beat()
{
    auto gfxclient = GfxClient::create(mPipename);
    auto intervalMs = duration_cast<milliseconds>(mPeriod);
    while(!mShuttingDown)
    {
        bool isCancelled = mSleeper.sleep(intervalMs);
        if (!isCancelled)
        {
            gfxclient.runHello("beat");
        }
    }
}

void GfxWorkerHelloBeater::shutdownOnce()
{
    bool wasShuttingdown = mShuttingDown.exchange(true);
    if (wasShuttingdown)
    {
        return;
    }

    // cancel sleeper, thread in sleep is woken up if it is
    mSleeper.cancel();

    if (mThread.joinable()) mThread.join();
}

GfxWorkerHelloBeater::~GfxWorkerHelloBeater()
{
    shutdownOnce();
}

bool GfxProviderIsolatedProcess::Formats::isValid() const
{
    return mIsValid;
}

const char* GfxProviderIsolatedProcess::Formats::formats() const
{
    return (mIsValid && !mFormats.empty()) ? mFormats.c_str() : nullptr;
}

const char* GfxProviderIsolatedProcess::Formats::videoformats() const
{
    return (mIsValid && !mVideoformats.empty()) ? mVideoformats.c_str() : nullptr;
}

void GfxProviderIsolatedProcess::Formats::setOnce(const std::string& formats, const std::string& videoformats)
{
    const std::lock_guard<std::mutex> l(mMutex);
     // do not set again if it has been set
    if (mIsValid) return;
    mFormats = formats;
    mVideoformats = videoformats;
    mIsValid = true;
}

GfxProviderIsolatedProcess::GfxProviderIsolatedProcess(std::shared_ptr<GfxIsolatedProcess> process)
    : mProcess(process)
    , mPipename(process->pipename())
{
    assert(mProcess);
}

std::vector<std::string> GfxProviderIsolatedProcess::generateImages(
    FileSystemAccess* fa,
    const LocalPath& localfilepath,
    const std::vector<GfxDimension>& dimensions)
{
    // default return
    std::vector<std::string> images(dimensions.size());

    auto gfxclient = GfxClient::create(mPipename);
    gfxclient.runGfxTask(localfilepath.toPath(false), dimensions, images);

    return images;
}

const char* GfxProviderIsolatedProcess::supportedformats()
{
    return getformats(&Formats::formats);
}

const char* GfxProviderIsolatedProcess::supportedvideoformats()
{
    return getformats(&Formats::videoformats);
}

const char* GfxProviderIsolatedProcess::getformats(const char* (Formats::*formatsFn)() const)
{
    // already fetched from the server
    if (mFormats.isValid())
    {
        return (mFormats.*formatsFn)();
    }

    // do fetching
    std::string formats, videoformats;
    if (!GfxClient::create(mPipename).runSupportFormats(formats, videoformats))
    {
        return nullptr;
    }
    else
    {
        mFormats.setOnce(formats, videoformats);
        return (mFormats.*formatsFn)();
    }
}

AutoStartLauncher::AutoStartLauncher(const std::vector<std::string>& argv, std::function<void()> shutdowner) :
    mArgv(argv),
    mShuttingDown(false),
    mThreadIsRunning(false),
    mShutdowner(std::move(shutdowner))
{
    assert(!mArgv.empty());

    // preventive check: at least one element (executable)
    if (!mArgv.empty())
    {
        // launch loop thread
        startlaunchLoopThread();
    }
    else
    {
        LOG_fatal << "AutoStartLauncher argv is empty";
    }
}

bool AutoStartLauncher::startUntilSuccess(Process& process)
{
    milliseconds backOff = START_BACKOFF;
    while (!mShuttingDown)
    {
        if (process.run(mArgv))
        {
            LOG_verbose << "process is started";
            return true;
        }

        LOG_err << "couldn't not start: " << mArgv[0];
        mSleeper.sleep(backOff);
        backOff = std::min(backOff * 2, MAX_BACKOFF); // double it and MAX_BACKOFF at most
    }

    // ends due to shutdown
    return false;
}

bool AutoStartLauncher::startlaunchLoopThread()
{
    static const milliseconds MAX_FAST_FAILURE_BACKOFF(2*1000);

    // There are permanent startup failure such as missing DLL. This is not likey to happen
    // at customer's side as it will be installed properly. It is more likely during development
    // and testing phases. We want to implement some backOff to reduce CPU usage if it does happen
    // There are program crash due to gfx processing. We don't want to have backOff for this scenario
    // and here assume the continuously gfx processing crash is so few.
    // This is a naive way checking used seconds as the judgement
    auto backoffForFastFailure = [this](std::function<void()> f) {
        milliseconds backOff = START_BACKOFF;
        while(!mShuttingDown) {
                const time_point<steady_clock> start = steady_clock::now();
                f();
                auto usedSeconds = (steady_clock::now() - start) / seconds(1);

                // <= 1 seconds, it fails right after startup.
                if ((usedSeconds <= 1) && !mShuttingDown)
                {
                    LOG_err << "process existed too fast: " << usedSeconds << " backoff" << backOff.count() << "ms";
                    mSleeper.sleep(backOff);
                    backOff = std::min(backOff * 2, MAX_FAST_FAILURE_BACKOFF); // double it and MAX_FAST_FAILURE_BACKOFF at most
                }
                else
                {
                    backOff = START_BACKOFF;
                }
        }
    };

    auto launcher = [this, backoffForFastFailure]() {
        mThreadIsRunning = true;

        backoffForFastFailure([this](){
            Process process;
            if (startUntilSuccess(process))
            {
                bool ret = process.wait();
                LOG_debug << "wait: " << ret
                          << " hasSignal: " << process.hasTerminateBySignal()
                          << " " << (process.hasTerminateBySignal() ? std::to_string(process.getTerminatingSignal()) : "")
                          << " hasExited: " << process.hasExited()
                          << " " << (process.hasExited() ? std::to_string(process.getExitCode()) : "");
            }
        });

        mThreadIsRunning = false;
    };

    mThread = std::thread(launcher);

    return true;
}

//
// there is racing that mShutdowner() signal will be lost while the process
// is just starting. so we'll retry in the loop, but there is no reason it
// couldn't be shut down in 15 seconds
//
void AutoStartLauncher::exitLaunchLoopThread()
{
    milliseconds backOff(10);
    while (mThreadIsRunning && backOff < seconds(15))
    {
        // shutdown the started process
        if (mShutdowner) mShutdowner();
        std::this_thread::sleep_for(backOff);
        backOff += milliseconds(10);
    }
}

void AutoStartLauncher::shutDownOnce()
{
    bool wasShuttingdown = mShuttingDown.exchange(true);
    if (wasShuttingdown)
    {
        return;
    }

    LOG_info << "AutoStartLauncher is shutting down";

    // cancel sleeper, thread in sleep is woken up if it is
    mSleeper.cancel();

    exitLaunchLoopThread();
    if (mThread.joinable()) mThread.join();

    LOG_info << "AutoStartLauncher is down";
}

AutoStartLauncher::~AutoStartLauncher()
{
    shutDownOnce();
}

bool CancellableSleeper::sleep(const milliseconds& period)
{
    std::unique_lock<std::mutex> l(mMutex);

    if (mCancelled) return true;

    return mCv.wait_for(l, period, [this](){ return mCancelled;});
}

void CancellableSleeper::cancel()
{
    std::lock_guard<std::mutex> l(mMutex);

    mCancelled = true;

    mCv.notify_all();
}

GfxIsolatedProcess:: GfxIsolatedProcess(const std::string& pipename,
                                        const std::string& executable,
                                        unsigned int aliveSeconds)
                                        : mPipename(pipename)
{
    // a function to shutdown the isolated process
    auto shutdowner = [pipename]() { ::mega::gfx::GfxClient::create(pipename).runShutDown(); };

    auto arguments = formatArguments(pipename, executable, aliveSeconds);

    mLauncher = ::mega::make_unique<::mega::AutoStartLauncher>(
        formatArguments(pipename, executable, std::max(MIN_ALIVE_SECONDS, aliveSeconds)),
        shutdowner);

    // allow two beats by divide 3
    seconds beatInterval = seconds(aliveSeconds / 3);

    mBeater = ::mega::make_unique<::mega::GfxWorkerHelloBeater>(beatInterval, pipename);
}

GfxIsolatedProcess::GfxIsolatedProcess(const std::string& pipename,
                                       const std::string& executable)
                                       : GfxIsolatedProcess(pipename, executable, 15)
{

}

std::vector<std::string> GfxIsolatedProcess::formatArguments(const std::string& pipename,
                                                             const std::string& executable,
                                                             unsigned int aliveSeconds) const
{
    LocalPath absolutePath = LocalPath::fromAbsolutePath(executable);

    std::vector<std::string> commandArgs = {
        absolutePath.toPath(false),
        "-n=" + pipename,
        "-l=" + std::to_string(aliveSeconds)
    };

    return commandArgs;
}

}
