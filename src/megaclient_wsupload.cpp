/**
 * @file megaclient_wsupload.cpp
 * @brief MegaClient WS-upload member-function definitions.
 *
 * Extracted from src/megaclient.cpp to keep that file focused. Methods defined
 * here remain MegaClient members; declarations live in include/mega/megaclient.h.
 * The entire translation unit is gated by MEGA_USE_WSUPLOAD.
 *
 * (c) 2013-2014 by Mega Limited, Auckland, New Zealand
 *
 * This file is part of the MEGA SDK - Client Access Engine.
 *
 * Applications using the MEGA API must present a valid application key
 * and comply with the rules set forth in the Terms of Service.
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

#ifdef MEGA_USE_WSUPLOAD

#include "mega.h"
#include "mega/logging.h"
#include "mega/megaclient.h"
#include "mega/testhooks.h"
#include "mega/transfer.h"

#include <cstring>
#include <functional>
#include <utility>

// S12 Cluster-E: annotate the INTENTIONAL engine leak (quiesce-or-leak logout path) so
// LeakSanitizer treats it as a live root instead of failing sanitizer runs; real leaks
// stay detectable.
#if defined(__SANITIZE_ADDRESS__)
#include <sanitizer/lsan_interface.h>
#define MEGA_WS_LSAN_IGNORE(p) __lsan_ignore_object(p)
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#include <sanitizer/lsan_interface.h>
#define MEGA_WS_LSAN_IGNORE(p) __lsan_ignore_object(p)
#endif
#endif
#ifndef MEGA_WS_LSAN_IGNORE
#define MEGA_WS_LSAN_IGNORE(p) ((void)(p))
#endif

namespace mega
{

void MegaClient::maybeStartWsUploadEngine()
{
    if (mWsEngineStarted)
        return;
    if (!ws::wsEnabled(*this))
        return;
    if (loggedin() == NOTLOGGEDIN)
        return;
    if (!m_wsEngine)
    {
        m_wsEngine.reset(new ws::UploadEngine(*this));
    }

    m_wsEngine->setMaxConnections(connections[PUT]);
    m_wsEngine->setMaxUploadSpeed(getmaxuploadspeed());
    m_wsEngine->start();
    installWsEngineCallbacks();
    mWsEngineStarted = true;
}

void MegaClient::forcePermanentWsReadFailure(Transfer& transfer)
{
    // Mirror legacy slot-based behavior for local-source invalidation by forcing
    // Transfer::failed(API_EREAD) to skip deferred retries.
    transfer.failcount = FILE_SYNC_MAX_RETRIES + 1;
}

MegaClient::WsFailureRequeuePosition MegaClient::wsDetachTransferBeforeFailure(Transfer& t)
{
    WsFailureRequeuePosition position;
    if (!wsEngine() || t.channel != Transfer::Channel::WebSocket)
    {
        return position;
    }

    auto& transferList = transferlist.transfers[t.type];
    for (auto lit = transferList.begin(); lit != transferList.end(); ++lit)
    {
        if (lit->transfer != &t)
        {
            continue;
        }

        auto next = lit;
        ++next;
        for (; next != transferList.end(); ++next)
        {
            if (next->transfer && next->transfer->channel == Transfer::Channel::WebSocket)
            {
                position.wsBefore = next->transfer;
                position.wsBeforeTh = position.wsBefore->uploadhandle;
                break;
            }
        }
        break;
    }

    wsEngine()->remove(t);
    return position;
}

void MegaClient::wsReenqueueTransferAfterFailure(Transfer& t,
                                                 const WsFailureRequeuePosition& position)
{
    if (!wsEngine() || t.channel != Transfer::Channel::WebSocket)
    {
        return;
    }

    wsEngine()->enqueue(t);
    if (position.wsBefore && wsIsTransferAlive(t.type, position.wsBefore) &&
        (position.wsBefore->uploadhandle == position.wsBeforeTh))
    {
        wsEngine()->reposition(t, position.wsBefore);
    }

    if (t.state == TRANSFERSTATE_PAUSED || (xferpaused[t.type] && !t.isForSupport()))
    {
        wsEngine()->pause(t);
    }

    dstime retryAt = t.bt.nextset();
    if (!retryAt || retryAt == 1)
    {
        retryAt = waiter->ds;
    }
    wsEngine()->setRetryUntil(t, retryAt);
}

#ifdef MEGASDK_DEBUG_TEST_HOOKS_ENABLED
bool MegaClient::wsIsTransferTrackedForTesting(const Transfer& t) const
{
    return wsEngine() && wsEngine()->isTrackedForTesting(t);
}
#endif

void MegaClient::installWsEngineCallbacks()
{
    if (!m_wsEngine)
        return;

    wsRefreshCanStartAnotherFileSnapshot();
    ws::UploadEngine::Callbacks cb;

    cb.canStartAnotherFile = [this]() -> bool
    {
        return wsCanStartAnotherFile();
    };

    cb.preflightStart = [this](Transfer& t) -> ws::UploadEngine::PreflightStartResult
    {
        return wsPrepareUploadForWsSync(t);
    };

    cb.onStart = [this](Transfer& t)
    {
        // Bounce to client thread: set pos/progress/state and notify app.
        wsPostTransferUpdate(
            &t,
            [this](Transfer& t, TransferDbCommitter& committer)
            {
                LOG_debug << "[MegaClient::wsPostTransferUpdate] onStart -> t.state = "
                             "TRANSFERSTATE_ACTIVE [t.localfilename = "
                          << t.localfilename << "]";

                // Keep any existing pos/progress/chunkmacs so resumed transfers
                // can continue without losing their persisted state.
                t.state = TRANSFERSTATE_ACTIVE;
                t.failcount = 0;
                t.lastaccesstime = m_time();
                t.ws_latched_speed = 0;
                t.ws_latched_mean_speed = 0;
                t.ws_latched_avg_latency_ms = 0;
                t.ws_latched_failed_request_ratio = 0.0;
                t.ws_latched_stats_valid = false;

                if (wsEngine())
                {
                    std::string sessionUrl;
                    if (wsEngine()->getSessionUrl(t, sessionUrl))
                    {
                        if (!t.ws_session_url.empty() && t.ws_session_url != sessionUrl)
                        {
                            // Session endpoint changed (or was invalidated): restart progress for
                            // safety.
                            t.chunkmacs.clear();
                            t.pos = 0;
                            t.setProgresscompleted(0);
                        }
#ifdef MEGASDK_DEBUG_TEST_HOOKS_ENABLED
                        const std::string prevUrlForHook = t.ws_session_url;
#endif
                        t.ws_session_url = std::move(sessionUrl);
#ifdef MEGASDK_DEBUG_TEST_HOOKS_ENABLED
                        DEBUG_TEST_HOOK_WS_SESSION_URL_TRANSITION(t.tag,
                                                                  prevUrlForHook,
                                                                  t.ws_session_url,
                                                                  "onStart");
#endif
                    }
                }
                transfercacheadd(&t, &committer);
            });
    };

    cb.onProgress = [this](Transfer& t, const m_off_t confirmed)
    {
        wsPostTransferUpdate(&t,
                             [this, confirmed](Transfer& t, TransferDbCommitter& committer)
                             {
                                 LOG_debug << "[MegaClient::wsPostTransferUpdate] onProgress -> "
                                              "t.setProgresscompleted(confirmed="
                                           << confirmed
                                           << ") [t.localfilename = " << t.localfilename << "]";

                                 // Apply any server-confirmed chunk MAC updates accumulated on the
                                 // WS worker threads before persisting.
                                 wsMergeDrainedChunkMacs(t);
                                 wsApplyLatchedTransferStats(t);

                                 const auto diff = confirmed - t.progresscompleted;
                                 if (diff > 0 && t.client && t.client->httpio)
                                 {
                                     t.client->httpio->updateuploadspeed(diff);
                                 }

                                 // WS can resume on the same pool/file without a fresh onStart.
                                 // Only switch to ACTIVE if the WS engine confirms the transfer
                                 // is currently uploading (avoid late-ACK state flips after pause).
                                 if (t.state == TRANSFERSTATE_QUEUED && wsEngine() &&
                                     wsEngine()->isUploading(t))
                                 {
                                     t.state = TRANSFERSTATE_ACTIVE;
                                 }

                                 t.failcount = 0;
                                 t.lastaccesstime = m_time();
                                 t.setProgresscompleted(confirmed);
                                 transfercacheadd(&t, &committer);
                             });
    };

    cb.onFail = [this](Transfer& t,
                       int apierr,
                       m_off_t /*aux*/,
                       ws::UploadEngine::FailureDisposition disposition)
    {
        auto* tp = &t;
        const auto type = t.type;
        const auto th = t.uploadhandle;

        wsPostToClientThread(
            [tp, type, th, apierr, disposition](MegaClient& client,
                                                TransferDbCommitter& committer) mutable
            {
                if (!tp)
                    return;

                if (!client.wsIsTransferAlive(type, tp) || !(tp->uploadhandle == th))
                    return;

                const error e = static_cast<error>(apierr);
                dstime retrydelay = 0;

                // Keep WS failure handling relying on legacy Transfer::failed() as the single source.
                // Remove transfer from WS first as later Transfer::failed() may delete it.
                // Then re-enqueue only if the transfer survives (with queue order/state restored below).
                const auto wsPosition = client.wsDetachTransferBeforeFailure(*tp);

#ifdef MEGASDK_DEBUG_TEST_HOOKS_ENABLED
                const bool stillTrackedAfterDetach = client.wsIsTransferTrackedForTesting(*tp);
                DEBUG_TEST_HOOK_WSUPLOAD_FAILURE_DETACHED("ws transfer failed",
                                                          stillTrackedAfterDetach);
#endif

                if (disposition == ws::UploadEngine::FailureDisposition::Permanent)
                {
                    forcePermanentWsReadFailure(*tp);
                }

                tp->failed(e, committer, retrydelay);

                if (!client.wsIsTransferAlive(type, tp) || !(tp->uploadhandle == th))
                    return;

                client.wsReenqueueTransferAfterFailure(*tp, wsPosition);

                if (client.app)
                {
                    client.app->transfer_update(tp);
                }
            });
    };

    cb.onComplete = [this](Transfer& t, const char* payload, const int len)
    {
        std::string payloadCopy;
        if (payload && len > 0)
            payloadCopy.assign(payload, payload + len);

        wsPostToClientThread(
            [tPtr = &t,
             type = t.type,
             th = t.uploadhandle,
             payloadCopy = std::move(payloadCopy),
             len](MegaClient& c, TransferDbCommitter& committer) mutable
            {
                if (!tPtr)
                    return;

                if (!c.wsIsTransferAlive(type, tPtr) || !(tPtr->uploadhandle == th))
                    return;

                Transfer& tt = *tPtr;

                if (len != UPLOADTOKENLEN ||
                    payloadCopy.size() != static_cast<size_t>(UPLOADTOKENLEN))
                {
                    LOG_warn << "[MegaClient::installWsEngineCallbacks] onComplete -> "
                                "missing/invalid upload token (len="
                             << len << ") [t.localfilename = " << tt.localfilename << "]";
                    const auto wsPosition = c.wsDetachTransferBeforeFailure(tt);

#ifdef MEGASDK_DEBUG_TEST_HOOKS_ENABLED
                    const bool stillTrackedAfterDetach = c.wsIsTransferTrackedForTesting(tt);
                    DEBUG_TEST_HOOK_WSUPLOAD_FAILURE_DETACHED("missing/invalid upload token",
                                                              stillTrackedAfterDetach);
#endif

                    tt.failed(API_EAGAIN, committer);

                    if (c.wsIsTransferAlive(type, tPtr) && (tPtr->uploadhandle == th))
                    {
                        c.wsReenqueueTransferAfterFailure(*tPtr, wsPosition);
                    }
                    return;
                }

                tt.ultoken.reset(new UploadToken);
                memcpy(tt.ultoken->data(), payloadCopy.data(), UPLOADTOKENLEN);

                // Apply any confirmed chunk MAC updates before computing the final macsmac.
                c.wsMergeDrainedChunkMacs(tt);

                memcpy(&tt.filekey.key, tt.transferkey.data(), SymmCipher::KEYLENGTH);
                tt.filekey.iv_u64 = static_cast<uint64_t>(tt.ctriv);
                tt.filekey.crc_u64 =
                    static_cast<uint64_t>(tt.chunkmacs.macsmac(tt.transfercipher()));
                SymmCipher::xorblock(tt.filekey.iv_bytes.data(), tt.filekey.key.data());

                c.wsApplyLatchedTransferStats(tt);
                if (c.wsEngine())
                    c.wsEngine()->remove(tt);

                // Verify local file consistency before updating transfer state/conters,
                // so final completion only happens after verification succeeds.
                const WsVerifyResult verifyResult = c.wsVerifyUploadUnchanged(tt, committer);
                if (verifyResult == WsVerifyResult::TransientError)
                {
                    c.wsScheduleVerifyUpload(tt);
                    return;
                }
                if (verifyResult == WsVerifyResult::Failed)
                    return;

                const m_off_t previous = tt.progresscompleted;
                tt.setProgresscompleted(tt.size);
                tt.state = TRANSFERSTATE_COMPLETING;
                if (tt.progresscompleted != previous)
                {
                    const m_off_t diff = tt.progresscompleted - previous;
                    if (c.httpio)
                    {
                        c.httpio->updateuploadspeed(std::max<m_off_t>(diff, 0));
                    }
                }

                c.transfercacheadd(&tt, &committer);
                if (c.app)
                {
                    c.app->transfer_update(&tt);
                }

                const bool addedStats = tt.addTransferStats();
                if (addedStats)
                {
                    tt.collectAndPrintTransferStatsIfLimitReached();
                }

                c.wsFinalizeUploadCompletion(tt);
            });
    };

    m_wsEngine->setCallbacks(std::move(cb));
}

void MegaClient::wsPostToClientThread(std::function<void(MegaClient&, TransferDbCommitter&)>&& f)
{
    size_t diagQueueSize = 0;
    {
        std::lock_guard<std::mutex> g(mWsClientActionsMutex);
        mWsClientActions.emplace_back(std::move(f));
        diagQueueSize = mWsClientActions.size();
    }
    // [SyncPutnodesDiag] track ws->client lambda queue depth — Windows-CI stall
    // RCA hypothesis: queue grows >200 during slow cs round-trips, blocking
    // sync-upload completion propagation. Logged AFTER unlock for low overhead.
    LOG_debug << "[SyncPutnodesDiag] wsPostToClientThread enqueued. queueSize="
              << diagQueueSize;
    waiter->notify(); // wake client thread to process actions in exec()
}

void MegaClient::wsPostTransferUpdate(Transfer* t,
                                      std::function<void(Transfer&, TransferDbCommitter&)>&& f)
{
    const direction_t type = t ? t->type : PUT;
    const UploadHandle th = t ? t->uploadhandle : UploadHandle();

    wsPostToClientThread(
        [t, type, th, f = std::move(f)](MegaClient& c, TransferDbCommitter& committer) mutable
        {
            if (!t)
                return;

            if (!c.wsIsTransferAlive(type, t) || !(t->uploadhandle == th))
                return;

            f(*t, committer);
            if (c.app)
                c.app->transfer_update(t);
        });
}

void* MegaClient::wsHandshakeForUpload(const std::string& url, long timeoutMs, std::string* err)
{
    auto* cio = dynamic_cast<CurlHttpIO*>(httpio);
    if (!cio)
    {
        return nullptr;
    }
    return static_cast<void*>(cio->wsHandshake(url, timeoutMs, err));
}

void MegaClient::wsDrainClientActions(dstime maxExecTimeDs, bool loudPhases)
{
    CodeCounter::ScopeTimer clientActionTime(performanceStats.clientThreadActions);
    const dstime ctr_start = waiter->ds;
    size_t ctr_N = 0;
    TransferDbCommitter committer(tctable);
    for (;;)
    {
        std::function<void(MegaClient&, TransferDbCommitter&)> f;
        {
            std::lock_guard<std::mutex> g(mWsClientActionsMutex);
            if (mWsClientActions.empty())
                break;
            f = std::move(mWsClientActions.front());
            mWsClientActions.pop_front();
        }
        // S13 round-3 (Cluster I+J): a wedged lambda logs nothing on its own — the
        // post-drain dt log below only fires after it RETURNS. On the locallogout
        // path, pre-log each action so a pin names the drain phase + action index.
        if (loudPhases)
        {
            LOG_debug << "WsUpload locallogout: draining action #" << ctr_N;
        }
        // [SyncPutnodesDiag] measure single-lambda drain time — identifies
        // whether wsVerifyUploadUnchanged FS open is the slow leg on Windows.
        const dstime diagLambdaStart = waiter->ds;
        f(*this, committer);
        const dstime diagLambdaDt = waiter->ds - diagLambdaStart;
        if (diagLambdaDt >= 1)
        {
            LOG_debug << "[SyncPutnodesDiag] WS action drained. dt_ds="
                      << diagLambdaDt;
        }
        ++ctr_N;
        waiter->bumpds();
        if (maxExecTimeDs > 0 && ctr_start + maxExecTimeDs < waiter->ds)
            break;
    }

    size_t n = 0;
    {
        std::lock_guard<std::mutex> g(mWsClientActionsMutex);
        n = mWsClientActions.size();
    }
    if (n)
    {
        LOG_debug << "Processed " << ctr_N << " WS requests in " << (waiter->ds - ctr_start)
                  << "ms, " << n << " WS requests outstanding";
    }

    wsCleanupPreflightRequests();
    wsRefreshCanStartAnotherFileSnapshot();
}

MegaClient::WsVerifyPending::WsVerifyPending(PrnGen& rng, Transfer& t):
    transfer(&t),
    type(t.type),
    uploadHandle(t.uploadhandle),
    retryTimer(rng)
{}

MegaClient::WsVerifyResult MegaClient::wsVerifyUploadUnchanged(Transfer& t,
                                                               TransferDbCommitter& committer)
{
    static constexpr std::string_view fingerprintIssue = "[Fingerprint Issue] ";
    for (file_list::iterator it = t.files.begin(); it != t.files.end();)
    {
        File* f = *it;
        const LocalPath localpath = f->getLocalname();

        LOG_debug << "Verifying upload: " << localpath.toPath(false);

        auto fa = fsaccess->newfileaccess();
        fa->mShareDelete = true; // Allow move/delete during post-upload fingerprint verification
        const bool isOpen = fa->fopen(localpath, FSLogging::logOnError);
        if (!isOpen && fsaccess->transient_error)
        {
            LOG_warn << "Retrying upload completion due to a transient error";
            return WsVerifyResult::TransientError;
        }

        const bool isNotOpenAndIsNotSyncxfer = (!f->syncxfer && !isOpen);
        const bool fingerprintChanged = isOpen && f->genfingerprint(fa.get());

        if (isNotOpenAndIsNotSyncxfer || fingerprintChanged)
        {
            bool skipRemoveTransferFile = false;
            if (isNotOpenAndIsNotSyncxfer)
            {
                LOG_warn << "Deletion detected after upload";
            }
            else
            {
                LOG_warn << fingerprintIssue
                         << "Modification detected after upload! Path: " << localpath.toPath(false)
                         << ". Transfer fingerprint: " << t.fingerprintDebugString()
                         << ". FA fingerprint: " << f->fingerprintDebugString();
                DEBUG_TEST_HOOK_FILEFINGERPRINT_USE_LEGACY_BUGGY_SPARSE_CRC(
                    skipRemoveTransferFile);
            }

            ++it; // removeTransferFile will erase current entry
            if (skipRemoveTransferFile)
            {
                LOG_debug
                    << fingerprintIssue
                    << "Debug test hook filefingerprint using legacy buggy sparse crc was "
                       "active. Skipping removeTransferFile and"
                    << " mark transfer as successful. There can be fingerprint mismatches "
                       "between IA and FA genfingerprints with buggy sparse crc calculation";
                f->crc = t.crc;
            }
            else
            {
                t.removeTransferFile(API_EREAD, f, &committer);
            }
        }
        else
        {
            ++it;
        }
    }

    if (t.files.empty())
    {
        t.failed(API_EREAD, committer);
        return WsVerifyResult::Failed;
    }

    return WsVerifyResult::Ok;
}

void MegaClient::wsFinalizeUploadCompletion(Transfer& t)
{
    if (!gfxdisabled)
        t.addAnyMissingMediaFileAttributes(nullptr, t.localfilename);
    checkfacompletion(t.uploadhandle, &t, true);
}

void MegaClient::wsScheduleVerifyUpload(Transfer& t)
{
    for (auto& pending: mWsVerifyPending)
    {
        if (pending && pending->transfer == &t)
        {
            pending->type = t.type;
            pending->uploadHandle = t.uploadhandle;
            pending->retryTimer.backoff(11);
            waiter->notify();
            return;
        }
    }

    auto pending = std::make_unique<WsVerifyPending>(rng, t);
    pending->retryTimer.backoff(11);
    mWsVerifyPending.emplace_back(std::move(pending));
    waiter->notify();
}

void MegaClient::wsProcessVerifyUploads()
{
    if (mWsVerifyPending.empty())
        return;

    TransferDbCommitter committer(tctable);
    for (auto it = mWsVerifyPending.begin(); it != mWsVerifyPending.end();)
    {
        WsVerifyPending& pending = *(*it);
        Transfer* t = pending.transfer;
        if (!t)
        {
            it = mWsVerifyPending.erase(it);
            continue;
        }

        if (!wsIsTransferAlive(pending.type, t) || !(t->uploadhandle == pending.uploadHandle))
        {
            it = mWsVerifyPending.erase(it);
            continue;
        }

        if (!pending.retryTimer.armed())
        {
            ++it;
            continue;
        }

        const WsVerifyResult result = wsVerifyUploadUnchanged(*t, committer);
        if (result == WsVerifyResult::TransientError)
        {
            pending.retryTimer.backoff(11);
            ++it;
            continue;
        }

        it = mWsVerifyPending.erase(it);
        if (result == WsVerifyResult::Ok)
        {
            const m_off_t previous = t->progresscompleted;
            t->setProgresscompleted(t->size);
            t->state = TRANSFERSTATE_COMPLETING;
            if (t->progresscompleted != previous)
            {
                const m_off_t diff = t->progresscompleted - previous;
                if (httpio)
                {
                    httpio->updateuploadspeed(std::max<m_off_t>(diff, 0));
                }
            }

            transfercacheadd(t, &committer);
            if (app)
            {
                app->transfer_update(t);
            }

            const bool addedStats = t->addTransferStats();
            if (addedStats)
            {
                t->collectAndPrintTransferStatsIfLimitReached();
            }

            wsFinalizeUploadCompletion(*t);
        }
    }
}

bool MegaClient::wsCanStartAnotherFile() const
{
    return mWsCanStartAnotherFile.load(std::memory_order_relaxed);
}

ws::UploadEngine::PreflightStartResult MegaClient::wsPrepareUploadForWsSync(Transfer& t)
{
    // Called by WS worker threads. Execute preflight on MegaClient thread.
    // Reuse a single in-flight future per (Transfer*, uploadhandle) so repeated
    // checks don't enqueue duplicate preflight jobs.
    using PreflightStartResult = ws::UploadEngine::PreflightStartResult;
    Transfer* tp = &t;
    const direction_t type = t.type;
    const UploadHandle th = t.uploadhandle;

    std::shared_future<bool> resultFuture;
    std::shared_ptr<std::promise<bool>> resultPromise;
    {
        std::lock_guard<std::mutex> g(mWsPreflightMutex);
        auto it = mWsPreflightRequests.find(tp);
        const bool reuseRequest = (it != mWsPreflightRequests.end()) &&
                                  (it->second.uploadHandle == th);
        if (reuseRequest)
        {
            resultFuture = it->second.future;
        }
        else
        {
            if (it != mWsPreflightRequests.end())
            {
                mWsPreflightRequests.erase(it);
            }

            resultPromise = std::make_shared<std::promise<bool>>();
            resultFuture = resultPromise->get_future().share();
            mWsPreflightRequests.emplace(tp,
                                         WsPreflightRequest{th,
                                                            resultFuture,
                                                            WsPreflightState::Queued});
        }
    }

    if (resultPromise)
    {
        wsPostToClientThread(
            [tp, type, th, resultPromise](MegaClient& c, TransferDbCommitter&) mutable
            {
                {
                    std::lock_guard<std::mutex> g(c.mWsPreflightMutex);
                    const auto it = c.mWsPreflightRequests.find(tp);
                    if (it != c.mWsPreflightRequests.end() && it->second.uploadHandle == th)
                    {
                        it->second.state = WsPreflightState::Running;
                    }
                }

                bool ok = false;
                bool transferStillAlive = false;
                if (tp)
                {
                    if (c.wsIsTransferAlive(type, tp) && (th.isUndef() || (tp->uploadhandle == th)))
                    {
                        transferStillAlive = true;
                        ok = c.prepareUploadForWs(*tp);
                    }
                }

                {
                    std::lock_guard<std::mutex> g(c.mWsPreflightMutex);
                    const auto it = c.mWsPreflightRequests.find(tp);
                    if (it != c.mWsPreflightRequests.end() && it->second.uploadHandle == th)
                    {
                        // prepareUploadForWs() may assign uploadhandle on first activation.
                        // Keep the cached request keyed to the assigned handle so future
                        // reuse/consume matches the same request instead of re-scheduling.
                        // Guard the tp->uploadhandle dereference with the alive-check we
                        // already performed above; freeq(PUT) in locallogout can destroy
                        // the Transfer before this lambda drains via wsLocallogoutCleanup.
                        if (transferStillAlive &&
                            it->second.uploadHandle.isUndef() &&
                            !tp->uploadhandle.isUndef())
                        {
                            it->second.uploadHandle = tp->uploadhandle;
                        }

                        it->second.state = ok ? WsPreflightState::Ready : WsPreflightState::Failed;
                    }
                }
                resultPromise->set_value(ok);
                if (auto* engine = c.wsEngine())
                {
                    engine->notifyWorkers();
                }
            });
    }

    // Non-blocking poll: if the client thread has not yet run the posted action,
    // return false so the pool worker releases uploadMutex and retries. This
    // avoids a circular wait where the pool worker holds uploadMutex while the
    // client thread needs it to service the posted preflight action.
    if (resultFuture.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready)
    {
        return PreflightStartResult::Pending;
    }

    // Consume ready result atomically with map erase so only one caller can
    // use this preflight outcome.
    bool canConsume = false;
    {
        std::lock_guard<std::mutex> g(mWsPreflightMutex);
        const auto it = mWsPreflightRequests.find(tp);
        const bool handleMatches =
            (it != mWsPreflightRequests.end()) &&
            ((it->second.uploadHandle == th) ||
             (th.isUndef() && tp && !it->second.uploadHandle.isUndef() &&
              tp->uploadhandle == it->second.uploadHandle));
        const bool mapFutureReady =
            (it != mWsPreflightRequests.end()) &&
            handleMatches &&
            (it->second.state == WsPreflightState::Ready ||
             it->second.state == WsPreflightState::Failed) &&
            it->second.future.valid();
        if (mapFutureReady)
        {
            mWsPreflightRequests.erase(it);
            canConsume = true;
        }
    }

    if (!canConsume)
    {
        return PreflightStartResult::Pending;
    }
    return resultFuture.get() ? PreflightStartResult::Ready : PreflightStartResult::Pending;
}

// Check transfer is alive by comparing its exact pointer identity.
// WS events are posted cross-thread so when lambda runs, tp may've been removed or replaced.
// Do not use multi_transfers::find(tp) for lookup which compares FileFingerprint not pointer,
// it can match a different transfer with the same fingerprint.
// Its comparator also dereferences the lookup key, which is unsafe for stale pointers.
bool MegaClient::wsIsTransferAlive(direction_t type, const Transfer* tp) const
{
    for (const auto& entry : multi_transfers[type])
    {
        if (entry.second == tp)
            return true;
    }
    return false;
}

// Cleanup those preflight requests whose bound transfer had been invalid
void MegaClient::wsCleanupPreflightRequests()
{
    static constexpr dstime WS_PREFLIGHT_CLEANUP_INTERVAL_DS = 20; // 2 second

    const dstime now = waiter->ds;
    if (mWsPreflightLastCleanupDs &&
        now < mWsPreflightLastCleanupDs + WS_PREFLIGHT_CLEANUP_INTERVAL_DS)
    {
        return;
    }

    // Update cleanup timestamp for every attempt, including empty-map cases,
    // to avoid retrying a no-op cleanup every exec iteration.
    mWsPreflightLastCleanupDs = now;

    {
        std::lock_guard<std::mutex> g(mWsPreflightMutex);
        if (mWsPreflightRequests.empty())
        {
            return;
        }
    }

    // Snapshot currently tracked PUT transfers once, then filter preflight requests
    // against that set to avoid (requests * transfers) scanning.
    std::unordered_set<Transfer*> aliveTransfers;
    aliveTransfers.reserve(multi_transfers[PUT].size());
    for (const auto& transferEntry: multi_transfers[PUT])
    {
        aliveTransfers.insert(transferEntry.second);
    }

    std::lock_guard<std::mutex> g(mWsPreflightMutex);
    for (auto it = mWsPreflightRequests.begin(); it != mWsPreflightRequests.end();)
    {
        if (aliveTransfers.find(it->first) == aliveTransfers.end())
        {
            it = mWsPreflightRequests.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

void MegaClient::wsRefreshCanStartAnotherFileSnapshot()
{
    const bool canStart = (ststatus != STORAGE_RED && ststatus != STORAGE_PAYWALL) &&
                          (queuedfa.size() < static_cast<size_t>(MAXQUEUEDFA));
    mWsCanStartAnotherFile.store(canStart, std::memory_order_relaxed);
}

bool MegaClient::prepareUploadForWs(Transfer& t)
{
    auto failWsPreflightRead = [this, &t](const char* reason)
    {
        LOG_warn << "[MegaClient::prepareUploadForWs] preflight failed (" << reason
                 << ") [t.localfilename = " << t.localfilename << "]";

        Transfer* tp = &t;
        const direction_t type = t.type;
        const UploadHandle th = t.uploadhandle;
        const std::string reasonText = reason ? reason : "";
        wsPostToClientThread(
            [tp, type, th, reasonText](MegaClient& c, TransferDbCommitter& committer)
            {
                if (!tp)
                {
                    return;
                }

                if (!c.wsIsTransferAlive(type, tp))
                {
                    return;
                }

                // If we had a defined upload handle when scheduling the task, ensure we still
                // target the same transfer generation.
                if (!th.isUndef() && !(tp->uploadhandle == th))
                {
                    return;
                }

                const auto wsPosition = c.wsDetachTransferBeforeFailure(*tp);

#ifdef MEGASDK_DEBUG_TEST_HOOKS_ENABLED
                const bool stillTracked = c.wsIsTransferTrackedForTesting(*tp);
                DEBUG_TEST_HOOK_WSUPLOAD_FAILURE_DETACHED(reasonText.c_str(), stillTracked);
#endif

                // Mirror legacy slot-based startup behavior: if local-file validation fails
                // before the request is even dispatched, fail immediately instead of consuming
                // the generic I/O retry budget.
                forcePermanentWsReadFailure(*tp);

                tp->failed(API_EREAD, committer);

                if (c.wsIsTransferAlive(type, tp) && (th.isUndef() || (tp->uploadhandle == th)))
                {
                    c.wsReenqueueTransferAfterFailure(*tp, wsPosition);
                }
            });
    };

    // Idempotent WS preflight: mirror the non-network work done by the legacy path
    // at openfinished, without TransferSlot/HttpReq:
    //
    // 0) Ensure transfer key/CTR IV exist (WS preflight can run before dispatchTransfers()).
    if (SymmCipher::isZeroKey(t.transferkey.data(), SymmCipher::KEYLENGTH))
    {
        byte keyctriv[SymmCipher::KEYLENGTH + sizeof(int64_t)];
        rng.genblock(keyctriv, sizeof keyctriv);
        memcpy(t.transferkey.data(), keyctriv, SymmCipher::KEYLENGTH);
        t.ctriv = static_cast<int64_t>(
            MemAccess::get<uint64_t>((const char*)keyctriv + SymmCipher::KEYLENGTH));
        LOG_debug << "[MegaClient::prepareUploadForWs] generated transferkey/ctriv [t.localfilename"
                     " = "
                  << t.localfilename << "]";
    }

    // 1) Ensure localfilename is prepared
    if (t.localfilename.empty())
    {
        LOG_debug << "[MegaClient::prepareUploadForWs] localfilename.empty() -> prepare(*fsaccess)";
        for (file_list::iterator it = t.files.begin();
             t.localfilename.empty() && it != t.files.end();
             ++it)
        {
            (*it)->prepare(*fsaccess);
        }

        if (t.localfilename.empty() || !t.localfilename.isAbsolute())
        {
            LOG_err << "[MegaClient::prepareUploadForWs] No absolute localfilename yet";
            return false; // defer start until we have a usable path
        }

        // App-side preparation (thumbnails may depend on this meta)
        app->transfer_prepare(&t);
    }

    // 2) Early local-file validation (legacy parity):
    //    verify the local file still matches the queued transfer metadata before
    //    starting any WS handshake/chunking work.
    {
        auto fa = fsaccess->newfileaccess();
        fa->mShareDelete = true; // Allow move/delete during preflight validation
        if (!fa->fopen(t.localfilename, OPEN_RDONLY, FSLogging::logOnError))
        {
            failWsPreflightRead("cannot open local file");
            return false;
        }

        if (fa->mtime != t.mtime || fa->size != t.size)
        {
            LOG_warn << "[MegaClient::prepareUploadForWs] Modification detected before WS upload."
                     << " Path: " << t.localfilename
                     << " Size: " << t.size
                     << " Mtime: " << t.mtime
                     << " FaSize: " << fa->size
                     << " FaMtime: " << fa->mtime;
            failWsPreflightRead("file modified");
            return false;
        }
    }

    // 3) First-activation only: create uploadhandle and enqueue FA imagery once
    if (t.uploadhandle.isUndef())
    {
        t.uploadhandle = mUploadHandle.next();
        LOG_debug << "[MegaClient::prepareUploadForWs] t.uploadhandle.isUndef() -> t.uploadhandle "
                     "= mUploadHandle.next() = "
                  << t.uploadhandle << " [t.localfilename = " << t.localfilename << "]";

        // 4) Enqueue thumbnail/preview FAs if applicable
        if (!gfxdisabled && gfx && gfx->isgfx(t.localfilename))
        {
            LOG_debug << "[MegaClient::prepareUploadForWs] !gfxdisabled && gfx && "
                         "gfx->isgfx(t.localfilename) -> generate and mark pending attributes "
                         "[t.localfilename = "
                      << t.localfilename << "]";
            // Keep the behavior: generate and mark pending attributes
            const int bitmask = gfx->gendimensionsputfa(t.localfilename,
                                                        NodeOrUploadHandle(t.uploadhandle),
                                                        t.transfercipher(),
                                                        -1);

            if (bitmask & (1 << GfxProc::THUMBNAIL))
            {
                LOG_debug
                    << "[MegaClient::prepareUploadForWs] bitmask & (1 << GfxProc::THUMBNAIL) -> "
                       "fileAttributesUploading.setFileAttributePending(t.uploadhandle, "
                       "GfxProc::THUMBNAIL, &t) [t.localfilename = "
                    << t.localfilename << "]";
                fileAttributesUploading.setFileAttributePending(t.uploadhandle,
                                                                GfxProc::THUMBNAIL,
                                                                &t);
            }
            if (bitmask & (1 << GfxProc::PREVIEW))
            {
                LOG_debug
                    << "[MegaClient::prepareUploadForWs] bitmask & (1 << GfxProc::PREVIEW) -> "
                       "fileAttributesUploading.setFileAttributePending(t.uploadhandle, "
                       "GfxProc::PREVIEW, &t) [t.localfilename = "
                    << t.localfilename << "]";
                fileAttributesUploading.setFileAttributePending(t.uploadhandle,
                                                                GfxProc::PREVIEW,
                                                                &t);
            }
        }
    }

    // chunkmacs/fingerprint are handled later in WS callbacks
    // (engine will open FA and verify mtime/size on first read)
    LOG_debug << "[MegaClient::prepareUploadForWs] return true [t.localfilename = "
              << t.localfilename << "]";
    return true;
}

// Drain server-confirmed chunk MAC updates from the WS engine and merge them into the
// Transfer's chunkmacs; advance contiguous/macsmac progress if anything merged.
// Bails out if the WS engine is unavailable.
void MegaClient::wsMergeDrainedChunkMacs(Transfer& t)
{
    auto* wse = wsEngine();
    if (!wse)
        return;

    std::vector<chunkmac_map> macUpdates;
    wse->drainConfirmedChunkMacs(t, macUpdates);
    bool mergedChunkMacs = false;
    for (auto& m: macUpdates)
    {
        if (m.size())
        {
            t.chunkmacs.finishedUploadChunks(m);
            mergedChunkMacs = true;
        }
    }
    if (mergedChunkMacs)
    {
        t.pos = t.chunkmacs.updateContiguousProgress(t.size);
        t.chunkmacs.updateMacsmacProgress(t.transfercipher());
    }
}

// Snapshot WS transfer stats from the engine into Transfer's ws_latched_* fields,
// capping the window speed by the user-configured max upload speed. Bails out if
// wsEngine() is unavailable or getTransferStats returns false.
void MegaClient::wsApplyLatchedTransferStats(Transfer& t)
{
    auto* wse = wsEngine();
    if (!wse)
        return;

    ws::UploadEngine::WsTransferStats wsStats;
    if (!wse->getTransferStats(t, wsStats))
    {
        // S13 round-3 (Cluster H): make the miss visible — if BOTH latch points
        // (onProgress + completion) hit this branch, the upload can only be counted
        // via a live-engine lookup at stats time, and a silent miss here is the
        // uncounted-upload precursor.
        LOG_debug << "[MegaClient::wsApplyLatchedTransferStats] engine stats unavailable — "
                     "latch skipped [size="
                  << t.size << "]";
        return;
    }

    m_off_t boundedSpeed = wsStats.windowSpeedBytesPerSecond;
    const m_off_t maxUploadSpeed = getmaxuploadspeed();
    if (maxUploadSpeed > 0)
    {
        boundedSpeed = std::min(boundedSpeed, maxUploadSpeed);
    }
    t.ws_latched_speed = boundedSpeed;
    t.ws_latched_mean_speed = wsStats.meanSpeedBytesPerSecond;
    t.ws_latched_avg_latency_ms = static_cast<m_off_t>(wsStats.avgStartTransferTime.count());
    t.ws_latched_failed_request_ratio = wsStats.failedRequestRatio;
    // Zero mean speed / latency are legitimate latched values (tiny file, slow
    // cold-start window) — validity is this flag, not ">0" sentinels (Cluster H, S13).
    t.ws_latched_stats_valid = true;
}

// WS logout cleanup: process pending WS client-thread actions before engine teardown.
// This helps flush state/cache updates and avoids worker shutdown waiting on queued
// client-side work (for example handshake-related tasks) while httpio is still valid.
// maybeStartWsUploadEngine() will recreate the engine lazily after the next login.
void MegaClient::wsLocallogoutCleanup()
{
    // S13 round-3 (Cluster I+J): phase-stamped so any future wedge names its phase —
    // the round-3 "Logout failed after 600 seconds" CI aborts were completely silent
    // between "MediaInfo version" and the harness timeout.
    const auto logoutT0 = std::chrono::steady_clock::now();
    const auto phaseMs = [&logoutT0]()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now() - logoutT0)
            .count();
    };
    if (m_wsEngine)
    {
        LOG_debug << "WsUpload locallogout: stop() begin";
        m_wsEngine->stop();
        LOG_debug << "WsUpload locallogout: stop() done (" << phaseMs() << " ms)";
    }
    wsDrainClientActions(30, /*loudPhases=*/true);
    LOG_debug << "WsUpload locallogout: drain done (" << phaseMs() << " ms)";
    if (m_wsEngine)
    {
        // S12 Cluster-E fix: the engine destructor joins worker threads UNBOUNDED. A
        // worker blocked in a filesystem syscall that may never return (win_9515: a
        // FUSE-backed upload source torn down mid-read) turned this into a locallogout
        // hang the CI watchdog had to kill. Quiesce with a bound; on timeout,
        // intentionally leak the engine — loudly — so logout completes and the stuck
        // threads' member accesses stay valid on the leaked-alive object. Workers exit
        // promptly when unblocked (terminate-aware loops + sliced sleeps).
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(3000);
        while (!m_wsEngine->workersQuiesced() && std::chrono::steady_clock::now() < deadline)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        if (!m_wsEngine->workersQuiesced())
        {
            LOG_err << "WsUpload: engine workers did not quiesce at locallogout — "
                       "intentionally leaking the engine to avoid an unbounded thread "
                       "join (stuck filesystem IO?) (Cluster E, S12)"
                    << " [liveWorkers=" << m_wsEngine->liveWorkerCount()
                    << " managerLive=" << m_wsEngine->managerThreadLive()
                    << " elapsedMs=" << phaseMs() << "]";
            MEGA_WS_LSAN_IGNORE(m_wsEngine.release());
        }
        else
        {
            LOG_debug << "WsUpload locallogout: engine quiesced (" << phaseMs() << " ms)";
        }
    }
    m_wsEngine.reset();
    LOG_debug << "WsUpload locallogout: engine destroyed (" << phaseMs() << " ms)";
    mWsEngineStarted = false;
}

// freeq() bypasses TransferList::removetransfer(), so explicitly detach the WS
// upload from the engine before the loop's `delete transferPtr.second` deletes the
// Transfer behind the engine's back.
void MegaClient::wsFreeqCleanupTransfer(direction_t d, Transfer* transfer)
{
    if (d == PUT && wsEngine() && transfer &&
        transfer->channel == Transfer::Channel::WebSocket)
    {
        wsEngine()->remove(*transfer);
    }
}

// Called from MegaClient::disconnect() so the WS engine can fast-fail in-flight
// chunk-sends and surface the disconnect to retry/backoff timers immediately.
void MegaClient::wsNotifyNetworkDisconnect()
{
    if (wsEngine())
    {
        wsEngine()->notifyNetworkDisconnect();
    }
}

// Activate overquota for the WS channel of a transfer that has no legacy slot.
// `alreadyOverquota` is captured by the caller BEFORE its `bt.backoff(NEVER)` runs,
// because that backoff() mutates bt state and we need the pre-mutation value to
// decide whether this is a fresh overquota event or a repeat (already retrying).
// Mirrors the non-WS slot-based branch in MegaClient::activateoverquota().
void MegaClient::wsActivateOverquotaForTransfer(Transfer* t,
                                                bool alreadyOverquota,
                                                bool isPaywall)
{
    if (wsEngine())
        wsEngine()->markFailed(*t, NEVER);
    if (!alreadyOverquota)
    {
        t->state = TRANSFERSTATE_RETRYING;
        app->transfer_failed(t, isPaywall ? API_EPAYWALL : API_EOVERQUOTA, 0);
        ++performanceStats.transferTempErrors;
    }
    else if (t->state != TRANSFERSTATE_RETRYING)
    {
        t->state = TRANSFERSTATE_RETRYING;
    }
}

// Re-arm the WS retry timer for `transfer` while the caller's abortbackoff() walks
// the multi_transfers loop. Uses the latched Waiter::ds (already bumpds()-updated by
// the caller) so all transfers in the same loop see a consistent "now".
void MegaClient::wsAbortBackoffForTransfer(Transfer* transfer)
{
    if (transfer && transfer->channel == Transfer::Channel::WebSocket && wsEngine())
    {
        wsEngine()->setRetryUntil(*transfer, Waiter::ds);
    }
}

// Legacy uploads are effectively stopped while blocked (doio gate). Mirror that for
// WS uploads by pausing active WS transfers and nudging worker threads to drop
// current socket sessions.
void MegaClient::wsHandleAccountBlocked()
{
    if (!wsEngine())
        return;
    for (auto& it: multi_transfers[PUT])
    {
        Transfer* t = it.second;
        if (!t || t->channel != Transfer::Channel::WebSocket)
        {
            continue;
        }
        wsEngine()->pause(*t);
    }
    wsEngine()->notifyNetworkDisconnect();
}

// Restore WS uploads that were paused because of account blocked state. Respect
// explicit user-per-transfer pauses and global PUT pause.
void MegaClient::wsHandleAccountUnblocked()
{
    if (!wsEngine() || xferpaused[PUT])
        return;
    for (auto& it: multi_transfers[PUT])
    {
        Transfer* t = it.second;
        if (!t || t->channel != Transfer::Channel::WebSocket)
        {
            continue;
        }
        if (t->state == TRANSFERSTATE_PAUSED)
        {
            continue;
        }
        wsEngine()->unpause(*t);
    }
}

// Pause/resume WS uploads when the caller's pausexfers() runs. Mirrors the
// legacy-slot pause/unpause for WS-channel transfers; honours support-upload
// bypass on pause; on a `hard` pause, additionally notifies network-disconnect
// so worker sockets drop in-flight chunks immediately.
void MegaClient::wsApplyTransferPause(direction_t d, bool pause, bool hard)
{
    if (d != PUT || !wsEngine())
        return;

    for (auto& it: multi_transfers[d])
    {
        Transfer* t = it.second;
        if (!t || t->channel != Transfer::Channel::WebSocket)
        {
            continue;
        }

        if (pause)
        {
            // Support uploads should bypass global pause.
            if (!t->isForSupport())
            {
                wsEngine()->pause(*t);
            }
        }
        else
        {
            wsEngine()->unpause(*t);
        }
    }

    if (pause && hard)
    {
        wsEngine()->notifyNetworkDisconnect();
    }
}

// Propagate applymaxconnections() updates to the WS engine for PUT.
void MegaClient::wsApplyMaxConnections(direction_t d, int num)
{
    if (d == PUT && wsEngine())
    {
        wsEngine()->setMaxConnections(static_cast<unsigned char>(num));
    }
}

// Propagate setmaxuploadspeed() updates to the WS engine.
void MegaClient::wsApplyMaxUploadSpeed(m_off_t normalizedLimit)
{
    if (wsEngine())
    {
        wsEngine()->setMaxUploadSpeed(normalizedLimit);
    }
}

} // namespace mega

#endif // MEGA_USE_WSUPLOAD
