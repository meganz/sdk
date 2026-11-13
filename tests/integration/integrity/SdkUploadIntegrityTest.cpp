/**
 * @file SdkUploadIntegrityTest.cpp
 * @brief Upload integrity core (SDK-6795): every upload made here must end with a node key whose
 * meta-MAC covers every chunk of the file.
 *
 * Strict on purpose. The apps keep the download-side gap tolerance for keys already stored; these
 * tests never rely on it. Each one compares the node's meta-MAC with the local file's, downloads
 * and hashes the file, and fails on any download gap match. A hash check alone cannot see a gap
 * key: the bytes on the server are intact. Random content gives every chunk a distinct MAC.
 *
 * - Triggers: the real paths that built gap keys before SDK-6795 (a lost completion recovered by
 *   the tail watchdog at 13/68/204 chunks, a corrupt completion token, a resume with acks still
 *   pending after the server holds every byte).
 * - Injected gaps: onUploadTokenChunkMacs drops chosen chunk MACs at the first upload token,
 *   the gap an incomplete merge leaves. The upload must notice, start again and end with a full
 *   key. Shapes: the S4 field file (chunks 0 and 12), one chunk at the start, middle or end, every
 *   chunk, a missing tail, holes plus a missing tail, the only chunk of a small file, a 1-byte last
 *   chunk, a large file missing most of its tail, a hole after the condensed prefix of a map with
 *   more than 101 entries, and random shapes.
 * - Background media upload: the app encrypts the file in portions; completing with a saved state
 *   that misses portions must be refused.
 * - Storage red mid-upload (S4's open case): the account goes red while the upload is early, half
 *   way and near its end, then recovers. The file must end with a full key. Disabled until a
 *   separate fix: today such an upload never ends (see the test).
 * - A transfer cache written before SDK-6795 (HTTP builds): an upload restored with its token
 *   and a key built without some chunk MACs must upload again instead of creating the node.
 *
 * With MEGA_USE_WSUPLOAD every upload goes through the WS engine; in a build without it the same
 * injected-gap and background cells run through the HTTP upload (TransferSlot). The trigger and
 * storage-red cells drive the WS engine and exist in WS builds only. Every test needs the debug
 * test hooks and skips in hooks-off builds.
 */

#include "api_request_hook.h"
#include "integrity/headers/MetaMacCheck.h"
#include "mega/scoped_helpers.h"
#include "mega/testhooks.h"
#include "mega/utils.h"
#include "megaapi.h"
#include "sdk_test_utils.h"
#include "SdkTest_test.h"
#include "test.h"
#ifdef MEGA_USE_WSUPLOAD
#include "wsupload/headers/WsQuotaClientThread.h"
#include "wsupload/headers/WsUploadHelpers.h"
#endif

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <future>
#include <memory>
#include <mutex>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace mega::test::integrity
{

#ifdef MEGA_USE_WSUPLOAD
class SdkUploadIntegrityTest: public SdkWsUploadTest
{};
#else
class SdkUploadIntegrityTest: public SdkTest
{};
#endif

namespace
{

constexpr size_t kFieldFileSize = 9930566; // 13 chunks: the size of the S4 field file

MegaUploadOptions integrityUploadOptions()
{
    MegaUploadOptions options;
    options.mtime = MegaUploadOptions::INVALID_CUSTOM_MOD_TIME;
    return options;
}

// A random local file in the working directory, removed when the object goes.
class LocalUploadFile
{
public:
    LocalUploadFile(const char* prefix, size_t size):
        mName{std::string{prefix} + std::to_string(size) + "_" +
              std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".bin"}
    {
        sdk_test::createRandomFile(fs::path(mName), size);
        mPath = fs::absolute(u8path_compat(mName));
        mHash = sdk_test::hashFileHex(mPath);
    }

    ~LocalUploadFile()
    {
        std::error_code ignore;
        fs::remove(mPath, ignore);
    }

    LocalUploadFile(const LocalUploadFile&) = delete;
    LocalUploadFile& operator=(const LocalUploadFile&) = delete;

    const std::string& name() const
    {
        return mName;
    }

    const fs::path& path() const
    {
        return mPath;
    }

    const std::string& hash() const
    {
        return mHash;
    }

private:
    std::string mName;
    fs::path mPath;
    std::string mHash;
};

void removeNode(MegaApi& api, MegaNode* node)
{
    if (!node)
    {
        return;
    }
    RequestTracker rt(&api);
    api.remove(node, &rt);
    rt.waitForResult(60);
}

// Uploads into the root folder; nullptr (with a test failure) if it does not complete.
std::unique_ptr<MegaNode> uploadToRoot(MegaApi& api, const LocalUploadFile& file)
{
    std::unique_ptr<MegaNode> root{api.getRootNode()};
    EXPECT_TRUE(root) << "no root node";
    if (!root)
    {
        return nullptr;
    }
    TransferTracker ut(&api);
    auto uploadOptions = integrityUploadOptions();
    api.startUpload(file.name(), root.get(), nullptr, &uploadOptions, &ut);
    const auto result = ut.waitForResult(600);
    EXPECT_EQ(result, API_OK) << "upload of " << file.name() << " did not complete";
    if (result != API_OK)
    {
        return nullptr;
    }
    std::unique_ptr<MegaNode> node(api.getNodeByHandle(ut.resultNodeHandle));
    EXPECT_TRUE(node) << "uploaded node not found";
    return node;
}

#ifdef MEGA_USE_WSUPLOAD
// Drops the first completion frame; the tail-completion watchdog (or the silent-byte-shortfall
// watchdog, when the final ack travelled with the completion) then recovers the upload.
void runLostCompletionRetry(SdkTest& t, size_t fileSize)
{
    ScopedMacGapLogWatch watch;
    MegaApi& api = *t.megaApi[0];
    LocalUploadFile file("integrity_retry_", fileSize);

    auto cleanupTransfers = makeScopedDestructor(
        [&t]()
        {
            (void)t.synchronousCancelTransfers(0, MegaTransfer::TYPE_UPLOAD);
        });
    auto clearHooks = makeScopedDestructor(
        []()
        {
            std::lock_guard<std::mutex> hookGuard(globalMegaTestHooks.mMutex);
            globalMegaTestHooks.onWsUploadDropCompletion = nullptr;
            globalMegaTestHooks.onWsTailCompletionRecovery = nullptr;
            globalMegaTestHooks.onWsSilentByteShortfallRecovery = nullptr;
            globalMegaTestHooks.onWsUploadTailCompletionTimeoutDs = nullptr;
        });

    // Heap-owned counters captured by value: a hook copy may outlive this frame on a worker.
    auto dropped = std::make_shared<std::atomic<int>>(0);
    auto tailRecoveries = std::make_shared<std::atomic<int>>(0);
    auto shortfallRecoveries = std::make_shared<std::atomic<int>>(0);
    {
        std::lock_guard<std::mutex> hookGuard(globalMegaTestHooks.mMutex);
        globalMegaTestHooks.onWsUploadTailCompletionTimeoutDs = [](dstime& window)
        {
            window = 50; // 5 s instead of the 60 s default
        };
        globalMegaTestHooks.onWsUploadDropCompletion = [dropped](std::uint32_t)
        {
            int expected = 0;
            return dropped->compare_exchange_strong(expected, 1);
        };
        globalMegaTestHooks.onWsTailCompletionRecovery =
            [tailRecoveries](std::uint32_t, ::mega::ws::WsPool*)
        {
            tailRecoveries->fetch_add(1);
        };
        globalMegaTestHooks.onWsSilentByteShortfallRecovery =
            [shortfallRecoveries](std::uint32_t, ::mega::ws::WsPool*)
        {
            shortfallRecoveries->fetch_add(1);
        };
    }

    auto node = uploadToRoot(api, file);
    ASSERT_TRUE(node);
    EXPECT_EQ(dropped->load(), 1) << "the completion frame was not dropped: repro did not engage";
    EXPECT_GE(tailRecoveries->load() + shortfallRecoveries->load(), 1)
        << "no watchdog recovered the lost completion";

    std::ostringstream summary;
    summary << "cell=LostCompletionRetry size=" << fileSize
            << " tailRecoveries=" << tailRecoveries->load()
            << " shortfallRecoveries=" << shortfallRecoveries->load();
    expectFullMetaMac(api, node.get(), file.path(), file.hash(), watch, summary.str());
    removeNode(api, node.get());
}
#endif

// At the first upload token, drops the MACs of `dropChunks` (chunk indexes): the gap an
// incomplete merge leaves. The upload must notice it (the token gate logs "Upload token with ...
// chunk MACs"), start again, and end with a key over every chunk.
void runInjectedGap(SdkTest& t,
                    size_t fileSize,
                    const std::set<size_t>& dropChunks,
                    const char* shape,
                    int tokensToBreak = 1)
{
    ScopedMacGapLogWatch watch;
    MegaApi& api = *t.megaApi[0];
    LocalUploadFile file("integrity_gap_", fileSize);

    auto cleanupTransfers = makeScopedDestructor(
        [&t]()
        {
            (void)t.synchronousCancelTransfers(0, MegaTransfer::TYPE_UPLOAD);
        });
    auto clearHook = makeScopedDestructor(
        []()
        {
            std::lock_guard<std::mutex> hookGuard(globalMegaTestHooks.mMutex);
            globalMegaTestHooks.onUploadTokenChunkMacs = nullptr;
        });

    auto dropped = std::make_shared<std::atomic<int>>(-1); // MACs dropped at the first token
    auto tokens = std::make_shared<std::atomic<int>>(0); // upload tokens seen
    {
        std::lock_guard<std::mutex> hookGuard(globalMegaTestHooks.mMutex);
        globalMegaTestHooks.onUploadTokenChunkMacs =
            [dropped, tokens, dropChunks, tokensToBreak](m_off_t size, chunkmac_map& macs)
        {
            if (tokens->fetch_add(1) >= tokensToBreak)
            {
                return; // later tokens keep every MAC
            }
            int count = 0;
            size_t index = 0;
            for (m_off_t pos = 0; pos < size; pos = ChunkedHash::chunkceil(pos, size), ++index)
            {
                if (dropChunks.count(index) && macs.dropChunkMacForTesting(pos))
                {
                    ++count;
                }
            }
            int none = -1;
            dropped->compare_exchange_strong(none, count);
        };
    }

    auto node = uploadToRoot(api, file);
    ASSERT_TRUE(node);
    EXPECT_EQ(dropped->load(), static_cast<int>(dropChunks.size()))
        << "the injected gap did not engage (shape " << shape << ")";
    const auto gate = watch.uploadGateLines();
    EXPECT_FALSE(gate.empty())
        << "the injected gap reached the node key without the upload noticing";

    std::ostringstream summary;
    summary << "cell=InjectedGap shape=" << shape << " size=" << fileSize
            << " dropped=" << dropped->load();
    expectFullMetaMac(api, node.get(), file.path(), file.hash(), watch, summary.str());
    EXPECT_EQ(tokens->load(), tokensToBreak + 1)
        << "expected " << tokensToBreak << " refused upload token(s), then one accepted";
    removeNode(api, node.get());
}

std::set<size_t> chunkRange(size_t first, size_t end)
{
    std::set<size_t> result;
    for (size_t i = first; i < end; ++i)
    {
        result.insert(i);
    }
    return result;
}

// "first+length,..." for a set of chunk indexes: the run list event 800042 reports.
std::string runsText(const std::set<size_t>& chunks)
{
    std::string out;
    size_t first = 0;
    size_t length = 0;
    auto flush = [&]()
    {
        if (length)
        {
            out += (out.empty() ? "" : ",") + std::to_string(first) + "+" + std::to_string(length);
        }
    };
    for (const auto c: chunks)
    {
        if (length && c == first + length)
        {
            ++length;
            continue;
        }
        flush();
        first = c;
        length = 1;
    }
    flush();
    return out;
}

} // namespace

#ifdef MEGASDK_DEBUG_TEST_HOOKS_ENABLED
#define INTEGRITY_REQUIRE_TEST_HOOKS() \
    do \
    {} \
    while (0)
#else
#define INTEGRITY_REQUIRE_TEST_HOOKS() GTEST_SKIP() << "Requires MEGASDK_DEBUG_TEST_HOOKS_ENABLED"
#endif

#define INTEGRITY_TEST_PROLOGUE(NAME) \
    LOG_info << "___TEST SdkUploadIntegrityTest." NAME "___"; \
    INTEGRITY_REQUIRE_TEST_HOOKS(); \
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1))

// ---------------------------------------------------------------------------------------------
// Triggers (WS engine)
// ---------------------------------------------------------------------------------------------

#ifdef MEGA_USE_WSUPLOAD
TEST_F(SdkUploadIntegrityTest, LostCompletionRetry13Chunks)
{
    INTEGRITY_TEST_PROLOGUE("LostCompletionRetry13Chunks");
    ASSERT_NO_FATAL_FAILURE(runLostCompletionRetry(*this, kFieldFileSize));
}

// 64 MiB: a gap this large fails the download (-14) instead of passing the tolerance.
TEST_F(SdkUploadIntegrityTest, LostCompletionRetry68Chunks)
{
    INTEGRITY_TEST_PROLOGUE("LostCompletionRetry68Chunks");
    ASSERT_NO_FATAL_FAILURE(runLostCompletionRetry(*this, 64 * 1024 * 1024));
}

// 200 MiB: the transfer map holds a condensed prefix (more than 101 entries) when the retry clears
// it.
TEST_F(SdkUploadIntegrityTest, LostCompletionRetry204Chunks)
{
    INTEGRITY_TEST_PROLOGUE("LostCompletionRetry204Chunks");
    ASSERT_NO_FATAL_FAILURE(runLostCompletionRetry(*this, 200 * 1024 * 1024));
}

// The first completion carries a token of the wrong length, so the transfer fails for a retry;
// before SDK-6795 the retry went back to the same WS slot, which already held the file.
TEST_F(SdkUploadIntegrityTest, CorruptTokenRetry)
{
    INTEGRITY_TEST_PROLOGUE("CorruptTokenRetry");
    ScopedMacGapLogWatch watch;
    MegaApi& api = *megaApi[0];
    LocalUploadFile file("integrity_token_", kFieldFileSize);

    auto cleanupTransfers = makeScopedDestructor(
        [this]()
        {
            (void)synchronousCancelTransfers(0, MegaTransfer::TYPE_UPLOAD);
        });
    auto clearHooks = makeScopedDestructor(
        []()
        {
            std::lock_guard<std::mutex> hookGuard(globalMegaTestHooks.mMutex);
            globalMegaTestHooks.onWsUploadCorruptToken = nullptr;
            globalMegaTestHooks.onWsUploadFailureDetached = nullptr;
        });

    auto corrupted = std::make_shared<std::atomic<int>>(0);
    auto detached = std::make_shared<std::atomic<int>>(0);
    {
        std::lock_guard<std::mutex> hookGuard(globalMegaTestHooks.mMutex);
        globalMegaTestHooks.onWsUploadCorruptToken =
            [corrupted](std::uint32_t, std::string& payload)
        {
            int expected = 0;
            if (!corrupted->compare_exchange_strong(expected, 1))
            {
                return false;
            }
            payload.resize(payload.empty() ? 0 : payload.size() - 1);
            return true;
        };
        globalMegaTestHooks.onWsUploadFailureDetached = [detached](const char*, bool)
        {
            detached->fetch_add(1);
        };
    }

    auto node = uploadToRoot(api, file);
    ASSERT_TRUE(node);
    EXPECT_EQ(corrupted->load(), 1)
        << "the completion token was not corrupted: repro did not engage";
    EXPECT_GE(detached->load(), 1) << "the invalid-token failure path did not run";

    std::ostringstream summary;
    summary << "cell=CorruptTokenRetry size=" << kFieldFileSize << " detached=" << detached->load();
    expectFullMetaMac(api, node.get(), file.path(), file.hash(), watch, summary.str());
    removeNode(api, node.get());
}

// Kill/restart while chunk acks are pending, after the server holds every byte. Every inbound
// frame after the first six is swallowed, so the acks of the last chunks and the completion never
// reach the client. Once every byte is on the wire the session is logged out locally and resumed;
// the resumed upload goes back to the slot that holds the whole file. One connection keeps the
// frame order deterministic; the watchdog windows are stretched so nothing fires before logout.
TEST_F(SdkUploadIntegrityTest, ResumeWithPendingAcks)
{
    INTEGRITY_TEST_PROLOGUE("ResumeWithPendingAcks");
    ScopedMacGapLogWatch watch;
    constexpr int framesBeforeSwallow = 6;
    LocalUploadFile file("integrity_resume_", kFieldFileSize);

    RequestTracker ct(megaApi[0].get());
    megaApi[0]->setMaxConnections(1, &ct);
    ASSERT_EQ(API_OK, ct.waitForResult(60)) << "setMaxConnections() failed or timed out";

    auto cleanupTransfers = makeScopedDestructor(
        [this]()
        {
            (void)synchronousCancelTransfers(0, MegaTransfer::TYPE_UPLOAD);
        });
    auto clearHooks = makeScopedDestructor(
        []()
        {
            globalMegaTestHooks.wsRecvSwallowHook.reset();
            std::lock_guard<std::mutex> hookGuard(globalMegaTestHooks.mMutex);
            globalMegaTestHooks.onWsUploadTailCompletionTimeoutDs = nullptr;
            globalMegaTestHooks.onWsUploadAckStallTimeoutDs = nullptr;
            globalMegaTestHooks.onWsChunkSent = nullptr;
        });

    auto sentEnd = std::make_shared<std::atomic<long long>>(0);
    {
        std::lock_guard<std::mutex> hookGuard(globalMegaTestHooks.mMutex);
        globalMegaTestHooks.onWsUploadTailCompletionTimeoutDs = [](dstime& window)
        {
            window = 6000; // 600 s: no tail/shortfall watchdog before the logout
        };
        globalMegaTestHooks.onWsUploadAckStallTimeoutDs = [](dstime& window)
        {
            window = 6000; // 600 s: no ack-stall reconnect before the logout
        };
        globalMegaTestHooks.onWsChunkSent =
            [sentEnd](const std::string&, std::uint32_t, m_off_t pos, int len, const std::string&)
        {
            const long long end = static_cast<long long>(pos) + len;
            long long current = sentEnd->load();
            while (end > current && !sentEnd->compare_exchange_weak(current, end))
            {}
        };
    }
    globalMegaTestHooks.wsRecvSwallowHook.configure(framesBeforeSwallow, 0);

    std::unique_ptr<MegaNode> rootnode{megaApi[0]->getRootNode()};
    ASSERT_TRUE(rootnode);
    TransferTracker ut(megaApi[0].get());
    auto uploadOptions = integrityUploadOptions();
    megaApi[0]->startUpload(file.name(), rootnode.get(), nullptr, &uploadOptions, &ut);

    ASSERT_TRUE(WaitFor(
        [&sentEnd, &ut]()
        {
            return sentEnd->load() >= static_cast<long long>(kFieldFileSize) || ut.finished;
        },
        180000))
        << "the whole file was not put on the wire";
    WaitMillisec(3000); // let the last frames reach the server
    const int swallowed = globalMegaTestHooks.wsRecvSwallowHook.getSwallowCount();
    ASSERT_FALSE(ut.finished) << "upload finished before the logout: repro did not engage";
    ASSERT_GE(swallowed, 1) << "no server frame was swallowed: repro did not engage";

    std::unique_ptr<char[]> session(dumpSession());
    ASSERT_NO_FATAL_FAILURE(locallogout());
    const int interrupted = ut.waitForResult();
    globalMegaTestHooks.wsRecvSwallowHook.reset();
    ASSERT_TRUE(interrupted == API_EACCESS || interrupted == API_EINCOMPLETE)
        << "Upload interrupted with unexpected code: " << interrupted;

    ASSERT_NO_FATAL_FAILURE(resumeSession(session.get()));
    ASSERT_NO_FATAL_FAILURE(fetchnodes(0));

    rootnode.reset(megaApi[0]->getRootNode());
    ASSERT_TRUE(rootnode);
    std::unique_ptr<MegaNode> node;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(300);
    while (std::chrono::steady_clock::now() < deadline)
    {
        node.reset(megaApi[0]->getNodeByPathOfType(file.name().c_str(),
                                                   rootnode.get(),
                                                   MegaNode::TYPE_FILE));
        if (node && node->getSize() == static_cast<long long>(kFieldFileSize))
        {
            break;
        }
        WaitMillisec(500);
    }
    ASSERT_TRUE(node) << "the resumed upload did not complete";
    ASSERT_EQ(node->getSize(), static_cast<long long>(kFieldFileSize));

    std::ostringstream summary;
    summary << "cell=ResumeWithPendingAcks size=" << kFieldFileSize << " swallowed=" << swallowed;
    expectFullMetaMac(*megaApi[0], node.get(), file.path(), file.hash(), watch, summary.str());
    removeNode(*megaApi[0], node.get());
}
#endif // MEGA_USE_WSUPLOAD

// ---------------------------------------------------------------------------------------------
// Injected gaps (the S4 shape and the other shapes the mechanisms can leave)
// ---------------------------------------------------------------------------------------------

TEST_F(SdkUploadIntegrityTest, InjectedGapFieldFileChunks0And12)
{
    INTEGRITY_TEST_PROLOGUE("InjectedGapFieldFileChunks0And12");
    ASSERT_NO_FATAL_FAILURE(runInjectedGap(*this, kFieldFileSize, {0, 12}, "0+1,12+1"));
}

TEST_F(SdkUploadIntegrityTest, InjectedGapFirstChunk)
{
    INTEGRITY_TEST_PROLOGUE("InjectedGapFirstChunk");
    ASSERT_NO_FATAL_FAILURE(runInjectedGap(*this, kFieldFileSize, {0}, "0+1"));
}

TEST_F(SdkUploadIntegrityTest, InjectedGapMiddleChunk)
{
    INTEGRITY_TEST_PROLOGUE("InjectedGapMiddleChunk");
    ASSERT_NO_FATAL_FAILURE(runInjectedGap(*this, kFieldFileSize, {6}, "6+1"));
}

TEST_F(SdkUploadIntegrityTest, InjectedGapLastChunk)
{
    INTEGRITY_TEST_PROLOGUE("InjectedGapLastChunk");
    ASSERT_NO_FATAL_FAILURE(runInjectedGap(*this, kFieldFileSize, {12}, "12+1"));
}

// Resume with pending acks: the chunks not acked yet are missing.
TEST_F(SdkUploadIntegrityTest, InjectedGapMissingTail)
{
    INTEGRITY_TEST_PROLOGUE("InjectedGapMissingTail");
    ASSERT_NO_FATAL_FAILURE(runInjectedGap(*this, kFieldFileSize, chunkRange(9, 13), "9+4"));
}

// Retry onto a finished slot: the first chunks sent, minus two still in flight on other
// connections (the three-run shape the current download tolerance cannot match).
TEST_F(SdkUploadIntegrityTest, InjectedGapTwoHolesAndMissingTail)
{
    INTEGRITY_TEST_PROLOGUE("InjectedGapTwoHolesAndMissingTail");
    ASSERT_NO_FATAL_FAILURE(
        runInjectedGap(*this, kFieldFileSize, {3, 5, 10, 11, 12}, "3+1,5+1,10+3"));
}

// No chunk MAC at all: the key would be the fold of an empty map.
TEST_F(SdkUploadIntegrityTest, InjectedGapEveryChunk)
{
    INTEGRITY_TEST_PROLOGUE("InjectedGapEveryChunk");
    ASSERT_NO_FATAL_FAILURE(runInjectedGap(*this, kFieldFileSize, chunkRange(0, 13), "0+13"));
}

TEST_F(SdkUploadIntegrityTest, InjectedGapOnlyChunkOfSmallFile)
{
    INTEGRITY_TEST_PROLOGUE("InjectedGapOnlyChunkOfSmallFile");
    ASSERT_NO_FATAL_FAILURE(runInjectedGap(*this, 100000, {0}, "0+1"));
}

// 4,718,593 bytes: eight growing chunks fill exactly 4,718,592 bytes, so the ninth is 1 byte.
TEST_F(SdkUploadIntegrityTest, InjectedGapOneByteLastChunk)
{
    INTEGRITY_TEST_PROLOGUE("InjectedGapOneByteLastChunk");
    ASSERT_NO_FATAL_FAILURE(runInjectedGap(*this, 4718593, {8}, "8+1"));
}

// 64 MiB (68 chunks), the shape of our 64 MiB retry repro: the first 9 chunks minus 4 and 5.
TEST_F(SdkUploadIntegrityTest, InjectedGapLargeFileMissingMostOfItsTail)
{
    INTEGRITY_TEST_PROLOGUE("InjectedGapLargeFileMissingMostOfItsTail");
    auto drop = chunkRange(9, 68);
    drop.insert({4, 5});
    ASSERT_NO_FATAL_FAILURE(runInjectedGap(*this, 64 * 1024 * 1024, drop, "4+2,9+59"));
}

// 110 chunks: the upload's map condenses its gap-free prefix once it holds more than 101 entries;
// a hole after that prefix must still be caught.
TEST_F(SdkUploadIntegrityTest, InjectedGapAfterCondensedPrefix)
{
    INTEGRITY_TEST_PROLOGUE("InjectedGapAfterCondensedPrefix");
    constexpr size_t fileSize = 4718592 + 102 * 1048576; // 110 chunks
    ASSERT_NO_FATAL_FAILURE(runInjectedGap(*this, fileSize, {105}, "105+1"));
}

// The gap comes back on the first retry as well: the upload must refuse two tokens, then complete.
TEST_F(SdkUploadIntegrityTest, InjectedGapTwiceInARow)
{
    INTEGRITY_TEST_PROLOGUE("InjectedGapTwiceInARow");
    ASSERT_NO_FATAL_FAILURE(runInjectedGap(*this, kFieldFileSize, {0, 12}, "0+1,12+1", 2));
}

// Random shapes on 13-chunk files: five uploads, each with its own non-empty set of dropped chunk
// MACs (up to seven runs). The seed is logged; MEGA_INTEGRITY_SEED replays a run.
TEST_F(SdkUploadIntegrityTest, InjectedGapRandomShapes)
{
    INTEGRITY_TEST_PROLOGUE("InjectedGapRandomShapes");
    unsigned seed = 6795;
    if (const char* env = std::getenv("MEGA_INTEGRITY_SEED"))
    {
        seed = static_cast<unsigned>(std::strtoul(env, nullptr, 10));
    }
    LOG_info << "[INTEGRITY] random shapes seed=" << seed;
    std::mt19937 rng(seed);
    for (int i = 0; i < 5; ++i)
    {
        const unsigned mask = std::uniform_int_distribution<unsigned>(1, (1u << 13) - 1)(rng);
        std::set<size_t> drop;
        for (size_t c = 0; c < 13; ++c)
        {
            if (mask & (1u << c))
            {
                drop.insert(c);
            }
        }
        const std::string runs = runsText(drop);
        SCOPED_TRACE("seed " + std::to_string(seed) + " runs " + runs);
        ASSERT_NO_FATAL_FAILURE(runInjectedGap(*this, kFieldFileSize, drop, runs.c_str()));
    }
}

// ---------------------------------------------------------------------------------------------
// Background media upload (the app encrypts and uploads; the SDK builds the key from its state)
// ---------------------------------------------------------------------------------------------

// The documented space-saving mode encrypts the file in portions from the end. An app that saves
// the state after the last portion, encrypts the rest, and later completes with that older saved
// state (killed and relaunched, say) holds the MACs of one portion only. The SDK must refuse that
// completion instead of creating a node whose key misses chunk MACs; the full state must succeed.
TEST_F(SdkUploadIntegrityTest, BackgroundMediaUploadRefusesStateMissingPortions)
{
    LOG_info
        << "___TEST SdkUploadIntegrityTest.BackgroundMediaUploadRefusesStateMissingPortions___";
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));
    ScopedMacGapLogWatch watch;
    MegaApi& api = *megaApi[0];
    LocalUploadFile file("integrity_bgmedia_", 3 * 1048576 + 12345);
    const std::string path = path_u8string(file.path());
    const int64_t fileSize = static_cast<int64_t>(fs::file_size(file.path()));

    std::unique_ptr<MegaBackgroundMediaUpload> state(
        MegaBackgroundMediaUpload::createInstance(&api));
    ASSERT_EQ(API_OK, synchronousMediaUploadRequestURL(0, fileSize, state.get(), nullptr));
    std::unique_ptr<char[]> url(state->getUploadURL());
    ASSERT_TRUE(url && url[0]) << "no media upload URL";

    // Portion boundaries, about 1 MiB each, adjusted to the chunk lattice by the SDK.
    std::vector<int64_t> starts;
    for (int64_t start = 0; start < fileSize;)
    {
        int64_t length = std::min<int64_t>(1048576, fileSize - start);
        std::unique_ptr<char[]> ok(state->encryptFile(path.c_str(), start, &length, nullptr, true));
        ASSERT_TRUE(ok && length > 0) << "cannot size the portion at " << start;
        starts.push_back(start);
        start += length;
    }
    ASSERT_GE(starts.size(), 2u) << "the file must be encrypted in more than one portion";

    // Encrypt from the end; save the state right after the last portion (the "older" state).
    std::vector<std::pair<std::string, fs::path>> portions; // {URL suffix, encrypted file}
    std::unique_ptr<char[]> olderSerialized;
    auto removePortions = makeScopedDestructor(
        [&portions]()
        {
            std::error_code ignore;
            for (const auto& p: portions)
            {
                fs::remove(p.second, ignore);
            }
        });
    for (size_t i = starts.size(); i-- > 0;)
    {
        int64_t length = (i + 1 < starts.size() ? starts[i + 1] : fileSize) - starts[i];
        const fs::path out = fs::absolute(u8path_compat(file.name() + ".enc" + std::to_string(i)));
        std::unique_ptr<char[]> suffix(state->encryptFile(path.c_str(),
                                                          starts[i],
                                                          &length,
                                                          path_u8string(out).c_str(),
                                                          false));
        ASSERT_TRUE(suffix) << "cannot encrypt the portion at " << starts[i];
        portions.emplace_back(suffix.get(), out);
        if (!olderSerialized)
        {
            olderSerialized.reset(state->serialize());
            ASSERT_TRUE(olderSerialized);
        }
    }

    std::string token;
    for (auto it = portions.rbegin(); it != portions.rend(); ++it)
    {
        std::string response;
        synchronousHttpPOSTFile(std::string{url.get()} + it->first,
                                path_u8string(it->second),
                                response);
        if (response.size() == 36)
        {
            token = response;
        }
    }
    ASSERT_EQ(token.size(), 36u) << "no upload token after posting every portion";
    std::unique_ptr<char[]> token64(api.binaryToBase64(token.data(), token.size()));
    std::unique_ptr<char[]> fingerprint(api.getFingerprint(path.c_str()));
    std::unique_ptr<MegaNode> root(api.getRootNode());
    ASSERT_TRUE(root);

    std::unique_ptr<MegaBackgroundMediaUpload> older(
        MegaBackgroundMediaUpload::unserialize(olderSerialized.get(), &api));
    ASSERT_TRUE(older) << "cannot restore the older state";
    EXPECT_EQ(API_EINCOMPLETE,
              synchronousMediaUploadComplete(0,
                                             older.get(),
                                             file.name().c_str(),
                                             root.get(),
                                             fingerprint.get(),
                                             nullptr,
                                             token64.get(),
                                             nullptr))
        << "a node was created from a state that misses the MACs of " << starts.size() - 1 << " of "
        << starts.size() << " portions";
    const auto gate = watch.uploadGateLines();
    EXPECT_TRUE(std::any_of(gate.begin(),
                            gate.end(),
                            [](const std::string& line)
                            {
                                return line.find("path=BACKGROUND") != std::string::npos;
                            }))
        << "event 800042 was not reported for the background upload" << joinLines(gate);

    ASSERT_EQ(API_OK,
              synchronousMediaUploadComplete(0,
                                             state.get(),
                                             file.name().c_str(),
                                             root.get(),
                                             fingerprint.get(),
                                             nullptr,
                                             token64.get(),
                                             nullptr));
    std::unique_ptr<MegaNode> node(
        api.getNodeByPathOfType(file.name().c_str(), root.get(), MegaNode::TYPE_FILE));
    ASSERT_TRUE(node) << "the completed background upload has no node";
    std::ostringstream summary;
    summary << "cell=BackgroundMediaUpload size=" << fileSize << " portions=" << starts.size();
    expectFullMetaMac(api, node.get(), file.path(), file.hash(), watch, summary.str());
    removeNode(api, node.get());
}

// ---------------------------------------------------------------------------------------------
// A transfer cache written before SDK-6795 (HTTP upload; WS builds upload a restored PUT again)
// ---------------------------------------------------------------------------------------------

#ifndef MEGA_USE_WSUPLOAD
// An older SDK built the node key when the upload token arrived, from whatever chunk MACs it had,
// and cached the transfer with its token, key and MACs. If the app stopped before the node was
// created, the next run completes from that cache. The SDK must not create the node with a key
// that misses chunk MACs: it must upload again.
//
// The cache is made in two steps. The upload runs to its token while the request that creates the
// node (putnodes) is held, so the transfer stays cached with its token; the session is logged out
// locally and resumed. When the restored transfer is about to complete, the hook turns it into
// what an older SDK would have cached: chunks 0 and 12 lose their MACs (the S4 shape) and the key
// is rebuilt, exactly as TransferSlot built it, from the MACs left.
TEST_F(SdkUploadIntegrityTest, RestoredUploadTokenWithGapKeyUploadsAgain)
{
    INTEGRITY_TEST_PROLOGUE("RestoredUploadTokenWithGapKeyUploadsAgain");
    ScopedMacGapLogWatch watch;
    LocalUploadFile file("integrity_restored_", kFieldFileSize);

    struct Hold
    {
        std::atomic<bool> active{true};
        std::atomic<bool> held{false};
    };

    const auto hold = std::make_shared<Hold>();
    const sdk_test::ScopedHttpReqPostHook holdPutnodes{
        [hold](HttpReq* req)
        {
            if (!hold->active.load() || !req || req->type != REQ_JSON || !req->out)
            {
                return false;
            }
            const auto commands = sdk_test::splitApiCommands(*req->out);
            const bool putnodes =
                std::any_of(commands.begin(),
                            commands.end(),
                            [](const std::string& command)
                            {
                                return sdk_test::commandStringArg(command, "a") == "p";
                            });
            if (putnodes)
            {
                hold->held = true;
            }
            return putnodes;
        }};

    auto emulated = std::make_shared<std::atomic<int>>(0);
    auto clearHook = makeScopedDestructor(
        []()
        {
            std::lock_guard<std::mutex> hookGuard(globalMegaTestHooks.mMutex);
            globalMegaTestHooks.onPendingUploadCompletion = nullptr;
        });
    auto cleanupTransfers = makeScopedDestructor(
        [this]()
        {
            (void)synchronousCancelTransfers(0, MegaTransfer::TYPE_UPLOAD);
        });

    std::unique_ptr<MegaNode> rootnode{megaApi[0]->getRootNode()};
    ASSERT_TRUE(rootnode);
    TransferTracker ut(megaApi[0].get());
    auto uploadOptions = integrityUploadOptions();
    megaApi[0]->startUpload(file.name(), rootnode.get(), nullptr, &uploadOptions, &ut);
    ASSERT_TRUE(WaitFor(
        [&hold]()
        {
            return hold->held.load();
        },
        180000))
        << "the upload never asked to create its node";
    ASSERT_FALSE(ut.finished) << "the upload finished although its putnodes was held";

    std::unique_ptr<char[]> session(dumpSession());
    ASSERT_NO_FATAL_FAILURE(locallogout());
    const int interrupted = ut.waitForResult();
    ASSERT_TRUE(interrupted == API_EACCESS || interrupted == API_EINCOMPLETE)
        << "Upload interrupted with unexpected code: " << interrupted;
    hold->active = false;

    {
        std::lock_guard<std::mutex> hookGuard(globalMegaTestHooks.mMutex);
        globalMegaTestHooks.onPendingUploadCompletion = [emulated](Transfer& t)
        {
            int expected = 0;
            if (!emulated->compare_exchange_strong(expected, 1))
            {
                return; // once: the restored transfer only
            }
            size_t index = 0;
            for (m_off_t pos = 0; pos < t.size; pos = ChunkedHash::chunkceil(pos, t.size), ++index)
            {
                if ((index == 0 || index == 12) && !t.chunkmacs.dropChunkMacForTesting(pos))
                {
                    emulated->store(-1); // the restored transfer did not hold that MAC
                }
            }
            std::copy(t.transferkey.begin(), t.transferkey.end(), t.filekey.key.begin());
            t.filekey.iv_u64 = static_cast<uint64_t>(t.ctriv);
            t.filekey.crc_u64 = static_cast<uint64_t>(t.chunkmacs.macsmac(t.transfercipher()));
            SymmCipher::xorblock(t.filekey.iv_bytes.data(), t.filekey.key.data());
        };
    }

    ASSERT_NO_FATAL_FAILURE(resumeSession(session.get()));
    ASSERT_NO_FATAL_FAILURE(fetchnodes(0));

    rootnode.reset(megaApi[0]->getRootNode());
    ASSERT_TRUE(rootnode);
    std::unique_ptr<MegaNode> node;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(300);
    while (std::chrono::steady_clock::now() < deadline)
    {
        node.reset(megaApi[0]->getNodeByPathOfType(file.name().c_str(),
                                                   rootnode.get(),
                                                   MegaNode::TYPE_FILE));
        if (node && node->getSize() == static_cast<long long>(kFieldFileSize))
        {
            break;
        }
        WaitMillisec(500);
    }
    ASSERT_TRUE(node) << "the restored upload did not complete";
    EXPECT_EQ(emulated->load(), 1) << "the restored transfer was not turned into a pre-fix cache";
    const auto gate = watch.uploadGateLines();
    EXPECT_TRUE(std::any_of(gate.begin(),
                            gate.end(),
                            [](const std::string& line)
                            {
                                return line.find("path=RESTORED") != std::string::npos &&
                                       line.find(" runs=0+1,12+1 ") != std::string::npos;
                            }))
        << "event 800042 was not reported for the restored transfer" << joinLines(gate);

    std::ostringstream summary;
    summary << "cell=RestoredUploadToken size=" << kFieldFileSize
            << " emulated=" << emulated->load();
    expectFullMetaMac(*megaApi[0], node.get(), file.path(), file.hash(), watch, summary.str());
    removeNode(*megaApi[0], node.get());
}
#endif // !MEGA_USE_WSUPLOAD

// ---------------------------------------------------------------------------------------------
// Storage red during an upload (S4's open case)
// ---------------------------------------------------------------------------------------------

#ifdef MEGA_USE_WSUPLOAD
namespace
{
// Runs MegaClient::activateoverquota(0, false) on the client thread: the path a storage overquota
// reported by the API takes. The account goes red and every WS upload is failed in place.
bool activateStorageOverquotaOnClientThread(MegaApi& api)
{
    bool ran = false;
    const bool dispatched = ::mega::test::wsupload::runOnClientThreadWithResult<bool>(
        api,
        ran,
        60,
        [](MegaClient* client, std::shared_ptr<std::promise<bool>> promise)
        {
            if (client)
            {
                client->activateoverquota(0, false);
            }
            promise->set_value(client != nullptr);
        });
    return dispatched && ran;
}
} // namespace

// The account goes red while the upload is early, half way and near its end, then recovers: the
// upload is failed in place and restarts on the same session. The restarted attempt can drop a
// chunk read while it was failed underneath; if that left a MAC missing, the key would have a
// hole anywhere in the file. The upload must end with a full key every time.
//
// Disabled: today the upload never ends. The file failed in place stays its pool's current file,
// WsPool::getWsUploadFile() resumes it without restarting the attempt (setUploadStart), so
// sendChunk() drops every chunk it reads as "failed underneath". No key is built, so it is not a
// MAC gap; the stuck upload is fixed separately, and this test is enabled with that fix.
TEST_F(SdkUploadIntegrityTest, DISABLED_StorageRedMidUploadThenRecovered)
{
    INTEGRITY_TEST_PROLOGUE("StorageRedMidUploadThenRecovered");
    MegaApi& api = *megaApi[0];
    std::unique_ptr<MegaNode> root{api.getRootNode()};
    ASSERT_TRUE(root);

    auto cleanup = makeScopedDestructor(
        [this, &api]()
        {
            (void)::mega::test::wsupload::setStorageStatusOnClientThread(api,
                                                                         ::mega::STORAGE_GREEN);
            api.setMaxUploadSpeed(-1);
            (void)synchronousCancelTransfers(0, MegaTransfer::TYPE_UPLOAD);
        });

    int exercised = 0;
    for (const int percent: {25, 60, 85})
    {
        SCOPED_TRACE("storage red at " + std::to_string(percent) + "%");
        ScopedMacGapLogWatch watch;
        LocalUploadFile file("integrity_red_", kFieldFileSize);
        // 1 MB/s: the 13-chunk file takes about 10 s, so the red state lands where it is aimed.
        api.setMaxUploadSpeed(1024 * 1024);
        TransferTracker tracker(&api);
        auto uploadOptions = integrityUploadOptions();
        api.startUpload(file.name(), root.get(), nullptr, &uploadOptions, &tracker);

        const auto aim = static_cast<long long>(kFieldFileSize) * percent / 100;
        ASSERT_TRUE(WaitFor(
            [&tracker, aim]()
            {
                return tracker.finished || tracker.mTransferredBytes.load() >= aim;
            },
            300000))
            << "the upload made no progress";
        const bool wentRed = !tracker.finished;
        if (wentRed)
        {
            ASSERT_TRUE(activateStorageOverquotaOnClientThread(api))
                << "storage overquota dispatch failed";
            WaitMillisec(3000);
            ASSERT_TRUE(
                ::mega::test::wsupload::setStorageStatusOnClientThread(api, ::mega::STORAGE_GREEN))
                << "storage recovery dispatch failed";
            ++exercised;
        }
        api.setMaxUploadSpeed(-1);
        ASSERT_EQ(tracker.waitForResult(600), API_OK) << "the upload did not complete";

        std::unique_ptr<MegaNode> node(api.getNodeByHandle(tracker.resultNodeHandle));
        ASSERT_TRUE(node) << "uploaded node not found";
        std::ostringstream summary;
        summary << "cell=StorageRed percent=" << percent << " wentRed=" << wentRed;
        expectFullMetaMac(api, node.get(), file.path(), file.hash(), watch, summary.str());
        removeNode(api, node.get());
    }
    if (!exercised)
    {
        GTEST_SKIP() << "every upload finished before storage could go red";
    }
}
#endif // MEGA_USE_WSUPLOAD

} // namespace mega::test::integrity
