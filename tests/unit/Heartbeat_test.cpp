/**
 * (c) 2026 by Mega Limited, New Zealand
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

// Unit coverage for the API request heartbeat (HTTP 103) support relies on HttpReq's
// platform-independent status-line handling so every HttpIO backend shares the same
// heartbeat/response-start state transitions.

#include <gtest/gtest.h>
#include <mega/http.h>
#include <mega/testhooks.h>

#include <string>
#include <string_view>

using namespace mega;

namespace
{

int parse(std::string_view line)
{
    return HttpReq::statusCodeFromHeaderLine(line.data(), line.size());
}

} // namespace

TEST(Heartbeat, ParsesInformationalStatusCodes)
{
    EXPECT_EQ(parse("HTTP/1.1 103 HB\r\n"), 103);
    EXPECT_EQ(parse("HTTP/1.1 100 Continue\r\n"), 100);
    EXPECT_EQ(parse("HTTP/1.1 102 Processing\r\n"), 102);
}

TEST(Heartbeat, ParsesFinalStatusCodes)
{
    EXPECT_EQ(parse("HTTP/1.1 200 OK\r\n"), 200);
    EXPECT_EQ(parse("HTTP/1.1 206 Partial Content\r\n"), 206);
    EXPECT_EQ(parse("HTTP/1.1 402 Payment Required\r\n"), 402);
    EXPECT_EQ(parse("HTTP/1.1 500 Internal Server Error\r\n"), 500);
}

TEST(Heartbeat, ToleratesVersionAndTerminatorVariants)
{
    EXPECT_EQ(parse("HTTP/2 200\r\n"), 200); // HTTP/2 style, no minor version, no reason phrase
    EXPECT_EQ(parse("HTTP/1.0 200 OK\n"), 200); // bare LF terminator
    EXPECT_EQ(parse("HTTP/1.1 103 HB"), 103); // no terminator at all
    EXPECT_EQ(parse("HTTP/1.1 500"), 500); // no reason phrase
}

TEST(Heartbeat, RejectsNonStatusLines)
{
    EXPECT_EQ(parse("Content-Length: 5\r\n"), 0);
    EXPECT_EQ(parse("X-Hashcash: 1:100:...\r\n"), 0);
    EXPECT_EQ(parse(""), 0);
    EXPECT_EQ(parse("HTTP/"), 0); // prefix only, no code
    EXPECT_EQ(parse("HTTP/1.1 \r\n"), 0); // no digits where the code should be
    EXPECT_EQ(parse("http/1.1 200 OK\r\n"), 0); // case-sensitive prefix, as cURL delivers it
}

TEST(Heartbeat, ClassifiesHeartbeatVsResponse)
{
    // A 1xx status line is a heartbeat; anything >= 200 marks the start of the actual response.
    auto isHeartbeat = [](std::string_view line)
    {
        const int code = parse(line);
        return code >= 100 && code < 200;
    };

    // The 103 HB heartbeat must be recognised as informational so it refreshes the request
    // timer without ending the pre-response phase...
    EXPECT_TRUE(isHeartbeat("HTTP/1.1 103 HB\r\n"));
    EXPECT_TRUE(isHeartbeat("HTTP/1.1 100 Continue\r\n"));

    // ...while the final status line must not, so it flips the request into the response phase.
    EXPECT_FALSE(isHeartbeat("HTTP/1.1 200 OK\r\n"));
    EXPECT_FALSE(isHeartbeat("HTTP/1.1 404 Not Found\r\n"));
    EXPECT_FALSE(isHeartbeat("Content-Length: 5\r\n"));
}

// Deterministic coverage of the shared status-line wiring (no server, no network): a received
// 1xx status line must not start the response, while a final (>= 200) status line must set
// HttpReq::mResponseStarted (which hands control from the heartbeat timeout to the network
// timeout).
TEST(Heartbeat, ProcessStatusLineMarksResponseStartedOnlyForFinalStatus)
{
    HttpReq req;
    req.httpio = nullptr;
    req.contentlength = -1;

    req.mResponseStarted = false;
    const std::string hb = "HTTP/1.1 103 HB\r\n";
    EXPECT_TRUE(req.processStatusLine(hb.data(), hb.size()));
    EXPECT_FALSE(req.mResponseStarted) << "a 1xx heartbeat must not start the response";

    req.mResponseStarted = false;
    req.contentlength = -1;
    const std::string ok = "HTTP/1.1 200 OK\r\n";
    EXPECT_TRUE(req.processStatusLine(ok.data(), ok.size()));
    EXPECT_TRUE(req.mResponseStarted) << "a final status line must start the response";
}

// A 1xx heartbeat must refresh the request's lastdata timer (this is what defers the heartbeat
// timeout while heartbeats keep arriving); a non-status header line must not.
TEST(Heartbeat, ProcessStatusLineRefreshesLastdataOnHeartbeat)
{
    HttpReq req;
    req.httpio = nullptr;

    req.contentlength = -1;
    req.lastdata = NEVER;
    const std::string hb = "HTTP/1.1 103 HB\r\n";
    EXPECT_TRUE(req.processStatusLine(hb.data(), hb.size()));
    EXPECT_EQ(req.lastdata, Waiter::ds) << "a 1xx heartbeat must refresh req->lastdata";

    req.contentlength = -1;
    req.lastdata = NEVER;
    const std::string other = "X-Not-A-Status: value\r\n";
    EXPECT_FALSE(req.processStatusLine(other.data(), other.size()));
    EXPECT_EQ(req.lastdata, NEVER) << "a non-1xx line must not be treated as a heartbeat";
}

TEST(Heartbeat, BodyDataMarksResponseStarted)
{
    HttpReq req;
    req.mResponseStarted = false;

    char body = 'x';
    req.put(&body, 1);

    EXPECT_TRUE(req.mResponseStarted);
}

#ifdef MEGASDK_DEBUG_TEST_HOOKS_ENABLED
// The shared status-line handler fires the heartbeat hook for a 1xx status, and not for a final
// one.
TEST(Heartbeat, ProcessStatusLineFiresHeartbeatHookForInformationalStatus)
{
    HttpReq req;
    req.httpio = nullptr;
    req.contentlength = -1;

    int seenCode = 0;
    globalMegaTestHooks.onHeartbeatReceived = [&](int code, uint32_t)
    {
        seenCode = code;
    };

    const std::string hb = "HTTP/1.1 103 HB\r\n";
    EXPECT_TRUE(req.processStatusLine(hb.data(), hb.size()));
    EXPECT_EQ(seenCode, 103) << "a 1xx status must fire the heartbeat hook with its code";

    seenCode = 0;
    req.contentlength = -1;
    const std::string ok = "HTTP/1.1 200 OK\r\n";
    EXPECT_TRUE(req.processStatusLine(ok.data(), ok.size()));
    EXPECT_EQ(seenCode, 0) << "a final status must not fire the heartbeat hook";

    globalMegaTestHooks.onHeartbeatReceived = nullptr;
}
#endif
