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
#include "megaapi_impl.h"
#include <algorithm>

#ifdef _WIN32
#include <filesystem>
#endif

using namespace std;

MegaFileSystemAccess fileSystemAccess;


#ifdef WIN32
DWORD ThreadId()
{
    return GetCurrentThreadId();
}
#else
pthread_t ThreadId()
{
    return pthread_self();
}
#endif



const char* cwd()
{
    // for windows and linux
    static char path[1024];
    const char* ret;
    #ifdef _WIN32
    ret = _getcwd(path, sizeof path);
    #else
    ret = getcwd(path, sizeof path);
    #endif
    assert(ret);
    return ret;
}

bool fileexists(const std::string& fn)
{
#ifdef _WIN32
    return std::experimental::filesystem::exists(fn);
#else
    struct stat   buffer;
    return (stat(fn.c_str(), &buffer) == 0);
#endif
}

void copyFile(std::string& from, std::string& to)
{
    std::string f = from, t = to;
    fileSystemAccess.path2local(&from, &f);
    fileSystemAccess.path2local(&to, &t);
    fileSystemAccess.copylocal(&f, &t, m_time());
}

std::string megaApiCacheFolder(int index)
{
    std::string p(cwd());
#ifdef _WIN32
    p += "\\";
#else
    p += "/";
#endif
    if (index == 0)
        p += "sdk_test_mega_cache_0";
    else
        p += "sdk_test_mega_cache_1";

    if (!fileexists(p))
    {

#ifdef _WIN32
        bool success = std::experimental::filesystem::create_directory(p);
        assert(success);
#else
        mkdir(p.c_str(), S_IRWXU);
        assert(fileexists(p));
#endif

    }
    return p;
}


void WaitMillisec(unsigned n)
{
#ifdef _WIN32
    Sleep(n);
#else
    usleep(n * 1000);
#endif
}

enum { USERALERT_ARRIVAL_MILLISEC = 1000 };

#ifdef WIN32
#include "mega/autocomplete.h"
#include <filesystem>
#define getcwd _getcwd
void usleep(int n) 
{
    Sleep(n / 1000);
}
#endif

void SdkTest::SetUp()
{
    // do some initialization
    megaApi[0] = megaApi[1] = NULL;

    char *buf = getenv("MEGA_EMAIL");
    if (buf)
        email[0].assign(buf);
    ASSERT_LT((size_t)0, email[0].length()) << "Set your username at the environment variable $MEGA_EMAIL";

    buf = getenv("MEGA_PWD");
    if (buf)
        pwd[0].assign(buf);
    ASSERT_LT((size_t)0, pwd[0].length()) << "Set your password at the environment variable $MEGA_PWD";

    testingInvalidArgs = false;

    if (megaApi[0] == NULL)
    {
        logger = new MegaLoggerSDK("SDK.log");
        MegaApi::addLoggerObject(logger);

        megaApi[0] = new MegaApi(APP_KEY.c_str(), megaApiCacheFolder(0).c_str(), USER_AGENT.c_str());

        megaApi[0]->setLoggingName("0");
        megaApi[0]->setLogLevel(MegaApi::LOG_LEVEL_DEBUG);
        megaApi[0]->addListener(this);

        megaApi[0]->log(MegaApi::LOG_LEVEL_INFO, "___ Initializing test (SetUp()) ___");

        ASSERT_NO_FATAL_FAILURE( login(0) );
        ASSERT_NO_FATAL_FAILURE( fetchnodes(0) );
    }
}

void SdkTest::TearDown()
{
    // do some cleanup

    testingInvalidArgs = false;

    deleteFile(UPFILE);
    deleteFile(DOWNFILE);
    deleteFile(PUBLICFILE);
    deleteFile(AVATARDST);

    releaseMegaApi(1);

    if (megaApi[0])
    {        
        megaApi[0]->log(MegaApi::LOG_LEVEL_INFO, "___ Cleaning up test (TearDown()) ___");

        // Remove nodes in Cloud & Rubbish
        purgeTree(megaApi[0]->getRootNode());
        purgeTree(megaApi[0]->getRubbishNode());
//        megaApi[0]->cleanRubbishBin();

        // Remove auxiliar contact
        MegaUserList *ul = megaApi[0]->getContacts();
        for (int i = 0; i < ul->size(); i++)
        {
            removeContact(ul->get(i)->getEmail());
        }

        // Remove pending contact requests
        MegaContactRequestList *crl = megaApi[0]->getOutgoingContactRequests();
        for (int i = 0; i < crl->size(); i++)
        {
            MegaContactRequest *cr = crl->get(i);
            megaApi[0]->inviteContact(cr->getTargetEmail(), "Removing you", MegaContactRequest::INVITE_ACTION_DELETE);
        }

        releaseMegaApi(0);

        MegaApi::removeLoggerObject(logger);
        delete logger;
    }
}

void SdkTest::onRequestFinish(MegaApi *api, MegaRequest *request, MegaError *e)
{
    unsigned int apiIndex;
    if (api == megaApi[0])
    {
        apiIndex = 0;
    }
    else if (api == megaApi[1])
    {
        apiIndex = 1;
    }
    else
    {
        LOG_err << "Instance of MegaApi not recognized";
        return;
    }

    requestFlags[apiIndex][request->getType()] = true;
    lastError[apiIndex] = e->getErrorCode();

    switch(request->getType())
    {
    case MegaRequest::TYPE_CREATE_FOLDER:
        h = request->getNodeHandle();
        break;

    case MegaRequest::TYPE_COPY:
        h = request->getNodeHandle();
        break;

    case MegaRequest::TYPE_EXPORT:
        if (lastError[apiIndex] == API_OK)
        {
            h = request->getNodeHandle();
            if (request->getAccess())
            {
                link.assign(request->getLink());
            }
        }
        break;

    case MegaRequest::TYPE_GET_PUBLIC_NODE:
        if (lastError[apiIndex] == API_OK)
        {
            publicNode = request->getPublicMegaNode();
        }
        break;

    case MegaRequest::TYPE_IMPORT_LINK:
        h = request->getNodeHandle();
        break;

    case MegaRequest::TYPE_GET_ATTR_USER:
        if ( (lastError[apiIndex] == API_OK) && (request->getParamType() != MegaApi::USER_ATTR_AVATAR) )
        {
            attributeValue = request->getText();
        }

        if (request->getParamType() == MegaApi::USER_ATTR_AVATAR)
        {
            if (lastError[apiIndex] == API_OK)
            {
                attributeValue = "Avatar changed";
            }

            if (lastError[apiIndex] == API_ENOENT)
            {
                attributeValue = "Avatar not found";
            }
        }
        break;

#ifdef ENABLE_CHAT

    case MegaRequest::TYPE_CHAT_CREATE:
        if (lastError[apiIndex] == API_OK)
        {
            MegaTextChat *chat = request->getMegaTextChatList()->get(0)->copy();

            chatid = chat->getHandle();
            if (chats.find(chatid) != chats.end())
            {
                delete chats[chatid];
            }
            chats[chatid] = chat;
        }
        break;

    case MegaRequest::TYPE_CHAT_INVITE:
        if (lastError[apiIndex] == API_OK)
        {
            chatid = request->getNodeHandle();
            if (chats.find(chatid) != chats.end())
            {
                MegaTextChat *chat = chats[chatid];
                MegaHandle uh = request->getParentHandle();
                int priv = request->getAccess();
                userpriv_vector *privsbuf = new userpriv_vector;

                const MegaTextChatPeerList *privs = chat->getPeerList();
                if (privs)
                {
                    for (int i = 0; i < privs->size(); i++)
                    {
                        if (privs->getPeerHandle(i) != uh)
                        {
                            privsbuf->push_back(userpriv_pair(privs->getPeerHandle(i), (privilege_t) privs->getPeerPrivilege(i)));
                        }
                    }
                }
                privsbuf->push_back(userpriv_pair(uh, (privilege_t) priv));
                privs = new MegaTextChatPeerListPrivate(privsbuf);
                chat->setPeerList(privs);
            }
            else
            {
                LOG_err << "Trying to remove a peer from unknown chat";
            }
        }
        break;

    case MegaRequest::TYPE_CHAT_REMOVE:
        if (lastError[apiIndex] == API_OK)
        {
            chatid = request->getNodeHandle();
            if (chats.find(chatid) != chats.end())
            {
                MegaTextChat *chat = chats[chatid];
                MegaHandle uh = request->getParentHandle();
                userpriv_vector *privsbuf = new userpriv_vector;

                const MegaTextChatPeerList *privs = chat->getPeerList();
                if (privs)
                {
                    for (int i = 0; i < privs->size(); i++)
                    {
                        if (privs->getPeerHandle(i) != uh)
                        {
                            privsbuf->push_back(userpriv_pair(privs->getPeerHandle(i), (privilege_t) privs->getPeerPrivilege(i)));
                        }
                    }
                }
                privs = new MegaTextChatPeerListPrivate(privsbuf);
                chat->setPeerList(privs);
            }
            else
            {
                LOG_err << "Trying to remove a peer from unknown chat";
            }
        }
        break;

    case MegaRequest::TYPE_CHAT_URL:
        if (lastError[apiIndex] == API_OK)
        {
            link.assign(request->getLink());
        }
        break;
#endif

    case MegaRequest::TYPE_CREATE_ACCOUNT:
        if (lastError[apiIndex] == API_OK)
        {
            sid = request->getSessionKey();
        }
        break;

    case MegaRequest::TYPE_FETCH_NODES:
        if (apiIndex == 0)
        {
            megaApi[0]->enableTransferResumption();
        }
        break;

    }
}

void SdkTest::onTransferFinish(MegaApi* api, MegaTransfer *transfer, MegaError* e)
{
    unsigned int apiIndex;
    if (api == megaApi[0])
    {
        apiIndex = 0;
    }
    else if (api == megaApi[1])
    {
        apiIndex = 1;
    }
    else
    {
        LOG_err << "Instance of MegaApi not recognized";
        return;
    }

    transferFlags[apiIndex][transfer->getType()] = true;
    lastError[apiIndex] = e->getErrorCode();

    if (lastError[apiIndex] == MegaError::API_OK)
        h = transfer->getNodeHandle();
}


void SdkTest::onAccountUpdate(MegaApi* api)
{
    unsigned int apiIndex;
    if (api == megaApi[0])
    {
        apiIndex = 0;
    }
    else if (api == megaApi[1])
    {
        apiIndex = 1;
    }
    else
    {
        LOG_err << "Instance of MegaApi not recognized";
        return;
    }

    accountUpdated[apiIndex] = true;
}

void SdkTest::onUsersUpdate(MegaApi* api, MegaUserList *users)
{
    unsigned int apiIndex;
    if (api == megaApi[0])
    {
        apiIndex = 0;
    }
    else if (api == megaApi[1])
    {
        apiIndex = 1;
    }
    else
    {
        LOG_err << "Instance of MegaApi not recognized";
        return;
    }

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
            userUpdated[apiIndex] = true;
        }
    }
}

void SdkTest::onNodesUpdate(MegaApi* api, MegaNodeList *nodes)
{
    unsigned int apiIndex;
    if (api == megaApi[0])
    {
        apiIndex = 0;
    }
    else if (api == megaApi[1])
    {
        apiIndex = 1;
    }
    else
    {
        LOG_err << "Instance of MegaApi not recognized";
        return;
    }

    nodeUpdated[apiIndex] = true;
}

void SdkTest::onContactRequestsUpdate(MegaApi* api, MegaContactRequestList* requests)
{
    unsigned int apiIndex;
    if (api == megaApi[0])
    {
        apiIndex = 0;
    }
    else if (api == megaApi[1])
    {
        apiIndex = 1;
    }
    else
    {
        LOG_err << "Instance of MegaApi not recognized";
        return;
    }

    contactRequestUpdated[apiIndex] = true;
}

#ifdef ENABLE_CHAT
void SdkTest::onChatsUpdate(MegaApi *api, MegaTextChatList *chats)
{
    unsigned int apiIndex;
    if (api == megaApi[0])
    {
        apiIndex = 0;

        MegaTextChatList *list = NULL;
        if (chats)
        {
            list = chats->copy();
        }
        else
        {
            list = megaApi[0]->getChatList();
        }
        for (int i = 0; i < list->size(); i++)
        {
            handle chatid = list->get(i)->getHandle();
            if (this->chats.find(chatid) != this->chats.end())
            {
                delete this->chats[chatid];
                this->chats[chatid] = list->get(i)->copy();
            }
            else
            {
                this->chats[chatid] = list->get(i)->copy();
            }
        }
        delete list;
    }
    else if (api == megaApi[1])
    {
        apiIndex = 1;
    }
    else
    {
        LOG_err << "Instance of MegaApi not recognized";
        return;
    }

    chatUpdated[apiIndex] = true;
}

void SdkTest::createChat(bool group, MegaTextChatPeerList *peers, int timeout)
{
    requestFlags[0][MegaRequest::TYPE_CHAT_CREATE] = false;
    megaApi[0]->createChat(group, peers);
    waitForResponse(&requestFlags[0][MegaRequest::TYPE_CHAT_CREATE], timeout);
    if (timeout)
    {
        ASSERT_TRUE(requestFlags[0][MegaRequest::TYPE_CHAT_CREATE]) << "Chat creation not finished after " << timeout  << " seconds";
    }

    ASSERT_EQ(MegaError::API_OK, lastError[0]) << "Chat creation failed (error: " << lastError[0] << ")";
}

#endif

void SdkTest::login(unsigned int apiIndex, int timeout)
{
    requestFlags[apiIndex][MegaRequest::TYPE_LOGIN] = false;
    megaApi[apiIndex]->login(email[apiIndex].data(), pwd[apiIndex].data());

    ASSERT_TRUE(waitForResponse(&requestFlags[apiIndex][MegaRequest::TYPE_LOGIN], timeout))
        << "Logging failed after " << timeout << " seconds";
    ASSERT_EQ(MegaError::API_OK, lastError[apiIndex]) << "Logging failed (error: " << lastError[apiIndex] << ")";
    ASSERT_TRUE(megaApi[apiIndex]->isLoggedIn()) << "Not logged it";
}


void SdkTest::fetchnodes(unsigned int apiIndex, int timeout)
{
    requestFlags[apiIndex][MegaRequest::TYPE_FETCH_NODES] = false;
    megaApi[apiIndex]->fetchNodes();

    ASSERT_TRUE( waitForResponse(&requestFlags[apiIndex][MegaRequest::TYPE_FETCH_NODES], timeout) )
            << "Fetchnodes failed after " << timeout  << " seconds";
    ASSERT_EQ(MegaError::API_OK, lastError[apiIndex]) << "Fetchnodes failed (error: " << lastError[apiIndex] << ")";
}

void SdkTest::logout(unsigned int apiIndex, int timeout)
{
    requestFlags[apiIndex][MegaRequest::TYPE_LOGOUT] = false;
    megaApi[apiIndex]->logout(this);

    EXPECT_TRUE( waitForResponse(&requestFlags[apiIndex][MegaRequest::TYPE_LOGOUT], timeout) )
            << "Logout failed after " << timeout  << " seconds";

    // if the connection was closed before the response of the request was received, the result is ESID
    if (lastError[apiIndex] == MegaError::API_ESID) lastError[apiIndex] = MegaError::API_OK;

    EXPECT_EQ(MegaError::API_OK, lastError[apiIndex]) << "Logout failed (error: " << lastError[apiIndex] << ")";
}

char* SdkTest::dumpSession()
{
    return megaApi[0]->dumpSession();
}

void SdkTest::locallogout(int timeout)
{
    requestFlags[0][MegaRequest::TYPE_LOGOUT] = false;
    megaApi[0]->localLogout(this);

    EXPECT_TRUE( waitForResponse(&requestFlags[0][MegaRequest::TYPE_LOGOUT], timeout) )
            << "Local logout failed after " << timeout  << " seconds";
    ASSERT_EQ(MegaError::API_OK, lastError[0]) << "Local logout failed (error: " << lastError[0] << ")";
}

void SdkTest::resumeSession(const char *session, int timeout)
{
    requestFlags[0][MegaRequest::TYPE_LOGIN] = false;
    megaApi[0]->fastLogin(session, this);

    ASSERT_TRUE( waitForResponse(&requestFlags[0][MegaRequest::TYPE_LOGIN], timeout) )
            << "Resume session failed after " << timeout  << " seconds";
    ASSERT_EQ(MegaError::API_OK, lastError[0]) << "Resume session failed (error: " << lastError[0] << ")";
}

void SdkTest::purgeTree(MegaNode *p)
{
    MegaNodeList *children;
    children = megaApi[0]->getChildren(p);

    for (int i = 0; i < children->size(); i++)
    {
        MegaNode *n = children->get(i);
        if (n->isFolder())
            purgeTree(n);


        requestFlags[0][MegaRequest::TYPE_REMOVE] = false;

        megaApi[0]->remove(n);

        ASSERT_TRUE( waitForResponse(&requestFlags[0][MegaRequest::TYPE_REMOVE]) )
                << "Remove node operation failed after " << maxTimeout  << " seconds";
        ASSERT_EQ(MegaError::API_OK, lastError[0]) << "Remove node operation failed (error: " << lastError[0] << ")";
    }
}

bool SdkTest::waitForResponse(bool *responseReceived, unsigned int timeout)
{
    timeout *= 1000000; // convert to micro-seconds
    unsigned int tWaited = 0;    // microseconds
    bool connRetried = false;
    while(!(*responseReceived))
    {
        WaitMillisec(pollingT / 1000);

        if (timeout)
        {
            tWaited += pollingT;
            if (tWaited >= timeout)
            {
                return false;   // timeout is expired
            }
            // if no response after 2 minutes...
            else if (!connRetried && tWaited > (pollingT * 240))
            {
                megaApi[0]->retryPendingConnections(true);
                if (megaApi[1] && megaApi[1]->isLoggedIn())
                {
                    megaApi[1]->retryPendingConnections(true);
                }
                connRetried = true;
            }
        }
    }

    return true;    // response is received
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
    if (megaApi[1] == NULL)
    {
        char *buf;
        buf = getenv("MEGA_EMAIL_AUX");
        if (buf)
            email[1].assign(buf);
        ASSERT_LT((size_t) 0, email[1].length()) << "Set auxiliar username at the environment variable $MEGA_EMAIL_AUX";

        buf = getenv("MEGA_PWD_AUX");
        if (buf)
            pwd[1].assign(buf);
        ASSERT_LT((size_t) 0, pwd[1].length()) << "Set the auxiliar password at the environment variable $MEGA_PWD_AUX";

        megaApi[1] = new MegaApi(APP_KEY.c_str(), megaApiCacheFolder(1).c_str(), USER_AGENT.c_str());

        megaApi[1]->setLoggingName("1");
        megaApi[1]->setLogLevel(MegaApi::LOG_LEVEL_DEBUG);
        megaApi[1]->addListener(this);

        ASSERT_NO_FATAL_FAILURE( login(1) );
        ASSERT_NO_FATAL_FAILURE( fetchnodes(1) );
    }
}

void SdkTest::releaseMegaApi(unsigned int apiIndex)
{
    if (megaApi[apiIndex])
    {
        if (megaApi[apiIndex]->isLoggedIn())
        {
            ASSERT_NO_FATAL_FAILURE( logout(apiIndex) );
        }

        delete megaApi[apiIndex];
        megaApi[apiIndex] = NULL;
    }
}

void SdkTest::inviteContact(string email, string message, int action, int timeout)
{
    requestFlags[0][MegaRequest::TYPE_INVITE_CONTACT] = false;
    megaApi[0]->inviteContact(email.data(), message.data(), action);

    ASSERT_TRUE( waitForResponse(&requestFlags[0][MegaRequest::TYPE_INVITE_CONTACT], timeout) )
            << "Contact invitation not finished after " << timeout  << " seconds";
    ASSERT_EQ(MegaError::API_OK, lastError[0]) << "Contact invitation failed (error: " << lastError[0] << ")";
}

void SdkTest::replyContact(MegaContactRequest *cr, int action, int timeout)
{
    requestFlags[1][MegaRequest::TYPE_REPLY_CONTACT_REQUEST] = false;
    megaApi[1]->replyContactRequest(cr, action);

    ASSERT_TRUE( waitForResponse(&requestFlags[1][MegaRequest::TYPE_REPLY_CONTACT_REQUEST], timeout) )
            << "Contact reply not finished after " << timeout  << " seconds";
    ASSERT_EQ(MegaError::API_OK, lastError[1]) << "Contact reply failed (error: " << lastError[1] << ")";
}

void SdkTest::removeContact(string email, int timeout)
{
    MegaUser *u = megaApi[0]->getContact(email.data());
    bool null_pointer = (u == NULL);
    ASSERT_FALSE(null_pointer) << "Cannot find the specified contact (" << email << ")";

    if (u->getVisibility() != MegaUser::VISIBILITY_VISIBLE)
    {
        userUpdated[0] = true;  // nothing to do
        delete u;
        return;
    }

    requestFlags[0][MegaRequest::TYPE_REMOVE_CONTACT] = false;
    megaApi[0]->removeContact(u);

    ASSERT_TRUE( waitForResponse(&requestFlags[0][MegaRequest::TYPE_REMOVE_CONTACT], timeout) )
            << "Contact deletion not finished after " << timeout  << " seconds";
    ASSERT_EQ(MegaError::API_OK, lastError[0]) << "Contact deletion failed (error: " << lastError[0] << ")";

    delete u;
}

void SdkTest::shareFolder(MegaNode *n, const char *email, int action, int timeout)
{
    requestFlags[0][MegaRequest::TYPE_SHARE] = false;
    megaApi[0]->share(n, email, action);

    ASSERT_TRUE( waitForResponse(&requestFlags[0][MegaRequest::TYPE_SHARE], timeout) )
            << "Folder sharing not finished after " << timeout  << " seconds";
    ASSERT_EQ(MegaError::API_OK, lastError[0]) << "Folder sharing failed (error: " << lastError[0] << ")" << endl << "User: " << email << " Action: " << action;
}

void SdkTest::createPublicLink(MegaNode *n, m_time_t expireDate, int timeout)
{
    requestFlags[0][MegaRequest::TYPE_EXPORT] = false;
    megaApi[0]->exportNode(n, expireDate);

    ASSERT_TRUE( waitForResponse(&requestFlags[0][MegaRequest::TYPE_EXPORT], timeout) )
            << "Public link creation not finished after " << timeout  << " seconds";
    if (!expireDate)
    {
        ASSERT_EQ(MegaError::API_OK, lastError[0]) << "Public link creation failed (error: " << lastError[0] << ")";
    }
    else
    {
        bool res = MegaError::API_OK != lastError[0];
        ASSERT_TRUE(res) << "Public link creation with expire time on free account (" << email[0] << ") succeed, and it mustn't";
    }
}

void SdkTest::importPublicLink(string link, MegaNode *parent, int timeout)
{
    requestFlags[0][MegaRequest::TYPE_IMPORT_LINK] = false;
    megaApi[0]->importFileLink(link.data(), parent);

    ASSERT_TRUE(waitForResponse(&requestFlags[0][MegaRequest::TYPE_IMPORT_LINK], timeout) )
            << "Public link import not finished after " << timeout  << " seconds";
    ASSERT_EQ(MegaError::API_OK, lastError[0]) << "Public link import failed (error: " << lastError[0] << ")";
}

void SdkTest::getPublicNode(string link, int timeout)
{
    requestFlags[1][MegaRequest::TYPE_GET_PUBLIC_NODE] = false;
    megaApi[1]->getPublicNode(link.data());

    ASSERT_TRUE(waitForResponse(&requestFlags[1][MegaRequest::TYPE_GET_PUBLIC_NODE], timeout) )
            << "Public link retrieval not finished after " << timeout  << " seconds";
    ASSERT_EQ(MegaError::API_OK, lastError[1]) << "Public link retrieval failed (error: " << lastError[1] << ")";
}

void SdkTest::removePublicLink(MegaNode *n, int timeout)
{
    requestFlags[0][MegaRequest::TYPE_EXPORT] = false;
    megaApi[0]->disableExport(n);

    ASSERT_TRUE( waitForResponse(&requestFlags[0][MegaRequest::TYPE_EXPORT], timeout) )
            << "Public link removal not finished after " << timeout  << " seconds";
    ASSERT_EQ(MegaError::API_OK, lastError[0]) << "Public link removal failed (error: " << lastError[0] << ")";
}

void SdkTest::getContactRequest(unsigned int apiIndex, bool outgoing, int expectedSize)
{
    MegaContactRequestList *crl;

    if (outgoing)
    {
        crl = megaApi[apiIndex]->getOutgoingContactRequests();
        ASSERT_EQ(expectedSize, crl->size()) << "Too many outgoing contact requests in main account";
        if (expectedSize)
            cr[apiIndex] = crl->get(0)->copy();
    }
    else
    {
        crl = megaApi[apiIndex]->getIncomingContactRequests();
        ASSERT_EQ(expectedSize, crl->size()) << "Too many incoming contact requests in auxiliar account";
        if (expectedSize)
            cr[apiIndex] = crl->get(0)->copy();
    }

    delete crl;
}

void SdkTest::createFolder(unsigned int apiIndex, char *name, MegaNode *n, int timeout)
{
    requestFlags[apiIndex][MegaRequest::TYPE_CREATE_FOLDER] = false;
    megaApi[apiIndex]->createFolder(name, n);

    ASSERT_TRUE( waitForResponse(&requestFlags[apiIndex][MegaRequest::TYPE_CREATE_FOLDER], timeout) )
            << "Folder creation failed after " << timeout  << " seconds";
    ASSERT_EQ(MegaError::API_OK, lastError[apiIndex]) << "Cannot create a folder (error: " << lastError[apiIndex] << ")";
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

    bool errorLevel = ((loglevel == logError) && !testingInvalidArgs);
    ASSERT_FALSE(errorLevel) << "Test aborted due to an SDK error: " << message << " (" << source << ")";

#ifdef _WIN32
    std::ostringstream s;
    s << "[" << time << "] " << SimpleLogger::toStr((LogLevel)loglevel) << ": ";
    s << message << " (" << source << ")" << endl;
    OutputDebugStringA(s.str().c_str());
#endif
}

void SdkTest::setUserAttribute(int type, string value, int timeout)
{
    requestFlags[0][MegaRequest::TYPE_SET_ATTR_USER] = false;

    if (type == MegaApi::USER_ATTR_AVATAR)
    {
        megaApi[0]->setAvatar(value.empty() ? NULL : value.c_str());
    }
    else
    {
        megaApi[0]->setUserAttribute(type, value.c_str());
    }

    ASSERT_TRUE( waitForResponse(&requestFlags[0][MegaRequest::TYPE_SET_ATTR_USER], timeout) )
            << "User attribute setup not finished after " << timeout  << " seconds";
    ASSERT_EQ(MegaError::API_OK, lastError[0]) << "User attribute setup failed (error: " << lastError[0] << ")";
}

void SdkTest::getUserAttribute(MegaUser *u, int type, int timeout, int accountIndex)
{
    requestFlags[accountIndex][MegaRequest::TYPE_GET_ATTR_USER] = false;

    if (type == MegaApi::USER_ATTR_AVATAR)
    {
        megaApi[accountIndex]->getUserAvatar(u, AVATARDST.data());
    }
    else
    {
        megaApi[accountIndex]->getUserAttribute(u, type);
    }

    ASSERT_TRUE( waitForResponse(&requestFlags[accountIndex][MegaRequest::TYPE_GET_ATTR_USER], timeout) )
            << "User attribute retrieval not finished after " << timeout  << " seconds";

    bool result = (lastError[accountIndex] == MegaError::API_OK) || (lastError[accountIndex] == MegaError::API_ENOENT);
    ASSERT_TRUE(result) << "User attribute retrieval failed (error: " << lastError[accountIndex] << ")";
}

///////////////////////////__ Tests using SdkTest __//////////////////////////////////

/**
 * @brief TEST_F SdkTestCreateAccount
 *
 * It tests the creation of a new account for a random user.
 *  - Create account and send confirmation link
 *  - Logout and resume the create-account process
 *  - Send the confirmation link to a different email address
 *  - Wait for confirmation of account by a different client
 */
TEST_F(SdkTest, DISABLED_SdkTestCreateAccount)
{
    string email1 = "user@domain.com";
    string pwd = "pwd";
    string email2 = "other-user@domain.com";

    megaApi[0]->log(MegaApi::LOG_LEVEL_INFO, "___TEST Create account___");

    // Create an ephemeral session internally and send a confirmation link to email
    requestFlags[0][MegaRequest::TYPE_CREATE_ACCOUNT] = false;
    megaApi[0]->createAccount(email1.c_str(), pwd.c_str(), "MyFirstname", "MyLastname");
    ASSERT_TRUE( waitForResponse(&requestFlags[0][MegaRequest::TYPE_CREATE_ACCOUNT]) )
            << "Account creation has failed after " << maxTimeout << " seconds";
    ASSERT_EQ(MegaError::API_OK, lastError[0]) << "Account creation failed (error: " << lastError[0] << ")";

    // Logout from ephemeral session and resume session
    ASSERT_NO_FATAL_FAILURE( locallogout() );
    requestFlags[0][MegaRequest::TYPE_CREATE_ACCOUNT] = false;
    megaApi[0]->resumeCreateAccount(sid.c_str());
    ASSERT_TRUE( waitForResponse(&requestFlags[0][MegaRequest::TYPE_CREATE_ACCOUNT]) )
            << "Account creation has failed after " << maxTimeout << " seconds";
    ASSERT_EQ(MegaError::API_OK, lastError[0]) << "Account creation failed (error: " << lastError[0] << ")";

    // Send the confirmation link to a different email address
    requestFlags[0][MegaRequest::TYPE_SEND_SIGNUP_LINK] = false;
    megaApi[0]->sendSignupLink(email2.c_str(), "MyFirstname", pwd.c_str());
    ASSERT_TRUE( waitForResponse(&requestFlags[0][MegaRequest::TYPE_SEND_SIGNUP_LINK]) )
            << "Send confirmation link to another email failed after " << maxTimeout << " seconds";
    ASSERT_EQ(MegaError::API_OK, lastError[0]) << "Send confirmation link to another email address failed (error: " << lastError[0] << ")";

    // Now, confirm the account by using a different client...

    // ...and wait for the AP notifying the confirmation
    bool *flag = &accountUpdated[0]; *flag = false;
    ASSERT_TRUE( waitForResponse(flag) )
            << "Account confirmation not received after " << maxTimeout << " seconds";
}

/**
 * @brief TEST_F SdkTestNodeAttributes
 *
 *
 */
TEST_F(SdkTest, SdkTestNodeAttributes)
{
    megaApi[0]->log(MegaApi::LOG_LEVEL_INFO, "___TEST Node attributes___");

    MegaNode *rootnode = megaApi[0]->getRootNode();

    string filename1 = UPFILE;
    createFile(filename1, false);
    transferFlags[0][MegaTransfer::TYPE_UPLOAD] = false;
    megaApi[0]->startUpload(filename1.data(), rootnode);
    waitForResponse(&transferFlags[0][MegaTransfer::TYPE_UPLOAD]);

    ASSERT_EQ(MegaError::API_OK, lastError[0]) << "Cannot upload a test file (error: " << lastError[0] << ")";

    MegaNode *n1 = megaApi[0]->getNodeByHandle(h);
    bool null_pointer = (n1 == NULL);
    ASSERT_FALSE(null_pointer) << "Cannot initialize test scenario (error: " << lastError[0] << ")";


    // ___ Set invalid duration of a node ___

    testingInvalidArgs = true;

    requestFlags[0][MegaRequest::TYPE_SET_ATTR_NODE] = false;
    megaApi[0]->setNodeDuration(n1, -14);
    waitForResponse(&requestFlags[0][MegaRequest::TYPE_SET_ATTR_NODE]);
    ASSERT_EQ(MegaError::API_EARGS, lastError[0]) << "Unexpected error setting invalid node duration (error: " << lastError[0] << ")";

    testingInvalidArgs = false;


    // ___ Set duration of a node ___

    requestFlags[0][MegaRequest::TYPE_SET_ATTR_NODE] = false;
    megaApi[0]->setNodeDuration(n1, 929734);
    waitForResponse(&requestFlags[0][MegaRequest::TYPE_SET_ATTR_NODE]);
    ASSERT_EQ(MegaError::API_OK, lastError[0]) << "Cannot set node duration (error: " << lastError[0] << ")";

    delete n1;
    n1 = megaApi[0]->getNodeByHandle(h);
    ASSERT_EQ(929734, n1->getDuration()) << "Duration value does not match";


    // ___ Reset duration of a node ___

    requestFlags[0][MegaRequest::TYPE_SET_ATTR_NODE] = false;
    megaApi[0]->setNodeDuration(n1, -1);
    waitForResponse(&requestFlags[0][MegaRequest::TYPE_SET_ATTR_NODE]);
    ASSERT_EQ(MegaError::API_OK, lastError[0]) << "Cannot reset node duration (error: " << lastError[0] << ")";

    delete n1;
    n1 = megaApi[0]->getNodeByHandle(h);
    ASSERT_EQ(-1, n1->getDuration()) << "Duration value does not match";


    // ___ Set invalid coordinates of a node (out of range) ___

    testingInvalidArgs = true;

    requestFlags[0][MegaRequest::TYPE_SET_ATTR_NODE] = false;
    megaApi[0]->setNodeCoordinates(n1, -1523421.8719987255814, +6349.54);
    waitForResponse(&requestFlags[0][MegaRequest::TYPE_SET_ATTR_NODE]);
    ASSERT_EQ(MegaError::API_EARGS, lastError[0]) << "Unexpected error setting invalid node coordinates (error: " << lastError[0] << ")";


    // ___ Set invalid coordinates of a node (out of range) ___

    requestFlags[0][MegaRequest::TYPE_SET_ATTR_NODE] = false;
    megaApi[0]->setNodeCoordinates(n1, -160.8719987255814, +49.54);    // latitude must be [-90, 90]
    waitForResponse(&requestFlags[0][MegaRequest::TYPE_SET_ATTR_NODE]);
    ASSERT_EQ(MegaError::API_EARGS, lastError[0]) << "Unexpected error setting invalid node coordinates (error: " << lastError[0] << ")";


    // ___ Set invalid coordinates of a node (out of range) ___

    requestFlags[0][MegaRequest::TYPE_SET_ATTR_NODE] = false;
    megaApi[0]->setNodeCoordinates(n1, MegaNode::INVALID_COORDINATE, +69.54);
    waitForResponse(&requestFlags[0][MegaRequest::TYPE_SET_ATTR_NODE]);
    ASSERT_EQ(MegaError::API_EARGS, lastError[0]) << "Unexpected error trying to reset only one coordinate (error: " << lastError[0] << ")";

    testingInvalidArgs = false;


    // ___ Set coordinates of a node ___

    double lat = -51.8719987255814;
    double lon = +179.54;

    requestFlags[0][MegaRequest::TYPE_SET_ATTR_NODE] = false;
    megaApi[0]->setNodeCoordinates(n1, lat, lon);
    waitForResponse(&requestFlags[0][MegaRequest::TYPE_SET_ATTR_NODE]);
    ASSERT_EQ(MegaError::API_OK, lastError[0]) << "Cannot set node coordinates (error: " << lastError[0] << ")";

    delete n1;
    n1 = megaApi[0]->getNodeByHandle(h);

    // do same conversions to lose the same precision
    int buf = int(((lat + 90) / 180) * 0xFFFFFF);
    double res = -90 + 180 * (double) buf / 0xFFFFFF;

    ASSERT_EQ(res, n1->getLatitude()) << "Latitude value does not match";

    buf = int((lon == 180) ? 0 : (lon + 180) / 360 * 0x01000000);
    res = -180 + 360 * (double) buf / 0x01000000;

    ASSERT_EQ(res, n1->getLongitude()) << "Longitude value does not match";


    // ___ Set coordinates of a node to origin (0,0) ___

    lon = 0;
    lat = 0;

    requestFlags[0][MegaRequest::TYPE_SET_ATTR_NODE] = false;
    megaApi[0]->setNodeCoordinates(n1, 0, 0);
    waitForResponse(&requestFlags[0][MegaRequest::TYPE_SET_ATTR_NODE]);
    ASSERT_EQ(MegaError::API_OK, lastError[0]) << "Cannot set node coordinates (error: " << lastError[0] << ")";

    delete n1;
    n1 = megaApi[0]->getNodeByHandle(h);

    // do same conversions to lose the same precision
    buf = int(((lat + 90) / 180) * 0xFFFFFF);
    res = -90 + 180 * (double) buf / 0xFFFFFF;

    ASSERT_EQ(res, n1->getLatitude()) << "Latitude value does not match";
    ASSERT_EQ(lon, n1->getLongitude()) << "Longitude value does not match";


    // ___ Set coordinates of a node to border values (90,180) ___

    lat = 90;
    lon = 180;

    requestFlags[0][MegaRequest::TYPE_SET_ATTR_NODE] = false;
    megaApi[0]->setNodeCoordinates(n1, lat, lon);
    waitForResponse(&requestFlags[0][MegaRequest::TYPE_SET_ATTR_NODE]);
    ASSERT_EQ(MegaError::API_OK, lastError[0]) << "Cannot set node coordinates (error: " << lastError[0] << ")";

    delete n1;
    n1 = megaApi[0]->getNodeByHandle(h);

    ASSERT_EQ(lat, n1->getLatitude()) << "Latitude value does not match";
    bool value_ok = ((n1->getLongitude() == lon) || (n1->getLongitude() == -lon));
    ASSERT_TRUE(value_ok) << "Longitude value does not match";


    // ___ Set coordinates of a node to border values (-90,-180) ___

    lat = -90;
    lon = -180;

    requestFlags[0][MegaRequest::TYPE_SET_ATTR_NODE] = false;
    megaApi[0]->setNodeCoordinates(n1, lat, lon);
    waitForResponse(&requestFlags[0][MegaRequest::TYPE_SET_ATTR_NODE]);
    ASSERT_EQ(MegaError::API_OK, lastError[0]) << "Cannot set node coordinates (error: " << lastError[0] << ")";

    delete n1;
    n1 = megaApi[0]->getNodeByHandle(h);

    ASSERT_EQ(lat, n1->getLatitude()) << "Latitude value does not match";
    value_ok = ((n1->getLongitude() == lon) || (n1->getLongitude() == -lon));
    ASSERT_TRUE(value_ok) << "Longitude value does not match";


    // ___ Reset coordinates of a node ___

    lat = lon = MegaNode::INVALID_COORDINATE;

    requestFlags[0][MegaRequest::TYPE_SET_ATTR_NODE] = false;
    megaApi[0]->setNodeCoordinates(n1, lat, lon);
    waitForResponse(&requestFlags[0][MegaRequest::TYPE_SET_ATTR_NODE]);

    delete n1;
    n1 = megaApi[0]->getNodeByHandle(h);
    ASSERT_EQ(lat, n1->getLatitude()) << "Latitude value does not match";
    ASSERT_EQ(lon, n1->getLongitude()) << "Longitude value does not match";
}

/**
 * @brief TEST_F SdkTestResumeSession
 *
 * It creates a local cache, logs out of the current session and tries to resume it later.
 */
TEST_F(SdkTest, SdkTestResumeSession)
{
    megaApi[0]->log(MegaApi::LOG_LEVEL_INFO, "___TEST Resume session___");

    char *session = dumpSession();

    ASSERT_NO_FATAL_FAILURE( locallogout() );
    ASSERT_NO_FATAL_FAILURE( resumeSession(session) );
    ASSERT_NO_FATAL_FAILURE( fetchnodes(0) );

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
    megaApi[0]->log(MegaApi::LOG_LEVEL_INFO, "___TEST Node operations___");

    // --- Create a new folder ---

    MegaNode *rootnode = megaApi[0]->getRootNode();
    char name1[64] = "New folder";

    ASSERT_NO_FATAL_FAILURE( createFolder(0, name1, rootnode) );


    // --- Rename a node ---

    MegaNode *n1 = megaApi[0]->getNodeByHandle(h);
    strcpy(name1, "Folder renamed");

    requestFlags[0][MegaRequest::TYPE_RENAME] = false;
    megaApi[0]->renameNode(n1, name1);
    ASSERT_TRUE( waitForResponse(&requestFlags[0][MegaRequest::TYPE_RENAME]) )
            << "Rename operation failed after " << maxTimeout << " seconds";
    ASSERT_EQ(MegaError::API_OK, lastError[0]) << "Cannot rename a node (error: " << lastError[0] << ")";


    // --- Copy a node ---

    MegaNode *n2;
    char name2[64] = "Folder copy";

    requestFlags[0][MegaRequest::TYPE_COPY] = false;
    megaApi[0]->copyNode(n1, rootnode, name2);
    ASSERT_TRUE( waitForResponse(&requestFlags[0][MegaRequest::TYPE_COPY]) )
            << "Copy operation failed after " << maxTimeout << " seconds";
    ASSERT_EQ(MegaError::API_OK, lastError[0]) << "Cannot create a copy of a node (error: " << lastError[0] << ")";
    n2 = megaApi[0]->getNodeByHandle(h);


    // --- Get child nodes ---

    MegaNodeList *children;
    children = megaApi[0]->getChildren(rootnode);

    EXPECT_EQ(megaApi[0]->getNumChildren(rootnode), children->size()) << "Wrong number of child nodes";
    ASSERT_LE(2, children->size()) << "Wrong number of children nodes found";
    EXPECT_STREQ(name2, children->get(0)->getName()) << "Wrong name of child node"; // "Folder copy"
    EXPECT_STREQ(name1, children->get(1)->getName()) << "Wrong name of child node"; // "Folder rename"

    delete children;


    // --- Get child node by name ---

    MegaNode *n3;
    n3 = megaApi[0]->getChildNode(rootnode, name2);

    bool null_pointer = (n3 == NULL);
    EXPECT_FALSE(null_pointer) << "Child node by name not found";
//    ASSERT_EQ(n2->getHandle(), n3->getHandle());  This test may fail due to multiple nodes with the same name


    // --- Get node by path ---

    char path[128] = "/Folder copy";
    MegaNode *n4;
    n4 = megaApi[0]->getNodeByPath(path);

    null_pointer = (n4 == NULL);
    EXPECT_FALSE(null_pointer) << "Node by path not found";


    // --- Search for a node ---
    MegaNodeList *nlist;
    nlist = megaApi[0]->search(rootnode, "copy");

    ASSERT_EQ(1, nlist->size());
    EXPECT_EQ(n4->getHandle(), nlist->get(0)->getHandle()) << "Search node by pattern failed";

    delete nlist;


    // --- Move a node ---

    requestFlags[0][MegaRequest::TYPE_MOVE] = false;
    megaApi[0]->moveNode(n1, n2);
    ASSERT_TRUE( waitForResponse(&requestFlags[0][MegaRequest::TYPE_MOVE]) )
            << "Move operation failed after " << maxTimeout << " seconds";
    ASSERT_EQ(MegaError::API_OK, lastError[0]) << "Cannot move node (error: " << lastError[0] << ")";


    // --- Get parent node ---

    MegaNode *n5;
    n5 = megaApi[0]->getParentNode(n1);

    ASSERT_EQ(n2->getHandle(), n5->getHandle()) << "Wrong parent node";


    // --- Send to Rubbish bin ---

    requestFlags[0][MegaRequest::TYPE_MOVE] = false;
    megaApi[0]->moveNode(n2, megaApi[0]->getRubbishNode());
    ASSERT_TRUE( waitForResponse(&requestFlags[0][MegaRequest::TYPE_MOVE]) )
            << "Move operation failed after " << maxTimeout << " seconds";
    ASSERT_EQ(MegaError::API_OK, lastError[0]) << "Cannot move node to Rubbish bin (error: " << lastError[0] << ")";


    // --- Remove a node ---

    requestFlags[0][MegaRequest::TYPE_REMOVE] = false;
    megaApi[0]->remove(n2);
    ASSERT_TRUE( waitForResponse(&requestFlags[0][MegaRequest::TYPE_REMOVE]) )
            << "Remove operation failed after " << maxTimeout << " seconds";
    ASSERT_EQ(MegaError::API_OK, lastError[0]) << "Cannot remove a node (error: " << lastError[0] << ")";

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
    megaApi[0]->log(MegaApi::LOG_LEVEL_INFO, "___TEST Transfers___");

    
    megaApi[0]->log(MegaApi::LOG_LEVEL_INFO, cwd());

    MegaNode *rootnode = megaApi[0]->getRootNode();
    string filename1 = UPFILE;
    createFile(filename1);


    // --- Cancel a transfer ---

    requestFlags[0][MegaRequest::TYPE_CANCEL_TRANSFERS] = false;
    megaApi[0]->startUpload(filename1.data(), rootnode);
    megaApi[0]->cancelTransfers(MegaTransfer::TYPE_UPLOAD);
    ASSERT_TRUE( waitForResponse(&requestFlags[0][MegaRequest::TYPE_CANCEL_TRANSFERS]) )
            << "Cancellation of transfers failed after " << maxTimeout << " seconds";
    EXPECT_EQ(MegaError::API_OK, lastError[0]) << "Transfer cancellation failed (error: " << lastError[0] << ")";


    // --- Upload a file (part 1) ---

    transferFlags[0][MegaTransfer::TYPE_UPLOAD] = false;
    megaApi[0]->startUpload(filename1.data(), rootnode);
    // do not wait yet for completion


    // --- Pause a transfer ---

    requestFlags[0][MegaRequest::TYPE_PAUSE_TRANSFERS] = false;
    megaApi[0]->pauseTransfers(true, MegaTransfer::TYPE_UPLOAD);
    ASSERT_TRUE( waitForResponse(&requestFlags[0][MegaRequest::TYPE_PAUSE_TRANSFERS]) )
            << "Pause of transfers failed after " << maxTimeout << " seconds";
    EXPECT_EQ(MegaError::API_OK, lastError[0]) << "Cannot pause transfer (error: " << lastError[0] << ")";
    EXPECT_TRUE(megaApi[0]->areTransfersPaused(MegaTransfer::TYPE_UPLOAD)) << "Upload transfer not paused";


    // --- Resume a transfer ---

    requestFlags[0][MegaRequest::TYPE_PAUSE_TRANSFERS] = false;
    megaApi[0]->pauseTransfers(false, MegaTransfer::TYPE_UPLOAD);
    ASSERT_TRUE( waitForResponse(&requestFlags[0][MegaRequest::TYPE_PAUSE_TRANSFERS]) )
            << "Resumption of transfers after pause has failed after " << maxTimeout << " seconds";
    EXPECT_EQ(MegaError::API_OK, lastError[0]) << "Cannot resume transfer (error: " << lastError[0] << ")";
    EXPECT_FALSE(megaApi[0]->areTransfersPaused(MegaTransfer::TYPE_UPLOAD)) << "Upload transfer not resumed";


    // --- Upload a file (part 2) ---


    ASSERT_TRUE( waitForResponse(&transferFlags[0][MegaTransfer::TYPE_UPLOAD], 600) )
            << "Upload transfer failed after " << 600 << " seconds";
    ASSERT_EQ(MegaError::API_OK, lastError[0]) << "Cannot upload file (error: " << lastError[0] << ")";

    MegaNode *n1 = megaApi[0]->getNodeByHandle(h);
    bool null_pointer = (n1 == NULL);

    ASSERT_FALSE(null_pointer) << "Cannot upload file (error: " << lastError[0] << ")";
    ASSERT_STREQ(filename1.data(), n1->getName()) << "Uploaded file with wrong name (error: " << lastError[0] << ")";


    // --- Get node by fingerprint (needs to be a file, not a folder) ---

    char *fingerprint = megaApi[0]->getFingerprint(n1);
    MegaNode *n2 = megaApi[0]->getNodeByFingerprint(fingerprint);

    null_pointer = (n2 == NULL);
    EXPECT_FALSE(null_pointer) << "Node by fingerprint not found";
//    ASSERT_EQ(n2->getHandle(), n4->getHandle());  This test may fail due to multiple nodes with the same name

    delete fingerprint;


    // --- Get the size of a file ---

    size_t filesize = getFilesize(filename1);
    long long nodesize = megaApi[0]->getSize(n2);
    EXPECT_EQ(filesize, nodesize) << "Wrong size of uploaded file";


    // --- Download a file ---

    string filename2 = "./" + DOWNFILE;

    transferFlags[0][MegaTransfer::TYPE_DOWNLOAD] = false;
    megaApi[0]->startDownload(n2, filename2.c_str());
    ASSERT_TRUE( waitForResponse(&transferFlags[0][MegaTransfer::TYPE_DOWNLOAD], 600) )
            << "Download transfer failed after " << maxTimeout << " seconds";
    ASSERT_EQ(MegaError::API_OK, lastError[0]) << "Cannot download the file (error: " << lastError[0] << ")";

    MegaNode *n3 = megaApi[0]->getNodeByHandle(h);
    null_pointer = (n3 == NULL);

    ASSERT_FALSE(null_pointer) << "Cannot download node";
    ASSERT_EQ(n2->getHandle(), n3->getHandle()) << "Cannot download node (error: " << lastError[0] << ")";


    // --- Upload a 0-bytes file ---

    string filename3 = EMPTYFILE;
    FILE *fp = fopen(filename3.c_str(), "w");
    fclose(fp);

    transferFlags[0][MegaTransfer::TYPE_UPLOAD] = false;
    megaApi[0]->startUpload(filename3.c_str(), rootnode);

    ASSERT_TRUE( waitForResponse(&transferFlags[0][MegaTransfer::TYPE_UPLOAD], 600) )
            << "Upload 0-byte file failed after " << 600 << " seconds";
    ASSERT_EQ(MegaError::API_OK, lastError[0]) << "Cannot upload file (error: " << lastError[0] << ")";

    MegaNode *n4 = megaApi[0]->getNodeByHandle(h);
    null_pointer = (n4 == NULL);

    ASSERT_FALSE(null_pointer) << "Cannot upload file (error: " << lastError[0] << ")";
    ASSERT_STREQ(filename3.data(), n4->getName()) << "Uploaded file with wrong name (error: " << lastError[0] << ")";


    // --- Download a 0-byte file ---

    filename3 = "./" + EMPTYFILE;
    transferFlags[0][MegaTransfer::TYPE_DOWNLOAD] = false;
    megaApi[0]->startDownload(n4, filename3.c_str());
    ASSERT_TRUE( waitForResponse(&transferFlags[0][MegaTransfer::TYPE_DOWNLOAD], 600) )
            << "Download 0-byte file failed after " << maxTimeout << " seconds";
    ASSERT_EQ(MegaError::API_OK, lastError[0]) << "Cannot download the file (error: " << lastError[0] << ")";

    MegaNode *n5 = megaApi[0]->getNodeByHandle(h);
    null_pointer = (n5 == NULL);

    ASSERT_FALSE(null_pointer) << "Cannot download node";
    ASSERT_EQ(n4->getHandle(), n5->getHandle()) << "Cannot download node (error: " << lastError[0] << ")";


    delete rootnode;
    delete n1;
    delete n2;
    delete n3;
    delete n4;
    delete n5;
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
 * = Set master key as exported
 * = Get preferred language
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
    megaApi[0]->log(MegaApi::LOG_LEVEL_INFO, "___TEST Contacts___");

    ASSERT_NO_FATAL_FAILURE( getMegaApiAux() );    // login + fetchnodes


    // --- Check my email and the email of the contact ---

    EXPECT_STREQ(email[0].data(), megaApi[0]->getMyEmail());
    EXPECT_STREQ(email[1].data(), megaApi[1]->getMyEmail());


    // --- Send a new contact request ---

    string message = "Hi contact. This is a testing message";

    contactRequestUpdated[0] = contactRequestUpdated[1] = false;
    ASSERT_NO_FATAL_FAILURE( inviteContact(email[1], message, MegaContactRequest::INVITE_ACTION_ADD) );
    // if there were too many invitations within a short period of time, the invitation can be rejected by
    // the API with `API_EOVERQUOTA = -17` as counter spamming meassure (+500 invites in the last 50 days)


    // --- Check the sent contact request ---

    ASSERT_TRUE( waitForResponse(&contactRequestUpdated[0]) )   // at the source side (main account)
            << "Contact request update not received after " << maxTimeout << " seconds";

    ASSERT_NO_FATAL_FAILURE( getContactRequest(0, true) );

    ASSERT_STREQ(message.data(), cr[0]->getSourceMessage()) << "Message sent is corrupted";
    ASSERT_STREQ(email[0].data(), cr[0]->getSourceEmail()) << "Wrong source email";
    ASSERT_STREQ(email[1].data(), cr[0]->getTargetEmail()) << "Wrong target email";
    ASSERT_EQ(MegaContactRequest::STATUS_UNRESOLVED, cr[0]->getStatus()) << "Wrong contact request status";
    ASSERT_TRUE(cr[0]->isOutgoing()) << "Wrong direction of the contact request";

    delete cr[0];      cr[0] = NULL;


    // --- Check received contact request ---

    ASSERT_TRUE( waitForResponse(&contactRequestUpdated[1]) )   // at the target side (auxiliar account)
            << "Contact request update not received after " << maxTimeout << " seconds";

    ASSERT_NO_FATAL_FAILURE( getContactRequest(1, false) );

    // There isn't message when a user invites the same user too many times, to avoid spamming
    if (cr[1]->getSourceMessage())
    {
        ASSERT_STREQ(message.data(), cr[1]->getSourceMessage()) << "Message received is corrupted";
    }
    ASSERT_STREQ(email[0].data(), cr[1]->getSourceEmail()) << "Wrong source email";
    ASSERT_STREQ(NULL, cr[1]->getTargetEmail()) << "Wrong target email";    // NULL according to MegaApi documentation
    ASSERT_EQ(MegaContactRequest::STATUS_UNRESOLVED, cr[1]->getStatus()) << "Wrong contact request status";
    ASSERT_FALSE(cr[1]->isOutgoing()) << "Wrong direction of the contact request";

    delete cr[1];   cr[1] = NULL;


    // --- Ignore received contact request ---

    ASSERT_NO_FATAL_FAILURE( getContactRequest(1, false) );

    contactRequestUpdated[1] = false;
    ASSERT_NO_FATAL_FAILURE( replyContact(cr[1], MegaContactRequest::REPLY_ACTION_IGNORE) );
    ASSERT_TRUE( waitForResponse(&contactRequestUpdated[1]) )   // at the target side (auxiliar account)
            << "Contact request update not received after " << maxTimeout << " seconds";

    // Ignoring a PCR does not generate actionpackets for the account sending the invitation

    delete cr[1];   cr[1] = NULL;

    ASSERT_NO_FATAL_FAILURE( getContactRequest(1, false, 0) );
    delete cr[1];   cr[1] = NULL;


    // --- Cancel the invitation ---

    message = "I don't wanna be your contact anymore";

    contactRequestUpdated[0] = false;
    ASSERT_NO_FATAL_FAILURE( inviteContact(email[1], message, MegaContactRequest::INVITE_ACTION_DELETE) );
    ASSERT_TRUE( waitForResponse(&contactRequestUpdated[0]) )   // at the target side (auxiliar account), where the deletion is checked
            << "Contact request update not received after " << maxTimeout << " seconds";

    ASSERT_NO_FATAL_FAILURE( getContactRequest(0, true, 0) );
    delete cr[0];      cr[0] = NULL;
    

    // --- Remind a contact invitation (cannot until 2 weeks after invitation/last reminder) ---

//    contactRequestUpdated[1] = false;
//    megaApi->inviteContact(email[1].data(), message.data(), MegaContactRequest::INVITE_ACTION_REMIND);
//    waitForResponse(&contactRequestUpdated[1], 0);    // only at auxiliar account, where the deletion is checked

//    ASSERT_TRUE(contactRequestUpdated[1]) << "Contact invitation reminder not received after " << timeout  << " seconds";


    // --- Invite a new contact (again) ---

    contactRequestUpdated[1] = false;
    ASSERT_NO_FATAL_FAILURE( inviteContact(email[1], message, MegaContactRequest::INVITE_ACTION_ADD) );
    ASSERT_TRUE( waitForResponse(&contactRequestUpdated[1]) )   // at the target side (auxiliar account)
            << "Contact request creation not received after " << maxTimeout << " seconds";


    // --- Deny a contact invitation ---

    ASSERT_NO_FATAL_FAILURE( getContactRequest(1, false) );

    contactRequestUpdated[0] = contactRequestUpdated[1] = false;
    ASSERT_NO_FATAL_FAILURE( replyContact(cr[1], MegaContactRequest::REPLY_ACTION_DENY) );
    ASSERT_TRUE( waitForResponse(&contactRequestUpdated[1]) )   // at the target side (auxiliar account)
            << "Contact request creation not received after " << maxTimeout << " seconds";
    ASSERT_TRUE( waitForResponse(&contactRequestUpdated[0]) )   // at the source side (main account)
            << "Contact request creation not received after " << maxTimeout << " seconds";

    delete cr[1];   cr[1] = NULL;

    ASSERT_NO_FATAL_FAILURE( getContactRequest(0, true, 0) );
    delete cr[0];   cr[0] = NULL;

    ASSERT_NO_FATAL_FAILURE( getContactRequest(1, false, 0) );
    delete cr[1];   cr[1] = NULL;


    // --- Invite a new contact (again) ---

    contactRequestUpdated[1] = false;
    ASSERT_NO_FATAL_FAILURE( inviteContact(email[1], message, MegaContactRequest::INVITE_ACTION_ADD) );
    ASSERT_TRUE( waitForResponse(&contactRequestUpdated[1]) )   // at the target side (auxiliar account)
            << "Contact request creation not received after " << maxTimeout << " seconds";


    // --- Accept a contact invitation ---

    ASSERT_NO_FATAL_FAILURE( getContactRequest(1, false) );

    contactRequestUpdated[0] = contactRequestUpdated[1] = false;
    ASSERT_NO_FATAL_FAILURE( replyContact(cr[1], MegaContactRequest::REPLY_ACTION_ACCEPT) );
    ASSERT_TRUE( waitForResponse(&contactRequestUpdated[0]) )   // at the target side (main account)
            << "Contact request creation not received after " << maxTimeout << " seconds";
    ASSERT_TRUE( waitForResponse(&contactRequestUpdated[1]) )   // at the target side (auxiliar account)
            << "Contact request creation not received after " << maxTimeout << " seconds";

    delete cr[1];   cr[1] = NULL;

    ASSERT_NO_FATAL_FAILURE( getContactRequest(0, true, 0) );
    delete cr[0];   cr[0] = NULL;

    ASSERT_NO_FATAL_FAILURE( getContactRequest(1, false, 0) );
    delete cr[1];   cr[1] = NULL;


    // --- Modify firstname ---

    string firstname = "My firstname";

    userUpdated[1] = false;
    ASSERT_NO_FATAL_FAILURE( setUserAttribute(MegaApi::USER_ATTR_FIRSTNAME, firstname));
    ASSERT_TRUE( waitForResponse(&userUpdated[1]) )   // at the target side (auxiliar account)
            << "User attribute update not received after " << maxTimeout << " seconds";


    // --- Check firstname of a contact

    MegaUser *u = megaApi[0]->getMyUser();

    bool null_pointer = (u == NULL);
    ASSERT_FALSE(null_pointer) << "Cannot find the MegaUser for email: " << email[0];

    ASSERT_NO_FATAL_FAILURE( getUserAttribute(u, MegaApi::USER_ATTR_FIRSTNAME));
    ASSERT_EQ( firstname, attributeValue) << "Firstname is wrong";

    delete u;


    // --- Set master key already as exported

    u = megaApi[0]->getMyUser();

    requestFlags[0][MegaRequest::TYPE_SET_ATTR_USER] = false;
    megaApi[0]->masterKeyExported();
    ASSERT_TRUE( waitForResponse(&requestFlags[0][MegaRequest::TYPE_SET_ATTR_USER]) );

    ASSERT_NO_FATAL_FAILURE( getUserAttribute(u, MegaApi::USER_ATTR_PWD_REMINDER, maxTimeout, 0));
    string pwdReminder = attributeValue;
    size_t offset = pwdReminder.find(':');
    offset += pwdReminder.find(':', offset+1);
    ASSERT_EQ( pwdReminder.at(offset), '1' ) << "Password reminder attribute not updated";

    delete u;


    // --- Get language preference

    u = megaApi[0]->getMyUser();

    string langCode = "es";
    ASSERT_NO_FATAL_FAILURE( setUserAttribute(MegaApi::USER_ATTR_LANGUAGE, langCode));
    ASSERT_NO_FATAL_FAILURE( getUserAttribute(u, MegaApi::USER_ATTR_LANGUAGE, maxTimeout, 0));
    string language = attributeValue;
    ASSERT_TRUE(!strcmp(langCode.c_str(), language.c_str())) << "Language code is wrong";

    delete u;


    // --- Load avatar ---

    ASSERT_TRUE(fileexists(AVATARSRC)) <<  "File " +AVATARSRC+ " is needed in folder " << cwd();

    userUpdated[1] = false;
    ASSERT_NO_FATAL_FAILURE( setUserAttribute(MegaApi::USER_ATTR_AVATAR, AVATARSRC));
    ASSERT_TRUE( waitForResponse(&userUpdated[1]) )   // at the target side (auxiliar account)
            << "User attribute update not received after " << maxTimeout << " seconds";


    // --- Get avatar of a contact ---

    u = megaApi[0]->getMyUser();

    null_pointer = (u == NULL);
    ASSERT_FALSE(null_pointer) << "Cannot find the MegaUser for email: " << email[0];

    attributeValue = "";

    ASSERT_NO_FATAL_FAILURE( getUserAttribute(u, MegaApi::USER_ATTR_AVATAR));
    ASSERT_STREQ( "Avatar changed", attributeValue.data()) << "Failed to change avatar";

    size_t filesizeSrc = getFilesize(AVATARSRC);
    size_t filesizeDst = getFilesize(AVATARDST);
    ASSERT_EQ(filesizeDst, filesizeSrc) << "Received avatar differs from uploaded avatar";

    delete u;


    // --- Delete avatar ---

    userUpdated[1] = false;
    ASSERT_NO_FATAL_FAILURE( setUserAttribute(MegaApi::USER_ATTR_AVATAR, ""));
    ASSERT_TRUE( waitForResponse(&userUpdated[1]) )   // at the target side (auxiliar account)
            << "User attribute update not received after " << maxTimeout << " seconds";


    // --- Get non-existing avatar of a contact ---

    u = megaApi[0]->getMyUser();

    null_pointer = (u == NULL);
    ASSERT_FALSE(null_pointer) << "Cannot find the MegaUser for email: " << email[0];

    attributeValue = "";

    ASSERT_NO_FATAL_FAILURE( getUserAttribute(u, MegaApi::USER_ATTR_AVATAR));
    ASSERT_STREQ("Avatar not found", attributeValue.data()) << "Failed to remove avatar";

    delete u;


    // --- Delete an existing contact ---

    userUpdated[0] = false;
    ASSERT_NO_FATAL_FAILURE( removeContact(email[1]) );
    ASSERT_TRUE( waitForResponse(&userUpdated[0]) )   // at the target side (main account)
            << "User attribute update not received after " << maxTimeout << " seconds";

    u = megaApi[0]->getContact(email[1].data());
    null_pointer = (u == NULL);

    ASSERT_FALSE(null_pointer) << "Cannot find the MegaUser for email: " << email[1];
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
 * - Create a file public link
 * - Import a file public link
 * - Get a node from a file public link
 * - Remove a public link
 * - Create a folder public link
 */

bool SdkTest::checkAlert(int apiIndex, const string& title, const string& path)
{
    bool ok = false;
    for (int i = 0; !ok && i < 10; ++i)
    {

        MegaUserAlertList* list = megaApi[apiIndex]->getUserAlerts();
        if (list->size() > 0)
        {
            MegaUserAlert* a = list->get(list->size() - 1);
            ok = title == a->getTitle() && path == a->getPath() && !ISUNDEF(a->getNodeHandle());

            if (!ok && i == 9)
            {
                EXPECT_STREQ(title.c_str(), a->getTitle());
                EXPECT_STREQ(path.c_str(), a->getPath());
                EXPECT_NE(a->getNodeHandle(), UNDEF);
            }
        }
        delete list;

        if (!ok)
        {
            LOG_info << "Waiting some more for the alert";
            WaitMillisec(USERALERT_ARRIVAL_MILLISEC);
        }
    }
    return ok;
}

bool SdkTest::checkAlert(int apiIndex, const string& title, handle h, int n)
{
    bool ok = false;
    for (int i = 0; !ok && i < 10; ++i)
    {

        MegaUserAlertList* list = megaApi[apiIndex]->getUserAlerts();
        if (list->size() > 0)
        {
            MegaUserAlert* a = list->get(list->size() - 1);
            ok = title == a->getTitle() && a->getNodeHandle() == h && a->getNumber(0) == n;

            if (!ok && i == 9)
            {
                EXPECT_STREQ(a->getTitle(), title.c_str());
                EXPECT_EQ(a->getNodeHandle(), h);
                EXPECT_EQ(a->getNumber(0), n); // 0 for number of folders
            }
        }
        delete list;

        if (!ok)
        {
            LOG_info << "Waiting some more for the alert";
            WaitMillisec(USERALERT_ARRIVAL_MILLISEC);
        }
    }
    return ok;
}


TEST_F(SdkTest, SdkTestShares)
{
    megaApi[0]->log(MegaApi::LOG_LEVEL_INFO, "___TEST Shares___");

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

    MegaNode *rootnode = megaApi[0]->getRootNode();
    char foldername1[64] = "Shared-folder";
    MegaHandle hfolder1;

    ASSERT_NO_FATAL_FAILURE( createFolder(0, foldername1, rootnode) );

    hfolder1 = h;     // 'h' is set in 'onRequestFinish()'
    n1 = megaApi[0]->getNodeByHandle(hfolder1);

    char foldername2[64] = "subfolder";
    MegaHandle hfolder2;

    ASSERT_NO_FATAL_FAILURE( createFolder(0, foldername2, megaApi[0]->getNodeByHandle(hfolder1)) );

    hfolder2 = h;

    MegaHandle hfile1;
    createFile(PUBLICFILE.data(), false);   // not a large file since don't need to test transfers here

    transferFlags[0][MegaTransfer::TYPE_UPLOAD] = false;
    megaApi[0]->startUpload(PUBLICFILE.data(), megaApi[0]->getNodeByHandle(hfolder1));
    waitForResponse(&transferFlags[0][MegaTransfer::TYPE_UPLOAD], 0);   // wait forever

    ASSERT_EQ(MegaError::API_OK, lastError[0]) << "Cannot upload file (error: " << lastError[0] << ")";
    hfile1 = h;


    // --- Download authorized node from another account ---

    MegaNode *nNoAuth = megaApi[0]->getNodeByHandle(hfile1);

    transferFlags[1][MegaTransfer::TYPE_DOWNLOAD] = false;
    megaApi[1]->startDownload(nNoAuth, "unauthorized_node");
    ASSERT_TRUE( waitForResponse(&transferFlags[1][MegaTransfer::TYPE_DOWNLOAD], 600) )
            << "Download transfer not finished after " << maxTimeout << " seconds";

    bool hasFailed = (lastError[1] != API_OK);
    ASSERT_TRUE(hasFailed) << "Download of node without authorization successful! (it should fail)";

    MegaNode *nAuth = megaApi[0]->authorizeNode(nNoAuth);

    transferFlags[1][MegaTransfer::TYPE_DOWNLOAD] = false;
    megaApi[1]->startDownload(nAuth, "authorized_node");
    ASSERT_TRUE( waitForResponse(&transferFlags[1][MegaTransfer::TYPE_DOWNLOAD], 600) )
            << "Download transfer not finished after " << maxTimeout << " seconds";
    ASSERT_EQ(MegaError::API_OK, lastError[1]) << "Cannot download authorized node (error: " << lastError[1] << ")";

    delete nNoAuth;
    delete nAuth;

    // Initialize a test scenario: create a new contact to share to

    string message = "Hi contact. Let's share some stuff";

    contactRequestUpdated[1] = false;
    ASSERT_NO_FATAL_FAILURE( inviteContact(email[1], message, MegaContactRequest::INVITE_ACTION_ADD) );
    ASSERT_TRUE( waitForResponse(&contactRequestUpdated[1]) )   // at the target side (auxiliar account)
            << "Contact request creation not received after " << maxTimeout << " seconds";


    ASSERT_NO_FATAL_FAILURE( getContactRequest(1, false) );

    contactRequestUpdated[0] = contactRequestUpdated[1] = false;
    ASSERT_NO_FATAL_FAILURE( replyContact(cr[1], MegaContactRequest::REPLY_ACTION_ACCEPT) );
    ASSERT_TRUE( waitForResponse(&contactRequestUpdated[1]) )   // at the target side (auxiliar account)
            << "Contact request creation not received after " << maxTimeout << " seconds";
    ASSERT_TRUE( waitForResponse(&contactRequestUpdated[0]) )   // at the source side (main account)
            << "Contact request creation not received after " << maxTimeout << " seconds";

    delete cr[1];   cr[1] = NULL;


    // --- Create a new outgoing share ---

    nodeUpdated[0] = nodeUpdated[1] = false;
    ASSERT_NO_FATAL_FAILURE( shareFolder(n1, email[1].data(), MegaShare::ACCESS_READ) );
    ASSERT_TRUE( waitForResponse(&nodeUpdated[0]) )   // at the target side (main account)
            << "Node update not received after " << maxTimeout << " seconds";
    ASSERT_TRUE( waitForResponse(&nodeUpdated[1]) )   // at the target side (auxiliar account)
            << "Node update not received after " << maxTimeout << " seconds";


    // --- Check the outgoing share ---

    sl = megaApi[0]->getOutShares();
    ASSERT_EQ(1, sl->size()) << "Outgoing share failed";
    s = sl->get(0);

    n1 = megaApi[0]->getNodeByHandle(hfolder1);    // get an updated version of the node

    ASSERT_EQ(MegaShare::ACCESS_READ, s->getAccess()) << "Wrong access level of outgoing share";
    ASSERT_EQ(hfolder1, s->getNodeHandle()) << "Wrong node handle of outgoing share";
    ASSERT_STREQ(email[1].data(), s->getUser()) << "Wrong email address of outgoing share";
    ASSERT_TRUE(n1->isShared()) << "Wrong sharing information at outgoing share";
    ASSERT_TRUE(n1->isOutShare()) << "Wrong sharing information at outgoing share";

    delete sl;


    // --- Check the incoming share ---

    sl = megaApi[1]->getInSharesList();
    ASSERT_EQ(1, sl->size()) << "Incoming share not received in auxiliar account";

    nl = megaApi[1]->getInShares(megaApi[1]->getContact(email[0].data()));
    ASSERT_EQ(1, nl->size()) << "Incoming share not received in auxiliar account";
    n = nl->get(0);

    ASSERT_EQ(hfolder1, n->getHandle()) << "Wrong node handle of incoming share";
    ASSERT_STREQ(foldername1, n->getName()) << "Wrong folder name of incoming share";
    ASSERT_EQ(MegaError::API_OK, megaApi[1]->checkAccess(n, MegaShare::ACCESS_READ).getErrorCode()) << "Wrong access level of incoming share";
    ASSERT_TRUE(n->isInShare()) << "Wrong sharing information at incoming share";
    ASSERT_TRUE(n->isShared()) << "Wrong sharing information at incoming share";

    delete nl;

    // check the corresponding user alert
    ASSERT_TRUE(checkAlert(1, "New shared folder from " + email[0], email[0] + ":Shared-folder"));

    // add a folder under the share
    char foldernameA[64] = "dummyname1";
    char foldernameB[64] = "dummyname2";
    ASSERT_NO_FATAL_FAILURE(createFolder(0, foldernameA, megaApi[0]->getNodeByHandle(hfolder2)));
    ASSERT_NO_FATAL_FAILURE(createFolder(0, foldernameB, megaApi[0]->getNodeByHandle(hfolder2)));

    // check the corresponding user alert
    ASSERT_TRUE(checkAlert(1, email[0] + " added 2 folders", megaApi[0]->getNodeByHandle(hfolder2)->getHandle(), 2));

    // --- Modify the access level of an outgoing share ---

    nodeUpdated[0] = nodeUpdated[1] = false;
    ASSERT_NO_FATAL_FAILURE( shareFolder(megaApi[0]->getNodeByHandle(hfolder1), email[1].data(), MegaShare::ACCESS_READWRITE) );
    ASSERT_TRUE( waitForResponse(&nodeUpdated[0]) )   // at the target side (main account)
            << "Node update not received after " << maxTimeout << " seconds";
    ASSERT_TRUE( waitForResponse(&nodeUpdated[1]) )   // at the target side (auxiliar account)
            << "Node update not received after " << maxTimeout << " seconds";

    nl = megaApi[1]->getInShares(megaApi[1]->getContact(email[0].data()));
    ASSERT_EQ(1, nl->size()) << "Incoming share not received in auxiliar account";
    n = nl->get(0);

    ASSERT_EQ(MegaError::API_OK, megaApi[1]->checkAccess(n, MegaShare::ACCESS_READWRITE).getErrorCode()) << "Wrong access level of incoming share";

    delete nl;


    // --- Revoke access to an outgoing share ---

    nodeUpdated[0] = nodeUpdated[1] = false;
    ASSERT_NO_FATAL_FAILURE( shareFolder(n1, email[1].data(), MegaShare::ACCESS_UNKNOWN) );
    ASSERT_TRUE( waitForResponse(&nodeUpdated[0]) )   // at the target side (main account)
            << "Node update not received after " << maxTimeout << " seconds";
    ASSERT_TRUE( waitForResponse(&nodeUpdated[1]) )   // at the target side (auxiliar account)
            << "Node update not received after " << maxTimeout << " seconds";

    sl = megaApi[0]->getOutShares();
    ASSERT_EQ(0, sl->size()) << "Outgoing share revocation failed";
    delete sl;

    nl = megaApi[1]->getInShares(megaApi[1]->getContact(email[0].data()));
    ASSERT_EQ(0, nl->size()) << "Incoming share revocation failed";
    delete nl;

    // check the corresponding user alert
    {
        MegaUserAlertList* list = megaApi[1]->getUserAlerts();
        ASSERT_TRUE(list->size() > 0);
        MegaUserAlert* a = list->get(list->size() - 1);
        ASSERT_STREQ(a->getTitle(), ("Access to folders shared by " + email[0] + " was removed").c_str());
        ASSERT_STREQ(a->getPath(), (email[0] + ":Shared-folder").c_str());
        ASSERT_NE(a->getNodeHandle(), UNDEF);
        delete list;
    }

    // --- Get pending outgoing shares ---

    char emailfake[64];
    srand(unsigned(time(NULL)));
    sprintf(emailfake, "%d@nonexistingdomain.com", rand()%1000000);
    // carefull, antispam rejects too many tries without response for the same address

    n = megaApi[0]->getNodeByHandle(hfolder2);

    contactRequestUpdated[0] = false;
    nodeUpdated[0] = false;
    ASSERT_NO_FATAL_FAILURE( shareFolder(n, emailfake, MegaShare::ACCESS_FULL) );
    ASSERT_TRUE( waitForResponse(&nodeUpdated[0]) )   // at the target side (main account)
            << "Node update not received after " << maxTimeout << " seconds";
    ASSERT_TRUE( waitForResponse(&contactRequestUpdated[0]) )   // at the target side (main account)
            << "Contact request update not received after " << maxTimeout << " seconds";

    sl = megaApi[0]->getPendingOutShares(n);   delete n;
    ASSERT_EQ(1, sl->size()) << "Pending outgoing share failed";
    s = sl->get(0);
    n = megaApi[0]->getNodeByHandle(s->getNodeHandle());

//    ASSERT_STREQ(emailfake, s->getUser()) << "Wrong email address of outgoing share"; User is not created yet
    ASSERT_FALSE(n->isShared()) << "Node is already shared, must be pending";
    ASSERT_FALSE(n->isOutShare()) << "Node is already shared, must be pending";
    ASSERT_FALSE(n->isInShare()) << "Node is already shared, must be pending";

    delete sl;
    delete n;


    // --- Create a file public link ---

    MegaNode *nfile1 = megaApi[0]->getNodeByHandle(hfile1);

    ASSERT_NO_FATAL_FAILURE( createPublicLink(nfile1) );
    // The created link is stored in this->link at onRequestFinish()

    // Get a fresh snapshot of the node and check it's actually exported
    nfile1 = megaApi[0]->getNodeByHandle(hfile1);
    ASSERT_TRUE(nfile1->isExported()) << "Node is not exported, must be exported";
    ASSERT_FALSE(nfile1->isTakenDown()) << "Public link is taken down, it mustn't";

    // Regenerate the same link should not trigger a new request
    string oldLink = link;
    link = "";
    nfile1 = megaApi[0]->getNodeByHandle(hfile1);
    ASSERT_NO_FATAL_FAILURE( createPublicLink(nfile1) );
    ASSERT_STREQ(oldLink.c_str(), link.c_str()) << "Wrong public link after link update";


    // Try to update the expiration time of an existing link (only for PRO accounts are allowed, otherwise -11
    ASSERT_NO_FATAL_FAILURE( createPublicLink(nfile1, 1577836800) );     // Wed, 01 Jan 2020 00:00:00 GMT
    nfile1 = megaApi[0]->getNodeByHandle(hfile1);
    ASSERT_EQ(0, nfile1->getExpirationTime()) << "Expiration time successfully set, when it shouldn't";
    ASSERT_FALSE(nfile1->isExpired()) << "Public link is expired, it mustn't";


    // --- Import a file public link ---

    ASSERT_NO_FATAL_FAILURE( importPublicLink(link, rootnode) );

    MegaNode *nimported = megaApi[0]->getNodeByHandle(h);

    ASSERT_STREQ(nfile1->getName(), nimported->getName()) << "Imported file with wrong name";
    ASSERT_EQ(rootnode->getHandle(), nimported->getParentHandle()) << "Imported file in wrong path";


    // --- Get node from file public link ---

    ASSERT_NO_FATAL_FAILURE( getPublicNode(link) );

    ASSERT_TRUE(publicNode->isPublic()) << "Cannot get a node from public link";


    // --- Remove a public link ---

    ASSERT_NO_FATAL_FAILURE( removePublicLink(nfile1) );

    delete nfile1;
    nfile1 = megaApi[0]->getNodeByHandle(h);
    ASSERT_FALSE(nfile1->isPublic()) << "Public link removal failed (still public)";

    delete nimported;


    // --- Create a folder public link ---

    MegaNode *nfolder1 = megaApi[0]->getNodeByHandle(hfolder1);

    ASSERT_NO_FATAL_FAILURE( createPublicLink(nfolder1) );
    // The created link is stored in this->link at onRequestFinish()

    delete nfolder1;

    // Get a fresh snapshot of the node and check it's actually exported
    nfolder1 = megaApi[0]->getNodeByHandle(hfolder1);
    ASSERT_TRUE(nfolder1->isExported()) << "Node is not exported, must be exported";
    ASSERT_FALSE(nfolder1->isTakenDown()) << "Public link is taken down, it mustn't";

    delete nfolder1;

    oldLink = link;
    link = "";
    nfolder1 = megaApi[0]->getNodeByHandle(hfolder1);
    ASSERT_STREQ(oldLink.c_str(), nfolder1->getPublicLink()) << "Wrong public link from MegaNode";

    // Regenerate the same link should not trigger a new request
    ASSERT_NO_FATAL_FAILURE( createPublicLink(nfolder1) );
    ASSERT_STREQ(oldLink.c_str(), link.c_str()) << "Wrong public link after link update";

    delete nfolder1;

}

/**
* @brief TEST_F SdkTestConsoleAutocomplete
*
* Run various tests confirming the console autocomplete will work as expected
*
*/
#ifdef WIN32

bool cmp(const autocomplete::CompletionState& c, std::vector<std::string>& s)
{
    bool result = true;
    if (c.completions.size() != s.size())
    {
        result = false;
    }
    else
    {
        std::sort(s.begin(), s.end());
        for (size_t i = c.completions.size(); i--; )
        {
            if (c.completions[i].s != s[i])
            {
                result = false;
                break;
            }
        }
    }
    if (!result)
    {
        for (size_t i = 0; i < c.completions.size() || i < s.size(); ++i)
        {
            cout << (i < s.size() ? s[i] : "") << "/" << (i < c.completions.size() ? c.completions[i].s : "") << endl;
        }
    }
    return result;
}

TEST_F(SdkTest, SdkTestConsoleAutocomplete)
{
    using namespace autocomplete;

    {
        std::unique_ptr<Either> p(new Either);
        p->Add(sequence(text("cd")));
        p->Add(sequence(text("lcd")));
        p->Add(sequence(text("ls"), opt(flag("-R"))));
        p->Add(sequence(text("lls"), opt(flag("-R")), param("folder")));
        ACN syntax(std::move(p));

        {
            auto r = autoComplete("", 0, syntax, false);
            std::vector<std::string> e{ "cd", "lcd", "ls", "lls" };
            ASSERT_TRUE(cmp(r, e));
        }

        {
            auto r = autoComplete("l", 1, syntax, false);
            std::vector<std::string> e{ "lcd", "ls", "lls" };
            ASSERT_TRUE(cmp(r, e));
        }

        {
            auto r = autoComplete("ll", 2, syntax, false);
            std::vector<std::string> e{ "lls" };
            ASSERT_TRUE(cmp(r, e));
        }

        {
            auto r = autoComplete("lls", 3, syntax, false);
            std::vector<std::string> e{ "lls" };
            ASSERT_TRUE(cmp(r, e));
        }

        {
            auto r = autoComplete("lls ", 4, syntax, false);
            std::vector<std::string> e{ "<folder>" };
            ASSERT_TRUE(cmp(r, e));
        }

        {
            auto r = autoComplete("lls -", 5, syntax, false);
            std::vector<std::string> e{ "-R" };
            ASSERT_TRUE(cmp(r, e));
        }

        {
            auto r = autoComplete("x", 1, syntax, false);
            std::vector<std::string> e{};
            ASSERT_TRUE(cmp(r, e));
        }

        {
            auto r = autoComplete("x ", 2, syntax, false);
            std::vector<std::string> e{};
            ASSERT_TRUE(cmp(r, e));
        }
    }

    ::mega::handle megaCurDir = UNDEF;

    MegaApiImpl* impl = *((MegaApiImpl**)(((char*)megaApi[0]) + sizeof(*megaApi[0])) - 1); //megaApi[0]->pImpl;
    MegaClient* client = impl->getMegaClient();


    std::unique_ptr<Either> p(new Either);
    p->Add(sequence(text("cd")));
    p->Add(sequence(text("lcd")));
    p->Add(sequence(text("ls"), opt(flag("-R")), opt(ACN(new MegaFS(true, true, client, &megaCurDir, "")))));
    p->Add(sequence(text("lls"), opt(flag("-R")), opt(ACN(new LocalFS(true, true, "")))));
    ACN syntax(std::move(p));

    std::experimental::filesystem::create_directory("test_autocomplete_files");
    std::experimental::filesystem::current_path("test_autocomplete_files");

    std::experimental::filesystem::create_directory("dir1");
    std::experimental::filesystem::create_directory("dir1\\sub11");
    std::experimental::filesystem::create_directory("dir1\\sub12");
    std::experimental::filesystem::create_directory("dir2");
    std::experimental::filesystem::create_directory("dir2\\sub21");
    std::experimental::filesystem::create_directory("dir2\\sub22");
    std::experimental::filesystem::create_directory("dir2a");
    std::experimental::filesystem::create_directory("dir2a\\dir space");
    std::experimental::filesystem::create_directory("dir2a\\dir space\\next");
    std::experimental::filesystem::create_directory("dir2a\\dir space2");
    std::experimental::filesystem::create_directory("dir2a\\nospace");

    {
        auto r = autoComplete("ls -R", 5, syntax, false);
        std::vector<std::string> e{"-R"};
        ASSERT_TRUE(cmp(r, e));
    }

    // dos style file completion, local fs
    CompletionTextOut s;

    {
        auto r = autoComplete("lls ", 4, syntax, false);
        std::vector<std::string> e{ "dir1", "dir2", "dir2a" };
        ASSERT_TRUE(cmp(r, e));
        applyCompletion(r, true, 100, s);
        ASSERT_EQ(r.line, "lls dir1");
    }

    {
        auto r = autoComplete("lls di", 6, syntax, false);
        std::vector<std::string> e{ "dir1", "dir2", "dir2a" };
        ASSERT_TRUE(cmp(r, e));
    }

    {
        auto r = autoComplete("lls dir2", 8, syntax, false);
        std::vector<std::string> e{ "dir2", "dir2a" };
        ASSERT_TRUE(cmp(r, e));
    }

    {
        auto r = autoComplete("lls dir2a", 9, syntax, false);
        std::vector<std::string> e{ "dir2a" };
        ASSERT_TRUE(cmp(r, e));
    }

    {
        auto r = autoComplete("lls dir2 something after", 8, syntax, false);
        std::vector<std::string> e{ "dir2", "dir2a" };
        ASSERT_TRUE(cmp(r, e));
    }

    {
        auto r = autoComplete("lls dir2something immeditely after", 8, syntax, false);
        std::vector<std::string> e{ "dir2", "dir2a" };
        ASSERT_TRUE(cmp(r, e));
    }

    {
        auto r = autoComplete("lls dir2\\", 9, syntax, false);
        std::vector<std::string> e{ "dir2\\sub21", "dir2\\sub22" };
        ASSERT_TRUE(cmp(r, e));
    }

    {
        auto r = autoComplete("lls dir2\\.\\", 11, syntax, false);
        std::vector<std::string> e{ "dir2\\.\\sub21", "dir2\\.\\sub22" };
        ASSERT_TRUE(cmp(r, e));
    }

    {
        auto r = autoComplete("lls dir2\\..", 11, syntax, false);
        std::vector<std::string> e{ "dir2\\.." };
        ASSERT_TRUE(cmp(r, e));
    }

    {
        auto r = autoComplete("lls dir2\\..\\", 12, syntax, false);
        std::vector<std::string> e{ "dir2\\..\\dir1", "dir2\\..\\dir2", "dir2\\..\\dir2a" };
        ASSERT_TRUE(cmp(r, e));
        applyCompletion(r, true, 100, s);
        ASSERT_EQ(r.line, "lls dir2\\..\\dir1");
        applyCompletion(r, true, 100, s);
        ASSERT_EQ(r.line, "lls dir2\\..\\dir2");
        applyCompletion(r, true, 100, s);
        ASSERT_EQ(r.line, "lls dir2\\..\\dir2a");
        applyCompletion(r, true, 100, s);
        ASSERT_EQ(r.line, "lls dir2\\..\\dir1");
        applyCompletion(r, false, 100, s);
        ASSERT_EQ(r.line, "lls dir2\\..\\dir2a");
        applyCompletion(r, false, 100, s);
        ASSERT_EQ(r.line, "lls dir2\\..\\dir2");
    }

    {
        auto r = autoComplete("lls dir2a\\", 10, syntax, false);
        applyCompletion(r, false, 100, s);
        ASSERT_EQ(r.line, "lls dir2a\\nospace");
        applyCompletion(r, false, 100, s);
        ASSERT_EQ(r.line, "lls \"dir2a\\dir space2\"");
        applyCompletion(r, false, 100, s);
        ASSERT_EQ(r.line, "lls \"dir2a\\dir space\"");
        applyCompletion(r, false, 100, s);
        ASSERT_EQ(r.line, "lls dir2a\\nospace");
    }

    {
        auto r = autoComplete("lls \"dir\"1\\", 11, syntax, false);
        applyCompletion(r, true, 100, s);
        ASSERT_EQ(r.line, "lls \"dir1\\sub11\"");
    }

    {
        auto r = autoComplete("lls dir1\\\"..\\dir2\\\"", std::string::npos, syntax, false);
        applyCompletion(r, true, 100, s);
        ASSERT_EQ(r.line, "lls \"dir1\\..\\dir2\\sub21\"");
    }

    {
        auto r = autoComplete("lls c:\\prog", std::string::npos, syntax, false);
        applyCompletion(r, true, 100, s);
        ASSERT_EQ(r.line, "lls \"c:\\Program Files\"");
        applyCompletion(r, true, 100, s);
        ASSERT_EQ(r.line, "lls \"c:\\Program Files (x86)\"");
    }

    {
        auto r = autoComplete("lls \"c:\\program files \"", std::string::npos, syntax, false);
        applyCompletion(r, true, 100, s);
        ASSERT_EQ(r.line, "lls \"c:\\Program Files (x86)\"");
    }

    // unix style completions, local fs

    {
        auto r = autoComplete("lls ", 4, syntax, true);
        std::vector<std::string> e{ "dir1\\", "dir2\\", "dir2a\\" };
        ASSERT_TRUE(cmp(r, e));
        applyCompletion(r, true, 100, s);
        ASSERT_EQ(r.line, "lls dir");
    }

    {
        auto r = autoComplete("lls di", 6, syntax, true);
        std::vector<std::string> e{ "dir1\\", "dir2\\", "dir2a\\" };
        ASSERT_TRUE(cmp(r, e));
        applyCompletion(r, true, 100, s);
        ASSERT_EQ(r.line, "lls dir");
    }

    {
        auto r = autoComplete("lls dir2", 8, syntax, true);
        std::vector<std::string> e{ "dir2\\", "dir2a\\" };
        ASSERT_TRUE(cmp(r, e));
        applyCompletion(r, true, 100, s);
        ASSERT_EQ(r.line, "lls dir2");
    }

    {
        auto r = autoComplete("lls dir2a", 9, syntax, true);
        std::vector<std::string> e{ "dir2a\\" };
        ASSERT_TRUE(cmp(r, e));
        applyCompletion(r, true, 100, s);
        ASSERT_EQ(r.line, "lls dir2a\\");
    }

    {
        auto r = autoComplete("lls dir2 something after", 8, syntax, true);
        std::vector<std::string> e{ "dir2\\", "dir2a\\" };
        ASSERT_TRUE(cmp(r, e));
        applyCompletion(r, true, 100, s);
        ASSERT_EQ(r.line, "lls dir2 something after");
    }

    {
        auto r = autoComplete("lls dir2asomething immediately after", 9, syntax, true);
        std::vector<std::string> e{ "dir2a\\" };
        ASSERT_TRUE(cmp(r, e));
        applyCompletion(r, true, 100, s);
        ASSERT_EQ(r.line, "lls dir2a\\something immediately after");
    }

    {
        auto r = autoComplete("lls dir2\\", 9, syntax, true);
        std::vector<std::string> e{ "dir2\\sub21\\", "dir2\\sub22\\" };
        ASSERT_TRUE(cmp(r, e));
        applyCompletion(r, true, 100, s);
        ASSERT_EQ(r.line, "lls dir2\\sub2");
        auto rr = autoComplete("lls dir2\\sub22", 14, syntax, true);
        applyCompletion(rr, true, 100, s);
        ASSERT_EQ(rr.line, "lls dir2\\sub22\\");
    }

    {
        auto r = autoComplete("lls dir2\\.\\", 11, syntax, true);
        std::vector<std::string> e{ "dir2\\.\\sub21\\", "dir2\\.\\sub22\\" };
        ASSERT_TRUE(cmp(r, e));
        applyCompletion(r, true, 100, s);
        ASSERT_EQ(r.line, "lls dir2\\.\\sub2");
    }

    {
        auto r = autoComplete("lls dir2\\..", 11, syntax, true);
        std::vector<std::string> e{ "dir2\\..\\" };
        ASSERT_TRUE(cmp(r, e));
        applyCompletion(r, true, 100, s);
        ASSERT_EQ(r.line, "lls dir2\\..\\");
    }

    {
        auto r = autoComplete("lls dir2\\..\\", 12, syntax, true);
        std::vector<std::string> e{ "dir2\\..\\dir1\\", "dir2\\..\\dir2\\", "dir2\\..\\dir2a\\" };
        ASSERT_TRUE(cmp(r, e));
        applyCompletion(r, true, 100, s);
        ASSERT_EQ(r.line, "lls dir2\\..\\dir");
    }

    {
        auto r = autoComplete("lls dir2\\..\\", 12, syntax, true);
        std::vector<std::string> e{ "dir2\\..\\dir1\\", "dir2\\..\\dir2\\", "dir2\\..\\dir2a\\" };
        ASSERT_TRUE(cmp(r, e));
        applyCompletion(r, true, 100, s);
        ASSERT_EQ(r.line, "lls dir2\\..\\dir");
    }

    {
        auto r = autoComplete("lls dir2a\\d", 11, syntax, true);
        applyCompletion(r, true, 100, s);
        ASSERT_EQ(r.line, "lls \"dir2a\\dir space\"");
        auto rr = autoComplete("lls \"dir2a\\dir space\"\\", std::string::npos, syntax, false);
        applyCompletion(rr, true, 100, s);
        ASSERT_EQ(rr.line, "lls \"dir2a\\dir space\\next\"");
    }

    {
        auto r = autoComplete("lls \"dir\"1\\", std::string::npos, syntax, true);
        applyCompletion(r, true, 100, s);
        ASSERT_EQ(r.line, "lls \"dir1\\sub1\"");
    }

    {
        auto r = autoComplete("lls dir1\\\"..\\dir2\\\"", std::string::npos, syntax, true);
        applyCompletion(r, true, 100, s);
        ASSERT_EQ(r.line, "lls \"dir1\\..\\dir2\\sub2\"");
    }

    {
        auto r = autoComplete("lls c:\\prog", std::string::npos, syntax, true);
        applyCompletion(r, true, 100, s);
        ASSERT_EQ(r.line, "lls c:\\program");
    }

    {
        auto r = autoComplete("lls \"c:\\program files \"", std::string::npos, syntax, true);
        applyCompletion(r, true, 100, s);
        ASSERT_EQ(r.line, "lls \"c:\\program files (x86)\\\"");
    }

    {
        auto r = autoComplete("lls 'c:\\program files '", std::string::npos, syntax, true);
        applyCompletion(r, true, 100, s);
        ASSERT_EQ(r.line, "lls 'c:\\program files (x86)\\'");
    }

    // mega dir setup

    MegaNode *rootnode = megaApi[0]->getRootNode();
    ASSERT_NO_FATAL_FAILURE(createFolder(0, "test_autocomplete_megafs", rootnode));
    MegaNode *n0 = megaApi[0]->getNodeByHandle(h);

    megaCurDir = h;

    ASSERT_NO_FATAL_FAILURE(createFolder(0, "dir1", n0));
    MegaNode *n1 = megaApi[0]->getNodeByHandle(h);
    ASSERT_NO_FATAL_FAILURE(createFolder(0, "sub11", n1));
    ASSERT_NO_FATAL_FAILURE(createFolder(0, "sub12", n1));

    ASSERT_NO_FATAL_FAILURE(createFolder(0, "dir2", n0));
    MegaNode *n2 = megaApi[0]->getNodeByHandle(h);
    ASSERT_NO_FATAL_FAILURE(createFolder(0, "sub21", n2));
    ASSERT_NO_FATAL_FAILURE(createFolder(0, "sub22", n2));

    ASSERT_NO_FATAL_FAILURE(createFolder(0, "dir2a", n0));
    MegaNode *n3 = megaApi[0]->getNodeByHandle(h);
    ASSERT_NO_FATAL_FAILURE(createFolder(0, "dir space", n3));
    MegaNode *n31 = megaApi[0]->getNodeByHandle(h);
    ASSERT_NO_FATAL_FAILURE(createFolder(0, "dir space2", n3));
    ASSERT_NO_FATAL_FAILURE(createFolder(0, "nospace", n3));
    ASSERT_NO_FATAL_FAILURE(createFolder(0, "next", n31));


    // dos style mega FS completions

    {
        auto r = autoComplete("ls ", std::string::npos, syntax, false);
        std::vector<std::string> e{ "dir1", "dir2", "dir2a" };
        ASSERT_TRUE(cmp(r, e));
        applyCompletion(r, true, 100, s);
        ASSERT_EQ(r.line, "ls dir1");
    }

    {
        auto r = autoComplete("ls di", std::string::npos, syntax, false);
        std::vector<std::string> e{ "dir1", "dir2", "dir2a" };
        ASSERT_TRUE(cmp(r, e));
    }

    {
        auto r = autoComplete("ls dir2", std::string::npos, syntax, false);
        std::vector<std::string> e{ "dir2", "dir2a" };
        ASSERT_TRUE(cmp(r, e));
    }

    {
        auto r = autoComplete("ls dir2a", std::string::npos, syntax, false);
        std::vector<std::string> e{ "dir2a" };
        ASSERT_TRUE(cmp(r, e));
    }

    {
        auto r = autoComplete("ls dir2 something after", 7, syntax, false);
        std::vector<std::string> e{ "dir2", "dir2a" };
        ASSERT_TRUE(cmp(r, e));
    }

    {
        auto r = autoComplete("ls dir2something immeditely after", 7, syntax, false);
        std::vector<std::string> e{ "dir2", "dir2a" };
        ASSERT_TRUE(cmp(r, e));
    }

    {
        auto r = autoComplete("ls dir2/", std::string::npos, syntax, false);
        std::vector<std::string> e{ "dir2/sub21", "dir2/sub22" };
        ASSERT_TRUE(cmp(r, e));
    }

    {
        auto r = autoComplete("ls dir2/./", std::string::npos, syntax, false);
        std::vector<std::string> e{ "dir2/./sub21", "dir2/./sub22" };
        ASSERT_TRUE(cmp(r, e));
    }

    {
        auto r = autoComplete("ls dir2/..", std::string::npos, syntax, false);
        std::vector<std::string> e{ "dir2/.." };
        ASSERT_TRUE(cmp(r, e));
    }

    {
        auto r = autoComplete("ls dir2/../", std::string::npos, syntax, false);
        std::vector<std::string> e{ "dir2/../dir1", "dir2/../dir2", "dir2/../dir2a" };
        ASSERT_TRUE(cmp(r, e));
        applyCompletion(r, true, 100, s);
        ASSERT_EQ(r.line, "ls dir2/../dir1");
        applyCompletion(r, true, 100, s);
        ASSERT_EQ(r.line, "ls dir2/../dir2");
        applyCompletion(r, true, 100, s);
        ASSERT_EQ(r.line, "ls dir2/../dir2a");
        applyCompletion(r, true, 100, s);
        ASSERT_EQ(r.line, "ls dir2/../dir1");
        applyCompletion(r, false, 100, s);
        ASSERT_EQ(r.line, "ls dir2/../dir2a");
        applyCompletion(r, false, 100, s);
        ASSERT_EQ(r.line, "ls dir2/../dir2");
    }

    {
        auto r = autoComplete("ls dir2a/", std::string::npos, syntax, false);
        applyCompletion(r, false, 100, s);
        ASSERT_EQ(r.line, "ls dir2a/nospace");
        applyCompletion(r, false, 100, s);
        ASSERT_EQ(r.line, "ls \"dir2a/dir space2\"");
        applyCompletion(r, false, 100, s);
        ASSERT_EQ(r.line, "ls \"dir2a/dir space\"");
        applyCompletion(r, false, 100, s);
        ASSERT_EQ(r.line, "ls dir2a/nospace");
    }

    {
        auto r = autoComplete("ls \"dir\"1/", std::string::npos, syntax, false);
        applyCompletion(r, true, 100, s);
        ASSERT_EQ(r.line, "ls \"dir1/sub11\"");
    }

    {
        auto r = autoComplete("ls dir1/\"../dir2/\"", std::string::npos, syntax, false);
        applyCompletion(r, true, 100, s);
        ASSERT_EQ(r.line, "ls \"dir1/../dir2/sub21\"");
    }

    {
        auto r = autoComplete("ls /test_autocomplete_meg", std::string::npos, syntax, false);
        applyCompletion(r, true, 100, s);
        ASSERT_EQ(r.line, "ls /test_autocomplete_megafs");
    }

    // unix style mega FS completions

    {
        auto r = autoComplete("ls ", std::string::npos, syntax, true);
        std::vector<std::string> e{ "dir1/", "dir2/", "dir2a/" };
        ASSERT_TRUE(cmp(r, e));
        applyCompletion(r, true, 100, s);
        ASSERT_EQ(r.line, "ls dir");
    }

    {
        auto r = autoComplete("ls di", std::string::npos, syntax, true);
        std::vector<std::string> e{ "dir1/", "dir2/", "dir2a/" };
        ASSERT_TRUE(cmp(r, e));
        applyCompletion(r, true, 100, s);
        ASSERT_EQ(r.line, "ls dir");
    }

    {
        auto r = autoComplete("ls dir2", std::string::npos, syntax, true);
        std::vector<std::string> e{ "dir2/", "dir2a/" };
        ASSERT_TRUE(cmp(r, e));
        applyCompletion(r, true, 100, s);
        ASSERT_EQ(r.line, "ls dir2");
    }

    {
        auto r = autoComplete("ls dir2a", std::string::npos, syntax, true);
        std::vector<std::string> e{ "dir2a/" };
        ASSERT_TRUE(cmp(r, e));
        applyCompletion(r, true, 100, s);
        ASSERT_EQ(r.line, "ls dir2a/");
    }

    {
        auto r = autoComplete("ls dir2 something after", 7, syntax, true);
        std::vector<std::string> e{ "dir2/", "dir2a/" };
        ASSERT_TRUE(cmp(r, e));
        applyCompletion(r, true, 100, s);
        ASSERT_EQ(r.line, "ls dir2 something after");
    }

    {
        auto r = autoComplete("ls dir2asomething immediately after", 8, syntax, true);
        std::vector<std::string> e{ "dir2a/" };
        ASSERT_TRUE(cmp(r, e));
        applyCompletion(r, true, 100, s);
        ASSERT_EQ(r.line, "ls dir2a/something immediately after");
    }

    {
        auto r = autoComplete("ls dir2/", std::string::npos, syntax, true);
        std::vector<std::string> e{ "dir2/sub21/", "dir2/sub22/" };
        ASSERT_TRUE(cmp(r, e));
        applyCompletion(r, true, 100, s);
        ASSERT_EQ(r.line, "ls dir2/sub2");
        auto rr = autoComplete("ls dir2/sub22", std::string::npos, syntax, true);
        applyCompletion(rr, true, 100, s);
        ASSERT_EQ(rr.line, "ls dir2/sub22/");
    }

    {
        auto r = autoComplete("ls dir2/./", std::string::npos, syntax, true);
        std::vector<std::string> e{ "dir2/./sub21/", "dir2/./sub22/" };
        ASSERT_TRUE(cmp(r, e));
        applyCompletion(r, true, 100, s);
        ASSERT_EQ(r.line, "ls dir2/./sub2");
    }

    {
        auto r = autoComplete("ls dir2/..", std::string::npos, syntax, true);
        std::vector<std::string> e{ "dir2/../" };
        ASSERT_TRUE(cmp(r, e));
        applyCompletion(r, true, 100, s);
        ASSERT_EQ(r.line, "ls dir2/../");
    }

    {
        auto r = autoComplete("ls dir2/../", std::string::npos, syntax, true);
        std::vector<std::string> e{ "dir2/../dir1/", "dir2/../dir2/", "dir2/../dir2a/" };
        ASSERT_TRUE(cmp(r, e));
        applyCompletion(r, true, 100, s);
        ASSERT_EQ(r.line, "ls dir2/../dir");
    }

    {
        auto r = autoComplete("ls dir2/../", std::string::npos, syntax, true);
        std::vector<std::string> e{ "dir2/../dir1/", "dir2/../dir2/", "dir2/../dir2a/" };
        ASSERT_TRUE(cmp(r, e));
        applyCompletion(r, true, 100, s);
        ASSERT_EQ(r.line, "ls dir2/../dir");
    }

    {
        auto r = autoComplete("ls dir2a/d", std::string::npos, syntax, true);
        applyCompletion(r, true, 100, s);
        ASSERT_EQ(r.line, "ls \"dir2a/dir space\"");
        auto rr = autoComplete("ls \"dir2a/dir space\"/", std::string::npos, syntax, false);
        applyCompletion(rr, true, 100, s);
        ASSERT_EQ(rr.line, "ls \"dir2a/dir space/next\"");
    }

    {
        auto r = autoComplete("ls \"dir\"1/", std::string::npos, syntax, true);
        applyCompletion(r, true, 100, s);
        ASSERT_EQ(r.line, "ls \"dir1/sub1\"");
    }

    {
        auto r = autoComplete("ls dir1/\"../dir2/\"", std::string::npos, syntax, true);
        applyCompletion(r, true, 100, s);
        ASSERT_EQ(r.line, "ls \"dir1/../dir2/sub2\"");
    }

    {
        auto r = autoComplete("ls /test_autocomplete_meg", std::string::npos, syntax, true);
        applyCompletion(r, true, 100, s);
        ASSERT_EQ(r.line, "ls /test_autocomplete_megafs/");
        r = autoComplete(r.line + "dir2a", std::string::npos, syntax, true);
        applyCompletion(r, true, 100, s);
        ASSERT_EQ(r.line, "ls /test_autocomplete_megafs/dir2a/");
        r = autoComplete(r.line + "d", std::string::npos, syntax, true);
        applyCompletion(r, true, 100, s);
        ASSERT_EQ(r.line, "ls \"/test_autocomplete_megafs/dir2a/dir space\"");
    }


}
#endif

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
 * - Update permissions of an existing peer in a chat
 */
TEST_F(SdkTest, SdkTestChat)
{
    megaApi[0]->log(MegaApi::LOG_LEVEL_INFO, "___TEST Chat___");

    ASSERT_NO_FATAL_FAILURE( getMegaApiAux() );    // login + fetchnodes    

    // --- Send a new contact request ---

    string message = "Hi contact. This is a testing message";

    contactRequestUpdated[1] = false;
    ASSERT_NO_FATAL_FAILURE( inviteContact(email[1], message, MegaContactRequest::INVITE_ACTION_ADD) );
    ASSERT_TRUE( waitForResponse(&contactRequestUpdated[1]) )   // at the target side (auxiliar account)
            << "Contact request update not received after " << maxTimeout << " seconds";

    // --- Accept a contact invitation ---

    ASSERT_NO_FATAL_FAILURE( getContactRequest(1, false) );

    contactRequestUpdated[0] = contactRequestUpdated[1] = false;
    ASSERT_NO_FATAL_FAILURE( replyContact(cr[1], MegaContactRequest::REPLY_ACTION_ACCEPT) );
    ASSERT_TRUE( waitForResponse(&contactRequestUpdated[1]) )   // at the target side (auxiliar account)
            << "Contact request update not received after " << maxTimeout << " seconds";
    ASSERT_TRUE( waitForResponse(&contactRequestUpdated[0]) )   // at the target side (main account)
            << "Contact request update not received after " << maxTimeout << " seconds";

    delete cr[1];   cr[1] = NULL;


    // --- Check list of available chats --- (fetch is done at SetUp())

    size_t numChats = chats.size();      // permanent chats cannot be deleted, so they're kept forever


    // --- Create a group chat ---

    MegaTextChatPeerList *peers;
    handle h;
    bool group;

    h = megaApi[1]->getMyUser()->getHandle();
    peers = MegaTextChatPeerList::createInstance();//new MegaTextChatPeerListPrivate();
    peers->addPeer(h, PRIV_STANDARD);
    group = true;

    chatUpdated[1] = false;
    requestFlags[0][MegaRequest::TYPE_CHAT_CREATE] = false;
    ASSERT_NO_FATAL_FAILURE( createChat(group, peers) );    
    ASSERT_TRUE( waitForResponse(&requestFlags[0][MegaRequest::TYPE_CHAT_CREATE]) )
            << "Cannot create a new chat";
    ASSERT_EQ(MegaError::API_OK, lastError[0]) << "Chat creation failed (error: " << lastError[0] << ")";
    ASSERT_TRUE( waitForResponse(&chatUpdated[1]) )   // at the target side (auxiliar account)
            << "Chat update not received after " << maxTimeout << " seconds";

    MegaHandle chatid = this->chatid;   // set at onRequestFinish() of chat creation request

    delete peers;

    // check the new chat information
    ASSERT_EQ(chats.size(), ++numChats) << "Unexpected received number of chats";
    ASSERT_TRUE(chatUpdated[1]) << "The peer didn't receive notification of the chat creation";


    // --- Remove a peer from the chat ---

    chatUpdated[1] = false;
    requestFlags[0][MegaRequest::TYPE_CHAT_REMOVE] = false;
    megaApi[0]->removeFromChat(chatid, h);
    ASSERT_TRUE( waitForResponse(&requestFlags[0][MegaRequest::TYPE_CHAT_REMOVE]) )
            << "Chat remove failed after " << maxTimeout << " seconds";
    ASSERT_EQ(MegaError::API_OK, lastError[0]) << "Removal of chat peer failed (error: " << lastError[0] << ")";
    int numpeers = chats[chatid]->getPeerList() ? chats[chatid]->getPeerList()->size() : 0;
    ASSERT_EQ(numpeers, 0) << "Wrong number of peers in the list of peers";
    ASSERT_TRUE( waitForResponse(&chatUpdated[1]) )   // at the target side (auxiliar account)
            << "Didn't receive notification of the peer removal after " << maxTimeout << " seconds";


    // --- Invite a contact to a chat ---

    chatUpdated[1] = false;
    requestFlags[0][MegaRequest::TYPE_CHAT_INVITE] = false;
    megaApi[0]->inviteToChat(chatid, h, PRIV_STANDARD);
    ASSERT_TRUE( waitForResponse(&requestFlags[0][MegaRequest::TYPE_CHAT_INVITE]) )
            << "Chat invitation failed after " << maxTimeout << " seconds";
    ASSERT_EQ(MegaError::API_OK, lastError[0]) << "Invitation of chat peer failed (error: " << lastError[0] << ")";
    numpeers = chats[chatid]->getPeerList() ? chats[chatid]->getPeerList()->size() : 0;
    ASSERT_EQ(numpeers, 1) << "Wrong number of peers in the list of peers";
    ASSERT_TRUE( waitForResponse(&chatUpdated[1]) )   // at the target side (auxiliar account)
            << "The peer didn't receive notification of the invitation after " << maxTimeout << " seconds";


    // --- Get the user-specific URL for the chat ---

    requestFlags[0][MegaRequest::TYPE_CHAT_URL] = false;
    megaApi[0]->getUrlChat(chatid);
    ASSERT_TRUE( waitForResponse(&requestFlags[0][MegaRequest::TYPE_CHAT_URL]) )
            << "Retrieval of chat URL failed after " << maxTimeout << " seconds";
    ASSERT_EQ(MegaError::API_OK, lastError[0]) << "Retrieval of chat URL failed (error: " << lastError[0] << ")";


    // --- Update Permissions of an existing peer in the chat

    chatUpdated[1] = false;
    requestFlags[0][MegaRequest::TYPE_CHAT_UPDATE_PERMISSIONS] = false;
    megaApi[0]->updateChatPermissions(chatid, h, PRIV_RO);
    ASSERT_TRUE( waitForResponse(&requestFlags[0][MegaRequest::TYPE_CHAT_UPDATE_PERMISSIONS]) )
            << "Update chat permissions failed after " << maxTimeout << " seconds";
    ASSERT_EQ(MegaError::API_OK, lastError[0]) << "Update of chat permissions failed (error: " << lastError[0] << ")";
    ASSERT_TRUE( waitForResponse(&chatUpdated[1]) )   // at the target side (auxiliar account)
            << "The peer didn't receive notification of the invitation after " << maxTimeout << " seconds";

}
#endif

class myMIS : public MegaInputStream
{
public:
    int64_t size;
    ifstream ifs;

    myMIS(const char* filename)
        : ifs(filename, ios::binary)
    {
        ifs.seekg(0, ios::end);
        size = ifs.tellg();
        ifs.seekg(0, ios::beg);
    }
    virtual int64_t getSize() { return size; }

    virtual bool read(char *buffer, size_t size) {
        if (buffer)
        {
            ifs.read(buffer, size);
        }
        else
        {
            ifs.seekg(size, ios::cur);
        }
        return !ifs.fail();
    }
};


TEST_F(SdkTest, SdkTestFingerprint)
{
    megaApi[0]->log(MegaApi::LOG_LEVEL_INFO, "___TEST fingerprint stream/file___");

    int filesizes[] = { 10, 100, 1000, 10000, 100000, 10000000 };
    string expected[] = {
        "DAQoBAMCAQQDAgEEAwAAAAAAAAQAypo7",
        "DAWQjMO2LBXoNwH_agtF8CX73QQAypo7",
        "EAugDFlhW_VTCMboWWFb9VMIxugQAypo7",
        "EAhAnWCqOGBx0gGOWe7N6wznWRAQAypo7",
        "GA6CGAQFLOwb40BGchttx22PvhZ5gQAypo7",
        "GA4CWmAdW1TwQ-bddEIKTmSDv0b2QQAypo7",
    };

    FSACCESS_CLASS fsa;
    string name = "testfile";
    string localname;
    fsa.path2local(&name, &localname);

    int value = 0x01020304;
    for (int i = sizeof filesizes / sizeof filesizes[0]; i--; )
    {

        {
            ofstream ofs(name.c_str(), ios::binary);
            char s[8192];
            ofs.rdbuf()->pubsetbuf(s, sizeof s);
            for (int j = filesizes[i] / sizeof(value); j-- ; ) ofs.write((char*)&value, sizeof(value));
            ofs.write((char*)&value, filesizes[i] % sizeof(value));
        }

        fsa.setmtimelocal(&localname, 1000000000);

        string streamfp, filefp;
        {
            m_time_t mtime = 0;
            {
                FileAccess* nfa = fsa.newfileaccess();
                nfa->fopen(&localname);
                mtime = nfa->mtime;
                delete nfa;
            }

            myMIS mis(name.c_str());
            streamfp.assign(megaApi[0]->getFingerprint(&mis, mtime));
        }

        filefp = megaApi[0]->getFingerprint(name.c_str());

        ASSERT_EQ(streamfp, filefp);
        ASSERT_EQ(streamfp, expected[i]);
    }
}


