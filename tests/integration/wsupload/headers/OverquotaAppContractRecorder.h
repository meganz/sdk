/**
 * @file OverquotaAppContractRecorder.h
 * @brief Records what an APP can see of an upload overquota (EVENT_STORAGE plus the
 *        upload temp-error / finish / progress stream) and grades it against the
 *        MEGAsync 6.7.0 decision rule.
 *
 * The SDK-6298 predictive hold emits `transfer_failed(API_EOVERQUOTA, 0)` for an
 * own-account `tfs` shortfall without any EVENT_STORAGE, so a hold that the SDK
 * considers "reported" is invisible to MEGAsync (QA, MEGAsync 6.7.0 RC1 on SDK
 * v10.21.0-rc.1: a 10 MiB upload with 8 MiB free stuck at 0 bytes, no dialog).
 * This recorder captures the public surface an app consumes, so a test can assert
 * the app contract instead of an SDK-internal hook.
 *
 * MEGAsync 6.7.0 decision rule (the contract graded by unactionableOwnOverquota()):
 *  - Its onTransferTemporaryError handler acts on API_EOVERQUOTA only when
 *    `transfer->isForeignOverquota()` (the "X's account is full" message for an
 *    inshare) or when `error->getValue() != 0` (transfer quota, with a wait time).
 *  - Its "Storage full" dialogs open only while its storage state is
 *    STORAGE_STATE_RED or STORAGE_STATE_PAYWALL, and that state is written only from
 *    MegaEvent::EVENT_STORAGE.
 *  - So a temp API_EOVERQUOTA with getValue() == 0 and !isForeignOverquota() is
 *    actionable only if EVENT_STORAGE RED/PAYWALL reaches the app around it. Every
 *    legacy own-account path satisfies this: activateoverquota() calls
 *    setstoragestatus(RED/PAYWALL) (notify_storage -> EVENT_STORAGE 2/4) before it
 *    surfaces the temp error. The SDK-6298 own-account hold is the only exception.
 *  - The app's storage state is sticky (it keeps the last EVENT_STORAGE number), so
 *    an error that arrives while the last recorded EVENT_STORAGE is RED/PAYWALL is
 *    also actionable (the dialog is already up). Only events this recorder saw count.
 *
 * Progress (waitForFinishProgressAware): the app-visible transferred bytes are clamped
 * monotonic for WS uploads (MegaApiImpl keeps max(progresscompleted, previous)), so a
 * WS re-upload from 0 is invisible there until it passes the old maximum. Activity
 * is therefore ANY change of either the app byte count or the client-side
 * Transfer::progresscompleted (which does drop to 0 on a restart), sampled about once
 * per second through the always-compiled fetchWsUploadTransferSnapshots.
 *
 * Also provides waitForQueueShortfall() / waitForOwnQueueShortfall(): the public
 * MegaApi::getWsUploadQueueQuotaFit query (observational, unchanged by the fix), used as
 * the "the ledger really holds the (own or foreign) shortfall" premise.
 *
 * Registers itself as a GLOBAL MegaListener (addListener) on construction and removes
 * itself in the destructor. Callbacks run on the SDK thread and only take this
 * object's own mutex; the waiters never call into MegaApi (or dispatch onto the
 * client thread) while holding it: those calls can block on the SDK mutex that the
 * callback thread holds.
 *
 * Header-only / fully inline; the public MegaApi surface plus the always-compiled
 * client-thread snapshot helper (no test hooks), so it compiles and works in
 * hooks-OFF (Release) builds. `::mega::` prefixes throughout (C++20).
 */

#pragma once

#include "megaapi.h"
#include "wsupload/headers/WsUploadTestHelpers.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace mega::test::wsupload
{

class OverquotaAppContractRecorder: public ::mega::MegaListener
{
public:
    using Clock = std::chrono::steady_clock;

    // A RED/PAYWALL EVENT_STORAGE this close to a temp error (either side) makes the
    // error actionable: legacy paths fire the event and the error in the same
    // client-thread step, so a few seconds of tolerance only absorbs scheduling noise.
    static constexpr std::chrono::seconds kStorageEventWindow{5};

    struct StorageEvent
    {
        int number = ::mega::MegaApi::STORAGE_STATE_UNKNOWN;
        Clock::time_point ts{};
    };

    struct TempError
    {
        int tag = -1;
        int code = ::mega::MegaError::API_OK;
        long long value = 0; // MegaError::getValue()
        bool foreign = false; // MegaTransfer::isForeignOverquota()
        long long transferred = 0;
        long long total = 0;
        Clock::time_point ts{};
    };

    struct Finish
    {
        int tag = -1;
        int code = ::mega::MegaError::API_OK;
        long long transferred = 0;
        long long total = 0;
        Clock::time_point ts{};
    };

    struct WaitResult
    {
        bool finished = false;
        int code = ::mega::MegaError::API_EINTERNAL;
        long long lastBytes = 0;
        long long total = 0;
        std::string reason;
    };

    explicit OverquotaAppContractRecorder(::mega::MegaApi* api):
        mApi(api)
    {
        if (mApi)
            mApi->addListener(this);
    }

    ~OverquotaAppContractRecorder() override
    {
        if (mApi)
            mApi->removeListener(this);
    }

    OverquotaAppContractRecorder(const OverquotaAppContractRecorder&) = delete;
    OverquotaAppContractRecorder& operator=(const OverquotaAppContractRecorder&) = delete;

    static bool isFullStorageState(int number)
    {
        return number == ::mega::MegaApi::STORAGE_STATE_RED ||
               number == ::mega::MegaApi::STORAGE_STATE_PAYWALL;
    }

    // ---- MegaListener ------------------------------------------------------

    void onEvent(::mega::MegaApi*, ::mega::MegaEvent* event) override
    {
        if (!event || event->getType() != ::mega::MegaEvent::EVENT_STORAGE)
            return;
        {
            std::lock_guard<std::mutex> lk(mMutex);
            mStorageEvents.push_back(
                StorageEvent{static_cast<int>(event->getNumber()), Clock::now()});
        }
        mCv.notify_all();
    }

    void onTransferStart(::mega::MegaApi*, ::mega::MegaTransfer* transfer) override
    {
        if (!isUpload(transfer))
            return;
        std::lock_guard<std::mutex> lk(mMutex);
        if (transfer->getFileName())
            mProgress[transfer->getTag()].name = transfer->getFileName();
        noteBytesLocked(transfer->getTag(),
                        transfer->getTransferredBytes(),
                        transfer->getTotalBytes());
    }

    void onTransferUpdate(::mega::MegaApi*, ::mega::MegaTransfer* transfer) override
    {
        if (!isUpload(transfer))
            return;
        {
            std::lock_guard<std::mutex> lk(mMutex);
            noteBytesLocked(transfer->getTag(),
                            transfer->getTransferredBytes(),
                            transfer->getTotalBytes());
        }
        mCv.notify_all();
    }

    void onTransferTemporaryError(::mega::MegaApi*,
                                  ::mega::MegaTransfer* transfer,
                                  ::mega::MegaError* error) override
    {
        if (!isUpload(transfer))
            return;
        TempError te;
        te.tag = transfer->getTag();
        te.code = error ? error->getErrorCode() : ::mega::MegaError::API_EINTERNAL;
        te.value = error ? error->getValue() : 0;
        te.foreign = transfer->isForeignOverquota();
        te.transferred = transfer->getTransferredBytes();
        te.total = transfer->getTotalBytes();
        te.ts = Clock::now();
        {
            std::lock_guard<std::mutex> lk(mMutex);
            mTempErrors.push_back(te);
            noteBytesLocked(te.tag, te.transferred, te.total);
        }
        mCv.notify_all();
    }

    void onTransferFinish(::mega::MegaApi*,
                          ::mega::MegaTransfer* transfer,
                          ::mega::MegaError* error) override
    {
        if (!isUpload(transfer))
            return;
        Finish f;
        f.tag = transfer->getTag();
        f.code = error ? error->getErrorCode() : ::mega::MegaError::API_EINTERNAL;
        f.transferred = transfer->getTransferredBytes();
        f.total = transfer->getTotalBytes();
        f.ts = Clock::now();
        {
            std::lock_guard<std::mutex> lk(mMutex);
            mFinishes.emplace(f.tag, f); // first finish per tag wins
            noteBytesLocked(f.tag, f.transferred, f.total);
        }
        mCv.notify_all();
    }

    // ---- Grading -------------------------------------------------------------

    // Temp errors for `tag` that MEGAsync 6.7.0 cannot act on (see the file comment):
    // API_EOVERQUOTA, getValue()==0, !isForeignOverquota(), and no EVENT_STORAGE
    // RED/PAYWALL within ±kStorageEventWindow, nor a RED/PAYWALL app state (last
    // recorded EVENT_STORAGE at or before the error).
    std::vector<TempError> unactionableOwnOverquota(int tag) const
    {
        std::lock_guard<std::mutex> lk(mMutex);
        std::vector<TempError> out;
        for (const auto& te: mTempErrors)
        {
            if (te.tag != tag || te.code != ::mega::MegaError::API_EOVERQUOTA || te.value != 0 ||
                te.foreign)
                continue;
            if (!storageContextActionableLocked(te.ts))
                out.push_back(te);
        }
        return out;
    }

    // Every temp API_EOVERQUOTA for `tag` raised while transferred < total, in order.
    std::vector<TempError> overquotaTempErrorsBeforeAllBytes(int tag) const
    {
        std::lock_guard<std::mutex> lk(mMutex);
        std::vector<TempError> out;
        for (const auto& te: mTempErrors)
            if (te.tag == tag && te.code == ::mega::MegaError::API_EOVERQUOTA && te.total > 0 &&
                te.transferred < te.total)
                out.push_back(te);
        return out;
    }

    // The grading rule's storage context for an error raised at `ts`: a RED/PAYWALL
    // EVENT_STORAGE within ±kStorageEventWindow, or a RED/PAYWALL sticky app state
    // (last recorded EVENT_STORAGE at or before `ts`).
    bool isStorageContextActionable(Clock::time_point ts) const
    {
        std::lock_guard<std::mutex> lk(mMutex);
        return storageContextActionableLocked(ts);
    }

    // Whether a RED/PAYWALL EVENT_STORAGE reached the app within ±kStorageEventWindow
    // of `ts` (diagnostic companion of the grading rule).
    bool fullStorageEventNear(Clock::time_point ts) const
    {
        std::lock_guard<std::mutex> lk(mMutex);
        return fullStorageEventNearLocked(ts);
    }

    std::optional<Finish> finishOf(int tag) const
    {
        std::lock_guard<std::mutex> lk(mMutex);
        auto it = mFinishes.find(tag);
        if (it == mFinishes.end())
            return std::nullopt;
        return it->second;
    }

    std::optional<int> lastStorageEventNumber() const
    {
        std::lock_guard<std::mutex> lk(mMutex);
        if (mStorageEvents.empty())
            return std::nullopt;
        return mStorageEvents.back().number;
    }

    // Non-blocking: first EVENT_STORAGE with a number in `numbers` recorded at or
    // after `since`.
    std::optional<StorageEvent> storageEventSince(const std::set<int>& numbers,
                                                  Clock::time_point since) const
    {
        std::lock_guard<std::mutex> lk(mMutex);
        return storageEventSinceLocked(numbers, since);
    }

    // ---- Waiters (cv + bounded slices; MegaApi polls happen outside the lock) ----

    // Waits up to `timeout` for an EVENT_STORAGE whose number is in `numbers`,
    // recorded at or after `since` (events already recorded count).
    std::optional<StorageEvent> waitForStorageEvent(const std::set<int>& numbers,
                                                    std::chrono::milliseconds timeout,
                                                    Clock::time_point since) const
    {
        std::unique_lock<std::mutex> lk(mMutex);
        std::optional<StorageEvent> found;
        mCv.wait_for(lk,
                     timeout,
                     [&]
                     {
                         found = storageEventSinceLocked(numbers, since);
                         return found.has_value();
                     });
        return found;
    }

    bool waitForFinish(int tag, std::chrono::seconds timeout) const
    {
        std::unique_lock<std::mutex> lk(mMutex);
        return mCv.wait_for(lk,
                            timeout,
                            [&]
                            {
                                return mFinishes.count(tag) > 0;
                            });
    }

    // Waits until `tag` has transferred at least `minBytes` (or finished). Returns
    // false on timeout. Polls MegaApi::getTransferByTag as well as onTransferUpdate so
    // it does not depend on the update cadence.
    bool waitForBytesAtLeast(int tag, long long minBytes, std::chrono::seconds timeout)
    {
        const auto deadline = Clock::now() + timeout;
        for (;;)
        {
            pollTransfer(tag);
            std::unique_lock<std::mutex> lk(mMutex);
            auto reached = [&]
            {
                auto it = mProgress.find(tag);
                return mFinishes.count(tag) > 0 ||
                       (it != mProgress.end() && it->second.lastBytes >= minBytes);
            };
            if (reached())
                return true;
            if (Clock::now() >= deadline)
                return false;
            mCv.wait_for(lk, std::chrono::milliseconds(500), reached);
        }
    }

    // Progress-aware completion wait (SDK-6470 fu1: a fixed wall-clock budget flakes
    // on slow uplinks). Keeps waiting while the upload shows activity; returns
    // unfinished when there has been none for `noProgressLimit` (a wedge: e.g. an
    // indefinite hold), when the absolute safety `cap` is reached, or when `stopEarly`
    // (called outside the lock, about once per second) returns true. Activity is ANY
    // change of the app byte count or of the client-side WS progressCompleted (see
    // the file comment): the app count is clamped monotonic, so only the WS counter
    // shows a restart from 0; a restart loop is caught by the cap.
    WaitResult waitForFinishProgressAware(int tag,
                                          std::chrono::seconds noProgressLimit,
                                          std::chrono::seconds cap,
                                          const std::function<bool()>& stopEarly = nullptr)
    {
        const auto waitStart = Clock::now();
        for (;;)
        {
            pollTransfer(tag);
            pollWsProgress(tag);
            const bool stop = stopEarly && stopEarly();

            std::unique_lock<std::mutex> lk(mMutex);
            WaitResult r = resultLocked(tag);
            if (r.finished)
            {
                r.reason = "finished";
                return r;
            }
            if (stop)
            {
                r.reason = "stop-early condition met";
                return r;
            }
            const auto now = Clock::now();
            const auto& p = mProgress[tag];
            const auto anchor = std::max(waitStart, p.lastActivityTs);
            if (now - anchor >= noProgressLimit)
            {
                std::ostringstream os;
                os << "no byte progress for " << noProgressLimit.count() << "s (stalled at "
                   << r.lastBytes << "/" << r.total << " bytes; WS progressCompleted "
                   << p.wsProgress << ")";
                r.reason = os.str();
                return r;
            }
            if (now - waitStart >= cap)
            {
                std::ostringstream os;
                os << "safety cap " << cap.count() << "s reached while bytes still moved ("
                   << r.lastBytes << "/" << r.total << " bytes; WS progressCompleted "
                   << p.wsProgress << ")";
                r.reason = os.str();
                return r;
            }
            mCv.wait_for(lk,
                         std::chrono::seconds(1),
                         [&]
                         {
                             return mFinishes.count(tag) > 0;
                         });
        }
    }

    // One-line dump of everything recorded for `tag` plus the storage events, for
    // failure messages. Times are ms relative to the first recorded item.
    std::string describe(int tag) const
    {
        std::lock_guard<std::mutex> lk(mMutex);
        const auto origin = originLocked();
        auto rel = [&](Clock::time_point t)
        {
            return std::chrono::duration_cast<std::chrono::milliseconds>(t - origin).count();
        };
        std::ostringstream os;
        os << "[tag " << tag << "] tempErrors={";
        for (const auto& te: mTempErrors)
            if (te.tag == tag)
                os << " (code=" << te.code << " value=" << te.value
                   << " foreign=" << (te.foreign ? 1 : 0) << " bytes=" << te.transferred << "/"
                   << te.total << " t=" << rel(te.ts) << "ms)";
        os << " } finish=";
        if (auto it = mFinishes.find(tag); it != mFinishes.end())
            os << "(code=" << it->second.code << " bytes=" << it->second.transferred << "/"
               << it->second.total << " t=" << rel(it->second.ts) << "ms)";
        else
            os << "none";
        if (auto it = mProgress.find(tag); it != mProgress.end())
            os << " lastBytes=" << it->second.lastBytes << "/" << it->second.total
               << " wsProgressCompleted=" << it->second.wsProgress;
        os << " EVENT_STORAGE={";
        for (const auto& e: mStorageEvents)
            os << " (" << e.number << " t=" << rel(e.ts) << "ms)";
        os << " }";
        return os.str();
    }

private:
    struct Progress
    {
        std::string name; // MegaTransfer::getFileName(), the WS snapshot fallback key
        long long lastBytes = 0; // app-visible (clamped monotonic for WS)
        long long total = 0;
        long long wsProgress = -1; // client-side Transfer::progresscompleted; -1 = unseen
        Clock::time_point lastActivityTs{}; // last change of either counter
    };

    static bool isUpload(const ::mega::MegaTransfer* transfer)
    {
        return transfer && transfer->getType() == ::mega::MegaTransfer::TYPE_UPLOAD;
    }

    void pollTransfer(int tag)
    {
        if (!mApi)
            return;
        std::unique_ptr<::mega::MegaTransfer> t{mApi->getTransferByTag(tag)};
        if (!t)
            return;
        std::lock_guard<std::mutex> lk(mMutex);
        noteBytesLocked(tag, t->getTransferredBytes(), t->getTotalBytes());
    }

    // Samples the client-side WS progressCompleted of `tag` (matched by Transfer::tag,
    // else by file name). Dispatches onto the client thread: never under mMutex.
    void pollWsProgress(int tag)
    {
        if (!mApi)
            return;
        std::string name;
        {
            std::lock_guard<std::mutex> lk(mMutex);
            if (auto it = mProgress.find(tag); it != mProgress.end())
                name = it->second.name;
        }
        std::vector<WsUploadTransferSnapshot> snapshots;
        if (!fetchWsUploadTransferSnapshots(*mApi, snapshots, 5))
            return;
        const auto it =
            std::find_if(snapshots.begin(),
                         snapshots.end(),
                         [&](const WsUploadTransferSnapshot& s)
                         {
                             return s.tag == tag || (!name.empty() && s.fileName == name);
                         });
        if (it == snapshots.end())
            return;
        std::lock_guard<std::mutex> lk(mMutex);
        auto& p = mProgress[tag];
        const long long ws = static_cast<long long>(it->progressCompleted);
        if (p.wsProgress >= 0 && ws != p.wsProgress)
            p.lastActivityTs = Clock::now();
        p.wsProgress = ws;
    }

    void noteBytesLocked(int tag, long long bytes, long long total)
    {
        auto& p = mProgress[tag];
        if (bytes != p.lastBytes)
            p.lastActivityTs = Clock::now();
        p.lastBytes = bytes;
        if (total > 0)
            p.total = total;
    }

    WaitResult resultLocked(int tag) const
    {
        WaitResult r;
        if (auto it = mProgress.find(tag); it != mProgress.end())
        {
            r.lastBytes = it->second.lastBytes;
            r.total = it->second.total;
        }
        if (auto it = mFinishes.find(tag); it != mFinishes.end())
        {
            r.finished = true;
            r.code = it->second.code;
            r.lastBytes = it->second.transferred;
            r.total = it->second.total;
        }
        return r;
    }

    bool fullStorageEventNearLocked(Clock::time_point ts) const
    {
        for (const auto& e: mStorageEvents)
        {
            const auto delta = e.ts > ts ? e.ts - ts : ts - e.ts;
            if (isFullStorageState(e.number) && delta <= kStorageEventWindow)
                return true;
        }
        return false;
    }

    bool storageContextActionableLocked(Clock::time_point ts) const
    {
        if (fullStorageEventNearLocked(ts))
            return true;
        // Sticky app state: the last EVENT_STORAGE at or before the error.
        const StorageEvent* last = nullptr;
        for (const auto& e: mStorageEvents)
            if (e.ts <= ts)
                last = &e;
        return last && isFullStorageState(last->number);
    }

    std::optional<StorageEvent> storageEventSinceLocked(const std::set<int>& numbers,
                                                        Clock::time_point since) const
    {
        for (const auto& e: mStorageEvents)
            if (e.ts >= since && numbers.count(e.number))
                return e;
        return std::nullopt;
    }

    Clock::time_point originLocked() const
    {
        Clock::time_point origin = Clock::time_point::max();
        for (const auto& e: mStorageEvents)
            origin = std::min(origin, e.ts);
        for (const auto& te: mTempErrors)
            origin = std::min(origin, te.ts);
        for (const auto& f: mFinishes)
            origin = std::min(origin, f.second.ts);
        return origin == Clock::time_point::max() ? Clock::now() : origin;
    }

    ::mega::MegaApi* mApi = nullptr;
    mutable std::mutex mMutex;
    mutable std::condition_variable mCv;
    std::vector<StorageEvent> mStorageEvents;
    std::vector<TempError> mTempErrors;
    std::map<int, Finish> mFinishes;
    std::map<int, Progress> mProgress;
};

// One reading of the public queue-fit query (MegaApi::getWsUploadQueueQuotaFit). It is
// observational, serialised with the client thread by the sdkMutex, and unchanged by
// the fix: an own-account shortfall is still REPORTED there, it just no longer holds.
struct QueueQuotaFitSample
{
    int state = ::mega::MegaWsUploadQuotaFit::STATE_UNKNOWN;
    long long shortfallBytes = 0;
    bool foreign = false;
};

inline QueueQuotaFitSample sampleQueueQuotaFit(::mega::MegaApi& api)
{
    QueueQuotaFitSample out;
    if (std::unique_ptr<::mega::MegaWsUploadQuotaFit> fit{api.getWsUploadQueueQuotaFit()})
    {
        out.state = fit->getState();
        out.shortfallBytes = fit->getShortfallBytes();
        out.foreign = fit->isForeignShortfall();
    }
    return out;
}

// Polls (every 100 ms, up to `timeout`) until the queue-fit query reports a shortfall:
// STATE_SHORTFALL, isForeignShortfall() == expectForeign and, when expectedShortfallBytes
// > 0, exactly that many bytes (otherwise any positive amount). Returns the last sample.
inline QueueQuotaFitSample waitForQueueShortfall(::mega::MegaApi& api,
                                                 long long expectedShortfallBytes,
                                                 bool expectForeign,
                                                 std::chrono::milliseconds timeout)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    QueueQuotaFitSample s;
    for (;;)
    {
        s = sampleQueueQuotaFit(api);
        const bool amountOk = expectedShortfallBytes > 0 ?
                                  s.shortfallBytes == expectedShortfallBytes :
                                  s.shortfallBytes > 0;
        if (s.state == ::mega::MegaWsUploadQuotaFit::STATE_SHORTFALL &&
            s.foreign == expectForeign && amountOk)
            return s;
        if (std::chrono::steady_clock::now() >= deadline)
            return s;
        ::WaitMillisec(100);
    }
}

// The OWN-shortfall form (!isForeignShortfall()) of waitForQueueShortfall.
inline QueueQuotaFitSample waitForOwnQueueShortfall(::mega::MegaApi& api,
                                                    long long expectedShortfallBytes,
                                                    std::chrono::milliseconds timeout)
{
    return waitForQueueShortfall(api, expectedShortfallBytes, false, timeout);
}

} // namespace mega::test::wsupload
