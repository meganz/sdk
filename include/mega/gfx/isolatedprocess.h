#pragma once

#include "mega.h"
#include "mega/gfx.h"
#include "mega/gfx/worker/tasks.h"
#include "mega/types.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>
#include <condition_variable>
#include <mutex>

namespace mega {

class Process;

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
    AutoStartLauncher(const std::vector<std::string>& argv, std::function<void()> shutdowner);

    ~AutoStartLauncher();

    void shutDownOnce();

private:

    bool startUntilSuccess(Process& process);

    bool startlaunchLoopThread();

    void exitLaunchLoopThread();

    std::vector<std::string> mArgv;

    std::thread mThread;

    std::atomic<bool> mShuttingDown;

    std::atomic<bool> mThreadIsRunning;

    CancellableSleeper mSleeper;

    // function to shutdown the started process if any
    std::function<void()> mShutdowner;

    const static std::chrono::milliseconds MAX_BACKOFF;

    const static std::chrono::milliseconds START_BACKOFF;
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
        : mShuttingDown(false)
        , mPeriod(period)
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

// This lauches the process and keep it running using beater
class GfxIsolatedProcess
{
public:
    // Notes: beatIntervalSeconds would be set to minimum 3 if not
    GfxIsolatedProcess(const std::string& pipename,
                       const std::string& executable,
                       unsigned int beatIntervalSeconds);

    GfxIsolatedProcess(const std::string& pipename,
                       const std::string& executable);

    const std::string& pipename() const { return mPipename; }
private:
    // this hide the detail of the isolated process command options and also provides the default value that callers
    // don't need to supply
    std::vector<std::string> formatArguments(const std::string& pipename,
                                             const std::string& executable,
                                             unsigned int aliveSeconds) const;

    static const unsigned int MIN_ALIVE_SECONDS;

    std::unique_ptr<ILauncher> mLauncher;

    std::unique_ptr<IHelloBeater> mBeater;

    std::string mPipename;
};

class GfxProviderIsolatedProcess : public IGfxProvider
{
public:

    GfxProviderIsolatedProcess(std::shared_ptr<GfxIsolatedProcess> process);

    std::vector<std::string> generateImages(FileSystemAccess* fa,
                                            const LocalPath& localfilepath,
                                            const std::vector<GfxDimension>& dimensions) override;

    const char* supportedformats() override;

    const char* supportedvideoformats() override;

private:

    // thread safe formats accessor
    class Formats
    {
    public:
        // whether has valie formats
        bool isValid() const;

        // return formats if it is valid and not empty, otherwise nullptr
        const char* formats() const;

        // return videoformats if it is valid and not empty, otherwise nullptr
        const char* videoformats() const;

        // set the formats and videoformats once
        void setOnce(const std::string& formats, const std::string& videoformats);

    private:
        std::string         mFormats;
        std::string         mVideoformats;
        std::atomic<bool>   mIsValid{false};
        std::mutex          mMutex;
    };

    const char* getformats(const char* (Formats::*formatsFunc)() const);

    Formats mFormats;

    std::shared_ptr<GfxIsolatedProcess> mProcess;

    std::string mPipename;
};

}
