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

#include "mega.h"
#include "../include/megaapi.h"
#include "../include/megaapi_impl.h"
#include "gtest/gtest.h"

using namespace mega;
using ::testing::Test;

static const string APP_KEY     = "8QxzVRxD";
static const string USER_AGENT  = "Unit Tests with GoogleTest framework";

// IMPORTANT: the main account must be empty (Cloud & Rubbish) before starting the test and it will be purged at exit.
// Both main and auxiliar accounts shouldn't be contacts yet and shouldn't have any pending contact requests.
// Set your login credentials as environment variables: $MEGA_EMAIL and $MEGA_PWD

static const unsigned int pollingT = 500000;  // (microseconds) to check if response from server is received

static const string UPFILE      = "file1.txt";
static const string DOWNFILE    = "file2.txt";

// Fixture class with common code for most of tests
class SdkTest : public ::testing::Test, public MegaListener, MegaRequestListener, MegaTransferListener {

public:
    MegaApi *megaApi = NULL;
    string email;
    string pwd;

    int lastError;

    bool loggingReceived;
    bool fetchnodesReceived;
    bool logoutReceived;
    bool responseReceived;

    bool downloadFinished;
    bool uploadFinished;
    bool transfersCancelled;
    bool transfersPaused;

    MegaHandle h;

    MegaApi *megaApiAux = NULL;
    string emailaux;

    bool contactInvitationFinished;
    bool contactReplyFinished;
    bool contactRequestUpdated;
    bool contactRequestUpdatedAux;
    bool contactRemoved;

private:


protected:
    virtual void SetUp()
    {
        // do some initialization

        readCredentials();

        if (megaApi == NULL)
        {
            char path[1024];
            getcwd(path, sizeof path);
            megaApi = new MegaApi(APP_KEY.c_str(), path, USER_AGENT.c_str());

            megaApi->addListener(this);

            login();
            fetchnodes();
        }
    }

    virtual void TearDown()
    {
        // do some cleanup        

        deleteFile(UPFILE);
        deleteFile(DOWNFILE);

        releaseMegaApiAux();

        if (megaApi)
        {
            // Remove nodes in Cloud & Rubbish
            purgeTree(megaApi->getRootNode());
            purgeTree(megaApi->getRubbishNode());

            if (megaApi->isLoggedIn())
                logout(10);

            delete megaApi;
        }
    }

    void onRequestStart(MegaApi *api, MegaRequest *request) {}
    void onRequestUpdate(MegaApi*api, MegaRequest *request) {}
    void onRequestFinish(MegaApi *api, MegaRequest *request, MegaError *e)
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
            responseReceived = true;
            h = request->getNodeHandle();
            break;

        case MegaRequest::TYPE_RENAME:
            responseReceived = true;
            break;

        case MegaRequest::TYPE_COPY:
            responseReceived = true;
            h = request->getNodeHandle();
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
            contactRemoved = true;
            break;
        }
    }
    void onRequestTemporaryError(MegaApi *api, MegaRequest *request, MegaError* error) {}
    void onTransferStart(MegaApi *api, MegaTransfer *transfer) { }
    void onTransferFinish(MegaApi* api, MegaTransfer *transfer, MegaError* e)
    {
        lastError = e->getErrorCode();

        switch(transfer->getType())
        {
        case MegaTransfer::TYPE_DOWNLOAD:
            downloadFinished = true;
            break;

        case MegaTransfer::TYPE_UPLOAD:
            uploadFinished = true;
            break;
        }

        if (lastError == MegaError::API_OK)
            h = transfer->getNodeHandle();
    }
    void onTransferUpdate(MegaApi *api, MegaTransfer *transfer) {}
    void onTransferTemporaryError(MegaApi *api, MegaTransfer *transfer, MegaError* error) {}
    void onUsersUpdate(MegaApi* api, MegaUserList *users)
    {
        // Main testing account
        if (api == megaApi)
        {
            contactRemoved = true;
        }
    }
    void onNodesUpdate(MegaApi* api, MegaNodeList *nodes) {}
    void onAccountUpdate(MegaApi *api) {}
    void onContactRequestsUpdate(MegaApi* api, MegaContactRequestList* requests)
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
    void onReloadNeeded(MegaApi *api) {}
#ifdef ENABLE_SYNC
    void onSyncFileStateChanged(MegaApi *api, MegaSync *sync, const char *filePath, int newState) {}
    void onSyncEvent(MegaApi *api, MegaSync *sync,  MegaSyncEvent *event) {}
    void onSyncStateChanged(MegaApi *api,  MegaSync *sync) {}
    void onGlobalSyncStateChanged(MegaApi* api) {}
#endif

public:
    void login(int timeout = 0)   // Seconds to wait for response. 0 means no timeout
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

    void fetchnodes(int timeout = 0)   // t: seconds to wait for response. 0 means no timeout
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

    void logout(int timeout = 0)
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

    char* dumpSession()
    {
        return megaApi->dumpSession();
    }

    void locallogout(int timeout = 0)
    {
        logoutReceived = false;

        megaApi->localLogout(this);

        waitForResponse(&logoutReceived, timeout);

        if (timeout)
        {
            EXPECT_TRUE(logoutReceived) << "Local logout failed after " << timeout  << " seconds";
        }

        EXPECT_EQ(MegaError::API_OK, lastError) << "Local logout failed (error: " << lastError << ")";
    }

    void resumeSession(char *session, int timeout = 0)
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

    void purgeTree(MegaNode *p)
    {
        MegaNodeList *children;
        children = megaApi->getChildren(p);

        for (int i = 0; i < children->size(); i++)
        {
            megaApi->remove(children->get(i));
        }
    }

    void readCredentials()
    {
        char *buf = getenv("MEGA_EMAIL");
        if (buf)
            email.assign(buf);
        ASSERT_LT(0, email.length()) << "Set your username at the environment variable $MEGA_EMAIL";

        buf = getenv("MEGA_PWD");
        if (buf)
            pwd.assign(buf);
        ASSERT_LT(0, pwd.length()) << "Set your password at the environment variable $MEGA_PWD";
    }

    void waitForResponse(bool *responseReceived, int timeout = 0)
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

    void createFile(string filename)
    {
        FILE *fp;
        fp = fopen(filename.c_str(), "w");

        if (fp)
        {
            // create a file large enough for long upload/download times (5-10MB)
            int limit = 1000000 + rand() % 1000000;
            for (int i = 0; i < limit; i++)
            {
                fprintf(fp, "test ");
            }

            fclose(fp);
        }
    }

    size_t getFilesize(string filename)
    {
        struct stat stat_buf;
        int rc = stat(filename.c_str(), &stat_buf);

        return rc == 0 ? stat_buf.st_size : -1;
    }

    void deleteFile(string filename)
    {
        remove(filename.c_str());
    }

    void getMegaApiAux()
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

            ASSERT_TRUE(megaApiAux->isLoggedIn()) << "Login failed in the auxiliar account";

            fetchnodesReceived = false;
            megaApiAux->fetchNodes();
            waitForResponse(&fetchnodesReceived);

            ASSERT_EQ(MegaError::API_OK, lastError) << "Fetchnodes failed in the auxiliar account (error: " << lastError << ")";
        }
    }

    void releaseMegaApiAux()
    {
        if (megaApiAux)
        {
            if (megaApiAux->isLoggedIn())
            {
                logoutReceived = false;
                megaApi->logout();
                waitForResponse(&logoutReceived, 5);
            }

            delete megaApiAux;
            megaApiAux = NULL;
        }
    }

    void inviteContact(string email, string message, int action, int timeout = 0)
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

    void replyContact(MegaContactRequest *cr, int action, int timeout = 0)
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

    void getContactRequest(MegaContactRequest *cr, bool outgoing)
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
};

///////////////////////////__ Tests using SdkTest __//////////////////////////////////

/**
 * @brief TEST_F SdkTestResumeSession
 *
 * It creates a local cache, logs out of the current session and tries to resume it later.
 */
TEST_F(SdkTest, DISABLED_SdkTestResumeSession)
{
    char *session = dumpSession();
    locallogout();

    resumeSession(session);

    delete session;
}

/**
 * @brief TEST_F SdkTestNodeOperations
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
TEST_F(SdkTest, DISABLED_SdkTestNodeOperations)
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

    EXPECT_EQ(1, nlist->size());
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
 * - Starts an upload transfer and cancel it
 * - Starts an upload transfer, pause it, resume it and complete it
 * - Get node by fingerprint
 * - Get size of a node
 * - Download a file
 */
TEST_F(SdkTest, DISABLED_SdkTestTransfers)
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
 */
TEST_F(SdkTest, SdkTestContacts)
{
    MegaContactRequestList *crl, *crlaux;
    MegaContactRequest *cr, *craux;

    getMegaApiAux();    // login + fetchnodes


    // --- Check my email and the email of the contact ---

    EXPECT_STREQ(email.data(), megaApi->getMyEmail());
    EXPECT_STREQ(emailaux.data(), megaApiAux->getMyEmail());


    // --- Send a new contact request ---

    string message = "Hi contact. This is a testing message";

    contactRequestUpdated = false;
    contactRequestUpdatedAux = false;

    inviteContact(emailaux, message, MegaContactRequest::INVITE_ACTION_ADD);

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
    replyContact(craux, MegaContactRequest::REPLY_ACTION_IGNORE);
    waitForResponse(&contactRequestUpdatedAux); // only at auxiliar account. Main account is not notified

    delete crlaux;

    crlaux = megaApiAux->getIncomingContactRequests();  // it only returns pending requests
    ASSERT_EQ(0, crlaux->size()) << "Incoming contact requests was not ignored properly";
    delete crlaux;


    // --- Cancel the invitation ---

    message = "I don't wanna be your contact anymore";

    contactRequestUpdated = false;
    inviteContact(emailaux, message, MegaContactRequest::INVITE_ACTION_DELETE);
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

    inviteContact(emailaux, message, MegaContactRequest::INVITE_ACTION_ADD);

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

    inviteContact(emailaux, message, MegaContactRequest::INVITE_ACTION_ADD);

    waitForResponse(&contactRequestUpdatedAux); // at the target side (auxiliar account)
    waitForResponse(&contactRequestUpdated);    // at the source side (main account)


    // --- Accept a contact invitation ---

    crlaux = megaApiAux->getIncomingContactRequests();
    ASSERT_EQ(1, crlaux->size()) << "Too many incoming contact requests in auxiliar account";
    craux = crlaux->get(0);

    contactReplyFinished = false;
    contactRequestUpdated = false;
    contactRequestUpdatedAux = false;

    megaApiAux->replyContactRequest(craux, MegaContactRequest::REPLY_ACTION_ACCEPT);

    waitForResponse(&contactReplyFinished); // at the source side (main account)    
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

    MegaUser *u = megaApi->getContact(emailaux.data());
    bool null_pointer = (u == NULL);
    ASSERT_FALSE(null_pointer) << "Cannot find new contact";

    contactRemoved = false;
    megaApi->removeContact(u);
    waitForResponse(&contactRemoved);

    delete u;

    u = megaApi->getContact(emailaux.data());
    ASSERT_EQ(MegaUser::VISIBILITY_HIDDEN, u->getVisibility()) << "New contact still visible";
    delete u;
}

TEST_F(SdkTest, DISABLED_SdkTestShares)
{
    // --- Create a new outgoing share ---
    // megaApi->share(node, user/email, level);


    // --- Get existing outgoing shares ---
    // MegaShareList *os;
    // outshares = megaApi->getOutShares();
    // delete outshares;


    // --- Get pending outgoing shares ---
    // MegaShareList *pos;
    // pos = megaApi->getPendingOutShares();
    // delete pos;


    // --- Modify the access level of an outgoing share ---
    // megaApi->share(node, user/email, level);


    // --- Check access level of a node ---
    // megaApi->checkAccess(level);


    // --- Revoke access to an outgoing share ---


    // --- Check if a node is shared ---
    // megaApi->isShared(node); sync


    // --- Receive a new incoming share ---
    // megaApi->getInShares(user);
    // megaApi->getInShares();   // from all the users


    // --- Create a public link ---
    // megaApi->exportNode(node);


    // --- Get node from public link ---
    // megaApi->getPublicNode(link); async


    // --- Import a public link ---
    // megaApi->importFileLink(link, parent);


    // --- Remove a public link ---
    // megaApi->disableExport(node);
}
