/**
 * @file tests/sdk_test.cpp
 * @brief Mega SDK test file
 *
 * (c) 2015 by Mega Limited, Wellsford, New Zealand
 *
 * This file is part of the MEGA SDK - Client Access Engine.
 *
 * Applications using the MEGA API must present a valid application key
 * and comply with the the rules set forth in the Terms of Service.
 *
 * The MEGA SDK is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * @copyright Simplified (2-clause) BSD License.
 *
 * You should have received a copy of the license along with this
 * program.
 */

#include "sdk_test.h"

void SdkTest::SetUp()
{
    // do some initialization

    char *buf = getenv("MEGA_EMAIL");
    if (buf)
        email.assign(buf);
    ASSERT_LT(0, email.length()) << "Set your username at the environment variable $MEGA_EMAIL";

    buf = getenv("MEGA_PWD");
    if (buf)
        pwd.assign(buf);
    ASSERT_LT(0, pwd.length()) << "Set your password at the environment variable $MEGA_PWD";

    if (megaApi == NULL)
    {
        logger = new MegaLoggerSDK("SDK.log");
        MegaApi::setLoggerObject(logger);

        char path[1024];
        getcwd(path, sizeof path);
        megaApi = new MegaApi(APP_KEY.c_str(), path, USER_AGENT.c_str());

        megaApi->setLogLevel(MegaApi::LOG_LEVEL_DEBUG);
        megaApi->addListener(this);

        ASSERT_NO_FATAL_FAILURE( login() );
        ASSERT_NO_FATAL_FAILURE( fetchnodes() );
    }
}

void SdkTest::TearDown()
{
    // do some cleanup

    deleteFile(UPFILE);
    deleteFile(DOWNFILE);
    deleteFile(PUBLICFILE);
    deleteFile(AVATARDST);

    releaseMegaApiAux();

    if (megaApi)
    {
        // Remove nodes in Cloud & Rubbish
        purgeTree(megaApi->getRootNode());
        purgeTree(megaApi->getRubbishNode());
//        megaApi->cleanRubbishBin();

        // Remove auxiliar contact
        MegaUserList *ul = megaApi->getContacts();
        for (int i = 0; i < ul->size(); i++)
        {
            removeContact(ul->get(i)->getEmail());
        }

        // Remove pending contact requests
        MegaContactRequestList *crl = megaApi->getOutgoingContactRequests();
        for (int i = 0; i < crl->size(); i++)
        {
            MegaContactRequest *cr = crl->get(i);
            megaApi->inviteContact(cr->getTargetEmail(), "Removing you", MegaContactRequest::INVITE_ACTION_DELETE);
        }

        if (megaApi->isLoggedIn())
            ASSERT_NO_FATAL_FAILURE( logout() );

        delete megaApi;

        MegaApi::setLoggerObject(NULL);
        delete logger;
    }
}

void SdkTest::onRequestFinish(MegaApi *api, MegaRequest *request, MegaError *e)
{
    unsigned int apiIndex = (api == megaApi) ? 0 : 1;
    requestFlags[apiIndex][request->getType()] = true;

    lastError = e->getErrorCode();

    switch(request->getType())
    {
    case MegaRequest::TYPE_CREATE_FOLDER:
        h = request->getNodeHandle();
        break;

    case MegaRequest::TYPE_COPY:
        h = request->getNodeHandle();
        break;

    case MegaRequest::TYPE_EXPORT:
        if (request->getAccess())
            link.assign(request->getLink());
        h = request->getNodeHandle();
        break;

    case MegaRequest::TYPE_GET_PUBLIC_NODE:
        if (lastError == API_OK)
            publicNode = request->getPublicMegaNode();
        break;

    case MegaRequest::TYPE_IMPORT_LINK:
        h = request->getNodeHandle();
        break;

    case MegaRequest::TYPE_GET_ATTR_USER:
        if ( (lastError == API_OK) && (request->getParamType() != MegaApi::USER_ATTR_AVATAR) )
            attributeValue = request->getText();

        if (request->getParamType() == MegaApi::USER_ATTR_AVATAR)
        {
            if (lastError == API_OK)
                attributeValue = "Avatar changed";

            if (lastError == API_ENOENT)
                attributeValue = "Avatar not found";
        }
        break;

#ifdef ENABLE_CHAT
    case MegaRequest::TYPE_CHAT_FETCH:
        if (lastError == API_OK)
        {
            chats = request->getMegaTextChatList()->copy();
        }
        break;

    case MegaRequest::TYPE_CHAT_CREATE:
        if (lastError == API_OK)
        {
            chats = request->getMegaTextChatList()->copy();
        }
        break;

    case MegaRequest::TYPE_CHAT_URL:
        if (lastError == API_OK)
        {
            link.assign(request->getLink());
        }
        break;
#endif

    }
}

void SdkTest::onTransferFinish(MegaApi* api, MegaTransfer *transfer, MegaError* e)
{
    unsigned int apiIndex = (api == megaApi) ? 0 : 1;
    transferFlags[apiIndex][transfer->getType()] = true;

    lastError = e->getErrorCode();

    if (lastError == MegaError::API_OK)
        h = transfer->getNodeHandle();
}

void SdkTest::onUsersUpdate(MegaApi* api, MegaUserList *users)
{
    unsigned int apiIndex = (api == megaApi) ? 0 : 1;

    if (!users)
        return;

    for (int i = 0; i < users->size(); i++)
    {
        MegaUser *u = users->get(i);

        if (u->hasChanged(MegaUser::CHANGE_TYPE_AVATAR)
                || u->hasChanged(MegaUser::CHANGE_TYPE_FIRSTNAME)
                || u->hasChanged(MegaUser::CHANGE_TYPE_LASTNAME))
        {
            userUpdated[apiIndex] = true;
        }
        else
        {
            // Contact is removed from main account
            requestFlags[apiIndex][MegaRequest::TYPE_REMOVE_CONTACT] = true;
        }
    }
}

void SdkTest::onNodesUpdate(MegaApi* api, MegaNodeList *nodes)
{
    unsigned int apiIndex = (api == megaApi) ? 0 : 1;
    nodeUpdated[apiIndex] = true;
}

void SdkTest::onContactRequestsUpdate(MegaApi* api, MegaContactRequestList* requests)
{
    unsigned int apiIndex = (api == megaApi) ? 0 : 1;
    contactRequestUpdated[apiIndex] = true;
}

#ifdef ENABLE_CHAT
void SdkTest::onChatsUpdate(MegaApi *api, MegaTextChatList *chats)
{
    unsigned int apiIndex = (api == megaApi) ? 0 : 1;
    chatUpdated[apiIndex] = true;
}

void SdkTest::fetchChats(int timeout)
{
    requestFlags[0][MegaRequest::TYPE_CHAT_FETCH] = false;
    megaApi->fetchChats();
    waitForResponse(&requestFlags[0][MegaRequest::TYPE_CHAT_FETCH], timeout);

    if (timeout)
    {
        ASSERT_TRUE(requestFlags[0][MegaRequest::TYPE_CHAT_FETCH]) << "Fetching chats not finished after " << timeout  << " seconds";
    }

    ASSERT_EQ(MegaError::API_OK, lastError) << "Fetching list of chats failed (error: " << lastError << ")";
}

void SdkTest::createChat(bool group, MegaTextChatPeerList *peers, int timeout)
{
    requestFlags[0][MegaRequest::TYPE_CHAT_CREATE] = false;
    megaApi->createChat(group, peers);
    waitForResponse(&requestFlags[0][MegaRequest::TYPE_CHAT_FETCH], timeout);
    if (timeout)
    {
        ASSERT_TRUE(requestFlags[0][MegaRequest::TYPE_CHAT_FETCH]) << "Chat creation not finished after " << timeout  << " seconds";
    }

    ASSERT_EQ(MegaError::API_OK, lastError) << "Chat creation failed (error: " << lastError << ")";
}

#endif

void SdkTest::login(int timeout)
{
    requestFlags[0][MegaRequest::TYPE_LOGIN] = false;
    megaApi->login(email.data(), pwd.data());
    waitForResponse(&requestFlags[0][MegaRequest::TYPE_LOGIN], timeout);

    if (timeout)
    {
        ASSERT_TRUE(requestFlags[0][MegaRequest::TYPE_LOGIN]) << "Logging failed after " << timeout  << " seconds";
    }

    ASSERT_EQ(MegaError::API_OK, lastError) << "Logging failed (error: " << lastError << ")";
    ASSERT_TRUE(megaApi->isLoggedIn()) << "Not logged it";
}

void SdkTest::fetchnodes(int timeout)
{
    requestFlags[0][MegaRequest::TYPE_FETCH_NODES] = false;
    megaApi->fetchNodes();
    waitForResponse(&requestFlags[0][MegaRequest::TYPE_FETCH_NODES], timeout);

    if (timeout)
    {
        ASSERT_TRUE(requestFlags[0][MegaRequest::TYPE_FETCH_NODES]) << "Fetchnodes failed after " << timeout  << " seconds";
    }

    ASSERT_EQ(MegaError::API_OK, lastError) << "Fetchnodes failed (error: " << lastError << ")";
}

void SdkTest::logout(int timeout)
{
    requestFlags[0][MegaRequest::TYPE_LOGOUT] = false;
    megaApi->logout(this);
    waitForResponse(&requestFlags[0][MegaRequest::TYPE_LOGOUT], timeout);

    if (timeout)
    {
        EXPECT_TRUE(requestFlags[0][MegaRequest::TYPE_LOGOUT]) << "Logout failed after " << timeout  << " seconds";
    }

    // if the connection was closed before the response of the request was received, the result is ESID
    if (lastError == MegaError::API_ESID) lastError = MegaError::API_OK;

    EXPECT_EQ(MegaError::API_OK, lastError) << "Logout failed (error: " << lastError << ")";
}

char* SdkTest::dumpSession()
{
    return megaApi->dumpSession();
}

void SdkTest::locallogout(int timeout)
{
    requestFlags[0][MegaRequest::TYPE_LOGOUT] = false;
    megaApi->localLogout(this);
    waitForResponse(&requestFlags[0][MegaRequest::TYPE_LOGOUT], timeout);

    if (timeout)
    {
        EXPECT_TRUE(requestFlags[0][MegaRequest::TYPE_LOGOUT]) << "Local logout failed after " << timeout  << " seconds";
    }

    ASSERT_EQ(MegaError::API_OK, lastError) << "Local logout failed (error: " << lastError << ")";
}

void SdkTest::resumeSession(char *session, int timeout)
{
    requestFlags[0][MegaRequest::TYPE_LOGIN] = false;
    megaApi->fastLogin(session, this);
    waitForResponse(&requestFlags[0][MegaRequest::TYPE_LOGIN], timeout);

    if (timeout)
    {
        ASSERT_TRUE(requestFlags[0][MegaRequest::TYPE_LOGIN]) << "Resume session failed after " << timeout  << " seconds";
    }

    ASSERT_EQ(MegaError::API_OK, lastError) << "Resume session failed (error: " << lastError << ")";
}

void SdkTest::purgeTree(MegaNode *p)
{
    MegaNodeList *children;
    children = megaApi->getChildren(p);

    for (int i = 0; i < children->size(); i++)
    {
        MegaNode *n = children->get(i);
        if (n->isFolder())
            purgeTree(n);

        megaApi->remove(n);
    }
}

void SdkTest::waitForResponse(bool *responseReceived, int timeout)
{
    timeout *= 1000000; // convert to micro-seconds
    int tWaited = 0;    // microseconds
    while(!(*responseReceived))
    {
        usleep(pollingT);

        if (timeout)
        {
            tWaited += pollingT;
            if (tWaited >= timeout)
            {
                break;
            }
        }
    }
}

void SdkTest::createFile(string filename, bool largeFile)
{
    FILE *fp;
    fp = fopen(filename.c_str(), "w");

    if (fp)
    {
        int limit = 2000;

        // create a file large enough for long upload/download times (5-10MB)
        if (largeFile)
            limit = 1000000 + rand() % 1000000;

        for (int i = 0; i < limit; i++)
        {
            fprintf(fp, "test ");
        }

        fclose(fp);
    }
}

size_t SdkTest::getFilesize(string filename)
{
    struct stat stat_buf;
    int rc = stat(filename.c_str(), &stat_buf);

    return rc == 0 ? stat_buf.st_size : -1;
}

void SdkTest::deleteFile(string filename)
{
    remove(filename.c_str());
}


void SdkTest::getMegaApiAux()
{
    if (megaApiAux == NULL)
    {
        char *buf;

        buf = getenv("MEGA_EMAIL_AUX");
        if (buf)
            emailaux.assign(buf);
        ASSERT_LT(0, emailaux.length()) << "Set auxiliar username at the environment variable $MEGA_EMAIL_AUX";

        string pwdaux;
        buf = getenv("MEGA_PWD_AUX");
        if (buf)
            pwdaux.assign(buf);
        ASSERT_LT(0, pwdaux.length()) << "Set the auxiliar password at the environment variable $MEGA_PWD_AUX";

        char path[1024];
        getcwd(path, sizeof path);
        megaApiAux = new MegaApi(APP_KEY.c_str(), path, USER_AGENT.c_str());

        megaApiAux->setLogLevel(MegaApi::LOG_LEVEL_DEBUG);
        megaApiAux->addListener(this);

        requestFlags[1][MegaRequest::TYPE_LOGIN] = false;
        megaApiAux->login(emailaux.data(), pwdaux.data());
        waitForResponse(&requestFlags[1][MegaRequest::TYPE_LOGIN]);

        ASSERT_EQ(MegaError::API_OK, lastError) << "Logging failed in the auxiliar account (error: " << lastError << ")";
        ASSERT_TRUE(megaApiAux->isLoggedIn()) << "Login failed in the auxiliar account";

        requestFlags[1][MegaRequest::TYPE_FETCH_NODES] = false;
        megaApiAux->fetchNodes();
        waitForResponse(&requestFlags[1][MegaRequest::TYPE_FETCH_NODES]);

        ASSERT_EQ(MegaError::API_OK, lastError) << "Fetchnodes failed in the auxiliar account (error: " << lastError << ")";
    }
}

void SdkTest::releaseMegaApiAux()
{
    if (megaApiAux)
    {
        if (megaApiAux->isLoggedIn())
        {
            requestFlags[1][MegaRequest::TYPE_LOGOUT] = false;
            megaApiAux->logout();
            waitForResponse(&requestFlags[1][MegaRequest::TYPE_LOGOUT]);
        }

        delete megaApiAux;
        megaApiAux = NULL;
    }
}

void SdkTest::inviteContact(string email, string message, int action, int timeout)
{
    requestFlags[0][MegaRequest::TYPE_INVITE_CONTACT] = false;
    megaApi->inviteContact(email.data(), message.data(), action);
    waitForResponse(&requestFlags[0][MegaRequest::TYPE_INVITE_CONTACT], timeout);    // at the source side (main account)

    if (timeout)
    {
        ASSERT_TRUE(requestFlags[0][MegaRequest::TYPE_INVITE_CONTACT]) << "Contact invitation not finished after " << timeout  << " seconds";
    }

    ASSERT_EQ(MegaError::API_OK, lastError) << "Contact invitation failed (error: " << lastError << ")";
}

void SdkTest::replyContact(MegaContactRequest *cr, int action, int timeout)
{
    requestFlags[1][MegaRequest::TYPE_REPLY_CONTACT_REQUEST] = false;
    megaApiAux->replyContactRequest(cr, action);
    waitForResponse(&requestFlags[1][MegaRequest::TYPE_REPLY_CONTACT_REQUEST], timeout);      // at the target side (auxiliar account)

    if (timeout)
    {
        ASSERT_TRUE(requestFlags[1][MegaRequest::TYPE_REPLY_CONTACT_REQUEST]) << "Contact reply not finished after " << timeout  << " seconds";
    }

    ASSERT_EQ(MegaError::API_OK, lastError) << "Contact reply failed (error: " << lastError << ")";
}

void SdkTest::removeContact(string email, int timeout)
{
    MegaUser *u = megaApi->getContact(email.data());
    bool null_pointer = (u == NULL);
    ASSERT_FALSE(null_pointer) << "Cannot find the specified contact (" << email << ")";

    if (u->getVisibility() != MegaUser::VISIBILITY_VISIBLE)
    {
        userUpdated[0] = true;  // nothing to do
        delete u;
        return;
    }

    requestFlags[0][MegaRequest::TYPE_REMOVE_CONTACT] = false;
    megaApi->removeContact(u);
    waitForResponse(&requestFlags[0][MegaRequest::TYPE_REMOVE_CONTACT], timeout);      // at the target side (auxiliar account)

    if (timeout)
    {
        ASSERT_TRUE(requestFlags[0][MegaRequest::TYPE_REMOVE_CONTACT]) << "Contact deletion not finished after " << timeout  << " seconds";
    }

    ASSERT_EQ(MegaError::API_OK, lastError) << "Contact deletion failed (error: " << lastError << ")";

    delete u;
}

void SdkTest::shareFolder(MegaNode *n, const char *email, int action, int timeout)
{
    requestFlags[0][MegaRequest::TYPE_SHARE] = false;
    megaApi->share(n, email, action);
    waitForResponse(&requestFlags[0][MegaRequest::TYPE_SHARE], timeout);

    if (timeout)
    {
        ASSERT_TRUE(requestFlags[0][MegaRequest::TYPE_SHARE]) << "Folder sharing not finished after " << timeout  << " seconds";
    }

    ASSERT_EQ(MegaError::API_OK, lastError) << "Folder sharing failed (error: " << lastError << ")" << endl << "User: " << email << " Action: " << action;
}

void SdkTest::createPublicLink(MegaNode *n, int timeout)
{
    requestFlags[0][MegaRequest::TYPE_EXPORT] = false;
    megaApi->exportNode(n);
    waitForResponse(&requestFlags[0][MegaRequest::TYPE_EXPORT], timeout);

    if (timeout)
    {
        ASSERT_TRUE(requestFlags[0][MegaRequest::TYPE_EXPORT]) << "Public link creation not finished after " << timeout  << " seconds";
    }

    ASSERT_EQ(MegaError::API_OK, lastError) << "Public link creation failed (error: " << lastError << ")";
}

void SdkTest::importPublicLink(string link, MegaNode *parent, int timeout)
{
    requestFlags[0][MegaRequest::TYPE_IMPORT_LINK] = false;
    megaApi->importFileLink(link.data(), parent);
    waitForResponse(&requestFlags[0][MegaRequest::TYPE_IMPORT_LINK], timeout);

    if (timeout)
    {
        ASSERT_TRUE(requestFlags[0][MegaRequest::TYPE_IMPORT_LINK]) << "Public link import not finished after " << timeout  << " seconds";
    }

    ASSERT_EQ(MegaError::API_OK, lastError) << "Public link import failed (error: " << lastError << ")";
}

void SdkTest::getPublicNode(string link, int timeout)
{
    requestFlags[1][MegaRequest::TYPE_GET_PUBLIC_NODE] = false;
    megaApiAux->getPublicNode(link.data());
    waitForResponse(&requestFlags[1][MegaRequest::TYPE_GET_PUBLIC_NODE], timeout);

    if (timeout)
    {
        ASSERT_TRUE(requestFlags[1][MegaRequest::TYPE_GET_PUBLIC_NODE]) << "Public link retrieval not finished after " << timeout  << " seconds";
    }

    ASSERT_EQ(MegaError::API_OK, lastError) << "Public link retrieval failed (error: " << lastError << ")";
}

void SdkTest::removePublicLink(MegaNode *n, int timeout)
{
    requestFlags[0][MegaRequest::TYPE_EXPORT] = false;
    megaApi->disableExport(n);
    waitForResponse(&requestFlags[0][MegaRequest::TYPE_EXPORT], timeout);

    if (timeout)
    {
        ASSERT_TRUE(requestFlags[0][MegaRequest::TYPE_EXPORT]) << "Public link removal not finished after " << timeout  << " seconds";
    }

    ASSERT_EQ(MegaError::API_OK, lastError) << "Public link removal failed (error: " << lastError << ")";
}

void SdkTest::getContactRequest(bool outgoing, int expectedSize)
{
    MegaContactRequestList *crl;

    if (outgoing)
    {
        crl = megaApi->getOutgoingContactRequests();
        ASSERT_EQ(expectedSize, crl->size()) << "Too many outgoing contact requests in main account";
        if (expectedSize)
            cr = crl->get(0)->copy();
    }
    else
    {
        crl = megaApiAux->getIncomingContactRequests();
        ASSERT_EQ(expectedSize, crl->size()) << "Too many incoming contact requests in auxiliar account";
        if (expectedSize)
            craux = crl->get(0)->copy();
    }

    delete crl;
}

MegaLoggerSDK::MegaLoggerSDK(const char *filename)
{
    sdklog.open(filename, ios::out | ios::app);
}

MegaLoggerSDK::~MegaLoggerSDK()
{
    sdklog.close();
}

void MegaLoggerSDK::log(const char *time, int loglevel, const char *source, const char *message)
{
    sdklog << "[" << time << "] " << SimpleLogger::toStr((LogLevel)loglevel) << ": ";
    sdklog << message << " (" << source << ")" << endl;

    bool errorLevel = (loglevel == logError);
    ASSERT_FALSE(errorLevel) << "Test aborted due to an SDK error.";
}

void SdkTest::setUserAttribute(int type, string value, int timeout)
{
    requestFlags[0][MegaRequest::TYPE_SET_ATTR_USER] = false;

    if (type == MegaApi::USER_ATTR_AVATAR)
    {
        megaApi->setAvatar(value.empty() ? NULL : value.c_str());
    }
    else
    {
        megaApi->setUserAttribute(type, value.c_str());
    }

    waitForResponse(&requestFlags[0][MegaRequest::TYPE_SET_ATTR_USER], timeout);

    if (timeout)
    {
        ASSERT_TRUE(requestFlags[0][MegaRequest::TYPE_SET_ATTR_USER]) << "User attribute setup not finished after " << timeout  << " seconds";
    }

    ASSERT_EQ(MegaError::API_OK, lastError) << "User attribute setup failed (error: " << lastError << ")";
}

void SdkTest::getUserAttribute(MegaUser *u, int type, int timeout)
{
    requestFlags[1][MegaRequest::TYPE_GET_ATTR_USER] = false;

    if (type == MegaApi::USER_ATTR_AVATAR)
    {
        megaApiAux->getUserAvatar(u, AVATARDST.data());
    }
    else
    {
        megaApiAux->getUserAttribute(u, type);
    }

    waitForResponse(&requestFlags[1][MegaRequest::TYPE_GET_ATTR_USER], timeout);

    if (timeout)
    {
        ASSERT_TRUE(requestFlags[1][MegaRequest::TYPE_GET_ATTR_USER]) << "User attribute retrieval not finished after " << timeout  << " seconds";
    }

    bool result = (lastError == MegaError::API_OK) || (lastError == MegaError::API_ENOENT);
    ASSERT_TRUE(result) << "User attribute retrieval failed (error: " << lastError << ")";
}

///////////////////////////__ Tests using SdkTest __//////////////////////////////////

/**
 * @brief TEST_F SdkTestCreateAccount
 *
 * It tests the creation of a new account for a random user.
 */
TEST_F(SdkTest, DISABLED_SdkTestCreateAccount)
{
    requestFlags[0][MegaRequest::TYPE_CREATE_ACCOUNT] = false;
    megaApi->createAccount("user@domain.com", "pwd", "MyFirstname", "MyLastname");
    waitForResponse(&requestFlags[0][MegaRequest::TYPE_CREATE_ACCOUNT]);

    ASSERT_TRUE(requestFlags[0][MegaRequest::TYPE_CREATE_ACCOUNT]) << "Account creation has failed after " << maxTimeout << " seconds";

    bool result = (lastError == MegaError::API_OK);
    ASSERT_TRUE(result) << "Account creation failed (error: " << lastError << ")";
}

/**
 * @brief TEST_F SdkTestResumeSession
 *
 * It creates a local cache, logs out of the current session and tries to resume it later.
 */
TEST_F(SdkTest, SdkTestResumeSession)
{
    megaApi->log(MegaApi::LOG_LEVEL_INFO, "___TEST Resume session___");

    char *session = dumpSession();
    ASSERT_NO_FATAL_FAILURE( locallogout() );

    ASSERT_NO_FATAL_FAILURE( resumeSession(session) );

    delete session;
}

/**
 * @brief TEST_F SdkTestNodeOperations
 *
 * It performs different operations with nodes, assuming the Cloud folder is empty at the beginning.
 *
 * - Create a new folder
 * - Rename a node
 * - Copy a node
 * - Get child nodes of given node
 * - Get child node by name
 * - Get node by path
 * - Get node by name
 * - Move a node
 * - Get parent node
 * - Move a node to Rubbish bin
 * - Remove a node
 */
TEST_F(SdkTest, SdkTestNodeOperations)
{
    megaApi->log(MegaApi::LOG_LEVEL_INFO, "___TEST Node operations___");

    // --- Create a new folder ---

    MegaNode *rootnode = megaApi->getRootNode();
    char name1[64] = "New folder";

    requestFlags[0][MegaRequest::TYPE_CREATE_FOLDER] = false;
    megaApi->createFolder(name1, rootnode);
    waitForResponse(&requestFlags[0][MegaRequest::TYPE_CREATE_FOLDER]);

    ASSERT_EQ(MegaError::API_OK, lastError) << "Cannot create a folder (error: " << lastError << ")";


    // --- Rename a node ---

    MegaNode *n1 = megaApi->getNodeByHandle(h);
    strcpy(name1, "Folder renamed");

    requestFlags[0][MegaRequest::TYPE_RENAME] = false;
    megaApi->renameNode(n1, name1);
    waitForResponse(&requestFlags[0][MegaRequest::TYPE_RENAME]);

    ASSERT_EQ(MegaError::API_OK, lastError) << "Cannot rename a node (error: " << lastError << ")";


    // --- Copy a node ---

    MegaNode *n2;
    char name2[64] = "Folder copy";

    requestFlags[0][MegaRequest::TYPE_COPY] = false;
    megaApi->copyNode(n1, rootnode, name2);
    waitForResponse(&requestFlags[0][MegaRequest::TYPE_COPY]);

    ASSERT_EQ(MegaError::API_OK, lastError) << "Cannot create a copy of a node (error: " << lastError << ")";
    n2 = megaApi->getNodeByHandle(h);


    // --- Get child nodes ---

    MegaNodeList *children;
    children = megaApi->getChildren(rootnode);

    EXPECT_EQ(megaApi->getNumChildren(rootnode), children->size()) << "Wrong number of child nodes";
    ASSERT_LE(2, children->size()) << "Wrong number of children nodes found";
    EXPECT_STREQ(name2, children->get(0)->getName()) << "Wrong name of child node"; // "Folder copy"
    EXPECT_STREQ(name1, children->get(1)->getName()) << "Wrong name of child node"; // "Folder rename"

    delete children;


    // --- Get child node by name ---

    MegaNode *n3;
    n3 = megaApi->getChildNode(rootnode, name2);

    bool null_pointer = (n3 == NULL);
    EXPECT_FALSE(null_pointer) << "Child node by name not found";
//    ASSERT_EQ(n2->getHandle(), n3->getHandle());  This test may fail due to multiple nodes with the same name


    // --- Get node by path ---

    char path[128] = "/Folder copy";
    MegaNode *n4;
    n4 = megaApi->getNodeByPath(path);

    null_pointer = (n4 == NULL);
    EXPECT_FALSE(null_pointer) << "Node by path not found";


    // --- Search for a node ---
    MegaNodeList *nlist;
    nlist = megaApi->search(rootnode, "copy");

    ASSERT_EQ(1, nlist->size());
    EXPECT_EQ(n4->getHandle(), nlist->get(0)->getHandle()) << "Search node by pattern failed";

    delete nlist;


    // --- Move a node ---

    requestFlags[0][MegaRequest::TYPE_MOVE] = false;
    megaApi->moveNode(n1, n2);
    waitForResponse(&requestFlags[0][MegaRequest::TYPE_MOVE]);

    ASSERT_EQ(MegaError::API_OK, lastError) << "Cannot move node (error: " << lastError << ")";


    // --- Get parent node ---

    MegaNode *n5;
    n5 = megaApi->getParentNode(n1);

    ASSERT_EQ(n2->getHandle(), n5->getHandle()) << "Wrong parent node";


    // --- Send to Rubbish bin ---

    requestFlags[0][MegaRequest::TYPE_MOVE] = false;
    megaApi->moveNode(n2, megaApi->getRubbishNode());
    waitForResponse(&requestFlags[0][MegaRequest::TYPE_MOVE]);

    ASSERT_EQ(MegaError::API_OK, lastError) << "Cannot move node to Rubbish bin (error: " << lastError << ")";


    // --- Remove a node ---

    requestFlags[0][MegaRequest::TYPE_REMOVE] = false;
    megaApi->remove(n2);
    waitForResponse(&requestFlags[0][MegaRequest::TYPE_REMOVE]);

    ASSERT_EQ(MegaError::API_OK, lastError) << "Cannot remove a node (error: " << lastError << ")";

    delete rootnode;
    delete n1;
    delete n2;
    delete n3;
    delete n4;
    delete n5;
}

/**
 * @brief TEST_F SdkTestTransfers
 *
 * It performs different operations related to transfers in both directions: up and down.
 *
 * - Starts an upload transfer and cancel it
 * - Starts an upload transfer, pause it, resume it and complete it
 * - Get node by fingerprint
 * - Get size of a node
 * - Download a file
 */
TEST_F(SdkTest, SdkTestTransfers)
{
    megaApi->log(MegaApi::LOG_LEVEL_INFO, "___TEST Transfers___");

    MegaNode *rootnode = megaApi->getRootNode();
    string filename1 = UPFILE;
    createFile(filename1);


    // --- Cancel a transfer ---

    requestFlags[0][MegaRequest::TYPE_CANCEL_TRANSFERS] = false;
    megaApi->startUpload(filename1.data(), rootnode);
    megaApi->cancelTransfers(MegaTransfer::TYPE_UPLOAD);
    waitForResponse(&requestFlags[0][MegaRequest::TYPE_CANCEL_TRANSFERS]);

    EXPECT_EQ(MegaError::API_OK, lastError) << "Transfer cancellation failed (error: " << lastError << ")";


    // --- Upload a file (part 1) ---

    transferFlags[0][MegaTransfer::TYPE_UPLOAD] = false;
    megaApi->startUpload(filename1.data(), rootnode);
    // do not wait yet for completion


    // --- Pause a transfer ---

    requestFlags[0][MegaRequest::TYPE_PAUSE_TRANSFERS] = false;
    megaApi->pauseTransfers(true, MegaTransfer::TYPE_UPLOAD);
    waitForResponse(&requestFlags[0][MegaRequest::TYPE_PAUSE_TRANSFERS]);

    EXPECT_EQ(MegaError::API_OK, lastError) << "Cannot pause transfer (error: " << lastError << ")";
    EXPECT_TRUE(megaApi->areTransfersPaused(MegaTransfer::TYPE_UPLOAD)) << "Upload transfer not paused";


    // --- Resume a transfer ---

    requestFlags[0][MegaRequest::TYPE_PAUSE_TRANSFERS] = false;
    megaApi->pauseTransfers(false, MegaTransfer::TYPE_UPLOAD);
    waitForResponse(&requestFlags[0][MegaRequest::TYPE_PAUSE_TRANSFERS]);

    EXPECT_EQ(MegaError::API_OK, lastError) << "Cannot resume transfer (error: " << lastError << ")";
    EXPECT_FALSE(megaApi->areTransfersPaused(MegaTransfer::TYPE_UPLOAD)) << "Upload transfer not resumed";


    // --- Upload a file (part 2) ---

    waitForResponse(&transferFlags[0][MegaTransfer::TYPE_UPLOAD]);

    ASSERT_EQ(MegaError::API_OK, lastError) << "Cannot upload file (error: " << lastError << ")";

    MegaNode *n1 = megaApi->getNodeByHandle(h);
    bool null_pointer = (n1 == NULL);

    ASSERT_FALSE(null_pointer) << "Cannot upload file (error: " << lastError << ")";
    ASSERT_STREQ(filename1.data(), n1->getName()) << "Uploaded file with wrong name (error: " << lastError << ")";


    // --- Get node by fingerprint (needs to be a file, not a folder) ---

    char *fingerprint = megaApi->getFingerprint(n1);
    MegaNode *n2 = megaApi->getNodeByFingerprint(fingerprint);

    null_pointer = (n2 == NULL);
    EXPECT_FALSE(null_pointer) << "Node by fingerprint not found";
//    ASSERT_EQ(n2->getHandle(), n4->getHandle());  This test may fail due to multiple nodes with the same name

    delete fingerprint;


    // --- Get the size of a file ---

    int filesize = getFilesize(filename1);
    int nodesize = megaApi->getSize(n2);
    EXPECT_EQ(filesize, nodesize) << "Wrong size of uploaded file";


    // --- Download a file ---

    string filename2 = "./" + DOWNFILE;

    transferFlags[0][MegaTransfer::TYPE_DOWNLOAD] = false;
    megaApi->startDownload(n2, filename2.c_str());
    waitForResponse(&transferFlags[0][MegaTransfer::TYPE_DOWNLOAD]);

    ASSERT_EQ(MegaError::API_OK, lastError) << "Cannot download the file (error: " << lastError << ")";

    MegaNode *n3 = megaApi->getNodeByHandle(h);
    null_pointer = (n3 == NULL);

    ASSERT_FALSE(null_pointer) << "Cannot download node";
    ASSERT_EQ(n2->getHandle(), n3->getHandle()) << "Cannot download node (error: " << lastError << ")";

    delete rootnode;
    delete n1;
    delete n2;
    delete n3;
}

/**
 * @brief TEST_F SdkTestContacts
 *
 * Creates an auxiliar 'MegaApi' object to interact with the main MEGA account.
 *
 * - Invite a contact
 * = Ignore the invitation
 * - Delete the invitation
 *
 * - Invite a contact
 * = Deny the invitation
 *
 * - Invite a contact
 * = Accept the invitation
 *
 * - Modify firstname
 * = Check firstname of a contact
 * - Load avatar
 * = Check avatar of a contact
 * - Delete avatar
 * = Check non-existing avatar of a contact
 *
 * - Remove contact
 *
 * TODO:
 * - Invite a contact not registered in MEGA yet (requires validation of account)
 * - Remind an existing invitation (requires 2 weeks wait)
 */
TEST_F(SdkTest, SdkTestContacts)
{
    megaApi->log(MegaApi::LOG_LEVEL_INFO, "___TEST Contacts___");

    ASSERT_NO_FATAL_FAILURE( getMegaApiAux() );    // login + fetchnodes


    // --- Check my email and the email of the contact ---

    EXPECT_STREQ(email.data(), megaApi->getMyEmail());
    EXPECT_STREQ(emailaux.data(), megaApiAux->getMyEmail());


    // --- Send a new contact request ---

    string message = "Hi contact. This is a testing message";

    contactRequestUpdated[1] = false;
    ASSERT_NO_FATAL_FAILURE( inviteContact(emailaux, message, MegaContactRequest::INVITE_ACTION_ADD) );
    waitForResponse(&contactRequestUpdated[1]); // at the target side (auxiliar account)


    // --- Check the sent contact request ---

    ASSERT_NO_FATAL_FAILURE( getContactRequest(true) );

    ASSERT_STREQ(message.data(), cr->getSourceMessage()) << "Message sent is corrupted";
    ASSERT_STREQ(email.data(), cr->getSourceEmail()) << "Wrong source email";
    ASSERT_STREQ(emailaux.data(), cr->getTargetEmail()) << "Wrong target email";
    ASSERT_EQ(MegaContactRequest::STATUS_UNRESOLVED, cr->getStatus()) << "Wrong contact request status";
    ASSERT_TRUE(cr->isOutgoing()) << "Wrong direction of the contact request";

    delete cr;      cr = NULL;


    // --- Check received contact request ---

    ASSERT_NO_FATAL_FAILURE( getContactRequest(false) );

    ASSERT_STREQ(message.data(), craux->getSourceMessage()) << "Message received is corrupted";
    ASSERT_STREQ(email.data(), craux->getSourceEmail()) << "Wrong source email";
    ASSERT_STREQ(NULL, craux->getTargetEmail()) << "Wrong target email";    // NULL according to MegaApi documentation
    ASSERT_EQ(MegaContactRequest::STATUS_UNRESOLVED, craux->getStatus()) << "Wrong contact request status";
    ASSERT_FALSE(craux->isOutgoing()) << "Wrong direction of the contact request";

    delete craux;   craux = NULL;


    // --- Ignore received contact request ---

    ASSERT_NO_FATAL_FAILURE( getContactRequest(false) );

    contactRequestUpdated[1] = false;
    ASSERT_NO_FATAL_FAILURE( replyContact(craux, MegaContactRequest::REPLY_ACTION_IGNORE) );
    waitForResponse(&contactRequestUpdated[1]);    // at the target side (auxiliar account)
    // waitForResponse(&contactRequestUpdated[0]);    // at the source side (main account)
    // Ignoring a PCR does not generate actionpackets for the main account

    delete craux;   craux = NULL;

    ASSERT_NO_FATAL_FAILURE( getContactRequest(false, 0) );
    delete craux;   craux = NULL;


    // --- Cancel the invitation ---

    message = "I don't wanna be your contact anymore";

    contactRequestUpdated[1] = false;
    ASSERT_NO_FATAL_FAILURE( inviteContact(emailaux, message, MegaContactRequest::INVITE_ACTION_DELETE) );
    waitForResponse(&contactRequestUpdated[1]);    // only at auxiliar account, where the deletion is checked

    ASSERT_NO_FATAL_FAILURE( getContactRequest(false, 0) );
    delete craux;   craux = NULL;

    // The target contact doesn't receive notification, since the invitation was ignored previously

    
    // --- Remind a contact invitation (cannot until 2 weeks after invitation/last reminder) ---

//    contactRequestUpdated[1] = false;
//    megaApi->inviteContact(emailaux.data(), message.data(), MegaContactRequest::INVITE_ACTION_REMIND);
//    waitForResponse(&contactRequestUpdated[1]);    // only at auxiliar account, where the deletion is checked

//    ASSERT_TRUE(contactRequestUpdated[1]) << "Contact invitation reminder not received after " << timeout  << " seconds";


    // --- Invite a new contact (again) ---

    contactRequestUpdated[1] = false;
    ASSERT_NO_FATAL_FAILURE( inviteContact(emailaux, message, MegaContactRequest::INVITE_ACTION_ADD) );
    waitForResponse(&contactRequestUpdated[1]); // at the target side (auxiliar account)


    // --- Deny a contact invitation ---

    ASSERT_NO_FATAL_FAILURE( getContactRequest(false) );

    contactRequestUpdated[0] = contactRequestUpdated[1] = false;
    ASSERT_NO_FATAL_FAILURE( replyContact(craux, MegaContactRequest::REPLY_ACTION_DENY) );
    waitForResponse(&contactRequestUpdated[1]);    // at the target side (auxiliar account)
    waitForResponse(&contactRequestUpdated[0]);    // at the source side (main account)

    delete craux;   craux = NULL;

    ASSERT_NO_FATAL_FAILURE( getContactRequest(true, 0) );
    delete cr;   cr = NULL;

    ASSERT_NO_FATAL_FAILURE( getContactRequest(false, 0) );
    delete craux;   craux = NULL;


    // --- Invite a new contact (again) ---

    contactRequestUpdated[1] = false;
    ASSERT_NO_FATAL_FAILURE( inviteContact(emailaux, message, MegaContactRequest::INVITE_ACTION_ADD) );
    waitForResponse(&contactRequestUpdated[1]); // at the target side (auxiliar account)

    // --- Accept a contact invitation ---

    ASSERT_NO_FATAL_FAILURE( getContactRequest(false) );

    contactRequestUpdated[0] = contactRequestUpdated[1] = false;
    ASSERT_NO_FATAL_FAILURE( replyContact(craux, MegaContactRequest::REPLY_ACTION_ACCEPT) );
    waitForResponse(&contactRequestUpdated[0]);     // at the source side (main account)
    waitForResponse(&contactRequestUpdated[1]);     // at the target side (auxiliar account)

    delete craux;   craux = NULL;

    ASSERT_NO_FATAL_FAILURE( getContactRequest(true, 0) );
    delete cr;   cr = NULL;

    ASSERT_NO_FATAL_FAILURE( getContactRequest(false, 0) );
    delete craux;   craux = NULL;


    // --- Modify firstname ---

    string firstname = "My firstname";

    userUpdated[0] = userUpdated[1] = false;
    ASSERT_NO_FATAL_FAILURE( setUserAttribute(MegaApi::USER_ATTR_FIRSTNAME, firstname));
    waitForResponse(&userUpdated[1]); // at the target side (auxiliar account)
    waitForResponse(&userUpdated[0]);    // at the source side (main account)


    // --- Check firstname of a contact

    MegaUser *u = megaApi->getMyUser();

    bool null_pointer = (u == NULL);
    ASSERT_FALSE(null_pointer) << "Cannot find the MegaUser for email: " << email;

    ASSERT_NO_FATAL_FAILURE( getUserAttribute(u, MegaApi::USER_ATTR_FIRSTNAME));
    ASSERT_EQ( firstname, attributeValue) << "Firstname is wrong";

    delete u;

    // --- Load avatar ---

    userUpdated[0] = userUpdated[1] = false;
    ASSERT_NO_FATAL_FAILURE( setUserAttribute(MegaApi::USER_ATTR_AVATAR, AVATARSRC));
    waitForResponse(&userUpdated[1]); // at the target side (auxiliar account)
    waitForResponse(&userUpdated[0]);    // at the source side (main account)


    // --- Get avatar of a contact ---

    u = megaApi->getMyUser();

    null_pointer = (u == NULL);
    ASSERT_FALSE(null_pointer) << "Cannot find the MegaUser for email: " << email;

    attributeValue = "";

    ASSERT_NO_FATAL_FAILURE( getUserAttribute(u, MegaApi::USER_ATTR_AVATAR));
    ASSERT_STREQ( "Avatar changed", attributeValue.data()) << "Failed to change avatar";

    int filesizeSrc = getFilesize(AVATARSRC);
    int filesizeDst = getFilesize(AVATARDST);
    ASSERT_EQ(filesizeDst, filesizeSrc) << "Received avatar differs from uploaded avatar";

    delete u;


    // --- Delete avatar ---

    userUpdated[0] = userUpdated[1] = false;
    ASSERT_NO_FATAL_FAILURE( setUserAttribute(MegaApi::USER_ATTR_AVATAR, ""));
    waitForResponse(&userUpdated[1]); // at the target side (auxiliar account)
    waitForResponse(&userUpdated[0]);    // at the source side (main account)


    // --- Get non-existing avatar of a contact ---

    u = megaApi->getMyUser();

    null_pointer = (u == NULL);
    ASSERT_FALSE(null_pointer) << "Cannot find the MegaUser for email: " << email;

    attributeValue = "";

    ASSERT_NO_FATAL_FAILURE( getUserAttribute(u, MegaApi::USER_ATTR_AVATAR));
    ASSERT_STREQ("Avatar not found", attributeValue.data()) << "Failed to remove avatar";

    delete u;


    // --- Delete an existing contact ---

    userUpdated[0] = false;
    ASSERT_NO_FATAL_FAILURE( removeContact(emailaux) );
    waitForResponse(&userUpdated[0]);    // at the source side (main account)

    u = megaApi->getContact(emailaux.data());
    null_pointer = (u == NULL);

    ASSERT_FALSE(null_pointer) << "Cannot find the MegaUser for email: " << email;
    ASSERT_EQ(MegaUser::VISIBILITY_HIDDEN, u->getVisibility()) << "New contact is still visible";

    delete u;
}

/**
 * @brief TEST_F SdkTestShares
 *
 * Initialize a test scenario by:
 *
 * - Creating/uploading some folders/files to share
 * - Creating a new contact to share to
 *
 * Performs different operations related to sharing:
 *
 * - Share a folder with an existing contact
 * - Check the correctness of the outgoing share
 * - Check the reception and correctness of the incoming share
 * - Modify the access level
 * - Revoke the access to the share
 * - Share a folder with a non registered email
 * - Check the correctness of the pending outgoing share
 * - Create a public link
 * - Import a public link
 * - Get a node from public link
 * - Remove a public link
 */
TEST_F(SdkTest, SdkTestShares)
{
    megaApi->log(MegaApi::LOG_LEVEL_INFO, "___TEST Shares___");

    MegaShareList *sl;
    MegaShare *s;
    MegaNodeList *nl;
    MegaNode *n;
    MegaNode *n1;

    ASSERT_NO_FATAL_FAILURE( getMegaApiAux() );    // login + fetchnodes


    // Initialize a test scenario : create some folders/files to share

    // Create some nodes to share
    //  |--Shared-folder
    //    |--subfolder
    //    |--file.txt

    MegaNode *rootnode = megaApi->getRootNode();
    char foldername1[64] = "Shared-folder";
    MegaHandle hfolder1;

    requestFlags[0][MegaRequest::TYPE_CREATE_FOLDER] = false;
    megaApi->createFolder(foldername1, rootnode);
    waitForResponse(&requestFlags[0][MegaRequest::TYPE_CREATE_FOLDER]);

    ASSERT_EQ(MegaError::API_OK, lastError) << "Cannot create a folder (error: " << lastError << ")";
    hfolder1 = h;     // 'h' is set in 'onRequestFinish()'
    n1 = megaApi->getNodeByHandle(hfolder1);

    char foldername2[64] = "subfolder";
    MegaHandle hfolder2;

    requestFlags[0][MegaRequest::TYPE_CREATE_FOLDER] = false;
    megaApi->createFolder(foldername2, megaApi->getNodeByHandle(hfolder1));
    waitForResponse(&requestFlags[0][MegaRequest::TYPE_CREATE_FOLDER]);

    ASSERT_EQ(MegaError::API_OK, lastError) << "Cannot create a folder (error: " << lastError << ")";
    hfolder2 = h;

    MegaHandle hfile1;
    createFile(PUBLICFILE.data(), false);   // not a large file since don't need to test transfers here

    transferFlags[0][MegaTransfer::TYPE_UPLOAD] = false;
    megaApi->startUpload(PUBLICFILE.data(), megaApi->getNodeByHandle(hfolder1));
    waitForResponse(&transferFlags[0][MegaTransfer::TYPE_UPLOAD], 0);   // wait forever

    ASSERT_EQ(MegaError::API_OK, lastError) << "Cannot upload file (error: " << lastError << ")";
    hfile1 = h;


    // --- Download authorized node from another account ---

    MegaNode *nNoAuth = megaApi->getNodeByHandle(hfile1);

    transferFlags[1][MegaTransfer::TYPE_DOWNLOAD] = false;
    megaApiAux->startDownload(nNoAuth, "unauthorized_node");
    waitForResponse(&transferFlags[1][MegaTransfer::TYPE_DOWNLOAD], 0);

    bool hasFailed = (lastError != API_OK);
    ASSERT_TRUE(hasFailed) << "Download of node without authorization successful! (it should fail)";

    MegaNode *nAuth = megaApi->authorizeNode(nNoAuth);

    transferFlags[1][MegaTransfer::TYPE_DOWNLOAD] = false;
    megaApiAux->startDownload(nAuth, "authorized_node");
    waitForResponse(&transferFlags[1][MegaTransfer::TYPE_DOWNLOAD], 0);

    ASSERT_EQ(MegaError::API_OK, lastError) << "Cannot download authorized node (error: " << lastError << ")";

    delete nNoAuth;
    delete nAuth;
    return;

    // Initialize a test scenario: create a new contact to share to

    string message = "Hi contact. Let's share some stuff";

    contactRequestUpdated[1] = false;
    ASSERT_NO_FATAL_FAILURE( inviteContact(emailaux, message, MegaContactRequest::INVITE_ACTION_ADD) );
    waitForResponse(&contactRequestUpdated[1]); // at the target side (auxiliar account)


    MegaContactRequestList *crlaux = megaApiAux->getIncomingContactRequests();
    ASSERT_EQ(1, crlaux->size()) << "Too many incoming contact requests in auxiliar account";
    MegaContactRequest *craux = crlaux->get(0);

    contactRequestUpdated[0] = contactRequestUpdated[1] = false;
    ASSERT_NO_FATAL_FAILURE( replyContact(craux, MegaContactRequest::REPLY_ACTION_ACCEPT) );
    waitForResponse(&contactRequestUpdated[1]);    // at the target side (auxiliar account)
    waitForResponse(&contactRequestUpdated[0]);    // at the source side (main account)

    delete crlaux;


    // --- Create a new outgoing share ---

    nodeUpdated[0] = nodeUpdated[1] = false;
    ASSERT_NO_FATAL_FAILURE( shareFolder(n1, emailaux.data(), MegaShare::ACCESS_READ) );
    waitForResponse(&nodeUpdated[0]);
    waitForResponse(&nodeUpdated[1]);


    // --- Check the outgoing share ---

    sl = megaApi->getOutShares();
    ASSERT_EQ(1, sl->size()) << "Outgoing share failed";
    s = sl->get(0);

    n1 = megaApi->getNodeByHandle(hfolder1);    // get an updated version of the node

    ASSERT_EQ(MegaShare::ACCESS_READ, s->getAccess()) << "Wrong access level of outgoing share";
    ASSERT_EQ(hfolder1, s->getNodeHandle()) << "Wrong node handle of outgoing share";
    ASSERT_STREQ(emailaux.data(), s->getUser()) << "Wrong email address of outgoing share";
    ASSERT_TRUE(n1->isShared()) << "Wrong sharing information at outgoing share";
    ASSERT_TRUE(n1->isOutShare()) << "Wrong sharing information at outgoing share";

    delete sl;


    // --- Check the incoming share ---

    sl = megaApiAux->getInSharesList();
    ASSERT_EQ(1, sl->size()) << "Incoming share not received in auxiliar account";

    nl = megaApiAux->getInShares(megaApiAux->getContact(email.data()));
    ASSERT_EQ(1, nl->size()) << "Incoming share not received in auxiliar account";
    n = nl->get(0);

    ASSERT_EQ(hfolder1, n->getHandle()) << "Wrong node handle of incoming share";
    ASSERT_STREQ(foldername1, n->getName()) << "Wrong folder name of incoming share";
    ASSERT_EQ(MegaError::API_OK, megaApiAux->checkAccess(n, MegaShare::ACCESS_READ).getErrorCode()) << "Wrong access level of incoming share";
    ASSERT_TRUE(n->isInShare()) << "Wrong sharing information at incoming share";
    ASSERT_TRUE(n->isShared()) << "Wrong sharing information at incoming share";

    delete nl;


    // --- Modify the access level of an outgoing share ---

    nodeUpdated[0] = nodeUpdated[1] = false;
    ASSERT_NO_FATAL_FAILURE( shareFolder(megaApi->getNodeByHandle(hfolder1), emailaux.data(), MegaShare::ACCESS_READWRITE) );
    waitForResponse(&nodeUpdated[0]);
    waitForResponse(&nodeUpdated[1]);

    nl = megaApiAux->getInShares(megaApiAux->getContact(email.data()));
    ASSERT_EQ(1, nl->size()) << "Incoming share not received in auxiliar account";
    n = nl->get(0);

    ASSERT_EQ(MegaError::API_OK, megaApiAux->checkAccess(n, MegaShare::ACCESS_READWRITE).getErrorCode()) << "Wrong access level of incoming share";

    delete nl;


    // --- Revoke access to an outgoing share ---

    nodeUpdated[0] = nodeUpdated[1] = false;
    ASSERT_NO_FATAL_FAILURE( shareFolder(n1, emailaux.data(), MegaShare::ACCESS_UNKNOWN) );
    waitForResponse(&nodeUpdated[0]);
    waitForResponse(&nodeUpdated[1]);

    sl = megaApi->getOutShares();
    ASSERT_EQ(0, sl->size()) << "Outgoing share revocation failed";
    delete sl;

    nl = megaApiAux->getInShares(megaApiAux->getContact(email.data()));
    ASSERT_EQ(0, nl->size()) << "Incoming share revocation failed";
    delete nl;


    // --- Get pending outgoing shares ---

    char emailfake[64];
    srand (time(NULL));
    sprintf(emailfake, "%d@nonexistingdomain.com", rand()%1000000);
    // carefull, antispam rejects too many tries without response for the same address

    n = megaApi->getNodeByHandle(hfolder2);

    contactRequestUpdated[0] = false;
    nodeUpdated[0] = false;
    ASSERT_NO_FATAL_FAILURE( shareFolder(n, emailfake, MegaShare::ACCESS_FULL) );
    waitForResponse(&nodeUpdated[0]);
    waitForResponse(&contactRequestUpdated[0]);    // at the source side (main account)

    sl = megaApi->getPendingOutShares(n);   delete n;
    ASSERT_EQ(1, sl->size()) << "Pending outgoing share failed";
    s = sl->get(0);
    n = megaApi->getNodeByHandle(s->getNodeHandle());

//    ASSERT_STREQ(emailfake, s->getUser()) << "Wrong email address of outgoing share"; User is not created yet
    ASSERT_FALSE(n->isShared()) << "Node is already shared, must be pending";
    ASSERT_FALSE(n->isOutShare()) << "Node is already shared, must be pending";
    ASSERT_FALSE(n->isInShare()) << "Node is already shared, must be pending";

    delete sl;
    delete n;


    // --- Create a public link ---

    MegaNode *nfile1 = megaApi->getNodeByHandle(hfile1);

    ASSERT_NO_FATAL_FAILURE( createPublicLink(nfile1) );
    // The created link is stored in this->link at onRequestFinish()

    // Get a fresh snapshot of the node and check it's actually exported
    nfile1 = megaApi->getNodeByHandle(hfile1);
    ASSERT_TRUE(nfile1->isExported()) << "Node is not exported, must be exported";
    ASSERT_FALSE(nfile1->isTakenDown()) << "Public link is taken down, it mustn't";

    // Regenerate the same link should not trigger a new request
    string oldLink = link;
    link = "";
    nfile1 = megaApi->getNodeByHandle(hfile1);
    ASSERT_NO_FATAL_FAILURE( createPublicLink(nfile1) );
    ASSERT_STREQ(oldLink.c_str(), link.c_str()) << "Wrong public link after link update";


    // --- Import a public link ---

    ASSERT_NO_FATAL_FAILURE( importPublicLink(link, rootnode) );

    MegaNode *nimported = megaApi->getNodeByHandle(h);

    ASSERT_STREQ(nfile1->getName(), nimported->getName()) << "Imported file with wrong name";
    ASSERT_EQ(rootnode->getHandle(), nimported->getParentHandle()) << "Imported file in wrong path";


    // --- Get node from public link ---

    ASSERT_NO_FATAL_FAILURE( getPublicNode(link) );

    ASSERT_TRUE(publicNode->isPublic()) << "Cannot get a node from public link";


    // --- Remove a public link ---

    ASSERT_NO_FATAL_FAILURE( removePublicLink(nfile1) );

    delete nfile1;
    nfile1 = megaApi->getNodeByHandle(h);
    ASSERT_FALSE(nfile1->isPublic()) << "Public link removal failed (still public)";

    delete nimported;
}

#ifdef ENABLE_CHAT

/**
 * @brief TEST_F SdkTestChat
 *
 * Initialize a test scenario by:
 *
 * - Setting a new contact to chat with
 *
 * Performs different operations related to chats:
 *
 * - Fetch the list of available chats
 * - Create a group chat
 * - Remove a peer from the chat
 * - Invite a contact to a chat
 * - Get the user-specific URL for the chat
 */
TEST_F(SdkTest, SdkTestChat)
{
    megaApi->log(MegaApi::LOG_LEVEL_INFO, "___TEST Chat___");

    ASSERT_NO_FATAL_FAILURE( getMegaApiAux() );    // login + fetchnodes    

    // --- Send a new contact request ---

    string message = "Hi contact. This is a testing message";

    contactRequestUpdated[1] = false;
    ASSERT_NO_FATAL_FAILURE( inviteContact(emailaux, message, MegaContactRequest::INVITE_ACTION_ADD) );
    waitForResponse(&contactRequestUpdated[1]); // at the target side (auxiliar account)


    // --- Accept a contact invitation ---

    ASSERT_NO_FATAL_FAILURE( getContactRequest(false) );

    contactRequestUpdated[0] = contactRequestUpdated[1] = false;
    ASSERT_NO_FATAL_FAILURE( replyContact(craux, MegaContactRequest::REPLY_ACTION_ACCEPT) );
    waitForResponse(&contactRequestUpdated[1]);    // at the target side (auxiliar account)
    waitForResponse(&contactRequestUpdated[0]);    // at the source side (main account)

    delete craux;   craux = NULL;


    // --- Fetch list of available chats ---

    ASSERT_NO_FATAL_FAILURE( fetchChats() );
    uint numChats = chats->size();      // permanent chats cannot be deleted, so they're kept forever


    // --- Create a group chat ---

    MegaTextChatPeerList *peers;
    handle h;
    bool group;

    h = megaApiAux->getMyUser()->getHandle();
    peers = MegaTextChatPeerList::createInstance();//new MegaTextChatPeerListPrivate();
    peers->addPeer(h, PRIV_RW);
    group = true;

    chatUpdated[1] = false;
    ASSERT_NO_FATAL_FAILURE( createChat(group, peers) );
    waitForResponse(&chatUpdated[1]);

    delete peers;

    // check the new chat information
    ASSERT_NO_FATAL_FAILURE( fetchChats() );
    ASSERT_EQ(chats->size(), ++numChats) << "Unexpected received number of chats";
    ASSERT_TRUE(chatUpdated[1]) << "The peer didn't receive notification of the chat creation";


    // --- Remove a peer from the chat ---

    handle chatid = chats->get(numChats - 1)->getHandle();

    chatUpdated[0] = false;
    requestFlags[0][MegaRequest::TYPE_CHAT_REMOVE] = false;
    megaApi->removeFromChat(chatid, h);
    waitForResponse(&requestFlags[0][MegaRequest::TYPE_CHAT_REMOVE]);
    ASSERT_EQ(MegaError::API_OK, lastError) << "Removal of chat peer failed (error: " << lastError << ")";

    waitForResponse(&chatUpdated[0]);
    ASSERT_TRUE(chatUpdated[0]) << "Didn't receive notification of the peer removal";


    // --- Invite a contact to a chat ---

    chatUpdated[1] = false;
    requestFlags[0][MegaRequest::TYPE_CHAT_INVITE] = false;
    megaApi->inviteToChat(chatid, h, PRIV_FULL);
    waitForResponse(&requestFlags[0][MegaRequest::TYPE_CHAT_INVITE]);
    ASSERT_EQ(MegaError::API_OK, lastError) << "Invitation of chat peer failed (error: " << lastError << ")";

    waitForResponse(&chatUpdated[1]);
    ASSERT_TRUE(chatUpdated[1]) << "The peer didn't receive notification of the invitation";


    // --- Get the user-specific URL for the chat ---

    requestFlags[0][MegaRequest::TYPE_CHAT_URL] = false;
    megaApi->getUrlChat(chatid);
    waitForResponse(&requestFlags[0][MegaRequest::TYPE_CHAT_URL]);
    ASSERT_EQ(MegaError::API_OK, lastError) << "Retrieval of chat URL failed (error: " << lastError << ")";
}

#endif
