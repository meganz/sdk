/**
 * @file transfer_quota_checker.cpp
 * @brief Evaluate the transfer-quota (download bandwidth) traffic light
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

#include "mega/transfer_quota_checker.h"

#include "mega/backofftimer.h"
#include "mega/command.h"
#include "mega/logging.h"
#include "mega/megaapp.h"
#include "mega/megaclient.h"
#include "mega/scoped_helpers.h"
#include "mega/transfer.h"
#include "mega/types.h"
#include "mega/waiter.h"

#include <algorithm>
#include <cstring>

namespace mega
{

// Ten minutes between Pro-account `uq` polls.
static constexpr dstime PRO_CHECK_INTERVAL_DS = 10 * 60 * 10;

// One-second debounce window for stream-triggered checks.
static constexpr dstime STREAM_CHECK_WINDOW_DS = 10;

transferquotastatus_t TransferQuotaChecker::statusForUsage(m_off_t used, m_off_t max)
{
    // Accounts without an allowance have a per-IP quota instead, which no ratio can describe
    if (max <= 0)
    {
        return TRANSFER_QUOTA_UNKNOWN;
    }

    // Compared before dividing rather than flooring a percentage, so the last percent is not read
    // as one step: 99.01% used is already red, where flooring would call it 99 and orange.
    const m_off_t consumed = std::min(used, max);

    if (consumed * 100 > max * 99)
    {
        return TRANSFER_QUOTA_RED;
    }

    return consumed * 100 >= max * 90 ? TRANSFER_QUOTA_ORANGE : TRANSFER_QUOTA_GREEN;
}

transferquotastatus_t TransferQuotaChecker::statusForUsage(const AccountDetails& details)
{
    return statusForUsage(details.transferUsed(), details.transfer_max);
}

transferquotastatus_t TransferQuotaChecker::statusForApiCode(int code)
{
    // The API answers on its own scale: 0 sufficient quota, 1 the unregistered-user allowance would
    // be nearly exhausted, 2 the registered-user one would be, 3 not enough or quota exceeded
    switch (code)
    {
        case 0:
            return TRANSFER_QUOTA_GREEN;
        case 1:
        case 2:
            return TRANSFER_QUOTA_ORANGE;
        case 3:
            return TRANSFER_QUOTA_RED;
        default: // an error code, so the state could not be determined
            return TRANSFER_QUOTA_UNKNOWN;
    }
}

// Strip any type tag (encodeHandleType) so only the node handle goes into n/p.
static handle bareNodeHandle(handle h)
{
    handle node = 0;
    std::memcpy(&node, &h, MegaClient::NODEHANDLE);
    return node;
}

// `qbq` counts each occurrence, so a node both downloaded and streamed contributes twice: every
// transfer and every stream is accumulated on its own, never merged by node handle.
static void accumulateConsumption(TransferQuotaChecker::DownloadProjection& projection,
                                  const handle node,
                                  const bool isPublic,
                                  const m_off_t size,
                                  const m_off_t done)
{
    // Clamp: progress can transiently exceed the size, which would take bytes off the total.
    const m_off_t downloaded = std::min(std::max<m_off_t>(0, done), size);

    projection.transferred += downloaded;
    (isPublic ? projection.publicNodes : projection.ownNodes).push_back(node);
}

void TransferQuotaChecker::projectStreamDownloads(DownloadProjection& projection) const
{
    // Client thread only: hdrns is read unlocked, safe under the exec() sdkMutex.

    // Charge the whole node, not the requested range: a download projects its whole file too, and
    // `qbq` charges by node handle regardless.
    for (const auto& hdrnPair: mClient.hdrns)
    {
        const DirectReadNode* drn = hdrnPair.second;

        // A node whose reads have all finished lingers here until its scheduled expiry, minutes
        // later. Nothing is pending on it, and its progress died with the reads, so it would be
        // projected at full size for that whole window.
        if (!drn || drn->reads.empty())
        {
            continue;
        }

        // Only the reads still in flight: a finished one is deleted, taking its progress with it.
        // Overlapping reads can sum past what was distinctly fetched; accumulate bounds it.
        m_off_t downloaded = 0;
        for (const DirectRead* dr: drn->reads)
        {
            downloaded += dr->progress;
        }

        accumulateConsumption(projection,
                              bareNodeHandle(drn->h),
                              drn->isPublicHandle,
                              drn->size,
                              downloaded);
    }
}

void TransferQuotaChecker::projectTransferDownloads(DownloadProjection& projection) const
{
    // Every GET transfer, active or queued: an active one keeps its place in the list until it is
    // destroyed, so this single list covers both.
    for (Transfer* t: mClient.transferlist.transfers[GET])
    {
        // Files naming different nodes share a transfer when their content matches (startxfer
        // merges by meta-MAC), and its bytes are then fetched once and copied locally. So count the
        // transfer once, through whichever of its files is first.
        const File* file = t->files.empty() ? nullptr : t->files.front();
        if (!file || file->h.isUndef())
        {
            continue;
        }

        // Private handle -> own node (`n`), public handle -> link (`p`)
        accumulateConsumption(projection,
                              bareNodeHandle(file->h.as8byte()),
                              !file->hprivate,
                              t->size,
                              t->progresscompleted);
    }
}

bool TransferQuotaChecker::isProAccount() const
{
    return mClient.mMyAccount.isProPlan();
}

bool TransferQuotaChecker::check(std::vector<handle> newOwnNodes,
                                 std::vector<handle> newPublicNodes,
                                 std::function<void(transferquotastatus_t, Error)> completion,
                                 CheckOrigin origin)
{
    // Stream nodes are projected from hdrns: the node lists are not used
    if (origin == CheckOrigin::Stream)
    {
        return checkStreamThrottled(std::move(completion));
    }

    // A Pro account's state is its allowance ratio, which no `qbq` verdict describes, so the nodes
    // starting now say nothing about it: the snapshot is the answer.
    if (isProAccount())
    {
        checkProAccount(std::move(completion));
        return true;
    }

    return checkFreeAccount(std::move(newOwnNodes),
                            std::move(newPublicNodes),
                            std::move(completion));
}

bool TransferQuotaChecker::checkFreeAccount(
    std::vector<handle> newOwnNodes,
    std::vector<handle> newPublicNodes,
    std::function<void(transferquotastatus_t, Error)> completion)
{
    // What is already being consumed, so the verdict covers the whole picture rather than only
    // the nodes starting now.
    DownloadProjection projection;
    projectStreamDownloads(projection);
    projectTransferDownloads(projection);

    // Nodes that have not started contribute no transferred bytes, so only their handles are
    // needed: `qbq` charges by handle and takes the adjustment as one scalar.
    // Do not need to de-duplicate nodes.
    projection.ownNodes.insert(projection.ownNodes.end(), newOwnNodes.begin(), newOwnNodes.end());
    projection.publicNodes.insert(projection.publicNodes.end(),
                                  newPublicNodes.begin(),
                                  newPublicNodes.end());

    if (projection.ownNodes.empty() && projection.publicNodes.empty())
    {
        return false;
    }

    auto onReply = [this, completion = std::move(completion)](int code)
    {
        // The account gained an allowance while this was in flight, so this verdict describes a
        // per-IP quota it no longer has. Report it as undetermined rather than as its state.
        const transferquotastatus_t state =
            isProAccount() ? TRANSFER_QUOTA_UNKNOWN : statusForApiCode(code);

        if (completion)
        {
            // Only a negative code is an error: 0 is the API answering that there is quota
            completion(state, code < 0 ? static_cast<error>(code) : API_OK);
        }
    };

    // Internal query: a borrowed tag could resolve against an unrelated app request. Scoped, so a
    // throwing queueCommand cannot leave it at 0 and have nextreqtag() reissue live tags.
    const auto scopedReqtag = makeScopedValue(mClient.reqtag, 0);

    mClient.querytransferquota(projection.ownNodes,
                               projection.publicNodes,
                               -projection.transferred,
                               std::move(onReply));

    return true;
}

bool TransferQuotaChecker::checkStreamThrottled(
    std::function<void(transferquotastatus_t, Error)> completion)
{
    if (mStreamTimerRunning)
    {
        mStreamCheckPending = true;
        mPendingStreamCompletion = std::move(completion);
        return true;
    }

    return queryStream(std::move(completion));
}

bool TransferQuotaChecker::queryStream(std::function<void(transferquotastatus_t, Error)> completion)
{
    if (isProAccount())
    {
        checkProAccount(std::move(completion));
    }
    else if (!checkFreeAccount({},
                               {},
                               std::move(completion))) // The new streams are already in hdrns
    {
        return false;
    }

    auto timer = new CallbackTimer(mClient.rng,
                                   [this]()
                                   {
                                       onStreamTimerExpired();
                                   });
    timer->backoff(STREAM_CHECK_WINDOW_DS);

    mClient.addtimer(timer);

    mStreamTimerRunning = true;
    return true;
}

void TransferQuotaChecker::onStreamTimerExpired()
{
    mStreamTimerRunning = false;

    if (!mStreamCheckPending)
    {
        return;
    }

    mStreamCheckPending = false;
    auto completion = std::move(mPendingStreamCompletion);
    mPendingStreamCompletion = nullptr;

    // Streams that all finished meanwhile leave nothing to ask about: stays idle
    queryStream(std::move(completion));
}

void TransferQuotaChecker::checkProAccount(
    std::function<void(transferquotastatus_t, Error)> completion)
{
    // Adapts the snapshot to the state the caller asked for. A failed poll leaves the status
    // UNKNOWN, which is reported as such rather than withheld.
    auto onSnapshot = [completion = std::move(completion)](ProTransferQuota quota, Error e)
    {
        if (completion)
        {
            completion(e == API_OK ? quota.status : TRANSFER_QUOTA_UNKNOWN, e);
        }
    };

    // getProTransferState decides whether the cached snapshot still answers or a `uq` is due.
    getProTransferState(std::move(onSnapshot));
}

void TransferQuotaChecker::cacheStatusFromDetails(const AccountDetails& details)
{
    if (!isProAccount())
    {
        return;
    }

    if (const transferquotastatus_t status = statusForUsage(details); status != mStatus)
    {
        LOG_debug << "[TransferQuotaChecker] Pro transfer-quota state " << mStatus << " -> "
                  << status;
        mStatus = status;
    }

    // The window runs from the moment the figures arrived, so what is served is never older than
    // the interval however often it is asked for.
    mProAccountCacheValidUntilDs = Waiter::ds + PRO_CHECK_INTERVAL_DS;

    // Keep the allowance figures alongside the status so getProTransferState can serve the full
    // snapshot.
    mProTransferMax = details.transfer_max;
    mProTransferOwnUsed = details.transfer_own_used;
    mProTransferSrvUsed = details.transfer_srv_used;
    mProTransferHist = details.transfer_hist;
    mProSnapshotLevel = mClient.mMyAccount.getProLevel();
}

TransferQuotaChecker::ProTransferQuota TransferQuotaChecker::cachedProQuota() const
{
    return {mStatus, mProTransferMax, mProTransferOwnUsed, mProTransferSrvUsed, mProTransferHist};
}

void TransferQuotaChecker::getProTransferQuota(
    std::function<void(ProTransferQuota, Error)> completion)
{
    if (completion)
    {
        mPendingProPolls.push_back(std::move(completion));
    }

    if (mProQueryInFlight)
    {
        return;
    }

    auto details = std::make_shared<AccountDetails>();

    // procresult caches the snapshot before this runs, so just surface it. Take the waiters first:
    // one may poll again, and must not be handed this same reply.
    auto onReply = [this](std::shared_ptr<AccountDetails>, Error e)
    {
        mProQueryInFlight = false;

        auto waiting = std::move(mPendingProPolls);
        mPendingProPolls.clear();

        // The account lost its allowance while this was in flight, so cacheStatusFromDetails
        // refused the reply and there is no snapshot behind it. EACCESS is what a non-Pro caller is
        // turned away with anyway, and beats handing over zeros that read as an answer.
        const bool stillPro = isProAccount();
        const ProTransferQuota quota = stillPro ? cachedProQuota() : ProTransferQuota{};
        const Error result = stillPro ? e : Error(API_EACCESS);

        for (auto& waiter: waiting)
        {
            waiter(quota, result);
        }
    };

    // Internal poll: CommandGetUserQuota always reports to MegaApp::account_details, which with a
    // borrowed tag would finish an unrelated request early. Scoped against a throwing queueCommand.
    const auto scopedReqtag = makeScopedValue(mClient.reqtag, 0);

    // Only the transfer (`xfer`) figures are needed for the allowance ratio.
    mClient.queueCommand(new CommandGetUserQuota(&mClient,
                                                 std::move(details),
                                                 false /* storage */,
                                                 true /* transfer */,
                                                 false /* pro */,
                                                 -1 /* source */,
                                                 std::move(onReply)));

    // Only past the queuing: a throw leaves the guard clear, and the waiters for the next poll
    mProQueryInFlight = true;
}

void TransferQuotaChecker::getProTransferState(
    std::function<void(ProTransferQuota, Error)> completion)
{
    // Serve the last snapshot while it is still usable. An UNKNOWN established nothing, and the
    // snapshot carries the allowance figures too, so a light without them is not servable either;
    // past its validity window it is stale, whatever it holds.
    const bool haveSnapshot = mStatus != TRANSFER_QUOTA_UNKNOWN && mProTransferMax &&
                              mProSnapshotLevel == mClient.mMyAccount.getProLevel();
    if (haveSnapshot && mProAccountCacheValidUntilDs > Waiter::ds)
    {
        if (completion)
        {
            completion(cachedProQuota(), API_OK);
        }
        return;
    }

    getProTransferQuota(std::move(completion));
}

void TransferQuotaChecker::reset()
{
    mStatus = TRANSFER_QUOTA_UNKNOWN;
    mProAccountCacheValidUntilDs = 0;
    // Session reset clears the command queue without running the completion that would lower this
    mProQueryInFlight = false;
    mStreamTimerRunning = false;
    mStreamCheckPending = false;
    mPendingStreamCompletion = nullptr;

    // Dropped, not run: their captures belong to the session going away (MegaApiImpl finishes those
    // requests itself, via abortPendingActions).
    mPendingProPolls.clear();

    forgetProSnapshot();
}

} // namespace mega
