/**
 * @file WsQuotaHoldTracker.h
 * @brief MegaTransferListener recording the ORDERED per-tag sequence of
 *        temporary errors (code + isForeignOverquota flag) and the final finish.
 *
 * The existing TransferTempErrorTracker latches only the FIRST result (its
 * promise fires once), which cannot express a predictive-hold lifecycle that is
 * [tempError EOVERQUOTA (foreign?)]…* then [finish OK] after quota is released.
 * This tracker keeps the full ordered event list per transfer tag and exposes
 * condition-variable waiters — no sleeps.
 *
 * Registers itself as a GLOBAL transfer listener (addTransferListener) on
 * construction and removes itself in the destructor, so it observes every
 * transfer on the api regardless of which per-transfer listener started it.
 * Events are keyed by MegaTransfer::getTag().
 *
 * Header-only / fully inline. `::mega::` prefixes throughout (C++20).
 */

#pragma once

#include "mega/types.h"
#include "megaapi.h"

#include <chrono>
#include <condition_variable>
#include <map>
#include <mutex>
#include <vector>

namespace mega::test::wsupload
{

class WsQuotaHoldTracker: public ::mega::MegaTransferListener
{
public:
    struct Event
    {
        enum class Kind
        {
            TemporaryError,
            Finish,
        };

        Kind kind = Kind::TemporaryError;
        ::mega::ErrorCodes code = ::mega::API_EINTERNAL;
        bool foreignOverquota = false;
    };

    explicit WsQuotaHoldTracker(::mega::MegaApi* api):
        mApi(api)
    {
        if (mApi)
            mApi->addTransferListener(this);
    }

    ~WsQuotaHoldTracker() override
    {
        if (mApi)
            mApi->removeTransferListener(this);
    }

    WsQuotaHoldTracker(const WsQuotaHoldTracker&) = delete;
    WsQuotaHoldTracker& operator=(const WsQuotaHoldTracker&) = delete;

    void onTransferTemporaryError(::mega::MegaApi*,
                                  ::mega::MegaTransfer* transfer,
                                  ::mega::MegaError* error) override
    {
        if (!transfer)
            return;
        Event ev;
        ev.kind = Event::Kind::TemporaryError;
        ev.code =
            static_cast<::mega::ErrorCodes>(error ? error->getErrorCode() : ::mega::API_EINTERNAL);
        ev.foreignOverquota = transfer->isForeignOverquota();
        record(transfer->getTag(), ev);
    }

    void onTransferFinish(::mega::MegaApi*,
                          ::mega::MegaTransfer* transfer,
                          ::mega::MegaError* error) override
    {
        if (!transfer)
            return;
        Event ev;
        ev.kind = Event::Kind::Finish;
        ev.code =
            static_cast<::mega::ErrorCodes>(error ? error->getErrorCode() : ::mega::API_EINTERNAL);
        ev.foreignOverquota = transfer->isForeignOverquota();
        record(transfer->getTag(), ev);
    }

    bool waitForTemporaryError(int tag, std::chrono::seconds timeout) const
    {
        std::unique_lock<std::mutex> lk(mMutex);
        return mCv.wait_for(lk,
                            timeout,
                            [&]
                            {
                                return hasKindLocked(tag, Event::Kind::TemporaryError);
                            });
    }

    bool waitForFinish(int tag, std::chrono::seconds timeout) const
    {
        std::unique_lock<std::mutex> lk(mMutex);
        return mCv.wait_for(lk,
                            timeout,
                            [&]
                            {
                                return hasKindLocked(tag, Event::Kind::Finish);
                            });
    }

    std::vector<Event> sequence(int tag) const
    {
        std::lock_guard<std::mutex> lk(mMutex);
        auto it = mByTag.find(tag);
        return it == mByTag.end() ? std::vector<Event>{} : it->second;
    }

    int temporaryErrorCount(int tag) const
    {
        std::lock_guard<std::mutex> lk(mMutex);
        auto it = mByTag.find(tag);
        if (it == mByTag.end())
            return 0;
        int n = 0;
        for (const auto& e: it->second)
            if (e.kind == Event::Kind::TemporaryError)
                ++n;
        return n;
    }

    int finishCount(int tag) const
    {
        std::lock_guard<std::mutex> lk(mMutex);
        auto it = mByTag.find(tag);
        if (it == mByTag.end())
            return 0;
        int n = 0;
        for (const auto& e: it->second)
            if (e.kind == Event::Kind::Finish)
                ++n;
        return n;
    }

private:
    void record(int tag, const Event& ev)
    {
        {
            std::lock_guard<std::mutex> lk(mMutex);
            mByTag[tag].push_back(ev);
        }
        mCv.notify_all();
    }

    bool hasKindLocked(int tag, Event::Kind kind) const
    {
        auto it = mByTag.find(tag);
        if (it == mByTag.end())
            return false;
        for (const auto& e: it->second)
            if (e.kind == kind)
                return true;
        return false;
    }

    mutable std::mutex mMutex;
    mutable std::condition_variable mCv;
    std::map<int, std::vector<Event>> mByTag;
    ::mega::MegaApi* mApi = nullptr;
};

} // namespace mega::test::wsupload
