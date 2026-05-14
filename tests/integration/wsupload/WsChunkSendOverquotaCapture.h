/**
 * @file WsChunkSendOverquotaCapture.h
 * @brief RAII helper class injecting OVERQUOTA via the WS chunk-send test hook.
 *
 * Mirrors WsUploadTransitionCapture.h (fu7-5/fu7-7) — installs
 * `globalMegaTestHooks.onWsChunkSendOverquota` on construction and removes it
 * on destruction. Used by `SdkWsUploadTest.OverquotaDuringTransfer` to
 * deterministically drive the WS-channel OVERQUOTA failure path without
 * depending on staging account quota state.
 *
 * One-shot semantics: the hook returns true on the FIRST chunk-send observed
 * for the configured transferTag; subsequent invocations return false so the
 * post-failure retry/cleanup proceeds without further injection.
 */

#pragma once

#include "mega/testhooks.h"

#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>

namespace mega
{
namespace test
{
namespace wsupload
{

class WsChunkSendOverquotaCapture
{
public:
    explicit WsChunkSendOverquotaCapture(int targetTransferTag)
        : mShared(std::make_shared<Shared>())
        , mTargetTag(targetTransferTag)
    {
#ifdef MEGASDK_DEBUG_TEST_HOOKS_ENABLED
        auto shared = mShared;
        const int filterTag = mTargetTag;
        std::lock_guard<std::mutex> g(::mega::globalMegaTestHooks.mMutex);
        ::mega::globalMegaTestHooks.onWsChunkSendOverquota =
            [shared, filterTag](int tag) -> bool
        {
            if (tag != filterTag)
                return false;
            std::lock_guard<std::mutex> lk(shared->m);
            ++shared->totalSeen;
            if (shared->fired)
                return false;
            shared->fired = true;
            shared->cv.notify_all();
            return true;
        };
#endif
    }

    ~WsChunkSendOverquotaCapture()
    {
#ifdef MEGASDK_DEBUG_TEST_HOOKS_ENABLED
        std::lock_guard<std::mutex> g(::mega::globalMegaTestHooks.mMutex);
        ::mega::globalMegaTestHooks.onWsChunkSendOverquota = nullptr;
#endif
    }

    WsChunkSendOverquotaCapture(const WsChunkSendOverquotaCapture&) = delete;
    WsChunkSendOverquotaCapture& operator=(const WsChunkSendOverquotaCapture&) = delete;

    bool waitForFire(std::chrono::seconds timeout) const
    {
        std::unique_lock<std::mutex> lk(mShared->m);
        return mShared->cv.wait_for(lk, timeout, [s = mShared] { return s->fired; });
    }

    bool hasFired() const
    {
        std::lock_guard<std::mutex> lk(mShared->m);
        return mShared->fired;
    }

    int totalSeen() const
    {
        std::lock_guard<std::mutex> lk(mShared->m);
        return mShared->totalSeen;
    }

private:
    struct Shared
    {
        std::mutex m;
        std::condition_variable cv;
        bool fired = false;
        int totalSeen = 0;
    };

    std::shared_ptr<Shared> mShared;
    int mTargetTag;
};

} // namespace wsupload
} // namespace test
} // namespace mega
