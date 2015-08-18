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
        char path[1024];
        getcwd(path, sizeof path);
        megaApi = new MegaApi(APP_KEY.c_str(), path, USER_AGENT.c_str());

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

    releaseMegaApiAux();

    if (megaApi)
    {
        // Remove nodes in Cloud & Rubbish
        purgeTree(megaApi->getRootNode());
        purgeTree(megaApi->getRubbishNode());

        // Remove auxiliar contact
        MegaUserList *ul = megaApi->getContacts();
        for (int i = 0; i < ul->size(); i++)
        {
            MegaUser *u = ul->get(i);
            megaApi->removeContact(u);
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
    }
}

void SdkTest::onRequestFinish(MegaApi *api, MegaRequest *request, MegaError *e)
{
    lastError = e->getErrorCode();

    switch(request->getType())
    {
    case MegaRequest::TYPE_LOGIN:
        loggingReceived = true;
        break;

    case MegaRequest::TYPE_FETCH_NODES:
        fetchnodesReceived = true;
        break;

    case MegaRequest::TYPE_LOGOUT:
        logoutReceived = true;
        break;

    case MegaRequest::TYPE_CREATE_FOLDER:
        h = request->getNodeHandle();
        responseReceived = true;
        break;

    case MegaRequest::TYPE_RENAME:
        responseReceived = true;
        break;

    case MegaRequest::TYPE_COPY:
        h = request->getNodeHandle();
        responseReceived = true;
        break;

    case MegaRequest::TYPE_MOVE:
        responseReceived = true;
        break;

    case MegaRequest::TYPE_REMOVE:
        responseReceived = true;
        break;

    case MegaRequest::TYPE_UPLOAD:
        uploadFinished = true;
        break;

    case MegaRequest::TYPE_PAUSE_TRANSFERS:
        transfersPaused = true;
        break;

    case MegaRequest::TYPE_CANCEL_TRANSFERS:
        transfersCancelled = true;
        break;

    case MegaRequest::TYPE_INVITE_CONTACT:
        contactInvitationFinished = true;
        break;

    case MegaRequest::TYPE_REPLY_CONTACT_REQUEST:
        contactReplyFinished = true;
        break;

    case MegaRequest::TYPE_REMOVE_CONTACT:
        responseReceived = true;
        break;

    case MegaRequest::TYPE_SHARE:
        responseReceived = true;
        break;

    case MegaRequest::TYPE_EXPORT:
        if (request->getAccess())
            link.assign(request->getLink());
        h = request->getNodeHandle();
        responseReceived = true;
        break;

    case MegaRequest::TYPE_GET_PUBLIC_NODE:
        if (lastError == API_OK)
            publicNode = request->getPublicMegaNode();
        responseReceived = true;
        break;

    case MegaRequest::TYPE_IMPORT_LINK:
        h = request->getNodeHandle();
        responseReceived = true;
        break;
    }
}

void SdkTest::onTransferFinish(MegaApi* api, MegaTransfer *transfer, MegaError* e)
{
    lastError = e->getErrorCode();

    if (lastError == MegaError::API_OK)
        h = transfer->getNodeHandle();

    switch(transfer->getType())
    {
    case MegaTransfer::TYPE_DOWNLOAD:
        downloadFinished = true;
        break;

    case MegaTransfer::TYPE_UPLOAD:
        uploadFinished = true;
        break;
    }
}

void SdkTest::onUsersUpdate(MegaApi* api, MegaUserList *users)
{
    // Main testing account
    if (api == megaApi)
    {
        contactRemoved = true;
    }
}

void SdkTest::onNodesUpdate(MegaApi* api, MegaNodeList *nodes)
{
    // Main testing account
    if (api == megaApi)
    {
        nodeUpdated = true;
    }

    // Auxiliar testing account
    if (api == megaApiAux)
    {
        nodeUpdatedAux = true;
    }
}

void SdkTest::onContactRequestsUpdate(MegaApi* api, MegaContactRequestList* requests)
{
    // Main testing account
    if (api == megaApi)
    {
        contactRequestUpdated = true;
    }

    // Auxiliar testing account
    if (api == megaApiAux)
    {
        contactRequestUpdatedAux = true;
    }
}

void SdkTest::login(int timeout)
{
    loggingReceived = false;

    megaApi->login(email.data(), pwd.data());

    waitForResponse(&loggingReceived, timeout);

    if (timeout)
    {
        ASSERT_TRUE(loggingReceived) << "Logging failed after " << timeout  << " seconds";
    }

    ASSERT_EQ(MegaError::API_OK, lastError) << "Logging failed (error: " << lastError << ")";
    ASSERT_TRUE(megaApi->isLoggedIn()) << "Not logged it";
}

void SdkTest::fetchnodes(int timeout)
{
    fetchnodesReceived = false;

    megaApi->fetchNodes();

    waitForResponse(&fetchnodesReceived, timeout);

    if (timeout)
    {
        ASSERT_TRUE(fetchnodesReceived) << "Fetchnodes failed after " << timeout  << " seconds";
    }

    ASSERT_EQ(MegaError::API_OK, lastError) << "Fetchnodes failed (error: " << lastError << ")";
}

void SdkTest::logout(int timeout)
{
    logoutReceived = false;

    megaApi->logout(this);

    waitForResponse(&logoutReceived, timeout);

    if (timeout)
    {
        EXPECT_TRUE(logoutReceived) << "Logout failed after " << timeout  << " seconds";
    }

    EXPECT_EQ(MegaError::API_OK, lastError) << "Logout failed (error: " << lastError << ")";
}

char* SdkTest::dumpSession()
{
    return megaApi->dumpSession();
}

void SdkTest::locallogout(int timeout)
{
    logoutReceived = false;

    megaApi->localLogout(this);

    waitForResponse(&logoutReceived, timeout);

    if (timeout)
    {
        EXPECT_TRUE(logoutReceived) << "Local logout failed after " << timeout  << " seconds";
    }

    ASSERT_EQ(MegaError::API_OK, lastError) << "Local logout failed (error: " << lastError << ")";
}

void SdkTest::resumeSession(char *session, int timeout)
{
    loggingReceived = false;

    megaApi->fastLogin(session, this);

    waitForResponse(&loggingReceived, timeout);

    if (timeout)
    {
        ASSERT_TRUE(loggingReceived) << "Resume session failed after " << timeout  << " seconds";
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

        megaApiAux->addListener(this);

        loggingReceived = false;
        megaApiAux->login(emailaux.data(), pwdaux.data());
        waitForResponse(&loggingReceived);

        ASSERT_EQ(MegaError::API_OK, lastError) << "Logging failed in the auxiliar account (error: " << lastError << ")";
        ASSERT_TRUE(megaApiAux->isLoggedIn()) << "Login failed in the auxiliar account";

        fetchnodesReceived = false;
        megaApiAux->fetchNodes();
        waitForResponse(&fetchnodesReceived);

        ASSERT_EQ(MegaError::API_OK, lastError) << "Fetchnodes failed in the auxiliar account (error: " << lastError << ")";
    }
}

void SdkTest::releaseMegaApiAux()
{
    if (megaApiAux)
    {
        if (megaApiAux->isLoggedIn())
        {
            logoutReceived = false;
            megaApiAux->logout();
            waitForResponse(&logoutReceived, 5);
        }

        delete megaApiAux;
        megaApiAux = NULL;
    }
}

void SdkTest::inviteContact(string email, string message, int action, int timeout)
{
    contactInvitationFinished = false;

    megaApi->inviteContact(email.data(), message.data(), action);

    waitForResponse(&contactInvitationFinished, timeout);    // at the source side (main account)

    if (timeout)
    {
        ASSERT_TRUE(contactInvitationFinished) << "Contact invitation not finished after " << timeout  << " seconds";
    }

    ASSERT_EQ(MegaError::API_OK, lastError) << "Contact invitation failed (error: " << lastError << ")";
}

void SdkTest::replyContact(MegaContactRequest *cr, int action, int timeout)
{
    contactReplyFinished = false;

    megaApiAux->replyContactRequest(cr, action);

    waitForResponse(&contactReplyFinished, timeout);      // at the target side (auxiliar account)

    if (timeout)
    {
        ASSERT_TRUE(contactReplyFinished) << "Contact reply not finished after " << timeout  << " seconds";
    }

    ASSERT_EQ(MegaError::API_OK, lastError) << "Contact reply failed (error: " << lastError << ")";
}

void SdkTest::removeContact(string email, int timeout)
{
    MegaUser *u = megaApi->getContact(email.data());
    bool null_pointer = (u == NULL);
    ASSERT_FALSE(null_pointer) << "Cannot find the specified contact (" << email << ")";

    responseReceived = false;

    megaApi->removeContact(u);

    waitForResponse(&responseReceived, timeout);      // at the target side (auxiliar account)

    if (timeout)
    {
        ASSERT_TRUE(responseReceived) << "Contact deletion not finished after " << timeout  << " seconds";
    }

    ASSERT_EQ(MegaError::API_OK, lastError) << "Contact deletion failed (error: " << lastError << ")";

    delete u;
}

void SdkTest::shareFolder(MegaNode *n, const char *email, int action, int timeout)
{
    responseReceived = false;

    megaApi->share(n, email, action);

    waitForResponse(&responseReceived, timeout);

    if (timeout)
    {
        ASSERT_TRUE(responseReceived) << "Folder sharing not finished after " << timeout  << " seconds";
    }

    ASSERT_EQ(MegaError::API_OK, lastError) << "Folder sharing failed (error: " << lastError << ")" << endl << "User: " << email << " Action: " << action;
}

void SdkTest::createPublicLink(MegaNode *n, int timeout)
{
    responseReceived = false;

    megaApi->exportNode(n);

    waitForResponse(&responseReceived, timeout);

    if (timeout)
    {
        ASSERT_TRUE(responseReceived) << "Public link creation not finished after " << timeout  << " seconds";
    }

    ASSERT_EQ(MegaError::API_OK, lastError) << "Public link creation failed (error: " << lastError << ")";
}

void SdkTest::importPublicLink(string link, MegaNode *parent, int timeout)
{
    responseReceived = false;

    megaApi->importFileLink(link.data(), parent);

    waitForResponse(&responseReceived, timeout);

    if (timeout)
    {
        ASSERT_TRUE(responseReceived) << "Public link import not finished after " << timeout  << " seconds";
    }

    ASSERT_EQ(MegaError::API_OK, lastError) << "Public link import failed (error: " << lastError << ")";
}

void SdkTest::getPublicNode(string link, int timeout)
{
    responseReceived = false;

    megaApiAux->getPublicNode(link.data());

    waitForResponse(&responseReceived, timeout);

    if (timeout)
    {
        ASSERT_TRUE(responseReceived) << "Public link retrieval not finished after " << timeout  << " seconds";
    }

    ASSERT_EQ(MegaError::API_OK, lastError) << "Public link retrieval failed (error: " << lastError << ")";
}

void SdkTest::removePublicLink(MegaNode *n, int timeout)
{
    responseReceived = false;

    megaApi->disableExport(n);

    waitForResponse(&responseReceived, timeout);

    if (timeout)
    {
        ASSERT_TRUE(responseReceived) << "Public link removal not finished after " << timeout  << " seconds";
    }

    ASSERT_EQ(MegaError::API_OK, lastError) << "Public link removal failed (error: " << lastError << ")";
}

void SdkTest::getContactRequest(MegaContactRequest *cr, bool outgoing)
{
    MegaContactRequestList *crl;

    cr = NULL;

    if (outgoing)
    {
        crl = megaApi->getOutgoingContactRequests();
        ASSERT_EQ(1, crl->size()) << "Too many outgoing contact requests in main account";
    }
    else
    {
        crl = megaApiAux->getIncomingContactRequests();
        ASSERT_EQ(1, crl->size()) << "Too many incoming contact requests in auxiliar account";
    }

    MegaContactRequest *temp = crl->get(0);
    cr = temp->copy();

    delete crl;
}

///////////////////////////__ Tests using SdkTest __//////////////////////////////////

/**
 * @brief TEST_F SdkTestResumeSession
 *
 * It creates a local cache, logs out of the current session and tries to resume it later.
 */
TEST_F(SdkTest, SdkTestResumeSession)
{
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
    // --- Create a new folder ---

    MegaNode *rootnode = megaApi->getRootNode();
    char name1[64] = "New folder";

    responseReceived = false;
    megaApi->createFolder(name1, rootnode);
    waitForResponse(&responseReceived);

    ASSERT_EQ(MegaError::API_OK, lastError) << "Cannot create a folder (error: " << lastError << ")";


    // --- Rename a node ---

    MegaNode *n1 = megaApi->getNodeByHandle(h);
    strcpy(name1, "Folder renamed");

    responseReceived = false;
    megaApi->renameNode(n1, name1);
    waitForResponse(&responseReceived);

    ASSERT_EQ(MegaError::API_OK, lastError) << "Cannot rename a node (error: " << lastError << ")";


    // --- Copy a node ---

    MegaNode *n2;
    char name2[64] = "Folder copy";

    responseReceived = false;
    megaApi->copyNode(n1, rootnode, name2);
    waitForResponse(&responseReceived);

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

    responseReceived = false;
    megaApi->moveNode(n1, n2);
    waitForResponse(&responseReceived);

    ASSERT_EQ(MegaError::API_OK, lastError) << "Cannot move node (error: " << lastError << ")";


    // --- Get parent node ---

    MegaNode *n5;
    n5 = megaApi->getParentNode(n1);

    ASSERT_EQ(n2->getHandle(), n5->getHandle()) << "Wrong parent node";


    // --- Send to Rubbish bin ---

    responseReceived = false;
    megaApi->moveNode(n2, megaApi->getRubbishNode());
    waitForResponse(&responseReceived);

    ASSERT_EQ(MegaError::API_OK, lastError) << "Cannot move node to Rubbish bin (error: " << lastError << ")";


    // --- Remove a node ---

    responseReceived = false;
    megaApi->remove(n2);
    waitForResponse(&responseReceived);

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
    MegaNode *rootnode = megaApi->getRootNode();
    string filename1 = UPFILE;
    createFile(filename1);


    // --- Cancel a transfer ---

    transfersCancelled = false;
    megaApi->startUpload(filename1.data(), rootnode);
    megaApi->cancelTransfers(MegaTransfer::TYPE_UPLOAD);
    waitForResponse(&transfersCancelled);

    EXPECT_EQ(MegaError::API_OK, lastError) << "Transfer cancellation failed (error: " << lastError << ")";


    // --- Upload a file (part 1) ---

    uploadFinished = false;
    megaApi->startUpload(filename1.data(), rootnode);
    // do not wait yet for completion


    // --- Pause a transfer ---

    transfersPaused = false;
    megaApi->pauseTransfers(true, MegaTransfer::TYPE_UPLOAD);
    waitForResponse(&transfersPaused);

    EXPECT_EQ(MegaError::API_OK, lastError) << "Cannot pause transfer (error: " << lastError << ")";
    EXPECT_TRUE(megaApi->areTransfersPaused(MegaTransfer::TYPE_UPLOAD)) << "Upload transfer not paused";


    // --- Resume a transfer ---

    transfersPaused = false;
    megaApi->pauseTransfers(false, MegaTransfer::TYPE_UPLOAD);
    waitForResponse(&transfersPaused);

    EXPECT_EQ(MegaError::API_OK, lastError) << "Cannot resume transfer (error: " << lastError << ")";
    EXPECT_FALSE(megaApi->areTransfersPaused(MegaTransfer::TYPE_UPLOAD)) << "Upload transfer not resumed";


    // --- Upload a file (part 2) ---

    waitForResponse(&uploadFinished);

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

    downloadFinished = false;
    megaApi->startDownload(n2, filename2.c_str());
    waitForResponse(&downloadFinished);

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
 * - Remove contact
 *
 * TODO:
 * - Invite a contact not registered in MEGA yet (requires validation of account)
 * - Remind an existing invitation (requires 2 weeks wait)
 */
TEST_F(SdkTest, SdkTestContacts)
{
    MegaContactRequestList *crl, *crlaux;
    MegaContactRequest *cr, *craux;

    ASSERT_NO_FATAL_FAILURE( getMegaApiAux() );    // login + fetchnodes


    // --- Check my email and the email of the contact ---

    EXPECT_STREQ(email.data(), megaApi->getMyEmail());
    EXPECT_STREQ(emailaux.data(), megaApiAux->getMyEmail());


    // --- Send a new contact request ---

    string message = "Hi contact. This is a testing message";

    contactRequestUpdated = false;
    contactRequestUpdatedAux = false;

    ASSERT_NO_FATAL_FAILURE( inviteContact(emailaux, message, MegaContactRequest::INVITE_ACTION_ADD) );

    waitForResponse(&contactRequestUpdatedAux); // at the target side (auxiliar account)
    waitForResponse(&contactRequestUpdated);    // at the source side (main account)


    // --- Check the sent contact request ---

//    getContactRequest(cr, true);

    crl = megaApi->getOutgoingContactRequests();
    ASSERT_EQ(1, crl->size()) << "Too many outgoing contact requests in main account";
    cr = crl->get(0);

    ASSERT_STREQ(message.data(), cr->getSourceMessage()) << "Message sent is corrupted";
    ASSERT_STREQ(email.data(), cr->getSourceEmail()) << "Wrong source email";
    ASSERT_STREQ(emailaux.data(), cr->getTargetEmail()) << "Wrong target email";
    ASSERT_EQ(MegaContactRequest::STATUS_UNRESOLVED, cr->getStatus()) << "Wrong contact request status";
    ASSERT_TRUE(cr->isOutgoing()) << "Wrong direction of the contact request";

    delete crl;


    // --- Check received contact request ---

    crlaux = megaApiAux->getIncomingContactRequests();
    ASSERT_EQ(1, crlaux->size()) << "Too many incoming contact requests in auxiliar account";
    craux = crlaux->get(0);

    ASSERT_STREQ(message.data(), craux->getSourceMessage()) << "Message received is corrupted";
    ASSERT_STREQ(email.data(), craux->getSourceEmail()) << "Wrong source email";
    ASSERT_STREQ(NULL, craux->getTargetEmail()) << "Wrong target email";    // NULL according to MegaApi documentation
    ASSERT_EQ(MegaContactRequest::STATUS_UNRESOLVED, craux->getStatus()) << "Wrong contact request status";
    ASSERT_FALSE(craux->isOutgoing()) << "Wrong direction of the contact request";

    delete crlaux;

    // --- Ignore received contact request ---

    crlaux = megaApiAux->getIncomingContactRequests();
    ASSERT_EQ(1, crlaux->size()) << "Too many incoming contact requests in auxiliar account";
    craux = crlaux->get(0);

    contactRequestUpdatedAux = false;
    ASSERT_NO_FATAL_FAILURE( replyContact(craux, MegaContactRequest::REPLY_ACTION_IGNORE) );
    waitForResponse(&contactRequestUpdatedAux); // only at auxiliar account. Main account is not notified

    delete crlaux;

    crlaux = megaApiAux->getIncomingContactRequests();  // it only returns pending requests
    ASSERT_EQ(0, crlaux->size()) << "Incoming contact requests was not ignored properly";
    delete crlaux;


    // --- Cancel the invitation ---

    message = "I don't wanna be your contact anymore";

    contactRequestUpdated = false;
    ASSERT_NO_FATAL_FAILURE( inviteContact(emailaux, message, MegaContactRequest::INVITE_ACTION_DELETE) );
    waitForResponse(&contactRequestUpdated);    // at the source side (main account)

    crl = megaApi->getOutgoingContactRequests();
    ASSERT_EQ(0, crl->size()) << "Outgoing contact requests still pending in main account";
    delete crl;
    // The target contact doesn't receive notification, since the invitation was ignored previously

    
    // --- Remind a contact invitation (cannot until 2 weeks after invitation/last reminder) ---

//    contactRequestReceived = false;
//    megaApi->inviteContact(emailaux.data(), message.data(), MegaContactRequest::INVITE_ACTION_REMIND);
//    waitForResponse(&contactRequestReceived, 30); // at the target side (auxiliar account)

//    ASSERT_TRUE(contactRequestReceived) << "Contact invitation reminder not received after " << timeout  << " seconds";


    // --- Invite a new contact (again) ---

    contactRequestUpdated = false;
    contactRequestUpdatedAux = false;

    ASSERT_NO_FATAL_FAILURE( inviteContact(emailaux, message, MegaContactRequest::INVITE_ACTION_ADD) );

    waitForResponse(&contactRequestUpdatedAux); // at the target side (auxiliar account)
    waitForResponse(&contactRequestUpdated);    // at the source side (main account)


    // --- Deny a contact invitation ---

    crlaux = megaApiAux->getIncomingContactRequests();
    ASSERT_EQ(1, crlaux->size()) << "Incoming contact requests was not received properly";
    craux = crlaux->get(0);

    contactRequestUpdated = false;
    contactRequestUpdatedAux = false;

    replyContact(craux, MegaContactRequest::REPLY_ACTION_DENY);

    waitForResponse(&contactRequestUpdatedAux); // at the target side (auxiliar account)
    waitForResponse(&contactRequestUpdated);    // at the source side (main account)

    delete crlaux;

    crl = megaApi->getOutgoingContactRequests();
    ASSERT_EQ(0, crl->size()) << "Outgoing contact request still pending in main account";
    delete crl;

    crlaux = megaApiAux->getIncomingContactRequests();
    ASSERT_EQ(0, crlaux->size()) << "Incoming contact requests still pending in auxliar account";
    delete crlaux;


    // --- Invite a new contact (again) ---

    contactRequestUpdated = false;
    contactRequestUpdatedAux = false;

    ASSERT_NO_FATAL_FAILURE( inviteContact(emailaux, message, MegaContactRequest::INVITE_ACTION_ADD) );

    waitForResponse(&contactRequestUpdatedAux); // at the target side (auxiliar account)
    waitForResponse(&contactRequestUpdated);    // at the source side (main account)


    // --- Accept a contact invitation ---

    crlaux = megaApiAux->getIncomingContactRequests();
    ASSERT_EQ(1, crlaux->size()) << "Too many incoming contact requests in auxiliar account";
    craux = crlaux->get(0);

    contactRequestUpdated = false;
    contactRequestUpdatedAux = false;

    ASSERT_NO_FATAL_FAILURE( replyContact(craux, MegaContactRequest::REPLY_ACTION_ACCEPT) );

    waitForResponse(&contactRequestUpdatedAux); // at the target side (auxiliar account)
    waitForResponse(&contactRequestUpdated);    // at the source side (main account)

    delete crlaux;

    crl = megaApi->getOutgoingContactRequests();
    ASSERT_EQ(0, crl->size()) << "Outgoing contact requests still pending in main account";
    delete crl;

    crlaux = megaApiAux->getIncomingContactRequests();
    ASSERT_EQ(0, crlaux->size()) << "Incoming contact requests still pending in auxiliar account";
    delete crlaux;


    // --- Delete an existing contact ---

    contactRemoved = false;
    ASSERT_NO_FATAL_FAILURE( removeContact(emailaux) );
    waitForResponse(&contactRemoved);

    MegaUser *u = megaApi->getContact(emailaux.data());
    ASSERT_EQ(MegaUser::VISIBILITY_HIDDEN, u->getVisibility()) << "New contact still visible";
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
    MegaShareList *sl;
    MegaShare *s;
    MegaNodeList *nl;
    MegaNode *n;
    MegaNode *n1;

    getMegaApiAux();    // login + fetchnodes


    // Initialize a test scenario : create some folders/files to share

    // Create some nodes to share
    //  |--Shared-folder
    //    |--subfolder
    //    |--file.txt

    MegaNode *rootnode = megaApi->getRootNode();
    char foldername1[64] = "Shared-folder";
    MegaHandle hfolder1;

    responseReceived = false;
    megaApi->createFolder(foldername1, rootnode);
    waitForResponse(&responseReceived);

    ASSERT_EQ(MegaError::API_OK, lastError) << "Cannot create a folder (error: " << lastError << ")";
    hfolder1 = h;     // 'h' is set in 'onRequestFinish()'
    n1 = megaApi->getNodeByHandle(hfolder1);

    char foldername2[64] = "subfolder";
    MegaHandle hfolder2;

    responseReceived = false;
    megaApi->createFolder(foldername2, megaApi->getNodeByHandle(hfolder1));
    waitForResponse(&responseReceived);

    ASSERT_EQ(MegaError::API_OK, lastError) << "Cannot create a folder (error: " << lastError << ")";
    hfolder2 = h;

    MegaHandle hfile1;
    createFile(PUBLICFILE.data(), false);   // not a large file since don't need to test transfers here

    uploadFinished = false;
    megaApi->startUpload(PUBLICFILE.data(), megaApi->getNodeByHandle(hfolder1));
    waitForResponse(&uploadFinished);

    ASSERT_EQ(MegaError::API_OK, lastError) << "Cannot upload file (error: " << lastError << ")";
    hfile1 = h;


    // Initialize a test scenario: create a new contact to share to

    string message = "Hi contact. Let's share some stuff";

    contactRequestUpdated = false;
    contactRequestUpdatedAux = false;

    ASSERT_NO_FATAL_FAILURE( inviteContact(emailaux, message, MegaContactRequest::INVITE_ACTION_ADD) );

    waitForResponse(&contactRequestUpdatedAux); // at the target side (auxiliar account)
    waitForResponse(&contactRequestUpdated);    // at the source side (main account)


    MegaContactRequestList *crlaux = megaApiAux->getIncomingContactRequests();
    ASSERT_EQ(1, crlaux->size()) << "Too many incoming contact requests in auxiliar account";
    MegaContactRequest *craux = crlaux->get(0);

    contactRequestUpdated = false;
    contactRequestUpdatedAux = false;

    ASSERT_NO_FATAL_FAILURE( replyContact(craux, MegaContactRequest::REPLY_ACTION_ACCEPT) );

    waitForResponse(&contactRequestUpdatedAux); // at the target side (auxiliar account)
    waitForResponse(&contactRequestUpdated);    // at the source side (main account)

    delete crlaux;


    // --- Create a new outgoing share ---

    nodeUpdated = false;
    nodeUpdatedAux = false;

    ASSERT_NO_FATAL_FAILURE( shareFolder(n1, emailaux.data(), MegaShare::ACCESS_READ) );

    waitForResponse(&nodeUpdated);
    waitForResponse(&nodeUpdatedAux);


    // --- Check the outgoing share ---

    sl = megaApi->getOutShares();
    ASSERT_EQ(1, sl->size()) << "Outgoing share failed";
    s = sl->get(0);

    ASSERT_EQ(MegaShare::ACCESS_READ, s->getAccess()) << "Wrong access level of outgoing share";
    ASSERT_EQ(hfolder1, s->getNodeHandle()) << "Wrong node handle of outgoing share";
    ASSERT_STREQ(emailaux.data(), s->getUser()) << "Wrong email address of outgoing share";
    ASSERT_TRUE(megaApi->isShared(n1)) << "Wrong sharing information at outgoing share";
    ASSERT_TRUE(megaApi->isOutShare(n1)) << "Wrong sharing information at outgoing share";

    delete sl;


    // --- Check the incoming share ---

    nl = megaApiAux->getInShares(megaApiAux->getContact(email.data()));
    ASSERT_EQ(1, nl->size()) << "Incoming share not received in auxiliar account";
    n = nl->get(0);

    ASSERT_EQ(hfolder1, n->getHandle()) << "Wrong node handle of incoming share";
    ASSERT_STREQ(foldername1, n->getName()) << "Wrong folder name of incoming share";
    ASSERT_EQ(MegaError::API_OK, megaApiAux->checkAccess(n, MegaShare::ACCESS_READ).getErrorCode()) << "Wrong access level of incoming share";
    ASSERT_TRUE(megaApiAux->isInShare(n)) << "Wrong sharing information at incoming share";
    ASSERT_TRUE(megaApiAux->isShared(n)) << "Wrong sharing information at incoming share";

    delete nl;


    // --- Modify the access level of an outgoing share ---

    nodeUpdated = false;
    nodeUpdatedAux = false;

    ASSERT_NO_FATAL_FAILURE( shareFolder(megaApi->getNodeByHandle(hfolder1), emailaux.data(), MegaShare::ACCESS_READWRITE) );

    waitForResponse(&nodeUpdated);
    waitForResponse(&nodeUpdatedAux);

    nl = megaApiAux->getInShares(megaApiAux->getContact(email.data()));
    ASSERT_EQ(1, nl->size()) << "Incoming share not received in auxiliar account";
    n = nl->get(0);

    ASSERT_EQ(MegaError::API_OK, megaApiAux->checkAccess(n, MegaShare::ACCESS_READWRITE).getErrorCode()) << "Wrong access level of incoming share";

    delete nl;


    // --- Revoke access to an outgoing share ---

    nodeUpdated = false;
    nodeUpdatedAux = false;

    ASSERT_NO_FATAL_FAILURE( shareFolder(n1, emailaux.data(), MegaShare::ACCESS_UNKNOWN) );

    waitForResponse(&nodeUpdated);
    waitForResponse(&nodeUpdatedAux);

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

    nodeUpdated = false;
    ASSERT_NO_FATAL_FAILURE( shareFolder(n, emailfake, MegaShare::ACCESS_FULL) );
    waitForResponse(&nodeUpdated);

    sl = megaApi->getPendingOutShares(n);   delete n;
    ASSERT_EQ(1, sl->size()) << "Pending outgoing share failed";
    s = sl->get(0);
    n = megaApi->getNodeByHandle(s->getNodeHandle());

//    ASSERT_STREQ(emailfake, s->getUser()) << "Wrong email address of outgoing share"; User is not created yet
    ASSERT_FALSE(megaApi->isShared(n)) << "Node is already shared, must be pending";
    ASSERT_FALSE(megaApi->isOutShare(n)) << "Node is already shared, must be pending";

    delete sl;
    delete n;


    // --- Create a public link ---

    MegaNode *nfile1 = megaApi->getNodeByHandle(hfile1);

    ASSERT_NO_FATAL_FAILURE( createPublicLink(nfile1) );
    // The created link is stored in this->link at onRequestFinish()


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
