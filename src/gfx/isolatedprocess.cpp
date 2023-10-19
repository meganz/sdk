#include "mega/gfx/isolatedprocess.h"
#include "mega/gfx/worker/client.h"
#include "mega/gfx/worker/tasks.h"
#include "mega/logging.h"
#include <algorithm>
#include <iterator>
#include <mutex>
#include <ratio>
#include <reproc++/reproc.hpp>
#include <system_error>
#include <thread>
#include <vector>
#include <chrono>

using mega::gfx::GfxClient;
using mega::gfx::GfxSize;
namespace mega {

void GfxWorkerHelloBeater::beat()
{
    while(!mShuttingDown)
    {
        bool isCancelled = mSleeper.sleep(std::chrono::duration_cast<std::chrono::milliseconds>(mPeriod));
        if (!isCancelled)
        {
            auto gfxclient = GfxClient::create(mPipename);
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
    const std::scoped_lock<std::mutex> l(mMutex);
     // guard: never update twice
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

std::vector<GfxSize> GfxProviderIsolatedProcess::toGfxSize(const std::vector<Dimension>& dimensions)
{
    std::vector<GfxSize> sizes;
    std::transform(std::begin(dimensions),
                   std::end(dimensions),
                   std::back_insert_iterator<std::vector<GfxSize>>(sizes),
                   [](const Dimension &d){ return GfxSize(d.width, d.height);});
    return sizes;
}

std::vector<std::string> GfxProviderIsolatedProcess::generateImages(
    FileSystemAccess* fa,
    const LocalPath& localfilepath,
    const std::vector<Dimension>& dimensions)
{
    auto sizes = toGfxSize(dimensions);

    // default return
    std::vector<std::string> images(dimensions.size());

    auto gfxclient = GfxClient::create(mPipename);
    gfxclient.runGfxTask(localfilepath.toPath(false), std::move(sizes), images);

    return images;
}

const char* GfxProviderIsolatedProcess::supportedformats()
{
    // already fetched from the server
    if (mFormats.isValid())
    {
        return mFormats.formats();
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
        return mFormats.formats();
    }
}

const char* GfxProviderIsolatedProcess::supportedvideoformats()
{
    // already fetched from the server
    if (mFormats.isValid())
    {
        return mFormats.videoformats();
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
        return mFormats.videoformats();
    }
}

std::unique_ptr<GfxProviderIsolatedProcess> GfxProviderIsolatedProcess::create(
    const std::vector<string>& arguments,
    const std::string& pipename,
    unsigned int beatIntervalSeconds)
{
    return ::mega::make_unique<::mega::GfxProviderIsolatedProcess>(
        std::make_shared<GfxIsolatedProcess>(
            arguments,
            pipename,
            beatIntervalSeconds
    ));
}

bool AutoStartLauncher::startUntilSuccess(reproc::process& process)
{
    std::chrono::milliseconds backOff(100);
    while (!mShuttingDown)
    {
        auto ec = process.start(mArgv);
        if (!ec) // the process is started successfully
        {
            LOG_verbose << "process is started";
            return true;
        }

        LOG_err << "couldn't not start error code: " << ec.value() << " message: " << ec.message();
        mSleeper.sleep(backOff);
        backOff = std::min(backOff * 2, std::chrono::milliseconds(60 * 1000)); // double it and 60 seconds at most
    }

    // ends due to shutdown
    return false;
}

bool AutoStartLauncher::startlaunchLoopThread()
{
    auto launcher = [this]() {
        mThreadIsRunning = true;
        while(!mShuttingDown) {
            reproc::process process;
            if (startUntilSuccess(process))
            {
                int status = 0;
                std::error_code ec;
                std::tie(status, ec) = process.wait(reproc::infinite);
                if (ec)
                {
                    LOG_err << "wait error code: " << ec.value() << " message: " << ec.message();
                }
            }
        };
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
    std::chrono::milliseconds backOff(10);
    while (mThreadIsRunning && backOff < std::chrono::seconds(15))
    {
        // shutdown the started process
        if (mShutdowner) mShutdowner();
        std::this_thread::sleep_for(backOff);
        backOff += std::chrono::milliseconds(10);
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

bool CancellableSleeper::sleep(const std::chrono::milliseconds& period)
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

GfxIsolatedProcess:: GfxIsolatedProcess(const std::vector<string>& arguments,
                                        const std::string& pipename,
                                        unsigned int beatIntervalSeconds)
                                        : mPipename(pipename)
{
    // a function to shutdown the isolated process
    auto shutdowner = [pipename]() { ::mega::gfx::GfxClient::create(pipename).runShutDown(); };

    auto mLauncher = ::mega::make_unique<::mega::AutoStartLauncher>(
        arguments,
        shutdowner);

    auto mBeater = ::mega::make_unique<::mega::GfxWorkerHelloBeater>(std::chrono::seconds(beatIntervalSeconds), pipename);
}

}
