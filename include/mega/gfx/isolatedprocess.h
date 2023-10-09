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
    bool sleep_for(const std::chrono::milliseconds& period);

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

    virtual bool launch() = 0;
};
class AutoStartLauncher : public ILauncher
{
public:
    AutoStartLauncher(std::vector<std::string>&& argv, std::function<void()> shutdowner) :
        mArgv(std::move(argv)),
        mShuttingDown(false),
        mShutdowner(std::move(shutdowner))
    {

    }

    ~AutoStartLauncher();

    void shutDownOnce();

    bool launch();

private:

    bool startUntilSuccess(reproc::process& process);

    std::vector<std::string> mArgv;

    std::thread mThread;

    std::atomic<bool> mShuttingDown;

    CancellableSleeper mSleeper;

    // function to shutdown the started process if any
    std::function<void()> mShutdowner;
};

class IHelloBeater
{
public:
    virtual ~IHelloBeater() = default;

    virtual void start() = 0;
};
class GfxWorkerHelloBeater : public IHelloBeater
{
public:
    GfxWorkerHelloBeater(const std::chrono::seconds& period) : mPeriod(period) {}

    ~GfxWorkerHelloBeater();

    void start() override;

    void shutdownOnce();

    // extend the beat to next period
    void extend();
private:
    void beat();

    std::thread mThread;

    std::atomic<bool> mShuttingDown;

    CancellableSleeper mSleeper;

    std::chrono::seconds mPeriod;
};

class GfxProviderIsolatedProcess : public IGfxProvider
{
public:

    GfxProviderIsolatedProcess(std::unique_ptr<ILauncher> launcher,
                               std::unique_ptr<IHelloBeater> beater);

    std::vector<std::string> generateImages(FileSystemAccess* fa,
                                            const LocalPath& localfilepath,
                                            const std::vector<Dimension>& dimensions) override;

    const char* supportedformats() override;

    const char* supportedvideoformats() override;

private:

    std::vector<gfx::GfxSize> toGfxSize(const std::vector<Dimension>& dimensions);

    std::string mformats;

    std::unique_ptr<ILauncher> mLauncher;

    std::unique_ptr<IHelloBeater> mBeater;
};


}
