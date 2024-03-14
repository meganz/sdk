#pragma once

#include "mega.h"
#include "mega/gfx.h"

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
    // true if sleeping is cancelled, otherwise, false
    bool sleep(const std::chrono::milliseconds& period);

    void cancel();
private:
    std::condition_variable mCv;
    std::mutex              mMutex;
    bool                    mCancelled = false;
};

class AutoStartLauncher
{
public:
    AutoStartLauncher(const std::vector<std::string>& argv, std::function<void()> shutdowner);

    ~AutoStartLauncher();

    void shutDownOnce();

private:

    bool startUntilSuccess(Process& process);

    bool startLaunchLoopThread();

    void exitLaunchLoopThread();

    std::vector<std::string> mArgv;

    std::thread mThread;

    std::atomic<bool> mShuttingDown{false};

    std::atomic<bool> mThreadIsRunning{false};

    CancellableSleeper mSleeper;

    // function to shutdown the started process if any
    std::function<void()> mShutdowner;

    const static std::chrono::milliseconds MAX_BACKOFF;

    const static std::chrono::milliseconds START_BACKOFF;
};

// This class creates a thread sending hello command to
// gfxworker in mPeriod interval. The purpose is to keep
// the gfxworker running.
class HelloBeater
{
public:
    HelloBeater(const std::chrono::seconds& period, const std::string& pipeName)
        : mPeriod(period)
        , mPipeName(pipeName)
    {
        mThread = std::thread(&HelloBeater::beat, this);
    }

    ~HelloBeater();

    void shutdownOnce();

private:
    void beat();

    std::thread mThread;

    std::atomic<bool> mShuttingDown{false};

    CancellableSleeper mSleeper;

    std::chrono::seconds mPeriod;

    std::string mPipeName;
};

// This lauches the process and keep it running using beater
class GfxIsolatedProcess
{
public:
    // Notes: beatIntervalSeconds would be set to minimum 3 if not
    GfxIsolatedProcess(const std::string& pipeName,
                       const std::string& executable,
                       unsigned int beatIntervalSeconds);

    GfxIsolatedProcess(const std::string& pipeName,
                       const std::string& executable);

    const std::string& pipeName() const { return mPipeName; }
private:
    // this hide the detail of the isolated process command options and also provides the default value that callers
    // don't need to supply
    static std::vector<std::string> formatArguments(const std::string& pipeName,
                                                    const std::string& executable,
                                                    unsigned int keepAliveInSeconds);

    static const unsigned int MIN_ALIVE_SECONDS;

    std::string mPipeName;

    AutoStartLauncher mLauncher;

    HelloBeater mBeater;
};

class GfxProviderIsolatedProcess : public IGfxProvider
{
public:

    GfxProviderIsolatedProcess(std::unique_ptr<GfxIsolatedProcess> process);

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
        // whether has valid formats
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

    std::unique_ptr<GfxIsolatedProcess> mProcess;

    std::string mPipeName;
};

}
