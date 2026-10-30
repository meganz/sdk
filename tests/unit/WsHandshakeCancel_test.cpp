/**
 * (c) 2026 by MEGA Privacy Kft, Csomad, Hungary
 *
 * This file is part of the MEGA SDK - Client Access Engine.
 *
 * SDK-6476 / SDK-6565: a parallel WS upload handshake blocked on a black-holed endpoint or
 * proxy must be cancellable when the upload engine stops, so locallogout can destroy the
 * engine instead of leaking it. These cells drive CurlHttpIO::wsHandshake against a real
 * loopback black hole (TCP accepts, TLS / HTTP upgrade never answered) and cancel it through
 * the stop flag. They assert behaviour, not mechanism: the handshake is driven by an SDK-owned
 * curl_multi loop (SDK-6565), so the bounds below must hold on every libcurl version. No test
 * hooks are used, so they also run in Release/hooks-off builds and exercise the platform TLS
 * backend on Linux, Windows and macOS.
 */

#include "mega.h"
#include "tcp_blackhole_listener.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <string>
#include <thread>

#ifdef MEGA_USE_WSUPLOAD

namespace
{

using namespace std::chrono;

// Runs CurlHttpIO::wsHandshake on its own thread (libcurl: one easy handle per thread).
class HandshakeRun
{
public:
    HandshakeRun(mega::CurlHttpIO& io,
                 std::string url,
                 long timeoutMs,
                 const std::atomic<bool>* stopFlag):
        mThread(
            [this, &io, url = std::move(url), timeoutMs, stopFlag]
            {
                mega::ws::WsCurlHandles handles = io.wsHandshake(url, timeoutMs, &mErr, stopFlag);
                mEndedAt = steady_clock::now();
                mSucceeded = static_cast<bool>(handles);
                // Paired invariant (ws_curl_handles.h): a failed or cancelled handshake hands
                // back nothing at all, a successful one hands back both handles.
                mPaired = (handles.easy == nullptr) == (handles.multi == nullptr);
                mega::ws::destroyWsCurlHandles(handles);
            })
    {}

    ~HandshakeRun()
    {
        join();
    }

    void join()
    {
        if (mThread.joinable())
        {
            mThread.join();
        }
    }

    // Only valid after join().
    bool succeeded() const
    {
        return mSucceeded;
    }

    bool paired() const
    {
        return mPaired;
    }

    const std::string& err() const
    {
        return mErr;
    }

    steady_clock::time_point endedAt() const
    {
        return mEndedAt;
    }

private:
    std::string mErr;
    bool mSucceeded{false};
    bool mPaired{false};
    steady_clock::time_point mEndedAt{};
    std::thread mThread;
};

long long msSince(steady_clock::time_point from, steady_clock::time_point to)
{
    return duration_cast<milliseconds>(to - from).count();
}

class WsHandshakeCancelTest: public ::testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_TRUE(mBlackhole.listening()) << "could not open a loopback listener";
        // A bare CurlHttpIO has proxytype AUTO, which leaves CURLOPT_PROXY unset and lets
        // libcurl honour ambient *_proxy environment variables on CI hosts. Force NONE so the
        // connect really goes to the loopback black hole.
        mega::Proxy noProxy;
        noProxy.setProxyType(mega::Proxy::NONE);
        mIo.setproxy(noProxy);
    }

    // Handshake timeout: generous enough that a returned-by-timeout result is unmistakable
    // from a cancellation (which must complete well inside the 3000 ms locallogout budget).
    static constexpr long kHandshakeTimeoutMs = 8000;

    sdk_test::TcpBlackholeListener mBlackhole;
    mega::CurlHttpIO mIo;
};

} // namespace

// The worker is accepted by the black hole (so it is inside the TLS phase of curl_easy_perform),
// then the stop flag flips: the handshake must abort with the SDK cancellation sentinel well
// inside the 3000 ms locallogout quiesce budget, not run to the 8000 ms handshake timeout.
TEST_F(WsHandshakeCancelTest, StopFlagAbortsBlackholedTlsHandshake)
{
    std::atomic<bool> stop{false};
    HandshakeRun run(mIo, mBlackhole.url("wss", "/ul/blackhole"), kHandshakeTimeoutMs, &stop);

    ASSERT_TRUE(mBlackhole.waitForAccept(seconds(10)))
        << "libcurl never connected to the loopback black hole";
    std::this_thread::sleep_for(milliseconds(100));

    const auto stopRequestedAt = steady_clock::now();
    stop.store(true, std::memory_order_release);
    run.join();

    const auto cancelMs = msSince(stopRequestedAt, run.endedAt());
    EXPECT_FALSE(run.succeeded()) << "a black-holed handshake cannot succeed";
    EXPECT_TRUE(run.paired());
    EXPECT_EQ(run.err(), mega::CurlHttpIO::kWsHandshakeCancelledErr)
        << "expected the cancellation sentinel, got: " << run.err();
    EXPECT_LT(cancelMs, 3000)
        << "cancellation took " << cancelMs
        << " ms; the locallogout quiesce budget is 3000 ms (SDK-6476 leak fallback)";
    // The SDK-owned loop notices the flag at the latest when its current poll slice ends; the
    // extra slices are headroom for sanitizer builds and loaded CI hosts.
    EXPECT_LT(cancelMs, 3 * mega::ws::kWsCurlPollSliceMs)
        << "cancellation took " << cancelMs << " ms; poll slice is " << mega::ws::kWsCurlPollSliceMs
        << " ms";
}

// The flag is already set before the call: the loop checks it before its first libcurl call,
// so no network operation is started at all and the handshake returns at once with both
// handles destroyed.
TEST_F(WsHandshakeCancelTest, PresetStopFlagAbortsImmediately)
{
    std::atomic<bool> stop{true};
    const auto startedAt = steady_clock::now();
    HandshakeRun run(mIo, mBlackhole.url("wss", "/ul/blackhole"), kHandshakeTimeoutMs, &stop);
    run.join();

    const auto elapsedMs = msSince(startedAt, run.endedAt());
    EXPECT_FALSE(run.succeeded());
    EXPECT_TRUE(run.paired());
    EXPECT_EQ(run.err(), mega::CurlHttpIO::kWsHandshakeCancelledErr) << run.err();
    EXPECT_LT(elapsedMs, mega::ws::kWsCurlPollSliceMs)
        << "preset stop flag not honoured before the first libcurl call: " << elapsedMs << " ms";
    EXPECT_FALSE(mBlackhole.waitForAccept(milliseconds(200)))
        << "a handshake cancelled before its first libcurl call must not open a TCP connection";
}

// Control: without a stop flag the handshake still ends by the libcurl timeout with the generic
// error text (the serial/baton path passes nullptr). The SDK-owned loop adds at most one poll
// slice of latency to CURLOPT_TIMEOUT_MS.
TEST_F(WsHandshakeCancelTest, NoStopFlagStillTimesOut)
{
    constexpr long kShortTimeoutMs = 1500;
    const auto startedAt = steady_clock::now();
    HandshakeRun run(mIo, mBlackhole.url("wss", "/ul/blackhole"), kShortTimeoutMs, nullptr);
    run.join();

    const auto elapsedMs = msSince(startedAt, run.endedAt());
    EXPECT_FALSE(run.succeeded());
    EXPECT_TRUE(run.paired());
    EXPECT_NE(run.err(), mega::CurlHttpIO::kWsHandshakeCancelledErr);
    EXPECT_NE(run.err().find("handshake failed"), std::string::npos) << run.err();
    EXPECT_GE(elapsedMs, 1000) << "timed out too early: " << elapsedMs << " ms";
    EXPECT_LT(elapsedMs, kShortTimeoutMs + 2 * mega::ws::kWsCurlPollSliceMs)
        << "CURLOPT_TIMEOUT_MS not honoured within one poll slice: " << elapsedMs << " ms";
}

// Plaintext ws://: TCP connects, libcurl sends the HTTP/1.1 Upgrade and waits for the 101 that
// never comes. Proves cancellation also covers the upgrade (DO/PERFORMING) phase independently
// of the TLS backend.
TEST_F(WsHandshakeCancelTest, StopFlagAbortsDuringWsUpgrade)
{
    std::atomic<bool> stop{false};
    HandshakeRun run(mIo, mBlackhole.url("ws", "/ul/blackhole"), kHandshakeTimeoutMs, &stop);

    ASSERT_TRUE(mBlackhole.waitForAccept(seconds(10)));
    std::this_thread::sleep_for(milliseconds(100));

    const auto stopRequestedAt = steady_clock::now();
    stop.store(true, std::memory_order_release);
    run.join();

    const auto cancelMs = msSince(stopRequestedAt, run.endedAt());
    EXPECT_FALSE(run.succeeded());
    EXPECT_TRUE(run.paired());
    EXPECT_EQ(run.err(), mega::CurlHttpIO::kWsHandshakeCancelledErr) << run.err();
    EXPECT_LT(cancelMs, 3000) << "cancellation took " << cancelMs << " ms";
    EXPECT_LT(cancelMs, 3 * mega::ws::kWsCurlPollSliceMs)
        << "cancellation took " << cancelMs << " ms; poll slice is " << mega::ws::kWsCurlPollSliceMs
        << " ms";
}

// Teardown sequence of the handle pair (ws_curl_handles.h): destroying a pair is idempotent and
// tolerates a half-initialised pair, so every failure branch in wsHandshake and every WsConn
// teardown path can call it unconditionally.
TEST(WsCurlHandlesTest, DestroyIsIdempotentAndTolerantOfPartialPairs)
{
    mega::ws::WsCurlHandles empty;
    EXPECT_FALSE(static_cast<bool>(empty));
    mega::ws::destroyWsCurlHandles(empty); // no-op
    mega::ws::destroyWsCurlHandles(empty); // still a no-op

    mega::ws::WsCurlHandles easyOnly;
    easyOnly.easy = curl_easy_init();
    ASSERT_NE(easyOnly.easy, nullptr);
    mega::ws::destroyWsCurlHandles(easyOnly);
    EXPECT_EQ(easyOnly.easy, nullptr);
    EXPECT_EQ(easyOnly.multi, nullptr);

    mega::ws::WsCurlHandles multiOnly;
    multiOnly.multi = curl_multi_init();
    ASSERT_NE(multiOnly.multi, nullptr);
    mega::ws::destroyWsCurlHandles(multiOnly);
    EXPECT_EQ(multiOnly.easy, nullptr);
    EXPECT_EQ(multiOnly.multi, nullptr);

    mega::ws::WsCurlHandles pair;
    pair.easy = curl_easy_init();
    pair.multi = curl_multi_init();
    ASSERT_NE(pair.easy, nullptr);
    ASSERT_NE(pair.multi, nullptr);
    ASSERT_EQ(curl_multi_add_handle(pair.multi, pair.easy), CURLM_OK);
    EXPECT_TRUE(static_cast<bool>(pair));
    mega::ws::destroyWsCurlHandles(pair);
    EXPECT_EQ(pair.easy, nullptr);
    EXPECT_EQ(pair.multi, nullptr);
    mega::ws::destroyWsCurlHandles(pair); // idempotent
}

#else

TEST(WsHandshakeCancelTest, DisabledWithoutWsUpload)
{
    GTEST_SKIP() << "MEGA_USE_WSUPLOAD is disabled";
}

#endif
