/**
 * (c) 2026 by MEGA Privacy Kft, Csomad, Hungary
 *
 * This file is part of the MEGA SDK - Client Access Engine.
 *
 * SDK-6476: parallel WS upload handshakes read CurlHttpIO's proxy/DNS/user-agent configuration
 * on pool worker threads while MegaApi::setProxySettings / disconnect() mutate it on the client
 * thread. These cells race the two paths on a bare CurlHttpIO.
 */

#include "mega.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <string>
#include <thread>
#include <vector>

namespace
{

#if defined(__SANITIZE_THREAD__) || (defined(__has_feature) && __has_feature(thread_sanitizer))
constexpr int kWriterIterations = 1000;
#else
constexpr int kWriterIterations = 2000;
#endif

// Two complete, disjoint CUSTOM proxy generations. Strings exceed the small-string buffer so a
// torn read is a heap access, not an in-object one.
mega::Proxy customProxy(const std::string& url,
                        const std::string& username,
                        const std::string& password)
{
    mega::Proxy proxy;
    proxy.setProxyType(mega::Proxy::CUSTOM);
    proxy.setProxyURL(url);
    proxy.setCredentials(username, password);
    return proxy;
}

const mega::Proxy kProxyA = customProxy("http://proxy-a.generation-a.invalid:1111",
                                        "username-of-generation-a",
                                        "password-of-generation-a");
const mega::Proxy kProxyB = customProxy("socks5://proxy-b.generation-b.invalid:2222",
                                        "username-of-generation-b",
                                        "password-of-generation-b");

} // namespace

#ifdef MEGA_USE_WSUPLOAD

// Pool workers call configureWsEasy() while the client thread calls setproxy(). On the unfixed
// tree this is a data race on std::string members (TSAN reports it; a torn c_str() handed to
// libcurl can also crash). Runs unmodified on pristine develop: both APIs are public.
TEST(CurlHttpIoConfig, ConfigureWsEasyRacesSetproxy)
{
    mega::CurlHttpIO io;
    std::atomic<bool> stop{false};

    std::vector<std::thread> readers;
    for (int i = 0; i < 3; ++i)
    {
        readers.emplace_back(
            [&io, &stop]
            {
                CURL* easy = curl_easy_init();
                if (!easy)
                {
                    return;
                }
                while (!stop.load(std::memory_order_acquire))
                {
                    io.configureWsEasy(easy, /*isPostJson*/ false);
                }
                // Detach from the shared cache before destroying the handle (curlsh outlives it).
                curl_easy_setopt(easy, CURLOPT_SHARE, nullptr);
                curl_easy_cleanup(easy);
            });
    }

    for (int i = 0; i < kWriterIterations; ++i)
    {
        io.setproxy((i & 1) ? kProxyB : kProxyA);
    }

    stop.store(true, std::memory_order_release);
    for (auto& reader: readers)
    {
        reader.join();
    }
    SUCCEED() << "no crash; data races are reported by ThreadSanitizer";
}

#else

TEST(CurlHttpIoConfig, ConfigureWsEasyRacesSetproxyDisabledWithoutWsUpload)
{
    GTEST_SKIP() << "MEGA_USE_WSUPLOAD is disabled";
}

#endif

namespace
{

// Every reader must observe exactly generation A or generation B — never a mix, and never the
// transient "proxyip cleared but type/credentials of the previous generation" state setproxy()
// passes through. The expected tuples are captured single-threaded from the same accessor so the
// test does not hard-code crackURI's formatting.
bool sameGeneration(const mega::CurlHttpIO::NetConfigSnapshot& a,
                    const mega::CurlHttpIO::NetConfigSnapshot& b)
{
    return a.proxyip == b.proxyip && a.proxyschema == b.proxyschema &&
           a.proxyusername == b.proxyusername && a.proxypassword == b.proxypassword &&
           a.proxytype == b.proxytype && a.useragent == b.useragent && a.dnsservers == b.dnsservers;
}

} // namespace

TEST(CurlHttpIoConfig, NetConfigSnapshotIsCoherentUnderSetproxy)
{
    mega::CurlHttpIO io;

    io.setproxy(kProxyA);
    const auto generationA = io.netConfigSnapshot();
    io.setproxy(kProxyB);
    const auto generationB = io.netConfigSnapshot();
    ASSERT_FALSE(sameGeneration(generationA, generationB));
    ASSERT_FALSE(generationA.proxyip.empty());
    ASSERT_FALSE(generationB.proxyip.empty());

    std::atomic<bool> stop{false};
    std::atomic<std::size_t> samples{0};
    std::atomic<std::size_t> incoherent{0};
    std::vector<std::thread> readers;
    for (int i = 0; i < 3; ++i)
    {
        readers.emplace_back(
            [&]
            {
                while (!stop.load(std::memory_order_acquire))
                {
                    const auto snapshot = io.netConfigSnapshot();
                    samples.fetch_add(1, std::memory_order_relaxed);
                    if (!sameGeneration(snapshot, generationA) &&
                        !sameGeneration(snapshot, generationB))
                    {
                        incoherent.fetch_add(1, std::memory_order_relaxed);
                    }
                }
            });
    }

    // Flip generations for at least 1 s AND until the readers have taken >= kMinSamples
    // snapshots (bounded at 10 s), so the meaningfulness bar below holds regardless of how
    // slowly an instrumented (TSAN/ASAN) build spins.
    constexpr std::size_t kMinSamples = 1000;
    const auto start = std::chrono::steady_clock::now();
    const auto minEnd = start + std::chrono::milliseconds(1000);
    const auto maxEnd = start + std::chrono::milliseconds(10000);
    for (int i = 0;; ++i)
    {
        io.setproxy((i & 1) ? kProxyB : kProxyA);
        const auto now = std::chrono::steady_clock::now();
        if (now >= maxEnd || (now >= minEnd && samples.load() >= kMinSamples))
        {
            break;
        }
    }

    stop.store(true, std::memory_order_release);
    for (auto& reader: readers)
    {
        reader.join();
    }

    EXPECT_GE(samples.load(), kMinSamples);
    EXPECT_EQ(incoherent.load(), 0u) << "torn proxy configuration observed by a snapshot reader";
}
