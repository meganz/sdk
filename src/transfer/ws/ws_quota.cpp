/**
 * @file src/transfer/ws/ws_quota.cpp
 * @brief ws::UploadQuotaManager bodies — the client-thread-only quota ledger.
 *
 * (c) 2026 by MEGA Privacy Kft, Csomad, Hungary
 *
 * This file is part of the MEGA SDK - Client Access Engine.
 *
 * Applications using the MEGA API must present a valid application key
 * and comply with the rules set forth in the Terms of Service.
 *
 * @copyright Simplified (2-clause) BSD License.
 */

#ifdef MEGA_USE_WSUPLOAD

#include "mega/transfer/ws/ws_quota.h"

#include "mega/logging.h"

namespace mega
{
namespace ws
{

void UploadQuotaManager::markDirty()
{
    mDirty = true;
}

void UploadQuotaManager::invalidate()
{
    // Bump the generation only when there is an in-flight reply or existing
    // balances to orphan; otherwise a storage-state transition on a fresh
    // session (no issue yet, no balances) would needlessly burn generation
    // numbers and desynchronise per-generation reply plans.
    if (mInFlight || mHaveBalances)
    {
        ++mLatestGen;
    }
    mInFlight = false;
    mDirty = true;
}

bool UploadQuotaManager::shouldIssue() const
{
    return mDirty && !mInFlight;
}

void UploadQuotaManager::consumeDirtyNoIssue()
{
    mDirty = false;
}

std::uint64_t UploadQuotaManager::beginIssue()
{
    ++mLatestGen;
    mInFlight = true;
    mDirty = false;
    return mLatestGen;
}

void UploadQuotaManager::endIssue(std::uint64_t gen)
{
    if (gen == mLatestGen)
    {
        mInFlight = false;
    }
}

bool UploadQuotaManager::applyGroups(std::uint64_t gen,
                                     const WsTfsGroupBalances& groups,
                                     const PoolClassifier& classify)
{
    if (gen != mLatestGen)
    {
        // Stale reply: leave all state untouched, the caller discards it.
        return false;
    }

    // Wholesale rebuild from the reply.
    mPools.clear();
    mPoolByFolder.clear();
    mPoolIndexByKey.clear();

    for (const auto& group: groups)
    {
        const m_off_t bytes = group.first;

        // Ledger-level fail-open mirror of the tfs parser contract: a malformed
        // entry (negative balance, or no folders) is skipped so it cannot corrupt
        // a pool. The parser already drops these for real server replies, but the
        // H2 test hook injects groups straight into applyGroups, and any future
        // direct caller must not be able to seed a pool with a negative remaining
        // (which would make every upload "exceed" it and produce false holds).
        if (bytes < 0 || group.second.empty())
        {
            continue;
        }

        for (const NodeHandle folder: group.second)
        {
            const std::pair<std::uint64_t, bool> classified = classify(folder);
            const std::uint64_t key = classified.first;
            const bool foreign = classified.second;

            std::size_t idx;
            const auto keyIt = mPoolIndexByKey.find(key);
            if (keyIt == mPoolIndexByKey.end())
            {
                idx = mPools.size();
                Pool pool;
                pool.remaining = bytes;
                pool.outstanding = 0;
                pool.foreign = foreign;
                mPools.push_back(pool);
                mPoolIndexByKey.emplace(key, idx);
            }
            else
            {
                idx = keyIt->second;
                // Conservative merge across reply entries sharing a pool.
                if (bytes < mPools[idx].remaining)
                {
                    mPools[idx].remaining = bytes;
                }
            }

            if (!mPoolByFolder.emplace(folder.as8byte(), idx).second)
            {
                LOG_warn << "WS quota: folder handle mapped to multiple pools; keeping first";
            }
        }
    }

    mHaveBalances = true;

    // Pre-accumulation default: outstanding is 0 after this wholesale rebuild, so
    // this derivation is trivially true. The client's evaluation scan rebuilds
    // real outstanding via the accumulation API immediately after apply and
    // recomputes mUnconstrained there.
    mUnconstrained = true;
    for (const Pool& pool: mPools)
    {
        if (pool.remaining < pool.outstanding)
        {
            mUnconstrained = false;
            break;
        }
    }

    return true;
}

void UploadQuotaManager::deductOnCompletion(NodeHandle folder, m_off_t size)
{
    const auto it = mPoolByFolder.find(folder.as8byte());
    if (it == mPoolByFolder.end())
    {
        return; // unknown folder: no-op
    }

    Pool& pool = mPools[it->second];
    pool.remaining -= size;
    if (pool.remaining < 0)
    {
        pool.remaining = 0;
    }
    pool.outstanding -= size;
    if (pool.outstanding < 0)
    {
        pool.outstanding = 0;
    }
}

m_off_t UploadQuotaManager::availableFor(NodeHandle folder) const
{
    const auto it = mPoolByFolder.find(folder.as8byte());
    if (it == mPoolByFolder.end())
    {
        return 0;
    }
    return mPools[it->second].remaining;
}

bool UploadQuotaManager::hasBalanceFor(NodeHandle folder) const
{
    return mPoolByFolder.find(folder.as8byte()) != mPoolByFolder.end();
}

bool UploadQuotaManager::isForeignGroup(NodeHandle folder) const
{
    const auto it = mPoolByFolder.find(folder.as8byte());
    return it != mPoolByFolder.end() && mPools[it->second].foreign;
}

std::uint64_t UploadQuotaManager::latestGen() const
{
    return mLatestGen;
}

bool UploadQuotaManager::haveBalances() const
{
    return mHaveBalances;
}

bool UploadQuotaManager::unconstrained() const
{
    return mUnconstrained;
}

bool UploadQuotaManager::evalPending() const
{
    return mEvalPending;
}

void UploadQuotaManager::setEvalPending(bool v)
{
    mEvalPending = v;
}

void UploadQuotaManager::beginOutstandingAccumulation()
{
    for (Pool& pool: mPools)
    {
        pool.outstanding = 0;
    }
}

void UploadQuotaManager::addOutstandingForTargets(const std::vector<NodeHandle>& folders,
                                                  m_off_t size)
{
    // Count `size` once per TARGET, NOT once per distinct pool. A multi-target
    // transfer uploads the bytes once but creates one NODE per target File
    // (File::completed -> one sendPutnodesOfUpload each), and quota is storage, not
    // bandwidth: N targets in one pool consume N * size of that pool. This mirrors
    // deductOnCompletion (also per File) and is what keeps the documented
    // `remaining >= outstanding` invariant true across completions — a per-pool
    // dedup credited 1x while completion debited Nx, so `unconstrained()` could
    // read true for a pool that was about to be overshot and the predictive hold
    // would silently fail to arm.
    for (const NodeHandle folder: folders)
    {
        const auto it = mPoolByFolder.find(folder.as8byte());
        if (it == mPoolByFolder.end())
        {
            continue; // unknown folder: not in any known pool
        }
        mPools[it->second].outstanding += size;
    }
}

void UploadQuotaManager::finishOutstandingAccumulation()
{
    mUnconstrained = true;
    for (const Pool& pool: mPools)
    {
        if (pool.remaining < pool.outstanding)
        {
            mUnconstrained = false;
            break;
        }
    }
}

void UploadQuotaManager::reset()
{
    mPools.clear();
    mPoolByFolder.clear();
    mPoolIndexByKey.clear();
    mHaveBalances = false;
    mUnconstrained = true;
    mEvalPending = false;
    mDirty = false;
    mInFlight = false;
    ++mLatestGen; // orphan any in-flight reply
}

WsQuotaQueueFit computeWsQuotaQueueFit(bool haveBalances, const std::vector<WsQuotaPoolFit>& pools)
{
    WsQuotaQueueFit fit;
    if (!haveBalances)
    {
        fit.state = WsQuotaQueueFit::State::Unknown; // fail-open: apps get "no data yet"
        return fit;
    }

    bool anyShort = false;
    for (const auto& p: pools)
    {
        const m_off_t over = p.sum - p.remaining;
        if (over > 0)
        {
            anyShort = true;
            fit.shortfallBytes += over;
            if (p.foreign)
                fit.foreignShortfall = true;
        }
    }
    fit.state = anyShort ? WsQuotaQueueFit::State::Shortfall : WsQuotaQueueFit::State::Fits;
    return fit;
}

} // namespace ws
} // namespace mega

#endif // MEGA_USE_WSUPLOAD
