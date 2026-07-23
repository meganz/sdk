/**
 * @file mega/transfer/ws/ws_quota.h
 * @brief Declaration of ws::UploadQuotaManager — the client-thread-only quota
 *        ledger used by the websocket-upload engine.
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

#ifndef MEGA_TRANSFER_WS_WS_QUOTA_H
#define MEGA_TRANSFER_WS_WS_QUOTA_H 1

#ifdef MEGA_USE_WSUPLOAD

#include "mega/transfer/ws/ws_quota_types.h"
#include "mega/types.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <unordered_map>
#include <utility>
#include <vector>

namespace mega
{
namespace ws
{

// Predictive, client-thread-only quota ledger for websocket uploads.
//
// Threading: EVERY method must be called on the MegaClient (client) thread. The
// class owns no mutex — all of its inputs (enqueue, "tfs" replies, upload
// completions, usl changes) already arrive on that thread, so client-side
// ownership needs no locking. It never touches MegaClient, the upload engine or
// multi_transfers; it is a pure ledger that a thin client-side layer drives.
//
// Balance model: the "tfs" reply returns one group per queried folder. Folders
// that share a quota pool (all own-account roots share one pool; each inshare
// owner and each folder-link root is its own pool) are MERGED into a single
// internal Pool, keyed by an opaque pool key supplied by the caller's
// classifier. Merging takes the minimum reported balance across the merged
// entries (conservative). availableFor()/hasBalanceFor()/isForeignGroup() then
// answer per merged pool, and deductOnCompletion() debits the merged pool, so
// two folders sharing a pool see each other's deductions.
class UploadQuotaManager
{
public:
    // Maps a folder handle to (poolKey, foreign). Folders sharing a poolKey form
    // one pool; foreign marks a pool the local user does not own (inshare/link).
    using PoolClassifier = std::function<std::pair<std::uint64_t, bool>(NodeHandle)>;

    // Coalescing / generation control: dirty-flag + in-flight guard + monotonic
    // generation counter (stale-reply discard).
    void markDirty();
    void invalidate();
    bool shouldIssue() const;
    std::uint64_t beginIssue();

    // Note the reply for generation `gen` arrived (success OR error), clearing the
    // in-flight guard so the next flush may issue again. Gen-guarded: a stale reply
    // for an orphaned generation must not clear a newer issue's in-flight flag.
    // Call before applyGroups(); error replies call only this.
    void endIssue(std::uint64_t gen);

    // Apply a "tfs" reply captured for generation `gen`. Returns false and
    // leaves all state untouched when `gen` is stale (no longer the latest);
    // otherwise rebuilds the ledger wholesale and returns true. `classify` maps
    // each reply folder handle to its (poolKey, foreign); entries whose folders
    // share a poolKey merge into one pool taking the minimum reported balance.
    // On duplicate folder handles the first mapping wins (a warning is logged).
    bool applyGroups(std::uint64_t gen,
                     const WsTfsGroupBalances& groups,
                     const PoolClassifier& classify);

    // Debit `size` from the merged pool owning `folder` (both remaining and
    // outstanding, each clamped at 0). Unknown folder: no-op.
    void deductOnCompletion(NodeHandle folder, m_off_t size);

    // Queries. availableFor() returns the merged pool's remaining balance, or 0
    // when the folder has no known balance — callers MUST consult hasBalanceFor()
    // to distinguish an unconstrained folder (fail-open, never held) from a
    // genuinely-exhausted pool.
    m_off_t availableFor(NodeHandle folder) const;
    bool hasBalanceFor(NodeHandle folder) const;
    bool isForeignGroup(NodeHandle folder) const;

    std::uint64_t latestGen() const;
    bool haveBalances() const;

    // Clear all balances and orphan any in-flight reply (bumps the generation).
    void reset();

private:
    struct Pool
    {
        m_off_t remaining{0};
        m_off_t outstanding{0};
        bool foreign{false};
    };

    std::vector<Pool> mPools;
    // Folder handle (NodeHandle::as8byte) -> index into mPools. Keyed by the raw
    // 6-byte handle because NodeHandle has no std::hash at this (core SDK) layer.
    std::unordered_map<handle, std::size_t> mPoolByFolder;
    // Opaque pool key -> index into mPools (entry-merge lookup).
    std::unordered_map<std::uint64_t, std::size_t> mPoolIndexByKey;

    bool mHaveBalances{false};
    bool mUnconstrained{true};
    bool mEvalPending{false};
    bool mDirty{false};
    bool mInFlight{false};
    std::uint64_t mLatestGen{0};
};

} // namespace ws
} // namespace mega

#endif // MEGA_USE_WSUPLOAD

#endif // MEGA_TRANSFER_WS_WS_QUOTA_H
