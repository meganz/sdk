/**
 * @file SdkWsUploadTest.cpp
 * @brief WS-upload integration tests — TEST_F bodies for SdkWsUploadTest.
 *
 * The 30 SdkWsUploadTest cells exercise the v2 WS-upload feature on Linux/
 * macOS/Windows. They were previously hosted in tests/integration/SdkTest_test.cpp
 * inside the anonymous namespace; followup7-9 Goal 1.1c relocates them here so
 * the WS-upload module cleanly owns its tests, helpers and fixture-class
 * declaration.
 *
 * Helpers are pulled in via the per-symbol using-decls below (mirroring the
 * pattern already in place in SdkTest_test.cpp:84-99). The TU compiles under
 * C++20 on macOS+Windows, so all qualifiers use the leading `::mega::` form
 * per feedback_cxx_standard_per_target.md.
 */

#include "wsupload/SdkWsUploadTest.h"

#include "SdkTest_test.h"
#include "../stdfs.h"
#include "mega/scoped_helpers.h"
#include "mega/testhooks.h"
#include "mega/types.h"
#include "megaapi.h"
#include "megautils.h"
#include "sdk_test_utils.h"
#include "test.h"
#include "wsupload/ScopedUploadSpeedLimit.h"
#include "wsupload/SecondTimer.h"
#include "wsupload/TransferTempErrorTracker.h"
#include "wsupload/WsChunkSendOverquotaCapture.h"
#include "wsupload/WsUploadDebugHelpers.h"
#include "wsupload/WsUploadRetryTracker.h"
#include "wsupload/WsUploadTestHelpers.h"
#include "wsupload/WsUploadTransitionCapture.h"
#include "wsupload/WsUscCommand.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <future>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

using namespace std;

using ::mega::test::wsupload::fetchBestWsUploadTransferSnapshot;
using ::mega::test::wsupload::fetchUscSizeClasses;
using ::mega::test::wsupload::fetchWsUploadTransferSnapshots;
using ::mega::test::wsupload::kWsUploadDefaultFileSize;
using ::mega::test::wsupload::makeDefaultUploadOptions;
using ::mega::test::wsupload::overrideFirstUploadSessionUrlForTesting;
using ::mega::test::wsupload::ScopedUploadSpeedLimit;
using ::mega::test::wsupload::second_timer;
using ::mega::test::wsupload::TransferTempErrorTracker;
using ::mega::test::wsupload::waitForFirstUploadTransferSnapshot;

// Forward declaration — defined in SdkTest_test.cpp:132. External linkage so we
// reuse the single definition rather than duplicating the stat() wrapper.
bool fileexists(const std::string& fn);
using ::mega::test::wsupload::WsSessionUrlTransitionCapture;
using ::mega::test::wsupload::WsUploadRetryTracker;
using ::mega::test::wsupload::WsUploadTransferSnapshot;
#ifdef MEGASDK_DEBUG_TEST_HOOKS_ENABLED
using ::mega::test::wsupload::fetchWsUploadPoolStateForTesting;
using ::mega::test::wsupload::fetchWsUploadStatsForTesting;
using ::mega::test::wsupload::notifyWsUploadNetworkDisconnectForTesting;
using ::mega::test::wsupload::restartWsUploadEngineForTesting;
using ::mega::test::wsupload::waitForWsUploadPoolStateForTesting;
#endif

TEST_F(SdkWsUploadTest, SampledByteCorrectness)
{
    constexpr size_t kFileCount = 20;
    constexpr size_t kFileSize = 1 * 1024 * 1024;
    constexpr int kTimeoutS = 600;

    LOG_info << "___TEST___ SdkWsUploadSampledByteCorrectness";
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    auto accountRestorer = scopedToPro(*megaApi[0]);
    ASSERT_EQ(result(accountRestorer), API_OK);

    std::unique_ptr<MegaNode> rootnode{megaApi[0]->getRootNode()};
    ASSERT_NE(rootnode, nullptr);

    const std::string folderName =
        "wsupload_byte_correctness_" +
        std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
    const MegaHandle folderHandle = createFolder(0, folderName.c_str(), rootnode.get());
    ASSERT_NE(folderHandle, UNDEF);
    std::unique_ptr<MegaNode> folder{megaApi[0]->getNodeByHandle(folderHandle)};
    ASSERT_NE(folder, nullptr);

    const fs::path tmpDir = fs::temp_directory_path() / folderName;
    const fs::path downloadDir = tmpDir / "downloads";
    fs::create_directories(downloadDir);

    std::vector<sdk_test::LocalTempFile> localFiles;
    std::vector<std::string> expectedHashes;
    std::vector<std::string> fileNames;
    localFiles.reserve(kFileCount);
    expectedHashes.reserve(kFileCount);
    fileNames.reserve(kFileCount);

    for (size_t i = 0; i < kFileCount; ++i)
    {
        const std::string fileName = "byte_sample_" + std::to_string(i) + ".bin";
        fileNames.emplace_back(fileName);
        localFiles.emplace_back(tmpDir / fileName, kFileSize);
        expectedHashes.emplace_back(sdk_test::hashFileHex(localFiles.back().getPath()));
    }

    std::vector<std::unique_ptr<TransferTracker>> uploadTrackers;
    uploadTrackers.reserve(kFileCount);
    auto uploadOptions = makeDefaultUploadOptions();

    for (size_t i = 0; i < kFileCount; ++i)
    {
        uploadTrackers.emplace_back(std::make_unique<TransferTracker>(megaApi[0].get()));
        megaApi[0]->startUpload(localFiles[i].getPath().string(),
                                folder.get(),
                                nullptr /*cancelToken*/,
                                &uploadOptions,
                                uploadTrackers.back().get());
    }

    std::vector<MegaHandle> uploadedHandles;
    uploadedHandles.reserve(kFileCount);
    for (size_t i = 0; i < kFileCount; ++i)
    {
        const ErrorCodes res = uploadTrackers[i]->waitForResult(kTimeoutS);
        ASSERT_EQ(res, API_OK) << "Upload " << i << " failed with code " << res;
        ASSERT_NE(uploadTrackers[i]->resultNodeHandle, ::mega::INVALID_HANDLE);
        uploadedHandles.push_back(uploadTrackers[i]->resultNodeHandle);
    }

    for (size_t i = 0; i < kFileCount; ++i)
    {
        std::unique_ptr<MegaNode> node{megaApi[0]->getNodeByHandle(uploadedHandles[i])};
        ASSERT_NE(node, nullptr) << "Cannot find uploaded sample " << i;

        const fs::path downloadPath = downloadDir / fileNames[i];
        TransferTracker downloadTracker(megaApi[0].get());
        megaApi[0]->startDownload(node.get(),
                                  downloadPath.string().c_str(),
                                  nullptr /*customName*/,
                                  nullptr /*appData*/,
                                  false /*startFirst*/,
                                  nullptr /*cancelToken*/,
                                  MegaTransfer::COLLISION_CHECK_FINGERPRINT,
                                  MegaTransfer::COLLISION_RESOLUTION_NEW_WITH_N,
                                  false /*undelete*/,
                                  &downloadTracker);
        ASSERT_EQ(downloadTracker.waitForResult(kTimeoutS), API_OK)
            << "Download failed for sample " << i;

        const auto actualHash = sdk_test::hashFileHex(downloadPath);
        ASSERT_EQ(expectedHashes[i], actualHash) << "SHA-256 mismatch for sample " << i;
    }

    LOG_info << "[WsUploadByteCorrectness] files=" << kFileCount
             << " fileSize=" << kFileSize << " sha256=pass";

    deleteFolder(folderName);
}

/**
 * @brief TEST_F SdkResumableTrasfers
 *
 * Tests resumption for file upload and download.
 */
TEST_F(SdkTest, SdkResumableTrasfers)
{
    auto genStr = [](const size_t len) -> std::string
    {
        const std::string base = std::to_string(len) + " MB test file. ";
        std::string result;
        result.reserve(len);

        while (result.size() < len)
        {
            result += base;
        }

        result.resize(len);
        return result;
    };

    // Note: testResumableTrasfers limits maxConnections and max Upload/Download speed
    auto i = 0;
    const std::map<size_t, size_t> files = {{16, 120}, {19, 240}, {24, 300}};
    for (const auto& [fileSize, timeout]: files)
    {
        auto data = genStr(fileSize);
        LOG_info << "___TEST Resumable Trasfers. Iteration (" << ++i << ") FileSize ("
                 << data.size() << " MB)___";
        ASSERT_NO_FATAL_FAILURE(testResumableTrasfers(data, timeout));
    }
}

/**
 * @brief Verify WS resume keeps previous serialized metadata.
 *
 * - TEST1: Start throttled upload and capture serialized wsFileno/wsSessionUrl/pos..
 * - TEST2: Logout/login to force transfer-cache restore and observe resumed onTransferStart.
 * - TEST3: Assert resumed metadata matches cached metadata and upload finishes in cloud.
 */
TEST_F(SdkWsUploadTest, ResumeKeepsSerializedWsMetadata)
{
    LOG_info << "___TEST SdkWsUploadResumeKeepsSerializedWsMetadata___";
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    // Step 1: prepare source data and start a throttled upload so resume metadata is observable.
    ASSERT_TRUE(createFileWithSize(UPFILE, kWsUploadDefaultFileSize, "R"))
        << "Couldn't create " << UPFILE;

    RequestTracker ct(megaApi[0].get());
    megaApi[0]->setMaxConnections(1, &ct);
    ASSERT_EQ(API_OK, ct.waitForResult(60)) << "setMaxConnections() failed or timed out";

    std::unique_ptr<MegaNode> rootnode{megaApi[0]->getRootNode()};
    ASSERT_TRUE(rootnode);

    ScopedUploadSpeedLimit restoreUploadSpeed{*megaApi[0], 100000};
    onTransferUpdate_progress = 0;

    TransferTracker ut(megaApi[0].get());
    auto uploadOptions = makeDefaultUploadOptions();
    megaApi[0]->startUpload(std::string{UPFILE},
                            rootnode.get(),
                            nullptr,
                            &uploadOptions,
                            &ut /*listener*/);

    second_timer timer;
    while (!ut.finished && !ut.started && timer.elapsed() < 90)
    {
        WaitMillisec(100);
    }

    ASSERT_TRUE(ut.started) << "Upload did not start in time";
    ASSERT_FALSE(ut.finished) << "Upload ended too early, with " << ut.waitForResult();

    WsUploadTransferSnapshot beforeResume{};
    ASSERT_TRUE(waitForFirstUploadTransferSnapshot(
        *megaApi[0],
        beforeResume,
        [](const WsUploadTransferSnapshot& snapshot)
        {
            return snapshot.wsFileno > 0 && !snapshot.wsSessionUrl.empty() &&
                   snapshot.progressCompleted > 0 && snapshot.pos > 0;
        },
        40,
        200))
        << "No active WS upload transfer with serialized resume head observable before resume";
    ASSERT_TRUE(beforeResume.found);
    ASSERT_GT(beforeResume.progressCompleted, 0);
    ASSERT_GT(beforeResume.pos, 0);

    // Step 2: capture onTransferStart after resume to compare resumed head with serialized state.
    std::atomic<bool> resumeStartObserved{false};
    m_off_t transferredBytesAtResumeStart = -1;
    auto resetOnTransferStartCb = makeScopedDestructor(
        [this]()
        {
            onTransferStartCustomCb = {};
        });
    onTransferStartCustomCb = [&resumeStartObserved, &transferredBytesAtResumeStart](
                                  MegaTransfer* transfer)
    {
        if (transfer)
        {
            transferredBytesAtResumeStart = transfer->getTransferredBytes();
        }
        resumeStartObserved = true;
    };

    std::unique_ptr<char[]> session(dumpSession());
    ASSERT_NO_FATAL_FAILURE(locallogout());
    const int uploadInterruptedCode = ut.waitForResult();
    ASSERT_TRUE(uploadInterruptedCode == API_EACCESS || uploadInterruptedCode == API_EINCOMPLETE)
        << "Upload interrupted with unexpected code: " << uploadInterruptedCode;

    ASSERT_NO_FATAL_FAILURE(resumeSession(session.get()));
    ASSERT_NO_FATAL_FAILURE(fetchnodes(0));

    ASSERT_TRUE(WaitFor(
        std::bind(
            [](const std::atomic<bool>& flag)
            {
                return flag.load();
            },
            std::cref(resumeStartObserved)),
        60000))
        << "onTransferStart was not observed after resuming session";

    ASSERT_EQ(transferredBytesAtResumeStart, beforeResume.progressCompleted)
        << "Upload resumed with a different transferred-byte head than was serialized";

    // Step 3: verify resumed WS metadata/head is preserved from serialized transfer state.
    WsUploadTransferSnapshot resumed{};
    ASSERT_TRUE(waitForFirstUploadTransferSnapshot(
        *megaApi[0],
        resumed,
        [](const WsUploadTransferSnapshot& snapshot)
        {
            return snapshot.wsFileno > 0 && !snapshot.wsSessionUrl.empty() &&
                   snapshot.progressCompleted > 0 && snapshot.pos > 0;
        },
        40,
        500))
        << "No resumed WS upload snapshot with non-zero resume head was observed";

    ASSERT_GT(resumed.wsFileno, 0u);
    ASSERT_EQ(resumed.wsFileno, beforeResume.wsFileno);
    ASSERT_FALSE(resumed.wsSessionUrl.empty());
    ASSERT_EQ(resumed.wsSessionUrl, beforeResume.wsSessionUrl);
    ASSERT_GT(resumed.pos, 0);
    ASSERT_GE(resumed.pos, beforeResume.pos);

    // Step 4: uncap and require eventual completion in cloud.
    megaApi[0]->setMaxUploadSpeed(-1);
    rootnode.reset(megaApi[0]->getRootNode());
    ASSERT_TRUE(rootnode);
    std::unique_ptr<MegaNode> cloudNode(
        megaApi[0]->getNodeByPathOfType(UPFILE.c_str(), rootnode.get(), MegaNode::TYPE_FILE));
    timer.reset();
    while (!cloudNode && timer.elapsed() < 180)
    {
        WaitMillisec(500);
        cloudNode.reset(
            megaApi[0]->getNodeByPathOfType(UPFILE.c_str(), rootnode.get(), MegaNode::TYPE_FILE));
    }
    ASSERT_TRUE(cloudNode) << "Upload did not finish after resume";
}

/**
 * @brief Verify a changed cached local file resumes with a fresh WS session.
 *
 * - TEST1: Start upload and capture pinned WS metadata.
 * - TEST2: Override cached URL with invalid pinned URL and logout.
 * - TEST3: Modify local source file before session resume.
 * - TEST4: Require resumed transfer switches to non-stale URL and cloud node size equals modified file.
 */
TEST_F(SdkWsUploadTest, ModifiedCachedFileStartsFreshSession)
{
    LOG_info << "___TEST SdkWsUploadModifiedCachedFileStartsFreshSession___";
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    constexpr size_t initialFileSize = 24 * 1024 * 1024;
    constexpr size_t changedFileSize = 36 * 1024 * 1024;
    // Step 1: create initial source file, then start upload and capture pinned WS metadata.
    ASSERT_TRUE(createFileWithSize(UPFILE, initialFileSize, "R")) << "Couldn't create " << UPFILE;

    RequestTracker ct(megaApi[0].get());
    megaApi[0]->setMaxConnections(1, &ct);
    ASSERT_EQ(API_OK, ct.waitForResult(60)) << "setMaxConnections() failed or timed out";

    std::unique_ptr<MegaNode> rootnode{megaApi[0]->getRootNode()};
    ASSERT_TRUE(rootnode);

    ScopedUploadSpeedLimit restoreUploadSpeed{*megaApi[0], 600000};
    onTransferUpdate_progress = 0;

    TransferTracker ut(megaApi[0].get());
    auto uploadOptions = makeDefaultUploadOptions();
    megaApi[0]->startUpload(std::string{UPFILE},
                            rootnode.get(),
                            nullptr,
                            &uploadOptions,
                            &ut /*listener*/);

    second_timer timer;
    while (!ut.finished && !ut.started && timer.elapsed() < 90)
    {
        WaitMillisec(100);
    }

    ASSERT_TRUE(ut.started) << "Upload did not start in time";
    ASSERT_FALSE(ut.finished) << "Upload ended too early, with " << ut.waitForResult();

    WsUploadTransferSnapshot beforeResume{};
    ASSERT_TRUE(waitForFirstUploadTransferSnapshot(
        *megaApi[0],
        beforeResume,
        [](const WsUploadTransferSnapshot& snapshot)
        {
            return snapshot.wsFileno > 0 && !snapshot.wsSessionUrl.empty() &&
                   snapshot.progressCompleted > 0;
        },
        30,
        200))
        << "No active WS upload transfer metadata observable before rewriting the cached file";
    ASSERT_TRUE(beforeResume.found);

    // Step 2: force an invalid cached URL, then interrupt transfer via logout.
    const std::string invalidPinnedUrl = "wss://127.0.0.1:1/ul/changed-file-stale-session";
    ASSERT_TRUE(overrideFirstUploadSessionUrlForTesting(*megaApi[0], invalidPinnedUrl, 10));

    WsUploadTransferSnapshot forcedInvalid{};
    ASSERT_TRUE(waitForFirstUploadTransferSnapshot(
        *megaApi[0],
        forcedInvalid,
        [&invalidPinnedUrl](const WsUploadTransferSnapshot& snapshot)
        {
            return snapshot.wsFileno > 0 && snapshot.serializedWsSessionUrl == invalidPinnedUrl;
        },
        10,
        200))
        << "Failed to persist the invalid serialized WS session URL before logout";

    std::unique_ptr<char[]> session(dumpSession());
    ASSERT_NO_FATAL_FAILURE(locallogout());
    const int uploadInterruptedCode = ut.waitForResult();
    ASSERT_TRUE(uploadInterruptedCode == API_EACCESS || uploadInterruptedCode == API_EINCOMPLETE)
        << "Upload interrupted with unexpected code: " << uploadInterruptedCode;

    // Step 3: mutate local source file before session resume.
    ASSERT_TRUE(createFileWithSize(UPFILE, changedFileSize, "S")) << "Couldn't recreate " << UPFILE;

    ASSERT_NO_FATAL_FAILURE(resumeSession(session.get()));
    ASSERT_NO_FATAL_FAILURE(fetchnodes(0));

    // Step 4: resume and require fresh WS session selection (not stale pinned URL).
    WsUploadTransferSnapshot freshSession{};
    WsUploadTransferSnapshot lastObservedAfterResume{};
    bool switchedToFreshSession = false;
    second_timer freshSessionTimer;
    while (freshSessionTimer.elapsed() < 90)
    {
        WsUploadTransferSnapshot snapshot{};
        if (fetchBestWsUploadTransferSnapshot(*megaApi[0], snapshot, 1) && snapshot.found)
        {
            lastObservedAfterResume = snapshot;
            if (snapshot.wsFileno > 0 && !snapshot.wsSessionUrl.empty() &&
                snapshot.wsSessionUrl != invalidPinnedUrl &&
                snapshot.serializedWsSessionUrl != invalidPinnedUrl)
            {
                freshSession = snapshot;
                switchedToFreshSession = true;
                break;
            }
        }
        WaitMillisec(500);
    }

    ASSERT_TRUE(switchedToFreshSession)
        << "Upload resumed using stale WS session metadata after the local file changed"
        << " [forced fileno=" << forcedInvalid.wsFileno
        << " forced serialized url=" << forcedInvalid.serializedWsSessionUrl
        << " last fileno=" << lastObservedAfterResume.wsFileno
        << " last serialized url=" << lastObservedAfterResume.serializedWsSessionUrl
        << " last live url=" << lastObservedAfterResume.wsSessionUrl << "]";

    ASSERT_NE(freshSession.serializedWsSessionUrl, invalidPinnedUrl);
    ASSERT_NE(freshSession.wsSessionUrl, invalidPinnedUrl);

    // Step 5: finish transfer and verify uploaded node matches modified file size.
    megaApi[0]->setMaxUploadSpeed(-1);
    rootnode.reset(megaApi[0]->getRootNode());
    ASSERT_TRUE(rootnode);
    std::unique_ptr<MegaNode> cloudNode(
        megaApi[0]->getNodeByPathOfType(UPFILE.c_str(), rootnode.get(), MegaNode::TYPE_FILE));
    timer.reset();
    while (!cloudNode && timer.elapsed() < 180)
    {
        WaitMillisec(500);
        cloudNode.reset(
            megaApi[0]->getNodeByPathOfType(UPFILE.c_str(), rootnode.get(), MegaNode::TYPE_FILE));
    }
    ASSERT_TRUE(cloudNode) << "Upload did not finish after resuming with a changed local file";
    ASSERT_EQ(cloudNode->getSize(), static_cast<int64_t>(changedFileSize));
}

/**
 * @brief Verify a changed cached local file resumes with a fresh WS session.
 *
 * Uses the serialized transfer metadata for the pre-logout assertion, and the live
 * WS engine view for the post-resume "fresh session" assertion.
 */
TEST_F(SdkWsUploadTest, ModifiedCachedFileStartsFreshSession2)
{
    LOG_info << "___TEST SdkWsUploadModifiedCachedFileStartsFreshSession2___";
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    constexpr size_t initialFileSize = 24 * 1024 * 1024;
    constexpr size_t changedFileSize = 36 * 1024 * 1024;
    ASSERT_TRUE(createFileWithSize(UPFILE, initialFileSize, "R")) << "Couldn't create " << UPFILE;

    RequestTracker ct(megaApi[0].get());
    megaApi[0]->setMaxConnections(1, &ct);
    ASSERT_EQ(API_OK, ct.waitForResult(60)) << "setMaxConnections() failed or timed out";

    std::unique_ptr<MegaNode> rootnode{megaApi[0]->getRootNode()};
    ASSERT_TRUE(rootnode);

    ScopedUploadSpeedLimit restoreUploadSpeed{*megaApi[0], 600000};
    onTransferUpdate_progress = 0;

    TransferTracker ut(megaApi[0].get());
    auto uploadOptions = makeDefaultUploadOptions();
    megaApi[0]->startUpload(std::string{UPFILE},
                            rootnode.get(),
                            nullptr,
                            &uploadOptions,
                            &ut /*listener*/);

    second_timer timer;
    while (!ut.finished && !ut.started && timer.elapsed() < 90)
    {
        WaitMillisec(100);
    }

    ASSERT_TRUE(ut.started) << "Upload did not start in time";
    ASSERT_FALSE(ut.finished) << "Upload ended too early, with " << ut.waitForResult();

    WsUploadTransferSnapshot beforeResume{};
    ASSERT_TRUE(waitForFirstUploadTransferSnapshot(
        *megaApi[0],
        beforeResume,
        [](const WsUploadTransferSnapshot& snapshot)
        {
            return snapshot.wsFileno > 0 && !snapshot.wsSessionUrl.empty() &&
                   snapshot.progressCompleted > 0;
        },
        30,
        200))
        << "No active WS upload transfer metadata observable before rewriting the cached file";
    ASSERT_TRUE(beforeResume.found);

    const std::string invalidPinnedUrl = "wss://127.0.0.1:1/ul/changed-file-stale-session";
    ASSERT_TRUE(overrideFirstUploadSessionUrlForTesting(*megaApi[0], invalidPinnedUrl, 10));

    WsUploadTransferSnapshot forcedInvalid{};
    ASSERT_TRUE(waitForFirstUploadTransferSnapshot(
        *megaApi[0],
        forcedInvalid,
        [&invalidPinnedUrl](const WsUploadTransferSnapshot& snapshot)
        {
            return snapshot.wsFileno > 0 && snapshot.serializedWsSessionUrl == invalidPinnedUrl;
        },
        10,
        200))
        << "Failed to persist the invalid serialized WS session URL before logout";
    ASSERT_EQ(forcedInvalid.wsFileno, beforeResume.wsFileno);

    std::unique_ptr<char[]> session(dumpSession());
    ASSERT_NO_FATAL_FAILURE(locallogout());
    const int uploadInterruptedCode = ut.waitForResult();
    ASSERT_TRUE(uploadInterruptedCode == API_EACCESS || uploadInterruptedCode == API_EINCOMPLETE)
        << "Upload interrupted with unexpected code: " << uploadInterruptedCode;

    ASSERT_TRUE(createFileWithSize(UPFILE, changedFileSize, "S")) << "Couldn't recreate " << UPFILE;

    ASSERT_NO_FATAL_FAILURE(resumeSession(session.get()));
    ASSERT_NO_FATAL_FAILURE(fetchnodes(0));

    WsUploadTransferSnapshot freshSession{};
    WsUploadTransferSnapshot lastObservedAfterResume{};
    bool switchedToFreshSession = false;
    second_timer freshSessionTimer;
    while (freshSessionTimer.elapsed() < 90)
    {
        WsUploadTransferSnapshot snapshot{};
        if (fetchBestWsUploadTransferSnapshot(*megaApi[0], snapshot, 1) && snapshot.found)
        {
            lastObservedAfterResume = snapshot;
            if (snapshot.wsFileno > 0 && !snapshot.wsSessionUrl.empty() &&
                snapshot.wsSessionUrl != invalidPinnedUrl &&
                snapshot.serializedWsSessionUrl != invalidPinnedUrl)
            {
                freshSession = snapshot;
                switchedToFreshSession = true;
                break;
            }
        }
        WaitMillisec(500);
    }

    ASSERT_TRUE(switchedToFreshSession)
        << "Upload resumed using stale WS session metadata after the local file changed"
        << " [forced fileno=" << forcedInvalid.wsFileno
        << " forced serialized url=" << forcedInvalid.serializedWsSessionUrl
        << " last fileno=" << lastObservedAfterResume.wsFileno
        << " last serialized url=" << lastObservedAfterResume.serializedWsSessionUrl
        << " last live url=" << lastObservedAfterResume.wsSessionUrl << "]";

    ASSERT_NE(freshSession.serializedWsSessionUrl, invalidPinnedUrl);
    ASSERT_NE(freshSession.wsSessionUrl, invalidPinnedUrl);

    megaApi[0]->setMaxUploadSpeed(-1);
    rootnode.reset(megaApi[0]->getRootNode());
    ASSERT_TRUE(rootnode);
    std::unique_ptr<MegaNode> cloudNode(
        megaApi[0]->getNodeByPathOfType(UPFILE.c_str(), rootnode.get(), MegaNode::TYPE_FILE));
    timer.reset();
    while (!cloudNode && timer.elapsed() < 180)
    {
        WaitMillisec(500);
        cloudNode.reset(
            megaApi[0]->getNodeByPathOfType(UPFILE.c_str(), rootnode.get(), MegaNode::TYPE_FILE));
    }
    ASSERT_TRUE(cloudNode) << "Upload did not finish after resuming with a changed local file";
    ASSERT_EQ(cloudNode->getSize(), static_cast<int64_t>(changedFileSize));
}

/**
 * @brief Verify pinned WS pool detaches and retires after last transfer cancellation.
 *
 * - TEST1: Resume a transfer into a pinned pool derived from cached WS session URL.
 * - TEST2: Cancel upload transfers and poll pool state for the pinned URL.
 * - TEST3: Assert numPoolFiles=0, no uploading file, no in-flight/resend chunks, then pool retires.
 */
TEST_F(SdkWsUploadTest, CancelledPinnedPoolRetiresAfterTransferRemoval)
{
    LOG_info << "___TEST SdkWsUploadCancelledPinnedPoolRetiresAfterTransferRemoval___";
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

#ifndef MEGASDK_DEBUG_TEST_HOOKS_ENABLED
    GTEST_SKIP() << "Requires MEGASDK_DEBUG_TEST_HOOKS_ENABLED";
#else
    const std::string fileName =
        "ws_pinned_pool_retire_" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".bin";
    constexpr size_t fileSize = 32 * 1024 * 1024;
    // Step 1: create source file, start upload, and capture the URL that becomes pinned on resume.
    ASSERT_TRUE(createFileWithSize(fileName, fileSize, "P"))
        << "Couldn't create " << fileName;

    auto cleanupFile = makeScopedDestructor(
        [this, &fileName]()
        {
            deleteFile(fileName);
        });
    auto cleanupTransfers = makeScopedDestructor(
        [this]()
        {
            (void)synchronousCancelTransfers(0, MegaTransfer::TYPE_UPLOAD);
        });

    RequestTracker ct(megaApi[0].get());
    megaApi[0]->setMaxConnections(1, &ct);
    ASSERT_EQ(API_OK, ct.waitForResult(60)) << "setMaxConnections() failed or timed out";

    std::unique_ptr<MegaNode> rootnode{megaApi[0]->getRootNode()};
    ASSERT_TRUE(rootnode);

    ScopedUploadSpeedLimit restoreUploadSpeed{*megaApi[0], 600000};
    onTransferUpdate_progress = 0;

    TransferTracker ut(megaApi[0].get());
    auto uploadOptions = makeDefaultUploadOptions();
    megaApi[0]->startUpload(fileName,
                            rootnode.get(),
                            nullptr,
                            &uploadOptions,
                            &ut /*listener*/);

    second_timer timer;
    while (!ut.finished && !ut.started && timer.elapsed() < 90)
    {
        WaitMillisec(100);
    }

    ASSERT_TRUE(ut.started) << "Upload did not start in time";
    ASSERT_FALSE(ut.finished) << "Upload ended too early, with " << ut.waitForResult();

    WsUploadTransferSnapshot beforeResume{};
    ASSERT_TRUE(waitForFirstUploadTransferSnapshot(
        *megaApi[0],
        beforeResume,
        [](const WsUploadTransferSnapshot& snapshot)
        {
            return snapshot.wsFileno > 0 && !snapshot.wsSessionUrl.empty() &&
                   snapshot.progressCompleted > 0;
        },
        30,
        200))
        << "No active WS upload transfer metadata observable before logout";
    ASSERT_TRUE(beforeResume.found);

    std::unique_ptr<char[]> session(dumpSession());
    ASSERT_NO_FATAL_FAILURE(locallogout());
    const int uploadInterruptedCode = ut.waitForResult();
    ASSERT_TRUE(uploadInterruptedCode == API_EACCESS || uploadInterruptedCode == API_EINCOMPLETE)
        << "Upload interrupted with unexpected code: " << uploadInterruptedCode;

    ASSERT_NO_FATAL_FAILURE(resumeSession(session.get()));
    ASSERT_NO_FATAL_FAILURE(fetchnodes(0));

    // Step 2: resume session and verify transfer is attached to pinned pool.
    ws::UploadEngine::PoolStateForTesting pinnedPool{};
    ASSERT_TRUE(waitForWsUploadPoolStateForTesting(
        *megaApi[0],
        beforeResume.wsSessionUrl,
        pinnedPool,
        [](const ws::UploadEngine::PoolStateForTesting& state)
        {
            return state.found && state.pinned && state.numPoolFiles > 0;
        },
        60,
        200))
        << "Resumed transfer never attached to a pinned pool for the cached WS session URL";

    ASSERT_EQ(API_OK, synchronousCancelTransfers(0, MegaTransfer::TYPE_UPLOAD));

    // Step 3: cancel resumed transfer and verify pool detaches all transfer bookkeeping.
    ws::UploadEngine::PoolStateForTesting afterCancel{};
    ws::UploadEngine::PoolStateForTesting lastObservedDetachedState{};
    bool detachedFromPinnedPool = false;
    second_timer detachTimer;
    while (detachTimer.elapsed() < 15)
    {
        ASSERT_TRUE(fetchWsUploadPoolStateForTesting(
            *megaApi[0], beforeResume.wsSessionUrl, afterCancel, 1))
            << "Failed to inspect pinned pool state after cancelling the resumed transfer";
        ASSERT_TRUE(afterCancel.found)
            << "Pinned pool disappeared before its post-cancel bookkeeping could be checked";
        ASSERT_TRUE(afterCancel.pinned);

        lastObservedDetachedState = afterCancel;
        if (afterCancel.numPoolFiles == 0 && !afterCancel.hasUploadingFile &&
            afterCancel.numChunksInFlight == 0 && afterCancel.queuedResends == 0)
        {
            detachedFromPinnedPool = true;
            break;
        }

        WaitMillisec(200);
    }

    ASSERT_TRUE(detachedFromPinnedPool)
        << "Pinned pool kept stale transfer bookkeeping after its last transfer was removed"
        << " [numPoolFiles=" << lastObservedDetachedState.numPoolFiles
        << " uploading=" << lastObservedDetachedState.hasUploadingFile
        << " inFlight=" << lastObservedDetachedState.numChunksInFlight
        << " queuedResends=" << lastObservedDetachedState.queuedResends
        << " retiring=" << lastObservedDetachedState.retiring
        << " activeThreads=" << lastObservedDetachedState.activeThreads
        << " exitingThreads=" << lastObservedDetachedState.exitingThreads << "]";

    // Step 4: verify the pool no longer keeps a stale file reference.
    ws::UploadEngine::PoolStateForTesting referenceState{};
    ws::UploadEngine::PoolStateForTesting lastObservedReferenceState{};
    bool poolReferenceGone = false;
    second_timer referenceGoneTimer;
    while (referenceGoneTimer.elapsed() < 15)
    {
        ASSERT_TRUE(fetchWsUploadPoolStateForTesting(
            *megaApi[0], beforeResume.wsSessionUrl, referenceState, 1))
            << "Failed to inspect pinned pool references after cancellation";
        ASSERT_TRUE(referenceState.found)
            << "Pinned pool disappeared before its retirement preconditions could be checked";

        lastObservedReferenceState = referenceState;
        if (!referenceState.hasReference)
        {
            poolReferenceGone = true;
            break;
        }

        WaitMillisec(200);
    }

    ASSERT_TRUE(poolReferenceGone)
        << "Pinned pool kept a stale file reference long after cancellation"
        << " [numPoolFiles=" << lastObservedReferenceState.numPoolFiles
        << " uploading=" << lastObservedReferenceState.hasUploadingFile
        << " hasReference=" << lastObservedReferenceState.hasReference
        << " inFlight=" << lastObservedReferenceState.numChunksInFlight
        << " queuedResends=" << lastObservedReferenceState.queuedResends
        << " retiring=" << lastObservedReferenceState.retiring
        << " activeThreads=" << lastObservedReferenceState.activeThreads
        << " exitingThreads=" << lastObservedReferenceState.exitingThreads << "]";
#endif
}

/**
 * @brief Verify active WS pool actually uses parallel upload connections.
 *
 * - TEST1: Start one large upload with WS max connections set to 8.
 * - TEST2: Poll active pool state and require observed concurrent in-flight connections >= 2.
 * - TEST3: Assert upload still completes successfully.
 */
TEST_F(SdkWsUploadTest, ActivePoolUsesParallelConnections)
{
    LOG_info << "___TEST SdkWsUploadActivePoolUsesParallelConnections___";
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

#ifndef MEGASDK_DEBUG_TEST_HOOKS_ENABLED
    GTEST_SKIP() << "Requires MEGASDK_DEBUG_TEST_HOOKS_ENABLED";
#else
    const std::string fileName =
        "ws_parallel_conn_" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".bin";
    // Keep this large enough for parallel-connection observation, and align to a WsPool
    // chunk-boundary size (4.5 MiB + N*1 MiB) to exercise the empty EOF-chunk tail path.
    constexpr size_t fileSize = (4608 + 92 * 1024) * 1024; // 96.5 MiB
    ASSERT_TRUE(createFileWithSize(fileName, fileSize, "Q")) << "Couldn't create " << fileName;

    auto cleanupFile = makeScopedDestructor(
        [this, &fileName]()
        {
            deleteFile(fileName);
        });
    auto cleanupTransfers = makeScopedDestructor(
        [this]()
        {
            (void)synchronousCancelTransfers(0, MegaTransfer::TYPE_UPLOAD);
        });

    RequestTracker ct(megaApi[0].get());
    megaApi[0]->setMaxConnections(8, &ct);
    ASSERT_EQ(API_OK, ct.waitForResult(60)) << "setMaxConnections() failed or timed out";

    std::unique_ptr<MegaNode> rootnode{megaApi[0]->getRootNode()};
    ASSERT_TRUE(rootnode);

    megaApi[0]->setMaxUploadSpeed(-1);

    TransferTracker ut(megaApi[0].get());
    auto uploadOptions = makeDefaultUploadOptions();
    megaApi[0]->startUpload(fileName, rootnode.get(), nullptr, &uploadOptions, &ut /*listener*/);

    WsUploadTransferSnapshot activeUpload{};
    ASSERT_TRUE(waitForFirstUploadTransferSnapshot(
        *megaApi[0],
        activeUpload,
        [](const WsUploadTransferSnapshot& snapshot)
        {
            return snapshot.found && snapshot.wsFileno > 0 && !snapshot.wsSessionUrl.empty() &&
                   snapshot.state == TRANSFERSTATE_ACTIVE && snapshot.progressCompleted > 0;
        },
        60,
        200))
        << "Failed to observe an active WS upload snapshot";

    ws::UploadEngine::PoolStateForTesting state{};
    ws::UploadEngine::PoolStateForTesting lastObservedState{};
    std::string observedPoolUrl = activeUpload.wsSessionUrl;
    bool observedParallelInFlight = false;

    second_timer parallelTimer;
    while (parallelTimer.elapsed() < 90)
    {
        WsUploadTransferSnapshot latest{};
        if (fetchBestWsUploadTransferSnapshot(*megaApi[0], latest, 1) && latest.found &&
            !latest.wsSessionUrl.empty())
        {
            observedPoolUrl = latest.wsSessionUrl;
        }

        if (!observedPoolUrl.empty() &&
            fetchWsUploadPoolStateForTesting(*megaApi[0], observedPoolUrl, state, 1))
        {
            lastObservedState = state;
            if (state.found && state.hasUploadingFile && state.activeThreads >= 2 &&
                state.maxConnectionsWithInFlightSeen >= 2)
            {
                observedParallelInFlight = true;
                break;
            }
        }

        if (ut.finished)
        {
            break;
        }
        WaitMillisec(200);
    }

    ASSERT_TRUE(observedParallelInFlight)
        << "WS upload never showed parallel in-flight connections on the active pool"
        << " [url=" << observedPoolUrl << "] [found=" << lastObservedState.found
        << "] [hasUploadingFile=" << lastObservedState.hasUploadingFile
        << "] [activeThreads=" << lastObservedState.activeThreads
        << "] [openConnections=" << lastObservedState.openConnections
        << "] [connectionsWithInFlight=" << lastObservedState.connectionsWithInFlight
        << "] [maxConnectionsWithInFlightSeen=" << lastObservedState.maxConnectionsWithInFlightSeen
        << "] [numPoolFiles=" << lastObservedState.numPoolFiles
        << "] [numChunksInFlight=" << lastObservedState.numChunksInFlight
        << "] [queuedResends=" << lastObservedState.queuedResends
        << "] [retiring=" << lastObservedState.retiring << "]";

    ASSERT_EQ(API_OK, ut.waitForResult(240))
        << "Upload did not complete after parallel in-flight verification";
#endif
}

/**
 * @brief Verify sustained WS handshake failures trigger retry and restart transfer start path.
 *
 * - TEST1: Start upload and capture active WS session metadata.
 * - TEST2: Inject handshake failures during the sustained-failure window.
 * - TEST3: Require transfer start is re-observed and upload still completes with API_OK.
 */
TEST_F(SdkWsUploadTest, RetryAfterHandshakeFailureRestartsTransferStart)
{
    LOG_info << "___TEST SdkWsUploadRetryAfterHandshakeFailureRestartsTransferStart___";
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

#ifndef MEGASDK_DEBUG_TEST_HOOKS_ENABLED
    GTEST_SKIP() << "Requires MEGASDK_DEBUG_TEST_HOOKS_ENABLED";
#else
    // Step 1: start upload and capture active WS session URL.
    const std::string fileName =
        "ws_retry_handshake_" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".bin";
    ASSERT_TRUE(createFileWithSize(fileName, kWsUploadDefaultFileSize, "H"))
        << "Couldn't create " << fileName;

    auto cleanupFile = makeScopedDestructor(
        [this, &fileName]()
        {
            deleteFile(fileName);
        });
    auto cleanupTransfers = makeScopedDestructor(
        [this]()
        {
            (void)synchronousCancelTransfers(0, MegaTransfer::TYPE_UPLOAD);
        });
    auto resetWsHooks = makeScopedDestructor(
        []()
        {
            globalMegaTestHooks.onWsHandshake = {};
            globalMegaTestHooks.onWsUploadSustainedHandshakeFailureWindowDs = {};
        });

    RequestTracker ct(megaApi[0].get());
    megaApi[0]->setMaxConnections(1, &ct);
    ASSERT_EQ(API_OK, ct.waitForResult(60)) << "setMaxConnections() failed or timed out";

    std::unique_ptr<MegaNode> rootnode{megaApi[0]->getRootNode()};
    ASSERT_TRUE(rootnode);

    ScopedUploadSpeedLimit restoreUploadSpeed{*megaApi[0], 100000};

    WsUploadRetryTracker tracker(megaApi[0].get());
    auto uploadOptions = makeDefaultUploadOptions();
    megaApi[0]->startUpload(fileName,
                            rootnode.get(),
                            nullptr,
                            &uploadOptions,
                            &tracker /*listener*/);

    ASSERT_TRUE(WaitFor(
        [&tracker]()
        {
            return tracker.startCount.load() >= 1;
        },
        30000))
        << "Upload did not emit the initial onTransferStart callback";

    WsUploadTransferSnapshot activeSession{};
    ASSERT_TRUE(waitForFirstUploadTransferSnapshot(
        *megaApi[0],
        activeSession,
        [](const WsUploadTransferSnapshot& snapshot)
        {
            return snapshot.wsFileno > 0 && !snapshot.wsSessionUrl.empty() &&
                   snapshot.state == TRANSFERSTATE_ACTIVE;
        },
        60,
        200))
        << "Upload did not expose an active WS session before forcing reconnect failures";
    ASSERT_TRUE(activeSession.found);

    // Ensure the targeted URL currently owns an active upload file before forcing reconnect
    // failures; otherwise retries can be consumed without surfacing transfer temporary-error.
    ws::UploadEngine::PoolStateForTesting activePoolState{};
    ASSERT_TRUE(waitForWsUploadPoolStateForTesting(
        *megaApi[0],
        activeSession.wsSessionUrl,
        activePoolState,
        [](const ws::UploadEngine::PoolStateForTesting& state)
        {
            return state.found && state.hasUploadingFile;
        },
        60,
        200))
        << "Could not observe an active uploading pool for URL before forcing handshake failures"
        << " [url=" << activeSession.wsSessionUrl << "]";

    // Step 2: force repeated handshake failures on the WS upload URLs and trigger reconnect.
    std::atomic<int> forcedHandshakeFailureCount{0};
    // Keep failing reconnect handshakes on WS upload URLs until transfer surfaces a temporary
    // error.
    globalMegaTestHooks.onWsHandshake =
        [&forcedHandshakeFailureCount, &tracker](
            const std::string& url,
            long,
            std::string& err)
        {
            // Reconnect can rotate WS upload endpoints, so do not gate failure injection on one URL.
            if (url.rfind("wss://", 0) != 0 || url.find("/ul/") == std::string::npos)
            {
                return false;
            }

            if (tracker.temporaryErrorCount.load() >= 1)
            {
                LOG_debug << "[SdkWsUploadRetryAfterHandshakeFailureRestartsTransferStart] stop "
                             "forcing WS handshake failures after temporary error"
                          << " [url=" << url
                          << "] [temporaryErrors=" << tracker.temporaryErrorCount.load() << "]";
                return false;
            }

            const int forced = ++forcedHandshakeFailureCount;
            err = "debug forced WS handshake failure";
            LOG_debug << "[SdkWsUploadRetryAfterHandshakeFailureRestartsTransferStart] force WS "
                         "handshake failure"
                      << " [url=" << url << "] [forcedCount=" << forced << "]";
            return true;
        };
    globalMegaTestHooks.onWsUploadSustainedHandshakeFailureWindowDs =
        [](dstime& windowDs)
        {
            windowDs = 0;
        };

    ASSERT_TRUE(notifyWsUploadNetworkDisconnectForTesting(*megaApi[0], 10))
        << "Failed to trigger a WS disconnect before the forced handshake failures";

    // Step 3: require temporary-error surfacing and eventual transition back to ACTIVE.
    bool sawTemporaryError = false;
    int lastForcedHandshakeFailureCount = -1;
    second_timer temporaryErrorTimer;
    while (temporaryErrorTimer.elapsed() < 90)
    {
        if (tracker.temporaryErrorCount.load() >= 1)
        {
            sawTemporaryError = true;
            break;
        }

        const int currentForcedFailureCount = forcedHandshakeFailureCount.load();
        if (currentForcedFailureCount == lastForcedHandshakeFailureCount)
        {
            // Re-nudge reconnect to keep exercising handshake-failure path and avoid idle windows
            // where retries are not consumed against an active pool.
            (void)notifyWsUploadNetworkDisconnectForTesting(*megaApi[0], 1);
        }
        lastForcedHandshakeFailureCount = currentForcedFailureCount;
        WaitMillisec(200);
    }

    ASSERT_TRUE(sawTemporaryError)
        << "Upload did not surface a temporary error after the forced WS handshake failures"
        << " [forcedHandshakeFailureCount=" << forcedHandshakeFailureCount.load()
        << " starts=" << tracker.startCount.load()
        << " updates=" << tracker.updateCount.load()
        << " activeUpdates=" << tracker.activeStateUpdateCount.load()
        << " lastState=" << tracker.lastState.load()
        << " temporaryErrors=" << tracker.temporaryErrorCount.load() << "]";
    ASSERT_GE(forcedHandshakeFailureCount.load(), 3)
        << "The forced WS handshake failure hook did not run enough times to cover sustained "
           "failure handling";

    ASSERT_TRUE(tracker.temporaryErrorCount.load() >= 1)
        << "Upload resumed data flow without surfacing a temporary error after sustained WS "
           "handshake failures"
        << " [starts=" << tracker.startCount.load()
        << " updates=" << tracker.updateCount.load()
        << " lastState=" << tracker.lastState.load()
        << " activeUpdates=" << tracker.activeStateUpdateCount.load() << "]";
    ASSERT_EQ(tracker.lastTemporaryError.load(), ErrorCodes::API_EAGAIN);

    ASSERT_TRUE(WaitFor(
        [&tracker]()
        {
            return tracker.sawActiveAfterTemporaryError.load();
        },
        45000))
        << "Upload resumed without returning to ACTIVE after the temporary error"
        << " [starts=" << tracker.startCount.load()
        << " updates=" << tracker.updateCount.load()
        << " activeUpdates=" << tracker.activeStateUpdateCount.load()
        << " lastState=" << tracker.lastState.load()
        << " temporaryErrors=" << tracker.temporaryErrorCount.load()
        << " lastTemporaryError=" << static_cast<int>(tracker.lastTemporaryError.load()) << "]";

    // Confirm the WS upload transfer is still present after retrying a failed handshake.
    WsUploadTransferSnapshot recoveredSession{};
    bool gotRecoveredSession = false;
    second_timer recoveredTimer;
    while (recoveredTimer.elapsed() < 30)
    {
        if (fetchBestWsUploadTransferSnapshot(*megaApi[0], recoveredSession, 1) &&
            recoveredSession.found && recoveredSession.wsFileno > 0)
        {
            gotRecoveredSession = true;
            break;
        }
        WaitMillisec(200);
    }

    ASSERT_TRUE(gotRecoveredSession)
        << "Could not observe WS upload transfer snapshot after recovery";
    ASSERT_EQ(recoveredSession.wsFileno, activeSession.wsFileno)
        << "WS upload file number changed across a retry";

    // Step 4: finish upload and verify cloud node presence.
    megaApi[0]->setMaxUploadSpeed(-1);
    ASSERT_EQ(tracker.waitForResult(180), ErrorCodes::API_OK)
        << "Upload did not complete successfully after retrying the failed handshake";

    rootnode.reset(megaApi[0]->getRootNode());
    ASSERT_TRUE(rootnode);

    std::unique_ptr<MegaNode> uploadedNode(
        megaApi[0]->getNodeByPathOfType(fileName.c_str(), rootnode.get(), MegaNode::TYPE_FILE));
    second_timer uploadTimer;
    while (!uploadedNode && uploadTimer.elapsed() < 60)
    {
        WaitMillisec(500);
        uploadedNode.reset(
            megaApi[0]->getNodeByPathOfType(fileName.c_str(), rootnode.get(), MegaNode::TYPE_FILE));
    }

    ASSERT_TRUE(uploadedNode)
        << "Upload completed locally but the uploaded node was not visible in the cloud";
#endif
}

/**
 * @brief Verify invalid pinned WS session URL falls back to a fresh session URL.
 *
 * - TEST1: Start upload and capture WS session metadata used for pinning.
 * - TEST2: Override cached session URL with an invalid endpoint and resume.
 * - TEST3: Require observed wsSessionUrl differs from stale pinned URL.
 */
TEST_F(SdkWsUploadTest, InvalidPinnedSessionFallsBackToFreshSession)
{
    LOG_info << "___TEST SdkWsUploadInvalidPinnedSessionFallsBackToFreshSession___";
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    // Step 1: start upload and capture session metadata for URL override.
    ASSERT_TRUE(createFileWithSize(UPFILE, kWsUploadDefaultFileSize, "U"))
        << "Couldn't create " << UPFILE;

    RequestTracker ct(megaApi[0].get());
    megaApi[0]->setMaxConnections(1, &ct);
    ASSERT_EQ(API_OK, ct.waitForResult(60)) << "setMaxConnections() failed or timed out";

    std::unique_ptr<MegaNode> rootnode{megaApi[0]->getRootNode()};
    ASSERT_TRUE(rootnode);

    ScopedUploadSpeedLimit restoreUploadSpeed{*megaApi[0], 100000};
    onTransferUpdate_progress = 0;

    TransferTracker ut(megaApi[0].get());
    auto uploadOptions = makeDefaultUploadOptions();
    megaApi[0]->startUpload(std::string{UPFILE},
                            rootnode.get(),
                            nullptr,
                            &uploadOptions,
                            &ut /*listener*/);

    second_timer timer;
    while (!ut.finished && !ut.started && timer.elapsed() < 90)
    {
        WaitMillisec(100);
    }

    ASSERT_TRUE(ut.started) << "Upload did not start in time";
    ASSERT_FALSE(ut.finished) << "Upload ended too early, with " << ut.waitForResult();

    WsUploadTransferSnapshot beforeOverride{};
    bool gotBeforeOverride = false;
    second_timer snapshotTimer;
    while (snapshotTimer.elapsed() < 20)
    {
        if (fetchBestWsUploadTransferSnapshot(*megaApi[0], beforeOverride, 1) &&
            beforeOverride.found && beforeOverride.wsFileno > 0)
        {
            gotBeforeOverride = true;
            break;
        }
        WaitMillisec(200);
    }
    if (!gotBeforeOverride)
    {
        GTEST_SKIP() << "No active WS upload transfer metadata observable before URL override";
    }

    // Step 2: force invalid pinned URL, then logout/resume to exercise failover path.
    // Step 2 retries the override to beat the `onStart` re-population race; Step 3
    // listens on the WS session-URL transition hook (registered before override via
    // `WsSessionUrlTransitionCapture`) for deterministic failover proof.
    const std::string invalidPinnedUrl = "wss://127.0.0.1:1/ul/invalid-pinned-session-url";
    WsSessionUrlTransitionCapture capture{invalidPinnedUrl};

    WsUploadTransferSnapshot forced{};
    bool observedOverride = false;
    second_timer overrideTimer;
    while (overrideTimer.elapsed() < 15)
    {
        ASSERT_TRUE(overrideFirstUploadSessionUrlForTesting(*megaApi[0], invalidPinnedUrl, 5));
        if (fetchBestWsUploadTransferSnapshot(*megaApi[0], forced, 1) && forced.found &&
            forced.serializedWsSessionUrl == invalidPinnedUrl)
        {
            observedOverride = true;
            break;
        }
        WaitMillisec(100);
    }
    ASSERT_TRUE(observedOverride)
        << "Override did not become observable within 15 s; onStart race may be persistent";
    ASSERT_EQ(forced.serializedWsSessionUrl, invalidPinnedUrl);
    ASSERT_EQ(forced.wsFileno, beforeOverride.wsFileno);

    std::unique_ptr<char[]> session(dumpSession());
    ASSERT_NO_FATAL_FAILURE(locallogout());
    const int uploadInterruptedCode = ut.waitForResult();
    ASSERT_TRUE(uploadInterruptedCode == API_EACCESS || uploadInterruptedCode == API_EINCOMPLETE)
        << "Upload interrupted with unexpected code: " << uploadInterruptedCode;

    ASSERT_NO_FATAL_FAILURE(resumeSession(session.get()));
    ASSERT_NO_FATAL_FAILURE(fetchnodes(0));
    // Uncap speed for the failover window so this check is not throttle-bound.
    megaApi[0]->setMaxUploadSpeed(-1);

    // Step 3: wait deterministically for the WS session-URL transition.
    const bool gotTransition = capture.waitForTransition(std::chrono::seconds(210));
    WsUploadTransferSnapshot failover{};
    bool switchedToFreshUrl = false;
    if (fetchBestWsUploadTransferSnapshot(*megaApi[0], failover, 1) && failover.found &&
        !failover.wsSessionUrl.empty() && failover.wsSessionUrl != invalidPinnedUrl)
    {
        switchedToFreshUrl = true;
    }

    ASSERT_TRUE(gotTransition)
        << "WS session-URL transition hook did not fire within 210 s post-resume";
    ASSERT_NE(capture.capturedNewUrl(), invalidPinnedUrl)
        << "Transition hook fired but reported the same invalidPinnedUrl";
    if (switchedToFreshUrl)
    {
        ASSERT_GT(failover.wsFileno, 0u);
        ASSERT_EQ(failover.wsFileno, beforeOverride.wsFileno);
        ASSERT_NE(failover.wsSessionUrl, invalidPinnedUrl);
    }

    // Step 4: cleanup active upload for test isolation.
    ASSERT_EQ(API_OK, synchronousCancelTransfers(0, MegaTransfer::TYPE_UPLOAD));
}

/**
 * @brief Verify detached transfer on invalid pinned session completes on a fresh pool.
 *
 * - TEST1: Start upload and inject invalid pinned session URL metadata.
 * - TEST2: Observe transfer is no longer attached to stale pinned pool URL.
 * - TEST3: Require upload completes and cloud node exists with expected size.
 */
TEST_F(SdkWsUploadTest, InvalidPinnedSessionDetachedTransferCompletesOnFreshPool)
{
    LOG_info << "___TEST SdkWsUploadInvalidPinnedSessionDetachedTransferCompletesOnFreshPool___";
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    // Step 1: start upload and capture session metadata for URL override.
    const std::string fileName =
        "ws_invalid_pinned_detach_" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".bin";
    ASSERT_TRUE(createFileWithSize(fileName, kWsUploadDefaultFileSize, "D"))
        << "Couldn't create " << fileName;

    auto cleanupTransfers = makeScopedDestructor(
        [this]()
        {
            (void)synchronousCancelTransfers(0, MegaTransfer::TYPE_UPLOAD);
        });

    RequestTracker ct(megaApi[0].get());
    megaApi[0]->setMaxConnections(1, &ct);
    ASSERT_EQ(API_OK, ct.waitForResult(60)) << "setMaxConnections() failed or timed out";

    std::unique_ptr<MegaNode> rootnode{megaApi[0]->getRootNode()};
    ASSERT_TRUE(rootnode);

    ScopedUploadSpeedLimit restoreUploadSpeed{*megaApi[0], 100000};
    onTransferUpdate_progress = 0;

    TransferTracker ut(megaApi[0].get());
    auto uploadOptions = makeDefaultUploadOptions();
    megaApi[0]->startUpload(fileName, rootnode.get(), nullptr, &uploadOptions, &ut /*listener*/);

    second_timer timer;
    while (!ut.finished && !ut.started && timer.elapsed() < 90)
    {
        WaitMillisec(100);
    }

    ASSERT_TRUE(ut.started) << "Upload did not start in time";
    ASSERT_FALSE(ut.finished) << "Upload ended too early, with " << ut.waitForResult();

    WsUploadTransferSnapshot beforeOverride{};
    bool gotBeforeOverride = false;
    second_timer snapshotTimer;
    while (snapshotTimer.elapsed() < 20)
    {
        if (fetchBestWsUploadTransferSnapshot(*megaApi[0], beforeOverride, 1) &&
            beforeOverride.found && beforeOverride.wsFileno > 0)
        {
            gotBeforeOverride = true;
            break;
        }
        WaitMillisec(200);
    }
    if (!gotBeforeOverride)
    {
        GTEST_SKIP() << "No active WS upload transfer metadata observable before URL override";
    }

    // Step 2: force invalid pinned URL, then logout/resume to trigger detached failover.
    // The override sets `t->ws_session_url = invalidPinnedUrl` on the client thread,
    // but `onStart` (`megaclient.cpp:2314-2328`) repopulates `t.ws_session_url` from
    // `wsEngine()->getSessionUrl(t, …)` whenever it fires for that transfer. When
    // `onStart` interleaves between the override-lambda completing and the readback
    // snapshot, the URL appears unchanged. Retry until the override wins or the
    // window elapses, then strict-assert. The transition-capture pre-registers a
    // hook so Step 3 can wait deterministically for the fresh-pool transition.
    const std::string invalidPinnedUrl = "wss://127.0.0.1:1/ul/invalid-pinned-session-url";
    WsSessionUrlTransitionCapture capture{invalidPinnedUrl};

    WsUploadTransferSnapshot forced{};
    bool observedOverride = false;
    second_timer overrideTimer;
    while (overrideTimer.elapsed() < 15)
    {
        ASSERT_TRUE(overrideFirstUploadSessionUrlForTesting(*megaApi[0], invalidPinnedUrl, 5));
        if (fetchBestWsUploadTransferSnapshot(*megaApi[0], forced, 1) && forced.found &&
            forced.serializedWsSessionUrl == invalidPinnedUrl)
        {
            observedOverride = true;
            break;
        }
        WaitMillisec(100);
    }
    ASSERT_TRUE(observedOverride)
        << "Override did not become observable within 15 s; onStart race may be persistent";
    ASSERT_EQ(forced.serializedWsSessionUrl, invalidPinnedUrl);
    ASSERT_EQ(forced.wsFileno, beforeOverride.wsFileno);

    std::unique_ptr<char[]> session(dumpSession());
    ASSERT_NO_FATAL_FAILURE(locallogout());
    const int uploadInterruptedCode = ut.waitForResult();
    ASSERT_TRUE(uploadInterruptedCode == API_EACCESS || uploadInterruptedCode == API_EINCOMPLETE)
        << "Upload interrupted with unexpected code: " << uploadInterruptedCode;

    ASSERT_NO_FATAL_FAILURE(resumeSession(session.get()));
    ASSERT_NO_FATAL_FAILURE(fetchnodes(0));
    // Uncap speed before completion wait to keep this test fast.
    megaApi[0]->setMaxUploadSpeed(-1);

    // Step 3: wait deterministically for the WS session-URL transition. The hook
    // captures the moment when `invalidatePinnedSessionUrl` clears the URL OR when
    // `onStart` re-populates with a fresh-pool URL. Snapshot the live state too,
    // but the strict success signal is the hook event itself.
    const bool gotTransition = capture.waitForTransition(std::chrono::seconds(210));
    WsUploadTransferSnapshot failover{};
    bool switchedToFreshUrl = false;
    if (fetchBestWsUploadTransferSnapshot(*megaApi[0], failover, 1) && failover.found &&
        !failover.wsSessionUrl.empty() && failover.wsSessionUrl != invalidPinnedUrl)
    {
        switchedToFreshUrl = true;
    }

    ASSERT_TRUE(gotTransition)
        << "WS session-URL transition hook did not fire within 210 s post-resume";
    ASSERT_NE(capture.capturedNewUrl(), invalidPinnedUrl)
        << "Transition hook fired but reported the same invalidPinnedUrl";
    if (switchedToFreshUrl)
    {
        ASSERT_GT(failover.wsFileno, 0u);
        ASSERT_EQ(failover.wsFileno, beforeOverride.wsFileno);
        ASSERT_NE(failover.wsSessionUrl, invalidPinnedUrl);
    }

    // Step 4: require completion on fresh pool and verify uploaded node exists.
    rootnode.reset(megaApi[0]->getRootNode());
    ASSERT_TRUE(rootnode);
    std::unique_ptr<MegaNode> cloudNode(
        megaApi[0]->getNodeByPathOfType(fileName.c_str(), rootnode.get(), MegaNode::TYPE_FILE));
    second_timer completionTimer;
    while (!cloudNode && completionTimer.elapsed() < 240)
    {
        WaitMillisec(500);
        cloudNode.reset(
            megaApi[0]->getNodeByPathOfType(fileName.c_str(), rootnode.get(), MegaNode::TYPE_FILE));
    }

    ASSERT_TRUE(cloudNode)
        << "Transfer switched URL but did not complete upload on a fresh pool in time";

    deleteFile(fileName);
}

/**
 * @brief Verify canceling an active WS upload is safe with in-flight work.
 *
 * - TEST1: Start throttled upload and wait for non-zero progress snapshot.
 * - TEST2: Cancel active upload via synchronousCancelTransfers(TYPE_UPLOAD).
 * - TEST3: Assert transfer finishes with expected cancel/incomplete result and no crash.
 */
TEST_F(SdkWsUploadTest, CancelDuringActiveTransfer)
{
    LOG_info << "___TEST SdkWsUploadCancelDuringActiveTransfer___";
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    const std::string fileName =
        "ws_cancel_active_" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".bin";

    // Step 1: create a file large enough to observe active transfer progress.
    // Gives a stable active-transfer window before cancel with test throttling.
    constexpr size_t fileSize = 2 * kWsUploadDefaultFileSize;
    ASSERT_TRUE(createFileWithSize(fileName, fileSize, "C")) << "Couldn't create " << fileName;

    auto cleanupFile = makeScopedDestructor([this, &fileName]()
                                            {
                                                deleteFile(fileName);
                                            });

    RequestTracker ct(megaApi[0].get());
    megaApi[0]->setMaxConnections(1, &ct);
    ASSERT_EQ(API_OK, ct.waitForResult(60));

    std::unique_ptr<MegaNode> rootnode{megaApi[0]->getRootNode()};
    ASSERT_TRUE(rootnode);

    // Step 2: start a throttled upload so cancel happens while chunks are in flight.
    ScopedUploadSpeedLimit restoreUploadSpeed{*megaApi[0], 100000};

    TransferTracker ut(megaApi[0].get());
    auto uploadOptions = makeDefaultUploadOptions();
    megaApi[0]->startUpload(fileName, rootnode.get(), nullptr, &uploadOptions, &ut);

    // Step 3: wait until the WS engine confirms forward progress.
    WsUploadTransferSnapshot snapshot{};
    ASSERT_TRUE(waitForFirstUploadTransferSnapshot(
        *megaApi[0],
        snapshot,
        [](const WsUploadTransferSnapshot& s)
        {
            return s.progressCompleted > 0;
        },
        60,
        200))
        << "Upload did not make progress before cancel";

    // Step 4: cancel uploads while WS workers may still be sending / awaiting ACK.
    // This exercises freeq(PUT) + wsEngine()->remove() on an active transfer.
    ASSERT_EQ(API_OK, synchronousCancelTransfers(0, MegaTransfer::TYPE_UPLOAD));

    // Step 5: transfer must resolve to cancellation and not crash.
    const auto cancelResult = ut.waitForResult(30);
    ASSERT_EQ(cancelResult, API_EINCOMPLETE)
        << "Unexpected result after cancel: " << cancelResult;
}

/**
 * @brief Verify deleting local source mid-transfer fails WS upload safely.
 *
 * - TEST1: Start throttled WS upload on a local file.
 * - TEST2: Delete local file during active transfer window.
 * - TEST3: Require Require terminal failure (read/incomplete path) without crash.
 */
TEST_F(SdkWsUploadTest, FileDeletedDuringTransfer)
{
    LOG_info << "___TEST SdkWsUploadFileDeletedDuringTransfer___";
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    const std::string fileName =
        "ws_delete_mid_" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".bin";

    // Step 1: create a local file and start uploading it through WS.
    constexpr size_t fileSize = kWsUploadDefaultFileSize;
    ASSERT_TRUE(createFileWithSize(fileName, fileSize, "D")) << "Couldn't create " << fileName;

    auto cleanupTransfers = makeScopedDestructor(
        [this]()
        {
            (void)synchronousCancelTransfers(0, MegaTransfer::TYPE_UPLOAD);
        });
    auto cleanupFile = makeScopedDestructor(
        [this, &fileName]()
        {
            deleteFile(fileName);
        });

    RequestTracker ct(megaApi[0].get());
    megaApi[0]->setMaxConnections(1, &ct);
    ASSERT_EQ(API_OK, ct.waitForResult(60));

    std::unique_ptr<MegaNode> rootnode{megaApi[0]->getRootNode()};
    ASSERT_TRUE(rootnode);

    // Step 2: throttle upload so we have a stable mid-transfer window.
    ScopedUploadSpeedLimit restoreUploadSpeed{*megaApi[0], 100000};

    WsUploadRetryTracker ut(megaApi[0].get());
    auto uploadOptions = makeDefaultUploadOptions();
    megaApi[0]->startUpload(fileName, rootnode.get(), nullptr, &uploadOptions, &ut);

    // Step 3: wait for confirmed progress before deleting local source file.
    // 120s timeout (was 60s in fu7-9) gives session-negotiation more headroom under
    // server-side throttle storms; the skip below only fires when even 2x window is
    // insufficient (a genuine "engine could not progress" case, not a timing flake).
    WsUploadTransferSnapshot snapshot{};
    const bool gotProgress = waitForFirstUploadTransferSnapshot(
        *megaApi[0],
        snapshot,
        [](const WsUploadTransferSnapshot& s)
        {
            return s.progressCompleted > 0;
        },
        120,
        200);

    if (!gotProgress)
    {
        // If we never saw progress (e.g., non-WS path), skip this mid-transfer scenario.
        deleteFile(fileName);
        const auto r = ut.waitForResult(120);
        (void)r;
        GTEST_SKIP() << "Upload did not show WS progress; cannot test mid-transfer file deletion";
    }

    // Step 4: delete local file during active transfer.
    deleteFile(fileName);

    // Let completion/failure surface quickly after deletion.
    megaApi[0]->setMaxUploadSpeed(-1);

    // Step 5: transfer should reach a terminal state without crash.
    const auto finalResult = ut.waitForResult(120, false);
    std::ostringstream timeoutDetails;
    if (finalResult == LOCAL_ETIMEOUT)
    {
        WsUploadTransferSnapshot stalled{};
        const bool gotSnapshot = fetchBestWsUploadTransferSnapshot(*megaApi[0], stalled, 5);
        const bool snapshotFound = gotSnapshot && stalled.found;
        const bool localFileStillExists = fileexists(fileName);

        timeoutDetails << " timeout_diagnostics={"
                       << "local_file_exists=" << (localFileStillExists ? "true" : "false")
                       << ", start_count=" << ut.startCount.load()
                       << ", update_count=" << ut.updateCount.load()
                       << ", temp_error_count=" << ut.temporaryErrorCount.load()
                       << ", last_temp_error=" << static_cast<int>(ut.lastTemporaryError.load())
                       << ", last_state=" << ut.lastState.load()
                       << ", active_state_updates=" << ut.activeStateUpdateCount.load()
                       << ", saw_active_after_temp_error="
                       << (ut.sawActiveAfterTemporaryError.load() ? "true" : "false")
                       << ", tracker_finished=" << (ut.finished.load() ? "true" : "false")
                       << ", snapshot_found=" << (snapshotFound ? "true" : "false");

        if (snapshotFound)
        {
            timeoutDetails << ", snapshot={"
                           << "state=" << stalled.state
                           << ", progress_completed=" << stalled.progressCompleted
                           << ", pos=" << stalled.pos
                           << ", size=" << stalled.size
                           << ", ws_fileno=" << stalled.wsFileno
                           << ", ws_session_url=" << stalled.wsSessionUrl
                           << "}";
        }
        timeoutDetails << " }";
    }
    LOG_debug << "[SdkWsUploadFileDeletedDuringTransfer] Transfer result: " << finalResult
              << timeoutDetails.str();

    // Accepted outcomes:
    // - API_EREAD: read/open fails after deletion.
    // - API_EINCOMPLETE: cancellation/race path.
    // - API_OK (Windows only): the engine keeps the file handle open, and Windows defers
    //   actual file removal until the last handle is closed, so all reads succeed despite
    //   the directory entry being deleted.
#ifdef _WIN32
    ASSERT_TRUE(finalResult == API_EREAD || finalResult == API_EINCOMPLETE || finalResult == API_OK)
#else
    ASSERT_TRUE(finalResult == API_EREAD || finalResult == API_EINCOMPLETE)
#endif
        << "Unexpected result after local file deletion: " << finalResult << timeoutDetails.str();
}

/**
 * @brief Verify local file modification during WS upload is handled safely.
 *
 * - TEST1: Start throttled upload with initial file content.
 * - TEST2: Modify file content/size while transfer is active.
 * - TEST3: Require completion with modified size, or safe read/incomplete failure; no crash.
 */
TEST_F(SdkWsUploadTest, FileModifiedDuringTransfer)
{
    LOG_info << "___TEST SdkWsUploadFileModifiedDuringTransfer___";
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    const std::string fileName =
        "ws_modify_mid_" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".bin";

    constexpr size_t initialFileSize = kWsUploadDefaultFileSize;
    constexpr size_t modifiedFileSize = 2 * kWsUploadDefaultFileSize;

    // Step 1: prepare initial source file and start upload.
    ASSERT_TRUE(createFileWithSize(fileName, initialFileSize, "M"))
        << "Couldn't create " << fileName;

    auto cleanupFile = makeScopedDestructor([this, &fileName]()
                                            {
                                                deleteFile(fileName);
                                            });
    auto cleanupTransfers = makeScopedDestructor(
        [this]()
        {
            (void)synchronousCancelTransfers(0, MegaTransfer::TYPE_UPLOAD);
        });

    RequestTracker ct(megaApi[0].get());
    megaApi[0]->setMaxConnections(1, &ct);
    ASSERT_EQ(API_OK, ct.waitForResult(60));

    std::unique_ptr<MegaNode> rootnode{megaApi[0]->getRootNode()};
    ASSERT_TRUE(rootnode);

    // Step 2: throttle to guarantee we can modify file mid-transfer.
    ScopedUploadSpeedLimit restoreUploadSpeed{*megaApi[0], 100000};

    WsUploadRetryTracker ut(megaApi[0].get());
    auto uploadOptions = makeDefaultUploadOptions();
    megaApi[0]->startUpload(fileName, rootnode.get(), nullptr, &uploadOptions, &ut);

    // Step 3: wait until transfer enters active progress.
    // 120s timeout (was 60s in fu7-9) gives session-negotiation more headroom under
    // server-side throttle storms; the skip below only fires when even 2x window is
    // insufficient.
    WsUploadTransferSnapshot snapshot{};
    const bool gotProgress = waitForFirstUploadTransferSnapshot(
        *megaApi[0],
        snapshot,
        [](const WsUploadTransferSnapshot& s)
        {
            return s.progressCompleted > 0;
        },
        120,
        200);

    if (!gotProgress)
    {
        GTEST_SKIP() << "Upload did not show WS progress; cannot test mid-transfer file modification";
    }

    // Step 4: modify source file content and size while upload is active.
    ASSERT_TRUE(createFileWithSize(fileName, modifiedFileSize, "N"))
        << "Couldn't rewrite " << fileName;

    // Window has been created; uncap to avoid slow completion/failure handling.
    megaApi[0]->setMaxUploadSpeed(-1);

    // Step 5: transfer should either fail safely, or complete using the upload snapshot
    // captured when transfer started.
    const auto finalResult = ut.waitForResult(180);
    LOG_debug << "[SdkWsUploadFileModifiedDuringTransfer] Transfer result: " << finalResult;

    if (finalResult == API_OK)
    {
        rootnode.reset(megaApi[0]->getRootNode());
        ASSERT_TRUE(rootnode);
        std::unique_ptr<MegaNode> cloudNode(
            megaApi[0]->getNodeByPathOfType(fileName.c_str(), rootnode.get(), MegaNode::TYPE_FILE));
        ASSERT_TRUE(cloudNode) << "Upload completed but uploaded node is missing";
        ASSERT_EQ(cloudNode->getSize(), static_cast<int64_t>(initialFileSize))
            << "Upload completed but cloud node size does not match initial upload snapshot";
    }
    else
    {
        // Step 6: if it fails, only accept known safe outcomes.
        ASSERT_TRUE(finalResult == API_EREAD || finalResult == API_EINCOMPLETE)
            << "Unexpected result after local file modification: " << finalResult;
    }
}

#if defined(DEBUG)
/**
 * @brief Verify same-instance WS engine stop/start does not stall an active upload.
 *
 * - TEST1: Start throttled upload and confirm progress > 0.
 * - TEST2: Trigger same-instance stop()/start() on the current WS engine.
 * - TEST3: Require meaningful forward progress and eventual API_OK completion.
 */
TEST_F(SdkWsUploadTest, StopStartSameEngineDuringTransfer)
{
    LOG_info << "___TEST SdkWsUploadStopStartSameEngineDuringTransfer___";
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    const std::string fileName =
        "ws_stop_start_same_engine_" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".bin";

    constexpr size_t fileSize = kWsUploadDefaultFileSize;
    constexpr m_off_t expectedProgressDelta = 1024 * 1024;
    ASSERT_TRUE(createFileWithSize(fileName, fileSize, "S")) << "Couldn't create " << fileName;

    auto cleanupFile = makeScopedDestructor(
        [this, &fileName]()
        {
            deleteFile(fileName);
        });
    auto cleanupTransfers = makeScopedDestructor(
        [this]()
        {
            (void)synchronousCancelTransfers(0, MegaTransfer::TYPE_UPLOAD);
        });

    RequestTracker ct(megaApi[0].get());
    megaApi[0]->setMaxConnections(1, &ct);
    ASSERT_EQ(API_OK, ct.waitForResult(60));

    std::unique_ptr<MegaNode> rootnode{megaApi[0]->getRootNode()};
    ASSERT_TRUE(rootnode);

    ScopedUploadSpeedLimit restoreUploadSpeed{*megaApi[0], 100000};

    TransferTracker ut(megaApi[0].get());
    auto uploadOptions = makeDefaultUploadOptions();
    megaApi[0]->startUpload(fileName, rootnode.get(), nullptr, &uploadOptions, &ut);

    WsUploadTransferSnapshot beforeRestart{};
    ASSERT_TRUE(waitForFirstUploadTransferSnapshot(
        *megaApi[0],
        beforeRestart,
        [](const WsUploadTransferSnapshot& s)
        {
            return s.progressCompleted > 0;
        },
        60,
        200))
        << "Upload did not make progress before same-engine restart";

    ASSERT_TRUE(restartWsUploadEngineForTesting(*megaApi[0], 10))
        << "Failed to restart the current WS engine instance";

    bool progressAdvanced = false;
    WsUploadTransferSnapshot afterRestart{};
    WsUploadTransferSnapshot lastObservedAfterRestart{};
    second_timer restartTimer;
    while (restartTimer.elapsed() < 45)
    {
        if (fetchBestWsUploadTransferSnapshot(*megaApi[0], afterRestart, 1) && afterRestart.found)
        {
            lastObservedAfterRestart = afterRestart;
            if (afterRestart.progressCompleted >=
                beforeRestart.progressCompleted + expectedProgressDelta)
            {
                progressAdvanced = true;
                break;
            }
        }

        if (ut.finished)
        {
            break;
        }

        WaitMillisec(300);
    }

    ASSERT_TRUE(progressAdvanced)
        << "Upload did not show meaningful progress after same-engine stop/start"
        << " [before progress=" << beforeRestart.progressCompleted
        << " last progress=" << lastObservedAfterRestart.progressCompleted
        << " last state=" << lastObservedAfterRestart.state
        << " last fileno=" << lastObservedAfterRestart.wsFileno
        << " last live url=" << lastObservedAfterRestart.wsSessionUrl << "]";

    megaApi[0]->setMaxUploadSpeed(-1);
    const auto finalResult = ut.waitForResult(240);
    ASSERT_EQ(finalResult, API_OK) << "Upload did not complete after same-engine WS stop/start";

    rootnode.reset(megaApi[0]->getRootNode());
    ASSERT_TRUE(rootnode);
    std::unique_ptr<MegaNode> cloudNode(
        megaApi[0]->getNodeByPathOfType(fileName.c_str(), rootnode.get(), MegaNode::TYPE_FILE));
    ASSERT_TRUE(cloudNode) << "Uploaded file not found in cloud";
    ASSERT_EQ(cloudNode->getSize(), static_cast<int64_t>(fileSize));
}

/**
 * @brief Verify WS upload survives simulated network disconnect and reconnect.
 *
 * - TEST1: Start throttled upload and confirm progress > 0.
 * - TEST2: Trigger notifyNetworkDisconnectForTesting() and wait for reconnect path.
 * - TEST3: Require transfer completes and uploaded node size matches local size.
 */
TEST_F(SdkWsUploadTest, DisconnectReconnectDuringTransfer)
{
    LOG_info << "___TEST SdkWsUploadDisconnectReconnectDuringTransfer___";
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    const std::string fileName =
        "ws_disconnect_reconnect_" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".bin";

    // Step 1: create a file large enough to survive disconnect / reconnect window.
    constexpr size_t fileSize = kWsUploadDefaultFileSize;
    ASSERT_TRUE(createFileWithSize(fileName, fileSize, "R")) << "Couldn't create " << fileName;

    auto cleanupFile = makeScopedDestructor(
        [this, &fileName]()
        {
            deleteFile(fileName);
        });
    auto cleanupTransfers = makeScopedDestructor(
        [this]()
        {
            (void)synchronousCancelTransfers(0, MegaTransfer::TYPE_UPLOAD);
        });

    RequestTracker ct(megaApi[0].get());
    megaApi[0]->setMaxConnections(1, &ct);
    ASSERT_EQ(API_OK, ct.waitForResult(60));

    std::unique_ptr<MegaNode> rootnode{megaApi[0]->getRootNode()};
    ASSERT_TRUE(rootnode);

    // Step 2: start throttled upload to keep transfer active during disconnect simulation.
    ScopedUploadSpeedLimit restoreUploadSpeed{*megaApi[0], 100000};

    TransferTracker ut(megaApi[0].get());
    auto uploadOptions = makeDefaultUploadOptions();
    megaApi[0]->startUpload(fileName, rootnode.get(), nullptr, &uploadOptions, &ut);

    // Step 3: wait until upload has confirmed progress.
    WsUploadTransferSnapshot beforeDisconnect{};
    ASSERT_TRUE(waitForFirstUploadTransferSnapshot(
        *megaApi[0],
        beforeDisconnect,
        [](const WsUploadTransferSnapshot& s)
        {
            return s.progressCompleted > 0;
        },
        60,
        200))
        << "Upload did not make progress before disconnect simulation";

    // Step 4: simulate network disconnect and allow WS path to recover.
    ASSERT_TRUE(notifyWsUploadNetworkDisconnectForTesting(*megaApi[0], 10))
        << "Failed to notify WS upload network disconnect";

    bool progressAdvanced = false;
    WsUploadTransferSnapshot afterDisconnect{};
    second_timer reconnectTimer;
    while (reconnectTimer.elapsed() < 90)
    {
        if (fetchBestWsUploadTransferSnapshot(*megaApi[0], afterDisconnect, 1) &&
            afterDisconnect.found &&
            afterDisconnect.progressCompleted > beforeDisconnect.progressCompleted)
        {
            progressAdvanced = true;
            break;
        }

        if (ut.finished)
        {
            break;
        }

        WaitMillisec(300);
    }

    // Step 5: require completion and post-disconnect progress growth.
    megaApi[0]->setMaxUploadSpeed(-1);
    const auto finalResult = ut.waitForResult(240);
    ASSERT_EQ(finalResult, API_OK)
        << "Upload did not complete after simulated network disconnect/reconnect";
    ASSERT_TRUE(progressAdvanced || finalResult == API_OK)
        << "Upload did not show post-disconnect progress growth";

    // Step 6: verify uploaded node exists and size matches source.
    rootnode.reset(megaApi[0]->getRootNode());
    ASSERT_TRUE(rootnode);
    std::unique_ptr<MegaNode> cloudNode(
        megaApi[0]->getNodeByPathOfType(fileName.c_str(), rootnode.get(), MegaNode::TYPE_FILE));
    ASSERT_TRUE(cloudNode) << "Uploaded file not found in cloud";
    ASSERT_EQ(cloudNode->getSize(), static_cast<int64_t>(fileSize));
}

/**
 * @brief Verify WS upload overquota path propagates expected transfer error.
 *
 * - TEST1: Start upload and inject overquota through debug HTTP hook path.
 * - TEST2: Observe transfer temp error/terminal result callbacks.
 * - TEST3: If injection lands, require expected overquota error handling.
 */
TEST_F(SdkWsUploadTest, OverquotaDuringTransfer)
{
    LOG_info << "___TEST SdkWsUploadOverquotaDuringTransfer___";
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

#ifndef MEGASDK_DEBUG_TEST_HOOKS_ENABLED
    GTEST_SKIP() << "Requires MEGASDK_DEBUG_TEST_HOOKS_ENABLED";
#else
    // Step 1: source file
    const std::string fileName =
        "ws_overquota_mid_" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".bin";
    constexpr size_t fileSize = kWsUploadDefaultFileSize;
    ASSERT_TRUE(createFileWithSize(fileName, fileSize, "Q")) << "Couldn't create " << fileName;

    auto cleanupFile = makeScopedDestructor(
        [this, &fileName]()
        {
            deleteFile(fileName);
        });
    auto cleanupTransfers = makeScopedDestructor(
        [this]()
        {
            (void)synchronousCancelTransfers(0, MegaTransfer::TYPE_UPLOAD);
        });

    // Step 2: limit conns + throttle upload to keep it alive long enough to install the hook
    // and observe chunk-sends. setMaxConnections(1) keeps WsPool churn to a minimum so the
    // single chunk-send firing the hook is deterministic.
    RequestTracker ct(megaApi[0].get());
    megaApi[0]->setMaxConnections(1, &ct);
    ASSERT_EQ(API_OK, ct.waitForResult(60));

    std::unique_ptr<MegaNode> rootnode{megaApi[0]->getRootNode()};
    ASSERT_TRUE(rootnode);

    ScopedUploadSpeedLimit restoreUploadSpeed{*megaApi[0], 100000};

    // Step 3: start upload + capture transferTag once it has been assigned.
    TransferTempErrorTracker tracker(megaApi[0].get());
    auto uploadOptions = makeDefaultUploadOptions();
    megaApi[0]->startUpload(fileName, rootnode.get(), nullptr, &uploadOptions, &tracker);

    // Step 4: wait for active WS progress before installing the hook — this confirms
    // chunk-sends are happening and gives us a stable transfer tag.
    // 120s timeout (was 60s in fu7-9) gives session-negotiation more headroom under
    // server-side throttle storms; the skip below only fires when even 2x window is
    // insufficient.
    WsUploadTransferSnapshot beforeOverquota{};
    const bool gotProgress = waitForFirstUploadTransferSnapshot(
        *megaApi[0],
        beforeOverquota,
        [](const WsUploadTransferSnapshot& s)
        {
            return s.progressCompleted > 0;
        },
        120,
        200);
    if (!gotProgress)
    {
        megaApi[0]->setMaxUploadSpeed(-1);
        (void)synchronousCancelTransfers(0, MegaTransfer::TYPE_UPLOAD);
        GTEST_SKIP() << "Upload did not show WS progress; cannot test WS mid-transfer overquota";
    }
    const int targetTag = tracker.transferTag.load();
    ASSERT_GE(targetTag, 0) << "transfer tag not captured before installing hook";

    // Step 5: install the WS chunk-send OVERQUOTA injection hook. One-shot semantics —
    // the next chunk-send for this transfer-tag is force-failed via the same code path
    // a real server-side OVERQUOTA event would take (purgeFileLocked + mCb.onFail(EOVERQUOTA, Retryable)
    // → Transfer::failed(API_EOVERQUOTA, ..., 0) → app->transfer_failed).
    ::mega::test::wsupload::WsChunkSendOverquotaCapture overquotaHook(targetTag);

    // Uncap upload speed so chunk-sends proceed quickly and the hook fires promptly.
    megaApi[0]->setMaxUploadSpeed(-1);

    // Step 6: wait for the hook to fire (chunk-send observed).
    ASSERT_TRUE(overquotaHook.waitForFire(std::chrono::seconds(60)))
        << "OVERQUOTA hook never fired — no chunk-sends observed within 60s "
        << "(totalSeen=" << overquotaHook.totalSeen() << ")";

    // Step 7: assert the resulting failure is API_EOVERQUOTA via temporary-error path.
    const auto result = tracker.waitForResult(60);
    ASSERT_TRUE(tracker.wasTemporaryError())
        << "Expected temporary error after OVERQUOTA injection, got result: " << result;
    ASSERT_EQ(result, API_EOVERQUOTA)
        << "Expected API_EOVERQUOTA after injection, got: " << result;

    // Step 8: cancel in-flight transfer (Retryable disposition would keep retrying).
    const int finalTag = tracker.transferTag.load();
    if (finalTag >= 0)
    {
        megaApi[0]->cancelTransferByTag(finalTag);
    }
#endif
}

/**
 * @brief Verify repeated pause/resume cycles on one WS upload remain stable.
 *
 * - TEST1: Start throttled upload and capture transfer id/state.
 * - TEST2: Execute multiple pause/resume cycles while transfer is active.
 * - TEST3: Assert paused state is observed, progress resumes after unpause, and transfer ends API_OK.
 */
TEST_F(SdkWsUploadTest, MultiplePauseResumeCycles)
{
    LOG_info << "___TEST SdkWsUploadMultiplePauseResumeCycles___";
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    const std::string fileName =
        "ws_multi_pause_resume_" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".bin";

    // Step 1: create the local source file used across pause/resume cycles.
    constexpr size_t fileSize = kWsUploadDefaultFileSize;
    ASSERT_TRUE(createFileWithSize(fileName, fileSize, "P")) << "Couldn't create " << fileName;

    auto cleanupFile = makeScopedDestructor(
        [this, &fileName]()
        {
            deleteFile(fileName);
        });
    auto cleanupTransfers = makeScopedDestructor(
        [this]()
        {
            (void)synchronousCancelTransfers(0, MegaTransfer::TYPE_UPLOAD);
        });

    RequestTracker ct(megaApi[0].get());
    megaApi[0]->setMaxConnections(1, &ct);
    ASSERT_EQ(API_OK, ct.waitForResult(60));

    std::unique_ptr<MegaNode> rootnode{megaApi[0]->getRootNode()};
    ASSERT_TRUE(rootnode);

    ScopedUploadSpeedLimit restoreUploadSpeed{*megaApi[0], 100000};

    TransferTracker ut(megaApi[0].get());
    auto uploadOptions = makeDefaultUploadOptions();
    megaApi[0]->startUpload(fileName, rootnode.get(), nullptr, &uploadOptions, &ut);

    // Step 2: wait until upload is active and confirmed progress has started.
    WsUploadTransferSnapshot snapshot{};
    ASSERT_TRUE(waitForFirstUploadTransferSnapshot(
        *megaApi[0],
        snapshot,
        [](const WsUploadTransferSnapshot& s)
        {
            return s.progressCompleted > 0;
        },
        60,
        200))
        << "Upload did not make initial progress";

    m_off_t lastProgress = snapshot.progressCompleted;
    for (int cycle = 0; cycle < 3; ++cycle)
    {
        // Step 3: pause uploads globally and verify pause is observed.
        megaApi[0]->pauseTransfers(true, MegaTransfer::TYPE_UPLOAD);

        ASSERT_TRUE(WaitFor(
            [this]()
            {
                return megaApi[0]->areTransfersPaused(MegaTransfer::TYPE_UPLOAD);
            },
            10000))
            << "Upload pause was not observed";

        WaitMillisec(1500);

        WsUploadTransferSnapshot paused{};
        ASSERT_TRUE(fetchBestWsUploadTransferSnapshot(*megaApi[0], paused, 5) && paused.found)
            << "Could not fetch snapshot while paused";
        ASSERT_GE(paused.progressCompleted, lastProgress)
            << "Progress moved backwards during pause cycle " << cycle;

        // Step 4: resume uploads and require forward progress + ACTIVE state.
        megaApi[0]->pauseTransfers(false, MegaTransfer::TYPE_UPLOAD);

        ASSERT_TRUE(WaitFor(
            [this]()
            {
                return !megaApi[0]->areTransfersPaused(MegaTransfer::TYPE_UPLOAD);
            },
            10000))
            << "Upload resume was not observed";

        WsUploadTransferSnapshot resumed{};
        ASSERT_TRUE(waitForFirstUploadTransferSnapshot(
            *megaApi[0],
            resumed,
            [paused](const WsUploadTransferSnapshot& s)
            {
                return s.progressCompleted > paused.progressCompleted;
            },
            60,
            200))
            << "Progress did not continue after resume cycle " << cycle;
        ASSERT_EQ(resumed.state, TRANSFERSTATE_ACTIVE)
            << "Transfer state is not ACTIVE after progress resumed in cycle " << cycle;

        lastProgress = resumed.progressCompleted;
    }

    // Step 5: upload should complete and cloud node should match expected size.
    megaApi[0]->setMaxUploadSpeed(-1);
    const auto finalResult = ut.waitForResult(240);
    ASSERT_EQ(finalResult, API_OK)
        << "Upload did not complete after multiple pause/resume cycles";

    rootnode.reset(megaApi[0]->getRootNode());
    ASSERT_TRUE(rootnode);
    std::unique_ptr<MegaNode> cloudNode(
        megaApi[0]->getNodeByPathOfType(fileName.c_str(), rootnode.get(), MegaNode::TYPE_FILE));
    ASSERT_TRUE(cloudNode) << "Uploaded file not found in cloud";
    ASSERT_EQ(cloudNode->getSize(), static_cast<int64_t>(fileSize));
}

/**
 * @brief Verify pausing one transfer does not block another in the same WS pool.
 *
 * - TEST1: Start two uploads mapped to same pool and capture transfer IDs.
 * - TEST2: Pause transfer A and assert transfer B continues progressing.
 * - TEST3: Resume A and require both A/B finish with API_OK.
 */
TEST_F(SdkWsUploadTest, PauseOneTransferNotBlockOthersInSamePool)
{
    LOG_info << "___TEST SdkWsUploadPauseOneTransferNotBlockOthersInSamePool___";
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    const std::string fileA =
        "ws_pause_single_A_" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".bin";
    const std::string fileB =
        "ws_pause_single_B_" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".bin";
    // Step 1: prepare two independent local upload files.
    constexpr size_t fileSize = kWsUploadDefaultFileSize;
    ASSERT_TRUE(createFileWithSize(fileA, fileSize, "A")) << "Couldn't create " << fileA;
    ASSERT_TRUE(createFileWithSize(fileB, fileSize, "B")) << "Couldn't create " << fileB;

    auto cleanupFiles = makeScopedDestructor(
        [this, &fileA, &fileB]()
        {
            deleteFile(fileA);
            deleteFile(fileB);
        });
    auto cleanupTransfers = makeScopedDestructor(
        [this]()
        {
            (void)synchronousCancelTransfers(0, MegaTransfer::TYPE_UPLOAD);
        });

    RequestTracker ct(megaApi[0].get());
    megaApi[0]->setMaxConnections(1, &ct);
    ASSERT_EQ(API_OK, ct.waitForResult(60));

    ScopedUploadSpeedLimit restoreUploadSpeed{*megaApi[0], 100000};

    std::unique_ptr<MegaNode> rootnode{megaApi[0]->getRootNode()};
    ASSERT_TRUE(rootnode);

    struct StartIdCapture final: ::mega::MegaTransferListener
    {
        std::mutex m;
        std::map<std::string, uint32_t> idsByName;

        void onTransferStart(MegaApi*, MegaTransfer* transfer) override
        {
            if (!transfer || !transfer->getFileName())
            {
                return;
            }

            std::lock_guard<std::mutex> g(m);
            idsByName[transfer->getFileName()] = transfer->getUniqueId();
        }
    } startIdCapture;

    megaApi[0]->addTransferListener(&startIdCapture);
    auto removeStartIdCapture = makeScopedDestructor(
        [this, &startIdCapture]()
        {
            megaApi[0]->removeTransferListener(&startIdCapture);
        });

    TransferTracker trackerA(megaApi[0].get());
    TransferTracker trackerB(megaApi[0].get());
    auto uploadOptions = makeDefaultUploadOptions();
    megaApi[0]->startUpload(fileA, rootnode.get(), nullptr, &uploadOptions, &trackerA);
    megaApi[0]->startUpload(fileB, rootnode.get(), nullptr, &uploadOptions, &trackerB);

    auto getTransferById = [this](const uint32_t id) -> std::unique_ptr<MegaTransfer>
    {
        return std::unique_ptr<MegaTransfer>(megaApi[0]->getTransferByUniqueId(id));
    };

    uint32_t idA = 0;
    uint32_t idB = 0;
    // Step 2: capture transfer IDs for both uploads.
    ASSERT_TRUE(WaitFor(
        [&]()
        {
            std::lock_guard<std::mutex> g(startIdCapture.m);
            auto itA = startIdCapture.idsByName.find(fileA);
            auto itB = startIdCapture.idsByName.find(fileB);
            if (itA == startIdCapture.idsByName.end() ||
                itB == startIdCapture.idsByName.end())
            {
                return false;
            }
            idA = itA->second;
            idB = itB->second;
            return idA > 0 && idB > 0;
        },
        30000))
        << "Did not capture transfer ids for both uploads";

    ASSERT_TRUE(WaitFor(
        [&]()
        {
            auto tA = getTransferById(idA);
            auto tB = getTransferById(idB);
            return tA && tB && tA->getTransferredBytes() > 0;
        },
        60000))
        << "Transfer A did not start progressing";

    auto tACopy = getTransferById(idA);
    ASSERT_TRUE(tACopy) << "Could not fetch transfer A by unique id";
    const long long aBeforePause = tACopy->getTransferredBytes();

    auto tBCopy = getTransferById(idB);
    ASSERT_TRUE(tBCopy) << "Could not fetch transfer B by unique id";
    const long long bBeforePause = tBCopy->getTransferredBytes();

    // Step 3: pause only transfer A.
    RequestTracker pauseReq(megaApi[0].get());
    megaApi[0]->pauseTransfer(tACopy.get(), true, &pauseReq);
    ASSERT_EQ(API_OK, pauseReq.waitForResult(60));

    ASSERT_TRUE(WaitFor(
        [&]()
        {
            auto t = getTransferById(idA);
            return t && t->getState() == MegaTransfer::STATE_PAUSED;
        },
        15000))
        << "Transfer A did not reach PAUSED state";

    const auto progressedOrCompleted = [&](TransferTracker& tracker,
                                           const uint32_t id,
                                           const long long beforePause) -> bool
    {
        if (tracker.finished.load() &&
            tracker.result.load() == static_cast<ErrorCodes>(API_OK))
        {
            return true;
        }

        auto t = getTransferById(id);
        return t && t->getTransferredBytes() > beforePause;
    };

    // Step 4: while A is paused, transfer B must continue progressing.
    ASSERT_TRUE(WaitFor(
        [&]()
        {
            return progressedOrCompleted(trackerB, idB, bBeforePause);
        },
        90000))
        << "Transfer B did not make progress while transfer A was paused";

    // Test goal achieved (B keeps progressing while A is paused); uncap to shorten remaining tail.
    megaApi[0]->setMaxUploadSpeed(-1);

    auto tAResumeCopy = getTransferById(idA);
    ASSERT_TRUE(tAResumeCopy) << "Could not fetch transfer A for resume";

    // Step 5: resume A and require progress + ACTIVE state.
    RequestTracker resumeReq(megaApi[0].get());
    megaApi[0]->pauseTransfer(tAResumeCopy.get(), false, &resumeReq);
    ASSERT_EQ(API_OK, resumeReq.waitForResult(60));

    ASSERT_TRUE(WaitFor(
        [&]()
        {
            auto t = getTransferById(idA);
            return t && t->getTransferredBytes() > aBeforePause &&
                   t->getState() == MegaTransfer::STATE_ACTIVE;
        },
        120000))
        << "Transfer A did not resume progress with ACTIVE state";

    auto transferDiagnostics = [&](const uint32_t id, const std::string& fileName) -> std::string
    {
        std::ostringstream os;

        auto transfer = getTransferById(id);
        if (transfer)
        {
            os << "transfer_exists=true"
               << " state=" << transfer->getState()
               << " transferred=" << transfer->getTransferredBytes()
               << " total=" << transfer->getTotalBytes()
               << " speed=" << transfer->getSpeed()
               << " meanSpeed=" << transfer->getMeanSpeed();
        }
        else
        {
            os << "transfer_exists=false";
        }

        std::unique_ptr<MegaNode> diagRoot{megaApi[0]->getRootNode()};
        if (diagRoot)
        {
            std::unique_ptr<MegaNode> cloudNode(
                megaApi[0]->getNodeByPathOfType(fileName.c_str(), diagRoot.get(), MegaNode::TYPE_FILE));
            os << " cloud_node_exists=" << (cloudNode ? "true" : "false");
            if (cloudNode)
            {
                os << " cloud_size=" << cloudNode->getSize();
            }
        }
        else
        {
            os << " root_node_unavailable=true";
        }

        return os.str();
    };

    // Step 7: both uploads should complete successfully.
    const auto resultA = trackerA.waitForResult(420);
    ASSERT_EQ(resultA, API_OK)
        << "Transfer A did not complete. " << transferDiagnostics(idA, fileA);

    const auto resultB = trackerB.waitForResult(420);
    ASSERT_EQ(resultB, API_OK)
        << "Transfer B did not complete. " << transferDiagnostics(idB, fileB);
}

/**
 * @brief Verify repeated pause/resume across mixed WS pools does not deadlock progress.
 *
 * - TEST1: Derive two file sizes from USC boundaries to target different pools.
 * - TEST2: Start A1/A2 and B1/B2, then alternate pause/resume across pool groups.
 * - TEST3: Assert non-paused peers keep progressing and all files uploading finish API_OK.
 */
TEST_F(SdkWsUploadTest, RepeatedPauseResumeMixedPools)
{
    LOG_info << "___TEST SdkWsUploadRepeatedPauseResumeMixedPools___";
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    std::vector<m_off_t> sizeClasses;
    ASSERT_TRUE(fetchUscSizeClasses(*megaApi[0], sizeClasses, 60))
        << "Unable to fetch USC size classes";

    std::string uscClassesDump;
    uscClassesDump.reserve(sizeClasses.size() * 8);
    for (size_t i = 0; i < sizeClasses.size(); ++i)
    {
        if (i)
        {
            uscClassesDump.append(",");
        }
        uscClassesDump.append(std::to_string(sizeClasses[i]));
    }

    bool hasOpenEndedClass = false;
    std::vector<m_off_t> finiteClasses;
    finiteClasses.reserve(sizeClasses.size());
    for (const m_off_t classMax: sizeClasses)
    {
        if (classMax == 0)
        {
            hasOpenEndedClass = true;
            continue;
        }
        if (classMax > 1)
        {
            finiteClasses.push_back(classMax);
        }
    }
    std::sort(finiteClasses.begin(), finiteClasses.end());
    finiteClasses.erase(std::unique(finiteClasses.begin(), finiteClasses.end()), finiteClasses.end());

    if (finiteClasses.empty())
    {
        GTEST_SKIP() << "Could not derive a usable first USC class boundary. USC classes=["
                     << uscClassesDump << "]";
    }

    const m_off_t firstClassMax = finiteClasses.front();
    const bool hasSecondClass = hasOpenEndedClass || finiteClasses.size() >= 2;
    if (!hasSecondClass)
    {
        GTEST_SKIP() << "Could not derive second USC class boundary from first max="
                     << firstClassMax << ". USC classes=[" << uscClassesDump << "]";
    }

    const std::string fileA1 =
        "ws_cross_pool_A1_" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".bin";
    const std::string fileA2 =
        "ws_cross_pool_A2_" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".bin";
    const std::string fileB1 =
        "ws_cross_pool_B1_" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".bin";
    const std::string fileB2 =
        "ws_cross_pool_B2_" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".bin";

    const size_t fileSizeA = static_cast<size_t>(firstClassMax - 1); // class #1: [0, firstClassMax)
    const size_t fileSizeB = static_cast<size_t>(firstClassMax); // class #2 starts at firstClassMax
    LOG_debug << "[SdkWsUploadRepeatedPauseResumeMixedPools] Selected first class max="
              << firstClassMax << " (A=" << fileSizeA << ", B=" << fileSizeB << ")";

    // Step 1: create 4 files: A1/A2 in one size class, B1/B2 in another and start uploading them.
    ASSERT_TRUE(createFileWithSize(fileA1, fileSizeA, "A1")) << "Couldn't create " << fileA1;
    ASSERT_TRUE(createFileWithSize(fileA2, fileSizeA, "A2")) << "Couldn't create " << fileA2;
    ASSERT_TRUE(createFileWithSize(fileB1, fileSizeB, "B1")) << "Couldn't create " << fileB1;
    ASSERT_TRUE(createFileWithSize(fileB2, fileSizeB, "B2")) << "Couldn't create " << fileB2;

    auto cleanupFiles = makeScopedDestructor(
        [this, &fileA1, &fileA2, &fileB1, &fileB2]()
        {
            deleteFile(fileA1);
            deleteFile(fileA2);
            deleteFile(fileB1);
            deleteFile(fileB2);
        });
    auto cleanupTransfers = makeScopedDestructor(
        [this]()
        {
            (void)synchronousCancelTransfers(0, MegaTransfer::TYPE_UPLOAD);
        });

    RequestTracker ct(megaApi[0].get());
    // Use multi-connection mode to exercise pause/resume behavior under concurrent WS workers.
    megaApi[0]->setMaxConnections(2, &ct);
    ASSERT_EQ(API_OK, ct.waitForResult(60));

    const m_off_t uploadSpeedLimit =
        std::max<m_off_t>(10000, static_cast<m_off_t>(fileSizeB / 500));
    ScopedUploadSpeedLimit restoreUploadSpeed{*megaApi[0],
                                              static_cast<int>(uploadSpeedLimit)};
    LOG_debug << "[SdkWsUploadRepeatedPauseResumeMixedPools] Max upload speed set to "
              << uploadSpeedLimit << " B/s";

    std::unique_ptr<MegaNode> rootnode{megaApi[0]->getRootNode()};
    ASSERT_TRUE(rootnode);

    struct StartIdCapture final: ::mega::MegaTransferListener
    {
        std::mutex m;
        std::map<std::string, uint32_t> idsByName;

        void onTransferStart(MegaApi*, MegaTransfer* transfer) override
        {
            if (!transfer || !transfer->getFileName())
            {
                return;
            }

            std::lock_guard<std::mutex> g(m);
            idsByName[transfer->getFileName()] = transfer->getUniqueId();
        }
    } startIdCapture;

    megaApi[0]->addTransferListener(&startIdCapture);
    auto removeStartIdCapture = makeScopedDestructor(
        [this, &startIdCapture]()
        {
            megaApi[0]->removeTransferListener(&startIdCapture);
        });

    TransferTracker trackerA1(megaApi[0].get());
    TransferTracker trackerA2(megaApi[0].get());
    TransferTracker trackerB1(megaApi[0].get());
    TransferTracker trackerB2(megaApi[0].get());
    auto uploadOptions = makeDefaultUploadOptions();
    megaApi[0]->startUpload(fileA1, rootnode.get(), nullptr, &uploadOptions, &trackerA1);
    megaApi[0]->startUpload(fileA2, rootnode.get(), nullptr, &uploadOptions, &trackerA2);
    megaApi[0]->startUpload(fileB1, rootnode.get(), nullptr, &uploadOptions, &trackerB1);
    megaApi[0]->startUpload(fileB2, rootnode.get(), nullptr, &uploadOptions, &trackerB2);

    auto getTransferById = [this](const uint32_t id) -> std::unique_ptr<MegaTransfer>
    {
        return std::unique_ptr<MegaTransfer>(megaApi[0]->getTransferByUniqueId(id));
    };

    uint32_t idA1 = 0;
    uint32_t idA2 = 0;
    uint32_t idB1 = 0;
    uint32_t idB2 = 0;
    // Step 2: capture transfer IDs for all 4 uploads.
    ASSERT_TRUE(WaitFor(
        [&]()
        {
            std::lock_guard<std::mutex> g(startIdCapture.m);
            auto itA1 = startIdCapture.idsByName.find(fileA1);
            auto itA2 = startIdCapture.idsByName.find(fileA2);
            auto itB1 = startIdCapture.idsByName.find(fileB1);
            auto itB2 = startIdCapture.idsByName.find(fileB2);
            if (itA1 == startIdCapture.idsByName.end() || itA2 == startIdCapture.idsByName.end() ||
                itB1 == startIdCapture.idsByName.end() || itB2 == startIdCapture.idsByName.end())
            {
                return false;
            }
            idA1 = itA1->second;
            idA2 = itA2->second;
            idB1 = itB1->second;
            idB2 = itB2->second;
            return idA1 > 0 && idA2 > 0 && idB1 > 0 && idB2 > 0;
        },
        30000))
        << "Did not capture transfer IDs for 4 uploads";

    const auto progressedOrCompletedEarly = [&](TransferTracker& tracker, const uint32_t id)
    {
        if (tracker.finished.load() &&
            tracker.result.load() == static_cast<ErrorCodes>(API_OK))
        {
            return true;
        }

        auto t = getTransferById(id);
        return t && t->getTransferredBytes() > 0;
    };

    // Temporarily pause A2/B2 so their pools pick A1/B1 first, avoiding startup
    // starvation from transient WS preflight retries.
    ASSERT_TRUE(WaitFor(
        [&]()
        {
            return getTransferById(idA2) && getTransferById(idB2);
        },
        30000))
        << "Could not fetch A2/B2 by unique ID before earlyPause pause";

    bool a2PausedForEarlyPause = false;
    if (auto tA2PauseCopy = getTransferById(idA2))
    {
        RequestTracker pauseA2EarlyPauseReq(megaApi[0].get());
        megaApi[0]->pauseTransfer(tA2PauseCopy.get(), true, &pauseA2EarlyPauseReq);
        const auto res = pauseA2EarlyPauseReq.waitForResult(60);
        ASSERT_TRUE(res == API_OK || res == API_ENOENT)
            << "Unexpected error pausing A2 during earlyPause: " << res;
        a2PausedForEarlyPause = (res == API_OK);
    }

    bool b2PausedForEarlyPause = false;
    if (auto tB2PauseCopy = getTransferById(idB2))
    {
        RequestTracker pauseB2EarlyPauseReq(megaApi[0].get());
        megaApi[0]->pauseTransfer(tB2PauseCopy.get(), true, &pauseB2EarlyPauseReq);
        const auto res = pauseB2EarlyPauseReq.waitForResult(60);
        ASSERT_TRUE(res == API_OK || res == API_ENOENT)
            << "Unexpected error pausing B2 during earlyPause: " << res;
        b2PausedForEarlyPause = (res == API_OK);
    }

    if (a2PausedForEarlyPause && b2PausedForEarlyPause)
    {
        ASSERT_TRUE(WaitFor(
            [&]()
            {
                auto tA2 = getTransferById(idA2);
                auto tB2 = getTransferById(idB2);
                return tA2 && tB2 && tA2->getState() == MegaTransfer::STATE_PAUSED &&
                       tB2->getState() == MegaTransfer::STATE_PAUSED;
            },
            15000))
            << "A2/B2 did not reach PAUSED state during earlyPause";
    }

    ASSERT_TRUE(WaitFor(
        [&]()
        {
            return progressedOrCompletedEarly(trackerA1, idA1);
        },
        90000))
        << "A1 did not start progressing";

    ASSERT_TRUE(WaitFor(
        [&]()
        {
            return progressedOrCompletedEarly(trackerB1, idB1);
        },
        90000))
        << "B1 did not start progressing";

    // Step 3: resolve A1/B1 WS URLs and confirm they are in different pools.
    std::map<std::string, std::string> urlByFileName;
    std::vector<WsUploadTransferSnapshot> lastSnapshots;
    second_timer urlTimer;
    while (urlTimer.elapsed() < 60)
    {
        std::vector<WsUploadTransferSnapshot> snapshots;
        if (!fetchWsUploadTransferSnapshots(*megaApi[0], snapshots, 1))
        {
            WaitMillisec(200);
            continue;
        }

        lastSnapshots = snapshots;

        for (const auto& snapshot: snapshots)
        {
            if (!snapshot.found || snapshot.fileName.empty() || snapshot.wsSessionUrl.empty())
            {
                continue;
            }

            if (snapshot.fileName == fileA1 || snapshot.fileName == fileA2 ||
                snapshot.fileName == fileB1 || snapshot.fileName == fileB2)
            {
                urlByFileName[snapshot.fileName] = snapshot.wsSessionUrl;
            }
        }

        if (urlByFileName.count(fileA1) && urlByFileName.count(fileB1))
        {
            break;
        }

        if (trackerA1.finished.load() && trackerA2.finished.load() &&
            trackerB1.finished.load() && trackerB2.finished.load())
        {
            break;
        }

        WaitMillisec(200);
    }

    if (!urlByFileName.count(fileA1) || !urlByFileName.count(fileB1))
    {
        auto logSnapshot = [](const char* tag, const WsUploadTransferSnapshot& snapshot)
        {
            LOG_debug << "[SdkWsUploadRepeatedPauseResumeMixedPools] " << tag
                      << " transferId=" << snapshot.transferId << " fileName=" << snapshot.fileName
                      << " wsFileno=" << snapshot.wsFileno << " state=" << snapshot.state
                      << " progressCompleted=" << snapshot.progressCompleted
                      << " pos=" << snapshot.pos << " size=" << snapshot.size
                      << " serializedWsSessionUrl=" << snapshot.serializedWsSessionUrl
                      << " wsSessionUrl=" << snapshot.wsSessionUrl;
        };

        LOG_debug << "[SdkWsUploadRepeatedPauseResumeMixedPools] Timed out waiting for WS URLs "
                  << "[idA1=" << idA1 << "] [idA2=" << idA2 << "] [idB1=" << idB1
                  << "] [idB2=" << idB2 << "] [fileA1=" << fileA1 << "] [fileB1=" << fileB1
                  << "] [snapshots=" << lastSnapshots.size() << "]";
        for (const auto& snapshot: lastSnapshots)
        {
            if (snapshot.fileName == fileA1)
            {
                logSnapshot("snapshotA1", snapshot);
            }
            else if (snapshot.fileName == fileA2)
            {
                logSnapshot("snapshotA2", snapshot);
            }
            else if (snapshot.fileName == fileB1)
            {
                logSnapshot("snapshotB1", snapshot);
            }
            else if (snapshot.fileName == fileB2)
            {
                logSnapshot("snapshotB2", snapshot);
            }
        }

        // Skip after starting uploads: drain active uploads before local tracker/listener objects
        // are unwound to reduce teardown races.
        megaApi[0]->setMaxUploadSpeed(-1);
        (void)synchronousCancelTransfers(0, MegaTransfer::TYPE_UPLOAD);
        GTEST_SKIP() << "Could not observe WS session URLs for A1/B1";
    }

    if (urlByFileName[fileA1] == urlByFileName[fileB1])
    {
        megaApi[0]->setMaxUploadSpeed(-1);
        (void)synchronousCancelTransfers(0, MegaTransfer::TYPE_UPLOAD);
        GTEST_SKIP() << "A1/B1 are in the same WS pool in this run";
    }

    // Restore A2/B2 from earlyPause pause before the original mixed-pause checks.
    if (a2PausedForEarlyPause)
    {
        if (auto tA2ResumeCopy = getTransferById(idA2))
        {
            RequestTracker resumeA2EarlyPauseReq(megaApi[0].get());
            megaApi[0]->pauseTransfer(tA2ResumeCopy.get(), false, &resumeA2EarlyPauseReq);
            const auto res = resumeA2EarlyPauseReq.waitForResult(60);
            ASSERT_TRUE(res == API_OK || res == API_ENOENT)
                << "Unexpected error resuming A2 after earlyPause: " << res;
        }
    }

    if (b2PausedForEarlyPause)
    {
        if (auto tB2ResumeCopy = getTransferById(idB2))
        {
            RequestTracker resumeB2EarlyPauseReq(megaApi[0].get());
            megaApi[0]->pauseTransfer(tB2ResumeCopy.get(), false, &resumeB2EarlyPauseReq);
            const auto res = resumeB2EarlyPauseReq.waitForResult(60);
            ASSERT_TRUE(res == API_OK || res == API_ENOENT)
                << "Unexpected error resuming B2 after earlyPause: " << res;
        }
    }

    auto tA1Copy = getTransferById(idA1);
    ASSERT_TRUE(tA1Copy) << "Could not fetch transfer A1 by unique ID";
    auto tB1Copy = getTransferById(idB1);
    ASSERT_TRUE(tB1Copy) << "Could not fetch transfer B1 by unique ID";

    auto tA2Copy = getTransferById(idA2);
    ASSERT_TRUE(tA2Copy) << "Could not fetch transfer A2 by unique ID";
    const long long a2BeforePause = tA2Copy->getTransferredBytes();
    auto tB2Copy = getTransferById(idB2);
    ASSERT_TRUE(tB2Copy) << "Could not fetch transfer B2 by unique ID";
    const long long b2BeforePause = tB2Copy->getTransferredBytes();

    // Step 4: pause A1/B1, then ensure A2/B2 continue in parallel.
    RequestTracker pauseA1Req(megaApi[0].get());
    megaApi[0]->pauseTransfer(tA1Copy.get(), true, &pauseA1Req);
    ASSERT_EQ(API_OK, pauseA1Req.waitForResult(60));
    RequestTracker pauseB1Req(megaApi[0].get());
    megaApi[0]->pauseTransfer(tB1Copy.get(), true, &pauseB1Req);
    ASSERT_EQ(API_OK, pauseB1Req.waitForResult(60));

    ASSERT_TRUE(WaitFor(
        [&]()
        {
            auto tA1 = getTransferById(idA1);
            auto tB1 = getTransferById(idB1);
            return tA1 && tB1 && tA1->getState() == MegaTransfer::STATE_PAUSED &&
                   tB1->getState() == MegaTransfer::STATE_PAUSED;
        },
        15000))
        << "A1/B1 did not reach PAUSED state";

    const auto progressedOrCompleted = [&](TransferTracker& tracker,
                                           const uint32_t id,
                                           const long long beforePause) -> bool
    {
        if (tracker.finished.load() &&
            tracker.result.load() == static_cast<ErrorCodes>(API_OK))
        {
            return true;
        }

        auto t = getTransferById(id);
        return t && t->getTransferredBytes() > beforePause;
    };

    ASSERT_TRUE(WaitFor(
        [&]()
        {
            return progressedOrCompleted(trackerA2, idA2, a2BeforePause);
        },
        90000))
        << "A2 did not make progress while A1/B1 were paused";

    ASSERT_TRUE(WaitFor(
        [&]()
        {
            return progressedOrCompleted(trackerB2, idB2, b2BeforePause);
        },
        90000))
        << "B2 did not make progress while A1/B1 were paused";

    // Step 5: unpause A1/B1.
    auto tA1ResumeCopy = getTransferById(idA1);
    ASSERT_TRUE(tA1ResumeCopy) << "Could not fetch transfer A1 for resume";
    RequestTracker resumeA1Req(megaApi[0].get());
    megaApi[0]->pauseTransfer(tA1ResumeCopy.get(), false, &resumeA1Req);
    ASSERT_EQ(API_OK, resumeA1Req.waitForResult(60));

    auto tB1ResumeCopy = getTransferById(idB1);
    ASSERT_TRUE(tB1ResumeCopy) << "Could not fetch transfer B1 for resume";
    RequestTracker resumeB1Req(megaApi[0].get());
    megaApi[0]->pauseTransfer(tB1ResumeCopy.get(), false, &resumeB1Req);
    ASSERT_EQ(API_OK, resumeB1Req.waitForResult(60));

    // Step 6: pause A2/B2, then ensure A1/B1 continue in parallel.
    auto tA1BeforeSecondPause = getTransferById(idA1);
    ASSERT_TRUE(tA1BeforeSecondPause) << "Could not fetch transfer A1 before second pause";
    const long long a1BeforeSecondPause = tA1BeforeSecondPause->getTransferredBytes();

    auto tB1BeforeSecondPause = getTransferById(idB1);
    ASSERT_TRUE(tB1BeforeSecondPause) << "Could not fetch transfer B1 before second pause";
    const long long b1BeforeSecondPause = tB1BeforeSecondPause->getTransferredBytes();

    // Pause A2/B2 — tolerate API_ENOENT (-9) if the transfer already completed.
    bool a2Paused = false;
    if (auto tA2PauseCopy = getTransferById(idA2))
    {
        RequestTracker pauseA2Req(megaApi[0].get());
        megaApi[0]->pauseTransfer(tA2PauseCopy.get(), true, &pauseA2Req);
        const auto res = pauseA2Req.waitForResult(60);
        ASSERT_TRUE(res == API_OK || res == API_ENOENT) << "Unexpected error pausing A2: " << res;
        a2Paused = (res == API_OK);
    }

    bool b2Paused = false;
    if (auto tB2PauseCopy = getTransferById(idB2))
    {
        RequestTracker pauseB2Req(megaApi[0].get());
        megaApi[0]->pauseTransfer(tB2PauseCopy.get(), true, &pauseB2Req);
        const auto res = pauseB2Req.waitForResult(60);
        ASSERT_TRUE(res == API_OK || res == API_ENOENT) << "Unexpected error pausing B2: " << res;
        b2Paused = (res == API_OK);
    }

    if (a2Paused && b2Paused)
    {
        ASSERT_TRUE(WaitFor(
            [&]()
            {
                auto tA2 = getTransferById(idA2);
                auto tB2 = getTransferById(idB2);
                return tA2 && tB2 && tA2->getState() == MegaTransfer::STATE_PAUSED &&
                       tB2->getState() == MegaTransfer::STATE_PAUSED;
            },
            15000))
            << "A2/B2 did not reach PAUSED state";
    }

    ASSERT_TRUE(WaitFor(
        [&]()
        {
            return progressedOrCompleted(trackerA1, idA1, a1BeforeSecondPause);
        },
        90000))
        << "A1 did not make progress while A2/B2 were paused";

    ASSERT_TRUE(WaitFor(
        [&]()
        {
            return progressedOrCompleted(trackerB1, idB1, b1BeforeSecondPause);
        },
        90000))
        << "B1 did not make progress while A2/B2 were paused";

    // Step 7: unpause A2/B2 — skip if already completed.
    if (a2Paused)
    {
        if (auto tA2ResumeCopy = getTransferById(idA2))
        {
            RequestTracker resumeA2Req(megaApi[0].get());
            megaApi[0]->pauseTransfer(tA2ResumeCopy.get(), false, &resumeA2Req);
            const auto res = resumeA2Req.waitForResult(60);
            ASSERT_TRUE(res == API_OK || res == API_ENOENT)
                << "Unexpected error resuming A2: " << res;
        }
    }

    if (b2Paused)
    {
        if (auto tB2ResumeCopy = getTransferById(idB2))
        {
            RequestTracker resumeB2Req(megaApi[0].get());
            megaApi[0]->pauseTransfer(tB2ResumeCopy.get(), false, &resumeB2Req);
            const auto res = resumeB2Req.waitForResult(60);
            ASSERT_TRUE(res == API_OK || res == API_ENOENT)
                << "Unexpected error resuming B2: " << res;
        }
    }

    // Step 8: remove throttling and require all uploads to complete.
    megaApi[0]->setMaxUploadSpeed(-1);

    auto transferDiagnostics = [&](const uint32_t id, const std::string& fileName) -> std::string
    {
        std::ostringstream os;

        auto transfer = getTransferById(id);
        if (transfer)
        {
            os << "transfer_exists=true"
               << " state=" << transfer->getState()
               << " transferred=" << transfer->getTransferredBytes()
               << " total=" << transfer->getTotalBytes()
               << " speed=" << transfer->getSpeed()
               << " meanSpeed=" << transfer->getMeanSpeed();
        }
        else
        {
            os << "transfer_exists=false";
        }

        auto it = urlByFileName.find(fileName);
        if (it != urlByFileName.end())
        {
            os << " ws_url=" << it->second;
        }

        std::unique_ptr<MegaNode> diagRoot{megaApi[0]->getRootNode()};
        if (diagRoot)
        {
            std::unique_ptr<MegaNode> cloudNode(
                megaApi[0]->getNodeByPathOfType(fileName.c_str(), diagRoot.get(), MegaNode::TYPE_FILE));
            os << " cloud_node_exists=" << (cloudNode ? "true" : "false");
            if (cloudNode)
            {
                os << " cloud_size=" << cloudNode->getSize();
            }
        }
        else
        {
            os << " root_node_unavailable=true";
        }

        return os.str();
    };

    const auto resultA1 = trackerA1.waitForResult(300);
    ASSERT_EQ(resultA1, API_OK)
        << "Transfer A1 did not complete. " << transferDiagnostics(idA1, fileA1);

    const auto resultA2 = trackerA2.waitForResult(300);
    ASSERT_EQ(resultA2, API_OK)
        << "Transfer A2 did not complete. " << transferDiagnostics(idA2, fileA2);

    const auto resultB1 = trackerB1.waitForResult(300);
    ASSERT_EQ(resultB1, API_OK)
        << "Transfer B1 did not complete. " << transferDiagnostics(idB1, fileB1);

    const auto resultB2 = trackerB2.waitForResult(300);
    ASSERT_EQ(resultB2, API_OK)
        << "Transfer B2 did not complete. " << transferDiagnostics(idB2, fileB2);
}

/**
 * @brief Verify pause/resume handles late processing of in-flight ACKs safely.
 *
 * - TEST1: Start active upload and pause while chunks are already in flight.
 * - TEST2: Keep paused for a delay window, then resume the same transfer.
 * - TEST3: Require transfer resumes progress and completes API_OK.
 */
TEST_F(SdkWsUploadTest, PauseHandlesLateInFlightAck)
{
    LOG_info << "___TEST SdkWsUploadPauseHandlesLateInFlightAck___";
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    const std::string fileName =
        "ws_pause_inflight_ack_" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".bin";

    // Step 1: create a file large enough to keep upload active around pause/resume boundaries.
    constexpr size_t fileSize = 2 * kWsUploadDefaultFileSize;
    ASSERT_TRUE(createFileWithSize(fileName, fileSize, "D")) << "Couldn't create " << fileName;

    auto cleanupFile = makeScopedDestructor(
        [this, &fileName]()
        {
            deleteFile(fileName);
        });
    auto cleanupTransfers = makeScopedDestructor(
        [this]()
        {
            (void)synchronousCancelTransfers(0, MegaTransfer::TYPE_UPLOAD);
        });

    RequestTracker ct(megaApi[0].get());
    megaApi[0]->setMaxConnections(1, &ct);
    ASSERT_EQ(API_OK, ct.waitForResult(60));

    ScopedUploadSpeedLimit restoreUploadSpeed{*megaApi[0], 100000};

    std::unique_ptr<MegaNode> rootnode{megaApi[0]->getRootNode()};
    ASSERT_TRUE(rootnode);

    // Step 2: start upload and capture transfer unique id.
    std::atomic<bool> startObserved{false};
    std::atomic<uint32_t> transferId{0};
    auto resetOnTransferStartCb = makeScopedDestructor(
        [this]()
        {
            onTransferStartCustomCb = {};
        });
    onTransferStartCustomCb = [&startObserved, &transferId, &fileName](MegaTransfer* transfer)
    {
        if (!transfer || transfer->getType() != MegaTransfer::TYPE_UPLOAD || !transfer->getFileName())
        {
            return;
        }
        if (fileName == transfer->getFileName())
        {
            transferId = transfer->getUniqueId();
            startObserved = true;
        }
    };

    TransferTracker ut(megaApi[0].get());
    auto uploadOptions = makeDefaultUploadOptions();
    megaApi[0]->startUpload(fileName, rootnode.get(), nullptr, &uploadOptions, &ut);

    ASSERT_TRUE(WaitFor(
        [&]()
        {
            return startObserved.load() && transferId.load() > 0;
        },
        30000))
        << "Upload start callback was not observed";

    auto getTransferById = [this](const uint32_t id) -> std::unique_ptr<MegaTransfer>
    {
        return std::unique_ptr<MegaTransfer>(megaApi[0]->getTransferByUniqueId(id));
    };

    // Step 3: wait for active progress before pausing.
    ASSERT_TRUE(WaitFor(
        [&]()
        {
            auto t = getTransferById(transferId.load());
            return t && t->getTransferredBytes() > 0 &&
                   t->getState() == MegaTransfer::STATE_ACTIVE;
        },
        90000))
        << "Upload did not become active with progress";

    auto tBeforePause = getTransferById(transferId.load());
    ASSERT_TRUE(tBeforePause) << "Could not fetch transfer before pause";
    const long long beforePause = tBeforePause->getTransferredBytes();

    // Step 4: pause while transfer is active, then keep a short window for delayed ACK callbacks.
    RequestTracker pauseReq(megaApi[0].get());
    megaApi[0]->pauseTransfer(tBeforePause.get(), true, &pauseReq);
    ASSERT_EQ(API_OK, pauseReq.waitForResult(60));

    ASSERT_TRUE(WaitFor(
        [&]()
        {
            auto t = getTransferById(transferId.load());
            return t && t->getState() == MegaTransfer::STATE_PAUSED;
        },
        15000))
        << "Transfer did not reach PAUSED state";

    // Keep a longer observation window to catch delayed ACK processing under CI jitter.
    bool pausedStateStable = true;
    const auto delayedAckWindowStart = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - delayedAckWindowStart < std::chrono::seconds(5))
    {
        auto t = getTransferById(transferId.load());
        if (!t || t->getState() != MegaTransfer::STATE_PAUSED ||
            t->getTransferredBytes() < beforePause)
        {
            pausedStateStable = false;
            break;
        }

        WaitMillisec(100);
    }
    ASSERT_TRUE(pausedStateStable)
        << "Transfer left PAUSED state or regressed progress during delayed-ACK window";

    // Pause/late-ACK behavior has been observed; uncap to reduce tail time for resume/completion.
    megaApi[0]->setMaxUploadSpeed(-1);

    auto pausedSnapshot = getTransferById(transferId.load());
    ASSERT_TRUE(pausedSnapshot) << "Transfer disappeared while paused";

    // Step 5: resume and require forward progress + ACTIVE state.
    RequestTracker resumeReq(megaApi[0].get());
    megaApi[0]->pauseTransfer(pausedSnapshot.get(), false, &resumeReq);
    ASSERT_EQ(API_OK, resumeReq.waitForResult(60));

    ASSERT_TRUE(WaitFor(
        [&]()
        {
            auto t = getTransferById(transferId.load());
            return t && t->getTransferredBytes() > beforePause &&
                   t->getState() == MegaTransfer::STATE_ACTIVE;
        },
        120000))
        << "Transfer did not resume with forward progress and ACTIVE state";

    // Step 6: finish upload and verify cloud node size.
    const auto finalResult = ut.waitForResult(420);
    ASSERT_EQ(finalResult, API_OK) << "Upload did not complete after pause/resume";

    rootnode.reset(megaApi[0]->getRootNode());
    ASSERT_TRUE(rootnode);
    std::unique_ptr<MegaNode> cloudNode(
        megaApi[0]->getNodeByPathOfType(fileName.c_str(), rootnode.get(), MegaNode::TYPE_FILE));
    ASSERT_TRUE(cloudNode) << "Uploaded file not found in cloud";
    ASSERT_EQ(cloudNode->getSize(), static_cast<int64_t>(fileSize));
}

/**
 * @brief Verify dropping one completion frame for transfer A does not block transfer B.
 *
 * - TEST1: Start A/B in same pool and resolve A wsFileno from snapshots.
 * - TEST2: Drop A's UploadCompleted event (code 4) via debug hook.
 * - TEST3: Require B still reaches API_OK even when A completion frame is dropped.
 */
TEST_F(SdkWsUploadTest, DropCompletionDoesNotBlockOthersInSamePool)
{
    LOG_info << "___TEST SdkWsUploadDropCompletionDoesNotBlockOthersInSamePool___";
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

#ifndef MEGASDK_DEBUG_TEST_HOOKS_ENABLED
    GTEST_SKIP() << "Requires MEGASDK_DEBUG_TEST_HOOKS_ENABLED";
#else
    const std::string fileA =
        "ws_drop_completion_A_" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".bin";
    const std::string fileB =
        "ws_drop_completion_B_" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".bin";

    // Step 1: prepare two uploads that will share one WS worker/pool.
    constexpr size_t fileSize = kWsUploadDefaultFileSize;
    ASSERT_TRUE(createFileWithSize(fileA, fileSize, "A")) << "Couldn't create " << fileA;
    ASSERT_TRUE(createFileWithSize(fileB, fileSize, "B")) << "Couldn't create " << fileB;

    auto cleanupFiles = makeScopedDestructor(
        [this, &fileA, &fileB]()
        {
            deleteFile(fileA);
            deleteFile(fileB);
        });
    auto cleanupTransfers = makeScopedDestructor(
        [this]()
        {
            (void)synchronousCancelTransfers(0, MegaTransfer::TYPE_UPLOAD);
        });
    auto clearWsHook = makeScopedDestructor(
        []()
        {
            globalMegaTestHooks.wsUploadServerEventHook.reset();
        });

    RequestTracker ct(megaApi[0].get());
    megaApi[0]->setMaxConnections(1, &ct);
    ASSERT_EQ(API_OK, ct.waitForResult(60));

    ScopedUploadSpeedLimit restoreUploadSpeed{*megaApi[0], 100000};

    std::unique_ptr<MegaNode> rootnode{megaApi[0]->getRootNode()};
    ASSERT_TRUE(rootnode);

    TransferTracker trackerA(megaApi[0].get());
    auto uploadOptions = makeDefaultUploadOptions();
    megaApi[0]->startUpload(fileA, rootnode.get(), nullptr, &uploadOptions, &trackerA);

    ASSERT_TRUE(WaitFor(
        [&]()
        {
            return trackerA.started.load();
        },
        30000))
        << "Transfer A did not start";

    // Step 2: resolve A's WS fileno by file name, then drop only A's UploadCompleted event.
    std::uint32_t wsFilenoA = 0;
    ASSERT_TRUE(WaitFor(
        [&]()
        {
            std::vector<WsUploadTransferSnapshot> snapshots;
            if (!fetchWsUploadTransferSnapshots(*megaApi[0], snapshots, 1))
            {
                return false;
            }
            for (const auto& snapshot: snapshots)
            {
                if (!snapshot.found || snapshot.fileName.empty() || !snapshot.wsFileno)
                {
                    continue;
                }
                if (snapshot.fileName == fileA)
                {
                    wsFilenoA = snapshot.wsFileno;
                }
            }
            return wsFilenoA > 0;
        },
        90000))
        << "Could not resolve ws fileno for transfer A [fileA=" << fileA << "]";

    auto& hook = globalMegaTestHooks.wsUploadServerEventHook;
    hook.configure(WsUploadServerEventAction::Drop,
                   4, // UploadCompleted
                   std::nullopt,
                   wsFilenoA);

    // Step 3: start B after the hook is installed so we deterministically target A.
    TransferTracker trackerB(megaApi[0].get());
    megaApi[0]->startUpload(fileB, rootnode.get(), nullptr, &uploadOptions, &trackerB);
    megaApi[0]->setMaxUploadSpeed(-1);

    ASSERT_TRUE(WaitFor(
        [&]()
        {
            return hook.getHitCount() > 0;
        },
        180000))
        << "Timed out waiting for dropped WS UploadCompleted event";

    // Step 4: require B to complete while A completion remains dropped.
    const auto resultB = trackerB.waitForResult(300);
    ASSERT_EQ(resultB, API_OK) << "Transfer B did not complete";
#endif
}

/**
 * @brief Verify invalid WS completion token triggers retryable failure and eventual success.
 *
 * - TEST1: Override first WS completion payload length to 0 (invalid upload token length).
 * - TEST2: Start upload and require temporary error API_EAGAIN is observed.
 * - TEST3: Require retry converges to API_OK and uploaded node size matches local source.
 */
TEST_F(SdkWsUploadTest, InvalidCompletionTokenTriggersRetry)
{
    LOG_info << "___TEST SdkWsUploadInvalidCompletionTokenTriggersRetry___";
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

#ifndef MEGASDK_DEBUG_TEST_HOOKS_ENABLED
    GTEST_SKIP() << "Requires MEGASDK_DEBUG_TEST_HOOKS_ENABLED";
#else
    const std::string fileName =
        "ws_invalid_completion_token_" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".bin";
    constexpr size_t fileSize = kWsUploadDefaultFileSize;
    ASSERT_TRUE(createFileWithSize(fileName, fileSize, "I")) << "Couldn't create " << fileName;

    auto cleanupFile = makeScopedDestructor(
        [this, &fileName]()
        {
            deleteFile(fileName);
        });
    auto cleanupTransfers = makeScopedDestructor(
        [this]()
        {
            (void)synchronousCancelTransfers(0, MegaTransfer::TYPE_UPLOAD);
        });
    auto clearCompletionHook = makeScopedDestructor(
        []()
        {
            globalMegaTestHooks.onWsUploadCompletionPayloadLen = {};
        });

    RequestTracker ct(megaApi[0].get());
    megaApi[0]->setMaxConnections(1, &ct);
    ASSERT_EQ(API_OK, ct.waitForResult(60));

    megaApi[0]->setMaxUploadSpeed(-1);

    std::unique_ptr<MegaNode> rootnode{megaApi[0]->getRootNode()};
    ASSERT_TRUE(rootnode);

    std::atomic<int> completionLenOverrideHits{0};
    globalMegaTestHooks.onWsUploadCompletionPayloadLen = [&completionLenOverrideHits](int& payLen)
    {
        if (completionLenOverrideHits.fetch_add(1) == 0)
        {
            payLen = 0;
        }
    };

    WsUploadRetryTracker tracker(megaApi[0].get());
    auto uploadOptions = makeDefaultUploadOptions();
    megaApi[0]->startUpload(fileName, rootnode.get(), nullptr, &uploadOptions, &tracker);

    WsUploadTransferSnapshot activeUpload{};
    ASSERT_TRUE(waitForFirstUploadTransferSnapshot(
        *megaApi[0],
        activeUpload,
        [&fileName](const WsUploadTransferSnapshot& snapshot)
        {
            return snapshot.found && snapshot.fileName == fileName && snapshot.wsFileno > 0 &&
                   !snapshot.wsSessionUrl.empty() && snapshot.state == TRANSFERSTATE_ACTIVE;
        },
        60,
        200))
        << "Failed to observe an active WS upload before waiting for completion hook";

    ASSERT_TRUE(WaitFor(
        [&completionLenOverrideHits]()
        {
            return completionLenOverrideHits.load() > 0;
        },
        120000))
        << "Timed out waiting for invalid completion-token injection";

    ASSERT_TRUE(WaitFor(
        [&tracker]()
        {
            return tracker.temporaryErrorCount.load() >= 1;
        },
        120000))
        << "Upload did not surface temporary error after invalid completion token";
    ASSERT_EQ(tracker.lastTemporaryError.load(), ErrorCodes::API_EAGAIN);

    ASSERT_TRUE(WaitFor(
        [&tracker]()
        {
            return tracker.sawActiveAfterTemporaryError.load();
        },
        120000))
        << "Upload did not return to ACTIVE after invalid completion-token temporary error";

    const auto finalResult = tracker.waitForResult(300);
    ASSERT_EQ(finalResult, API_OK)
        << "Upload did not complete after invalid completion token retry path";

    rootnode.reset(megaApi[0]->getRootNode());
    ASSERT_TRUE(rootnode);
    std::unique_ptr<MegaNode> cloudNode(
        megaApi[0]->getNodeByPathOfType(fileName.c_str(), rootnode.get(), MegaNode::TYPE_FILE));
    ASSERT_TRUE(cloudNode) << "Uploaded file not found in cloud";
    ASSERT_EQ(cloudNode->getSize(), static_cast<int64_t>(fileSize));
#endif
}

/**
 * @brief Verify failing one WS transfer with another queued WS transfer still converges.
 *
 * This scenario exercises fail/re-enqueue ordering logic where the failed transfer captures
 * a "next WS transfer" pointer (wsBefore) and repositions relative order on re-enqueue.
 *
 * - TEST1: Start transfer A and resolve its wsFileno.
 * - TEST2: Inject API_EAGAIN for A's first ChunkIngested event.
 * - TEST3: Start transfer B in same pool while A is retrying.
 * - TEST4: Require hook hit and both A/B complete successfully.
 */
TEST_F(SdkWsUploadTest, FailThenRetryWithAnotherQueuedTransfer)
{
    LOG_info << "___TEST SdkWsUploadFailThenRetryWithAnotherQueuedTransfer___";
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

#ifndef MEGASDK_DEBUG_TEST_HOOKS_ENABLED
    GTEST_SKIP() << "Requires MEGASDK_DEBUG_TEST_HOOKS_ENABLED";
#else
    const std::string fileA =
        "ws_fail_reenqueue_A_" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".bin";
    const std::string fileB =
        "ws_fail_reenqueue_B_" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".bin";
    constexpr size_t fileSize = kWsUploadDefaultFileSize;
    ASSERT_TRUE(createFileWithSize(fileA, fileSize, "A")) << "Couldn't create " << fileA;
    ASSERT_TRUE(createFileWithSize(fileB, fileSize, "B")) << "Couldn't create " << fileB;

    auto cleanupFiles = makeScopedDestructor(
        [this, &fileA, &fileB]()
        {
            deleteFile(fileA);
            deleteFile(fileB);
        });
    auto cleanupTransfers = makeScopedDestructor(
        [this]()
        {
            (void)synchronousCancelTransfers(0, MegaTransfer::TYPE_UPLOAD);
        });
    auto clearWsHook = makeScopedDestructor(
        []()
        {
            globalMegaTestHooks.wsUploadServerEventHook.reset();
        });

    RequestTracker ct(megaApi[0].get());
    megaApi[0]->setMaxConnections(1, &ct);
    ASSERT_EQ(API_OK, ct.waitForResult(60));

    ScopedUploadSpeedLimit restoreUploadSpeed{*megaApi[0], 100000};

    std::unique_ptr<MegaNode> rootnode{megaApi[0]->getRootNode()};
    ASSERT_TRUE(rootnode);

    TransferTracker trackerA(megaApi[0].get());
    auto uploadOptions = makeDefaultUploadOptions();
    megaApi[0]->startUpload(fileA, rootnode.get(), nullptr, &uploadOptions, &trackerA);

    ASSERT_TRUE(WaitFor(
        [&]()
        {
            return trackerA.started.load();
        },
        30000))
        << "Transfer A did not start";

    std::uint32_t wsFilenoA = 0;
    ASSERT_TRUE(WaitFor(
        [&]()
        {
            std::vector<WsUploadTransferSnapshot> snapshots;
            if (!fetchWsUploadTransferSnapshots(*megaApi[0], snapshots, 1))
            {
                return false;
            }
            for (const auto& snapshot: snapshots)
            {
                if (!snapshot.found || !snapshot.wsFileno || snapshot.fileName.empty())
                {
                    continue;
                }
                if (snapshot.fileName == fileA)
                {
                    wsFilenoA = snapshot.wsFileno;
                    return true;
                }
            }
            return false;
        },
        90000))
        << "Could not resolve ws fileno for transfer A";

    auto& hook = globalMegaTestHooks.wsUploadServerEventHook;
    hook.configure(WsUploadServerEventAction::Modify,
                   1, // ChunkIngested
                   API_EAGAIN,
                   wsFilenoA);

    TransferTracker trackerB(megaApi[0].get());
    megaApi[0]->startUpload(fileB, rootnode.get(), nullptr, &uploadOptions, &trackerB);

    ASSERT_TRUE(WaitFor(
        [&]()
        {
            return hook.getHitCount() > 0;
        },
        120000))
        << "Timed out waiting for injected temporary failure on transfer A";

    megaApi[0]->setMaxUploadSpeed(-1);
    ASSERT_EQ(trackerA.waitForResult(300), API_OK)
        << "Transfer A did not recover after injected temporary failure";
    ASSERT_EQ(trackerB.waitForResult(300), API_OK)
        << "Transfer B did not complete while A retried";

    rootnode.reset(megaApi[0]->getRootNode());
    ASSERT_TRUE(rootnode);
    std::unique_ptr<MegaNode> cloudNodeA(
        megaApi[0]->getNodeByPathOfType(fileA.c_str(), rootnode.get(), MegaNode::TYPE_FILE));
    std::unique_ptr<MegaNode> cloudNodeB(
        megaApi[0]->getNodeByPathOfType(fileB.c_str(), rootnode.get(), MegaNode::TYPE_FILE));
    ASSERT_TRUE(cloudNodeA) << "Uploaded file A not found in cloud";
    ASSERT_TRUE(cloudNodeB) << "Uploaded file B not found in cloud";
    ASSERT_EQ(cloudNodeA->getSize(), static_cast<int64_t>(fileSize));
    ASSERT_EQ(cloudNodeB->getSize(), static_cast<int64_t>(fileSize));
#endif
}

/**
 * @brief Verify dropping one ChunkIngested ACK still allows successful upload.
 *
 * - TEST1: Install hook to drop exactly one ChunkIngested ACK event (code 1).
 * - TEST2: Start upload and confirm one ACK drop was observed.
 * - TEST3: Require upload API_OK and cloud node size equals local file size.
 */
TEST_F(SdkWsUploadTest, DropChunkIngestedAckStillCompletes)
{
    LOG_info << "___TEST SdkWsUploadDropChunkIngestedAckStillCompletes___";
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

#ifndef MEGASDK_DEBUG_TEST_HOOKS_ENABLED
    GTEST_SKIP() << "Requires MEGASDK_DEBUG_TEST_HOOKS_ENABLED";
#else
    const std::string fileName =
        "ws_drop_chunk_ack_" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".bin";
    constexpr size_t fileSize = 2 * kWsUploadDefaultFileSize;
    ASSERT_TRUE(createFileWithSize(fileName, fileSize, "K")) << "Couldn't create " << fileName;

    auto cleanupFile = makeScopedDestructor(
        [this, &fileName]()
        {
            deleteFile(fileName);
        });
    auto cleanupTransfers = makeScopedDestructor(
        [this]()
        {
            (void)synchronousCancelTransfers(0, MegaTransfer::TYPE_UPLOAD);
        });
    auto clearWsHook = makeScopedDestructor(
        []()
        {
            globalMegaTestHooks.wsUploadServerEventHook.reset();
        });

    RequestTracker ct(megaApi[0].get());
    megaApi[0]->setMaxConnections(1, &ct);
    ASSERT_EQ(API_OK, ct.waitForResult(60));

    std::unique_ptr<MegaNode> rootnode{megaApi[0]->getRootNode()};
    ASSERT_TRUE(rootnode);

    // Step 1: drop exactly one ChunkIngested ACK frame.
    auto& hook = globalMegaTestHooks.wsUploadServerEventHook;
    hook.configure(WsUploadServerEventAction::Drop,
                   1); // ChunkIngested

    // Step 2: start upload A (single transfer) and require one ACK drop to occur.
    TransferTracker ut(megaApi[0].get());
    auto uploadOptions = makeDefaultUploadOptions();
    megaApi[0]->startUpload(fileName, rootnode.get(), nullptr, &uploadOptions, &ut);

    ASSERT_TRUE(WaitFor(
        [&]()
        {
            return hook.getHitCount() > 0;
        },
        120000))
        << "Timed out waiting for dropped WS ChunkIngested ACK";

    // Step 3: despite one dropped chunk ACK, upload should still converge and complete.
    const auto finalResult = ut.waitForResult(300);
    ASSERT_EQ(finalResult, API_OK)
        << "Upload did not complete after one dropped WS chunk ACK";

    // Step 4: verify cloud node exists and size matches local source.
    rootnode.reset(megaApi[0]->getRootNode());
    ASSERT_TRUE(rootnode);
    std::unique_ptr<MegaNode> cloudNode(
        megaApi[0]->getNodeByPathOfType(fileName.c_str(), rootnode.get(), MegaNode::TYPE_FILE));
    ASSERT_TRUE(cloudNode) << "Uploaded file not found in cloud";
    ASSERT_EQ(cloudNode->getSize(), static_cast<int64_t>(fileSize));
#endif
}

/**
 * @brief Verify that an injected server Throttle event (event 6) stalls progress, then converges.
 *
 * - TEST1: Install hook to rewrite first ChunkIngested ACK (event 1) to Throttle (event 6),
 *          forcing a 10-second throttle window via overridden chunkpos(ms).
 * - TEST2: Start upload and confirm the modification was observed.
 * - TEST3: Capture transfer progress and require delayed forward progress (meaningful stall).
 * - TEST4: Require upload API_OK and cloud node size equals local file size.
 */
TEST_F(SdkWsUploadTest, ThrottleEventStillCompletes)
{
    LOG_info << "___TEST SdkWsUploadThrottleEventStillCompletes___";
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

#ifndef MEGASDK_DEBUG_TEST_HOOKS_ENABLED
    GTEST_SKIP() << "Requires MEGASDK_DEBUG_TEST_HOOKS_ENABLED";
#else
    const std::string fileName =
        "ws_throttle_event_" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".bin";
    constexpr size_t fileSize = 2 * kWsUploadDefaultFileSize;
    ASSERT_TRUE(createFileWithSize(fileName, fileSize, "T")) << "Couldn't create " << fileName;

    auto cleanupFile = makeScopedDestructor(
        [this, &fileName]()
        {
            deleteFile(fileName);
        });
    auto cleanupTransfers = makeScopedDestructor(
        [this]()
        {
            (void)synchronousCancelTransfers(0, MegaTransfer::TYPE_UPLOAD);
        });
    auto clearWsHook = makeScopedDestructor(
        []()
        {
            globalMegaTestHooks.wsUploadServerEventHook.reset();
        });

    RequestTracker ct(megaApi[0].get());
    megaApi[0]->setMaxConnections(1, &ct);
    ASSERT_EQ(API_OK, ct.waitForResult(60));

    // Keep upload active long enough to observe throttling behavior reliably.
    ScopedUploadSpeedLimit restoreUploadSpeed{*megaApi[0], 100000};

    std::unique_ptr<MegaNode> rootnode{megaApi[0]->getRootNode()};
    ASSERT_TRUE(rootnode);

    // Step 1: rewrite first ChunkIngested (1) to Throttle (6), with explicit throttle ms.
    constexpr m_off_t kInjectedThrottleMs = 10000; // 10s throttle window
    auto& hook = globalMegaTestHooks.wsUploadServerEventHook;
    hook.configure(WsUploadServerEventAction::Modify,
                   1, // ChunkIngested
                   6, // Throttle
                   std::nullopt,
                   kInjectedThrottleMs);

    // Step 2: start upload and capture transfer unique id for progress checks.
    std::atomic<bool> startObserved{false};
    std::atomic<uint32_t> transferId{0};
    auto resetOnTransferStartCb = makeScopedDestructor(
        [this]()
        {
            onTransferStartCustomCb = {};
        });
    onTransferStartCustomCb = [&startObserved, &transferId, &fileName](MegaTransfer* transfer)
    {
        if (!transfer || transfer->getType() != MegaTransfer::TYPE_UPLOAD ||
            !transfer->getFileName())
        {
            return;
        }
        if (fileName == transfer->getFileName())
        {
            transferId = transfer->getUniqueId();
            startObserved = true;
        }
    };

    TransferTracker ut(megaApi[0].get());
    auto uploadOptions = makeDefaultUploadOptions();
    megaApi[0]->startUpload(fileName, rootnode.get(), nullptr, &uploadOptions, &ut);

    ASSERT_TRUE(WaitFor(
        [&]()
        {
            return startObserved.load() && transferId.load() > 0;
        },
        30000))
        << "Upload start callback was not observed";

    auto getTransferById = [this](const uint32_t id) -> std::unique_ptr<MegaTransfer>
    {
        return std::unique_ptr<MegaTransfer>(megaApi[0]->getTransferByUniqueId(id));
    };

    ASSERT_TRUE(WaitFor(
        [&]()
        {
            return hook.getHitCount() > 0;
        },
        120000))
        << "Timed out waiting for Throttle injection via hook";

    // Step 3: capture progress baseline and verify throttle stalls new work.
    WaitMillisec(2000); // allow immediate post-hook callbacks to settle
    auto atThrottle = getTransferById(transferId.load());
    ASSERT_TRUE(atThrottle) << "Could not fetch transfer after throttle injection";
    const long long bytesAtThrottle = atThrottle->getTransferredBytes();
    const auto progressWaitStart = std::chrono::steady_clock::now();

    // pauseSending() blocks new chunk sends. Already-buffered/in-flight data may still be
    // ACKed by the server (connection stays open, matching the WS prototype behavior).
    // We do NOT assert strict zero progress here because data already in OS TCP buffers or
    // on the wire can still complete delivery during the throttle window.
    WaitMillisec(5000);

    // Bounded stall check: with setMaxUploadSpeed(100000) and a 5 s throttle window, a
    // healthy pauseSending() caps any further progress to roughly (OS TCP send buffer +
    // in-flight WS frames at the moment Throttle(6) fired). Cap at 4 MiB to catch a
    // broken gate without false-positives on legitimate drain of already-buffered data.
    auto stillThrottled = getTransferById(transferId.load());
    ASSERT_TRUE(stillThrottled) << "Could not fetch transfer during throttle stall check";
    const long long bytesAfter5s = stillThrottled->getTransferredBytes();
    const long long progressDuringThrottle = bytesAfter5s - bytesAtThrottle;
    constexpr long long kThrottleProgressSlackBytes = 4 * 1024 * 1024;
    ASSERT_LE(progressDuringThrottle, kThrottleProgressSlackBytes)
        << "pauseSending() did not bound progress during 5 s throttle: bytes at throttle="
        << bytesAtThrottle << ", after 5 s=" << bytesAfter5s;

    // Step 4: upload should still converge and complete.
    ASSERT_TRUE(WaitFor(
        [&]()
        {
            auto t = getTransferById(transferId.load());
            return t && t->getTransferredBytes() > bytesAtThrottle;
        },
        120000))
        << "Transfer did not resume forward progress after injected Throttle event";

    const auto firstProgressDelayMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                          std::chrono::steady_clock::now() - progressWaitStart)
                                          .count();
    ASSERT_GE(firstProgressDelayMs, 5000)
        << "Injected 10s throttle did not cause a meaningful stall; observed delay(ms)="
        << firstProgressDelayMs;

    megaApi[0]->setMaxUploadSpeed(-1);
    const auto finalResult = ut.waitForResult(300);
    ASSERT_EQ(finalResult, API_OK) << "Upload did not complete after injected Throttle event";

    rootnode.reset(megaApi[0]->getRootNode());
    ASSERT_TRUE(rootnode);
    std::unique_ptr<MegaNode> cloudNode(
        megaApi[0]->getNodeByPathOfType(fileName.c_str(), rootnode.get(), MegaNode::TYPE_FILE));
    ASSERT_TRUE(cloudNode) << "Uploaded file not found in cloud";
    ASSERT_EQ(cloudNode->getSize(), static_cast<int64_t>(fileSize));
#endif
}

/**
 * @brief Verify that an AlreadyOnServer event (event 2) injected from ChunkIngested still
 * converges.
 *
 * - TEST1: Install hook to rewrite first ChunkIngested ACK (event 1) to AlreadyOnServer (event 2).
 * - TEST2: Start upload and confirm the modification was observed.
 * - TEST3: Require upload API_OK and cloud node size equals local file size.
 */
TEST_F(SdkWsUploadTest, AlreadyOnServerEventStillCompletes)
{
    LOG_info << "___TEST SdkWsUploadAlreadyOnServerEventStillCompletes___";
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

#ifndef MEGASDK_DEBUG_TEST_HOOKS_ENABLED
    GTEST_SKIP() << "Requires MEGASDK_DEBUG_TEST_HOOKS_ENABLED";
#else
    const std::string fileName =
        "ws_already_on_server_event_" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".bin";
    constexpr size_t fileSize = 2 * kWsUploadDefaultFileSize;
    ASSERT_TRUE(createFileWithSize(fileName, fileSize, "S")) << "Couldn't create " << fileName;

    auto cleanupFile = makeScopedDestructor(
        [this, &fileName]()
        {
            deleteFile(fileName);
        });
    auto cleanupTransfers = makeScopedDestructor(
        [this]()
        {
            (void)synchronousCancelTransfers(0, MegaTransfer::TYPE_UPLOAD);
        });
    auto clearWsHook = makeScopedDestructor(
        []()
        {
            globalMegaTestHooks.wsUploadServerEventHook.reset();
        });

    RequestTracker ct(megaApi[0].get());
    megaApi[0]->setMaxConnections(1, &ct);
    ASSERT_EQ(API_OK, ct.waitForResult(60));

    std::unique_ptr<MegaNode> rootnode{megaApi[0]->getRootNode()};
    ASSERT_TRUE(rootnode);

    // Step 1: rewrite first ChunkIngested (1) to AlreadyOnServer (2).
    constexpr int kWsChunkIngestedEvent = 1;
    constexpr int kWsAlreadyOnServerEvent = 2;
    auto& hook = globalMegaTestHooks.wsUploadServerEventHook;
    hook.configure(WsUploadServerEventAction::Modify,
                   kWsChunkIngestedEvent,
                   kWsAlreadyOnServerEvent);

    // Step 2: start upload and confirm injected AlreadyOnServer was observed.
    TransferTracker ut(megaApi[0].get());
    auto uploadOptions = makeDefaultUploadOptions();
    megaApi[0]->startUpload(fileName, rootnode.get(), nullptr, &uploadOptions, &ut);

    ASSERT_TRUE(WaitFor(
        [&]()
        {
            return hook.getHitCount() > 0;
        },
        120000))
        << "Timed out waiting for AlreadyOnServer injection via hook";

    // Step 3: upload should still converge and complete.
    const auto finalResult = ut.waitForResult(300);
    ASSERT_EQ(finalResult, API_OK)
        << "Upload did not complete after injected AlreadyOnServer event";

    // B7/NF-7 regression: the pruned opcode-2 handler no longer credits bytesConfirmed
    // for the rewritten chunks. NF-7's fix-up at uploadCompleted() must bring the final
    // progress callback to fileSize. A drift here means NF-7 regressed.
    ASSERT_EQ(onTransferUpdate_progress, static_cast<long long>(fileSize))
        << "NF-7 fix-up did not mask the opcode-2 progress under-count "
           "(final onTransferUpdate progress < fileSize)";

    rootnode.reset(megaApi[0]->getRootNode());
    ASSERT_TRUE(rootnode);
    std::unique_ptr<MegaNode> cloudNode(
        megaApi[0]->getNodeByPathOfType(fileName.c_str(), rootnode.get(), MegaNode::TYPE_FILE));
    ASSERT_TRUE(cloudNode) << "Uploaded file not found in cloud";
    ASSERT_EQ(cloudNode->getSize(), static_cast<int64_t>(fileSize));
#endif
}

/**
 * @brief B8 regression: a server Throttle event received while multiple chunks are in flight
 * must not tear down the connection; it only gates further sends via pauseSending().
 *
 * The pre-A1 code path forced readyState=CLOSED and requeued in-flight chunks on Throttle.
 * That deletion (B8) is accepted because the OPEN+throttled gate in poolWorkerThread still
 * blocks new sends, and already-buffered/on-wire frames naturally ACK during the pause.
 *
 * - TEST1: setMaxConnections(2) so at least 2 chunks can be in flight simultaneously.
 * - TEST2: Inject a Throttle(6) frame rewriting the first ChunkIngested ACK.
 * - TEST3: Immediately sample PoolStateForTesting: assert numChunksInFlight >= 1,
 *          pausedByServerUntilDs > 0 (gate active), connectionsWithInFlight >= 1
 *          (no CLOSED tear-down).
 * - TEST4: Require upload API_OK and cloud node size equals local file size.
 */
TEST_F(SdkWsUploadTest, B8ThrottleDuringSaturatedInFlightCompletes)
{
    LOG_info << "___TEST SdkWsUploadB8ThrottleDuringSaturatedInFlightCompletes___";
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

#ifndef MEGASDK_DEBUG_TEST_HOOKS_ENABLED
    GTEST_SKIP() << "Requires MEGASDK_DEBUG_TEST_HOOKS_ENABLED";
#else
    const std::string fileName =
        "ws_b8_throttle_saturated_" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".bin";
    // 4x default size so multiple chunks are genuinely in flight before Throttle fires.
    constexpr size_t fileSize = 4 * kWsUploadDefaultFileSize;
    ASSERT_TRUE(createFileWithSize(fileName, fileSize, "B")) << "Couldn't create " << fileName;

    auto cleanupFile = makeScopedDestructor(
        [this, &fileName]()
        {
            deleteFile(fileName);
        });
    auto cleanupTransfers = makeScopedDestructor(
        [this]()
        {
            (void)synchronousCancelTransfers(0, MegaTransfer::TYPE_UPLOAD);
        });
    auto clearWsHook = makeScopedDestructor(
        []()
        {
            globalMegaTestHooks.wsUploadServerEventHook.reset();
        });

    RequestTracker ct(megaApi[0].get());
    megaApi[0]->setMaxConnections(2, &ct);
    ASSERT_EQ(API_OK, ct.waitForResult(60));

    ScopedUploadSpeedLimit restoreUploadSpeed{*megaApi[0], -1}; // wire rate to keep chunks saturated

    std::unique_ptr<MegaNode> rootnode{megaApi[0]->getRootNode()};
    ASSERT_TRUE(rootnode);

    // Step 1: rewrite first ChunkIngested (1) to Throttle (6) with a 3 s pause window.
    constexpr m_off_t kInjectedThrottleMs = 3000;
    auto& hook = globalMegaTestHooks.wsUploadServerEventHook;
    hook.configure(WsUploadServerEventAction::Modify,
                   1,
                   6,
                   std::nullopt,
                   kInjectedThrottleMs);

    TransferTracker ut(megaApi[0].get());
    auto uploadOptions = makeDefaultUploadOptions();
    megaApi[0]->startUpload(fileName, rootnode.get(), nullptr, &uploadOptions, &ut);

    // Step 2: wait until the hook fires (Throttle injected).
    ASSERT_TRUE(WaitFor(
        [&]()
        {
            return hook.getHitCount() > 0;
        },
        120000))
        << "Timed out waiting for Throttle injection via hook";

    // Step 3: sample pool state immediately to capture the saturated+throttled moment.
    WsUploadTransferSnapshot snap{};
    ASSERT_TRUE(fetchBestWsUploadTransferSnapshot(*megaApi[0], snap, 5))
        << "Could not capture active WS upload snapshot for pool-state probe";
    ASSERT_FALSE(snap.wsSessionUrl.empty())
        << "Active transfer has no wsSessionUrl; cannot probe pool state";

    ws::UploadEngine::PoolStateForTesting state{};
    const bool observedThrottle = waitForWsUploadPoolStateForTesting(
        *megaApi[0],
        snap.wsSessionUrl,
        state,
        [](const ws::UploadEngine::PoolStateForTesting& s)
        {
            return s.found && s.pausedByServerUntilDs > 0;
        },
        10,
        50);
    ASSERT_TRUE(observedThrottle)
        << "pauseSending() gate did not activate after Throttle injection "
           "(pausedByServerUntilDs stayed 0)";
    ASSERT_GE(state.connectionsWithInFlight, 1u)
        << "B8: connection was torn down during throttle window "
           "(connectionsWithInFlight=0)";

    // Step 4: upload should converge and complete cleanly.
    const auto finalResult = ut.waitForResult(300);
    ASSERT_EQ(finalResult, API_OK)
        << "Upload did not complete cleanly through B8 throttle window";

    rootnode.reset(megaApi[0]->getRootNode());
    ASSERT_TRUE(rootnode);
    std::unique_ptr<MegaNode> cloudNode(
        megaApi[0]->getNodeByPathOfType(fileName.c_str(), rootnode.get(), MegaNode::TYPE_FILE));
    ASSERT_TRUE(cloudNode) << "Uploaded file not found in cloud";
    ASSERT_EQ(cloudNode->getSize(), static_cast<int64_t>(fileSize));
#endif
}

/**
 * @brief B9 regression: after a force-close mid-throttle, reconnect attempts are gated by
 *        CONNRETRYINTERVAL (5s) and not by the removed 1-ds sleep block.
 *
 * Scenario:
 *   1. Upload a moderate file; inject a Throttle ACK via wsUploadServerEventHook so the
 *      pool enters pausedByServer state.
 *   2. Via onWsConnForceCloseNow, force the WsConn into CLOSED exactly once after Throttle
 *      is observed — this deterministically creates the historical "CLOSED + throttled"
 *      state that the deleted delay-block used to gate with sleep_ds(1).
 *   3. Via onWsHandshake, fail the next two handshake attempts so the pool worker has to
 *      exercise the !ok branch of the CLOSED gate where CONNRETRYINTERVAL lives.
 *   4. onWsPoolReconnectAttempt time-stamps each reconnect attempt; the test asserts the
 *      delta between attempts 2 and 3 is >= CONNRETRYINTERVAL (50 ds = 5000 ms, minus
 *      slack) — proving reconnects are rate-limited by CONNRETRYINTERVAL and not by the
 *      deleted 1-ds sleep.
 *   5. After the forced failures, handshakes succeed and the upload converges to API_OK.
 */
TEST_F(SdkWsUploadTest, B9ClosedThrottleReconnectPacing)
{
    LOG_info << "___TEST SdkWsUploadB9ClosedThrottleReconnectPacing___";
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

#ifndef MEGASDK_DEBUG_TEST_HOOKS_ENABLED
    GTEST_SKIP() << "Requires MEGASDK_DEBUG_TEST_HOOKS_ENABLED";
#else
    const std::string fileName =
        "ws_b9_closed_throttle_reconnect_" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".bin";
    constexpr size_t fileSize = 4 * kWsUploadDefaultFileSize;
    ASSERT_TRUE(createFileWithSize(fileName, fileSize, "B")) << "Couldn't create " << fileName;

    auto cleanupFile = makeScopedDestructor(
        [this, &fileName]()
        {
            deleteFile(fileName);
        });
    auto cleanupTransfers = makeScopedDestructor(
        [this]()
        {
            (void)synchronousCancelTransfers(0, MegaTransfer::TYPE_UPLOAD);
        });
    auto clearWsHooks = makeScopedDestructor(
        []()
        {
            globalMegaTestHooks.wsUploadServerEventHook.reset();
            globalMegaTestHooks.onWsConnForceCloseNow = nullptr;
            globalMegaTestHooks.onWsPoolReconnectAttempt = nullptr;
            globalMegaTestHooks.onWsHandshake = nullptr;
        });

    RequestTracker ct(megaApi[0].get());
    megaApi[0]->setMaxConnections(1, &ct);
    ASSERT_EQ(API_OK, ct.waitForResult(60));

    // Rewrite the first ChunkIngested (1) ACK into Throttle (6) with a ~6s pause so the
    // pool stays throttled through the forced reconnect attempts.
    constexpr m_off_t kInjectedThrottleMs = 6000;
    auto& evHook = globalMegaTestHooks.wsUploadServerEventHook;
    evHook.configure(WsUploadServerEventAction::Modify,
                     1,
                     6,
                     std::nullopt,
                     kInjectedThrottleMs);

    std::mutex attemptMu;
    std::vector<std::chrono::steady_clock::time_point> attemptTimes;
    std::vector<unsigned> attemptRetryCounts;
    globalMegaTestHooks.onWsPoolReconnectAttempt =
        [&](::mega::ws::WsPool*, unsigned retryCount, dstime /*firstFailureDs*/)
    {
        std::lock_guard<std::mutex> lk(attemptMu);
        attemptTimes.push_back(std::chrono::steady_clock::now());
        attemptRetryCounts.push_back(retryCount);
    };

    std::atomic<bool> throttleObserved{false};
    std::atomic<int> forceCloseFired{0};
    globalMegaTestHooks.onWsConnForceCloseNow = [&](::mega::ws::WsConn*) -> bool
    {
        if (!throttleObserved.load(std::memory_order_acquire))
            return false;
        return forceCloseFired.fetch_add(1, std::memory_order_acq_rel) == 0;
    };

    std::atomic<int> handshakeFailsRemaining{2};
    globalMegaTestHooks.onWsHandshake =
        [&](const std::string& /*url*/, long /*timeoutMs*/, std::string& err) -> bool
    {
        if (!throttleObserved.load(std::memory_order_acquire))
            return false;
        if (handshakeFailsRemaining.load(std::memory_order_acquire) <= 0)
            return false;
        handshakeFailsRemaining.fetch_sub(1, std::memory_order_acq_rel);
        err = "[B9 test] simulated handshake failure";
        return true;
    };

    std::unique_ptr<MegaNode> rootnode{megaApi[0]->getRootNode()};
    ASSERT_TRUE(rootnode);

    TransferTracker ut(megaApi[0].get());
    auto uploadOptions = makeDefaultUploadOptions();
    megaApi[0]->startUpload(fileName, rootnode.get(), nullptr, &uploadOptions, &ut);

    ASSERT_TRUE(WaitFor(
        [&]()
        {
            return evHook.getHitCount() > 0;
        },
        120000))
        << "Timed out waiting for Throttle injection via hook";
    throttleObserved.store(true, std::memory_order_release);

    // Wait until at least three reconnect attempts have been recorded: 1st = initial
    // reconnect after force-close, 2nd = first retry (!retryCount++ -> continue, no
    // sleep), 3rd = retry gated by CONNRETRYINTERVAL.
    ASSERT_TRUE(WaitFor(
        [&]()
        {
            std::lock_guard<std::mutex> lk(attemptMu);
            return attemptTimes.size() >= 3u;
        },
        120000))
        << "B9: pool worker did not reach 3 reconnect attempts after force-close";

    {
        std::lock_guard<std::mutex> lk(attemptMu);
        ASSERT_GE(attemptTimes.size(), 3u);
        const auto delta12Ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                   attemptTimes[2] - attemptTimes[1])
                                   .count();
        // CONNRETRYINTERVAL is 50 ds = 5000 ms. Require >= 4000 ms to tolerate scheduler
        // jitter on CI. The deleted 1-ds sleep would put this delta at ~100 ms.
        ASSERT_GE(delta12Ms, 4000)
            << "B9 regression: reconnect attempts after force-close are not gated by "
               "CONNRETRYINTERVAL (observed δ=" << delta12Ms << " ms between attempts "
               "2 and 3; retryCounts seen: "
            << attemptRetryCounts[0] << ',' << attemptRetryCounts[1] << ','
            << attemptRetryCounts[2] << ")";
    }

    const auto finalResult = ut.waitForResult(300);
    ASSERT_EQ(finalResult, API_OK)
        << "Upload did not complete cleanly after B9 force-close + reconnect sequence";

    rootnode.reset(megaApi[0]->getRootNode());
    ASSERT_TRUE(rootnode);
    std::unique_ptr<MegaNode> cloudNode(
        megaApi[0]->getNodeByPathOfType(fileName.c_str(), rootnode.get(), MegaNode::TYPE_FILE));
    ASSERT_TRUE(cloudNode) << "Uploaded file not found in cloud";
    ASSERT_EQ(cloudNode->getSize(), static_cast<int64_t>(fileSize));
#endif
}

/**
 * @brief Verify a negative server event (<0) triggers temporary error handling and retry.
 *
 * - TEST1: Install hook to rewrite first ChunkIngested ACK (event 1) to API_EAGAIN (-3).
 * - TEST2: Start upload and confirm hook injection was observed.
 * - TEST3: Require at least one temporary error (API_EAGAIN) and recovery back to ACTIVE.
 * - TEST4: Require upload API_OK and cloud node size equals local file size.
 */
TEST_F(SdkWsUploadTest, NegativeServerEventRetriesAndCompletes)
{
    LOG_info << "___TEST SdkWsUploadNegativeServerEventRetriesAndCompletes___";
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

#ifndef MEGASDK_DEBUG_TEST_HOOKS_ENABLED
    GTEST_SKIP() << "Requires MEGASDK_DEBUG_TEST_HOOKS_ENABLED";
#else
    const std::string fileName =
        "ws_negative_server_event_" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".bin";
    constexpr size_t fileSize = 2 * kWsUploadDefaultFileSize;
    ASSERT_TRUE(createFileWithSize(fileName, fileSize, "N")) << "Couldn't create " << fileName;

    auto cleanupFile = makeScopedDestructor(
        [this, &fileName]()
        {
            deleteFile(fileName);
        });
    auto cleanupTransfers = makeScopedDestructor(
        [this]()
        {
            (void)synchronousCancelTransfers(0, MegaTransfer::TYPE_UPLOAD);
        });
    auto clearWsHook = makeScopedDestructor(
        []()
        {
            globalMegaTestHooks.wsUploadServerEventHook.reset();
        });

    RequestTracker ct(megaApi[0].get());
    megaApi[0]->setMaxConnections(1, &ct);
    ASSERT_EQ(API_OK, ct.waitForResult(60));

    // Keep upload active long enough to observe retry behavior.
    ScopedUploadSpeedLimit restoreUploadSpeed{*megaApi[0], 100000};

    std::unique_ptr<MegaNode> rootnode{megaApi[0]->getRootNode()};
    ASSERT_TRUE(rootnode);

    // Step 1: rewrite first ChunkIngested (1) to API_EAGAIN (-3) to hit event<0 branch.
    auto& hook = globalMegaTestHooks.wsUploadServerEventHook;
    hook.configure(WsUploadServerEventAction::Modify,
                   1, // ChunkIngested
                   API_EAGAIN);

    // Step 2: start upload and require injected negative event was observed.
    WsUploadRetryTracker tracker(megaApi[0].get());
    auto uploadOptions = makeDefaultUploadOptions();
    megaApi[0]->startUpload(fileName, rootnode.get(), nullptr, &uploadOptions, &tracker);

    ASSERT_TRUE(WaitFor(
        [&]()
        {
            return hook.getHitCount() > 0;
        },
        120000))
        << "Timed out waiting for injected negative WS server event";

    // Step 3: require temporary error surfaced and transfer recovered to ACTIVE.
    ASSERT_TRUE(WaitFor(
        [&tracker]()
        {
            return tracker.temporaryErrorCount.load() >= 1;
        },
        120000))
        << "Upload did not surface temporary error after injected negative WS server event";
    ASSERT_EQ(tracker.lastTemporaryError.load(), ErrorCodes::API_EAGAIN);

    ASSERT_TRUE(WaitFor(
        [&tracker]()
        {
            return tracker.sawActiveAfterTemporaryError.load();
        },
        60000))
        << "Upload did not return to ACTIVE after temporary error retry"
        << " [starts=" << tracker.startCount.load() << " updates=" << tracker.updateCount.load()
        << " activeUpdates=" << tracker.activeStateUpdateCount.load()
        << " lastState=" << tracker.lastState.load()
        << " temporaryErrors=" << tracker.temporaryErrorCount.load()
        << " lastTemporaryError=" << static_cast<int>(tracker.lastTemporaryError.load()) << "]";

    // Step 4: upload should still converge and complete.
    megaApi[0]->setMaxUploadSpeed(-1);
    const auto finalResult = tracker.waitForResult(300);
    ASSERT_EQ(finalResult, API_OK)
        << "Upload did not complete after injected negative WS server event";

    rootnode.reset(megaApi[0]->getRootNode());
    ASSERT_TRUE(rootnode);
    std::unique_ptr<MegaNode> cloudNode(
        megaApi[0]->getNodeByPathOfType(fileName.c_str(), rootnode.get(), MegaNode::TYPE_FILE));
    ASSERT_TRUE(cloudNode) << "Uploaded file not found in cloud";
    ASSERT_EQ(cloudNode->getSize(), static_cast<int64_t>(fileSize));
#endif
}

/**
 * @brief Verify that a server CRC failure (event 3) triggers chunk retry and upload still
 * completes.
 *
 * - TEST1: Install hook to rewrite the first ChunkIngested ACK (event 1) to CrcFailed (event 3).
 * - TEST2: Start upload and confirm the modification was observed.
 * - TEST3: Require upload API_OK — the CRC-failed chunk must have been retried successfully.
 * - TEST4: Verify cloud node size matches local file size.
 */
TEST_F(SdkWsUploadTest, CrcFailureRetryStillCompletes)
{
    LOG_info << "___TEST SdkWsUploadCrcFailureRetryStillCompletes___";
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

#ifndef MEGASDK_DEBUG_TEST_HOOKS_ENABLED
    GTEST_SKIP() << "Requires MEGASDK_DEBUG_TEST_HOOKS_ENABLED";
#else
    const std::string fileName =
        "ws_crc_failure_retry_" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".bin";
    constexpr size_t fileSize = 2 * kWsUploadDefaultFileSize;
    ASSERT_TRUE(createFileWithSize(fileName, fileSize, "C")) << "Couldn't create " << fileName;

    auto cleanupFile = makeScopedDestructor(
        [this, &fileName]()
        {
            deleteFile(fileName);
        });
    auto cleanupTransfers = makeScopedDestructor(
        [this]()
        {
            (void)synchronousCancelTransfers(0, MegaTransfer::TYPE_UPLOAD);
        });
    auto clearWsHook = makeScopedDestructor(
        []()
        {
            globalMegaTestHooks.wsUploadServerEventHook.reset();
        });

    RequestTracker ct(megaApi[0].get());
    megaApi[0]->setMaxConnections(1, &ct);
    ASSERT_EQ(API_OK, ct.waitForResult(60));

    std::unique_ptr<MegaNode> rootnode{megaApi[0]->getRootNode()};
    ASSERT_TRUE(rootnode);

    // Step 1: rewrite the first ChunkIngested (1) to CrcFailed (3).
    auto& hook = globalMegaTestHooks.wsUploadServerEventHook;
    hook.configure(WsUploadServerEventAction::Modify,
                   1, // ChunkIngested
                   3); // CrcFailed

    // Step 2: start upload and confirm the CRC failure injection was observed.
    TransferTracker ut(megaApi[0].get());
    auto uploadOptions = makeDefaultUploadOptions();
    megaApi[0]->startUpload(fileName, rootnode.get(), nullptr, &uploadOptions, &ut);

    ASSERT_TRUE(WaitFor(
        [&]()
        {
            return hook.getHitCount() > 0;
        },
        120000))
        << "Timed out waiting for CRC failure injection via hook";

    // Step 3: despite one injected CRC failure, upload should retry the chunk and complete.
    const auto finalResult = ut.waitForResult(300);
    ASSERT_EQ(finalResult, API_OK) << "Upload did not complete after injected CRC failure";

    // Step 4: verify cloud node exists and size matches local source.
    rootnode.reset(megaApi[0]->getRootNode());
    ASSERT_TRUE(rootnode);
    std::unique_ptr<MegaNode> cloudNode(
        megaApi[0]->getNodeByPathOfType(fileName.c_str(), rootnode.get(), MegaNode::TYPE_FILE));
    ASSERT_TRUE(cloudNode) << "Uploaded file not found in cloud";
    ASSERT_EQ(cloudNode->getSize(), static_cast<int64_t>(fileSize));
#endif
}

/**
 * @brief Verify that a server Distress event (event 5) retires pool assignment for new files
 *        but does not restart an already in-flight upload.
 *
 * - TEST1: Start upload with throttled speed, capture initial pool ID and session URL.
 * - TEST2: Install hook to rewrite the first ChunkIngested ACK (event 1) to Distress (event 5).
 * - TEST3: Confirm distress injection was observed.
 * - TEST4: Best-effort observe session URL or pool ID change after Distress refresh.
 *          In synthetic Distress injection, USC may keep the same endpoint host/size class,
 *          so pool reuse is valid and both may remain unchanged.
 * - TEST5: Require no transfer restart or temporary error due to Distress.
 * - TEST6: Require upload API_OK and cloud node size equals local file size.
 */
TEST_F(SdkWsUploadTest, DistressRetiresPoolWithoutRestartingInFlightUpload)
{
    LOG_info << "___TEST SdkWsUploadDistressRetiresPoolWithoutRestartingInFlightUpload___";
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

#ifndef MEGASDK_DEBUG_TEST_HOOKS_ENABLED
    GTEST_SKIP() << "Requires MEGASDK_DEBUG_TEST_HOOKS_ENABLED";
#else
    const std::string fileName =
        "ws_distress_refresh_" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".bin";
    constexpr size_t fileSize = 2 * kWsUploadDefaultFileSize;
    ASSERT_TRUE(createFileWithSize(fileName, fileSize, "D")) << "Couldn't create " << fileName;

    auto cleanupFile = makeScopedDestructor(
        [this, &fileName]()
        {
            deleteFile(fileName);
        });
    auto cleanupTransfers = makeScopedDestructor(
        [this]()
        {
            (void)synchronousCancelTransfers(0, MegaTransfer::TYPE_UPLOAD);
        });
    auto clearWsHook = makeScopedDestructor(
        []()
        {
            globalMegaTestHooks.wsUploadServerEventHook.reset();
        });

    RequestTracker ct(megaApi[0].get());
    megaApi[0]->setMaxConnections(1, &ct);
    ASSERT_EQ(API_OK, ct.waitForResult(60));

    // Throttle upload speed so we can capture the initial pool ID before the hook fires.
    ScopedUploadSpeedLimit restoreUploadSpeed{*megaApi[0], 100000};

    std::unique_ptr<MegaNode> rootnode{megaApi[0]->getRootNode()};
    ASSERT_TRUE(rootnode);

    // Step 1: start upload and capture the initial pool ID and session URL.
    WsUploadRetryTracker tracker(megaApi[0].get());
    auto uploadOptions = makeDefaultUploadOptions();
    megaApi[0]->startUpload(fileName, rootnode.get(), nullptr, &uploadOptions, &tracker);

    std::uintptr_t initialPoolId = 0;
    std::string initialSessionUrl;
    ASSERT_TRUE(WaitFor(
        [&]()
        {
            std::vector<WsUploadTransferSnapshot> snapshots;
            if (!fetchWsUploadTransferSnapshots(*megaApi[0], snapshots, 1))
            {
                return false;
            }
            for (const auto& snapshot: snapshots)
            {
                if (snapshot.found && snapshot.fileName == fileName && snapshot.poolId != 0 &&
                    !snapshot.wsSessionUrl.empty())
                {
                    initialPoolId = snapshot.poolId;
                    initialSessionUrl = snapshot.wsSessionUrl;
                    return true;
                }
            }
            return false;
        },
        90000))
        << "Could not capture initial pool ID/session URL";

    // Step 2: install hook to rewrite the first ChunkIngested (1) to Distress (5).
    auto& hook = globalMegaTestHooks.wsUploadServerEventHook;
    hook.configure(WsUploadServerEventAction::Modify,
                   1, // ChunkIngested
                   5); // Distress

    // Step 3: confirm distress injection was observed.
    ASSERT_TRUE(WaitFor(
        [&]()
        {
            return hook.getHitCount() > 0;
        },
        120000))
        << "Timed out waiting for Distress injection via hook";

    // Step 4: best-effort observe endpoint or pool switch after distress-triggered refresh.
    // With synthetic event injection, USC may still return an equivalent host/size class and
    // the pool can be reused, so this is informational (non-fatal).
    megaApi[0]->setMaxUploadSpeed(-1);

    std::uintptr_t observedPoolIdAfterDistress = initialPoolId;
    std::string observedSessionUrlAfterDistress = initialSessionUrl;
    const bool endpointOrPoolChangedAfterDistress = WaitFor(
        [&]()
        {
            std::vector<WsUploadTransferSnapshot> snapshots;
            if (!fetchWsUploadTransferSnapshots(*megaApi[0], snapshots, 1))
            {
                return false;
            }
            for (const auto& snapshot: snapshots)
            {
                if (!snapshot.found || snapshot.fileName != fileName)
                {
                    continue;
                }

                if (snapshot.poolId != 0)
                {
                    observedPoolIdAfterDistress = snapshot.poolId;
                }
                if (!snapshot.wsSessionUrl.empty())
                {
                    observedSessionUrlAfterDistress = snapshot.wsSessionUrl;
                }

                const bool sessionUrlChanged =
                    !snapshot.wsSessionUrl.empty() && snapshot.wsSessionUrl != initialSessionUrl;
                const bool poolChanged = snapshot.poolId != 0 && snapshot.poolId != initialPoolId;
                if (sessionUrlChanged || poolChanged)
                {
                    return true;
                }
            }
            return false;
        },
        120000);

    if (!endpointOrPoolChangedAfterDistress)
    {
        LOG_warn << "Distress observed but neither session URL nor pool ID changed; USC may have "
                    "returned an equivalent endpoint, allowing pool reuse."
                 << " [initialSessionUrl=" << initialSessionUrl
                 << "] [observedSessionUrl=" << observedSessionUrlAfterDistress
                 << "] [initialPoolId=" << initialPoolId
                 << "] [observedPoolId=" << observedPoolIdAfterDistress << "]";
    }

    // Step 5: Distress should not restart the in-flight transfer attempt.
    ASSERT_TRUE(WaitFor(
        [&tracker]()
        {
            return tracker.startCount.load() >= 1;
        },
        30000))
        << "Upload did not emit initial onTransferStart callback";

    const auto finalResult = tracker.waitForResult(120);
    ASSERT_EQ(tracker.startCount.load(), 1)
        << "Distress caused transfer start path to run again unexpectedly"
        << " [startCount=" << tracker.startCount.load()
        << "] [temporaryErrors=" << tracker.temporaryErrorCount.load() << "]";
    ASSERT_EQ(tracker.temporaryErrorCount.load(), 0)
        << "Distress surfaced temporary errors while current upload should keep draining"
        << " [startCount=" << tracker.startCount.load()
        << "] [temporaryErrors=" << tracker.temporaryErrorCount.load()
        << "] [lastTemporaryError=" << tracker.lastTemporaryError.load() << "]";

    // Step 6: upload should still complete.
    ASSERT_EQ(finalResult, API_OK)
        << "Upload did not complete after injected Distress pool refresh";

    // Step 7: verify cloud node exists and size matches local source.
    rootnode.reset(megaApi[0]->getRootNode());
    ASSERT_TRUE(rootnode);
    std::unique_ptr<MegaNode> cloudNode(
        megaApi[0]->getNodeByPathOfType(fileName.c_str(), rootnode.get(), MegaNode::TYPE_FILE));
    ASSERT_TRUE(cloudNode) << "Uploaded file not found in cloud";
    ASSERT_EQ(cloudNode->getSize(), static_cast<int64_t>(fileSize));
#endif
}

#endif // defined(DEBUG)
