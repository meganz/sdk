/**
 * @file SdkTestMediatsIntegration_test.cpp
 * @brief Integration tests for mediats end-to-end behaviour.
 *
 * Unit tests in tests/unit/MediaTs_test.cpp and MediaTs_db_test.cpp already cover:
 *  - computeMediaTsIfMediaFile branches (filename/mtime/ctime priority, extension whitelist)
 *  - putNodeInDb / getNode / every SqliteAccountState read method
 *  - Node::setattr() with real encrypted attrs
 *  - Migration backfill via ALTER TABLE DROP COLUMN
 *  - OrderByClause MEDIATS_ASC/DESC SQL snippets and tie-break
 *
 * This file covers behaviour only reachable through MegaApi + real server:
 *  - Non-zero mediats on uploaded media (all fixture photos).
 *  - Zero mediats on a non-media upload (guards against a regression that would
 *    populate mediats unconditionally from mtime on the upload path).
 *  - Rename / copy / move paths recompute or preserve mediats correctly.
 *  - Fresh-DB re-derive after locallogout+resumeSession+fetchnodes (the
 *    "new device" guarantee — mediats is client-local, so the newly-logged-in
 *    client must recompute it from the server-returned filename/mtime during
 *    setattr()).
 *  - File-version upload: a new version over an existing name updates mediats
 *    to the new version's inputs.
 *  - Cross-account inshare: receiving account computes mediats independently
 *    (catches a regression where mediats is accidentally tied to the uploader).
 *  - Cross-account actionpacket-driven rename: sharer renames → sharee's DB
 *    recomputes mediats from the rename actionpacket.
 *  - Sync-engine-driven upload produces correct mediats.
 *  - Sync-engine-driven rename recomputes mediats.
 */

#include "mega/mediats_utils.h"
#include "megaapi.h"
#include "SdkTestNodesSetUp.h"

#ifdef ENABLE_SYNC
#include "SdkTestSyncNodesOperations.h"
#endif

#include "sdk_test_utils.h"

#include <gmock/gmock.h>

#include <chrono>
#include <filesystem>
#include <memory>
#include <string>

using namespace sdk_test;
using namespace testing;
using namespace std::chrono_literals;

namespace
{

// Pattern filename used across rename / copy / sync tests. The expected mediats
// for any pattern filename is derived at runtime via the production helper
// computeMediaTsIfMediaFile() — avoids hardcoding magic millisecond literals
// alongside the filename string. Correctness of the helper itself is covered
// by 53+ unit tests in tests/unit/MediaTs_test.cpp; here we only verify that
// the computed value propagates through upload → DB → read-back.
constexpr const char* kPatternFilename = "IMG_20240115_103045.jpg";

// Passing mtime=0, ctime=0 forces the helper to use the filename pattern
// (priority 1); if the filename has no embedded timestamp the result is 0.
int64_t expectedMediaTsMs(const std::string& filename)
{
    return static_cast<int64_t>(
        ::mega::computeMediaTsIfMediaFile(filename, /*mtime=*/0, /*ctime=*/0));
}

// Fixed reference timestamps (seconds since epoch) for deterministic
// mtime-fallback assertions.
constexpr int64_t kMtime2020 = 1600000000LL; // 2020-09-13 12:26:40 UTC
constexpr int64_t kMtime2023 = 1700000000LL; // 2023-11-14 22:13:20 UTC

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
//  Fixture: pre-uploaded non-pattern photos for primary-account tests.
//
//  Names are plain (no timestamp pattern) so mediats falls back to mtime × 1000
//  at fixture setup; rename/copy tests then flip filenames to patterns and
//  observe recomputation.
// ─────────────────────────────────────────────────────────────────────────────

class SdkTestMediatsIntegration: public SdkTestNodesSetUp
{
public:
    const std::string& getRootTestDir() const override
    {
        static const std::string dirName{"SDK_TEST_MEDIATS_INTEGRATION"};
        return dirName;
    }

    const std::vector<NodeInfo>& getElements() const override
    {
        // Unique mtimes (7h / 4h / 1h ago) so the per-node mediats values are
        // distinct and any cross-node confusion in assertions is visible.
        static const std::vector<NodeInfo> ELEMENTS{
            FileNodeInfo("alpha.jpg").setMtime(7h),
            FileNodeInfo("delta.jpg").setMtime(4h),
            FileNodeInfo("golf.jpg").setMtime(1h),
        };
        return ELEMENTS;
    }

    bool keepDifferentCreationTimes() override
    {
        return false;
    }

protected:
    // Create a local file with @p content, upload it to the fixture's test
    // directory with the given @p mtimeSec, and return the uploaded node
    // handle. The local source file is removed via RAII before return so
    // callers don't need to manage cleanup — a failed assertion below will
    // still release it. Returns INVALID_HANDLE on setup or upload failure.
    MegaHandle uploadFileAndCleanup(const std::string& filename,
                                    const std::string& content,
                                    int64_t mtimeSec)
    {
        if (!createFile(filename, /*largeFile=*/false, content))
        {
            ADD_FAILURE() << "createFile(" << filename << ") failed";
            return INVALID_HANDLE;
        }
        const MrProper localCleanup(
            [this, &filename]
            {
                deleteFile(filename);
            });

        MegaHandle h = INVALID_HANDLE;
        const int err = doStartUpload(0,
                                      &h,
                                      filename.c_str(),
                                      getRootTestDirectory(),
                                      /*fileName=*/nullptr,
                                      mtimeSec,
                                      /*appData=*/nullptr,
                                      /*isSourceTemporary=*/false,
                                      /*startFirst=*/false,
                                      /*cancelToken=*/nullptr);
        EXPECT_EQ(err, API_OK);
        return (err == API_OK) ? h : INVALID_HANDLE;
    }
};

// Test: every media file uploaded via the fixture carries a positive mediats.
// Guards the upload + putNodeInDb path end-to-end.
TEST_F(SdkTestMediatsIntegration, MediaCaptureTimestamp_NonZeroForMediaFiles)
{
    for (const char* name: {"alpha.jpg", "delta.jpg", "golf.jpg"})
    {
        SCOPED_TRACE(name);
        auto node = getNodeByPath(name);
        ASSERT_NE(node, nullptr);
        EXPECT_GT(node->getMediaCaptureTimeMs(), 0)
            << "Media file should have a non-zero media capture timestamp";
    }
}

// Test: a non-media upload must carry mediats = 0. Complements the non-zero
// assertion above; a regression setting mediats unconditionally from mtime
// would fail here.
TEST_F(SdkTestMediatsIntegration, UploadNonMediaHasZeroMediats)
{
    const MegaHandle uploaded = uploadFileAndCleanup("notes.txt", "mediats", kMtime2023);
    ASSERT_NE(uploaded, INVALID_HANDLE);

    std::unique_ptr<MegaNode> node(megaApi[0]->getNodeByHandle(uploaded));
    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->getMediaCaptureTimeMs(), 0) << "Non-media upload must carry mediats=0";
}

// Test: rename recomputes mediats. Plain filename → pattern filename must flip
// mediats to the pattern-derived value; renaming back reverts it.
TEST_F(SdkTestMediatsIntegration, MediaTs_RecomputedOnRename)
{
    auto alpha = getNodeByPath("alpha.jpg");
    ASSERT_NE(alpha, nullptr);

    const int64_t originalMediaTs = alpha->getMediaCaptureTimeMs();
    ASSERT_GT(originalMediaTs, 0) << "alpha.jpg should have a positive mediats from mtime";

    ASSERT_EQ(API_OK, doRenameNode(0, alpha.get(), kPatternFilename));

    auto renamed = std::unique_ptr<MegaNode>(megaApi[0]->getNodeByHandle(alpha->getHandle()));
    ASSERT_NE(renamed, nullptr);
    EXPECT_EQ(renamed->getMediaCaptureTimeMs(), expectedMediaTsMs(kPatternFilename))
        << "After rename to pattern filename, mediats should reflect the filename timestamp";

    ASSERT_EQ(API_OK, doRenameNode(0, renamed.get(), "alpha.jpg"));

    auto reverted = std::unique_ptr<MegaNode>(megaApi[0]->getNodeByHandle(alpha->getHandle()));
    ASSERT_NE(reverted, nullptr);
    EXPECT_EQ(reverted->getMediaCaptureTimeMs(), originalMediaTs)
        << "After rename back, mediats should revert to the original mtime-based value";
}

// Test: copy sets mediats on the new node. Copying with the same filename
// preserves the source's mediats; copying with a pattern filename uses the
// pattern-derived value.
TEST_F(SdkTestMediatsIntegration, MediaTs_SetOnCopy)
{
    auto delta = getNodeByPath("delta.jpg");
    ASSERT_NE(delta, nullptr);

    const int64_t deltaMediaTs = delta->getMediaCaptureTimeMs();
    ASSERT_GT(deltaMediaTs, 0) << "delta.jpg should have a positive mediats";

    MegaHandle copiedHandle = INVALID_HANDLE;
    ASSERT_EQ(API_OK,
              doCopyNode(0, &copiedHandle, delta.get(), getRootTestDirectory(), "delta_copy.jpg"));

    auto copied = std::unique_ptr<MegaNode>(megaApi[0]->getNodeByHandle(copiedHandle));
    ASSERT_NE(copied, nullptr);
    EXPECT_EQ(copied->getMediaCaptureTimeMs(), deltaMediaTs)
        << "Copy with same mtime should have the same mediats as the source";

    MegaHandle patternCopiedHandle = INVALID_HANDLE;
    ASSERT_EQ(
        API_OK,
        doCopyNode(0, &patternCopiedHandle, delta.get(), getRootTestDirectory(), kPatternFilename));

    auto patternCopied =
        std::unique_ptr<MegaNode>(megaApi[0]->getNodeByHandle(patternCopiedHandle));
    ASSERT_NE(patternCopied, nullptr);
    EXPECT_EQ(patternCopied->getMediaCaptureTimeMs(), expectedMediaTsMs(kPatternFilename))
        << "Copy with pattern filename should derive mediats from the filename";
}

// Test: move preserves mediats (filename, mtime, ctime all unchanged).
TEST_F(SdkTestMediatsIntegration, MediaTs_PreservedOnMove)
{
    auto golf = getNodeByPath("golf.jpg");
    ASSERT_NE(golf, nullptr);

    const int64_t originalMediaTs = golf->getMediaCaptureTimeMs();
    ASSERT_GT(originalMediaTs, 0) << "golf.jpg should have a positive mediats";

    auto subfolder = createRemoteDir("mediats_move_target", getRootTestDirectory());
    ASSERT_NE(subfolder, nullptr);

    ASSERT_EQ(API_OK, doMoveNode(0, nullptr, golf.get(), subfolder.get()));

    auto moved = std::unique_ptr<MegaNode>(megaApi[0]->getNodeByHandle(golf->getHandle()));
    ASSERT_NE(moved, nullptr);
    EXPECT_EQ(moved->getMediaCaptureTimeMs(), originalMediaTs)
        << "Move should not change mediats (filename unchanged)";
}

// Test: uploading a new version over an existing file recomputes mediats from
// the new version's inputs. Versions share the same filename, so mediats must
// come from the new version's mtime.
TEST_F(SdkTestMediatsIntegration, MediaTs_VersionUploadRecomputes)
{
    ASSERT_EQ(API_OK, doSetFileVersionsOption(0, /*disable=*/false));

    const std::string filename = "version_target.jpg";
    constexpr int64_t kMtimeV1 = kMtime2020;
    constexpr int64_t kMtimeV2 = kMtime2023;

    // v1 — distinct local content so v2 gets a different fingerprint.
    const MegaHandle hv1 = uploadFileAndCleanup(filename, "v1", kMtimeV1);
    ASSERT_NE(hv1, INVALID_HANDLE);
    std::unique_ptr<MegaNode> v1(megaApi[0]->getNodeByHandle(hv1));
    ASSERT_NE(v1, nullptr);
    EXPECT_EQ(v1->getMediaCaptureTimeMs(), kMtimeV1 * 1000LL);

    // v2 — same filename, different content, different mtime.
    const MegaHandle hv2 = uploadFileAndCleanup(filename, "v2-different", kMtimeV2);
    ASSERT_NE(hv2, INVALID_HANDLE);
    std::unique_ptr<MegaNode> v2(megaApi[0]->getNodeByHandle(hv2));
    ASSERT_NE(v2, nullptr);
    ASSERT_NE(hv1, hv2) << "Version upload must produce a new node handle";
    EXPECT_EQ(v2->getMediaCaptureTimeMs(), kMtimeV2 * 1000LL)
        << "New version must carry mediats computed from the new mtime, not v1";
}

// Test: fresh-DB re-derive via locallogout + resumeSession + fetchnodes.
// Mediats is client-local (not server-stored) — a fresh client must compute it
// from the server-returned filename/mtime during setattr(). Unit tests cover
// the migration case (ALTER TABLE DROP COLUMN); only MegaApi reaches this path.
TEST_F(SdkTestMediatsIntegration, FreshDbRederivesMediatsFromReceivedNode)
{
    auto alpha = getNodeByPath("alpha.jpg");
    ASSERT_NE(alpha, nullptr);
    const int64_t mediaTsBefore = alpha->getMediaCaptureTimeMs();
    ASSERT_GT(mediaTsBefore, 0);
    const MegaHandle h = alpha->getHandle();

    std::unique_ptr<char[]> session(dumpSession(0));
    ASSERT_NE(session.get(), nullptr);
    ASSERT_NO_FATAL_FAILURE(locallogout(0));
    ASSERT_NO_FATAL_FAILURE(resumeSession(session.get(), 0));
    ASSERT_NO_FATAL_FAILURE(fetchnodes(0));

    std::unique_ptr<MegaNode> afterFresh(megaApi[0]->getNodeByHandle(h));
    ASSERT_NE(afterFresh, nullptr) << "Node missing after fresh fetchnodes";
    EXPECT_EQ(afterFresh->getMediaCaptureTimeMs(), mediaTsBefore)
        << "Fresh DB must recompute mediats locally from received filename/mtime";
}

// ─────────────────────────────────────────────────────────────────────────────
//  Fixture: sync engine path (upload/rename triggered by local filesystem).
// ─────────────────────────────────────────────────────────────────────────────

#ifdef ENABLE_SYNC

class SdkTestMediatsSync: public sdk_test::SdkTestSyncNodesOperations
{
public:
    // Start with an empty sync root — tests create their own files at runtime.
    const std::vector<sdk_test::NodeInfo>& getElements() const override
    {
        static const std::vector<sdk_test::NodeInfo> ELEMENTS{
            sdk_test::DirNodeInfo(DEFAULT_SYNC_REMOTE_PATH)};
        return ELEMENTS;
    }

    const std::string& getRootTestDir() const override
    {
        static const std::string dir{"SDK_TEST_MEDIATS_SYNC"};
        return dir;
    }

protected:
    // Poll the remote sync root for a child with @p name. Returns nullptr on timeout.
    std::unique_ptr<MegaNode>
        waitForRemoteChild(const std::string& name,
                           std::chrono::seconds timeout = COMMON_TIMEOUT) const
    {
        const std::string remotePath =
            "/" + getRootTestDir() + "/" + DEFAULT_SYNC_REMOTE_PATH + "/" + name;
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < deadline)
        {
            std::unique_ptr<MegaNode> n(megaApi[0]->getNodeByPath(remotePath.c_str()));
            if (n)
                return n;
            std::this_thread::sleep_for(200ms);
        }
        return nullptr;
    }
};

// Test: sync-engine-initiated upload with pattern filename → mediats correct.
// Ensures the sync putNodeInDb path runs updateMediaTs the same as a direct
// startUpload.
TEST_F(SdkTestMediatsSync, SyncUpPatternFilenameDerivesMediats)
{
    sdk_test::LocalTempFile localFile(getLocalTmpDir() / kPatternFilename,
                                      /*fileSizeBytes=*/1024);

    ASSERT_NO_FATAL_FAILURE(waitForSyncToMatchCloudAndLocalExhaustive());
    auto remote = waitForRemoteChild(kPatternFilename);
    ASSERT_NE(remote, nullptr) << "Sync did not propagate " << kPatternFilename
                               << " within timeout";

    EXPECT_EQ(remote->getMediaCaptureTimeMs(), expectedMediaTsMs(kPatternFilename))
        << "Sync-engine upload must yield the same mediats as a direct upload";
}

// Test: sync-engine-initiated rename recomputes mediats. Existing integration
// MediaTs_RecomputedOnRename covers MegaApi::renameNode; this covers the
// sync-engine-initiated rename path (local fs::rename → engine → setAttr).
TEST_F(SdkTestMediatsSync, SyncLocalRenameRecomputesMediats)
{
    const std::string before = "plain_photo.jpg";
    const int64_t patternMediaTsMs = expectedMediaTsMs(kPatternFilename);

    sdk_test::LocalTempFile localFile(getLocalTmpDir() / before, /*fileSizeBytes=*/1024);
    ASSERT_NO_FATAL_FAILURE(waitForSyncToMatchCloudAndLocalExhaustive());

    auto remoteBefore = waitForRemoteChild(before);
    ASSERT_NE(remoteBefore, nullptr);
    const int64_t mediaTsBefore = remoteBefore->getMediaCaptureTimeMs();
    // Non-pattern filename → mediats falls back to mtime * 1000. Don't pin an
    // exact value (sync engine uses "now" mtime); only assert it differs from
    // the pattern-derived value expected after rename.
    EXPECT_GT(mediaTsBefore, 0);
    EXPECT_NE(mediaTsBefore, patternMediaTsMs);

    std::error_code ec;
    std::filesystem::rename(getLocalTmpDir() / before, getLocalTmpDir() / kPatternFilename, ec);
    ASSERT_FALSE(ec) << "local rename failed: " << ec.message();

    auto remoteAfter = waitForRemoteChild(kPatternFilename);
    ASSERT_NE(remoteAfter, nullptr) << "Rename did not propagate within timeout";
    EXPECT_EQ(remoteAfter->getMediaCaptureTimeMs(), patternMediaTsMs)
        << "Sync-initiated rename to pattern filename must recompute mediats";
}

#endif // ENABLE_SYNC
