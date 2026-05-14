/**
 * @file WsUploadTransitionCapture.h
 * @brief RAII helper class capturing WS session-URL transitions for tests.
 *
 * Extracted from SdkTest_test.cpp. Used by InvalidPinned*
 * SdkWsUpload tests to capture the failover transition driven by
 * `globalMegaTestHooks.onWsSessionUrlTransition` (hook added in
 * commit `26718098cc`).
 *
 * The class is fully inline so the header is self-contained — multiple
 * TUs may include it without ODR concerns.
 */

#pragma once

#include "mega/testhooks.h"

#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

namespace mega
{
namespace test
{
namespace wsupload
{

// RAII helper for the WS session-URL transition hook. Registers
// `globalMegaTestHooks.onWsSessionUrlTransition` on construction and unregisters
// on destruction. Uses a shared_ptr-indirected state so any in-flight hook
// callback retains its referent after the test scope exits (the macro idiom
// inside DEBUG_TEST_HOOK_WS_SESSION_URL_TRANSITION copies the std::function under
// `mMutex` before invoking — so a hook firing during destruction operates on the
// shared state, not on freed stack locals).
//
// Filters to transitions for one specific invalid-pinned URL. Records the first
// transition observed (`oldUrl == invalidPinnedUrl` for invalidatePinned events,
// or `newUrl != invalidPinnedUrl && !newUrl.empty()` for onStart events).
class WsSessionUrlTransitionCapture
{
public:
    explicit WsSessionUrlTransitionCapture(std::string invalidPinnedUrl)
        : mShared(std::make_shared<Shared>())
        , mInvalidPinnedUrl(std::move(invalidPinnedUrl))
    {
#ifdef MEGASDK_DEBUG_TEST_HOOKS_ENABLED
        auto shared = mShared;
        const std::string filterUrl = mInvalidPinnedUrl;
        std::lock_guard<std::mutex> g(::mega::globalMegaTestHooks.mMutex);
        ::mega::globalMegaTestHooks.onWsSessionUrlTransition =
            [shared, filterUrl](int /*tag*/,
                                const std::string& oldUrl,
                                const std::string& newUrl,
                                const char* reason)
        {
            std::lock_guard<std::mutex> lk(shared->m);
            if (shared->observed)
                return;
            const std::string r = reason ? reason : "";
            const bool isInvalidate =
                r == "invalidatePinned" && oldUrl == filterUrl;
            const bool isFreshOnStart =
                r == "onStart" && !newUrl.empty() && newUrl != filterUrl;
            if (isInvalidate || isFreshOnStart)
            {
                shared->observed = true;
                shared->capturedNewUrl = newUrl;
                shared->capturedReason = r;
                shared->cv.notify_all();
            }
        };
#endif
    }

    ~WsSessionUrlTransitionCapture()
    {
#ifdef MEGASDK_DEBUG_TEST_HOOKS_ENABLED
        std::lock_guard<std::mutex> g(::mega::globalMegaTestHooks.mMutex);
        ::mega::globalMegaTestHooks.onWsSessionUrlTransition = nullptr;
#endif
    }

    WsSessionUrlTransitionCapture(const WsSessionUrlTransitionCapture&) = delete;
    WsSessionUrlTransitionCapture& operator=(const WsSessionUrlTransitionCapture&) = delete;

    // Returns true if a matching transition is observed within `timeout`.
    bool waitForTransition(std::chrono::seconds timeout) const
    {
        std::unique_lock<std::mutex> lk(mShared->m);
        return mShared->cv.wait_for(lk, timeout, [s = mShared] { return s->observed; });
    }

    bool observed() const
    {
        std::lock_guard<std::mutex> lk(mShared->m);
        return mShared->observed;
    }

    std::string capturedNewUrl() const
    {
        std::lock_guard<std::mutex> lk(mShared->m);
        return mShared->capturedNewUrl;
    }

    std::string capturedReason() const
    {
        std::lock_guard<std::mutex> lk(mShared->m);
        return mShared->capturedReason;
    }

private:
    struct Shared
    {
        std::mutex m;
        std::condition_variable cv;
        bool observed = false;
        std::string capturedNewUrl;
        std::string capturedReason;
    };

    std::shared_ptr<Shared> mShared;
    std::string mInvalidPinnedUrl;
};

} // namespace wsupload
} // namespace test
} // namespace mega
