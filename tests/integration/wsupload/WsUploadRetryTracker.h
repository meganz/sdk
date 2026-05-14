/**
 * @file WsUploadRetryTracker.h
 * @brief MegaTransferListener that captures retry / state-transition signals.
 *
 * Extracted from SdkTest_test.cpp anonymous-namespace helper. Used by WS-upload
 * tests that need to observe onTransferTemporaryError / state-flow transitions
 * (e.g. InvalidPinned*, *DistressRetiresPool*).
 *
 * Header-only / fully inline so consumers may include without ODR concerns.
 */

#pragma once

#include "megaapi.h"
#include "mega/types.h"

#include <atomic>
#include <chrono>
#include <future>

namespace mega::test::wsupload
{

struct WsUploadRetryTracker : public ::mega::MegaTransferListener
{
    std::atomic<int> startCount{0};
    std::atomic<int> updateCount{0};
    std::atomic<int> temporaryErrorCount{0};
    std::atomic<::mega::ErrorCodes> lastTemporaryError{::mega::API_OK};
    std::atomic<int> lastState{-1};
    std::atomic<int> activeStateUpdateCount{0};
    std::atomic<bool> sawActiveAfterTemporaryError{false};
    std::atomic<bool> finished{false};
    std::atomic<::mega::ErrorCodes> result{::mega::API_EINTERNAL};
    std::promise<::mega::ErrorCodes> promiseResult;
    ::mega::MegaApi* mApi;
    std::future<::mega::ErrorCodes> futureResult;

    explicit WsUploadRetryTracker(::mega::MegaApi* api):
        mApi(api),
        futureResult(promiseResult.get_future())
    {}

    ~WsUploadRetryTracker() override
    {
        if (!finished && mApi)
        {
            mApi->removeTransferListener(this);
        }
    }

    void onTransferStart(::mega::MegaApi*, ::mega::MegaTransfer*) override
    {
        ++startCount;
    }

    void onTransferUpdate(::mega::MegaApi*, ::mega::MegaTransfer* transfer) override
    {
        ++updateCount;
        if (!transfer)
        {
            return;
        }

        lastState = transfer->getState();
        if (transfer->getState() == ::mega::MegaTransfer::STATE_ACTIVE)
        {
            ++activeStateUpdateCount;
            if (temporaryErrorCount.load() >= 1)
            {
                sawActiveAfterTemporaryError = true;
            }
        }
    }

    void onTransferTemporaryError(::mega::MegaApi*,
                                  ::mega::MegaTransfer*,
                                  ::mega::MegaError* error) override
    {
        ++temporaryErrorCount;
        lastTemporaryError = static_cast<::mega::ErrorCodes>(
            error ? error->getErrorCode() : ::mega::API_EINTERNAL);
    }

    void onTransferFinish(::mega::MegaApi*,
                          ::mega::MegaTransfer*,
                          ::mega::MegaError* error) override
    {
        bool expected = false;
        if (!finished.compare_exchange_strong(expected, true))
            return;

        result = static_cast<::mega::ErrorCodes>(
            error ? error->getErrorCode() : ::mega::API_EINTERNAL);

        std::promise<::mega::ErrorCodes> localPromise = std::move(promiseResult);
        localPromise.set_value(result);
    }

    // Default `seconds` matches SdkTest_test.h's `defaultTimeout` (60). Inlined here so
    // this header has no dependency on SdkTest_test.h's `using namespace mega;` pollution.
    ::mega::ErrorCodes waitForResult(int seconds = 60, bool unregisterListenerOnTimeout = true)
    {
        if (std::future_status::ready != futureResult.wait_for(std::chrono::seconds(seconds)))
        {
            if (unregisterListenerOnTimeout && mApi)
            {
                mApi->removeTransferListener(this);
            }
            return static_cast<::mega::ErrorCodes>(::mega::LOCAL_ETIMEOUT);
        }
        return futureResult.get();
    }
};

} // namespace mega::test::wsupload
