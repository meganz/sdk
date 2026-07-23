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
    ++mLatestGen;
    mInFlight = false;
    mDirty = true;
}

bool UploadQuotaManager::shouldIssue() const
{
    return mDirty && !mInFlight;
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

    // P1: outstanding stays at its rebuild value (0). P3 populates outstanding
    // from the multi_transfers scan and recomputes mUnconstrained there; here it
    // is derived from remaining >= outstanding with outstanding as-is.
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

} // namespace ws
} // namespace mega

#endif // MEGA_USE_WSUPLOAD
