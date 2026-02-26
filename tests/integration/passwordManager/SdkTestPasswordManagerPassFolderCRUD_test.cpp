/**
 * @file
 * @brief Test CRUD operation on password folders
 */

#include "mock_listeners.h"
#include "SdkTestPasswordManager.h"

#include <gmock/gmock.h>

using namespace testing;

TEST_F(SdkTestPasswordManager, CreateNewPassFolderNode)
{
    const auto mnBase = getBaseNode();
    const auto folderName = getFilePrefix();
    ASSERT_NE(INVALID_HANDLE, createFolder(0, folderName.c_str(), mnBase.get()));
}

class SdkTestPasswordManagerPassFolderCRUD: public SdkTestPasswordManager
{
public:
    void SetUp() override
    {
        SdkTestPasswordManager::SetUp();
        const auto mnBase = getBaseNode();
        const auto folderName = getFolderName();
        folderHandle = createFolder(0, folderName.c_str(), mnBase.get());
        ASSERT_NE(INVALID_HANDLE, folderHandle);
    }

    void TearDown() override
    {
        if (const auto folder = getFolderNode(); folder)
            doDeleteNode(0, folder.get());
        SdkTestPasswordManager::TearDown();
    }

    std::string getFolderName() const
    {
        return getFilePrefix();
    }

    handle getFolderHandle() const
    {
        return folderHandle;
    }

    std::unique_ptr<MegaNode> getFolderNode() const
    {
        return std::unique_ptr<MegaNode>{mApi->getNodeByHandle(getFolderHandle())};
    }

private:
    handle folderHandle{INVALID_HANDLE};
};

TEST_F(SdkTestPasswordManagerPassFolderCRUD, GetPassFolder)
{
    const auto mnPNFolder = getFolderNode();
    ASSERT_NE(nullptr, mnPNFolder);
    ASSERT_TRUE(mApi->isPasswordManagerNodeFolder(mnPNFolder->getHandle()));
    ASSERT_EQ(getFolderName(), mnPNFolder->getName());
}

TEST_F(SdkTestPasswordManagerPassFolderCRUD, RenameFolderName)
{
    const char* updatedFolderName = "UpdatedPNF";
    auto folder = getFolderNode();

    static const auto logPre = getLogPrefix();
    LOG_debug << logPre << "Renaming folder";
    ASSERT_NE(nullptr, folder);
    ASSERT_EQ(API_OK, doRenameNode(0, folder.get(), updatedFolderName));

    LOG_debug << logPre << "Validating new name";
    folder = getFolderNode();
    ASSERT_NE(nullptr, folder);
    ASSERT_TRUE(mApi->isPasswordManagerNodeFolder(folder->getHandle()));
    ASSERT_STREQ(updatedFolderName, folder->getName());
}

TEST_F(SdkTestPasswordManagerPassFolderCRUD, DeleteFolder)
{
    auto folder = getFolderNode();
    ASSERT_TRUE(folder);
    ASSERT_EQ(API_OK, doDeleteNode(0, folder.get()));
    folder.reset(mApi->getNodeByHandle(getFolderHandle()));
    ASSERT_EQ(nullptr, folder);
}
