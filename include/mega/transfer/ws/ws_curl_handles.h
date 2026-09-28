/**
 * @file mega/transfer/ws/ws_curl_handles.h
 * @brief The libcurl handle pair behind one websocket-upload connection and the
 *        single teardown sequence for it. Shared by CurlHttpIO::wsHandshake
 *        (src/posix/net.cpp), which produces the pair, and WsConn
 *        (src/transfer/ws/ws_conn.cpp), which owns it for the connection's life.
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

#ifndef MEGA_TRANSFER_WS_WS_CURL_HANDLES_H
#define MEGA_TRANSFER_WS_WS_CURL_HANDLES_H 1

#ifdef MEGA_USE_WSUPLOAD

#include <curl/curl.h>

namespace mega
{
namespace ws
{

// Poll slice for every SDK-driven curl_multi_poll() in the websocket-upload engine: the
// WsPoolMgr IO loop and the WS handshake loop. It is the upper bound on how long an engine
// stop request can go unnoticed by a thread sleeping inside libcurl, so it must stay well
// inside the 3000 ms locallogout quiesce window (MegaClient::wsLocallogoutCleanup).
constexpr int kWsCurlPollSliceMs = 500;

// The libcurl handles of one websocket connection.
//
// `easy` was driven to a completed CONNECT_ONLY=2 handshake by `multi` (the SDK runs
// the curl_multi loop itself instead of calling curl_easy_perform, so a stop request can
// be honoured between libcurl calls in every connect phase, on every libcurl version).
//
// Invariants (see WsConn::adoptCurl / destroyCurl and CurlHttpIO::wsHandshake):
//  - Paired: both members are null or both are non-null. They are created, handed over and
//    destroyed together, always on the same thread as each other.
//  - Resident: `easy` stays added to `multi` until destroyWsCurlHandles(). curl_ws_send/recv
//    locate the connection through the connection pool of the multi the easy handle belongs
//    to, and curl_multi_remove_handle() terminates a CONNECT_ONLY connection immediately
//    (lib/multi.c, identical in every supported libcurl), nobody calls curl_multi_* on `multi`
//    after the handshake completed.
struct WsCurlHandles
{
    CURL* easy{nullptr};
    CURLM* multi{nullptr};

    explicit operator bool() const noexcept
    {
        return easy != nullptr;
    }
};

// The one teardown sequence for a handle pair, in the order libcurl mandates
// (curl_multi_cleanup(3): remove the easy handle, clean the easy handle, then clean the multi
// handle). Idempotent and safe on a half-initialised pair. Does not block, provided QUICK_EXIT
// is set (the easy CURLOPT_QUICK_EXIT on curl <= 8.19, the multi CURLMOPT_QUICK_EXIT on
// curl >= 8.20, where curl_multi_cleanup otherwise joins resolver threads still inside
// getaddrinfo; CurlHttpIO::wsHandshake sets both): removing a CONNECT_ONLY easy handle closes
// its connection abortively, and the multi cleanup runs no graceful shutdown outside libcurl
// debug builds.
inline void destroyWsCurlHandles(WsCurlHandles& h) noexcept
{
    if (h.easy && h.multi)
    {
        curl_multi_remove_handle(h.multi, h.easy);
    }
    if (h.easy)
    {
        curl_easy_cleanup(h.easy);
        h.easy = nullptr;
    }
    if (h.multi)
    {
        curl_multi_cleanup(h.multi);
        h.multi = nullptr;
    }
}

} // namespace ws
} // namespace mega

#endif // MEGA_USE_WSUPLOAD

#endif // MEGA_TRANSFER_WS_WS_CURL_HANDLES_H
