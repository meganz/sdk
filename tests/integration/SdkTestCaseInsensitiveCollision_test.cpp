/**
 * @brief Test collision behavior uploading files for case insensitive systems
 *
 */

#include "integration_test_utils.h"
#include "mega/utils.h"
#include "sdk_test_utils.h"
#include "SdkTest_test.h"

class SdkTestCapitalisationCollision: public SdkTest
{
public:
    void SetUp() override;
    void TearDown() override;

    /**
     * @brief Create and download folders
     * @param remoteNames Remote folder names (created by the test)
     * @param localNames Local folder names after download
     * @param collisionResolution Method to resolve conflicts
     */
    void testCapitalisationFolderCollision(const std::vector<std::string>& remoteNames,
                                           const std::set<std::string>& localNames,
                                           const int collisionResolution);

    /**
     * @brief Create a Folder with two subfolders and download them
     * Remote names should create a conflict when they are downloaded
     * As folder is downloaded and we don't know the order of the subfolders, we check for the
     * existence of the suffix
     * @param remoteNames Remote folder names (created by the test)
     * @param collisionResolution Method to resolve conflicts
     */
    void testCapitalisationDownloadFolderWithCollision(const std::vector<std::string>& remoteNames,
                                                       const size_t numElementsWithSuffix,
                                                       const size_t numElementsWithoutSuffix,
                                                       const int collisionResolution);

    /**
     * @brief Create and download two files
     * @param remoteNames Remote file names (created by the test)
     * @param localNames Local file names after download
     * @param collisionResolution Method to resolve conflicts
     */
    void testCapitalisationFile(const std::vector<std::string>& remoteNames,
                                const std::set<std::string>& localNames,
                                const int collisionResolution);

    bool mIsCaseInsensitive{false};

private:
    std::filesystem::path mFolderDestination;
};

void SdkTestCapitalisationCollision::SetUp()
{
    SdkTest::SetUp();
    mFolderDestination = fs::current_path() / "Destination";
    std::filesystem::create_directory(mFolderDestination);
    std::unique_ptr<::mega::FileSystemAccess> fileSystemAccess = ::mega::createFSA();
    LocalPath path = LocalPath::fromAbsolutePath(fs::current_path().u8string());
    auto value = isCaseInsensitive(path, fileSystemAccess.get());
    mIsCaseInsensitive = value.has_value() ? value.value() : false;
}

void SdkTestCapitalisationCollision::TearDown()
{
    SdkTest::TearDown();
    std::filesystem::remove_all(mFolderDestination);
}

void SdkTestCapitalisationCollision::testCapitalisationFolderCollision(
    const std::vector<std::string>& remoteNames,
    const std::set<std::string>& localNames,
    const int collisionResolution)
{
    ASSERT_GE(remoteNames.size(), 2u);

    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));
    std::unique_ptr<MegaNode> rootnode{megaApi[0]->getRootNode()};
    ASSERT_TRUE(rootnode.get());

    MegaHandle hfolder1 = createFolder(0, remoteNames[0].c_str(), rootnode.get());
    ASSERT_NE(hfolder1, INVALID_HANDLE);
    std::unique_ptr<MegaNode> n1(megaApi[0]->getNodeByHandle(hfolder1));
    ASSERT_TRUE(n1.get());

    MegaHandle hfolder2 = createFolder(0, remoteNames[1].c_str(), rootnode.get());
    ASSERT_NE(hfolder2, INVALID_HANDLE);
    std::unique_ptr<MegaNode> n2(megaApi[0]->getNodeByHandle(hfolder2));
    ASSERT_TRUE(n2.get());

    auto errCode = sdk_test::downloadNode(megaApi[0].get(),
                                          n1.get(),
                                          mFolderDestination,
                                          true,
                                          180s /*timeout*/,
                                          MegaTransfer::COLLISION_CHECK_ASSUMEDIFFERENT,
                                          collisionResolution);

    ASSERT_EQ(errCode, API_OK);

    errCode = sdk_test::downloadNode(megaApi[0].get(),
                                     n2.get(),
                                     mFolderDestination,
                                     true,
                                     180s /*timeout*/,
                                     MegaTransfer::COLLISION_CHECK_ASSUMEDIFFERENT,
                                     collisionResolution);

    ASSERT_EQ(errCode, API_OK);

    size_t numElements = 0;
    for (const auto& element: std::filesystem::directory_iterator(mFolderDestination))
    {
        numElements++;
        const auto fileName = element.path().filename().u8string();
        ASSERT_NE(localNames.find(fileName), localNames.end())
            << "Unexpected file name: " << fileName;
    }

    ASSERT_EQ(numElements, localNames.size());
}

void SdkTestCapitalisationCollision::testCapitalisationDownloadFolderWithCollision(
    const std::vector<std::string>& remoteNames,
    const size_t numElementsWithSuffix,
    const size_t numElementsWithoutSuffix,
    const int collisionResolution)
{
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));
    std::unique_ptr<MegaNode> rootnode{megaApi[0]->getRootNode()};
    ASSERT_TRUE(rootnode.get());

    MegaHandle hTest = createFolder(0, "Test", rootnode.get());
    std::unique_ptr<MegaNode> testNode(megaApi[0]->getNodeByHandle(hTest));
    ASSERT_TRUE(testNode.get());

    MegaHandle hfolder1 = createFolder(0, remoteNames[0].c_str(), testNode.get());
    ASSERT_NE(hfolder1, INVALID_HANDLE);
    MegaHandle hfolder2 = createFolder(0, remoteNames[1].c_str(), testNode.get());
    ASSERT_NE(hfolder2, INVALID_HANDLE);

    auto errCode = sdk_test::downloadNode(megaApi[0].get(),
                                          testNode.get(),
                                          mFolderDestination,
                                          true,
                                          180s /*timeout*/,
                                          MegaTransfer::COLLISION_CHECK_ASSUMEDIFFERENT,
                                          collisionResolution);

    ASSERT_EQ(errCode, API_OK);

    size_t elementsWithSuffix = 0;
    size_t elementsWithoutSuffix = 0;
    std::string suffix{};
    switch (collisionResolution)
    {
        case MegaTransfer::COLLISION_RESOLUTION_EXISTING_TO_OLDN:
            suffix = ".old1";
            break;

        case MegaTransfer::COLLISION_RESOLUTION_NEW_WITH_N:
            suffix = "(1)";
            break;

        case MegaTransfer::COLLISION_RESOLUTION_OVERWRITE:
            suffix = "No suffix";
            break;
    }

    for (const auto& element: std::filesystem::directory_iterator(mFolderDestination / "Test"))
    {
        if (element.path().filename().u8string().find(suffix) != std::string::npos)
        {
            elementsWithSuffix++;
        }
        else
        {
            elementsWithoutSuffix++;
        }
    }

    ASSERT_EQ(elementsWithoutSuffix, numElementsWithoutSuffix);
    ASSERT_EQ(elementsWithSuffix, numElementsWithSuffix);
}

void SdkTestCapitalisationCollision::testCapitalisationFile(
    const std::vector<std::string>& remoteNames,
    const std::set<std::string>& localNames,
    const int collisionResolution)
{
    std::string fileName{"f.txt"};
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));
    std::unique_ptr<MegaNode> rootnode{megaApi[0]->getRootNode()};
    ASSERT_TRUE(rootnode.get());
    ASSERT_TRUE(createFile(fileName, false)) << "Couldn't create " << fileName;

    MegaHandle uploadedNode1 = UNDEF;
    ASSERT_EQ(MegaError::API_OK,
              doStartUpload(0,
                            &uploadedNode1,
                            fileName.c_str(),
                            rootnode.get(),
                            remoteNames[0].c_str(),
                            ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
                            nullptr /*appData*/,
                            false /*isSourceTemporary*/,
                            false /*startFirst*/,
                            nullptr /*cancelToken*/))
        << "Cannot upload a test file";

    std::unique_ptr<MegaNode> n1(megaApi[0]->getNodeByHandle(uploadedNode1));
    ASSERT_TRUE(n1.get());

    MegaHandle uploadedNode2 = UNDEF;
    ASSERT_EQ(MegaError::API_OK,
              doStartUpload(0,
                            &uploadedNode2,
                            fileName.c_str(),
                            rootnode.get(),
                            remoteNames[1].c_str(),
                            ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
                            nullptr /*appData*/,
                            false /*isSourceTemporary*/,
                            false /*startFirst*/,
                            nullptr /*cancelToken*/))
        << "Cannot upload a test file";

    std::unique_ptr<MegaNode> n2(megaApi[0]->getNodeByHandle(uploadedNode2));
    ASSERT_TRUE(n2.get());

    auto errCode = sdk_test::downloadNode(megaApi[0].get(),
                                          n1.get(),
                                          mFolderDestination,
                                          true,
                                          180s /*timeout*/,
                                          MegaTransfer::COLLISION_CHECK_ASSUMEDIFFERENT,
                                          collisionResolution);

    ASSERT_EQ(errCode, API_OK);

    errCode = sdk_test::downloadNode(megaApi[0].get(),
                                     n2.get(),
                                     mFolderDestination,
                                     true,
                                     180s /*timeout*/,
                                     MegaTransfer::COLLISION_CHECK_ASSUMEDIFFERENT,
                                     collisionResolution);

    ASSERT_EQ(errCode, API_OK);

    size_t numElements = 0;
    for (const auto& element: std::filesystem::directory_iterator(mFolderDestination))
    {
        numElements++;
        const auto fileName = element.path().filename().u8string();
        ASSERT_NE(localNames.find(fileName), localNames.end())
            << "Unexpected file name: " << fileName;
    }

    ASSERT_EQ(numElements, localNames.size());
}

/**
 * @brief TEST_F CapitalisationCollistionFileOldN
 *
 * Steps:
 * - Upload two files with names "File.txt" and "FILE.TXT"
 * - Download with conflict resolution EXISTING_TO_OLDN
 * - Check result "File.old1.txt" and "FILE.TXT"
 */
TEST_F(SdkTestCapitalisationCollision, CapitalisationCollistionFileOldN)
{
    std::set<std::string> localNames;
    if (mIsCaseInsensitive)
    {
        localNames = {"File.old1.txt", "FILE.TXT"};
    }
    else
    {
        localNames = {"File.txt", "FILE.TXT"};
    }

    std::vector<std::string> remoteNames{"File.txt", "FILE.TXT"};

    testCapitalisationFile(remoteNames,
                           localNames,
                           MegaTransfer::COLLISION_RESOLUTION_EXISTING_TO_OLDN);
}

/**
 * @brief TEST_F CapitalisationCollistionFileNewWithN
 *
 * Steps:
 * - Upload two files with names "File.txt" and "FILE.TXT"
 * - Download with conflict resolution NEW_WITH_N
 * - Check result "File.txt" and "FILE (1).TXT"
 */
TEST_F(SdkTestCapitalisationCollision, CapitalisationCollistionFileNewWithN)
{
    std::set<std::string> localNames;
    if (mIsCaseInsensitive)
    {
        localNames = {"File.txt", "FILE (1).TXT"};
    }
    else
    {
        localNames = {"File.txt", "FILE.TXT"};
    }

    std::vector<std::string> remoteNames{"File.txt", "FILE.TXT"};

    testCapitalisationFile(remoteNames, localNames, MegaTransfer::COLLISION_RESOLUTION_NEW_WITH_N);
}

/**
 * @brief TEST_F CapitalisationCollistionFileOverwrite
 *
 * Steps:
 * - Upload two files with names "File.txt" and "FILE.TXT"
 * - Download with conflict resolution OVERWRITE
 * - Check result "FILE.TXT"
 */
TEST_F(SdkTestCapitalisationCollision, CapitalisationCollistionFileOverwrite)
{
    std::set<std::string> localNames;
    if (mIsCaseInsensitive)
    {
        localNames = {"FILE.TXT"};
    }
    else
    {
        localNames = {"File.txt", "FILE.TXT"};
    }

    std::vector<std::string> remoteNames{"File.txt", "FILE.TXT"};

    testCapitalisationFile(remoteNames, localNames, MegaTransfer::COLLISION_RESOLUTION_OVERWRITE);
}

/**
 * @brief TEST_F FolderCapitalisationCollistionNewWithN
 *
 * Steps:
 * - Create in the cloud two folders with names "Folder" and "FOLDER"
 * - Download with conflict resolution NEW_WITH_N
 * - Check result "Folder" and "FOLDER (1)"
 */
TEST_F(SdkTestCapitalisationCollision, FolderCapitalisationCollistionNewWithN)
{
    std::set<std::string> localNames;
    if (mIsCaseInsensitive)
    {
        localNames = {"Folder", "FOLDER (1)"};
    }
    else
    {
        localNames = {"Folder", "FOLDER"};
    }

    std::vector<std::string> remoteNames{"Folder", "FOLDER"};
    testCapitalisationFolderCollision(remoteNames,
                                      localNames,
                                      MegaTransfer::COLLISION_RESOLUTION_NEW_WITH_N);
}

/**
 * @brief TEST_F FolderCapitalisationCollistionOldN
 *
 * Steps:
 * - Create in the cloud two folders with names "Folder" and "FOLDER"
 * - Download with conflict resolution EXISTING_TO_OLDN
 * - Check result "Folder.old1" and "FOLDER"
 */
TEST_F(SdkTestCapitalisationCollision, FolderCapitalisationCollistionOldN)
{
    std::set<std::string> localNames;
    if (mIsCaseInsensitive)
    {
        localNames = {"Folder.old1", "FOLDER"};
    }
    else
    {
        localNames = {"Folder", "FOLDER"};
    }
    std::vector<std::string> remoteNames{"Folder", "FOLDER"};
    testCapitalisationFolderCollision(remoteNames,
                                      localNames,
                                      MegaTransfer::COLLISION_RESOLUTION_EXISTING_TO_OLDN);
}

/**
 * @brief TEST_F FolderCapitalisationCollistionOverwrite
 *
 * Steps:
 * - Create in the cloud two folders with names "Folder" and "FOLDER"
 * - Download with conflict resolution OVERWRITE
 * - Check result "Folder"
 */
TEST_F(SdkTestCapitalisationCollision, FolderCapitalisationCollistionOverwrite)
{
    std::set<std::string> localNames;
    if (mIsCaseInsensitive)
    {
        localNames = {"Folder"};
    }
    else
    {
        localNames = {"Folder", "FOLDER"};
    }

    std::vector<std::string> remoteNames{"Folder", "FOLDER"};
    testCapitalisationFolderCollision(remoteNames,
                                      localNames,
                                      MegaTransfer::COLLISION_RESOLUTION_OVERWRITE);
}

/**
 * @brief TEST_F DownloadFolderWithCapitalisationCollisionNewWithN
 *
 * Steps:
 * - Create in the cloud a Folder "Test" with two subfolders with names "Folder" and "FOLDER"
 * - Download with "Test" conflict resolution NEW_WITH_N
 * - Check result (two folders, one with suffix (1) order is not guaranteed)
 */
TEST_F(SdkTestCapitalisationCollision, DownloadFolderWithCapitalisationCollisionNewWithN)
{
    size_t numElementWithSuffix = 0;
    size_t numElementWithoutSuffix = 0;
    if (mIsCaseInsensitive)
    {
        numElementWithSuffix = 1;
        numElementWithoutSuffix = 1;
    }
    else
    {
        numElementWithoutSuffix = 2;
    }
    std::vector<std::string> remoteNames{"Folder", "FOLDER"};
    testCapitalisationDownloadFolderWithCollision(remoteNames,
                                                  numElementWithSuffix,
                                                  numElementWithoutSuffix,
                                                  MegaTransfer::COLLISION_RESOLUTION_NEW_WITH_N);
}

/**
 * @brief TEST_FDownloadFolderWithCapitalisationCollisionOldN
 *
 * Steps:
 * - Create in the cloud a Folder "Test" with two subfolders with names "Folder" and "FOLDER"
 * - Download with "Test" conflict resolution EXISTING_TO_OLDN
 * - Check result (two folders, one with suffix (.old1) order is not guaranteed)
 */
TEST_F(SdkTestCapitalisationCollision, DownloadFolderWithCapitalisationCollisionOldN)
{
    size_t numElementWithSuffix = 0;
    size_t numElementWithoutSuffix = 0;
    if (mIsCaseInsensitive)
    {
        numElementWithSuffix = 1;
        numElementWithoutSuffix = 1;
    }
    else
    {
        numElementWithoutSuffix = 2;
    }
    std::vector<std::string> remoteNames{"Folder", "FOLDER"};
    testCapitalisationDownloadFolderWithCollision(
        remoteNames,
        numElementWithSuffix,
        numElementWithoutSuffix,
        MegaTransfer::COLLISION_RESOLUTION_EXISTING_TO_OLDN);
}

/**
 * @brief TEST_F DownloadFolderWithCapitalisationCollisionOverwrite
 *
 * Steps:
 * - Create in the cloud a Folder "Test" with two subfolders with names "Folder" and "FOLDER"
 * - Download with "Test" conflict resolution OVERWRITE
 * - Check result (one folder, order is not guaranteed)
 */
TEST_F(SdkTestCapitalisationCollision, DownloadFolderWithCapitalisationCollisionOverwrite)
{
    size_t numElementWithSuffix = 0;
    size_t numElementWithoutSuffix = 0;
    if (mIsCaseInsensitive)
    {
        numElementWithoutSuffix = 1;
    }
    else
    {
        numElementWithoutSuffix = 2;
    }
    std::vector<std::string> remoteNames{"Folder", "FOLDER"};
    testCapitalisationDownloadFolderWithCollision(remoteNames,
                                                  numElementWithSuffix,
                                                  numElementWithoutSuffix,
                                                  MegaTransfer::COLLISION_RESOLUTION_OVERWRITE);
}
