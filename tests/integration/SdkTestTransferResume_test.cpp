/**
 * @file SdkTestTransferResume_test.cpp
 * @brief Integration tests for transfer behaviour across session restarts
 *        (session save → local logout → session resume).
 *
 * Covers:
 *   - SdkTestTransferPriorityRestore: transfer priorities survive a restart.
 *   - SdkTestTransfersResumedEvent:   EVENT_TRANSFERS_RESUMED fires exactly
 *     once after restart, carrying the unique IDs of all resumed transfers.
 *
 * Priority-restore bug description
 * ---------------------------------
 * When a transfer is saved to the transfer DB while still queued (no transfer
 * slot allocated yet), its Transfer::priority is correctly persisted by
 * Transfer::serialize.  However, when the session is resumed in
 * MegaClient::startxfer, the matching logic that looks up the cached Transfer
 * in multi_cachedtransfers requires:
 *
 *     For GET: downloadFileHandle == f->h && !downloadFileHandle.isUndef()
 *     For PUT: it->second->localfilename == f->getLocalname()
 *
 * Because 'downloadFileHandle' is only populated after the slot receives the
 * actual download URLs, queued-but-not-started downloads always have it set to
 * undef.  And 'localfilename' is only populated after a slot is assigned.
 * The match therefore fails, startxfer creates a brand-new Transfer with
 * priority 0, and addtransfer assigns a fresh sequential priority — the
 * original priority is lost.
 */

#include "sdk_test_utils.h"
#include "SdkTest_test.h"

#include <algorithm>
#include <map>
#include <mutex>
#include <set>

using namespace mega;

namespace
{

/**
 * Helper: wait until the transfer-list snapshot reported by the API contains
 * exactly the expected number of transfers of the given type.
 */
bool waitForTransferCount(MegaApi* api, int type, int expectedCount, unsigned timeoutMs = 30000)
{
    return SdkTest::WaitFor(
        [api, type, expectedCount]()
        {
            auto list = std::unique_ptr<MegaTransferList>{api->getTransfers(type)};
            return list && list->size() == expectedCount;
        },
        timeoutMs);
}

/**
 * Helper for DOWNLOAD transfers: collect {nodeHandle → priority}.
 *
 * For downloads, MegaTransfer::getNodeHandle() returns the cloud node handle
 * of the source file (set in MegaApiImpl::file_added when type == GET).  This
 * handle is unique per file and is stable across session restarts, making it
 * an ideal key.
 *
 * NOTE: do NOT use this for uploads. For upload transfers the cloud node does
 * not exist yet (the file hasn't been uploaded), so getNodeHandle() returns
 * UNDEF for every transfer regardless of which local file it refers to. Use
 * collectPathToPriority() for uploads instead.
 */
std::map<MegaHandle, unsigned long long> collectHandleToPriority(MegaApi* api, int type)
{
    std::map<MegaHandle, unsigned long long> result;
    auto list = std::unique_ptr<MegaTransferList>{api->getTransfers(type)};
    if (!list)
        return result;
    for (int i = 0; i < list->size(); ++i)
    {
        auto* t = list->get(i);
        result[t->getNodeHandle()] = t->getPriority();
    }
    return result;
}

/**
 * Helper for UPLOAD transfers: collect {localPath → priority}.
 *
 * For uploads, the cloud node handle is UNDEF until the upload completes, so
 * it cannot serve as a unique key. The local file path (MegaTransfer::getPath)
 * is set from File::logicalPath() in MegaApiImpl::file_added and is restored to
 * the same value when the transfer is resumed from DB after a restart.  It is
 * therefore a stable, unique key for queued upload transfers.
 */
std::map<std::string, unsigned long long> collectPathToPriority(MegaApi* api, int type)
{
    std::map<std::string, unsigned long long> result;
    auto list = std::unique_ptr<MegaTransferList>{api->getTransfers(type)};
    if (!list)
        return result;
    for (int i = 0; i < list->size(); ++i)
    {
        auto* t = list->get(i);
        const char* path = t->getPath();
        if (path && *path != '\0')
            result[path] = t->getPriority();
    }
    return result;
}

} // anonymous namespace

class SdkTestTransferPriorityRestore: public SdkTest
{
public:
    void SetUp() override
    {
        SdkTest::SetUp();
        ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));
    }

    void TearDown() override
    {
        if (megaApi[0] && megaApi[0]->isLoggedIn())
        {
            megaApi[0]->pauseTransfers(false, MegaTransfer::TYPE_UPLOAD);
            megaApi[0]->pauseTransfers(false, MegaTransfer::TYPE_DOWNLOAD);
            synchronousCancelTransfers(0, MegaTransfer::TYPE_UPLOAD);
            synchronousCancelTransfers(0, MegaTransfer::TYPE_DOWNLOAD);

            for (const auto handle: mCloudNodesToDelete)
            {
                auto node = std::unique_ptr<MegaNode>{megaApi[0]->getNodeByHandle(handle)};
                if (node)
                    doDeleteNode(0, node.get());
            }
        }
        SdkTest::TearDown();
    }

protected:
    std::vector<MegaHandle> mCloudNodesToDelete;
};

// Test 1: Upload priorities are preserved after a session restart.
TEST_F(SdkTestTransferPriorityRestore, upload_priorities_preserved_after_restart)
{
    constexpr int NUM_FILES = 3;
    constexpr size_t FILE_SIZE = 64;

    auto rootNode = std::unique_ptr<MegaNode>{megaApi[0]->getRootNode()};
    ASSERT_TRUE(rootNode) << "Cannot get root node";

    // Create local files.
    std::vector<sdk_test::LocalTempFile> localFiles;
    localFiles.reserve(static_cast<size_t>(NUM_FILES));
    for (int i = 0; i < NUM_FILES; ++i)
    {
        localFiles.emplace_back(fs::current_path() / (getFilePrefix() + std::to_string(i) + ".bin"),
                                FILE_SIZE);
    }

    // Pause all uploads so they queue up without being scheduled.
    megaApi[0]->pauseTransfers(true, MegaTransfer::TYPE_UPLOAD);

    // Start the uploads while paused.
    std::vector<std::unique_ptr<TransferTracker>> trackers;
    trackers.reserve(static_cast<size_t>(NUM_FILES));
    MegaUploadOptions opts;
    opts.mtime = MegaApi::INVALID_CUSTOM_MOD_TIME;
    for (int i = 0; i < NUM_FILES; ++i)
    {
        trackers.push_back(std::make_unique<TransferTracker>(megaApi[0].get()));
        megaApi[0]->startUpload(localFiles[static_cast<size_t>(i)].getPath().string().c_str(),
                                rootNode.get(),
                                nullptr,
                                &opts,
                                trackers.back().get());
    }

    // Wait for all uploads to appear in the queue.
    ASSERT_TRUE(waitForTransferCount(megaApi[0].get(), MegaTransfer::TYPE_UPLOAD, NUM_FILES, 30000))
        << "Uploads did not appear in the queue within timeout";

    // Record priorities before restart, keyed by local file path
    auto prioritiesBefore = collectPathToPriority(megaApi[0].get(), MegaTransfer::TYPE_UPLOAD);
    ASSERT_EQ(static_cast<int>(prioritiesBefore.size()), NUM_FILES)
        << "Expected " << NUM_FILES
        << " distinct upload entries keyed by local path; "
           "if this fails the helper may not see a valid getPath() for each transfer";

    // Verify they have distinct, non-zero priorities.
    {
        std::vector<unsigned long long> pVals;
        pVals.reserve(prioritiesBefore.size());
        for (auto& [localPath, p]: prioritiesBefore)
            pVals.push_back(p);
        std::sort(pVals.begin(), pVals.end());
        ASSERT_EQ(std::unique(pVals.begin(), pVals.end()), pVals.end())
            << "Priorities before restart must all be distinct";
        for (auto p: pVals)
            ASSERT_GT(p, 0u) << "Priority must be non-zero";
    }

    // Save session and do a local logout (simulate app restart).
    std::unique_ptr<char[]> session(dumpSession());
    ASSERT_TRUE(session) << "Cannot dump session";
    ASSERT_NO_FATAL_FAILURE(locallogout());

    // Resume session (app restart).
    // Pause uploads before fetchnodes so that when the SDK restores transfers
    // from the transfer DB during fetchnodes, they are queued but not dispatched.
    // Calling pauseTransfers after fetchnodes would be racy: on a fast network the
    // restored transfers could complete in the window between fetchnodes returning
    // and the pause taking effect, causing waitForTransferCount to time out.
    ASSERT_NO_FATAL_FAILURE(resumeSession(session.get()));
    megaApi[0]->pauseTransfers(true, MegaTransfer::TYPE_UPLOAD);
    ASSERT_NO_FATAL_FAILURE(fetchnodes(0));

    // Wait for resumed uploads to appear.
    ASSERT_TRUE(waitForTransferCount(megaApi[0].get(), MegaTransfer::TYPE_UPLOAD, NUM_FILES, 30000))
        << "Resumed uploads did not appear within timeout";

    // Verify priorities are unchanged.
    auto prioritiesAfter = collectPathToPriority(megaApi[0].get(), MegaTransfer::TYPE_UPLOAD);
    for (auto& [localPath, priorityBefore]: prioritiesBefore)
    {
        ASSERT_TRUE(prioritiesAfter.count(localPath))
            << "Upload transfer for path '" << localPath << "' missing after restart";
        EXPECT_EQ(prioritiesAfter.at(localPath), priorityBefore)
            << "Upload priority changed after restart for path '" << localPath << "'";
    }

    // Cancel remaining uploads.
    megaApi[0]->pauseTransfers(false, MegaTransfer::TYPE_UPLOAD);
    ASSERT_EQ(API_OK, synchronousCancelTransfers(0, MegaTransfer::TYPE_UPLOAD));
}

// Test 2: Download priorities are preserved after restart when the downloads
//         were queued but NEVER started (downloadFileHandle stays undef).
TEST_F(SdkTestTransferPriorityRestore, queued_download_priorities_preserved_after_restart)
{
    constexpr int NUM_FILES = 3;
    constexpr size_t FILE_SIZE = 64;

    auto rootNode = std::unique_ptr<MegaNode>{megaApi[0]->getRootNode()};
    ASSERT_TRUE(rootNode) << "Cannot get root node";

    // Upload source files to have nodes to download.
    std::vector<sdk_test::LocalTempFile> srcFiles;
    srcFiles.reserve(static_cast<size_t>(NUM_FILES));
    std::vector<MegaHandle> nodeHandles(static_cast<size_t>(NUM_FILES), UNDEF);
    for (int i = 0; i < NUM_FILES; ++i)
    {
        srcFiles.emplace_back(fs::current_path() /
                                  (getFilePrefix() + "src_" + std::to_string(i) + ".bin"),
                              FILE_SIZE);
        ASSERT_EQ(API_OK,
                  doStartUpload(0,
                                &nodeHandles[static_cast<size_t>(i)],
                                srcFiles[static_cast<size_t>(i)].getPath().string().c_str(),
                                rootNode.get(),
                                nullptr,
                                MegaApi::INVALID_CUSTOM_MOD_TIME,
                                nullptr,
                                false,
                                false,
                                nullptr))
            << "Failed to upload source file " << i;
        ASSERT_NE(UNDEF, nodeHandles[static_cast<size_t>(i)]);
        mCloudNodesToDelete.push_back(nodeHandles[static_cast<size_t>(i)]);
    }

    // Pause all downloads before starting them.
    // This ensures no slot is ever allocated, so downloadFileHandle stays undef.
    megaApi[0]->pauseTransfers(true, MegaTransfer::TYPE_DOWNLOAD);

    sdk_test::LocalTempDir downloadDir(fs::current_path() / (getFilePrefix() + "dl"));

    std::vector<std::unique_ptr<TransferTracker>> dlTrackers;
    dlTrackers.reserve(static_cast<size_t>(NUM_FILES));
    for (int i = 0; i < NUM_FILES; ++i)
    {
        auto node = std::unique_ptr<MegaNode>{
            megaApi[0]->getNodeByHandle(nodeHandles[static_cast<size_t>(i)])};
        ASSERT_TRUE(node) << "Cannot get node " << i;

        dlTrackers.push_back(std::make_unique<TransferTracker>(megaApi[0].get()));
        megaApi[0]->startDownload(
            node.get(),
            (downloadDir.getPath().string() + LocalPath::localPathSeparator_utf8).c_str(),
            nullptr,
            nullptr,
            false,
            nullptr,
            MegaTransfer::COLLISION_CHECK_FINGERPRINT,
            MegaTransfer::COLLISION_RESOLUTION_NEW_WITH_N,
            false,
            dlTrackers.back().get());
    }

    // Wait until all downloads are visible in the queue.
    ASSERT_TRUE(
        waitForTransferCount(megaApi[0].get(), MegaTransfer::TYPE_DOWNLOAD, NUM_FILES, 30000))
        << "Downloads did not appear in the queue within timeout";

    // Record priorities before restart, keyed by node handle (stable across restart).
    auto prioritiesBefore = collectHandleToPriority(megaApi[0].get(), MegaTransfer::TYPE_DOWNLOAD);
    ASSERT_EQ(static_cast<int>(prioritiesBefore.size()), NUM_FILES);

    // All priorities must be distinct and non-zero.
    {
        std::vector<unsigned long long> pVals;
        pVals.reserve(prioritiesBefore.size());
        for (auto& [h, p]: prioritiesBefore)
            pVals.push_back(p);
        std::sort(pVals.begin(), pVals.end());
        ASSERT_EQ(std::unique(pVals.begin(), pVals.end()), pVals.end())
            << "Priorities before restart must all be distinct";
        for (auto p: pVals)
            ASSERT_GT(p, 0u) << "Priority must be non-zero";
    }

    // Simulate app restart: save session, local logout, resume session.
    std::unique_ptr<char[]> session(dumpSession());
    ASSERT_TRUE(session) << "Cannot dump session";
    ASSERT_NO_FATAL_FAILURE(locallogout());

    // Pause downloads before fetchnodes so that restored transfers from the
    // transfer DB are queued but not dispatched. See upload_priorities test
    // for a detailed explanation of the race this avoids.
    ASSERT_NO_FATAL_FAILURE(resumeSession(session.get()));
    megaApi[0]->pauseTransfers(true, MegaTransfer::TYPE_DOWNLOAD);
    ASSERT_NO_FATAL_FAILURE(fetchnodes(0));

    // Wait for the resumed downloads to appear in the queue.
    ASSERT_TRUE(
        waitForTransferCount(megaApi[0].get(), MegaTransfer::TYPE_DOWNLOAD, NUM_FILES, 30000))
        << "Resumed downloads did not appear within timeout. "
           "The transfers may not have been persisted, or they were discarded on resume.";

    // Collect priorities after restart.
    auto prioritiesAfter = collectHandleToPriority(megaApi[0].get(), MegaTransfer::TYPE_DOWNLOAD);

    // Each download has the same priority as before the restart.
    for (auto& [handle, priorityBefore]: prioritiesBefore)
    {
        ASSERT_TRUE(prioritiesAfter.count(handle))
            << "Download transfer for node " << handle << " is missing after restart";

        EXPECT_EQ(prioritiesAfter.at(handle), priorityBefore)
            << "BUG: Download priority changed after restart for node " << handle
            << ". Before=" << priorityBefore << " After=" << prioritiesAfter.at(handle)
            << ". Queued downloads without a transfer slot lose their priority because "
               "startxfer cannot match them in multi_cachedtransfers when "
               "downloadFileHandle is undef.";
    }

    // Cancel pending downloads.
    megaApi[0]->pauseTransfers(false, MegaTransfer::TYPE_DOWNLOAD);
    ASSERT_EQ(API_OK, synchronousCancelTransfers(0, MegaTransfer::TYPE_DOWNLOAD));
}

// Test 3: Relative priority order of queued downloads is preserved after restart.
TEST_F(SdkTestTransferPriorityRestore, queued_download_priority_order_preserved_after_restart)
{
    constexpr int NUM_FILES = 4;
    constexpr size_t FILE_SIZE = 64;

    auto rootNode = std::unique_ptr<MegaNode>{megaApi[0]->getRootNode()};
    ASSERT_TRUE(rootNode) << "Cannot get root node";

    // Upload source files.
    std::vector<sdk_test::LocalTempFile> srcFiles;
    srcFiles.reserve(static_cast<size_t>(NUM_FILES));
    std::vector<MegaHandle> nodeHandles(static_cast<size_t>(NUM_FILES), UNDEF);
    for (int i = 0; i < NUM_FILES; ++i)
    {
        srcFiles.emplace_back(fs::current_path() /
                                  (getFilePrefix() + "src_" + std::to_string(i) + ".bin"),
                              FILE_SIZE);
        ASSERT_EQ(API_OK,
                  doStartUpload(0,
                                &nodeHandles[static_cast<size_t>(i)],
                                srcFiles[static_cast<size_t>(i)].getPath().string().c_str(),
                                rootNode.get(),
                                nullptr,
                                MegaApi::INVALID_CUSTOM_MOD_TIME,
                                nullptr,
                                false,
                                false,
                                nullptr))
            << "Failed to upload source file " << i;
        ASSERT_NE(UNDEF, nodeHandles[static_cast<size_t>(i)]);
        mCloudNodesToDelete.push_back(nodeHandles[static_cast<size_t>(i)]);
    }

    // Pause downloads before starting them.
    megaApi[0]->pauseTransfers(true, MegaTransfer::TYPE_DOWNLOAD);

    sdk_test::LocalTempDir downloadDir(fs::current_path() / (getFilePrefix() + "dl"));

    std::vector<std::unique_ptr<TransferTracker>> dlTrackers;
    dlTrackers.reserve(static_cast<size_t>(NUM_FILES));
    for (int i = 0; i < NUM_FILES; ++i)
    {
        auto node = std::unique_ptr<MegaNode>{
            megaApi[0]->getNodeByHandle(nodeHandles[static_cast<size_t>(i)])};
        ASSERT_TRUE(node);

        dlTrackers.push_back(std::make_unique<TransferTracker>(megaApi[0].get()));
        megaApi[0]->startDownload(
            node.get(),
            (downloadDir.getPath().string() + LocalPath::localPathSeparator_utf8).c_str(),
            nullptr,
            nullptr,
            false,
            nullptr,
            MegaTransfer::COLLISION_CHECK_FINGERPRINT,
            MegaTransfer::COLLISION_RESOLUTION_NEW_WITH_N,
            false,
            dlTrackers.back().get());
    }

    ASSERT_TRUE(
        waitForTransferCount(megaApi[0].get(), MegaTransfer::TYPE_DOWNLOAD, NUM_FILES, 30000))
        << "Downloads did not appear in the queue";

    // Build ordered list [handle, priority] sorted ascending by priority.
    struct HndPri
    {
        MegaHandle handle;
        unsigned long long priority;
    };

    auto buildOrderedList = [](MegaApi* api, int type) -> std::vector<HndPri>
    {
        std::vector<HndPri> vec;
        auto list = std::unique_ptr<MegaTransferList>{api->getTransfers(type)};
        if (!list)
            return vec;
        for (int i = 0; i < list->size(); ++i)
        {
            auto* t = list->get(i);
            vec.push_back({t->getNodeHandle(), t->getPriority()});
        }
        std::sort(vec.begin(),
                  vec.end(),
                  [](const HndPri& a, const HndPri& b)
                  {
                      return a.priority < b.priority;
                  });
        return vec;
    };

    auto orderBefore = buildOrderedList(megaApi[0].get(), MegaTransfer::TYPE_DOWNLOAD);
    ASSERT_EQ(static_cast<int>(orderBefore.size()), NUM_FILES);

    // Simulate app restart.
    // Pause downloads before fetchnodes to avoid the race described in the
    // upload_priorities test: restored DB transfers must not be dispatched
    // in the window between fetchnodes returning and the pause taking effect.
    std::unique_ptr<char[]> session(dumpSession());
    ASSERT_TRUE(session);
    ASSERT_NO_FATAL_FAILURE(locallogout());
    ASSERT_NO_FATAL_FAILURE(resumeSession(session.get()));
    megaApi[0]->pauseTransfers(true, MegaTransfer::TYPE_DOWNLOAD);
    ASSERT_NO_FATAL_FAILURE(fetchnodes(0));

    ASSERT_TRUE(
        waitForTransferCount(megaApi[0].get(), MegaTransfer::TYPE_DOWNLOAD, NUM_FILES, 30000))
        << "Resumed downloads did not appear after restart";

    auto orderAfter = buildOrderedList(megaApi[0].get(), MegaTransfer::TYPE_DOWNLOAD);
    ASSERT_EQ(orderBefore.size(), orderAfter.size());

    // Sorted handle order is identical (relative priority preserved).
    for (size_t i = 0; i < orderBefore.size(); ++i)
    {
        EXPECT_EQ(orderBefore[i].handle, orderAfter[i].handle)
            << "BUG: Relative download priority order changed after restart at position " << i
            << ". Before: handle=" << orderBefore[i].handle
            << " priority=" << orderBefore[i].priority << "; After: handle=" << orderAfter[i].handle
            << " priority=" << orderAfter[i].priority;
    }

    megaApi[0]->pauseTransfers(false, MegaTransfer::TYPE_DOWNLOAD);
    ASSERT_EQ(API_OK, synchronousCancelTransfers(0, MegaTransfer::TYPE_DOWNLOAD));
}

// ---------------------------------------------------------------------------
// SdkTestTransfersResumedEvent
// Tests for EVENT_TRANSFERS_RESUMED (SDK-5093)
// ---------------------------------------------------------------------------

namespace
{

/**
 * Listener that captures EVENT_TRANSFERS_RESUMED payloads.
 * Thread-safe; call reset() before the restart sequence.
 */
class TransfersResumedListener: public MegaListener
{
public:
    void onEvent(MegaApi*, MegaEvent* event) override
    {
        if (!event || event->getType() != MegaEvent::EVENT_TRANSFERS_RESUMED)
            return;

        std::lock_guard<std::mutex> lock(mMutex);
        ++mFiredCount;
        if (const MegaIntegerList* list = event->getIntegerList())
        {
            for (int i = 0; i < list->size(); ++i)
                mIds.insert(static_cast<uint32_t>(list->get(i)));
        }
    }

    void reset()
    {
        std::lock_guard<std::mutex> lock(mMutex);
        mFiredCount = 0;
        mIds.clear();
    }

    int firedCount() const
    {
        std::lock_guard<std::mutex> lock(mMutex);
        return mFiredCount;
    }

    std::set<uint32_t> ids() const
    {
        std::lock_guard<std::mutex> lock(mMutex);
        return mIds;
    }

private:
    mutable std::mutex mMutex;
    int mFiredCount = 0;
    std::set<uint32_t> mIds;
};

std::set<uint32_t> collectUniqueIds(MegaApi* api, int type)
{
    std::set<uint32_t> result;
    auto list = std::unique_ptr<MegaTransferList>{api->getTransfers(type)};
    if (!list)
        return result;
    for (int i = 0; i < list->size(); ++i)
        result.insert(list->get(i)->getUniqueId());
    return result;
}

} // anonymous namespace

class SdkTestTransfersResumedEvent: public SdkTest
{
public:
    void SetUp() override
    {
        SdkTest::SetUp();
        ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));
        megaApi[0]->addListener(&mListener);
    }

    void TearDown() override
    {
        megaApi[0]->removeListener(&mListener);
        if (megaApi[0] && megaApi[0]->isLoggedIn())
        {
            megaApi[0]->pauseTransfers(false, MegaTransfer::TYPE_UPLOAD);
            megaApi[0]->pauseTransfers(false, MegaTransfer::TYPE_DOWNLOAD);
            synchronousCancelTransfers(0, MegaTransfer::TYPE_UPLOAD);
            synchronousCancelTransfers(0, MegaTransfer::TYPE_DOWNLOAD);
            for (MegaHandle h: mNodesToDelete)
            {
                auto node = std::unique_ptr<MegaNode>{megaApi[0]->getNodeByHandle(h)};
                if (node)
                    doDeleteNode(0, node.get());
            }
        }
        SdkTest::TearDown();
    }

protected:
    TransfersResumedListener mListener;
    std::vector<MegaHandle> mNodesToDelete;
};

// EVENT_TRANSFERS_RESUMED fires exactly once after restart, and the IDs
// in the event match the unique IDs of the resumed upload transfers.
TEST_F(SdkTestTransfersResumedEvent, event_fired_with_resumed_upload_ids)
{
    constexpr int NUM_FILES = 3;
    constexpr size_t FILE_SIZE = 64;

    auto rootNode = std::unique_ptr<MegaNode>{megaApi[0]->getRootNode()};
    ASSERT_TRUE(rootNode);

    std::vector<sdk_test::LocalTempFile> localFiles;
    localFiles.reserve(NUM_FILES);
    for (int i = 0; i < NUM_FILES; ++i)
        localFiles.emplace_back(fs::current_path() / (getFilePrefix() + std::to_string(i) + ".bin"),
                                FILE_SIZE);

    // Pause so the uploads queue up without being dispatched (ensuring they
    // are written to the transfer DB before the restart).
    megaApi[0]->pauseTransfers(true, MegaTransfer::TYPE_UPLOAD);

    MegaUploadOptions opts;
    opts.mtime = MegaApi::INVALID_CUSTOM_MOD_TIME;
    for (size_t i = 0; i < NUM_FILES; ++i)
        megaApi[0]->startUpload(localFiles[i].getPath().string().c_str(),
                                rootNode.get(),
                                nullptr,
                                &opts,
                                nullptr);

    ASSERT_TRUE(waitForTransferCount(megaApi[0].get(),
                                     MegaTransfer::TYPE_UPLOAD,
                                     NUM_FILES,
                                     defaultTimeoutMs))
        << "Uploads did not appear in the queue within timeout";

    // Simulate app restart.
    std::unique_ptr<char[]> session(dumpSession());
    ASSERT_TRUE(session);
    ASSERT_NO_FATAL_FAILURE(locallogout());

    mListener.reset();
    ASSERT_NO_FATAL_FAILURE(resumeSession(session.get()));
    // Pause before fetchnodes to prevent resumed transfers from completing
    // in the window between fetchnodes returning and our checks running.
    megaApi[0]->pauseTransfers(true, MegaTransfer::TYPE_UPLOAD);
    ASSERT_NO_FATAL_FAILURE(fetchnodes(0));

    ASSERT_TRUE(WaitFor(
        [this]
        {
            return mListener.firedCount() >= 1;
        },
        defaultTimeoutMs))
        << "EVENT_TRANSFERS_RESUMED was not fired after session restart";

    EXPECT_EQ(mListener.firedCount(), 1) << "EVENT_TRANSFERS_RESUMED must fire exactly once";

    // Wait for the resumed transfers to appear in the queue.
    ASSERT_TRUE(waitForTransferCount(megaApi[0].get(),
                                     MegaTransfer::TYPE_UPLOAD,
                                     NUM_FILES,
                                     defaultTimeoutMs))
        << "Resumed uploads did not appear within timeout";

    const auto eventIds = mListener.ids();
    EXPECT_EQ(static_cast<int>(eventIds.size()), NUM_FILES)
        << "Event must contain exactly " << NUM_FILES << " unique IDs";

    const auto queueIds = collectUniqueIds(megaApi[0].get(), MegaTransfer::TYPE_UPLOAD);
    for (uint32_t id: eventIds)
        EXPECT_TRUE(queueIds.count(id))
            << "Event ID " << id << " does not correspond to any queued upload transfer";

    megaApi[0]->pauseTransfers(false, MegaTransfer::TYPE_UPLOAD);
    ASSERT_EQ(API_OK, synchronousCancelTransfers(0, MegaTransfer::TYPE_UPLOAD));
}

// EVENT_TRANSFERS_RESUMED must NOT fire when there are no cached transfers.
TEST_F(SdkTestTransfersResumedEvent, no_event_when_no_transfers_to_resume)
{
    std::unique_ptr<char[]> session(dumpSession());
    ASSERT_TRUE(session);
    ASSERT_NO_FATAL_FAILURE(locallogout());

    mListener.reset();
    ASSERT_NO_FATAL_FAILURE(resumeSession(session.get()));
    ASSERT_NO_FATAL_FAILURE(fetchnodes(0));

    // Allow time for any spurious event to arrive before asserting silence.
    WaitMillisec(3000);
    EXPECT_EQ(mListener.firedCount(), 0)
        << "EVENT_TRANSFERS_RESUMED must not fire when there are no cached transfers";
}

// A single EVENT_TRANSFERS_RESUMED covers both upload and download transfers,
// and every ID in the event corresponds to a resumed transfer.
TEST_F(SdkTestTransfersResumedEvent, event_covers_uploads_and_downloads)
{
    constexpr int NUM_EACH = 2;
    constexpr size_t FILE_SIZE = 64;

    auto rootNode = std::unique_ptr<MegaNode>{megaApi[0]->getRootNode()};
    ASSERT_TRUE(rootNode);

    // Upload source files for the download side (these run to completion).
    std::vector<sdk_test::LocalTempFile> srcFiles;
    srcFiles.reserve(NUM_EACH);
    std::vector<MegaHandle> nodeHandles(NUM_EACH, UNDEF);
    for (size_t i = 0; i < NUM_EACH; ++i)
    {
        srcFiles.emplace_back(fs::current_path() /
                                  (getFilePrefix() + "src_" + std::to_string(i) + ".bin"),
                              FILE_SIZE);
        ASSERT_EQ(API_OK,
                  doStartUpload(0,
                                &nodeHandles[i],
                                srcFiles[i].getPath().string().c_str(),
                                rootNode.get(),
                                nullptr,
                                MegaApi::INVALID_CUSTOM_MOD_TIME,
                                nullptr,
                                false,
                                false,
                                nullptr))
            << "Failed to upload source file " << i;
        ASSERT_NE(UNDEF, nodeHandles[i]);
        mNodesToDelete.push_back(nodeHandles[i]);
    }

    // Pause both directions before queuing so all transfers are saved to DB.
    megaApi[0]->pauseTransfers(true, MegaTransfer::TYPE_UPLOAD);
    megaApi[0]->pauseTransfers(true, MegaTransfer::TYPE_DOWNLOAD);

    // Queue uploads.
    std::vector<sdk_test::LocalTempFile> uploadFiles;
    uploadFiles.reserve(NUM_EACH);
    MegaUploadOptions opts;
    opts.mtime = MegaApi::INVALID_CUSTOM_MOD_TIME;
    for (size_t i = 0; i < NUM_EACH; ++i)
    {
        uploadFiles.emplace_back(fs::current_path() /
                                     (getFilePrefix() + "up_" + std::to_string(i) + ".bin"),
                                 FILE_SIZE);
        megaApi[0]->startUpload(uploadFiles[i].getPath().string().c_str(),
                                rootNode.get(),
                                nullptr,
                                &opts,
                                nullptr);
    }

    // Queue downloads.
    sdk_test::LocalTempDir downloadDir(fs::current_path() / (getFilePrefix() + "dl"));
    for (size_t i = 0; i < NUM_EACH; ++i)
    {
        auto node = std::unique_ptr<MegaNode>{megaApi[0]->getNodeByHandle(nodeHandles[i])};
        ASSERT_TRUE(node);
        megaApi[0]->startDownload(
            node.get(),
            (downloadDir.getPath().string() + LocalPath::localPathSeparator_utf8).c_str(),
            nullptr,
            nullptr,
            false,
            nullptr,
            MegaTransfer::COLLISION_CHECK_FINGERPRINT,
            MegaTransfer::COLLISION_RESOLUTION_NEW_WITH_N,
            false,
            nullptr);
    }

    ASSERT_TRUE(WaitFor(
        [this]
        {
            auto ul = std::unique_ptr<MegaTransferList>{
                megaApi[0]->getTransfers(MegaTransfer::TYPE_UPLOAD)};
            auto dl = std::unique_ptr<MegaTransferList>{
                megaApi[0]->getTransfers(MegaTransfer::TYPE_DOWNLOAD)};
            return ul && dl && ul->size() == NUM_EACH && dl->size() == NUM_EACH;
        },
        defaultTimeoutMs))
        << "Transfers did not appear in the queue within timeout";

    // Simulate restart.
    std::unique_ptr<char[]> session(dumpSession());
    ASSERT_TRUE(session);
    ASSERT_NO_FATAL_FAILURE(locallogout());

    mListener.reset();
    ASSERT_NO_FATAL_FAILURE(resumeSession(session.get()));
    megaApi[0]->pauseTransfers(true, MegaTransfer::TYPE_UPLOAD);
    megaApi[0]->pauseTransfers(true, MegaTransfer::TYPE_DOWNLOAD);
    ASSERT_NO_FATAL_FAILURE(fetchnodes(0));

    ASSERT_TRUE(WaitFor(
        [this]
        {
            return mListener.firedCount() >= 1;
        },
        defaultTimeoutMs))
        << "EVENT_TRANSFERS_RESUMED was not fired after session restart";

    EXPECT_EQ(mListener.firedCount(), 1) << "EVENT_TRANSFERS_RESUMED must fire exactly once";

    const auto eventIds = mListener.ids();
    EXPECT_EQ(static_cast<int>(eventIds.size()), NUM_EACH * 2)
        << "Event must contain IDs for both the uploads and the downloads";

    // Wait for resumed transfers and verify every event ID maps to a real transfer.
    ASSERT_TRUE(WaitFor(
        [this]
        {
            auto ul = std::unique_ptr<MegaTransferList>{
                megaApi[0]->getTransfers(MegaTransfer::TYPE_UPLOAD)};
            auto dl = std::unique_ptr<MegaTransferList>{
                megaApi[0]->getTransfers(MegaTransfer::TYPE_DOWNLOAD)};
            return ul && dl && ul->size() == NUM_EACH && dl->size() == NUM_EACH;
        },
        defaultTimeoutMs))
        << "Resumed transfers did not appear within timeout";

    auto allQueueIds = collectUniqueIds(megaApi[0].get(), MegaTransfer::TYPE_UPLOAD);
    const auto dlIds = collectUniqueIds(megaApi[0].get(), MegaTransfer::TYPE_DOWNLOAD);
    allQueueIds.insert(dlIds.begin(), dlIds.end());

    for (uint32_t id: eventIds)
        EXPECT_TRUE(allQueueIds.count(id))
            << "Event ID " << id << " does not correspond to any resumed transfer";

    megaApi[0]->pauseTransfers(false, MegaTransfer::TYPE_UPLOAD);
    megaApi[0]->pauseTransfers(false, MegaTransfer::TYPE_DOWNLOAD);
    ASSERT_EQ(API_OK, synchronousCancelTransfers(0, MegaTransfer::TYPE_UPLOAD));
    ASSERT_EQ(API_OK, synchronousCancelTransfers(0, MegaTransfer::TYPE_DOWNLOAD));
}

// EVENT_TRANSFERS_RESUMED must report the cached transfer ID when resume completes
// via remote copy (same fingerprint, different remote name).
TEST_F(SdkTestTransfersResumedEvent, event_fired_for_remote_copy_on_resume)
{
    constexpr size_t FILE_SIZE = 64;
    const std::string kSourceName{"source.bin"};
    const std::string kCopyName{"remote_copy.bin"};
    const std::string kFolder1Name{"remote_copy_resume_folder1"};
    const std::string kFolder2Name{"remote_copy_resume_folder2"};

    auto rootNode = makeUniqueFrom(megaApi[0]->getRootNode());
    ASSERT_TRUE(rootNode);

    auto [errCode1, folder1Handle] = createRemoteFolder(0, kFolder1Name.c_str(), rootNode.get());
    ASSERT_EQ(errCode1, API_OK);
    mNodesToDelete.push_back(folder1Handle);

    auto [errCode2, folder2Handle] = createRemoteFolder(0, kFolder2Name.c_str(), rootNode.get());
    ASSERT_EQ(errCode2, API_OK);
    mNodesToDelete.push_back(folder2Handle);

    auto folder1Node = makeUniqueFrom(megaApi[0]->getNodeByHandle(folder1Handle));
    auto folder2Node = makeUniqueFrom(megaApi[0]->getNodeByHandle(folder2Handle));
    ASSERT_TRUE(folder1Node);
    ASSERT_TRUE(folder2Node);

    sdk_test::LocalTempFile localFile(fs::current_path() /
                                          (getFilePrefix() + "remote_copy_resume.bin"),
                                      FILE_SIZE);

    // Queue an upload before any matching fingerprint exists in the cloud.
    megaApi[0]->pauseTransfers(true, MegaTransfer::TYPE_UPLOAD);

    MegaUploadOptions opts;
    opts.fileName = kCopyName;
    opts.mtime = MegaApi::INVALID_CUSTOM_MOD_TIME;
    megaApi[0]->startUpload(localFile.getPath().string().c_str(),
                            folder1Node.get(),
                            nullptr,
                            &opts,
                            nullptr);

    ASSERT_TRUE(
        waitForTransferCount(megaApi[0].get(), MegaTransfer::TYPE_UPLOAD, 1, defaultTimeoutMs))
        << "Queued upload did not appear within timeout";

    auto transferList = makeUniqueFrom(megaApi[0]->getTransfers(MegaTransfer::TYPE_UPLOAD));
    ASSERT_TRUE(transferList && transferList->size() == 1);
    const uint32_t queuedUniqueId = transferList->get(0)->getUniqueId();
    ASSERT_NE(queuedUniqueId, 0u);

    std::unique_ptr<char[]> session(dumpSession());
    ASSERT_TRUE(session);
    ASSERT_NO_FATAL_FAILURE(locallogout());

    // First restart: restore the queued upload, then add a same-fingerprint source elsewhere.
    mListener.reset();
    ASSERT_NO_FATAL_FAILURE(resumeSession(session.get()));
    megaApi[0]->pauseTransfers(true, MegaTransfer::TYPE_UPLOAD);
    ASSERT_NO_FATAL_FAILURE(fetchnodes(0));

    ASSERT_TRUE(
        waitForTransferCount(megaApi[0].get(), MegaTransfer::TYPE_UPLOAD, 1, defaultTimeoutMs))
        << "Resumed upload did not reappear within timeout";

    transferList.reset(megaApi[0]->getTransfers(MegaTransfer::TYPE_UPLOAD));
    ASSERT_TRUE(transferList && transferList->size() == 1);
    auto copyTransfer = makeUniqueFrom(transferList->get(0)->copy());

    mApi[0].requestFlags[MegaRequest::TYPE_PAUSE_TRANSFER] = false;
    megaApi[0]->pauseTransfer(copyTransfer.get(), true);
    ASSERT_TRUE(waitForResponse(&mApi[0].requestFlags[MegaRequest::TYPE_PAUSE_TRANSFER]))
        << "Failed to keep the copy upload paused individually";

    mApi[0].requestFlags[MegaRequest::TYPE_PAUSE_TRANSFERS] = false;
    megaApi[0]->pauseTransfers(false, MegaTransfer::TYPE_UPLOAD);
    ASSERT_TRUE(waitForResponse(&mApi[0].requestFlags[MegaRequest::TYPE_PAUSE_TRANSFERS]))
        << "Failed to unpause uploads globally";

    MegaHandle sourceHandle = UNDEF;
    ASSERT_EQ(API_OK,
              doStartUpload(0,
                            &sourceHandle,
                            localFile.getPath().string().c_str(),
                            folder2Node.get(),
                            kSourceName.c_str(),
                            MegaApi::INVALID_CUSTOM_MOD_TIME,
                            nullptr,
                            false,
                            false,
                            nullptr));
    ASSERT_NE(sourceHandle, UNDEF);

    ASSERT_TRUE(
        waitForTransferCount(megaApi[0].get(), MegaTransfer::TYPE_UPLOAD, 1, defaultTimeoutMs))
        << "Copy upload must remain queued while source upload completes";

    megaApi[0]->pauseTransfers(true, MegaTransfer::TYPE_UPLOAD);

    session.reset(dumpSession());
    ASSERT_TRUE(session);
    ASSERT_NO_FATAL_FAILURE(locallogout());

    // Second restart: resume must complete via remote copy and report the cached ID.
    mListener.reset();
    ASSERT_NO_FATAL_FAILURE(resumeSession(session.get()));
    ASSERT_NO_FATAL_FAILURE(fetchnodes(0));

    ASSERT_TRUE(WaitFor(
        [this]
        {
            return mListener.firedCount() >= 1;
        },
        defaultTimeoutMs))
        << "EVENT_TRANSFERS_RESUMED was not fired after remote-copy resume";

    EXPECT_EQ(mListener.firedCount(), 1) << "EVENT_TRANSFERS_RESUMED must fire exactly once";

    const auto eventIds = mListener.ids();
    ASSERT_EQ(eventIds.size(), 1u) << "Event must contain exactly one resumed transfer ID";
    EXPECT_EQ(*eventIds.begin(), queuedUniqueId)
        << "Event ID must match the cached transfer unique ID from before restart";

    ASSERT_TRUE(
        waitForTransferCount(megaApi[0].get(), MegaTransfer::TYPE_UPLOAD, 0, defaultTimeoutMs))
        << "Remote-copy resume must not leave an upload transfer in the queue";

    ASSERT_TRUE(waitForEvent(
        [this, folder1Handle, kCopyName]()
        {
            auto folder = makeUniqueFrom(megaApi[0]->getNodeByHandle(folder1Handle));
            if (!folder)
                return false;
            auto copyNode =
                makeUniqueFrom(megaApi[0]->getChildNode(folder.get(), kCopyName.c_str()));
            return copyNode != nullptr;
        },
        maxTimeout))
        << "Remote copy node was not created after resume";

    auto folder1After = makeUniqueFrom(megaApi[0]->getNodeByHandle(folder1Handle));
    ASSERT_TRUE(folder1After);
    EXPECT_EQ(megaApi[0]->getNumChildren(folder1After.get()), 1)
        << "Folder1 must contain the remote-copy node";

    // A remote copy reuses the source node key, whereas a plain re-upload would generate a fresh
    // random key. Comparing the keys proves the resume completed through the remote-copy branch and
    // not through a re-upload that would satisfy every assertion above just the same.
    auto copyNode = makeUniqueFrom(megaApi[0]->getChildNode(folder1After.get(), kCopyName.c_str()));
    ASSERT_TRUE(copyNode);
    auto sourceNode = makeUniqueFrom(megaApi[0]->getNodeByHandle(sourceHandle));
    ASSERT_TRUE(sourceNode);

    std::unique_ptr<char[]> copyKey{copyNode->getBase64Key()};
    std::unique_ptr<char[]> sourceKey{sourceNode->getBase64Key()};
    ASSERT_TRUE(copyKey);
    ASSERT_TRUE(sourceKey);
    EXPECT_STREQ(copyKey.get(), sourceKey.get())
        << "Remote copy must reuse the source node key. A re-upload would get a fresh random key";
}
