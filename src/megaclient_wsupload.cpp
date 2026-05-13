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
#include <utility>

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

    // Phase 2: minimal mutation + app notification
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

} // namespace mega

#endif // MEGA_USE_WSUPLOAD
