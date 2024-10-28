/**
 * @file
 * @brief This file contains definition of convenient mock listeners
 */

#ifndef INCLUDE_INTEGRATION_MOCK_LISTENERS_H_
#define INCLUDE_INTEGRATION_MOCK_LISTENERS_H_

#include "megaapi.h"

#include <gmock/gmock.h>

#include <chrono>
#include <future>

class SynchronizationHelper
{
public:
    SynchronizationHelper():
        finishedFuture(finishedPromise.get_future())
    {}

    // Wait indefinitely until the operation finishes
    void waitForFinish()
    {
        finishedFuture.wait();
    }

    // Wait for the specified duration
    template<typename Rep, typename Period>
    bool waitForFinishOrTimeout(const std::chrono::duration<Rep, Period>& duration)
    {
        auto status = finishedFuture.wait_for(duration);
        return status == std::future_status::ready;
    }

    // Mark the operation as finished
    void markAsFinished()
    {
        finishedPromise.set_value();
    }

private:
    std::promise<void> finishedPromise;
    std::future<void> finishedFuture;
};

class MockRequestListener: public ::mega::MegaRequestListener, public SynchronizationHelper
{
public:
    MockRequestListener()
    {
        ON_CALL(*this, onRequestFinish)
            .WillByDefault(::testing::Invoke(this, &MockRequestListener::defaultOnRequestFinish));
    }

    MOCK_METHOD(void,
                onRequestStart,
                (::mega::MegaApi * api, ::mega::MegaRequest* request),
                (override));
    MOCK_METHOD(void,
                onRequestUpdate,
                (::mega::MegaApi * api, ::mega::MegaRequest* request),
                (override));
    MOCK_METHOD(void,
                onRequestTemporaryError,
                (::mega::MegaApi * api, ::mega::MegaRequest* request, ::mega::MegaError* error),
                (override));

    MOCK_METHOD(void,
                onRequestFinish,
                (::mega::MegaApi*, ::mega::MegaRequest*, ::mega::MegaError*),
                (override));

    void setErrorExpectations(const int reqError, const std::optional<int> syncError)
    {
        using namespace testing;
        const auto expectedErr = Pointee(Property(&::mega::MegaError::getErrorCode, reqError));
        if (!syncError)
        {
            EXPECT_CALL(*this, onRequestFinish(_, _, expectedErr));
            return;
        }
        const auto expectedSyncErr =
            Pointee(Property(&::mega::MegaError::getSyncError, *syncError));
        EXPECT_CALL(*this, onRequestFinish(_, _, AllOf(expectedErr, expectedSyncErr)));
    }

private:
    void defaultOnRequestFinish(::mega::MegaApi*, ::mega::MegaRequest*, ::mega::MegaError*)
    {
        markAsFinished();
    }
};

#ifdef ENABLE_SYNC
/**
 * @class MockSyncListener
 * @brief Mock listener only implementing methods for syncs
 *
 */
class MockSyncListener: public ::mega::MegaListener, SynchronizationHelper
{
public:
    MOCK_METHOD(
        void,
        onSyncFileStateChanged,
        (::mega::MegaApi * api, ::mega::MegaSync* sync, std::string* localPath, int newState),
        (override));
    MOCK_METHOD(void, onSyncAdded, (::mega::MegaApi * api, ::mega::MegaSync* sync), (override));
    MOCK_METHOD(void, onSyncDeleted, (::mega::MegaApi * api, ::mega::MegaSync* sync), (override));
    MOCK_METHOD(void,
                onSyncStateChanged,
                (::mega::MegaApi * api, ::mega::MegaSync* sync),
                (override));
    MOCK_METHOD(void,
                onSyncStatsUpdated,
                (::mega::MegaApi * api, ::mega::MegaSyncStats* syncStats),
                (override));
    MOCK_METHOD(void, onGlobalSyncStateChanged, (::mega::MegaApi * api), (override));
    MOCK_METHOD(void,
                onSyncRemoteRootChanged,
                (::mega::MegaApi * api, ::mega::MegaSync* sync),
                (override));
    MOCK_METHOD(void,
                onRequestFinish,
                (::mega::MegaApi*, ::mega::MegaRequest*, ::mega::MegaError*),
                (override));
};
#endif // ENABLE_SYNC

#endif // INCLUDE_INTEGRATION_MOCK_LISTENERS_H_
