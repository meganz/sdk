/**
 * @file
 * @brief Integration tests for Vault access restrictions on client apps.
 *
 * What these tests verify:
 *   - Access reporting: getAccess() returns ACCESS_READ for the whole Vault (the
 *     root and every descendant at any depth, the Password Manager subtree
 *     included), while nodes outside the Vault (Cloud Drive and its children) keep
 *     their normal access.
 *   - Write operations: generic mutations targeting the Vault tree (createFolder,
 *     rename, remove, move, upload, copy) are rejected with API_EACCESS.
 *   - Sync: a non-backup sync whose remote target is inside the Vault is rejected.
 *   - Mount: mounting a Vault node over FUSE is rejected.
 *
 * The cases are split across two fixtures because no single client type loads all
 * the nodes involved:
 *   - SdkTestVaultAccess (DEFAULT client) loads the Cloud Drive, Rubbish and Vault
 *     roots, covering the Vault root, "My Backups" and the Cloud Drive cases.
 *   - SdkTestVaultAccessPasswordManager (PASSWORD_MANAGER client) auto-creates the
 *     Password Manager base and loads the Vault root, covering the password subtree
 *     with the read-only branch genuinely exercised (firstancestor() reaches the
 *     loaded VAULTNODE).
 */

#include "integration_test_utils.h"
#include "passwordManager/SdkTestPasswordManager.h"

#include <gmock/gmock.h>
#include <mega/fuse/common/mount_result.h>

using namespace testing;

class SdkTestVaultAccess: public SdkTest
{};

TEST_F(SdkTestVaultAccess, VaultIsReadOnlyCloudDriveIsNot)
{
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));
    MegaApi* const api = megaApi[0].get();
    ASSERT_NE(api, nullptr);

    // The Vault root is read-only for the app.
    std::unique_ptr<MegaNode> vault{api->getVaultNode()};
    ASSERT_TRUE(vault) << "Vault node is not available";
    EXPECT_EQ(MegaShare::ACCESS_READ, api->getAccess(vault.get()))
        << "Vault root should be read-only for the app";

    // A non-password Vault descendant ("My Backups") is read-only too.
    ASSERT_TRUE(sdk_test::ensureMyBackupsFolderExists(api, "My backups").first)
        << "Could not ensure the My Backups folder exists";
    const auto [gotBackups, backupsHandle] = sdk_test::getMyBackupsFolder(api);
    ASSERT_TRUE(gotBackups);
    ASSERT_NE(backupsHandle, UNDEF);
    EXPECT_EQ(MegaShare::ACCESS_READ, api->getAccess(backupsHandle))
        << "My Backups folder (Vault descendant) should be read-only for the app";

    // Cloud Drive is outside the Vault and must keep owner access.
    std::unique_ptr<MegaNode> cloud{api->getRootNode()};
    ASSERT_TRUE(cloud) << "Cloud Drive root is not available";
    EXPECT_EQ(MegaShare::ACCESS_OWNER, api->getAccess(cloud.get()))
        << "Cloud Drive root should keep owner access";

    const MegaHandle folder = createFolder(0, "vault_access_regression", cloud.get());
    ASSERT_NE(folder, UNDEF);
    EXPECT_EQ(MegaShare::ACCESS_OWNER, api->getAccess(folder))
        << "A Cloud Drive folder should keep owner access";

    std::unique_ptr<MegaNode> folderNode{api->getNodeByHandle(folder)};
    if (folderNode)
    {
        ASSERT_EQ(API_OK, doDeleteNode(0, folderNode.get()));
    }

    // The Rubbish Bin is another root outside the Vault and must keep owner access.
    std::unique_ptr<MegaNode> rubbish{api->getRubbishNode()};
    ASSERT_TRUE(rubbish) << "Rubbish Bin root is not available";
    EXPECT_EQ(MegaShare::ACCESS_OWNER, api->getAccess(rubbish.get()))
        << "Rubbish Bin root should keep owner access";
}

// Generic write operations targeting the Vault tree must be rejected (only dedicated
// backup/sync/password-manager flows, which set the 'vw' flag, may write there).
TEST_F(SdkTestVaultAccess, VaultWriteOperationsRejected)
{
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));
    MegaApi* const api = megaApi[0].get();
    ASSERT_NE(api, nullptr);

    std::unique_ptr<MegaNode> vault{api->getVaultNode()};
    ASSERT_TRUE(vault) << "Vault node is not available";

    // A non-password Vault descendant to operate on: "My Backups".
    ASSERT_TRUE(sdk_test::ensureMyBackupsFolderExists(api, "My backups").first)
        << "Could not ensure the My Backups folder exists";
    const auto [gotBackups, backupsHandle] = sdk_test::getMyBackupsFolder(api);
    ASSERT_TRUE(gotBackups);
    ASSERT_NE(backupsHandle, UNDEF);
    std::unique_ptr<MegaNode> backups{api->getNodeByHandle(backupsHandle)};
    ASSERT_TRUE(backups);

    std::unique_ptr<MegaNode> cloud{api->getRootNode()};
    ASSERT_TRUE(cloud);

    // createFolder into the Vault (root or descendant) is rejected. Drive it directly
    // (the createFolder helper asserts success, so it can't be used for the reject case).
    {
        RequestTracker rt(api);
        api->createFolder("vault_reject_root", vault.get(), &rt);
        EXPECT_EQ(API_EACCESS, rt.waitForResult())
            << "createFolder under the Vault root should be rejected";
    }
    {
        RequestTracker rt(api);
        api->createFolder("vault_reject_child", backups.get(), &rt);
        EXPECT_EQ(API_EACCESS, rt.waitForResult())
            << "createFolder under a Vault folder should be rejected";
    }

    // rename / remove / move of a Vault node is rejected.
    EXPECT_EQ(API_EACCESS, doRenameNode(0, backups.get(), "vault_reject_rename"));
    EXPECT_EQ(API_EACCESS, doDeleteNode(0, backups.get()));
    MegaHandle moved = UNDEF;
    EXPECT_EQ(API_EACCESS, doMoveNode(0, &moved, backups.get(), cloud.get()));

    // upload into a Vault node is rejected.
    const std::string uploadFile = "vault_reject_upload.txt";
    ASSERT_TRUE(createFile(uploadFile, false)) << "Couldn't create " << uploadFile;
    MegaHandle uploaded = UNDEF;
    EXPECT_EQ(API_EACCESS,
              doStartUpload(0,
                            &uploaded,
                            uploadFile.c_str(),
                            backups.get(),
                            nullptr,
                            ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
                            nullptr,
                            false,
                            false,
                            nullptr));

    // control: the same createFolder succeeds on the Cloud Drive.
    const MegaHandle ok = createFolder(0, "vault_reject_control", cloud.get());
    EXPECT_NE(ok, UNDEF) << "createFolder in the Cloud Drive should succeed";
    if (ok != UNDEF)
    {
        std::unique_ptr<MegaNode> okNode{api->getNodeByHandle(ok)};
        ASSERT_TRUE(okNode);
        // copying a Cloud node into a Vault node is rejected.
        MegaHandle copied = UNDEF;
        EXPECT_EQ(API_EACCESS, doCopyNode(0, &copied, okNode.get(), backups.get()));
        doDeleteNode(0, okNode.get());
    }
}

#ifdef ENABLE_SYNC
// A non-backup sync whose remote target is in the Vault must be rejected.
TEST_F(SdkTestVaultAccess, NonBackupSyncToVaultRejected)
{
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));
    MegaApi* const api = megaApi[0].get();
    ASSERT_NE(api, nullptr);

    ASSERT_TRUE(sdk_test::ensureMyBackupsFolderExists(api, "My backups").first)
        << "Could not ensure the My Backups folder exists";
    const auto [gotBackups, backupsHandle] = sdk_test::getMyBackupsFolder(api);
    ASSERT_TRUE(gotBackups);
    ASSERT_NE(backupsHandle, UNDEF);

    const fs::path localBasePath = makeNewTestRoot();
    const fs::path localFolderPath = localBasePath / "vault_sync_reject";
    fs::create_directories(localFolderPath);

    MegaHandle syncRoot = UNDEF;
    const int err = synchronousSyncFolder(0,
                                          &syncRoot,
                                          MegaSync::TYPE_TWOWAY,
                                          path_u8string(localFolderPath).c_str(),
                                          "vault_sync_reject",
                                          backupsHandle,
                                          nullptr);
    EXPECT_EQ(API_EARGS, err) << "A non-backup sync into the Vault should be rejected";
}
#endif // ENABLE_SYNC

// Mounting (FUSE) a Vault node must be rejected by the shared mount validator, which
// covers both the low-level SDK (e.g. megacli) and MegaApi paths. Skipped when FUSE
// isn't available in this build/platform.
TEST_F(SdkTestVaultAccess, VaultMountRejected)
{
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));
    MegaApi* const api = megaApi[0].get();
    ASSERT_NE(api, nullptr);

    if (!api->isFUSESupported())
        GTEST_SKIP() << "FUSE is not supported in this build/platform";

    std::unique_ptr<MegaNode> vault{api->getVaultNode()};
    ASSERT_TRUE(vault) << "Vault node is not available";

    std::unique_ptr<MegaMount> mount{MegaMount::create()};
    ASSERT_TRUE(mount);
    mount->setHandle(vault->getHandle());
    mount->setPath(path_u8string(makeNewTestRoot()).c_str());

    int mountResult = ::mega::fuse::MOUNT_SUCCESS;
    RequestTracker rt(api,
                      [&mountResult](MegaError& e, MegaRequest&)
                      {
                          mountResult = e.getMountResult();
                      });
    api->addMount(mount.get(), &rt);
    rt.waitForResult();

    EXPECT_EQ(static_cast<int>(::mega::fuse::MOUNT_REMOTE_VAULT), mountResult)
        << "Mounting a Vault node should be rejected";
}

class SdkTestVaultAccessPasswordManager: public SdkTestPasswordManager
{};

TEST_F(SdkTestVaultAccessPasswordManager, PasswordNodeIsReadOnly)
{
    // The Vault root is read-only even on a Password Manager client.
    std::unique_ptr<MegaNode> vault{mApi->getVaultNode()};
    ASSERT_TRUE(vault) << "Vault node is not available";
    EXPECT_EQ(MegaShare::ACCESS_READ, mApi->getAccess(vault.get()))
        << "Vault root should be read-only for the app";

    // The Password Manager base is read-only.
    const std::unique_ptr<MegaNode> base = getBaseNode();
    ASSERT_TRUE(base) << "Password Manager base node is not available";
    EXPECT_EQ(MegaShare::ACCESS_READ, mApi->getAccess(base.get()))
        << "Password Manager base is read-only";

    // A password node under the base is also read-only.
    std::unique_ptr<MegaNode::PasswordNodeData> data{
        MegaNode::PasswordNodeData::createInstance("pwd", "notes", "url", "user", nullptr)};
    const auto pwdHandle =
        sdk_test::createPasswordNode(mApi, "vault_access_pwd", data.get(), getBaseHandle());
    ASSERT_NE(pwdHandle, UNDEF) << "Password node was not created";
    EXPECT_EQ(MegaShare::ACCESS_READ, mApi->getAccess(pwdHandle)) << "A password node is read-only";
}
