#pragma once

#include "mega.h"
#include "mega/gfx.h"
#include "mega/gfx/isolatedprocess.h"
#include "mega/gfx/worker/tasks.h"
#include "mega/types.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>
#include <condition_variable>
#include <mutex>
#include <reproc++/reproc.hpp>

namespace mega {

// a simple sleeper can be only cancelled for once and forever
class CancellableSleeper
{
public:
    CancellableSleeper() : mCancelled(false) {}

    // true if sleeping is cancelled, otherwise, false
    bool sleep(const std::chrono::milliseconds& period);

    void cancel();
private:
    std::condition_variable mCv;
    std::mutex              mMutex;
    bool                    mCancelled;
};

class ILauncher
{
public:
    virtual ~ILauncher() = default;
};

class AutoStartLauncher : public ILauncher
{
public:
    AutoStartLauncher(const std::vector<std::string>& argv, std::function<void()> shutdowner) :
        mArgv(argv),
        mShuttingDown(false),
        mThreadIsRunning(false),
        mShutdowner(std::move(shutdowner))
    {
        startlaunchLoopThread();
    }

    ~AutoStartLauncher();

    void shutDownOnce();

private:

    bool startUntilSuccess(reproc::process& process);

    bool startlaunchLoopThread();

    void exitLaunchLoopThread();

    std::vector<std::string> mArgv;

    std::thread mThread;

    std::atomic<bool> mShuttingDown;

    std::atomic<bool> mThreadIsRunning;

    CancellableSleeper mSleeper;

    // function to shutdown the started process if any
    std::function<void()> mShutdowner;
};

class IHelloBeater
{
public:
    virtual ~IHelloBeater() = default;
};

// This class creates a thread sending hello command to
// gfxworker in mPeriod interval. The purpose is to keep
// the gfxworker running.
class GfxWorkerHelloBeater : public IHelloBeater
{
public:
    GfxWorkerHelloBeater(const std::chrono::seconds& period, const std::string& pipename)
        : mPeriod(period)
        , mPipename(pipename)
    {
        mThread = std::thread(&GfxWorkerHelloBeater::beat, this);
    }

    ~GfxWorkerHelloBeater();

    void shutdownOnce();

private:
    void beat();

    std::thread mThread;

    std::atomic<bool> mShuttingDown;

    CancellableSleeper mSleeper;

    std::chrono::seconds mPeriod;

    std::string mPipename;
};

class GfxProviderIsolatedProcess : public IGfxProvider
{
public:

    GfxProviderIsolatedProcess(std::unique_ptr<ILauncher> launcher,
                               std::unique_ptr<IHelloBeater> beater,
                               const std::string& pipename);

    std::vector<std::string> generateImages(FileSystemAccess* fa,
                                            const LocalPath& localfilepath,
                                            const std::vector<Dimension>& dimensions) override;

    const char* supportedformats() override;

    const char* supportedvideoformats() override;

    static std::unique_ptr<GfxProviderIsolatedProcess> create(const std::vector<string>& arguments,
                                                              const std::string& pipename,
                                                              unsigned int beatIntervalSeconds);

private:

    std::vector<gfx::GfxSize> toGfxSize(const std::vector<Dimension>& dimensions);

    std::string mformats;

    std::unique_ptr<ILauncher> mLauncher;

    std::unique_ptr<IHelloBeater> mBeater;

    std::string mPipename;
};


}
