/**
 * @file SdkTestPasswordManager_test.cpp
 * @brief This file defines some tests for testing password manager functionalities
 */

#include "megaapi.h"
#include "megautils.h"
#include "sdk_test_utils.h"
#include "SdkTest_test.h"

#include <gmock/gmock.h>

/**
 * @class SdkTestPasswordManager
 * @brief Fixture for test suite to test password manager functionality
 *
 */
class SdkTestPasswordManager: public SdkTest
{};

/**
 * @brief TEST_F SdkTestPasswordManager
 *
 * Tests MEGA Password Manager functionality.
 *
 * Notes:
 * - Base folder created hangs from Vault root node, and it cannot be deleted
 *
 * Test description:
 * #1 Get Password Manager Base node
 * - U1: get account for test for client type password manager
 * - U1: get Password Manager Base node via get user's attribute command
 * - U1: get Password Manager Base node again; no get user attribute requests expected
 *
 * #2 Password Node CRUD operations
 * - U1: create a Password Node
 * - U1: retrieve an existing Password Node
 * - U1: update an existing Password Node
 * - U1: delete an existing Password Node
 *
 * #3 Password Node Folder CRUD operations
 * - U1: create a Password Node Folder
 * - U1: retrieve an existing Password Node Folder
 * - U1: update an existing Password Node Folder
 * - U1: delete an existing Password Node Folder
 *
 * #4 Attempt deletion of Password Manager Base node
 * - U1: try to delete Password Manager Base node
 */
TEST_F(SdkTestPasswordManager, SdkTestPasswordManager)
{
    LOG_info << "___TEST SdkTestPasswordManager";

    LOG_debug << "# U1: Get account";
    const unsigned userIdx = 0, totalAccounts = 1;
    ASSERT_NO_FATAL_FAILURE(
        getAccountsForTest(totalAccounts, true, MegaApi::CLIENT_TYPE_PASSWORD_MANAGER));

    LOG_debug << "\t# get Password Manager Base node handle";
    RequestTracker rt2{megaApi[userIdx].get()};
    megaApi[userIdx]->getPasswordManagerBase(&rt2);
    ASSERT_EQ(API_OK, rt2.waitForResult()) << "Getting Password Manager Base node failed";
    ASSERT_NE(nullptr, rt2.request) << "Missing getPasswordManagerBase request data after finish";
    const MegaHandle nhBase = rt2.request->getNodeHandle();
    LOG_debug << "\t# get Password Manager Base node by handle";
    std::unique_ptr<MegaNode> mnBase{megaApi[userIdx]->getNodeByHandle(nhBase)};
    ASSERT_NE(nullptr, mnBase.get())
        << "Error retrieving MegaNode for Password Base with handle " << toNodeHandle(nhBase);

    LOG_debug << "# U1: get Password Manager Base via get user's attribute command";
    RequestTracker rt3{megaApi[userIdx].get()};
    megaApi[userIdx]->getUserAttribute(MegaApi::USER_ATTR_PWM_BASE, &rt3);
    ASSERT_EQ(API_OK, rt3.waitForResult()) << "Unexpected error retrieving pwmh user attribute";
    ASSERT_NE(nullptr, rt3.request) << "Missing get user attribute pwmh request data after finish";
    ASSERT_EQ(nhBase, rt3.request->getNodeHandle()) << "Mismatch in user attribute pwmh retrieved";

    LOG_debug << "# U1: create a new Password Node under Password Manager Base";
    RequestTracker rtC{megaApi[userIdx].get()};
    const std::string pwdNodeName = "FirstPwd";
    std::unique_ptr<MegaNode> existingPwdNode{
        megaApi[userIdx]->getChildNode(mnBase.get(), pwdNodeName.c_str())};
    std::unique_ptr<MegaNode::PasswordNodeData> pwdData{
        MegaNode::PasswordNodeData::createInstance("12},\" '34", "notes", "url", "userName")};
    bool check1;
    mApi[userIdx].mOnNodesUpdateCompletion =
        createOnNodesUpdateLambda(INVALID_HANDLE, MegaNode::CHANGE_TYPE_NEW, check1);
    megaApi[userIdx]->createPasswordNode(pwdNodeName.c_str(), pwdData.get(), nhBase, &rtC);
    ASSERT_EQ(API_OK, rtC.waitForResult()) << "Failure creating Password Node";
    if (existingPwdNode)
    {
        LOG_debug << "Existing Password Node with the same name retrieved";
    }
    else
    {
        ASSERT_TRUE(waitForResponse(&check1))
            << "Node creation not received after " << maxTimeout << " seconds";
    }
    ASSERT_NE(nullptr, rtC.request);
    const auto newPwdNodeHandle = rtC.request->getNodeHandle();
    ASSERT_NE(UNDEF, newPwdNodeHandle) << "Wrong MegaHandle for new Password Node";
    const std::unique_ptr<MegaNode> newPwdNode{megaApi[userIdx]->getNodeByHandle(newPwdNodeHandle)};
    ASSERT_NE(newPwdNode.get(), nullptr) << "New node could not be retrieved";
    ASSERT_TRUE(newPwdNode->isPasswordNode());
    ASSERT_FALSE(megaApi[userIdx]->isPasswordNodeFolder(newPwdNode->getHandle()));
    auto aux = newPwdNode->getName();
    ASSERT_NE(aux, nullptr);
    ASSERT_STREQ(aux, newPwdNode->getName());
    std::unique_ptr<MegaNode::PasswordNodeData> receivedPwdData{newPwdNode->getPasswordData()};
    ASSERT_NE(nullptr, receivedPwdData);
    const auto equals = [](const MegaNode::PasswordNodeData* lhs,
                           const MegaNode::PasswordNodeData* rhs) -> bool
    {
        std::string lp = lhs->password() ? lhs->password() : "";
        std::string rp = rhs->password() ? rhs->password() : "";
        if (lp != rp)
            LOG_err << "\tTest: passwords differ |" << lp << "| != |" << rp << "|";

        std::string ln = lhs->notes() ? lhs->notes() : "";
        std::string rn = rhs->notes() ? rhs->notes() : "";
        if (ln != rn)
            LOG_err << "\tTest: notes differ |" << ln << "| != |" << rn << "|";

        std::string lu = lhs->url() ? lhs->url() : "";
        std::string ru = rhs->url() ? rhs->url() : "";
        if (lu != ru)
            LOG_err << "\tTest: urls differ |" << lu << "| != |" << ru << "|";

        std::string lun = lhs->userName() ? lhs->userName() : "";
        std::string run = rhs->userName() ? rhs->userName() : "";
        if (lun != run)
            LOG_err << "\tTest: userNames differ |" << lun << "| != |" << run << "|";

        return (lp == rp && ln == rn && lu == ru && lun == run);
    };
    ASSERT_TRUE(equals(pwdData.get(), receivedPwdData.get()));
    {
        LOG_debug << "\t# validate & verify copy/cloning capabilities of Password Node Data";
        std::unique_ptr<MegaNode> clonedNode{newPwdNode->copy()};
        std::unique_ptr<MegaNode::PasswordNodeData> clonedPwdData{clonedNode->getPasswordData()};
        ASSERT_NE(nullptr, clonedPwdData);
        ASSERT_TRUE(equals(clonedPwdData.get(), receivedPwdData.get()));
    }

    LOG_debug << "\t# U1: attempt creation of new Password Node with same name as existing one";
    RequestTracker rtCErrorExists{megaApi[userIdx].get()};
    megaApi[userIdx]->createPasswordNode(pwdNodeName.c_str(),
                                         pwdData.get(),
                                         nhBase,
                                         &rtCErrorExists);
    ASSERT_EQ(API_EEXIST, rtCErrorExists.waitForResult());

    LOG_debug << "\t# U1: attempt creation of new Password Node with wrong parameters";
    RequestTracker rtCError{megaApi[userIdx].get()};
    megaApi[userIdx]->createPasswordNode(nullptr, nullptr, INVALID_HANDLE, &rtCError);
    ASSERT_EQ(API_EARGS, rtCError.waitForResult());

    LOG_debug << "# U1: retrieve Password Node by NodeHandle";
    std::unique_ptr<MegaNode> retrievedPwdNode{megaApi[userIdx]->getNodeByHandle(newPwdNodeHandle)};
    ASSERT_NE(nullptr, retrievedPwdNode.get());
    ASSERT_TRUE(retrievedPwdNode->isPasswordNode());
    retrievedPwdNode.reset(megaApi[userIdx]->getNodeByHandle(nhBase));
    ASSERT_NE(nullptr, retrievedPwdNode.get());
    ASSERT_FALSE(retrievedPwdNode->isPasswordNode());

    LOG_debug << "# U1: update Password Node";
    const char* nName = "SecondPwd";
    LOG_debug << "\t# rename the Password Node";
    ASSERT_EQ(API_OK, doRenameNode(userIdx, newPwdNode.get(), nName));
    retrievedPwdNode.reset(megaApi[userIdx]->getNodeByHandle(newPwdNodeHandle));
    ASSERT_NE(nullptr, retrievedPwdNode.get());
    ASSERT_TRUE(retrievedPwdNode->isPasswordNode());
    aux = retrievedPwdNode->getName();
    ASSERT_NE(nullptr, aux);
    ASSERT_STREQ(nName, aux) << "Password Node name not updated correctly";
    receivedPwdData.reset(retrievedPwdNode->getPasswordData());
    ASSERT_NE(nullptr, receivedPwdData);
    ASSERT_TRUE(equals(pwdData.get(), receivedPwdData.get()));

    LOG_debug << "\t# update only password attribute providing all attributes";
    const char* nPwd = "5678";
    pwdData->setPassword(nPwd);
    check1 = false;
    mApi[userIdx].mOnNodesUpdateCompletion =
        createOnNodesUpdateLambda(newPwdNode->getHandle(), MegaNode::CHANGE_TYPE_PWD, check1);
    RequestTracker rtUpdate{megaApi[userIdx].get()};
    megaApi[userIdx]->updatePasswordNode(newPwdNodeHandle, pwdData.get(), &rtUpdate);
    ASSERT_EQ(API_OK, rtUpdate.waitForResult());
    ASSERT_TRUE(waitForResponse(&check1))
        << "Node update not received after " << maxTimeout << " seconds";
    const auto isExpectedData =
        [&api = megaApi[userIdx], &equals](MegaHandle nh,
                                           const MegaNode::PasswordNodeData* expectedData)
    {
        std::unique_ptr<MegaNode> retrievedNode{api->getNodeByHandle(nh)};
        ASSERT_TRUE(retrievedNode);
        ASSERT_TRUE(retrievedNode->isPasswordNode());
        std::unique_ptr<MegaNode::PasswordNodeData> retrievedData{retrievedNode->getPasswordData()};
        ASSERT_TRUE(retrievedData);
        ASSERT_TRUE(equals(expectedData, retrievedData.get()));
    };
    ASSERT_NO_FATAL_FAILURE(isExpectedData(newPwdNodeHandle, pwdData.get()));

    LOG_debug << "\t# update only notes attribute (the non-updated attributes should be the same)";
    const char* newNotes = "Updated Notes";
    pwdData->setNotes(newNotes); // expected data
    std::unique_ptr<MegaNode::PasswordNodeData> updatedData{
        MegaNode::PasswordNodeData::createInstance(nullptr, newNotes, nullptr, nullptr)};
    check1 = false;
    mApi[userIdx].mOnNodesUpdateCompletion =
        createOnNodesUpdateLambda(newPwdNode->getHandle(), MegaNode::CHANGE_TYPE_PWD, check1);
    RequestTracker rtUNotes{megaApi[userIdx].get()};
    megaApi[userIdx]->updatePasswordNode(newPwdNodeHandle, updatedData.get(), &rtUNotes);
    ASSERT_EQ(API_OK, rtUNotes.waitForResult());
    ASSERT_TRUE(waitForResponse(&check1))
        << "Notes node update not received after " << maxTimeout << " seconds";
    ASSERT_NO_FATAL_FAILURE(isExpectedData(newPwdNodeHandle, pwdData.get()));

    LOG_debug << "\t# update only url attribute (the non-updated attributes should be the same)";
    const char* newURL = "Updated url";
    pwdData->setUrl(newURL); // expected data
    updatedData.reset(
        MegaNode::PasswordNodeData::createInstance(nullptr, nullptr, newURL, nullptr));
    check1 = false;
    mApi[userIdx].mOnNodesUpdateCompletion =
        createOnNodesUpdateLambda(newPwdNode->getHandle(), MegaNode::CHANGE_TYPE_PWD, check1);
    RequestTracker rtUURL{megaApi[userIdx].get()};
    megaApi[userIdx]->updatePasswordNode(newPwdNodeHandle, updatedData.get(), &rtUURL);
    ASSERT_EQ(API_OK, rtUURL.waitForResult());
    ASSERT_TRUE(waitForResponse(&check1))
        << "URL node update not received after " << maxTimeout << " seconds";
    ASSERT_NO_FATAL_FAILURE(isExpectedData(newPwdNodeHandle, pwdData.get()));

    LOG_debug
        << "\t# update only user name attribute (the non-updated attributes should be the same)";
    const char* newUserName = "Updated userName";
    pwdData->setUserName(newUserName); // expected data
    updatedData.reset(
        MegaNode::PasswordNodeData::createInstance(nullptr, nullptr, nullptr, newUserName));
    check1 = false;
    mApi[userIdx].mOnNodesUpdateCompletion =
        createOnNodesUpdateLambda(newPwdNode->getHandle(), MegaNode::CHANGE_TYPE_PWD, check1);
    RequestTracker rtUUserName{megaApi[userIdx].get()};
    megaApi[userIdx]->updatePasswordNode(newPwdNodeHandle, updatedData.get(), &rtUUserName);
    ASSERT_EQ(API_OK, rtUUserName.waitForResult());
    ASSERT_TRUE(waitForResponse(&check1))
        << "User name node update not received after " << maxTimeout << " seconds";
    ASSERT_NO_FATAL_FAILURE(isExpectedData(newPwdNodeHandle, pwdData.get()));

    LOG_debug << "\t# update attempt without new data";
    RequestTracker rtUError1{megaApi[userIdx].get()};
    megaApi[userIdx]->updatePasswordNode(newPwdNodeHandle, nullptr, &rtUError1);
    ASSERT_EQ(API_EARGS, rtUError1.waitForResult());

    LOG_debug << "\t# update attempt with empty new data";
    pwdData.reset(MegaNode::PasswordNodeData::createInstance(nullptr, nullptr, nullptr, nullptr));
    RequestTracker rtUError2{megaApi[userIdx].get()};
    megaApi[userIdx]->updatePasswordNode(newPwdNodeHandle, pwdData.get(), &rtUError2);
    ASSERT_EQ(API_EARGS, rtUError2.waitForResult());

    LOG_debug << "# U1: delete Password Node";
    ASSERT_EQ(API_OK, doDeleteNode(userIdx, retrievedPwdNode.get()));
    retrievedPwdNode.reset(megaApi[userIdx]->getNodeByHandle(newPwdNodeHandle));
    ASSERT_EQ(nullptr, retrievedPwdNode.get());

    LOG_debug << "# U1: create a new Password Node Folder";
    const char* newFolderName = "NewPasswordNodeFolder";
    const MegaHandle nhPNFolder = createFolder(userIdx, newFolderName, mnBase.get());
    ASSERT_NE(INVALID_HANDLE, nhPNFolder);

    LOG_debug << "# U1: retrieve newly created Password Node Folder";
    std::unique_ptr<MegaNode> mnPNFolder{megaApi[userIdx]->getNodeByHandle(nhPNFolder)};
    ASSERT_NE(nullptr, mnPNFolder);
    ASSERT_TRUE(megaApi[userIdx]->isPasswordNodeFolder(mnPNFolder->getHandle()));
    ASSERT_STREQ(newFolderName, mnPNFolder->getName());

    LOG_debug << "# U1: update (rename) an existing Password Node Folder";
    const char* updatedFolderName = "UpdatedPNF";
    ASSERT_EQ(API_OK, doRenameNode(userIdx, mnPNFolder.get(), updatedFolderName));
    mnPNFolder.reset(megaApi[userIdx]->getNodeByHandle(nhPNFolder));
    ASSERT_NE(nullptr, mnPNFolder);
    ASSERT_TRUE(megaApi[userIdx]->isPasswordNodeFolder(mnPNFolder->getHandle()));
    ASSERT_STREQ(updatedFolderName, mnPNFolder->getName());

    LOG_debug << "# U1: delete an existing Password Node Folder";
    ASSERT_EQ(API_OK, doDeleteNode(userIdx, mnPNFolder.get()));
    mnPNFolder.reset(megaApi[userIdx]->getNodeByHandle(nhPNFolder));
    ASSERT_EQ(nullptr, mnPNFolder);

    LOG_debug << "\t# deletion attempted with Password Manager Base as handle";
    ASSERT_EQ(API_EARGS, doDeleteNode(userIdx, mnBase.get()));
}

/**
 * @brief SdkTestImportPassword
 *
 *  - Create a local file to import
 *  - Get password node base
 *  - Import google cvs file
 */
TEST_F(SdkTestPasswordManager, SdkTestImportPassword)
{
    LOG_info << "___TEST SdkTestImportPassword___";
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1, true, MegaApi::CLIENT_TYPE_PASSWORD_MANAGER));

    LOG_debug << "# Create csv file";
    constexpr std::string_view fileContents{R"(name,url,username,password,note
foo.com,https://foo.com/,tx,"hola""""\""\"".,,",
hello.co,https://hello.co/,hello,hello.1234,Description with ñ
test.com,https://test.com/,test3,"hello.12,34",
test.com,https://test.com/,txema,hel\nlo.1234,""
test2.com,https://test2.com/,test,hello.1234,
)"};

    const std::string fname = "test.csv";
    sdk_test::LocalTempFile f{fname, fileContents};

    LOG_debug << "# Get Password Manager Base";
    RequestTracker rtPasswordManagerBase(megaApi[0].get());
    megaApi[0]->getPasswordManagerBase(&rtPasswordManagerBase);
    rtPasswordManagerBase.waitForResult();
    MegaHandle parentHandle = rtPasswordManagerBase.getNodeHandle();
    ASSERT_NE(parentHandle, INVALID_HANDLE);
    std::shared_ptr<MegaNode> parent{megaApi[0]->getNodeByHandle(parentHandle)};
    ASSERT_TRUE(parent);
    MrProper cleanup(
        [parent, this]()
        {
            purgeTree(0, parent.get(), false);
        });

    LOG_debug << "# Import google csv file";
    RequestTracker rt(megaApi[0].get());
    megaApi[0]->importPasswordsFromFile(fname.c_str(),
                                        MegaApi::IMPORT_PASSWORD_SOURCE_GOOGLE,
                                        parentHandle,
                                        &rt);
    ASSERT_EQ(rt.waitForResult(), API_OK);
    MegaHandleList* handleList = rt.request->getMegaHandleList();
    ASSERT_TRUE(handleList);
    ASSERT_EQ(handleList->size(), 5);
    std::unique_ptr<MegaNodeList> list{megaApi[0]->getChildren(parent.get())};
    ASSERT_TRUE(list);
    ASSERT_THAT(toNamesVector(*list),
                testing::UnorderedElementsAre("foo.com",
                                              "hello.co",
                                              "test.com",
                                              "test.com (1)",
                                              "test2.com"));

    MegaStringIntegerMap* stringIntegerList = rt.request->getMegaStringIntegerMap();
    ASSERT_TRUE(stringIntegerList);
    ASSERT_EQ(stringIntegerList->size(), 0);
}

/**
 * @brief SdkTestImportPasswordFails
 *
 *  - Try to import password node file with invalid path
 *  - Try to import password node file from google with empty file
 *  - Try to import password node file from google with invalid rows
 */
TEST_F(SdkTestPasswordManager, SdkTestImportPasswordFails)
{
    LOG_info << "___TEST SdkTestImportPasswordFails";
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1, true, MegaApi::CLIENT_TYPE_PASSWORD_MANAGER));

    LOG_debug << "# Get Password Manager Base";
    RequestTracker rtPasswordManagerBase(megaApi[0].get());
    megaApi[0]->getPasswordManagerBase(&rtPasswordManagerBase);
    rtPasswordManagerBase.waitForResult();
    MegaHandle parentHandle = rtPasswordManagerBase.getNodeHandle();
    ASSERT_NE(parentHandle, INVALID_HANDLE);
    std::shared_ptr<MegaNode> parent{megaApi[0]->getNodeByHandle(parentHandle)};
    ASSERT_TRUE(parent);

    {
        LOG_debug << "# Import google csv file - null path";
        RequestTracker rt(megaApi[0].get());
        megaApi[0]->importPasswordsFromFile("",
                                            MegaApi::IMPORT_PASSWORD_SOURCE_GOOGLE,
                                            parentHandle,
                                            &rt);
        ASSERT_EQ(rt.waitForResult(), API_EREAD);
    }

    {
        LOG_debug << "# Import google csv file - empty file";
        const std::string fname = "test.csv";
        sdk_test::LocalTempFile f{fname, 0};
        RequestTracker rt(megaApi[0].get());
        megaApi[0]->importPasswordsFromFile(fname.c_str(),
                                            MegaApi::IMPORT_PASSWORD_SOURCE_GOOGLE,
                                            parentHandle,
                                            &rt);
        ASSERT_EQ(rt.waitForResult(), API_EACCESS);
    }

    {
        LOG_debug << "# Create csv file";
        constexpr std::string_view fileContents{R"(name,url,username,password,note
name,https://foo.com/,username,password,note
name2,https://foo.com/,username,,note
name3,username,password,note
)"};

        const std::string fname = "test.csv";
        sdk_test::LocalTempFile f{fname, fileContents};

        MrProper cleanup(
            [parent, this]()
            {
                purgeTree(0, parent.get(), false);
            });

        RequestTracker rt(megaApi[0].get());
        megaApi[0]->importPasswordsFromFile(fname.c_str(),
                                            MegaApi::IMPORT_PASSWORD_SOURCE_GOOGLE,
                                            parentHandle,
                                            &rt);
        ASSERT_EQ(rt.waitForResult(), API_OK);
        MegaHandleList* handleList = rt.request->getMegaHandleList();
        ASSERT_TRUE(handleList);
        ASSERT_EQ(handleList->size(), 1);

        MegaStringIntegerMap* stringIntegerList = rt.request->getMegaStringIntegerMap();
        ASSERT_TRUE(stringIntegerList);
        ASSERT_EQ(stringIntegerList->size(), 2);

        std::unique_ptr<MegaStringList> keys{stringIntegerList->getKeys()};
        ASSERT_TRUE(keys);

        for (int i = 0; i < keys->size(); ++i)
        {
            ASSERT_TRUE(keys->get(i));
            std::string key{keys->get(i)};
            std::unique_ptr<MegaIntegerList> badEntries{stringIntegerList->get(key.c_str())};
            ASSERT_TRUE(badEntries);
            ASSERT_EQ(badEntries->size(), 1);
            std::vector<int64_t> errors{MegaApi::IMPORTED_PASSWORD_ERROR_PARSER,
                                        MegaApi::IMPORTED_PASSWORD_ERROR_MISSINGPASSWORD};
            ASSERT_THAT(errors, testing::Contains(badEntries->get(0)));

            ASSERT_NE(fileContents.find(key), std::string::npos);
        }
    }
}
