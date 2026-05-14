/**
 * @file TransferTempErrorTracker.h
 * @brief MegaTransferListener that records first finish/error and exposes a
 *        wait-for-result future, distinguishing transient vs final outcomes.
 *
 * Extracted from SdkTest_test.cpp's anonymous-namespace helper cluster
 * (followup7-9 Goal 1.1c). Consumed both by the WS-upload overquota test
 * (SdkWsUploadTest.OverquotaDuringTransfer in wsupload/SdkWsUploadTest.cpp)
 * and by the SDK-side overquota test (SdkTest.SdkTestUploadsOverquota in
 * SdkTest_test.cpp).
 *
 * Header-only / fully inline so consumers may include without ODR concerns.
 */

#pragma once

#include "megaapi.h"
#include "mega/types.h"
#include "test.h"  // defaultTimeout, LOCAL_ETIMEOUT

#include <atomic>
#include <chrono>
#include <future>
#include <utility>

namespace mega::test::wsupload
{

struct TransferTempErrorTracker: public ::mega::MegaTransferListener
{
    std::atomic<bool> done{false};
    std::atomic<bool> temporary{false};
    std::atomic<::mega::ErrorCodes> result{::mega::ErrorCodes::API_EINTERNAL};
    std::atomic<int> transferTag{-1};
    std::promise<::mega::ErrorCodes> promiseResult;
    ::mega::MegaApi* mApi;
    std::future<::mega::ErrorCodes> futureResult;

    explicit TransferTempErrorTracker(::mega::MegaApi* api):
        mApi(api),
        futureResult(promiseResult.get_future())
    {}

    ~TransferTempErrorTracker() override
    {
        if (!done && mApi)
        {
            mApi->removeTransferListener(this);
        }
    }

    void onTransferStart(::mega::MegaApi*, ::mega::MegaTransfer* transfer) override
    {
        if (transfer && transferTag.load() < 0)
        {
            transferTag = transfer->getTag();
        }
    }

    void onTransferFinish(::mega::MegaApi*,
                          ::mega::MegaTransfer* transfer,
                          ::mega::MegaError* error) override
    {
        recordResult(transfer, error, false);
    }

    void onTransferTemporaryError(::mega::MegaApi*,
                                  ::mega::MegaTransfer* transfer,
                                  ::mega::MegaError* error) override
    {
        recordResult(transfer, error, true);
    }

    ::mega::ErrorCodes waitForResult(int seconds = defaultTimeout,
                                     bool unregisterListenerOnTimeout = true)
    {
        if (std::future_status::ready !=
            futureResult.wait_for(std::chrono::seconds(seconds)))
        {
            if (unregisterListenerOnTimeout && mApi)
            {
                mApi->removeTransferListener(this);
            }
            return static_cast<::mega::ErrorCodes>(LOCAL_ETIMEOUT);
        }
        return futureResult.get();
    }

    bool wasTemporaryError() const { return temporary; }

private:
    void recordResult(::mega::MegaTransfer* transfer,
                      ::mega::MegaError* error,
                      const bool isTemporary)
    {
        bool expected = false;
        if (!done.compare_exchange_strong(expected, true))
            return;

        if (transfer && transferTag.load() < 0)
        {
            transferTag = transfer->getTag();
        }

        const int code = error ? error->getErrorCode() : ::mega::API_EINTERNAL;
        result = static_cast<::mega::ErrorCodes>(code);
        temporary = isTemporary;

        std::promise<::mega::ErrorCodes> localPromise = std::move(promiseResult);
        localPromise.set_value(result);
    }
};

} // namespace mega::test::wsupload
