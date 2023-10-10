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
            auto gfxclient = GfxClient::create();
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

GfxProviderIsolatedProcess::GfxProviderIsolatedProcess(
    std::unique_ptr<ILauncher> launcher,
    std::unique_ptr<IHelloBeater> beater)
    : mLauncher(std::move(launcher))
    , mBeater(std::move(beater))
{
    assert(mLauncher);
    assert(mBeater);
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

    auto gfxclient = GfxClient::create();
    gfxclient.runGfxTask(localfilepath.toPath(false), std::move(sizes), images);

    return images;
}

const char* GfxProviderIsolatedProcess::supportedformats()
{
    if (mformats.empty())
    {
        // hard coded at moment, need to get from isolated process once
        mformats+=".jpg.png.bmp.jpeg.cut.dds.g3.gif.hdr.ico.iff.ilbm"
           ".jbig.jng.jif.koala.pcd.mng.pcx.pbm.pgm.ppm.pfm.pds.raw.3fr.ari"
           ".arw.bay.crw.cr2.cap.dcs.dcr.dng.drf.eip.erf.fff.iiq.k25.kdc.mdc.mef.mos.mrw"
           ".nef.nrw.obm.orf.pef.ptx.pxn.r3d.raf.raw.rwl.rw2.rwz.sr2.srf.srw.x3f.ras.tga"
           ".xbm.xpm.jp2.j2k.jpf.jpx.";
    }

    return mformats.c_str();
}

// video formats are not supported at moment.
const char* GfxProviderIsolatedProcess::supportedvideoformats()
{
    return nullptr;
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

}
