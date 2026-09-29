/**
 * @file
 * @brief Tests for the sync-disable reason reported when a sync upload's putnodes ends in
 * API_EOVERQUOTA: it must blame the target's owner, i.e. FOREIGN_TARGET_OVERSTORAGE only when the
 * sync target is an inshare, and STORAGE_OVERQUOTA when it is our own cloud.
 *
 * Both flavours of sync putnodes are covered, as each one classifies the reason on its own: the
 * putnodes of an uploaded file and the putnodes cloning a node that already holds the same data.
 */

#ifdef ENABLE_SYNC

#include "api_request_hook.h"
#include "integration_test_utils.h"
#include "mega/utils.h"
#include "sdk_test_utils.h"
#include "SdkTest_test.h"

#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <system_error>
#include <utility>

#ifdef MEGASDK_DEBUG_TEST_HOOKS_ENABLED

using namespace sdk_test;

namespace
{

bool isPutnodesCommand(const std::string& command)
{
    return commandStringArg(command, "a") == "p";
}

std::string commandPitag(const std::string& command)
{
    return commandStringArg(command, "p");
}

// The putnodes whose target owner must look out of storage: every one, or only the ones carrying
// the given pitag, so that an unrelated putnodes cannot suspend the sync before the one under test
// is even sent.
ScopedApiErrorInjector::CommandPredicate putnodesWithPitag(std::string pitag = {})
{
    return [pitag = std::move(pitag)](const std::string& command)
    {
        return isPutnodesCommand(command) && (pitag.empty() || commandPitag(command) == pitag);
    };
}

} // namespace

/**
 * @class SdkTestSyncOverquotaReason
 * @brief Fixture providing a local sync root plus the helpers to sync it into either our own cloud
 * or a full-access inshare, and to read back the reason a sync was suspended.
 */
class SdkTestSyncOverquotaReason: public SdkTest
{
protected:
    static constexpr auto MAX_TIMEOUT = 3min;
    static constexpr auto CLONE_FILE_NAME = "fileToClone";
    static constexpr size_t CLONE_FILE_SIZE = 1024;

    void SetUp() override
    {
        SdkTest::SetUp();
        const std::string uniqueName{getFilePrefix() + getThisThreadIdStr()};
        mLocalRoot.emplace(fs::current_path() / uniqueName);
        mStagingRoot.emplace(fs::current_path() / (uniqueName + "_staging"));
    }

    void TearDown() override
    {
        // A simulated over quota on our own target leaves the client in STORAGE_RED, which is
        // cached and would outlive this test, so ask the API for the real storage state again.
        for (unsigned i = 0; i < megaApi.size(); ++i)
        {
            if (megaApi[i] && megaApi[i]->isLoggedIn())
            {
                EXPECT_EQ(API_OK, synchronousGetSpecificAccountDetails(i, true, false, false));
            }
        }
        SdkTest::TearDown();
    }

    const fs::path& getLocalRoot() const
    {
        return mLocalRoot->getPath();
    }

    // Remote folder to sync into, created under the root of the given account.
    MegaHandle createRemoteRoot(const unsigned apiIndex, const std::string& nameSuffix = {})
    {
        const std::unique_ptr<MegaNode> rootNode{megaApi[apiIndex]->getRootNode()};
        EXPECT_TRUE(rootNode);
        if (!rootNode)
            return UNDEF;

        const std::string name{getFilePrefix() + getThisThreadIdStr() + nameSuffix};
        return createFolder(apiIndex, name.c_str(), rootNode.get());
    }

    // The pitag the sync engine puts on the putnodes that clones a node, i.e. the one built by
    // SyncUpload_inClient::buildSyncClonePitag() once the target has been resolved.
    static std::string syncClonePitag(const PitagTarget target)
    {
        return pitagToString(Pitag{PitagPurpose::CopyInternal,
                                   PitagTrigger::SyncAlgorithm,
                                   PitagNodeType::File,
                                   target,
                                   PitagImportSource::NotApplicable});
    }

    // Uploads a file staged outside the sync, so that moving that same file into the sync root
    // leaves the engine with a node holding its data to clone instead of uploading it again.
    void createCloneCandidate(const unsigned apiIndex)
    {
        const MegaHandle candidatesFolder = createRemoteRoot(apiIndex, "_cloneCandidates");
        ASSERT_NE(candidatesFolder, UNDEF) << "Could not create the folder of clone candidates";

        const std::unique_ptr<MegaNode> candidatesNode{
            megaApi[apiIndex]->getNodeByHandle(candidatesFolder)};
        ASSERT_TRUE(candidatesNode);

        mCloneSource.emplace(mStagingRoot->getPath() / CLONE_FILE_NAME, CLONE_FILE_SIZE);
        ASSERT_EQ(API_OK,
                  doStartUpload(apiIndex,
                                nullptr /*newNodeHandleResult*/,
                                path_u8string(mCloneSource->getPath()).c_str(),
                                candidatesNode.get(),
                                nullptr /*fileName*/,
                                ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
                                nullptr /*appData*/,
                                false /*isSourceTemporary*/,
                                false /*startFirst*/,
                                nullptr /*cancelToken*/))
            << "Could not upload the node to be cloned";
    }

    // A move keeps the file's mtime, so the node uploaded from it is an exact clone candidate, and
    // it is atomic, so the sync cannot see the file before it is complete.
    void moveCloneCandidateIntoSync()
    {
        ASSERT_TRUE(mCloneSource.has_value());
        const std::error_code ec = mCloneSource->move(getLocalRoot() / CLONE_FILE_NAME);
        ASSERT_FALSE(ec) << "Could not move the file to clone into the sync root: " << ec.message();
    }

    // Shares hfolder (owned by account 0) with account 1 with full access, and waits until account
    // 1 can see it decrypted, so it can be used as a sync target.
    void shareWithFullAccess(const MegaHandle hfolder)
    {
        const std::unique_ptr<MegaUser> existingContact{
            megaApi[0]->getContact(mApi[1].email.c_str())};
        if (!existingContact || existingContact->getVisibility() != MegaUser::VISIBILITY_VISIBLE)
        {
            mApi[1].contactRequestUpdated = false;
            ASSERT_NO_FATAL_FAILURE(inviteContact(0,
                                                  mApi[1].email,
                                                  "SdkTestSyncOverquotaReason contact request",
                                                  MegaContactRequest::INVITE_ACTION_ADD));
            ASSERT_TRUE(waitForResponse(&mApi[1].contactRequestUpdated, 60u))
                << "Contact request not received by the sharee";

            ASSERT_NO_FATAL_FAILURE(getContactRequest(1, false));

            mApi[0].contactRequestUpdated = false;
            mApi[1].contactRequestUpdated = false;
            ASSERT_NO_FATAL_FAILURE(
                replyContact(mApi[1].cr.get(), MegaContactRequest::REPLY_ACTION_ACCEPT));
            ASSERT_TRUE(waitForResponse(&mApi[1].contactRequestUpdated, 60u))
                << "Contact request reply not received by the sharee";
            ASSERT_TRUE(waitForResponse(&mApi[0].contactRequestUpdated, 60u))
                << "Contact request reply not received by the sharer";
            mApi[1].cr.reset();
        }

        if (gManualVerification)
        {
            if (!areCredentialsVerified(0, mApi[1].email))
            {
                ASSERT_NO_FATAL_FAILURE(verifyCredentials(0, mApi[1].email));
            }
            if (!areCredentialsVerified(1, mApi[0].email))
            {
                ASSERT_NO_FATAL_FAILURE(verifyCredentials(1, mApi[0].email));
            }
        }

        const std::unique_ptr<MegaNode> folder{megaApi[0]->getNodeByHandle(hfolder)};
        ASSERT_TRUE(folder);
        ASSERT_NO_FATAL_FAILURE(
            shareFolder(folder.get(), mApi[1].email.c_str(), MegaShare::ACCESS_FULL));

        const auto isUsableInshare = [this, hfolder]()
        {
            const std::unique_ptr<MegaNode> inshare{megaApi[1]->getNodeByHandle(hfolder)};
            return inshare && inshare->isInShare() && inshare->isNodeKeyDecrypted();
        };
        ASSERT_TRUE(waitFor(isUsableInshare, MAX_TIMEOUT, 1s))
            << "The inshare never became usable by the sharee";
    }

    // Reason reported once the sync gets suspended; nullopt if it never was.
    std::optional<int> waitForSuspendReason(const unsigned apiIndex, const handle backupId) const
    {
        std::optional<int> reason;
        const auto isSuspended = [this, apiIndex, backupId, &reason]()
        {
            const std::unique_ptr<MegaSync> sync{megaApi[apiIndex]->getSyncByBackupId(backupId)};
            if (!sync || sync->getRunState() != MegaSync::RUNSTATE_SUSPENDED)
                return false;

            reason = sync->getError();
            return true;
        };
        waitFor(isSuspended, MAX_TIMEOUT, 1s);
        return reason;
    }

private:
    // Destroyed with the fixture, i.e. after SdkTest::TearDown() has removed the sync using it.
    std::optional<LocalTempDir> mLocalRoot;

    // Sibling of the sync root, to hold files the sync must not see yet.
    std::optional<LocalTempDir> mStagingRoot;

    std::optional<LocalTempFile> mCloneSource;
};

/**
 * @brief SdkTestSyncOverquotaReason.OwnTargetReportsStorageOverquota
 *
 * A sync into our own cloud whose putnodes fails with API_EOVERQUOTA means our own account is full,
 * so the sync must be suspended with STORAGE_OVERQUOTA (and not blame the owner of an inshare we
 * are not even using).
 */
TEST_F(SdkTestSyncOverquotaReason, OwnTargetReportsStorageOverquota)
{
    const auto logPre = getLogPrefix();
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    LOG_verbose << logPre << "Creating the remote sync root in our own cloud";
    const MegaHandle remoteRoot = createRemoteRoot(0);
    ASSERT_NE(remoteRoot, UNDEF) << "Could not create the remote sync root";

    LOG_verbose << logPre << "Starting the sync";
    const handle backupId = syncFolder(megaApi[0].get(), path_u8string(getLocalRoot()), remoteRoot);
    ASSERT_NE(backupId, UNDEF) << "Could not start the sync";

    LOG_verbose << logPre << "Uploading a file with putnodes forced to fail as over quota";
    const ScopedApiErrorInjector overquota{API_EOVERQUOTA, putnodesWithPitag()};
    sdk_test::createFile(getLocalRoot() / "overquotaFile", 1);

    const auto reason = waitForSuspendReason(0, backupId);
    ASSERT_TRUE(overquota.fired())
        << "The sync never sent a putnodes, so no over quota could be simulated";
    ASSERT_TRUE(reason.has_value()) << "The sync was not suspended";
    EXPECT_EQ(*reason, MegaSync::STORAGE_OVERQUOTA)
        << "Own storage over quota was reported as '" << MegaSync::getMegaSyncErrorCode(*reason)
        << "'";
}

/**
 * @brief SdkTestSyncOverquotaReason.InshareTargetReportsForeignOverstorage
 *
 * A sync into an inshare whose putnodes fails with API_EOVERQUOTA means the sharer's account is
 * full, so the sync must be suspended with FOREIGN_TARGET_OVERSTORAGE.
 */
TEST_F(SdkTestSyncOverquotaReason, InshareTargetReportsForeignOverstorage)
{
    const auto logPre = getLogPrefix();
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(2));

    LOG_verbose << logPre << "Sharing a folder of account 0 with account 1";
    const MegaHandle remoteRoot = createRemoteRoot(0);
    ASSERT_NE(remoteRoot, UNDEF) << "Could not create the folder to share";
    ASSERT_NO_FATAL_FAILURE(shareWithFullAccess(remoteRoot));

    LOG_verbose << logPre << "Starting a sync of account 1 into the inshare";
    const handle backupId = syncFolder(megaApi[1].get(), path_u8string(getLocalRoot()), remoteRoot);
    ASSERT_NE(backupId, UNDEF) << "Could not start the sync into the inshare";

    LOG_verbose << logPre << "Uploading a file with putnodes forced to fail as over quota";
    const ScopedApiErrorInjector overquota{API_EOVERQUOTA, putnodesWithPitag()};
    sdk_test::createFile(getLocalRoot() / "overquotaFile", 1);

    const auto reason = waitForSuspendReason(1, backupId);
    ASSERT_TRUE(overquota.fired())
        << "The sync never sent a putnodes, so no over quota could be simulated";
    ASSERT_TRUE(reason.has_value()) << "The sync was not suspended";
    EXPECT_EQ(*reason, MegaSync::FOREIGN_TARGET_OVERSTORAGE)
        << "Inshare owner's storage over quota was reported as '"
        << MegaSync::getMegaSyncErrorCode(*reason) << "'";
}

/**
 * @brief SdkTestSyncOverquotaReason.OwnTargetCloneReportsStorageOverquota
 *
 * As OwnTargetReportsStorageOverquota, but for a file the engine resolves by cloning an existing
 * node: that putnodes is sent from its own code path, which must classify the reason the same way.
 */
TEST_F(SdkTestSyncOverquotaReason, OwnTargetCloneReportsStorageOverquota)
{
    const auto logPre = getLogPrefix();
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    LOG_verbose << logPre << "Uploading the node to be cloned, outside the sync";
    ASSERT_NO_FATAL_FAILURE(createCloneCandidate(0));

    LOG_verbose << logPre << "Creating the remote sync root in our own cloud";
    const MegaHandle remoteRoot = createRemoteRoot(0);
    ASSERT_NE(remoteRoot, UNDEF) << "Could not create the remote sync root";

    LOG_verbose << logPre << "Starting the sync";
    const handle backupId = syncFolder(megaApi[0].get(), path_u8string(getLocalRoot()), remoteRoot);
    ASSERT_NE(backupId, UNDEF) << "Could not start the sync";

    LOG_verbose << logPre
                << "Moving the file to clone into the sync, with the putnodes of the "
                   "clone forced to fail as over quota";
    const ScopedApiErrorInjector overquota{
        API_EOVERQUOTA,
        putnodesWithPitag(syncClonePitag(PitagTarget::CloudDrive))};
    ASSERT_NO_FATAL_FAILURE(moveCloneCandidateIntoSync());

    const auto reason = waitForSuspendReason(0, backupId);
    ASSERT_TRUE(overquota.fired())
        << "The sync never cloned the node, so no over quota could be simulated";
    ASSERT_TRUE(reason.has_value()) << "The sync was not suspended";
    EXPECT_EQ(*reason, MegaSync::STORAGE_OVERQUOTA)
        << "Own storage over quota was reported as '" << MegaSync::getMegaSyncErrorCode(*reason)
        << "'";
}

/**
 * @brief SdkTestSyncOverquotaReason.InshareTargetCloneReportsForeignOverstorage
 *
 * As InshareTargetReportsForeignOverstorage, but for a file the engine resolves by cloning a node
 * of our own cloud into the inshare: the sharer's account is the one that is full.
 */
TEST_F(SdkTestSyncOverquotaReason, InshareTargetCloneReportsForeignOverstorage)
{
    const auto logPre = getLogPrefix();
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(2));

    LOG_verbose << logPre << "Sharing a folder of account 0 with account 1";
    const MegaHandle remoteRoot = createRemoteRoot(0);
    ASSERT_NE(remoteRoot, UNDEF) << "Could not create the folder to share";
    ASSERT_NO_FATAL_FAILURE(shareWithFullAccess(remoteRoot));

    LOG_verbose << logPre << "Uploading the node to be cloned, in the cloud of account 1";
    ASSERT_NO_FATAL_FAILURE(createCloneCandidate(1));

    LOG_verbose << logPre << "Starting a sync of account 1 into the inshare";
    const handle backupId = syncFolder(megaApi[1].get(), path_u8string(getLocalRoot()), remoteRoot);
    ASSERT_NE(backupId, UNDEF) << "Could not start the sync into the inshare";

    LOG_verbose << logPre
                << "Moving the file to clone into the sync, with the putnodes of the "
                   "clone forced to fail as over quota";
    const ScopedApiErrorInjector overquota{
        API_EOVERQUOTA,
        putnodesWithPitag(syncClonePitag(PitagTarget::IncomingShare))};
    ASSERT_NO_FATAL_FAILURE(moveCloneCandidateIntoSync());

    const auto reason = waitForSuspendReason(1, backupId);
    ASSERT_TRUE(overquota.fired())
        << "The sync never cloned the node, so no over quota could be simulated";
    ASSERT_TRUE(reason.has_value()) << "The sync was not suspended";
    EXPECT_EQ(*reason, MegaSync::FOREIGN_TARGET_OVERSTORAGE)
        << "Inshare owner's storage over quota was reported as '"
        << MegaSync::getMegaSyncErrorCode(*reason) << "'";
}

#endif // MEGASDK_DEBUG_TEST_HOOKS_ENABLED

#endif // ENABLE_SYNC
