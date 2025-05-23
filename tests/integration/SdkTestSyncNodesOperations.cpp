/**
 * @file SdkTestSyncNodesOperations.cpp
 * @brief This file is expected to contain SdkTestSyncNodesOperations class definition.
 */

#ifdef ENABLE_SYNC

#include "SdkTestSyncNodesOperations.h"

#include "integration_test_utils.h"
#include "mega/utils.h"
#include "megautils.h"
#include "sdk_test_utils.h"

#include <gmock/gmock.h>

using namespace sdk_test;
using namespace testing;

const std::string SdkTestSyncNodesOperations::DEFAULT_SYNC_REMOTE_PATH{"dir1"};

void SdkTestSyncNodesOperations::SetUp()
{
    SdkTestNodesSetUp::SetUp();
    if (createSyncOnSetup())
    {
        ASSERT_NO_FATAL_FAILURE(
            initiateSync(getLocalTmpDirU8string(), DEFAULT_SYNC_REMOTE_PATH, mBackupId));
        ASSERT_NO_FATAL_FAILURE(waitForSyncToMatchCloudAndLocal());
    }
}

void SdkTestSyncNodesOperations::TearDown()
{
    if (mBackupId != UNDEF)
    {
        ASSERT_TRUE(removeSync(megaApi[0].get(), mBackupId));
    }
    SdkTestNodesSetUp::TearDown();
}

const std::vector<NodeInfo>& SdkTestSyncNodesOperations::getElements() const
{
    // To ensure "testCommonFile" is identical in both dirs
    static const auto currentTime = std::chrono::system_clock::now();
    static const std::vector<NodeInfo> ELEMENTS{
        DirNodeInfo(DEFAULT_SYNC_REMOTE_PATH)
            .addChild(FileNodeInfo("testFile").setSize(1))
            .addChild(FileNodeInfo("testCommonFile").setMtime(currentTime))
            .addChild(FileNodeInfo("testFile1")),
        DirNodeInfo("dir2")
            .addChild(FileNodeInfo("testFile").setSize(2))
            .addChild(FileNodeInfo("testCommonFile").setMtime(currentTime))
            .addChild(FileNodeInfo("testFile2"))};
    return ELEMENTS;
}

const std::string& SdkTestSyncNodesOperations::getRootTestDir() const
{
    static const std::string dirName{"SDK_TEST_SYNC_NODE_OPERATIONS_AUX_DIR"};
    return dirName;
}

const fs::path& SdkTestSyncNodesOperations::localTmpPath()
{
    // Prevent parallel test from the same suite writing to the same dir
    thread_local const fs::path localTmpDir{"./SDK_TEST_SYNC_NODE_OPERATIONS_AUX_LOCAL_DIR_" +
                                            getThisThreadIdStr()};
    return localTmpDir;
}

const fs::path& SdkTestSyncNodesOperations::getLocalTmpDir() const
{
    return mTempLocalDir.getPath();
}

std::string SdkTestSyncNodesOperations::getLocalTmpDirU8string() const
{
    return getLocalTmpDir().u8string();
}

std::unique_ptr<MegaSync> SdkTestSyncNodesOperations::getSync() const
{
    return std::unique_ptr<MegaSync>(megaApi[0]->getSyncByBackupId(mBackupId));
}

void SdkTestSyncNodesOperations::moveRemoteNode(const std::string& sourcePath,
                                                const std::string& destPath)
{
    const auto source = getNodeByPath(sourcePath);
    const auto dest = getNodeByPath(destPath);
    ASSERT_EQ(API_OK, doMoveNode(0, nullptr, source.get(), dest.get()));
}

void SdkTestSyncNodesOperations::renameRemoteNode(const std::string& sourcePath,
                                                  const std::string& newName)
{
    const auto source = getNodeByPath(sourcePath);
    ASSERT_EQ(API_OK, doRenameNode(0, source.get(), newName.c_str()));
}

void SdkTestSyncNodesOperations::removeRemoteNode(const std::string& path)
{
    const auto node = getNodeByPath(path);
    ASSERT_EQ(API_OK, doDeleteNode(0, node.get()));
}

void SdkTestSyncNodesOperations::ensureSyncNodeIsRunning(const std::string& path)
{
    const auto syncNode = getNodeByPath(path);
    ASSERT_TRUE(syncNode);
    const auto sync = megaApi[0]->getSyncByNode(syncNode.get());
    ASSERT_TRUE(sync);
    ASSERT_EQ(sync->getRunState(), MegaSync::RUNSTATE_RUNNING);
}

void SdkTestSyncNodesOperations::suspendSync()
{
    ASSERT_TRUE(sdk_test::suspendSync(megaApi[0].get(), mBackupId))
        << "Error when trying to suspend the sync";
}

void SdkTestSyncNodesOperations::disableSync()
{
    ASSERT_TRUE(sdk_test::disableSync(megaApi[0].get(), mBackupId))
        << "Error when trying to disable the sync";
}

void SdkTestSyncNodesOperations::resumeSync()
{
    ASSERT_TRUE(sdk_test::resumeSync(megaApi[0].get(), mBackupId))
        << "Error when trying to resume the sync";
}

void SdkTestSyncNodesOperations::ensureSyncLastKnownMegaFolder(const std::string& path)
{
    std::unique_ptr<MegaSync> sync(megaApi[0]->getSyncByBackupId(getBackupId()));
    ASSERT_TRUE(sync);
    ASSERT_EQ(sync->getLastKnownMegaFolder(), convertToTestPath(path));
}

void SdkTestSyncNodesOperations::initiateSync(const std::string& localPath,
                                              const std::string& remotePath,
                                              MegaHandle& backupId)
{
    LOG_verbose << "SdkTestSyncNodesOperations : Initiate sync";
    backupId =
        sdk_test::syncFolder(megaApi[0].get(), localPath, getNodeByPath(remotePath)->getHandle());
    ASSERT_NE(backupId, UNDEF);
}

void SdkTestSyncNodesOperations::waitForSyncToMatchCloudAndLocal()
{
    const auto areLocalAndCloudSynched = [this]() -> bool
    {
        const auto childrenCloudName =
            getCloudFirstChildrenNames(megaApi[0].get(), getSync()->getMegaHandle());
        return childrenCloudName &&
               Value(getLocalFirstChildrenNames(), UnorderedElementsAreArray(*childrenCloudName));
    };
    ASSERT_TRUE(waitFor(areLocalAndCloudSynched, COMMON_TIMEOUT, 10s));
}

void SdkTestSyncNodesOperations::checkCurrentLocalMatchesOriginal(
    const std::string_view cloudDirName)
{
    const auto& originals = getElements();
    const auto it = std::find_if(std::begin(originals),
                                 std::end(originals),
                                 [&cloudDirName](const auto& node)
                                 {
                                     return getNodeName(node) == cloudDirName;
                                 });
    ASSERT_NE(it, std::end(originals))
        << cloudDirName << ": directory not found in original elements";
    const auto* dirNode = std::get_if<DirNodeInfo>(&(*it));
    ASSERT_TRUE(dirNode) << "The found original element is not a directory";

    using ChildNameSize = std::pair<std::string, std::optional<unsigned>>;
    // Get info from original cloud
    std::vector<ChildNameSize> childOriginalInfo;
    std::transform(std::begin(dirNode->childs),
                   std::end(dirNode->childs),
                   std::back_inserter(childOriginalInfo),
                   [](const auto& child) -> ChildNameSize
                   {
                       return std::visit(overloaded{[](const DirNodeInfo& dir) -> ChildNameSize
                                                    {
                                                        return {dir.name, {}};
                                                    },
                                                    [](const FileNodeInfo& file) -> ChildNameSize
                                                    {
                                                        return {file.name, file.size};
                                                    }},
                                         child);
                   });

    // Get info from current local
    std::vector<ChildNameSize> childLocalInfo;
    std::filesystem::directory_iterator children{getLocalTmpDir()};
    std::for_each(begin(children),
                  end(children),
                  [&childLocalInfo](const std::filesystem::path& path)
                  {
                      const auto name = path.filename().string();
                      if (name.front() == '.' || name == DEBRISFOLDER)
                          return;
                      if (std::filesystem::is_directory(path))
                          childLocalInfo.push_back({name, {}});
                      else
                          childLocalInfo.push_back(
                              {name, static_cast<unsigned>(std::filesystem::file_size(path))});
                  });

    ASSERT_THAT(childLocalInfo, UnorderedElementsAreArray(childOriginalInfo));
}

void SdkTestSyncNodesOperations::thereIsAStall(const std::string_view fileName) const
{
    const auto stalls = sdk_test::getStalls(megaApi[0].get());
    ASSERT_EQ(stalls.size(), 1);
    ASSERT_TRUE(stalls[0]);
    const auto& stall = *stalls[0];
    ASSERT_THAT(stall.path(false, 0), EndsWith(fileName));
    ASSERT_THAT(
        stall.reason(),
        MegaSyncStall::SyncStallReason::LocalAndRemotePreviouslyUnsyncedDiffer_userMustChoose);
}

void SdkTestSyncNodesOperations::checkCurrentLocalMatchesMirror() const
{
    ASSERT_THAT(getLocalFirstChildrenNames(),
                UnorderedElementsAre("testFile", "testCommonFile", "testFile1", "testFile2"));
    ASSERT_TRUE(sdk_test::waitForSyncStallState(megaApi[0].get()));
    ASSERT_NO_FATAL_FAILURE(thereIsAStall("testFile"));
}

std::vector<std::string> SdkTestSyncNodesOperations::getLocalFirstChildrenNames() const
{
    return sdk_test::getLocalFirstChildrenNames_if(getLocalTmpDir(),
                                                   [](const std::string& name)
                                                   {
                                                       return name.front() != '.' &&
                                                              name != DEBRISFOLDER;
                                                   });
}

#endif // ENABLE_SYNC
