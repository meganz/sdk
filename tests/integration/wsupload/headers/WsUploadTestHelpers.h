/**
 * @file WsUploadTestHelpers.h
 * @brief WS-upload test helpers: snapshot fetchers, predicate-waiters, URL overrides.
 *
 * Extracted from SdkTest_test.cpp anonymous-namespace helpers (originally gated under
 * `#ifdef MEGA_USE_WSUPLOAD`). Header-only inline so consumers may include this from
 * multiple TUs without ODR issues; the inline bodies access SDK-private members of
 * MegaClient/Transfer that are public in the SDK headers anyway.
 *
 * Default `timeoutSeconds` values match SdkTest_test.h's `defaultTimeout = 60` constant
 * (inlined as a literal to avoid pulling `using namespace mega;` into this header).
 */

#pragma once

#include "megaapi.h"
#include "megaapi_impl.h"
#include "mega.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace mega::test::wsupload
{

inline constexpr std::size_t kWsUploadDefaultFileSize = 12 * 1024 * 1024;

inline ::MegaUploadOptions makeDefaultUploadOptions()
{
    ::MegaUploadOptions options;
    options.mtime = ::MegaUploadOptions::INVALID_CUSTOM_MOD_TIME;
    return options;
}

struct WsUploadTransferSnapshot
{
    bool found = false;
    std::uint32_t transferId = 0;
    std::string fileName;
    std::uint32_t wsFileno = 0;
    std::string serializedWsSessionUrl;
    std::string wsSessionUrl;
    int state = ::mega::MegaTransfer::STATE_NONE;
    ::m_off_t progressCompleted = 0;
    ::m_off_t pos = 0;
    ::m_off_t size = 0;
    std::uintptr_t poolId = 0;
};

inline bool fetchWsUploadTransferSnapshots(::mega::MegaApi& api,
                                           std::vector<WsUploadTransferSnapshot>& out,
                                           int timeoutSeconds = 60)
{
    ::mega::MegaApiImpl* impl = ::mega::MegaApiImpl::ImplOf(&api);
    if (!impl)
    {
        return false;
    }

    auto promise = std::make_shared<std::promise<std::vector<WsUploadTransferSnapshot>>>();
    auto future = promise->get_future();

    auto exec = std::make_shared<::mega::ExecuteOnce>(
        [impl, promise]()
        {
            std::vector<WsUploadTransferSnapshot> snapshots;
            ::mega::MegaClient* client = impl->getClientForTesting();
            if (client)
            {
                std::set<const ::mega::Transfer*> seen;
                auto appendTransfer = [&](::mega::Transfer* t)
                {
                    if (!t || t->type != ::mega::PUT || t->finished || !seen.insert(t).second)
                    {
                        return;
                    }

                    WsUploadTransferSnapshot snapshot;
                    snapshot.found = true;
                    snapshot.transferId = t->dbid;
                    snapshot.fileName = t->localfilename.leafName().toPath(false);
                    snapshot.wsFileno = t->ws_fileno;
                    snapshot.serializedWsSessionUrl = t->ws_session_url;
                    snapshot.wsSessionUrl = snapshot.serializedWsSessionUrl;
                    if (snapshot.wsSessionUrl.empty() && client->wsEngine())
                    {
                        std::string sessionUrlFromEngine;
                        if (client->wsEngine()->getSessionUrl(*t, sessionUrlFromEngine))
                        {
                            snapshot.wsSessionUrl = std::move(sessionUrlFromEngine);
                        }
                    }
                    snapshot.progressCompleted = t->progresscompleted;
                    snapshot.pos = t->pos;
                    snapshot.size = t->size;
                    snapshot.state = t->state;
                    if (client->wsEngine())
                    {
                        snapshot.poolId = client->wsEngine()->getFilePoolIdForTesting(*t);
                    }
                    snapshots.emplace_back(std::move(snapshot));
                };

                auto& uploads = client->multi_transfers[::mega::PUT];
                for (const auto& entry : uploads)
                {
                    appendTransfer(entry.second);
                }

                auto& queuedUploads = client->transferlist.transfers[::mega::PUT];
                for (auto it = queuedUploads.begin(); it != queuedUploads.end(); ++it)
                {
                    appendTransfer(*it);
                }
            }
            promise->set_value(std::move(snapshots));
        });

    impl->executeOnThreadForTesting(exec);

    if (future.wait_for(std::chrono::seconds(timeoutSeconds)) != std::future_status::ready)
    {
        return false;
    }

    out = future.get();
    return true;
}

inline bool fetchBestWsUploadTransferSnapshot(::mega::MegaApi& api,
                                              WsUploadTransferSnapshot& out,
                                              int timeoutSeconds = 60)
{
    ::mega::MegaApiImpl* impl = ::mega::MegaApiImpl::ImplOf(&api);
    if (!impl)
    {
        return false;
    }

    auto promise = std::make_shared<std::promise<WsUploadTransferSnapshot>>();
    auto future = promise->get_future();

    auto exec = std::make_shared<::mega::ExecuteOnce>(
        [impl, promise]()
        {
            WsUploadTransferSnapshot snapshot;
            ::mega::MegaClient* client = impl->getClientForTesting();
            if (client)
            {
                int bestScore = -1;
                auto considerTransfer = [&](::mega::Transfer* t)
                {
                    if (!t || t->type != ::mega::PUT || t->finished)
                    {
                        return;
                    }

                    WsUploadTransferSnapshot candidate;
                    candidate.found = true;
                    candidate.transferId = t->dbid;
                    candidate.fileName = t->localfilename.leafName().toPath(false);
                    candidate.wsFileno = t->ws_fileno;
                    candidate.serializedWsSessionUrl = t->ws_session_url;
                    candidate.wsSessionUrl = candidate.serializedWsSessionUrl;
                    if (candidate.wsSessionUrl.empty() && client->wsEngine())
                    {
                        std::string sessionUrlFromEngine;
                        if (client->wsEngine()->getSessionUrl(*t, sessionUrlFromEngine))
                        {
                            candidate.wsSessionUrl = std::move(sessionUrlFromEngine);
                        }
                    }
                    candidate.progressCompleted = t->progresscompleted;
                    candidate.pos = t->pos;
                    candidate.size = t->size;
                    candidate.state = t->state;

                    int score = 0;
                    if (t->channel == ::mega::Transfer::Channel::WebSocket)
                        score += 4;
                    if (candidate.wsFileno > 0)
                        score += 2;
                    if (!candidate.wsSessionUrl.empty())
                        score += 1;

                    if (!snapshot.found || score > bestScore ||
                        (score == bestScore &&
                         candidate.progressCompleted > snapshot.progressCompleted))
                    {
                        snapshot = std::move(candidate);
                        bestScore = score;
                    }
                };

                auto& uploads = client->multi_transfers[::mega::PUT];
                for (const auto& entry : uploads)
                {
                    considerTransfer(entry.second);
                }

                if (!snapshot.found)
                {
                    auto& queuedUploads = client->transferlist.transfers[::mega::PUT];
                    for (auto it = queuedUploads.begin(); it != queuedUploads.end(); ++it)
                    {
                        considerTransfer(*it);
                    }
                }
            }
            promise->set_value(std::move(snapshot));
        });

    impl->executeOnThreadForTesting(exec);

    if (future.wait_for(std::chrono::seconds(timeoutSeconds)) != std::future_status::ready)
    {
        return false;
    }

    out = future.get();
    return true;
}

inline bool waitForFirstUploadTransferSnapshot(
    ::mega::MegaApi& api,
    WsUploadTransferSnapshot& out,
    const std::function<bool(const WsUploadTransferSnapshot&)>& predicate,
    int timeoutSeconds = 60,
    unsigned pollMillis = 200)
{
    const auto start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start < std::chrono::seconds(timeoutSeconds))
    {
        WsUploadTransferSnapshot snapshot{};
        if (fetchBestWsUploadTransferSnapshot(api, snapshot, 1) && snapshot.found &&
            predicate(snapshot))
        {
            out = std::move(snapshot);
            return true;
        }

        ::WaitMillisec(pollMillis);
    }

    return false;
}

inline bool overrideFirstUploadSessionUrlForTesting(::mega::MegaApi& api,
                                                    const std::string& forcedUrl,
                                                    int timeoutSeconds = 60)
{
    ::mega::MegaApiImpl* impl = ::mega::MegaApiImpl::ImplOf(&api);
    if (!impl)
    {
        return false;
    }

    auto promise = std::make_shared<std::promise<bool>>();
    auto future = promise->get_future();

    auto exec = std::make_shared<::mega::ExecuteOnce>(
        [impl, promise, forcedUrl]()
        {
            bool updated = false;
            ::mega::MegaClient* client = impl->getClientForTesting();
            if (client)
            {
                auto& uploads = client->multi_transfers[::mega::PUT];
                for (const auto& entry : uploads)
                {
                    auto* t = entry.second;
                    if (!t || t->type != ::mega::PUT || t->finished)
                    {
                        continue;
                    }

                    t->ws_session_url = forcedUrl;
                    client->transfercacheadd(t, nullptr);
                    updated = true;
                    break;
                }
            }
            promise->set_value(updated);
        });

    impl->executeOnThreadForTesting(exec);

    if (future.wait_for(std::chrono::seconds(timeoutSeconds)) != std::future_status::ready)
    {
        return false;
    }

    return future.get();
}

} // namespace mega::test::wsupload
