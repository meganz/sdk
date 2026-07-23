/**
 * @file WsQuotaHookCaptures.h
 * @brief RAII installers for the SDK-6298 WS upload-quota test hooks (H1–H4, H6).
 *
 * Each capture class installs one `globalMegaTestHooks.onWsTfs*` /
 * `onWsQuota*` callback under `globalMegaTestHooks.mMutex` on construction and
 * clears it in the destructor, exactly mirroring the mechanics of
 * `WsChunkSendOverquotaCapture.h`. Waiters are condition-variable + predicate
 * based with an explicit timeout — never sleeps.
 *
 * Contract (from analysis/DESIGN_IMPL_SDK6298.md Q9): every hook fires on the
 * client thread with no engine lock held, so the callbacks below only take the
 * capture's own internal mutex; no re-entrancy into globalMegaTestHooks.mMutex.
 *
 *  - WsTfsIssuedCapture      -> H1 onWsTfsIssued(folders, gen)
 *  - WsTfsResultScript       -> H2 onWsTfsResult(gen, Error&, groups&, action&)
 *  - WsQuotaHoldChangedCapture-> H3 onWsQuotaHoldChanged(tag, held, foreign, avail)
 *  - WsTfsStaleDiscardedCapture-> H4 onWsTfsStaleDiscarded(staleGen, currentGen)
 *  - WsQuotaDeductedCapture   -> H6 onWsQuotaDeducted(folder, size, remainingAfter)
 *
 * Header-only / fully inline so consumers may include from multiple TUs without
 * ODR concerns. `::mega::` prefixes are used throughout (C++20 std::mega
 * ambiguity, feedback_cxx_standard_per_target.md).
 */

#pragma once

#include "mega/testhooks.h"
#include "mega/transfer/ws/ws_quota_types.h"
#include "mega/types.h"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

namespace mega::test::wsupload
{

// ----------------------------------------------------------------------------
// H1 — onWsTfsIssued(const std::vector<NodeHandle>& folders, uint64_t gen)
// Records every issuance (the queried folder set + generation) in order.
// ----------------------------------------------------------------------------
class WsTfsIssuedCapture
{
public:
    struct Issuance
    {
        std::vector<::mega::NodeHandle> folders;
        std::uint64_t gen = 0;
    };

    WsTfsIssuedCapture():
        mShared(std::make_shared<Shared>())
    {
        auto shared = mShared;
        std::lock_guard<std::mutex> g(::mega::globalMegaTestHooks.mMutex);
        ::mega::globalMegaTestHooks.onWsTfsIssued =
            [shared](const std::vector<::mega::NodeHandle>& folders, std::uint64_t gen)
        {
            std::lock_guard<std::mutex> lk(shared->m);
            shared->issuances.push_back(Issuance{folders, gen});
            shared->cv.notify_all();
        };
    }

    ~WsTfsIssuedCapture()
    {
        std::lock_guard<std::mutex> g(::mega::globalMegaTestHooks.mMutex);
        ::mega::globalMegaTestHooks.onWsTfsIssued = nullptr;
    }

    WsTfsIssuedCapture(const WsTfsIssuedCapture&) = delete;
    WsTfsIssuedCapture& operator=(const WsTfsIssuedCapture&) = delete;

    // Wait until at least `n` issuances have been recorded.
    bool waitForIssuance(std::size_t n, std::chrono::seconds timeout) const
    {
        std::unique_lock<std::mutex> lk(mShared->m);
        return mShared->cv.wait_for(lk,
                                    timeout,
                                    [&]
                                    {
                                        return mShared->issuances.size() >= n;
                                    });
    }

    // Convenience: the canonical first-behavioral-assert waiter (>= 1 issuance).
    bool waitForFire(std::chrono::seconds timeout) const
    {
        return waitForIssuance(1, timeout);
    }

    std::size_t issuanceCount() const
    {
        std::lock_guard<std::mutex> lk(mShared->m);
        return mShared->issuances.size();
    }

    std::vector<::mega::NodeHandle> foldersOfIssuance(std::size_t i) const
    {
        std::lock_guard<std::mutex> lk(mShared->m);
        if (i >= mShared->issuances.size())
            return {};
        return mShared->issuances[i].folders;
    }

    std::uint64_t genOfIssuance(std::size_t i) const
    {
        std::lock_guard<std::mutex> lk(mShared->m);
        if (i >= mShared->issuances.size())
            return 0;
        return mShared->issuances[i].gen;
    }

    // Deduped union of every queried folder handle across all issuances at index
    // >= fromIndex. Used by T11 (a follow-up tfs whose `n` includes the newly
    // enqueued folder).
    std::vector<::mega::NodeHandle> unionFoldersFrom(std::size_t fromIndex) const
    {
        std::lock_guard<std::mutex> lk(mShared->m);
        std::vector<::mega::NodeHandle> out;
        for (std::size_t i = fromIndex; i < mShared->issuances.size(); ++i)
        {
            for (const auto& h: mShared->issuances[i].folders)
            {
                bool present = false;
                for (const auto& e: out)
                {
                    if (e == h)
                    {
                        present = true;
                        break;
                    }
                }
                if (!present)
                    out.push_back(h);
            }
        }
        return out;
    }

private:
    struct Shared
    {
        std::mutex m;
        std::condition_variable cv;
        std::vector<Issuance> issuances;
    };

    std::shared_ptr<Shared> mShared;
};

// ----------------------------------------------------------------------------
// H2 — onWsTfsResult(gen, Error&, WsTfsGroupBalances&, WsTfsReplyAction&)
// Scriptable + observing. Fires after the gen-check, before apply; every ref is
// mutable so a test can forge API_OK + synthetic balances (server-independent),
// force an error, or impose Drop/Requeue. Also records the INCOMING groups (as
// parsed by the SDK) for passthrough/observe-only cells (T21).
// ----------------------------------------------------------------------------
class WsTfsResultScript
{
public:
    struct Plan
    {
        // If set, the reply's Error is overwritten with this before apply.
        std::optional<::mega::error> forcedError;
        // If set, the reply's parsed groups are replaced wholesale with these.
        std::optional<::mega::WsTfsGroupBalances> forcedGroups;
        // Action imposed on the reply.
        ::mega::WsTfsReplyAction action = ::mega::WsTfsReplyAction::Apply;
    };

    struct Observation
    {
        std::uint64_t gen = 0;
        ::mega::WsTfsGroupBalances incomingGroups;
    };

    // Default plan is observe-only (no forcedError, no forcedGroups, Apply).
    WsTfsResultScript():
        mShared(std::make_shared<Shared>())
    {
        auto shared = mShared;
        std::lock_guard<std::mutex> g(::mega::globalMegaTestHooks.mMutex);
        ::mega::globalMegaTestHooks.onWsTfsResult = [shared](std::uint64_t gen,
                                                             ::mega::Error& err,
                                                             ::mega::WsTfsGroupBalances& groups,
                                                             ::mega::WsTfsReplyAction& action)
        {
            Plan plan;
            {
                std::lock_guard<std::mutex> lk(shared->m);
                shared->observations.push_back(Observation{gen, groups});
                auto it = shared->genPlans.find(gen);
                plan = (it != shared->genPlans.end()) ? it->second : shared->defaultPlan;
                shared->cv.notify_all();
            }
            if (plan.forcedError)
                err.setErrorCode(*plan.forcedError);
            if (plan.forcedGroups)
                groups = *plan.forcedGroups;
            action = plan.action;
        };
    }

    ~WsTfsResultScript()
    {
        std::lock_guard<std::mutex> g(::mega::globalMegaTestHooks.mMutex);
        ::mega::globalMegaTestHooks.onWsTfsResult = nullptr;
    }

    WsTfsResultScript(const WsTfsResultScript&) = delete;
    WsTfsResultScript& operator=(const WsTfsResultScript&) = delete;

    // Thread-safe reconfigure between phases.
    void setDefaultPlan(const Plan& p)
    {
        std::lock_guard<std::mutex> lk(mShared->m);
        mShared->defaultPlan = p;
    }

    void setPlanForGen(std::uint64_t gen, const Plan& p)
    {
        std::lock_guard<std::mutex> lk(mShared->m);
        mShared->genPlans[gen] = p;
    }

    void clearGenPlans()
    {
        std::lock_guard<std::mutex> lk(mShared->m);
        mShared->genPlans.clear();
    }

    bool waitForResult(std::size_t n, std::chrono::seconds timeout) const
    {
        std::unique_lock<std::mutex> lk(mShared->m);
        return mShared->cv.wait_for(lk,
                                    timeout,
                                    [&]
                                    {
                                        return mShared->observations.size() >= n;
                                    });
    }

    std::size_t resultCount() const
    {
        std::lock_guard<std::mutex> lk(mShared->m);
        return mShared->observations.size();
    }

    Observation observationAt(std::size_t i) const
    {
        std::lock_guard<std::mutex> lk(mShared->m);
        if (i >= mShared->observations.size())
            return {};
        return mShared->observations[i];
    }

    // ---- Plan factories (readability) --------------------------------------

    static Plan observeOnly()
    {
        return Plan{};
    }

    // One quota group of `bytes` writable, shared by `folders`. Any folder not
    // present in a group is "no data" downstream (fail-open, never held).
    static Plan singleGroup(::m_off_t bytes, std::vector<::mega::NodeHandle> folders)
    {
        Plan p;
        p.forcedGroups = ::mega::WsTfsGroupBalances{{bytes, std::move(folders)}};
        return p;
    }

    // Effectively-unbounded balance for `folders` (nothing ever held).
    static Plan generous(std::vector<::mega::NodeHandle> folders)
    {
        return singleGroup(::m_off_t{1} << 42, std::move(folders)); // 4 TiB
    }

    // Arbitrary caller-built groups (garbage/partial/negative cases).
    static Plan withGroups(::mega::WsTfsGroupBalances groups)
    {
        Plan p;
        p.forcedGroups = std::move(groups);
        return p;
    }

    static Plan drop()
    {
        Plan p;
        p.action = ::mega::WsTfsReplyAction::Drop;
        return p;
    }

    static Plan requeue(::mega::WsTfsGroupBalances groups)
    {
        Plan p;
        p.forcedGroups = std::move(groups);
        p.action = ::mega::WsTfsReplyAction::Requeue;
        return p;
    }

    static Plan forceError(::mega::error e)
    {
        Plan p;
        p.forcedError = e;
        return p;
    }

private:
    struct Shared
    {
        std::mutex m;
        std::condition_variable cv;
        Plan defaultPlan;
        std::map<std::uint64_t, Plan> genPlans;
        std::vector<Observation> observations;
    };

    std::shared_ptr<Shared> mShared;
};

// ----------------------------------------------------------------------------
// H3 — onWsQuotaHoldChanged(int tag, bool held, bool foreign, m_off_t avail)
// Ordered log of every hold/release transition.
// ----------------------------------------------------------------------------
class WsQuotaHoldChangedCapture
{
public:
    struct Event
    {
        int tag = -1;
        bool held = false;
        bool foreign = false;
        ::m_off_t availableBytes = 0;
    };

    WsQuotaHoldChangedCapture():
        mShared(std::make_shared<Shared>())
    {
        auto shared = mShared;
        std::lock_guard<std::mutex> g(::mega::globalMegaTestHooks.mMutex);
        ::mega::globalMegaTestHooks.onWsQuotaHoldChanged =
            [shared](int tag, bool held, bool foreign, ::m_off_t avail)
        {
            std::lock_guard<std::mutex> lk(shared->m);
            shared->events.push_back(Event{tag, held, foreign, avail});
            shared->cv.notify_all();
        };
    }

    ~WsQuotaHoldChangedCapture()
    {
        std::lock_guard<std::mutex> g(::mega::globalMegaTestHooks.mMutex);
        ::mega::globalMegaTestHooks.onWsQuotaHoldChanged = nullptr;
    }

    WsQuotaHoldChangedCapture(const WsQuotaHoldChangedCapture&) = delete;
    WsQuotaHoldChangedCapture& operator=(const WsQuotaHoldChangedCapture&) = delete;

    // Wait until a held==true event has been seen for `tag`.
    bool waitForHold(int tag, std::chrono::seconds timeout) const
    {
        std::unique_lock<std::mutex> lk(mShared->m);
        return mShared->cv.wait_for(lk,
                                    timeout,
                                    [&]
                                    {
                                        return heldSeenLocked(tag);
                                    });
    }

    // Wait until a release (held==false after a prior held==true) for `tag`.
    bool waitForRelease(int tag, std::chrono::seconds timeout) const
    {
        std::unique_lock<std::mutex> lk(mShared->m);
        return mShared->cv.wait_for(lk,
                                    timeout,
                                    [&]
                                    {
                                        return releasedLocked(tag);
                                    });
    }

    int countFor(int tag) const
    {
        std::lock_guard<std::mutex> lk(mShared->m);
        int n = 0;
        for (const auto& e: mShared->events)
            if (e.tag == tag)
                ++n;
        return n;
    }

    int heldCountFor(int tag) const
    {
        std::lock_guard<std::mutex> lk(mShared->m);
        int n = 0;
        for (const auto& e: mShared->events)
            if (e.tag == tag && e.held)
                ++n;
        return n;
    }

    bool sawHold(int tag) const
    {
        std::lock_guard<std::mutex> lk(mShared->m);
        return heldSeenLocked(tag);
    }

    // First held==true event for `tag` (for foreign / availableBytes asserts).
    std::optional<Event> firstHold(int tag) const
    {
        std::lock_guard<std::mutex> lk(mShared->m);
        for (const auto& e: mShared->events)
            if (e.tag == tag && e.held)
                return e;
        return std::nullopt;
    }

    std::vector<Event> eventsFor(int tag) const
    {
        std::lock_guard<std::mutex> lk(mShared->m);
        std::vector<Event> out;
        for (const auto& e: mShared->events)
            if (e.tag == tag)
                out.push_back(e);
        return out;
    }

    std::size_t totalEvents() const
    {
        std::lock_guard<std::mutex> lk(mShared->m);
        return mShared->events.size();
    }

private:
    struct Shared
    {
        std::mutex m;
        std::condition_variable cv;
        std::vector<Event> events;
    };

    bool heldSeenLocked(int tag) const
    {
        for (const auto& e: mShared->events)
            if (e.tag == tag && e.held)
                return true;
        return false;
    }

    bool releasedLocked(int tag) const
    {
        bool sawHeld = false;
        for (const auto& e: mShared->events)
        {
            if (e.tag != tag)
                continue;
            if (e.held)
                sawHeld = true;
            else if (sawHeld)
                return true;
        }
        return false;
    }

    std::shared_ptr<Shared> mShared;
};

// ----------------------------------------------------------------------------
// H4 — onWsTfsStaleDiscarded(uint64_t staleGen, uint64_t currentGen)
// ----------------------------------------------------------------------------
class WsTfsStaleDiscardedCapture
{
public:
    struct Record
    {
        std::uint64_t staleGen = 0;
        std::uint64_t currentGen = 0;
    };

    WsTfsStaleDiscardedCapture():
        mShared(std::make_shared<Shared>())
    {
        auto shared = mShared;
        std::lock_guard<std::mutex> g(::mega::globalMegaTestHooks.mMutex);
        ::mega::globalMegaTestHooks.onWsTfsStaleDiscarded =
            [shared](std::uint64_t staleGen, std::uint64_t currentGen)
        {
            std::lock_guard<std::mutex> lk(shared->m);
            shared->records.push_back(Record{staleGen, currentGen});
            shared->cv.notify_all();
        };
    }

    ~WsTfsStaleDiscardedCapture()
    {
        std::lock_guard<std::mutex> g(::mega::globalMegaTestHooks.mMutex);
        ::mega::globalMegaTestHooks.onWsTfsStaleDiscarded = nullptr;
    }

    WsTfsStaleDiscardedCapture(const WsTfsStaleDiscardedCapture&) = delete;
    WsTfsStaleDiscardedCapture& operator=(const WsTfsStaleDiscardedCapture&) = delete;

    bool waitForFire(std::chrono::seconds timeout) const
    {
        std::unique_lock<std::mutex> lk(mShared->m);
        return mShared->cv.wait_for(lk,
                                    timeout,
                                    [&]
                                    {
                                        return !mShared->records.empty();
                                    });
    }

    std::size_t count() const
    {
        std::lock_guard<std::mutex> lk(mShared->m);
        return mShared->records.size();
    }

    Record recordAt(std::size_t i) const
    {
        std::lock_guard<std::mutex> lk(mShared->m);
        if (i >= mShared->records.size())
            return {};
        return mShared->records[i];
    }

private:
    struct Shared
    {
        std::mutex m;
        std::condition_variable cv;
        std::vector<Record> records;
    };

    std::shared_ptr<Shared> mShared;
};

// ----------------------------------------------------------------------------
// H6 — onWsQuotaDeducted(NodeHandle folder, m_off_t size, m_off_t remainingAfter)
// ----------------------------------------------------------------------------
class WsQuotaDeductedCapture
{
public:
    struct Record
    {
        ::mega::NodeHandle folder;
        ::m_off_t size = 0;
        ::m_off_t remainingAfter = 0;
    };

    WsQuotaDeductedCapture():
        mShared(std::make_shared<Shared>())
    {
        auto shared = mShared;
        std::lock_guard<std::mutex> g(::mega::globalMegaTestHooks.mMutex);
        ::mega::globalMegaTestHooks.onWsQuotaDeducted =
            [shared](::mega::NodeHandle folder, ::m_off_t size, ::m_off_t remainingAfter)
        {
            std::lock_guard<std::mutex> lk(shared->m);
            shared->records.push_back(Record{folder, size, remainingAfter});
            shared->cv.notify_all();
        };
    }

    ~WsQuotaDeductedCapture()
    {
        std::lock_guard<std::mutex> g(::mega::globalMegaTestHooks.mMutex);
        ::mega::globalMegaTestHooks.onWsQuotaDeducted = nullptr;
    }

    WsQuotaDeductedCapture(const WsQuotaDeductedCapture&) = delete;
    WsQuotaDeductedCapture& operator=(const WsQuotaDeductedCapture&) = delete;

    bool waitForFire(std::chrono::seconds timeout) const
    {
        std::unique_lock<std::mutex> lk(mShared->m);
        return mShared->cv.wait_for(lk,
                                    timeout,
                                    [&]
                                    {
                                        return !mShared->records.empty();
                                    });
    }

    std::size_t count() const
    {
        std::lock_guard<std::mutex> lk(mShared->m);
        return mShared->records.size();
    }

    int countForFolder(::mega::NodeHandle folder) const
    {
        std::lock_guard<std::mutex> lk(mShared->m);
        int n = 0;
        for (const auto& r: mShared->records)
            if (r.folder == folder)
                ++n;
        return n;
    }

    Record recordAt(std::size_t i) const
    {
        std::lock_guard<std::mutex> lk(mShared->m);
        if (i >= mShared->records.size())
            return {};
        return mShared->records[i];
    }

private:
    struct Shared
    {
        std::mutex m;
        std::condition_variable cv;
        std::vector<Record> records;
    };

    std::shared_ptr<Shared> mShared;
};

} // namespace mega::test::wsupload
