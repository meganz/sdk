/**
 * @file mega/transfer_quota_checker.h
 * @brief Evaluate the account's transfer-quota (download bandwidth) traffic light
 *
 * (c) 2026 by Mega Limited, Auckland, New Zealand
 *
 * This file is part of the MEGA SDK - Client Access Engine.
 *
 * Applications using the MEGA API must present a valid application key
 * and comply with the the rules set forth in the Terms of Service.
 *
 * The MEGA SDK is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * @copyright Simplified (2-clause) BSD License.
 *
 * You should have received a copy of the license along with this
 * program.
 */

#ifndef MEGA_TRANSFER_QUOTA_CHECKER_H
#define MEGA_TRANSFER_QUOTA_CHECKER_H 1

#include "mega/types.h"

#include <functional>
#include <vector>

namespace mega
{

class MegaClient;
struct AccountDetails;

// Evaluates the transfer-quota traffic light from the queued downloads and streaming reads in
// flight, by asking the API (`qbq`) about them.
class TransferQuotaChecker
{
public:
    explicit TransferQuotaChecker(MegaClient& client):
        mClient(client)
    {}

    // Stream checks are debounced: an idle check is answered at once (`qbq` for free, the snapshot
    // for Pro) and starts a one-second timer. Checks while it runs are folded into one when it
    // expires, which restarts it; only the latest of them has its completion run. None means back
    // to idle.
    enum class CheckOrigin
    {
        Default,
        Stream,
    };

    // Nodes still to download, split as `qbq` takes them, with the bytes already fetched.
    struct DownloadProjection
    {
        // Split as `qbq` takes them; bare handles, since a tagged one is not a node id it accepts
        std::vector<handle> ownNodes;
        std::vector<handle> publicNodes;

        // Bytes of those nodes already downloaded
        m_off_t transferred = 0;
    };

    // For free/feature accounts, evaluate the state now for nodes that are about to start
    // downloading. For Pro accounts, fetch the current transfer quota snapshot from the server.
    // Cached 10 minutes.
    //
    // The completion also receives the query's own error, so a caller can tell a failed query from
    // one that answered but could not determine the state -- both give TRANSFER_QUOTA_UNKNOWN.
    //
    // False when nothing was asked or will be: completion is not run. A folded Stream check returns
    // true but may still drop its completion: superseded, or no streams left at expiry.
    bool check(std::vector<handle> newOwnNodes,
               std::vector<handle> newPublicNodes,
               std::function<void(transferquotastatus_t, Error)> completion,
               CheckOrigin origin = CheckOrigin::Default);

    // Reset to the just-logged-in state, on session (re)init.
    void reset();

    // A paid plan with a fixed transfer allowance (excludes feature-only plans like VPN/PWM).
    bool isProAccount() const;

    // A Pro account's transfer-quota snapshot: the traffic light plus the allowance figures it was
    // derived from (all 0 when never polled). Lets the caller surface the same transfer fields as
    // getAccountDetails without a fresh server round-trip.
    struct ProTransferQuota
    {
        transferquotastatus_t status = TRANSFER_QUOTA_UNKNOWN;
        m_off_t max = 0;
        m_off_t ownUsed = 0;
        m_off_t srvUsed = 0;

        // The per-IP window as the reply carried it, oldest to newest. Counts towards the used
        // bytes like the two above, and its record count is the window length apps read.
        std::vector<m_off_t> hist;
    };

    // Pro-account transfer quota, cache-first: the last polled snapshot, or a query when the
    // periodic poll has yet to settle one.
    void getProTransferState(std::function<void(ProTransferQuota, Error)> completion);

    // Cache the Pro traffic light and allowance figures from a settled `uq` transfer reply,
    // whichever path fetched it. Client-thread only.
    void cacheStatusFromDetails(const AccountDetails& details);

    // Traffic light last reported to the app.
    transferquotastatus_t status() const
    {
        return mStatus;
    }

    // Traffic light for a used/max ratio.
    static transferquotastatus_t statusForUsage(m_off_t used, m_off_t max);

    // Traffic light for an account's transfer figures, from their ratio alone.
    static transferquotastatus_t statusForUsage(const AccountDetails& details);

    // Traffic light for a `qbq` reply code.
    static transferquotastatus_t statusForApiCode(int code);

    // Append the streams in flight to a projection. Public, for unit tests. Client thread only:
    // reads hdrns under the exec() sdkMutex.
    void projectStreamDownloads(DownloadProjection& projection) const;

    // Append the queued download transfers to a projection. Split from the streams so a check that
    // skips need not walk the download queue. Public, for unit tests. Client thread only: reads
    // transferlist under the exec() sdkMutex.
    void projectTransferDownloads(DownloadProjection& projection) const;

private:
    // Pro path: the allowance snapshot is the state
    void checkProAccount(std::function<void(transferquotastatus_t, Error)> completion);

    // Free/feature path: ask `qbq` about the nodes starting now plus everything already in
    // flight, and hand its verdict to @p completion. False when nothing was asked, as check().
    bool checkFreeAccount(std::vector<handle> newOwnNodes,
                          std::vector<handle> newPublicNodes,
                          std::function<void(transferquotastatus_t, Error)> completion);

    // Stream path of check(): answers now when idle, else folds into the timer's check.
    bool checkStreamThrottled(std::function<void(transferquotastatus_t, Error)> completion);

    // Answer a stream check now (`qbq` or the Pro snapshot) and start the timer. False when nothing
    // was asked.
    bool queryStream(std::function<void(transferquotastatus_t, Error)> completion);

    // Answer the folded stream check, or go idle when nothing was folded.
    void onStreamTimerExpired();

    // Issue a `uq` transfer query; on reply derive and cache the snapshot, then run completion.
    void getProTransferQuota(std::function<void(ProTransferQuota, Error)> completion);

    // The cached Pro snapshot (mStatus plus the last-polled allowance figures).
    ProTransferQuota cachedProQuota() const;

    // Drop the Pro snapshot. Left behind across a plan change, its figures would describe the
    // plan that is gone.
    void forgetProSnapshot()
    {
        mProTransferMax = 0;
        mProTransferOwnUsed = 0;
        mProTransferSrvUsed = 0;
        mProTransferHist.clear();
        mProSnapshotLevel = ACCOUNT_TYPE_UNKNOWN;
    }

    MegaClient& mClient;

    // Only for Pro accounts: the traffic light last evaluated.
    transferquotastatus_t mStatus = TRANSFER_QUOTA_UNKNOWN;

    dstime mProAccountCacheValidUntilDs = 0;

    // A `uq` poll for a Pro account is awaiting its reply: don't stack another
    bool mProQueryInFlight = false;

    // Callers waiting on that one reply. They are dropped, not run, on reset(): their captures may
    // not outlive the session.
    std::vector<std::function<void(ProTransferQuota, Error)>> mPendingProPolls;

    // A MegaClient::bttimers entry, started when a stream check is answered or sent, not when its
    // reply arrives.
    // Owned there: locallogout drops it alongside reset().
    bool mStreamTimerRunning = false;

    // A stream check arrived while the timer ran: its expiry owes a check
    bool mStreamCheckPending = false;
    std::function<void(transferquotastatus_t, Error)> mPendingStreamCompletion;

    // Transfer allowance figures from the last Pro `uq` reply, cached alongside mStatus so
    // getProTransferState can return them without another round-trip.
    m_off_t mProTransferMax = 0;
    m_off_t mProTransferOwnUsed = 0;
    m_off_t mProTransferSrvUsed = 0;

    // The per-IP window at that reply, kept whole so a served snapshot reports the same used bytes
    // and the same window length as the reply it came from.
    std::vector<m_off_t> mProTransferHist;

    // The plan the snapshot describes. A different one now means its allowance figures are those
    // of the old plan, so the snapshot is not servable however fresh its window is.
    AccountType mProSnapshotLevel = ACCOUNT_TYPE_UNKNOWN;
};

} // namespace mega

#endif // MEGA_TRANSFER_QUOTA_CHECKER_H
