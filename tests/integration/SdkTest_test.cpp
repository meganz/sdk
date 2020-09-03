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

#include "test.h"
#include "SdkTest_test.h"
#include "mega/testhooks.h"
#include "megaapi_impl.h"
#include <algorithm>

#ifdef WIN32
#include <filesystem>
namespace fs = ::std::filesystem;
#else
#include <experimental/filesystem>
namespace fs = ::std::experimental::filesystem;
#endif

using namespace std;

MegaFileSystemAccess fileSystemAccess;

#ifdef _WIN32
#if (__cplusplus >= 201700L)
namespace fs = std::filesystem;
#else
namespace fs = std::experimental::filesystem;
#endif
#endif

#ifdef _WIN32
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

#ifndef WIN32
#define DOTSLASH "./"
#else
#define DOTSLASH ".\\"
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
    return fs::exists(fn);
#else
    struct stat   buffer;
    return (stat(fn.c_str(), &buffer) == 0);
#endif
}

void copyFile(std::string& from, std::string& to)
{
    LocalPath f = LocalPath::fromPath(from, fileSystemAccess);
    LocalPath t = LocalPath::fromPath(to, fileSystemAccess);
    fileSystemAccess.copylocal(f, t, m_time());
}

std::string megaApiCacheFolder(int index)
{
    std::string p(cwd());
#ifdef _WIN32
    p += "\\";
#else
    p += "/";
#endif
    p += "sdk_test_mega_cache_" + to_string(index);

    if (!fileexists(p))
    {

#ifdef _WIN32
        #ifndef NDEBUG
        bool success =
        #endif
        fs::create_directory(p);
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

bool WaitFor(std::function<bool()>&& f, unsigned millisec)
{
    unsigned waited = 0;
    for (;;)
    {
        if (f()) return true;
        if (waited >= millisec) return false;
        WaitMillisec(100);
        waited += 100;
    }
}


enum { USERALERT_ARRIVAL_MILLISEC = 1000 };

#ifdef _WIN32
#include "mega/autocomplete.h"
#include <filesystem>
#define getcwd _getcwd
void usleep(int n)
{
    Sleep(n / 1000);
}
#endif

// helper functions and struct/classes
namespace
{

    bool buildLocalFolders(fs::path targetfolder, const string& prefix, int n, int recurselevel, int filesperfolder)
    {
        fs::path p = targetfolder / fs::u8path(prefix);
        if (!fs::create_directory(p))
            return false;

        for (int i = 0; i < filesperfolder; ++i)
        {
            string filename = "file" + to_string(i) + "_" + prefix;
            fs::path fp = p / fs::u8path(filename);
#if (__cplusplus >= 201700L)
            ofstream fs(fp/*, ios::binary*/);
#else
            ofstream fs(fp.u8string()/*, ios::binary*/);
#endif
            fs << filename;
        }

        if (recurselevel > 0)
        {
            for (int i = 0; i < n; ++i)
            {
                if (!buildLocalFolders(p, prefix + "_" + to_string(i), n, recurselevel - 1, filesperfolder))
                    return false;
            }
        }

        return true;
    }

    bool createLocalFile(fs::path path, const char *name)
    {
        if (!name)
        {
           return false;
        }

        fs::path fp = path / fs::u8path(name);
#if (__cplusplus >= 201700L)
        ofstream fs(fp/*, ios::binary*/);
#else
        ofstream fs(fp.u8string()/*, ios::binary*/);
#endif
        fs << name;
        return true;
    }
}

std::string logTime()
{
    // why do the tests take so long to run?  Log some info about what is slow.
    auto t = std::time(NULL);
    char ts[50];
    struct tm dt;
    ::mega::m_gmtime(t, &dt);
    if (!std::strftime(ts, sizeof(ts), "%H:%M:%S ", &dt))
    {
        ts[0] = '\0';
    }
    return ts;
}

std::map<size_t, std::string> gSessionIDs;

void SdkTest::SetUp()
{
    gTestingInvalidArgs = false;
}

void SdkTest::TearDown()
{
    out() << logTime() << "Test done, teardown starts" << endl;
    // do some cleanup

    for (size_t i = 0; i < gSessionIDs.size(); ++i)
    {
        if (gResumeSessions && megaApi[i] && gSessionIDs[i].empty())
        {
            if (auto p = unique_ptr<char[]>(megaApi[i]->dumpSession()))
            {
                gSessionIDs[i] = p.get();
            }
        }
    }

    gTestingInvalidArgs = false;

    LOG_info << "___ Cleaning up test (TearDown()) ___";

    out() << logTime() << "Cleaning up account" << endl;
    Cleanup();

    releaseMegaApi(1);
    releaseMegaApi(2);
    if (megaApi[0])
    {
        releaseMegaApi(0);
    }
    out() << logTime() << "Teardown done, test exiting" << endl;
}

void SdkTest::Cleanup()
{
    deleteFile(UPFILE);
    deleteFile(DOWNFILE);
    deleteFile(PUBLICFILE);
    deleteFile(AVATARDST);

    std::vector<std::unique_ptr<RequestTracker>> delSyncTrackers;

    for (auto &m : megaApi)
    {
        auto syncs = unique_ptr<MegaSyncList>(m->getSyncs());
        for (int i = syncs->size(); i--; )
        {
            delSyncTrackers.push_back(std::unique_ptr<RequestTracker>(new RequestTracker(m.get())));
            m->removeSync(syncs->get(i), delSyncTrackers.back().get());
        }
    }

    // wait for delsyncs to complete:
    for (auto& d : delSyncTrackers) d->waitForResult();
    WaitMillisec(5000);


    if (megaApi[0])
    {

        // Remove nodes in Cloud & Rubbish
        purgeTree(std::unique_ptr<MegaNode>{megaApi[0]->getRootNode()}.get(), false);
        purgeTree(std::unique_ptr<MegaNode>{megaApi[0]->getRubbishNode()}.get(), false);
        //        megaApi[0]->cleanRubbishBin();

        // Remove auxiliar contact
        std::unique_ptr<MegaUserList> ul{megaApi[0]->getContacts()};
        for (int i = 0; i < ul->size(); i++)
        {
            removeContact(ul->get(i)->getEmail());
        }

        // Remove pending contact requests
        std::unique_ptr<MegaContactRequestList> crl{megaApi[0]->getOutgoingContactRequests()};
        for (int i = 0; i < crl->size(); i++)
        {
            MegaContactRequest *cr = crl->get(i);
            megaApi[0]->inviteContact(cr->getTargetEmail(), "Removing you", MegaContactRequest::INVITE_ACTION_DELETE);
        }
    }
}

int SdkTest::getApiIndex(MegaApi* api)
{
    int apiIndex = -1;
    for (int i = int(megaApi.size()); i--; )  if (megaApi[i].get() == api) apiIndex = i;
    if (apiIndex == -1)
    {
        LOG_warn << "Instance of MegaApi not recognized";  // this can occur during MegaApi deletion due to callbacks on shutdown
    }
    return apiIndex;
}

void SdkTest::onRequestFinish(MegaApi *api, MegaRequest *request, MegaError *e)
{
    if (request->getType() == MegaRequest::TYPE_DELETE)
    {
        return;
    }
    int apiIndex = getApiIndex(api);
    if (apiIndex < 0) return;
    mApi[apiIndex].requestFlags[request->getType()] = true;
    mApi[apiIndex].lastError = e->getErrorCode();

    // there could be a race on these getting set?
    LOG_info << "lastError (by request) for MegaApi " << apiIndex << ": " << mApi[apiIndex].lastError;

    switch(request->getType())
    {
    case MegaRequest::TYPE_CREATE_FOLDER:
        mApi[apiIndex].h = request->getNodeHandle();
        break;

    case MegaRequest::TYPE_COPY:
        mApi[apiIndex].h = request->getNodeHandle();
        break;

    case MegaRequest::TYPE_EXPORT:
        if (mApi[apiIndex].lastError == API_OK)
        {
            mApi[apiIndex].h = request->getNodeHandle();
            if (request->getAccess())
            {
                link.assign(request->getLink());
            }
        }
        break;

    case MegaRequest::TYPE_GET_PUBLIC_NODE:
        if (mApi[apiIndex].lastError == API_OK)
        {
            publicNode = request->getPublicMegaNode();
        }
        break;

    case MegaRequest::TYPE_IMPORT_LINK:
        mApi[apiIndex].h = request->getNodeHandle();
        break;

    case MegaRequest::TYPE_GET_ATTR_USER:
        if ( (mApi[apiIndex].lastError == API_OK) && (request->getParamType() != MegaApi::USER_ATTR_AVATAR) )
        {
            attributeValue = request->getText();
        }

        if (request->getParamType() == MegaApi::USER_ATTR_AVATAR)
        {
            if (mApi[apiIndex].lastError == API_OK)
            {
                attributeValue = "Avatar changed";
            }

            if (mApi[apiIndex].lastError == API_ENOENT)
            {
                attributeValue = "Avatar not found";
            }
        }
        break;

#ifdef ENABLE_CHAT

    case MegaRequest::TYPE_CHAT_CREATE:
        if (mApi[apiIndex].lastError == API_OK)
        {
            MegaTextChat *chat = request->getMegaTextChatList()->get(0)->copy();

            mApi[apiIndex].chatid = chat->getHandle();
            mApi[apiIndex].chats[mApi[apiIndex].chatid].reset(chat);
        }
        break;

    case MegaRequest::TYPE_CHAT_INVITE:
        if (mApi[apiIndex].lastError == API_OK)
        {
            mApi[apiIndex].chatid = request->getNodeHandle();
            if (mApi[apiIndex].chats.find(mApi[apiIndex].chatid) != mApi[apiIndex].chats.end())
            {
                MegaTextChat *chat = mApi[apiIndex].chats[mApi[apiIndex].chatid].get();
                MegaHandle uh = request->getParentHandle();
                int priv = request->getAccess();
                unique_ptr<userpriv_vector> privsbuf{new userpriv_vector};

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
                privs = new MegaTextChatPeerListPrivate(privsbuf.get());
                chat->setPeerList(privs);
                delete privs;
            }
            else
            {
                LOG_err << "Trying to remove a peer from unknown chat";
            }
        }
        break;

    case MegaRequest::TYPE_CHAT_REMOVE:
        if (mApi[apiIndex].lastError == API_OK)
        {
            mApi[apiIndex].chatid = request->getNodeHandle();
            if (mApi[apiIndex].chats.find(mApi[apiIndex].chatid) != mApi[apiIndex].chats.end())
            {
                MegaTextChat *chat = mApi[apiIndex].chats[mApi[apiIndex].chatid].get();
                MegaHandle uh = request->getParentHandle();
                std::unique_ptr<userpriv_vector> privsbuf{new userpriv_vector};

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
                privs = new MegaTextChatPeerListPrivate(privsbuf.get());
                chat->setPeerList(privs);
                delete privs;
            }
            else
            {
                LOG_err << "Trying to remove a peer from unknown chat";
            }
        }
        break;

    case MegaRequest::TYPE_CHAT_URL:
        if (mApi[apiIndex].lastError == API_OK)
        {
            link.assign(request->getLink());
        }
        break;
#endif

    case MegaRequest::TYPE_CREATE_ACCOUNT:
        if (mApi[apiIndex].lastError == API_OK)
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

    case MegaRequest::TYPE_GET_REGISTERED_CONTACTS:
        if (mApi[apiIndex].lastError == API_OK)
        {
            stringTable.reset(request->getMegaStringTable()->copy());
        }
        break;

    case MegaRequest::TYPE_GET_COUNTRY_CALLING_CODES:
        if (mApi[apiIndex].lastError == API_OK)
        {
            stringListMap.reset(request->getMegaStringListMap()->copy());
        }
        break;

    case MegaRequest::TYPE_FETCH_TIMEZONE:
        mApi[apiIndex].tzDetails.reset(mApi[apiIndex].lastError == API_OK ? request->getMegaTimeZoneDetails()->copy() : nullptr);
        break;

    case MegaRequest::TYPE_GET_USER_EMAIL:
        if (mApi[apiIndex].lastError == API_OK)
        {
            mApi[apiIndex].email = request->getEmail();
        }
        break;

    case MegaRequest::TYPE_ACCOUNT_DETAILS:
        mApi[apiIndex].accountDetails.reset(mApi[apiIndex].lastError == API_OK ? request->getMegaAccountDetails() : nullptr);
        break;
    }
}

void SdkTest::onTransferFinish(MegaApi* api, MegaTransfer *transfer, MegaError* e)
{
    int apiIndex = getApiIndex(api);
    if (apiIndex < 0) return;

    mApi[apiIndex].transferFlags[transfer->getType()] = true;
    mApi[apiIndex].lastError = e->getErrorCode();   // todo: change the rest of the transfer test code to use lastTransferError instead.
    mApi[apiIndex].lastTransferError = e->getErrorCode();

    // there could be a race on these getting set?
    LOG_info << "lastError (by transfer) for MegaApi " << apiIndex << ": " << mApi[apiIndex].lastError;

    onTranferFinishedCount += 1;

    if (mApi[apiIndex].lastError == MegaError::API_OK)
        mApi[apiIndex].h = transfer->getNodeHandle();
}

void SdkTest::onTransferUpdate(MegaApi *api, MegaTransfer *transfer)
{
    onTransferUpdate_progress = transfer->getTransferredBytes();
    onTransferUpdate_filesize = transfer->getTotalBytes();
}

void SdkTest::onAccountUpdate(MegaApi* api)
{
    int apiIndex = getApiIndex(api);
    if (apiIndex < 0) return;

    mApi[apiIndex].accountUpdated = true;
}

void SdkTest::onUsersUpdate(MegaApi* api, MegaUserList *users)
{
    int apiIndex = getApiIndex(api);
    if (apiIndex < 0) return;

    if (!users)
        return;

    for (int i = 0; i < users->size(); i++)
    {
        MegaUser *u = users->get(i);

        if (u->hasChanged(MegaUser::CHANGE_TYPE_AVATAR)
                || u->hasChanged(MegaUser::CHANGE_TYPE_FIRSTNAME)
                || u->hasChanged(MegaUser::CHANGE_TYPE_LASTNAME))
        {
            mApi[apiIndex].userUpdated = true;
        }
        else
        {
            // Contact is removed from main account
            mApi[apiIndex].requestFlags[MegaRequest::TYPE_REMOVE_CONTACT] = true;
            mApi[apiIndex].userUpdated = true;
        }
    }
}

void SdkTest::onNodesUpdate(MegaApi* api, MegaNodeList *nodes)
{
    int apiIndex = getApiIndex(api);
    if (apiIndex < 0) return;

    mApi[apiIndex].nodeUpdated = true;
}

void SdkTest::onContactRequestsUpdate(MegaApi* api, MegaContactRequestList* requests)
{
    int apiIndex = getApiIndex(api);
    if (apiIndex < 0) return;

    mApi[apiIndex].contactRequestUpdated = true;
}

#ifdef ENABLE_CHAT
void SdkTest::onChatsUpdate(MegaApi *api, MegaTextChatList *chats)
{
    int apiIndex = getApiIndex(api);
    if (apiIndex < 0) return;

    MegaTextChatList *list = NULL;
    if (chats)
    {
        list = chats->copy();
    }
    else
    {
        list = megaApi[apiIndex]->getChatList();
    }
    for (int i = 0; i < list->size(); i++)
    {
        handle chatid = list->get(i)->getHandle();
        if (mApi[apiIndex].chats.find(chatid) != mApi[apiIndex].chats.end())
        {
            mApi[apiIndex].chats[chatid].reset(list->get(i)->copy());
        }
        else
        {
            mApi[apiIndex].chats[chatid].reset(list->get(i)->copy());
        }
    }
    delete list;

    mApi[apiIndex].chatUpdated = true;
}

void SdkTest::createChat(bool group, MegaTextChatPeerList *peers, int timeout)
{
    int apiIndex = 0;
    mApi[apiIndex].requestFlags[MegaRequest::TYPE_CHAT_CREATE] = false;
    megaApi[0]->createChat(group, peers);
    waitForResponse(&mApi[apiIndex].requestFlags[MegaRequest::TYPE_CHAT_CREATE], timeout);
    if (timeout)
    {
        ASSERT_TRUE(mApi[apiIndex].requestFlags[MegaRequest::TYPE_CHAT_CREATE]) << "Chat creation not finished after " << timeout  << " seconds";
    }

    ASSERT_EQ(MegaError::API_OK, mApi[apiIndex].lastError) << "Chat creation failed (error: " << mApi[apiIndex].lastError << ")";
}

#endif

void SdkTest::onEvent(MegaApi*, MegaEvent *event)
{
    std::lock_guard<std::mutex> lock{lastEventMutex};
    lastEvent.reset(event->copy());
}


void SdkTest::fetchnodes(unsigned int apiIndex, int timeout)
{
    mApi[apiIndex].requestFlags[MegaRequest::TYPE_FETCH_NODES] = false;

    mApi[apiIndex].megaApi->fetchNodes();

    ASSERT_TRUE( waitForResponse(&mApi[apiIndex].requestFlags[MegaRequest::TYPE_FETCH_NODES], timeout) )
            << "Fetchnodes failed after " << timeout  << " seconds";
    ASSERT_EQ(MegaError::API_OK, mApi[apiIndex].lastError) << "Fetchnodes failed (error: " << mApi[apiIndex].lastError << ")";
}

void SdkTest::logout(unsigned int apiIndex, int timeout)
{
    mApi[apiIndex].requestFlags[MegaRequest::TYPE_LOGOUT] = false;
    mApi[apiIndex].megaApi->logout(this);

    EXPECT_TRUE( waitForResponse(&mApi[apiIndex].requestFlags[MegaRequest::TYPE_LOGOUT], timeout) )
            << "Logout failed after " << timeout  << " seconds";

    // if the connection was closed before the response of the request was received, the result is ESID
    if (mApi[apiIndex].lastError == MegaError::API_ESID) mApi[apiIndex].lastError = MegaError::API_OK;

    EXPECT_EQ(MegaError::API_OK, mApi[apiIndex].lastError) << "Logout failed (error: " << mApi[apiIndex].lastError << ")";
}

char* SdkTest::dumpSession()
{
    return megaApi[0]->dumpSession();
}

void SdkTest::locallogout(int timeout)
{
    auto logoutErr = doRequestLocalLogout(0);
    ASSERT_EQ(MegaError::API_OK, logoutErr) << "Local logout failed (error: " << logoutErr << ")";
}

void SdkTest::resumeSession(const char *session, int timeout)
{
    int apiIndex = 0;
    ASSERT_EQ(MegaError::API_OK, synchronousFastLogin(apiIndex, session, this)) << "Resume session failed (error: " << mApi[apiIndex].lastError << ")";
}

void SdkTest::purgeTree(MegaNode *p, bool depthfirst)
{
    int apiIndex = 0;
    std::unique_ptr<MegaNodeList> children{megaApi[0]->getChildren(p)};

    for (int i = 0; i < children->size(); i++)
    {
        MegaNode *n = children->get(i);

        // removing the folder removes the children anyway
        if (depthfirst && n->isFolder())
            purgeTree(n);

        string nodepath = n->getName() ? n->getName() : "<no name>";
        auto result = synchronousRemove(apiIndex, n);
        if (result == API_EEXIST || result == API_ENOENT)
        {
            LOG_warn << "node " << nodepath << " was already removed in api " << apiIndex << ", detected by error code " << result;
            result = API_OK;
        }

        ASSERT_EQ(MegaError::API_OK, result) << "Remove node operation failed (error: " << mApi[apiIndex].lastError << ")";
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

bool SdkTest::synchronousTransfer(unsigned apiIndex, int type, std::function<void()> f, unsigned int timeout)
{
    auto& flag = mApi[apiIndex].transferFlags[type];
    flag = false;
    f();
    auto result = waitForResponse(&flag, timeout);
    EXPECT_TRUE(result) << "Transfer (type " << type << ") not finished yet after " << timeout << " seconds";
    if (!result) mApi[apiIndex].lastError = -999; // local timeout
    if (!result) mApi[apiIndex].lastTransferError = -999; // local timeout    TODO: switch all transfer code to use lastTransferError .  Some still uses lastError
    return result;
}

bool SdkTest::synchronousRequest(unsigned apiIndex, int type, std::function<void()> f, unsigned int timeout)
{
    auto& flag = mApi[apiIndex].requestFlags[type];
    flag = false;
    f();
    auto result = waitForResponse(&flag, timeout);
    EXPECT_TRUE(result) << "Request (type " << type << ") failed after " << timeout << " seconds";
    if (!result) mApi[apiIndex].lastError = -999;
    return result;
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

int64_t SdkTest::getFilesize(string filename)
{
    struct stat stat_buf;
    int rc = stat(filename.c_str(), &stat_buf);

    return rc == 0 ? int64_t(stat_buf.st_size) : int64_t(-1);
}

void SdkTest::deleteFile(string filename)
{
    remove(filename.c_str());
}

const char* envVarAccount[] = {"MEGA_EMAIL", "MEGA_EMAIL_AUX", "MEGA_EMAIL_AUX2"};
const char* envVarPass[] = {"MEGA_PWD", "MEGA_PWD_AUX", "MEGA_PWD_AUX2"};


void SdkTest::getAccountsForTest(unsigned howMany)
{
    assert(howMany > 0 && howMany <= 3);
    out() << logTime() << "Test setting up for " << howMany << " accounts " << endl;

    megaApi.resize(howMany);
    mApi.resize(howMany);

    std::vector<std::unique_ptr<RequestTracker>> trackers;
    trackers.resize(howMany);

    for (unsigned index = 0; index < howMany; ++index)
    {
        if (const char *buf = getenv(envVarAccount[index]))
        {
            mApi[index].email.assign(buf);
        }
        ASSERT_LT((size_t) 0, mApi[index].email.length()) << "Set test account " << index << " username at the environment variable $" << envVarAccount[index];

        if (const char* buf = getenv(envVarPass[index]))
        {
            mApi[index].pwd.assign(buf);
        }
        ASSERT_LT((size_t) 0, mApi[index].pwd.length()) << "Set test account " << index << " password at the environment variable $" << envVarPass[index];

        megaApi[index].reset(new MegaApi(APP_KEY.c_str(), megaApiCacheFolder(index).c_str(), USER_AGENT.c_str(), int(0), unsigned(THREADS_PER_MEGACLIENT)));
        mApi[index].megaApi = megaApi[index].get();

        megaApi[index]->setLoggingName(to_string(index).c_str());
        megaApi[index]->addListener(this);    // TODO: really should be per api

        if (!gResumeSessions || gSessionIDs[index].empty() || gSessionIDs[index] == "invalid")
        {
            out() << logTime() << "Logging into account " << index << endl;
            trackers[index] = asyncRequestLogin(index, mApi[index].email.c_str(), mApi[index].pwd.c_str());
        }
        else
        {
            out() << logTime() << "Resuming session for account " << index << endl;
            trackers[index] = asyncRequestFastLogin(index, gSessionIDs[index].c_str());
        }
    }

    // wait for logins to complete:
    bool anyLoginFailed = false;
    for (unsigned index = 0; index < howMany; ++index)
    {
        auto loginResult = trackers[index]->waitForResult();
        EXPECT_EQ(API_OK, loginResult) << " Failed to establish a login/session for accout " << index;
        if (loginResult != API_OK) anyLoginFailed = true;
    }
    ASSERT_FALSE(anyLoginFailed);

    // perform parallel fetchnodes for each
    for (unsigned index = 0; index < howMany; ++index)
    {
        out() << logTime() << "Fetching nodes for account " << index << endl;
        trackers[index] = asyncRequestFetchnodes(index);
    }

    // wait for fetchnodes to complete:
    bool anyFetchnodesFailed = false;
    for (unsigned index = 0; index < howMany; ++index)
    {
        auto fetchnodesResult = trackers[index]->waitForResult();
        EXPECT_EQ(API_OK, fetchnodesResult) << " Failed to fetchnodes for accout " << index;
        anyFetchnodesFailed = anyFetchnodesFailed || (fetchnodesResult != API_OK);
    }
    ASSERT_FALSE(anyFetchnodesFailed);

    // In case the last test exited without cleaning up (eg, debugging etc)
    out() << logTime() << "Cleaning up account 0" << endl;
    Cleanup();
    out() << logTime() << "Test setup done, test starts" << endl;
}

void SdkTest::releaseMegaApi(unsigned int apiIndex)
{
    if (mApi.size() <= apiIndex)
    {
        return;
    }

    assert(megaApi[apiIndex].get() == mApi[apiIndex].megaApi);
    if (mApi[apiIndex].megaApi)
    {
        if (mApi[apiIndex].megaApi->isLoggedIn())
        {
            if (!gResumeSessions)
                ASSERT_NO_FATAL_FAILURE( logout(apiIndex) );
            else
                ASSERT_NO_FATAL_FAILURE( locallogout(apiIndex) );
        }

        megaApi[apiIndex].reset();
        mApi[apiIndex].megaApi = NULL;
    }
}

void SdkTest::inviteContact(unsigned apiIndex, string email, string message, int action)
{
    ASSERT_EQ(MegaError::API_OK, synchronousInviteContact(apiIndex, email.data(), message.data(), action)) << "Contact invitation failed";
}

void SdkTest::replyContact(MegaContactRequest *cr, int action)
{
    int apiIndex = 1;
    ASSERT_EQ(MegaError::API_OK, synchronousReplyContactRequest(apiIndex, cr, action)) << "Contact reply failed";
}

void SdkTest::removeContact(string email, int timeout)
{
    int apiIndex = 0;
    MegaUser *u = megaApi[apiIndex]->getContact(email.data());
    bool null_pointer = (u == NULL);
    ASSERT_FALSE(null_pointer) << "Cannot find the specified contact (" << email << ")";

    if (u->getVisibility() != MegaUser::VISIBILITY_VISIBLE)
    {
        mApi[apiIndex].userUpdated = true;  // nothing to do
        delete u;
        return;
    }

    auto result = synchronousRemoveContact(apiIndex, u);

    if (result == API_EEXIST)
    {
        LOG_warn << "Contact " << email << " was already removed in api " << apiIndex;
        result = API_OK;
    }

    ASSERT_EQ(MegaError::API_OK, result) << "Contact deletion of " << email << " failed on api " << apiIndex;

    delete u;
}

void SdkTest::shareFolder(MegaNode *n, const char *email, int action, int timeout)
{
    int apiIndex = 0;
    ASSERT_EQ(MegaError::API_OK, synchronousShare(apiIndex, n, email, action)) << "Folder sharing failed" << endl << "User: " << email << " Action: " << action;
}

void SdkTest::createPublicLink(unsigned apiIndex, MegaNode *n, m_time_t expireDate, int timeout, bool isFreeAccount)
{
    mApi[apiIndex].requestFlags[MegaRequest::TYPE_EXPORT] = false;

    auto err = synchronousExportNode(apiIndex, n, expireDate);

    if (!expireDate || !isFreeAccount)
    {
        ASSERT_EQ(MegaError::API_OK, err) << "Public link creation failed (error: " << mApi[apiIndex].lastError << ")";
    }
    else
    {
        bool res = MegaError::API_OK != err && err != -999;
        ASSERT_TRUE(res) << "Public link creation with expire time on free account (" << mApi[apiIndex].email << ") succeed, and it mustn't";
    }
}

void SdkTest::importPublicLink(unsigned apiIndex, string link, MegaNode *parent, int timeout)
{
    mApi[apiIndex].requestFlags[MegaRequest::TYPE_IMPORT_LINK] = false;
    mApi[apiIndex].megaApi->importFileLink(link.data(), parent);

    ASSERT_TRUE(waitForResponse(&mApi[apiIndex].requestFlags[MegaRequest::TYPE_IMPORT_LINK], timeout) )
            << "Public link import not finished after " << timeout  << " seconds";
    ASSERT_EQ(MegaError::API_OK, mApi[apiIndex].lastError) << "Public link import failed (error: " << mApi[apiIndex].lastError << ")";
}

void SdkTest::getPublicNode(unsigned apiIndex, string link, int timeout)
{
    mApi[apiIndex].requestFlags[MegaRequest::TYPE_GET_PUBLIC_NODE] = false;
    mApi[apiIndex].megaApi->getPublicNode(link.data());

    ASSERT_TRUE(waitForResponse(&mApi[apiIndex].requestFlags[MegaRequest::TYPE_GET_PUBLIC_NODE], timeout) )
            << "Public link retrieval not finished after " << timeout  << " seconds";
    ASSERT_EQ(MegaError::API_OK, mApi[apiIndex].lastError) << "Public link retrieval failed (error: " << mApi[apiIndex].lastError << ")";
}

void SdkTest::removePublicLink(unsigned apiIndex, MegaNode *n, int timeout)
{
    mApi[apiIndex].requestFlags[MegaRequest::TYPE_EXPORT] = false;
    mApi[apiIndex].megaApi->disableExport(n);

    ASSERT_TRUE( waitForResponse(&mApi[apiIndex].requestFlags[MegaRequest::TYPE_EXPORT], timeout) )
            << "Public link removal not finished after " << timeout  << " seconds";
    ASSERT_EQ(MegaError::API_OK, mApi[apiIndex].lastError) << "Public link removal failed (error: " << mApi[apiIndex].lastError << ")";
}

void SdkTest::getContactRequest(unsigned int apiIndex, bool outgoing, int expectedSize)
{
    MegaContactRequestList *crl;

    if (outgoing)
    {
        crl = mApi[apiIndex].megaApi->getOutgoingContactRequests();
        ASSERT_EQ(expectedSize, crl->size()) << "Too many outgoing contact requests in account " << apiIndex;
        if (expectedSize)
            mApi[apiIndex].cr.reset(crl->get(0)->copy());
    }
    else
    {
        crl = mApi[apiIndex].megaApi->getIncomingContactRequests();
        ASSERT_EQ(expectedSize, crl->size()) << "Too many incoming contact requests in account " << apiIndex;
        if (expectedSize)
            mApi[apiIndex].cr.reset(crl->get(0)->copy());
    }

    delete crl;
}

void SdkTest::createFolder(unsigned int apiIndex, const char *name, MegaNode *n, int timeout)
{
    mApi[apiIndex].requestFlags[MegaRequest::TYPE_CREATE_FOLDER] = false;
    mApi[apiIndex].megaApi->createFolder(name, n);

    ASSERT_TRUE( waitForResponse(&mApi[apiIndex].requestFlags[MegaRequest::TYPE_CREATE_FOLDER], timeout) )
            << "Folder creation failed after " << timeout  << " seconds";
    ASSERT_EQ(MegaError::API_OK, mApi[apiIndex].lastError) << "Cannot create a folder (error: " << mApi[apiIndex].lastError << ")";
}

void SdkTest::getRegisteredContacts(const std::map<std::string, std::string>& contacts)
{
    int apiIndex = 0;

    auto contactsStringMap = std::unique_ptr<MegaStringMap>{MegaStringMap::createInstance()};
    for  (const auto& pair : contacts)
    {
        contactsStringMap->set(pair.first.c_str(), pair.second.c_str());
    }

    ASSERT_EQ(MegaError::API_OK, synchronousGetRegisteredContacts(apiIndex, contactsStringMap.get(), this)) << "Get registered contacts failed";
}

void SdkTest::getCountryCallingCodes(const int timeout)
{
    int apiIndex = 0;
    ASSERT_EQ(MegaError::API_OK, synchronousGetCountryCallingCodes(apiIndex, this)) << "Get country calling codes failed";
}

void SdkTest::setUserAttribute(int type, string value, int timeout)
{
    int apiIndex = 0;
    mApi[apiIndex].requestFlags[MegaRequest::TYPE_SET_ATTR_USER] = false;

    if (type == MegaApi::USER_ATTR_AVATAR)
    {
        megaApi[apiIndex]->setAvatar(value.empty() ? NULL : value.c_str());
    }
    else
    {
        megaApi[apiIndex]->setUserAttribute(type, value.c_str());
    }

    ASSERT_TRUE( waitForResponse(&mApi[apiIndex].requestFlags[MegaRequest::TYPE_SET_ATTR_USER], timeout) )
            << "User attribute setup not finished after " << timeout  << " seconds";
    ASSERT_EQ(MegaError::API_OK, mApi[apiIndex].lastError) << "User attribute setup failed (error: " << mApi[apiIndex].lastError << ")";
}

void SdkTest::getUserAttribute(MegaUser *u, int type, int timeout, int apiIndex)
{
    mApi[apiIndex].requestFlags[MegaRequest::TYPE_GET_ATTR_USER] = false;

    int err;
    if (type == MegaApi::USER_ATTR_AVATAR)
    {
        err = synchronousGetUserAvatar(apiIndex, u, AVATARDST.data());
    }
    else
    {
        err = synchronousGetUserAttribute(apiIndex, u, type);
    }
    bool result = (err == MegaError::API_OK) || (err == MegaError::API_ENOENT);
    ASSERT_TRUE(result) << "User attribute retrieval failed (error: " << err << ")";
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
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(2));

    string email1 = "user@domain.com";
    string pwd = "pwd";
    string email2 = "other-user@domain.com";

    LOG_info << "___TEST Create account___";

    // Create an ephemeral session internally and send a confirmation link to email
    ASSERT_TRUE(synchronousCreateAccount(0, email1.c_str(), pwd.c_str(), "MyFirstname", "MyLastname"))
            << "Account creation has failed after " << maxTimeout << " seconds";
    ASSERT_EQ(MegaError::API_OK, mApi[0].lastError) << "Account creation failed (error: " << mApi[0].lastError << ")";

    // Logout from ephemeral session and resume session
    ASSERT_NO_FATAL_FAILURE( locallogout() );
    ASSERT_TRUE(synchronousResumeCreateAccount(0, sid.c_str()))
            << "Account creation has failed after " << maxTimeout << " seconds";
    ASSERT_EQ(MegaError::API_OK, mApi[0].lastError) << "Account creation failed (error: " << mApi[0].lastError << ")";

    // Send the confirmation link to a different email address
    ASSERT_TRUE(synchronousSendSignupLink(0, email2.c_str(), "MyFirstname", pwd.c_str()))
            << "Send confirmation link to another email failed after " << maxTimeout << " seconds";
    ASSERT_EQ(MegaError::API_OK, mApi[0].lastError) << "Send confirmation link to another email address failed (error: " << mApi[0].lastError << ")";

    // Now, confirm the account by using a different client...

    // ...and wait for the AP notifying the confirmation
    bool *flag = &mApi[0].accountUpdated; *flag = false;
    ASSERT_TRUE( waitForResponse(flag) )
            << "Account confirmation not received after " << maxTimeout << " seconds";
}

bool veryclose(double a, double b)
{
    double diff = b - a;
    double denom = fabs(a) + fabs(b);
    if (denom == 0)
    {
        return diff == 0;
    }
    double ratio = fabs(diff / denom);
    return ratio * 1000000 < 1;
}

/**
 * @brief TEST_F SdkTestNodeAttributes
 *
 *
 */
TEST_F(SdkTest, SdkTestNodeAttributes)
{
    LOG_info << "___TEST Node attributes___";
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(2));

    std::unique_ptr<MegaNode> rootnode{megaApi[0]->getRootNode()};

    string filename1 = UPFILE;
    createFile(filename1, false);

    ASSERT_EQ(MegaError::API_OK, synchronousStartUpload(0, filename1.data(), rootnode.get())) << "Cannot upload a test file";

    std::unique_ptr<MegaNode> n1(megaApi[0]->getNodeByHandle(mApi[0].h));
    bool null_pointer = (n1.get() == NULL);
    ASSERT_FALSE(null_pointer) << "Cannot initialize test scenario (error: " << mApi[0].lastError << ")";


    // ___ Set invalid duration of a node ___

    gTestingInvalidArgs = true;

    ASSERT_EQ(MegaError::API_EARGS, synchronousSetNodeDuration(0, n1.get(), -14)) << "Unexpected error setting invalid node duration";

    gTestingInvalidArgs = false;


    // ___ Set duration of a node ___

    ASSERT_EQ(MegaError::API_OK, synchronousSetNodeDuration(0, n1.get(), 929734)) << "Cannot set node duration";

    n1.reset(megaApi[0]->getNodeByHandle(mApi[0].h));
    ASSERT_EQ(929734, n1->getDuration()) << "Duration value does not match";


    // ___ Reset duration of a node ___

    ASSERT_EQ(MegaError::API_OK, synchronousSetNodeDuration(0, n1.get(), -1)) << "Cannot reset node duration";

    n1.reset(megaApi[0]->getNodeByHandle(mApi[0].h));
    ASSERT_EQ(-1, n1->getDuration()) << "Duration value does not match";

    // set several values that the requests will need to consolidate, some will be in the same batch
    megaApi[0]->setCustomNodeAttribute(n1.get(), "custom1", "value1");
    megaApi[0]->setCustomNodeAttribute(n1.get(), "custom1", "value12");
    megaApi[0]->setCustomNodeAttribute(n1.get(), "custom1", "value13");
    megaApi[0]->setCustomNodeAttribute(n1.get(), "custom2", "value21");
    WaitMillisec(100);
    megaApi[0]->setCustomNodeAttribute(n1.get(), "custom2", "value22");
    megaApi[0]->setCustomNodeAttribute(n1.get(), "custom2", "value23");
    megaApi[0]->setCustomNodeAttribute(n1.get(), "custom3", "value31");
    megaApi[0]->setCustomNodeAttribute(n1.get(), "custom3", "value32");
    megaApi[0]->setCustomNodeAttribute(n1.get(), "custom3", "value33");
    ASSERT_EQ(MegaError::API_OK, doSetNodeDuration(0, n1.get(), 929734)) << "Cannot set node duration";
    n1.reset(megaApi[0]->getNodeByHandle(mApi[0].h));

    ASSERT_STREQ("value13", n1->getCustomAttr("custom1"));
    ASSERT_STREQ("value23", n1->getCustomAttr("custom2"));
    ASSERT_STREQ("value33", n1->getCustomAttr("custom3"));


    // ___ Set invalid coordinates of a node (out of range) ___

    gTestingInvalidArgs = true;

    ASSERT_EQ(MegaError::API_EARGS, synchronousSetNodeCoordinates(0, n1.get(), -1523421.8719987255814, +6349.54)) << "Unexpected error setting invalid node coordinates";


    // ___ Set invalid coordinates of a node (out of range) ___

    ASSERT_EQ(MegaError::API_EARGS, synchronousSetNodeCoordinates(0, n1.get(), -160.8719987255814, +49.54)) << "Unexpected error setting invalid node coordinates";


    // ___ Set invalid coordinates of a node (out of range) ___

    ASSERT_EQ(MegaError::API_EARGS, synchronousSetNodeCoordinates(0, n1.get(), MegaNode::INVALID_COORDINATE, +69.54)) << "Unexpected error trying to reset only one coordinate";

    gTestingInvalidArgs = false;

    // ___ Set coordinates of a node ___

    double lat = -51.8719987255814;
    double lon = +179.54;

    ASSERT_EQ(MegaError::API_OK, synchronousSetNodeCoordinates(0, n1.get(), lat, lon)) << "Cannot set node coordinates";

    n1.reset(megaApi[0]->getNodeByHandle(mApi[0].h));

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

    ASSERT_EQ(MegaError::API_OK, synchronousSetNodeCoordinates(0, n1.get(), 0, 0)) << "Cannot set node coordinates";

    n1.reset(megaApi[0]->getNodeByHandle(mApi[0].h));

    // do same conversions to lose the same precision
    buf = int(((lat + 90) / 180) * 0xFFFFFF);
    res = -90 + 180 * (double) buf / 0xFFFFFF;

    ASSERT_EQ(res, n1->getLatitude()) << "Latitude value does not match";
    ASSERT_EQ(lon, n1->getLongitude()) << "Longitude value does not match";


    // ___ Set coordinates of a node to border values (90,180) ___

    lat = 90;
    lon = 180;

    ASSERT_EQ(MegaError::API_OK, synchronousSetNodeCoordinates(0, n1.get(), lat, lon)) << "Cannot set node coordinates";

    n1.reset(megaApi[0]->getNodeByHandle(mApi[0].h));

    ASSERT_EQ(lat, n1->getLatitude()) << "Latitude value does not match";
    bool value_ok = ((n1->getLongitude() == lon) || (n1->getLongitude() == -lon));
    ASSERT_TRUE(value_ok) << "Longitude value does not match";


    // ___ Set coordinates of a node to border values (-90,-180) ___

    lat = -90;
    lon = -180;

    ASSERT_EQ(MegaError::API_OK, synchronousSetNodeCoordinates(0, n1.get(), lat, lon)) << "Cannot set node coordinates";

    n1.reset(megaApi[0]->getNodeByHandle(mApi[0].h));

    ASSERT_EQ(lat, n1->getLatitude()) << "Latitude value does not match";
    value_ok = ((n1->getLongitude() == lon) || (n1->getLongitude() == -lon));
    ASSERT_TRUE(value_ok) << "Longitude value does not match";


    // ___ Reset coordinates of a node ___

    lat = lon = MegaNode::INVALID_COORDINATE;

    synchronousSetNodeCoordinates(0, n1.get(), lat, lon);

    n1.reset(megaApi[0]->getNodeByHandle(mApi[0].h));
    ASSERT_EQ(lat, n1->getLatitude()) << "Latitude value does not match";
    ASSERT_EQ(lon, n1->getLongitude()) << "Longitude value does not match";


    // ******************    also test shareable / unshareable versions:

    ASSERT_EQ(MegaError::API_OK, synchronousGetSpecificAccountDetails(0, true, true, true)) << "Cannot get account details";

    // ___ set the coords  (shareable)
    lat = -51.8719987255814;
    lon = +179.54;
    ASSERT_EQ(MegaError::API_OK, synchronousSetNodeCoordinates(0, n1.get(), lat, lon)) << "Cannot set node coordinates";

    // ___ get a link to the file node
    ASSERT_NO_FATAL_FAILURE(createPublicLink(0, n1.get(), 0, maxTimeout, mApi[0].accountDetails->getProLevel() == 0));
    // The created link is stored in this->link at onRequestFinish()
    string nodelink = this->link;

    // ___ import the link
    ASSERT_NO_FATAL_FAILURE(importPublicLink(1, nodelink, std::unique_ptr<MegaNode>{megaApi[1]->getRootNode()}.get()));
    std::unique_ptr<MegaNode> nimported{megaApi[1]->getNodeByHandle(mApi[1].h)};

    ASSERT_TRUE(veryclose(lat, nimported->getLatitude())) << "Latitude " << n1->getLatitude() << " value does not match " << lat;
    ASSERT_TRUE(veryclose(lon, nimported->getLongitude())) << "Longitude " << n1->getLongitude() << " value does not match " << lon;

    // ___ remove the imported node, for a clean next test
    mApi[1].requestFlags[MegaRequest::TYPE_REMOVE] = false;
    megaApi[1]->remove(nimported.get());
    ASSERT_TRUE(waitForResponse(&mApi[1].requestFlags[MegaRequest::TYPE_REMOVE]))
        << "Remove operation failed after " << maxTimeout << " seconds";
    ASSERT_EQ(MegaError::API_OK, mApi[1].lastError) << "Cannot remove a node (error: " << mApi[1].lastError << ")";


    // ___ again but unshareable this time - totally separate new node - set the coords  (unshareable)

    string filename2 = "a"+UPFILE;
    createFile(filename2, false);
    ASSERT_EQ(MegaError::API_OK, synchronousStartUpload(0, filename2.data(), rootnode.get())) << "Cannot upload a test file";
    MegaNode *n2 = megaApi[0]->getNodeByHandle(mApi[0].h);
    ASSERT_NE(n2, ((void*)NULL)) << "Cannot initialize second node for scenario (error: " << mApi[0].lastError << ")";

    lat = -5 + -51.8719987255814;
    lon = -5 + +179.54;
    mApi[0].requestFlags[MegaRequest::TYPE_SET_ATTR_NODE] = false;
    megaApi[0]->setUnshareableNodeCoordinates(n2, lat, lon);
    waitForResponse(&mApi[0].requestFlags[MegaRequest::TYPE_SET_ATTR_NODE]);
    ASSERT_EQ(MegaError::API_OK, mApi[0].lastError) << "Cannot set unshareable node coordinates (error: " << mApi[0].lastError << ")";

    // ___ confirm this user can read them
    MegaNode* selfread = megaApi[0]->getNodeByHandle(n2->getHandle());
    ASSERT_TRUE(veryclose(lat, selfread->getLatitude())) << "Latitude " << n2->getLatitude() << " value does not match " << lat;
    ASSERT_TRUE(veryclose(lon, selfread->getLongitude())) << "Longitude " << n2->getLongitude() << " value does not match " << lon;

    // ___ get a link to the file node
    this->link.clear();
    ASSERT_NO_FATAL_FAILURE(createPublicLink(0, n2, 0, maxTimeout, mApi[0].accountDetails->getProLevel() == 0));

    // The created link is stored in this->link at onRequestFinish()
    string nodelink2 = this->link;

    // ___ import the link
    ASSERT_NO_FATAL_FAILURE(importPublicLink(1, nodelink2, std::unique_ptr<MegaNode>{megaApi[1]->getRootNode()}.get()));
    nimported = std::unique_ptr<MegaNode>{megaApi[1]->getNodeByHandle(mApi[1].h)};

    // ___ confirm other user cannot read them
    lat = nimported->getLatitude();
    lon = nimported->getLongitude();
    ASSERT_EQ(MegaNode::INVALID_COORDINATE, lat) << "Latitude value does not match";
    ASSERT_EQ(MegaNode::INVALID_COORDINATE, lon) << "Longitude value does not match";
}


TEST_F(SdkTest, SdkTestExerciseOtherCommands)
{
    LOG_info << "___TEST SdkTestExerciseOtherCommands___";
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(2));

    /*bool HttpReqCommandPutFA::procresult(Result r)
    bool CommandGetFA::procresult(Result r)
    bool CommandAttachFA::procresult(Result r)
    bool CommandPutFileBackgroundURL::procresult(Result r)
    bool CommandPutNodes::procresult(Result r)
    bool CommandDelVersions::procresult(Result r)
    bool CommandKillSessions::procresult(Result r)
    bool CommandEnumerateQuotaItems::procresult(Result r)
    bool CommandPurchaseAddItem::procresult(Result r)
    bool CommandPurchaseCheckout::procresult(Result r)
    bool CommandPutMultipleUAVer::procresult(Result r)
    bool CommandPutUAVer::procresult(Result r)
    bool CommandDelUA::procresult(Result r)
    bool CommandSendDevCommand::procresult(Result r)
    bool CommandGetUserEmail::procresult(Result r)
    bool CommandGetMiscFlags::procresult(Result r)
    bool CommandQueryTransferQuota::procresult(Result r)
    bool CommandGetUserTransactions::procresult(Result r)
    bool CommandGetUserPurchases::procresult(Result r)
    bool CommandGetUserSessions::procresult(Result r)
    bool CommandSetMasterKey::procresult(Result r)
    bool CommandCreateEphemeralSession::procresult(Result r)
    bool CommandResumeEphemeralSession::procresult(Result r)
    bool CommandCancelSignup::procresult(Result r)
    bool CommandWhyAmIblocked::procresult(Result r)
    bool CommandSendSignupLink::procresult(Result r)
    bool CommandSendSignupLink2::procresult(Result r)
    bool CommandQuerySignupLink::procresult(Result r)
    bool CommandConfirmSignupLink2::procresult(Result r)
    bool CommandConfirmSignupLink::procresult(Result r)
    bool CommandSetKeyPair::procresult(Result r)
    bool CommandReportEvent::procresult(Result r)
    bool CommandSubmitPurchaseReceipt::procresult(Result r)
    bool CommandCreditCardStore::procresult(Result r)
    bool CommandCreditCardQuerySubscriptions::procresult(Result r)
    bool CommandCreditCardCancelSubscriptions::procresult(Result r)
    bool CommandCopySession::procresult(Result r)
    bool CommandGetPaymentMethods::procresult(Result r)
    bool CommandUserFeedbackStore::procresult(Result r)
    bool CommandSupportTicket::procresult(Result r)
    bool CommandCleanRubbishBin::procresult(Result r)
    bool CommandGetRecoveryLink::procresult(Result r)
    bool CommandQueryRecoveryLink::procresult(Result r)
    bool CommandGetPrivateKey::procresult(Result r)
    bool CommandConfirmRecoveryLink::procresult(Result r)
    bool CommandConfirmCancelLink::procresult(Result r)
    bool CommandResendVerificationEmail::procresult(Result r)
    bool CommandResetSmsVerifiedPhoneNumber::procresult(Result r)
    bool CommandValidatePassword::procresult(Result r)
    bool CommandGetEmailLink::procresult(Result r)
    bool CommandConfirmEmailLink::procresult(Result r)
    bool CommandGetVersion::procresult(Result r)
    bool CommandGetLocalSSLCertificate::procresult(Result r)
    bool CommandChatGrantAccess::procresult(Result r)
    bool CommandChatRemoveAccess::procresult(Result r)
    bool CommandChatTruncate::procresult(Result r)
    bool CommandChatSetTitle::procresult(Result r)
    bool CommandChatPresenceURL::procresult(Result r)
    bool CommandRegisterPushNotification::procresult(Result r)
    bool CommandArchiveChat::procresult(Result r)
    bool CommandSetChatRetentionTime::procresult(Result r)
    bool CommandRichLink::procresult(Result r)
    bool CommandChatLink::procresult(Result r)
    bool CommandChatLinkURL::procresult(Result r)
    bool CommandChatLinkClose::procresult(Result r)
    bool CommandChatLinkJoin::procresult(Result r)
    bool CommandGetMegaAchievements::procresult(Result r)
    bool CommandGetWelcomePDF::procresult(Result r)
    bool CommandMediaCodecs::procresult(Result r)
    bool CommandContactLinkCreate::procresult(Result r)
    bool CommandContactLinkQuery::procresult(Result r)
    bool CommandContactLinkDelete::procresult(Result r)
    bool CommandKeepMeAlive::procresult(Result r)
    bool CommandMultiFactorAuthSetup::procresult(Result r)
    bool CommandMultiFactorAuthCheck::procresult(Result r)
    bool CommandMultiFactorAuthDisable::procresult(Result r)
    bool CommandGetPSA::procresult(Result r)
    bool CommandSetLastAcknowledged::procresult(Result r)
    bool CommandSMSVerificationSend::procresult(Result r)
    bool CommandSMSVerificationCheck::procresult(Result r)
    bool CommandFolderLinkInfo::procresult(Result r)
    bool CommandBackupPut::procresult(Result r)
    bool CommandBackupPutHeartBeat::procresult(Result r)
    bool CommandBackupRemove::procresult(Result r)*/

}

/**
 * @brief TEST_F SdkTestResumeSession
 *
 * It creates a local cache, logs out of the current session and tries to resume it later.
 */
TEST_F(SdkTest, SdkTestResumeSession)
{
    LOG_info << "___TEST Resume session___";
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(2));

    unique_ptr<char[]> session(dumpSession());

    ASSERT_NO_FATAL_FAILURE( locallogout() );
    ASSERT_NO_FATAL_FAILURE( resumeSession(session.get()) );
    ASSERT_NO_FATAL_FAILURE( fetchnodes(0) );
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
    LOG_info <<  "___TEST Node operations___";
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(2));

    // --- Create a new folder ---

    MegaNode *rootnode = megaApi[0]->getRootNode();
    char name1[64] = "New folder";

    ASSERT_NO_FATAL_FAILURE( createFolder(0, name1, rootnode) );


    // --- Rename a node ---

    MegaNode *n1 = megaApi[0]->getNodeByHandle(mApi[0].h);
    strcpy(name1, "Folder renamed");

    mApi[0].requestFlags[MegaRequest::TYPE_RENAME] = false;
    megaApi[0]->renameNode(n1, name1);
    ASSERT_TRUE( waitForResponse(&mApi[0].requestFlags[MegaRequest::TYPE_RENAME]) )
            << "Rename operation failed after " << maxTimeout << " seconds";
    ASSERT_EQ(MegaError::API_OK, mApi[0].lastError) << "Cannot rename a node (error: " << mApi[0].lastError << ")";


    // --- Copy a node ---

    MegaNode *n2;
    char name2[64] = "Folder copy";

    mApi[0].requestFlags[MegaRequest::TYPE_COPY] = false;
    megaApi[0]->copyNode(n1, rootnode, name2);
    ASSERT_TRUE( waitForResponse(&mApi[0].requestFlags[MegaRequest::TYPE_COPY]) )
            << "Copy operation failed after " << maxTimeout << " seconds";
    ASSERT_EQ(MegaError::API_OK, mApi[0].lastError) << "Cannot create a copy of a node (error: " << mApi[0].lastError << ")";
    n2 = megaApi[0]->getNodeByHandle(mApi[0].h);


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

    mApi[0].requestFlags[MegaRequest::TYPE_MOVE] = false;
    megaApi[0]->moveNode(n1, n2);
    ASSERT_TRUE( waitForResponse(&mApi[0].requestFlags[MegaRequest::TYPE_MOVE]) )
            << "Move operation failed after " << maxTimeout << " seconds";
    ASSERT_EQ(MegaError::API_OK, mApi[0].lastError) << "Cannot move node (error: " << mApi[0].lastError << ")";


    // --- Get parent node ---

    MegaNode *n5;
    n5 = megaApi[0]->getParentNode(n1);

    ASSERT_EQ(n2->getHandle(), n5->getHandle()) << "Wrong parent node";


    // --- Send to Rubbish bin ---

    mApi[0].requestFlags[MegaRequest::TYPE_MOVE] = false;
    megaApi[0]->moveNode(n2, megaApi[0]->getRubbishNode());
    ASSERT_TRUE( waitForResponse(&mApi[0].requestFlags[MegaRequest::TYPE_MOVE]) )
            << "Move operation failed after " << maxTimeout << " seconds";
    ASSERT_EQ(MegaError::API_OK, mApi[0].lastError) << "Cannot move node to Rubbish bin (error: " << mApi[0].lastError << ")";


    // --- Remove a node ---

    mApi[0].requestFlags[MegaRequest::TYPE_REMOVE] = false;
    megaApi[0]->remove(n2);
    ASSERT_TRUE( waitForResponse(&mApi[0].requestFlags[MegaRequest::TYPE_REMOVE]) )
            << "Remove operation failed after " << maxTimeout << " seconds";
    ASSERT_EQ(MegaError::API_OK, mApi[0].lastError) << "Cannot remove a node (error: " << mApi[0].lastError << ")";

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
    LOG_info << "___TEST Transfers___";
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(2));

    LOG_info << cwd();

    MegaNode *rootnode = megaApi[0]->getRootNode();
    string filename1 = UPFILE;
    createFile(filename1);


    // --- Cancel a transfer ---

    mApi[0].requestFlags[MegaRequest::TYPE_CANCEL_TRANSFERS] = false;
    megaApi[0]->startUpload(filename1.data(), rootnode);
    megaApi[0]->cancelTransfers(MegaTransfer::TYPE_UPLOAD);
    ASSERT_TRUE( waitForResponse(&mApi[0].requestFlags[MegaRequest::TYPE_CANCEL_TRANSFERS]) )
            << "Cancellation of transfers failed after " << maxTimeout << " seconds";
    EXPECT_EQ(MegaError::API_OK, mApi[0].lastError) << "Transfer cancellation failed (error: " << mApi[0].lastError << ")";


    // --- Upload a file (part 1) ---

    mApi[0].transferFlags[MegaTransfer::TYPE_UPLOAD] = false;
    megaApi[0]->startUpload(filename1.data(), rootnode);
    // do not wait yet for completion


    // --- Pause a transfer ---

    mApi[0].requestFlags[MegaRequest::TYPE_PAUSE_TRANSFERS] = false;
    megaApi[0]->pauseTransfers(true, MegaTransfer::TYPE_UPLOAD);
    ASSERT_TRUE( waitForResponse(&mApi[0].requestFlags[MegaRequest::TYPE_PAUSE_TRANSFERS]) )
            << "Pause of transfers failed after " << maxTimeout << " seconds";
    EXPECT_EQ(MegaError::API_OK, mApi[0].lastError) << "Cannot pause transfer (error: " << mApi[0].lastError << ")";
    EXPECT_TRUE(megaApi[0]->areTransfersPaused(MegaTransfer::TYPE_UPLOAD)) << "Upload transfer not paused";


    // --- Resume a transfer ---

    mApi[0].requestFlags[MegaRequest::TYPE_PAUSE_TRANSFERS] = false;
    megaApi[0]->pauseTransfers(false, MegaTransfer::TYPE_UPLOAD);
    ASSERT_TRUE( waitForResponse(&mApi[0].requestFlags[MegaRequest::TYPE_PAUSE_TRANSFERS]) )
            << "Resumption of transfers after pause has failed after " << maxTimeout << " seconds";
    EXPECT_EQ(MegaError::API_OK, mApi[0].lastError) << "Cannot resume transfer (error: " << mApi[0].lastError << ")";
    EXPECT_FALSE(megaApi[0]->areTransfersPaused(MegaTransfer::TYPE_UPLOAD)) << "Upload transfer not resumed";


    // --- Upload a file (part 2) ---


    ASSERT_TRUE( waitForResponse(&mApi[0].transferFlags[MegaTransfer::TYPE_UPLOAD], 600) )
            << "Upload transfer failed after " << 600 << " seconds";
    ASSERT_EQ(MegaError::API_OK, mApi[0].lastError) << "Cannot upload file (error: " << mApi[0].lastError << ")";

    MegaNode *n1 = megaApi[0]->getNodeByHandle(mApi[0].h);
    bool null_pointer = (n1 == NULL);

    ASSERT_FALSE(null_pointer) << "Cannot upload file (error: " << mApi[0].lastError << ")";
    ASSERT_STREQ(filename1.data(), n1->getName()) << "Uploaded file with wrong name (error: " << mApi[0].lastError << ")";


    // --- Get node by fingerprint (needs to be a file, not a folder) ---

    std::unique_ptr<char[]> fingerprint{megaApi[0]->getFingerprint(n1)};
    MegaNode *n2 = megaApi[0]->getNodeByFingerprint(fingerprint.get());

    null_pointer = (n2 == NULL);
    EXPECT_FALSE(null_pointer) << "Node by fingerprint not found";
//    ASSERT_EQ(n2->getHandle(), n4->getHandle());  This test may fail due to multiple nodes with the same name

    // --- Get the size of a file ---

    int64_t filesize = getFilesize(filename1);
    int64_t nodesize = megaApi[0]->getSize(n2);
    EXPECT_EQ(filesize, nodesize) << "Wrong size of uploaded file";


    // --- Download a file ---

    string filename2 = DOTSLASH + DOWNFILE;

    mApi[0].transferFlags[MegaTransfer::TYPE_DOWNLOAD] = false;
    megaApi[0]->startDownload(n2, filename2.c_str());
    ASSERT_TRUE( waitForResponse(&mApi[0].transferFlags[MegaTransfer::TYPE_DOWNLOAD], 600) )
            << "Download transfer failed after " << maxTimeout << " seconds";
    ASSERT_EQ(MegaError::API_OK, mApi[0].lastError) << "Cannot download the file (error: " << mApi[0].lastError << ")";

    MegaNode *n3 = megaApi[0]->getNodeByHandle(mApi[0].h);
    null_pointer = (n3 == NULL);

    ASSERT_FALSE(null_pointer) << "Cannot download node";
    ASSERT_EQ(n2->getHandle(), n3->getHandle()) << "Cannot download node (error: " << mApi[0].lastError << ")";


    // --- Upload a 0-bytes file ---

    string filename3 = EMPTYFILE;
    FILE *fp = fopen(filename3.c_str(), "w");
    fclose(fp);

    ASSERT_EQ(MegaError::API_OK, synchronousStartUpload(0, filename3.c_str(), rootnode)) << "Cannot upload a test file";

    MegaNode *n4 = megaApi[0]->getNodeByHandle(mApi[0].h);
    null_pointer = (n4 == NULL);

    ASSERT_FALSE(null_pointer) << "Cannot upload file (error: " << mApi[0].lastError << ")";
    ASSERT_STREQ(filename3.data(), n4->getName()) << "Uploaded file with wrong name (error: " << mApi[0].lastError << ")";


    // --- Download a 0-byte file ---

    filename3 = DOTSLASH +  EMPTYFILE;

    mApi[0].transferFlags[MegaTransfer::TYPE_DOWNLOAD] = false;
    megaApi[0]->startDownload(n4, filename3.c_str());
    ASSERT_TRUE( waitForResponse(&mApi[0].transferFlags[MegaTransfer::TYPE_DOWNLOAD], 600) )
            << "Download 0-byte file failed after " << maxTimeout << " seconds";
    ASSERT_EQ(MegaError::API_OK, mApi[0].lastError) << "Cannot download the file (error: " << mApi[0].lastError << ")";

    MegaNode *n5 = megaApi[0]->getNodeByHandle(mApi[0].h);
    null_pointer = (n5 == NULL);

    ASSERT_FALSE(null_pointer) << "Cannot download node";
    ASSERT_EQ(n4->getHandle(), n5->getHandle()) << "Cannot download node (error: " << mApi[0].lastError << ")";


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
    LOG_info << "___TEST Contacts___";
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(2));


    // --- Check my email and the email of the contact ---

    EXPECT_STRCASEEQ(mApi[0].email.data(), std::unique_ptr<char[]>{megaApi[0]->getMyEmail()}.get());
    EXPECT_STRCASEEQ(mApi[1].email.data(), std::unique_ptr<char[]>{megaApi[1]->getMyEmail()}.get());


    // --- Send a new contact request ---

    string message = "Hi contact. This is a testing message";

    mApi[0].contactRequestUpdated = mApi[1].contactRequestUpdated = false;
    ASSERT_NO_FATAL_FAILURE( inviteContact(0, mApi[1].email, message, MegaContactRequest::INVITE_ACTION_ADD) );
    // if there were too many invitations within a short period of time, the invitation can be rejected by
    // the API with `API_EOVERQUOTA = -17` as counter spamming meassure (+500 invites in the last 50 days)


    // --- Check the sent contact request ---

    ASSERT_TRUE( waitForResponse(&mApi[0].contactRequestUpdated) )   // at the source side (main account)
            << "Contact request update not received after " << maxTimeout << " seconds";

    ASSERT_NO_FATAL_FAILURE( getContactRequest(0, true) );

    ASSERT_STREQ(message.data(), mApi[0].cr->getSourceMessage()) << "Message sent is corrupted";
    ASSERT_STRCASEEQ(mApi[0].email.data(), mApi[0].cr->getSourceEmail()) << "Wrong source email";
    ASSERT_STRCASEEQ(mApi[1].email.data(), mApi[0].cr->getTargetEmail()) << "Wrong target email";
    ASSERT_EQ(MegaContactRequest::STATUS_UNRESOLVED, mApi[0].cr->getStatus()) << "Wrong contact request status";
    ASSERT_TRUE(mApi[0].cr->isOutgoing()) << "Wrong direction of the contact request";

    mApi[0].cr.reset();


    // --- Check received contact request ---

    ASSERT_TRUE( waitForResponse(&mApi[1].contactRequestUpdated) )   // at the target side (auxiliar account)
            << "Contact request update not received after " << maxTimeout << " seconds";

    ASSERT_NO_FATAL_FAILURE( getContactRequest(1, false) );

    // There isn't message when a user invites the same user too many times, to avoid spamming
    if (mApi[1].cr->getSourceMessage())
    {
        ASSERT_STREQ(message.data(), mApi[1].cr->getSourceMessage()) << "Message received is corrupted";
    }
    ASSERT_STRCASEEQ(mApi[0].email.data(), mApi[1].cr->getSourceEmail()) << "Wrong source email";
    ASSERT_STREQ(NULL, mApi[1].cr->getTargetEmail()) << "Wrong target email";    // NULL according to MegaApi documentation
    ASSERT_EQ(MegaContactRequest::STATUS_UNRESOLVED, mApi[1].cr->getStatus()) << "Wrong contact request status";
    ASSERT_FALSE(mApi[1].cr->isOutgoing()) << "Wrong direction of the contact request";

    mApi[1].cr.reset();


    // --- Ignore received contact request ---

    ASSERT_NO_FATAL_FAILURE( getContactRequest(1, false) );

    mApi[1].contactRequestUpdated = false;
    ASSERT_NO_FATAL_FAILURE( replyContact(mApi[1].cr.get(), MegaContactRequest::REPLY_ACTION_IGNORE) );
    ASSERT_TRUE( waitForResponse(&mApi[1].contactRequestUpdated) )   // at the target side (auxiliar account)
            << "Contact request update not received after " << maxTimeout << " seconds";

    // Ignoring a PCR does not generate actionpackets for the account sending the invitation

    mApi[1].cr.reset();

    ASSERT_NO_FATAL_FAILURE( getContactRequest(1, false, 0) );
    mApi[1].cr.reset();


    // --- Cancel the invitation ---

    message = "I don't wanna be your contact anymore";

    mApi[0].contactRequestUpdated = false;
    ASSERT_NO_FATAL_FAILURE( inviteContact(0, mApi[1].email, message, MegaContactRequest::INVITE_ACTION_DELETE) );
    ASSERT_TRUE( waitForResponse(&mApi[0].contactRequestUpdated) )   // at the target side (auxiliar account), where the deletion is checked
            << "Contact request update not received after " << maxTimeout << " seconds";

    ASSERT_NO_FATAL_FAILURE( getContactRequest(0, true, 0) );
    mApi[0].cr.reset();


    // --- Remind a contact invitation (cannot until 2 weeks after invitation/last reminder) ---

//    mApi[1].contactRequestUpdated = false;
//    megaApi->inviteContact(mApi[1].email.data(), message.data(), MegaContactRequest::INVITE_ACTION_REMIND);
//    waitForResponse(&mApi[1].contactRequestUpdated, 0);    // only at auxiliar account, where the deletion is checked

//    ASSERT_TRUE(mApi[1].contactRequestUpdated) << "Contact invitation reminder not received after " << timeout  << " seconds";


    // --- Invite a new contact (again) ---

    mApi[1].contactRequestUpdated = false;
    ASSERT_NO_FATAL_FAILURE( inviteContact(0, mApi[1].email, message, MegaContactRequest::INVITE_ACTION_ADD) );
    ASSERT_TRUE( waitForResponse(&mApi[1].contactRequestUpdated) )   // at the target side (auxiliar account)
            << "Contact request creation not received after " << maxTimeout << " seconds";


    // --- Deny a contact invitation ---

    ASSERT_NO_FATAL_FAILURE( getContactRequest(1, false) );

    mApi[0].contactRequestUpdated = mApi[1].contactRequestUpdated = false;
    ASSERT_NO_FATAL_FAILURE( replyContact(mApi[1].cr.get(), MegaContactRequest::REPLY_ACTION_DENY) );
    ASSERT_TRUE( waitForResponse(&mApi[1].contactRequestUpdated) )   // at the target side (auxiliar account)
            << "Contact request creation not received after " << maxTimeout << " seconds";
    ASSERT_TRUE( waitForResponse(&mApi[0].contactRequestUpdated) )   // at the source side (main account)
            << "Contact request creation not received after " << maxTimeout << " seconds";

    mApi[1].cr.reset();

    ASSERT_NO_FATAL_FAILURE( getContactRequest(0, true, 0) );
    mApi[0].cr.reset();

    ASSERT_NO_FATAL_FAILURE( getContactRequest(1, false, 0) );
    mApi[1].cr.reset();


    // --- Invite a new contact (again) ---

    mApi[1].contactRequestUpdated = false;
    ASSERT_NO_FATAL_FAILURE( inviteContact(0, mApi[1].email, message, MegaContactRequest::INVITE_ACTION_ADD) );
    ASSERT_TRUE( waitForResponse(&mApi[1].contactRequestUpdated) )   // at the target side (auxiliar account)
            << "Contact request creation not received after " << maxTimeout << " seconds";


    // --- Accept a contact invitation ---

    ASSERT_NO_FATAL_FAILURE( getContactRequest(1, false) );

    mApi[0].contactRequestUpdated = mApi[1].contactRequestUpdated = false;
    ASSERT_NO_FATAL_FAILURE( replyContact(mApi[1].cr.get(), MegaContactRequest::REPLY_ACTION_ACCEPT) );
    ASSERT_TRUE( waitForResponse(&mApi[0].contactRequestUpdated) )   // at the target side (main account)
            << "Contact request creation not received after " << maxTimeout << " seconds";
    ASSERT_TRUE( waitForResponse(&mApi[1].contactRequestUpdated) )   // at the target side (auxiliar account)
            << "Contact request creation not received after " << maxTimeout << " seconds";

    mApi[1].cr.reset();

    ASSERT_NO_FATAL_FAILURE( getContactRequest(0, true, 0) );
    mApi[0].cr.reset();

    ASSERT_NO_FATAL_FAILURE( getContactRequest(1, false, 0) );
    mApi[1].cr.reset();


    // --- Modify firstname ---

    string firstname = "My firstname";

    mApi[1].userUpdated = false;
    ASSERT_NO_FATAL_FAILURE( setUserAttribute(MegaApi::USER_ATTR_FIRSTNAME, firstname));
    ASSERT_TRUE( waitForResponse(&mApi[1].userUpdated) )   // at the target side (auxiliar account)
            << "User attribute update not received after " << maxTimeout << " seconds";


    // --- Check firstname of a contact

    MegaUser *u = megaApi[0]->getMyUser();

    bool null_pointer = (u == NULL);
    ASSERT_FALSE(null_pointer) << "Cannot find the MegaUser for email: " << mApi[0].email;

    ASSERT_NO_FATAL_FAILURE( getUserAttribute(u, MegaApi::USER_ATTR_FIRSTNAME));
    ASSERT_EQ( firstname, attributeValue) << "Firstname is wrong";

    delete u;


    // --- Set master key already as exported

    u = megaApi[0]->getMyUser();

    mApi[0].requestFlags[MegaRequest::TYPE_SET_ATTR_USER] = false;
    megaApi[0]->masterKeyExported();
    ASSERT_TRUE( waitForResponse(&mApi[0].requestFlags[MegaRequest::TYPE_SET_ATTR_USER]) );

    ASSERT_NO_FATAL_FAILURE( getUserAttribute(u, MegaApi::USER_ATTR_PWD_REMINDER, maxTimeout, 0));
    string pwdReminder = attributeValue;
    size_t offset = pwdReminder.find(':');
    offset = pwdReminder.find(':', offset+1);
    ASSERT_EQ( pwdReminder.at(offset+1), '1' ) << "Password reminder attribute not updated";

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

    mApi[1].userUpdated = false;
    ASSERT_NO_FATAL_FAILURE( setUserAttribute(MegaApi::USER_ATTR_AVATAR, AVATARSRC));
    ASSERT_TRUE( waitForResponse(&mApi[1].userUpdated) )   // at the target side (auxiliar account)
            << "User attribute update not received after " << maxTimeout << " seconds";


    // --- Get avatar of a contact ---

    u = megaApi[0]->getMyUser();

    null_pointer = (u == NULL);
    ASSERT_FALSE(null_pointer) << "Cannot find the MegaUser for email: " << mApi[0].email;

    attributeValue = "";

    ASSERT_NO_FATAL_FAILURE( getUserAttribute(u, MegaApi::USER_ATTR_AVATAR));
    ASSERT_STREQ( "Avatar changed", attributeValue.data()) << "Failed to change avatar";

    int64_t filesizeSrc = getFilesize(AVATARSRC);
    int64_t filesizeDst = getFilesize(AVATARDST);
    ASSERT_EQ(filesizeDst, filesizeSrc) << "Received avatar differs from uploaded avatar";

    delete u;


    // --- Delete avatar ---

    mApi[1].userUpdated = false;
    ASSERT_NO_FATAL_FAILURE( setUserAttribute(MegaApi::USER_ATTR_AVATAR, ""));
    ASSERT_TRUE( waitForResponse(&mApi[1].userUpdated) )   // at the target side (auxiliar account)
            << "User attribute update not received after " << maxTimeout << " seconds";


    // --- Get non-existing avatar of a contact ---

    u = megaApi[0]->getMyUser();

    null_pointer = (u == NULL);
    ASSERT_FALSE(null_pointer) << "Cannot find the MegaUser for email: " << mApi[0].email;

    attributeValue = "";

    ASSERT_NO_FATAL_FAILURE( getUserAttribute(u, MegaApi::USER_ATTR_AVATAR));
    ASSERT_STREQ("Avatar not found", attributeValue.data()) << "Failed to remove avatar";

    delete u;


    // --- Delete an existing contact ---

    mApi[0].userUpdated = false;
    ASSERT_NO_FATAL_FAILURE( removeContact(mApi[1].email) );
    ASSERT_TRUE( waitForResponse(&mApi[0].userUpdated) )   // at the target side (main account)
            << "User attribute update not received after " << maxTimeout << " seconds";

    u = megaApi[0]->getContact(mApi[1].email.data());
    null_pointer = (u == NULL);

    ASSERT_FALSE(null_pointer) << "Cannot find the MegaUser for email: " << mApi[1].email;
    ASSERT_EQ(MegaUser::VISIBILITY_HIDDEN, u->getVisibility()) << "New contact is still visible";

    delete u;
}

bool SdkTest::checkAlert(int apiIndex, const string& title, const string& path)
{
    bool ok = false;
    for (int i = 0; !ok && i < 10; ++i)
    {

        MegaUserAlertList* list = mApi[apiIndex].megaApi->getUserAlerts();
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
TEST_F(SdkTest, SdkTestShares)
{
    LOG_info << "___TEST Shares___";
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(2));

    MegaShareList *sl;
    MegaShare *s;
    MegaNodeList *nl;
    MegaNode *n;
    MegaNode *n1;

    // Initialize a test scenario : create some folders/files to share

    // Create some nodes to share
    //  |--Shared-folder
    //    |--subfolder
    //    |--file.txt

    std::unique_ptr<MegaNode> rootnode{megaApi[0]->getRootNode()};
    char foldername1[64] = "Shared-folder";
    MegaHandle hfolder1;

    ASSERT_NO_FATAL_FAILURE( createFolder(0, foldername1, rootnode.get()) );

    hfolder1 = mApi[0].h;     // 'h' is set in 'onRequestFinish()'
    n1 = megaApi[0]->getNodeByHandle(hfolder1);

    char foldername2[64] = "subfolder";
    MegaHandle hfolder2;

    ASSERT_NO_FATAL_FAILURE( createFolder(0, foldername2, std::unique_ptr<MegaNode>{megaApi[0]->getNodeByHandle(hfolder1)}.get()) );

    hfolder2 = mApi[0].h;

    MegaHandle hfile1;
    createFile(PUBLICFILE.data(), false);   // not a large file since don't need to test transfers here

    ASSERT_EQ(MegaError::API_OK, synchronousStartUpload(0, PUBLICFILE.data(), std::unique_ptr<MegaNode>{megaApi[0]->getNodeByHandle(hfolder1)}.get())) << "Cannot upload a test file";

    hfile1 = mApi[0].h;


    // --- Download authorized node from another account ---

    MegaNode *nNoAuth = megaApi[0]->getNodeByHandle(hfile1);

    int transferError = synchronousStartDownload(1, nNoAuth, "unauthorized_node");

    bool hasFailed = (transferError != API_OK);
    ASSERT_TRUE(hasFailed) << "Download of node without authorization successful! (it should fail)";

    MegaNode *nAuth = megaApi[0]->authorizeNode(nNoAuth);

    transferError = synchronousStartDownload(1, nAuth, "authorized_node");
    ASSERT_EQ(MegaError::API_OK, transferError) << "Cannot download authorized node (error: " << mApi[1].lastError << ")";

    delete nNoAuth;
    delete nAuth;

    // Initialize a test scenario: create a new contact to share to

    string message = "Hi contact. Let's share some stuff";

    mApi[1].contactRequestUpdated = false;
    ASSERT_NO_FATAL_FAILURE( inviteContact(0, mApi[1].email, message, MegaContactRequest::INVITE_ACTION_ADD) );
    ASSERT_TRUE( waitForResponse(&mApi[1].contactRequestUpdated) )   // at the target side (auxiliar account)
            << "Contact request creation not received after " << maxTimeout << " seconds";


    ASSERT_NO_FATAL_FAILURE( getContactRequest(1, false) );

    mApi[0].contactRequestUpdated = mApi[1].contactRequestUpdated = false;
    ASSERT_NO_FATAL_FAILURE( replyContact(mApi[1].cr.get(), MegaContactRequest::REPLY_ACTION_ACCEPT) );
    ASSERT_TRUE( waitForResponse(&mApi[1].contactRequestUpdated) )   // at the target side (auxiliar account)
            << "Contact request creation not received after " << maxTimeout << " seconds";
    ASSERT_TRUE( waitForResponse(&mApi[0].contactRequestUpdated) )   // at the source side (main account)
            << "Contact request creation not received after " << maxTimeout << " seconds";

    mApi[1].cr.reset();


    // --- Create a new outgoing share ---

    mApi[0].nodeUpdated = mApi[1].nodeUpdated = false;
    ASSERT_NO_FATAL_FAILURE( shareFolder(n1, mApi[1].email.data(), MegaShare::ACCESS_READ) );
    ASSERT_TRUE( waitForResponse(&mApi[0].nodeUpdated) )   // at the target side (main account)
            << "Node update not received after " << maxTimeout << " seconds";
    ASSERT_TRUE( waitForResponse(&mApi[1].nodeUpdated) )   // at the target side (auxiliar account)
            << "Node update not received after " << maxTimeout << " seconds";


    // --- Check the outgoing share ---

    sl = megaApi[0]->getOutShares();
    ASSERT_EQ(1, sl->size()) << "Outgoing share failed";
    s = sl->get(0);

    n1 = megaApi[0]->getNodeByHandle(hfolder1);    // get an updated version of the node

    ASSERT_EQ(MegaShare::ACCESS_READ, s->getAccess()) << "Wrong access level of outgoing share";
    ASSERT_EQ(hfolder1, s->getNodeHandle()) << "Wrong node handle of outgoing share";
    ASSERT_STREQ(mApi[1].email.data(), s->getUser()) << "Wrong email address of outgoing share";
    ASSERT_TRUE(n1->isShared()) << "Wrong sharing information at outgoing share";
    ASSERT_TRUE(n1->isOutShare()) << "Wrong sharing information at outgoing share";

    delete sl;


    // --- Check the incoming share ---

    sl = megaApi[1]->getInSharesList();
    ASSERT_EQ(1, sl->size()) << "Incoming share not received in auxiliar account";

    nl = megaApi[1]->getInShares(megaApi[1]->getContact(mApi[0].email.data()));
    ASSERT_EQ(1, nl->size()) << "Incoming share not received in auxiliar account";
    n = nl->get(0);

    ASSERT_EQ(hfolder1, n->getHandle()) << "Wrong node handle of incoming share";
    ASSERT_STREQ(foldername1, n->getName()) << "Wrong folder name of incoming share";
    ASSERT_EQ(MegaError::API_OK, megaApi[1]->checkAccess(n, MegaShare::ACCESS_READ).getErrorCode()) << "Wrong access level of incoming share";
    ASSERT_TRUE(n->isInShare()) << "Wrong sharing information at incoming share";
    ASSERT_TRUE(n->isShared()) << "Wrong sharing information at incoming share";

    delete nl;

    // check the corresponding user alert
    ASSERT_TRUE(checkAlert(1, "New shared folder from " + mApi[0].email, mApi[0].email + ":Shared-folder"));

    // add a folder under the share
    char foldernameA[64] = "dummyname1";
    char foldernameB[64] = "dummyname2";
    ASSERT_NO_FATAL_FAILURE(createFolder(0, foldernameA, std::unique_ptr<MegaNode>{megaApi[0]->getNodeByHandle(hfolder2)}.get()));
    ASSERT_NO_FATAL_FAILURE(createFolder(0, foldernameB, std::unique_ptr<MegaNode>{megaApi[0]->getNodeByHandle(hfolder2)}.get()));

    // check the corresponding user alert
    ASSERT_TRUE(checkAlert(1, mApi[0].email + " added 2 folders", std::unique_ptr<MegaNode>{megaApi[0]->getNodeByHandle(hfolder2)}->getHandle(), 2));

    // --- Modify the access level of an outgoing share ---

    mApi[0].nodeUpdated = mApi[1].nodeUpdated = false;
    ASSERT_NO_FATAL_FAILURE( shareFolder(megaApi[0]->getNodeByHandle(hfolder1), mApi[1].email.data(), MegaShare::ACCESS_READWRITE) );
    ASSERT_TRUE( waitForResponse(&mApi[0].nodeUpdated) )   // at the target side (main account)
            << "Node update not received after " << maxTimeout << " seconds";
    ASSERT_TRUE( waitForResponse(&mApi[1].nodeUpdated) )   // at the target side (auxiliar account)
            << "Node update not received after " << maxTimeout << " seconds";

    nl = megaApi[1]->getInShares(megaApi[1]->getContact(mApi[0].email.data()));
    ASSERT_EQ(1, nl->size()) << "Incoming share not received in auxiliar account";
    n = nl->get(0);

    ASSERT_EQ(MegaError::API_OK, megaApi[1]->checkAccess(n, MegaShare::ACCESS_READWRITE).getErrorCode()) << "Wrong access level of incoming share";

    delete nl;


    // --- Revoke access to an outgoing share ---

    mApi[0].nodeUpdated = mApi[1].nodeUpdated = false;
    ASSERT_NO_FATAL_FAILURE( shareFolder(n1, mApi[1].email.data(), MegaShare::ACCESS_UNKNOWN) );
    ASSERT_TRUE( waitForResponse(&mApi[0].nodeUpdated) )   // at the target side (main account)
            << "Node update not received after " << maxTimeout << " seconds";
    ASSERT_TRUE( waitForResponse(&mApi[1].nodeUpdated) )   // at the target side (auxiliar account)
            << "Node update not received after " << maxTimeout << " seconds";

    delete sl;
    sl = megaApi[0]->getOutShares();
    ASSERT_EQ(0, sl->size()) << "Outgoing share revocation failed";
    delete sl;

    nl = megaApi[1]->getInShares(megaApi[1]->getContact(mApi[0].email.data()));
    ASSERT_EQ(0, nl->size()) << "Incoming share revocation failed";
    delete nl;

    // check the corresponding user alert
    {
        MegaUserAlertList* list = megaApi[1]->getUserAlerts();
        ASSERT_TRUE(list->size() > 0);
        MegaUserAlert* a = list->get(list->size() - 1);
        ASSERT_STREQ(a->getTitle(), ("Access to folders shared by " + mApi[0].email + " was removed").c_str());
        ASSERT_STREQ(a->getPath(), (mApi[0].email + ":Shared-folder").c_str());
        ASSERT_NE(a->getNodeHandle(), UNDEF);
        delete list;
    }

    // --- Get pending outgoing shares ---

    char emailfake[64];
    srand(unsigned(time(NULL)));
    sprintf(emailfake, "%d@nonexistingdomain.com", rand()%1000000);
    // carefull, antispam rejects too many tries without response for the same address

    n = megaApi[0]->getNodeByHandle(hfolder2);

    mApi[0].contactRequestUpdated = false;
    mApi[0].nodeUpdated = false;
    ASSERT_NO_FATAL_FAILURE( shareFolder(n, emailfake, MegaShare::ACCESS_FULL) );
    ASSERT_TRUE( waitForResponse(&mApi[0].nodeUpdated) )   // at the target side (main account)
            << "Node update not received after " << maxTimeout << " seconds";
    ASSERT_TRUE( waitForResponse(&mApi[0].contactRequestUpdated) )   // at the target side (main account)
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

    ASSERT_EQ(MegaError::API_OK, synchronousGetSpecificAccountDetails(0, true, true, true)) << "Cannot get account details";

    std::unique_ptr<MegaNode> nfile1{megaApi[0]->getNodeByHandle(hfile1)};

    ASSERT_NO_FATAL_FAILURE( createPublicLink(0, nfile1.get(), 0, maxTimeout, mApi[0].accountDetails->getProLevel() == 0) );
    // The created link is stored in this->link at onRequestFinish()

    // Get a fresh snapshot of the node and check it's actually exported
    nfile1 = std::unique_ptr<MegaNode>{megaApi[0]->getNodeByHandle(hfile1)};
    ASSERT_TRUE(nfile1->isExported()) << "Node is not exported, must be exported";
    ASSERT_FALSE(nfile1->isTakenDown()) << "Public link is taken down, it mustn't";

    // Regenerate the same link should not trigger a new request
    string oldLink = link;
    link = "";
    nfile1 = std::unique_ptr<MegaNode>{megaApi[0]->getNodeByHandle(hfile1)};
    ASSERT_NO_FATAL_FAILURE( createPublicLink(0, nfile1.get(), 0, maxTimeout, mApi[0].accountDetails->getProLevel() == 0) );
    ASSERT_STREQ(oldLink.c_str(), link.c_str()) << "Wrong public link after link update";


    // Try to update the expiration time of an existing link (only for PRO accounts are allowed, otherwise -11
    ASSERT_NO_FATAL_FAILURE( createPublicLink(0, nfile1.get(), m_time() + 30*86400, maxTimeout, mApi[0].accountDetails->getProLevel() == 0) );
    nfile1 = std::unique_ptr<MegaNode>{megaApi[0]->getNodeByHandle(hfile1)};
    if (mApi[0].accountDetails->getProLevel() == 0)
    {
        ASSERT_EQ(0, nfile1->getExpirationTime()) << "Expiration time successfully set, when it shouldn't";
    }
    ASSERT_FALSE(nfile1->isExpired()) << "Public link is expired, it mustn't";


    // --- Import a file public link ---

    ASSERT_NO_FATAL_FAILURE( importPublicLink(0, link, rootnode.get()) );

    MegaNode *nimported = megaApi[0]->getNodeByHandle(mApi[0].h);

    ASSERT_STREQ(nfile1->getName(), nimported->getName()) << "Imported file with wrong name";
    ASSERT_EQ(rootnode->getHandle(), nimported->getParentHandle()) << "Imported file in wrong path";


    // --- Get node from file public link ---

    ASSERT_NO_FATAL_FAILURE( getPublicNode(1, link) );

    ASSERT_TRUE(publicNode->isPublic()) << "Cannot get a node from public link";


    // --- Remove a public link ---

    ASSERT_NO_FATAL_FAILURE( removePublicLink(0, nfile1.get()) );

    nfile1 = std::unique_ptr<MegaNode>{megaApi[0]->getNodeByHandle(mApi[0].h)};
    ASSERT_FALSE(nfile1->isPublic()) << "Public link removal failed (still public)";

    delete nimported;


    // --- Create a folder public link ---

    MegaNode *nfolder1 = megaApi[0]->getNodeByHandle(hfolder1);

    ASSERT_NO_FATAL_FAILURE( createPublicLink(0, nfolder1, 0, maxTimeout, mApi[0].accountDetails->getProLevel() == 0) );
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
    ASSERT_NO_FATAL_FAILURE( createPublicLink(0, nfolder1, 0, maxTimeout, mApi[0].accountDetails->getProLevel() == 0) );
    ASSERT_STREQ(oldLink.c_str(), link.c_str()) << "Wrong public link after link update";

    delete nfolder1;

}


TEST_F(SdkTest, SdkTestShareKeys)
{
    LOG_info << "___TEST ShareKeys___";
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(3));

    // Three user scenario, with nested shares and new nodes created that need keys to be shared to the other users.
    // User A creates folder and shares it with user B
    // User A creates folders / subfolder and shares it with user C
    // When user C adds files to subfolder, does B receive the keys ?

    unique_ptr<MegaNode> rootnodeA(megaApi[0]->getRootNode());
    unique_ptr<MegaNode> rootnodeB(megaApi[1]->getRootNode());
    unique_ptr<MegaNode> rootnodeC(megaApi[2]->getRootNode());

    ASSERT_TRUE(rootnodeA &&rootnodeB &&rootnodeC);

    ASSERT_NO_FATAL_FAILURE(createFolder(0, "share-folder-A", rootnodeA.get()));
    unique_ptr<MegaNode> shareFolderA(megaApi[0]->getNodeByHandle(mApi[0].h));
    ASSERT_TRUE(!!shareFolderA);

    ASSERT_NO_FATAL_FAILURE(createFolder(0, "sub-folder-A", shareFolderA.get()));
    unique_ptr<MegaNode> subFolderA(megaApi[0]->getNodeByHandle(mApi[0].h));
    ASSERT_TRUE(!!subFolderA);

    // Initialize a test scenario: create a new contact to share to

    ASSERT_EQ(MegaError::API_OK, synchronousInviteContact(0, mApi[1].email.c_str(), "SdkTestShareKeys contact request A to B", MegaContactRequest::INVITE_ACTION_ADD));
    ASSERT_EQ(MegaError::API_OK, synchronousInviteContact(0, mApi[2].email.c_str(), "SdkTestShareKeys contact request A to C", MegaContactRequest::INVITE_ACTION_ADD));

    ASSERT_TRUE(WaitFor([this]() {return unique_ptr<MegaContactRequestList>(megaApi[1]->getIncomingContactRequests())->size() == 1
                                      && unique_ptr<MegaContactRequestList>(megaApi[2]->getIncomingContactRequests())->size() == 1;}, 60000));
    ASSERT_NO_FATAL_FAILURE(getContactRequest(1, false));
    ASSERT_NO_FATAL_FAILURE(getContactRequest(2, false));


    ASSERT_EQ(MegaError::API_OK, synchronousReplyContactRequest(1, mApi[1].cr.get(), MegaContactRequest::REPLY_ACTION_ACCEPT));
    ASSERT_EQ(MegaError::API_OK, synchronousReplyContactRequest(2, mApi[2].cr.get(), MegaContactRequest::REPLY_ACTION_ACCEPT));

    WaitMillisec(3000);

    ASSERT_EQ(MegaError::API_OK, synchronousShare(0, shareFolderA.get(), mApi[1].email.c_str(), MegaShare::ACCESS_READ));
    ASSERT_EQ(MegaError::API_OK, synchronousShare(0, subFolderA.get(), mApi[2].email.c_str(), MegaShare::ACCESS_FULL));

    ASSERT_TRUE(WaitFor([this]() { return unique_ptr<MegaShareList>(megaApi[1]->getInSharesList())->size() == 1
                           && unique_ptr<MegaShareList>(megaApi[2]->getInSharesList())->size() == 1; }, 60000));

    unique_ptr<MegaNodeList> nl1(megaApi[1]->getInShares(megaApi[1]->getContact(mApi[0].email.c_str())));
    unique_ptr<MegaNodeList> nl2(megaApi[2]->getInShares(megaApi[2]->getContact(mApi[0].email.c_str())));

    ASSERT_EQ(1, nl1->size());
    ASSERT_EQ(1, nl2->size());

    MegaNode* receivedShareNodeB = nl1->get(0);
    MegaNode* receivedShareNodeC = nl2->get(0);

    ASSERT_NO_FATAL_FAILURE(createFolder(2, "folderByC1", receivedShareNodeC));
    ASSERT_NO_FATAL_FAILURE(createFolder(2, "folderByC2", receivedShareNodeC));

    ASSERT_TRUE(WaitFor([this, &subFolderA]() { unique_ptr<MegaNodeList> aView(megaApi[0]->getChildren(subFolderA.get()));
                                   return aView->size() == 2; }, 60000));

    WaitMillisec(10000);  // make it shorter once we do actually get the keys (seems to need a bug fix)

    // can A see the added folders?

    unique_ptr<MegaNodeList> aView(megaApi[0]->getChildren(subFolderA.get()));
    ASSERT_EQ(2, aView->size());
    ASSERT_STREQ(aView->get(0)->getName(), "folderByC1");
    ASSERT_STREQ(aView->get(1)->getName(), "folderByC2");

    // Can B see the added folders?
    unique_ptr<MegaNodeList> bView(megaApi[1]->getChildren(receivedShareNodeB));
    ASSERT_EQ(1, bView->size());
    ASSERT_STREQ(bView->get(0)->getName(), "sub-folder-A");
    unique_ptr<MegaNodeList> bView2(megaApi[1]->getChildren(bView->get(0)));
    ASSERT_EQ(2, bView2->size());
    ASSERT_STREQ(bView2->get(0)->getName(), "NO_KEY");  // TODO: This is technically not correct but a current side effect of avoiding going back to the servers frequently - to be fixed soon.  For now choose the value that matches production
    ASSERT_STREQ(bView2->get(1)->getName(), "NO_KEY");
}

string localpathToUtf8Leaf(const LocalPath& itemlocalname, FSACCESS_CLASS& fsa)
{
    size_t lastpart = itemlocalname.lastpartlocal(fsa);
    LocalPath name(itemlocalname.subpathFrom(lastpart));
    return name.toPath(fsa);
}

LocalPath fspathToLocal(const fs::path& p, FSACCESS_CLASS& fsa)
{
    string path(p.u8string());
    return LocalPath::fromPath(path, fsa);
}



TEST_F(SdkTest, SdkTestFolderIteration)
{
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(2));

    for (int testcombination = 0; testcombination < 2; testcombination++)
    {
        bool openWithNameOrUseFileAccess = testcombination == 0;

        error_code ec;
        if (fs::exists("test_SdkTestFolderIteration"))
        {
            fs::remove_all("test_SdkTestFolderIteration", ec);
            ASSERT_TRUE(!ec) << "could not remove old test folder";
        }

        fs::create_directory("test_SdkTestFolderIteration", ec);
        ASSERT_TRUE(!ec) << "could not create test folder";

        fs::path iteratePath = fs::current_path() / "test_SdkTestFolderIteration";

        // make a directory
        fs::create_directory(iteratePath / "folder");

        // make a file
        {
            ofstream f( (iteratePath / "file.txt").u8string().c_str());
            f << "file content";
        }

        // make some content to test the glob flag
        {
            fs::create_directory(iteratePath / "glob1folder");
            fs::create_directory(iteratePath / "glob2folder");
            ofstream f1( (iteratePath / "glob1file.txt").u8string().c_str());
            ofstream f2( (iteratePath / "glob2file.txt").u8string().c_str());
            f1 << "file content";
            f2 << "file content";
        }
        unsigned glob_entries = 4;

        // make a symlink to a folder (not recoginised by our dnext() on windows currently)
        fs::create_directory_symlink(iteratePath / "folder", iteratePath / "folderlink", ec);
        ASSERT_TRUE(!ec) << "could not create folder symlink";

        // make a symlinnk to a file
        fs::create_symlink(iteratePath / "file.txt", iteratePath / "filelink.txt", ec);
        ASSERT_TRUE(!ec) << "could not create folder symlink";

        // note on windows:  symlinks are excluded by skipAttributes for FILE_ATTRIBUTE_REPARSE_POINT (also see https://docs.microsoft.com/en-us/windows/win32/fileio/determining-whether-a-directory-is-a-volume-mount-point)

        struct FileAccessFields
        {
            m_off_t size = -2;
            m_time_t mtime = 2;
            handle fsid = 3;
            bool fsidvalid = false;
            nodetype_t type = (nodetype_t)-9;
            bool mIsSymLink = false;
            bool retry = false;
            int errorcode = -998;

            FileAccessFields() = default;

            FileAccessFields(const FileAccess& f)
            {
                size = f.size;
                mtime = f.mtime;
                fsid = f.fsid;
                fsidvalid = f.fsidvalid;
                type = f.type;
                mIsSymLink = f.mIsSymLink;
                retry = f.retry;
                errorcode = f.errorcode;
            }
            bool operator == (const FileAccessFields& f) const
            {
                if (size != f.size) { EXPECT_EQ(size, f.size); return false; }
                if (mtime != f.mtime) { EXPECT_EQ(mtime, f.mtime); return false; }

                if (!mIsSymLink)
                {
                    // do we need fsid to be correct for symlink?  Seems on mac plain vs iterated differ
                    if (fsid != f.fsid) { EXPECT_EQ(fsid, f.fsid); return false; }
                }

                if (fsidvalid != f.fsidvalid) { EXPECT_EQ(fsidvalid, f.fsidvalid); return false; }
                if (type != f.type) { EXPECT_EQ(type, f.type); return false; }
                if (mIsSymLink != f.mIsSymLink) { EXPECT_EQ(mIsSymLink, f.mIsSymLink); return false; }
                if (retry != f.retry) { EXPECT_EQ(retry, f.retry); return false; }
                if (errorcode != f.errorcode) { EXPECT_EQ(errorcode, f.errorcode); return false; }
                return true;
            }
        };

        // capture results from the ways of gettnig the file info
        std::map<std::string, FileAccessFields > plain_fopen;
        std::map<std::string, FileAccessFields > iterate_fopen;
        std::map<std::string, FileAccessFields > plain_follow_fopen;
        std::map<std::string, FileAccessFields > iterate_follow_fopen;

        FSACCESS_CLASS fsa;
        auto localdir = fspathToLocal(iteratePath, fsa);

        std::unique_ptr<FileAccess> fopen_directory(fsa.newfileaccess(false));  // false = don't follow symlinks
        ASSERT_TRUE(fopen_directory->fopen(localdir, true, false));

        // now open and iterate the directory, not following symlinks (either by name or fopen'd directory)
        std::unique_ptr<DirAccess> da(fsa.newdiraccess());
        if (da->dopen(openWithNameOrUseFileAccess ? &localdir : NULL, openWithNameOrUseFileAccess ? NULL : fopen_directory.get(), false))
        {
            nodetype_t type;
            LocalPath itemlocalname;
            while (da->dnext(localdir, itemlocalname, false, &type))
            {
                string leafNameUtf8 = localpathToUtf8Leaf(itemlocalname, fsa);

                std::unique_ptr<FileAccess> plain_fopen_fa(fsa.newfileaccess(false));
                std::unique_ptr<FileAccess> iterate_fopen_fa(fsa.newfileaccess(false));

                LocalPath localpath = localdir;
                localpath.appendWithSeparator(itemlocalname, true, fsa.localseparator);

                ASSERT_TRUE(plain_fopen_fa->fopen(localpath, true, false));
                plain_fopen[leafNameUtf8] = *plain_fopen_fa;

                ASSERT_TRUE(iterate_fopen_fa->fopen(localpath, true, false, da.get()));
                iterate_fopen[leafNameUtf8] = *iterate_fopen_fa;
            }
        }

        std::unique_ptr<FileAccess> fopen_directory2(fsa.newfileaccess(true));  // true = follow symlinks
        ASSERT_TRUE(fopen_directory2->fopen(localdir, true, false));

        // now open and iterate the directory, following symlinks (either by name or fopen'd directory)
        std::unique_ptr<DirAccess> da_follow(fsa.newdiraccess());
        if (da_follow->dopen(openWithNameOrUseFileAccess ? &localdir : NULL, openWithNameOrUseFileAccess ? NULL : fopen_directory2.get(), false))
        {
            nodetype_t type;
            LocalPath itemlocalname;
            while (da_follow->dnext(localdir, itemlocalname, true, &type))
            {
                string leafNameUtf8 = localpathToUtf8Leaf(itemlocalname, fsa);

                std::unique_ptr<FileAccess> plain_follow_fopen_fa(fsa.newfileaccess(true));
                std::unique_ptr<FileAccess> iterate_follow_fopen_fa(fsa.newfileaccess(true));

                LocalPath localpath = localdir;
                localpath.appendWithSeparator(itemlocalname, true, fsa.localseparator);

                ASSERT_TRUE(plain_follow_fopen_fa->fopen(localpath, true, false));
                plain_follow_fopen[leafNameUtf8] = *plain_follow_fopen_fa;

                ASSERT_TRUE(iterate_follow_fopen_fa->fopen(localpath, true, false, da_follow.get()));
                iterate_follow_fopen[leafNameUtf8] = *iterate_follow_fopen_fa;
            }
        }

    #ifdef WIN32
        std::set<std::string> plain_names { "folder", "file.txt" }; // currently on windows, any type of symlink is ignored when iterating directories
        std::set<std::string> follow_names { "folder", "file.txt"};
    #else
        std::set<std::string> plain_names { "folder", "file.txt" };
        std::set<std::string> follow_names { "folder", "file.txt", "folderlink", "filelink.txt" };
    #endif

        ASSERT_EQ(plain_fopen.size(), plain_names.size() + glob_entries);
        ASSERT_EQ(iterate_fopen.size(), plain_names.size() + glob_entries);
        ASSERT_EQ(plain_follow_fopen.size(), follow_names.size() + glob_entries);
        ASSERT_EQ(iterate_follow_fopen.size(), follow_names.size() + glob_entries);

        for (auto& name : follow_names)
        {
            bool expected_non_follow = plain_names.find(name) != plain_names.end();
            bool issymlink = name.find("link") != string::npos;

            if (expected_non_follow)
            {
                ASSERT_TRUE(plain_fopen.find(name) != plain_fopen.end()) << name;
                ASSERT_TRUE(iterate_fopen.find(name) != iterate_fopen.end()) << name;

                auto& plain = plain_fopen[name];
                auto& iterate = iterate_fopen[name];

                ASSERT_EQ(plain, iterate)  << name;
                ASSERT_TRUE(plain.mIsSymLink == issymlink);
            }

            ASSERT_TRUE(plain_follow_fopen.find(name) != plain_follow_fopen.end()) << name;
            ASSERT_TRUE(iterate_follow_fopen.find(name) != iterate_follow_fopen.end()) << name;

            auto& plain_follow = plain_follow_fopen[name];
            auto& iterate_follow = iterate_follow_fopen[name];

            ASSERT_EQ(plain_follow, iterate_follow) << name;
            ASSERT_TRUE(plain_follow.mIsSymLink == issymlink);
        }

        //ASSERT_EQ(plain_fopen["folder"].size, 0);  size field is not set for folders
        ASSERT_EQ(plain_fopen["folder"].type, FOLDERNODE);
        ASSERT_EQ(plain_fopen["folder"].fsidvalid, true);
        ASSERT_EQ(plain_fopen["folder"].mIsSymLink, false);

        ASSERT_EQ(plain_fopen["file.txt"].size, 12);
        ASSERT_EQ(plain_fopen["file.txt"].fsidvalid, true);
        ASSERT_EQ(plain_fopen["file.txt"].type, FILENODE);
        ASSERT_EQ(plain_fopen["file.txt"].mIsSymLink, false);

// on windows and mac and linux, without the follow flag on, directory iteration does not report symlinks (currently)
//
//        //ASSERT_EQ(plain_fopen["folder"].size, 0);  size field is not set for folders
//        ASSERT_EQ(plain_fopen["folderlink"].type, FOLDERNODE);
//        ASSERT_EQ(plain_fopen["folderlink"].fsidvalid, true);
//        ASSERT_EQ(plain_fopen["folderlink"].mIsSymLink, true);
//
//        ASSERT_EQ(plain_fopen["filelink.txt"].size, 12);
//        ASSERT_EQ(plain_fopen["filelink.txt"].fsidvalid, true);
//        ASSERT_EQ(plain_fopen["filelink.txt"].type, FILENODE);
//        ASSERT_EQ(plain_fopen["filelink.txt"].mIsSymLink, true);
//
        ASSERT_TRUE(plain_fopen.find("folderlink") == plain_fopen.end());
        ASSERT_TRUE(plain_fopen.find("filelink.txt") == plain_fopen.end());

        // check the glob flag
        auto localdirGlob = fspathToLocal(iteratePath / "glob1*", fsa);
        std::unique_ptr<DirAccess> da2(fsa.newdiraccess());
        if (da2->dopen(&localdirGlob, NULL, true))
        {
            nodetype_t type;
            LocalPath itemlocalname;
            set<string> remainingExpected { "glob1folder", "glob1file.txt" };
            while (da2->dnext(localdir, itemlocalname, true, &type))
            {
                string leafNameUtf8 = localpathToUtf8Leaf(itemlocalname, fsa);
                ASSERT_EQ(leafNameUtf8.substr(0, 5), string("glob1"));
                ASSERT_TRUE(remainingExpected.find(leafNameUtf8) != remainingExpected.end());
                remainingExpected.erase(leafNameUtf8);
            }
            ASSERT_EQ(remainingExpected.size(), 0u);
        }

    }
}



/**
* @brief TEST_F SdkTestConsoleAutocomplete
*
* Run various tests confirming the console autocomplete will work as expected
*
*/
#ifdef _WIN32

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
            out() << (i < s.size() ? s[i] : "") << "/" << (i < c.completions.size() ? c.completions[i].s : "") << endl;
        }
    }
    return result;
}

TEST_F(SdkTest, SdkTestConsoleAutocomplete)
{
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(2));
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

    MegaApiImpl* impl = *((MegaApiImpl**)(((char*)megaApi[0].get()) + sizeof(*megaApi[0].get())) - 1); //megaApi[0]->pImpl;
    MegaClient* client = impl->getMegaClient();


    std::unique_ptr<Either> p(new Either);
    p->Add(sequence(text("cd")));
    p->Add(sequence(text("lcd")));
    p->Add(sequence(text("ls"), opt(flag("-R")), opt(ACN(new MegaFS(true, true, client, &megaCurDir, "")))));
    p->Add(sequence(text("lls"), opt(flag("-R")), opt(ACN(new LocalFS(true, true, "")))));
    ACN syntax(std::move(p));

    error_code e;
    fs::remove_all("test_autocomplete_files", e);

    fs::create_directory("test_autocomplete_files");
    fs::path old_cwd = fs::current_path();
    fs::current_path("test_autocomplete_files");

    fs::create_directory("dir1");
    fs::create_directory("dir1\\sub11");
    fs::create_directory("dir1\\sub12");
    fs::create_directory("dir2");
    fs::create_directory("dir2\\sub21");
    fs::create_directory("dir2\\sub22");
    fs::create_directory("dir2a");
    fs::create_directory("dir2a\\dir space");
    fs::create_directory("dir2a\\dir space\\next");
    fs::create_directory("dir2a\\dir space2");
    fs::create_directory("dir2a\\nospace");

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
    MegaNode *n0 = megaApi[0]->getNodeByHandle(mApi[0].h);

    megaCurDir = mApi[0].h;

    ASSERT_NO_FATAL_FAILURE(createFolder(0, "dir1", n0));
    MegaNode *n1 = megaApi[0]->getNodeByHandle(mApi[0].h);
    ASSERT_NO_FATAL_FAILURE(createFolder(0, "sub11", n1));
    ASSERT_NO_FATAL_FAILURE(createFolder(0, "sub12", n1));

    ASSERT_NO_FATAL_FAILURE(createFolder(0, "dir2", n0));
    MegaNode *n2 = megaApi[0]->getNodeByHandle(mApi[0].h);
    ASSERT_NO_FATAL_FAILURE(createFolder(0, "sub21", n2));
    ASSERT_NO_FATAL_FAILURE(createFolder(0, "sub22", n2));

    ASSERT_NO_FATAL_FAILURE(createFolder(0, "dir2a", n0));
    MegaNode *n3 = megaApi[0]->getNodeByHandle(mApi[0].h);
    ASSERT_NO_FATAL_FAILURE(createFolder(0, "dir space", n3));
    MegaNode *n31 = megaApi[0]->getNodeByHandle(mApi[0].h);
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

    fs::current_path(old_cwd);

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
    LOG_info << "___TEST Chat___";
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(2));

    // --- Send a new contact request ---

    string message = "Hi contact. This is a testing message";

    mApi[1].contactRequestUpdated = false;
    ASSERT_NO_FATAL_FAILURE( inviteContact(0, mApi[1].email, message, MegaContactRequest::INVITE_ACTION_ADD) );
    ASSERT_TRUE( waitForResponse(&mApi[1].contactRequestUpdated) )   // at the target side (auxiliar account)
            << "Contact request update not received after " << maxTimeout << " seconds";
    // if there were too many invitations within a short period of time, the invitation can be rejected by
    // the API with `API_EOVERQUOTA = -17` as counter spamming meassure (+500 invites in the last 50 days)

    // --- Accept a contact invitation ---

    ASSERT_NO_FATAL_FAILURE( getContactRequest(1, false) );

    mApi[0].contactRequestUpdated = mApi[1].contactRequestUpdated = false;
    ASSERT_NO_FATAL_FAILURE( replyContact(mApi[1].cr.get(), MegaContactRequest::REPLY_ACTION_ACCEPT) );
    ASSERT_TRUE( waitForResponse(&mApi[1].contactRequestUpdated) )   // at the target side (auxiliar account)
            << "Contact request update not received after " << maxTimeout << " seconds";
    ASSERT_TRUE( waitForResponse(&mApi[0].contactRequestUpdated) )   // at the target side (main account)
            << "Contact request update not received after " << maxTimeout << " seconds";

    mApi[1].cr.reset();


    // --- Check list of available chats --- (fetch is done at SetUp())

    size_t numChats = mApi[0].chats.size();      // permanent chats cannot be deleted, so they're kept forever


    // --- Create a group chat ---

    MegaTextChatPeerList *peers;
    handle h;
    bool group;

    h = megaApi[1]->getMyUser()->getHandle();
    peers = MegaTextChatPeerList::createInstance();//new MegaTextChatPeerListPrivate();
    peers->addPeer(h, PRIV_STANDARD);
    group = true;

    mApi[1].chatUpdated = false;
    mApi[0].requestFlags[MegaRequest::TYPE_CHAT_CREATE] = false;
    ASSERT_NO_FATAL_FAILURE( createChat(group, peers) );
    ASSERT_TRUE( waitForResponse(&mApi[0].requestFlags[MegaRequest::TYPE_CHAT_CREATE]) )
            << "Cannot create a new chat";
    ASSERT_EQ(MegaError::API_OK, mApi[0].lastError) << "Chat creation failed (error: " << mApi[0].lastError << ")";
    ASSERT_TRUE( waitForResponse(&mApi[1].chatUpdated ))   // at the target side (auxiliar account)
            << "Chat update not received after " << maxTimeout << " seconds";

    MegaHandle chatid = mApi[0].chatid;   // set at onRequestFinish() of chat creation request

    delete peers;

    // check the new chat information
    ASSERT_EQ(mApi[0].chats.size(), ++numChats) << "Unexpected received number of chats";
    ASSERT_TRUE(mApi[1].chatUpdated) << "The peer didn't receive notification of the chat creation";


    // --- Remove a peer from the chat ---

    mApi[1].chatUpdated = false;
    mApi[0].requestFlags[MegaRequest::TYPE_CHAT_REMOVE] = false;
    megaApi[0]->removeFromChat(chatid, h);
    ASSERT_TRUE( waitForResponse(&mApi[0].requestFlags[MegaRequest::TYPE_CHAT_REMOVE]) )
            << "Chat remove failed after " << maxTimeout << " seconds";
    ASSERT_EQ(MegaError::API_OK, mApi[0].lastError) << "Removal of chat peer failed (error: " << mApi[0].lastError << ")";
    int numpeers = mApi[0].chats[chatid]->getPeerList() ? mApi[0].chats[chatid]->getPeerList()->size() : 0;
    ASSERT_EQ(numpeers, 0) << "Wrong number of peers in the list of peers";
    ASSERT_TRUE( waitForResponse(&mApi[1].chatUpdated) )   // at the target side (auxiliar account)
            << "Didn't receive notification of the peer removal after " << maxTimeout << " seconds";


    // --- Invite a contact to a chat ---

    mApi[1].chatUpdated = false;
    mApi[0].requestFlags[MegaRequest::TYPE_CHAT_INVITE] = false;
    megaApi[0]->inviteToChat(chatid, h, PRIV_STANDARD);
    ASSERT_TRUE( waitForResponse(&mApi[0].requestFlags[MegaRequest::TYPE_CHAT_INVITE]) )
            << "Chat invitation failed after " << maxTimeout << " seconds";
    ASSERT_EQ(MegaError::API_OK, mApi[0].lastError) << "Invitation of chat peer failed (error: " << mApi[0].lastError << ")";
    numpeers = mApi[0].chats[chatid]->getPeerList() ? mApi[0].chats[chatid]->getPeerList()->size() : 0;
    ASSERT_EQ(numpeers, 1) << "Wrong number of peers in the list of peers";
    ASSERT_TRUE( waitForResponse(&mApi[1].chatUpdated) )   // at the target side (auxiliar account)
            << "The peer didn't receive notification of the invitation after " << maxTimeout << " seconds";


    // --- Get the user-specific URL for the chat ---

    mApi[0].requestFlags[MegaRequest::TYPE_CHAT_URL] = false;
    megaApi[0]->getUrlChat(chatid);
    ASSERT_TRUE( waitForResponse(&mApi[0].requestFlags[MegaRequest::TYPE_CHAT_URL]) )
            << "Retrieval of chat URL failed after " << maxTimeout << " seconds";
    ASSERT_EQ(MegaError::API_OK, mApi[0].lastError) << "Retrieval of chat URL failed (error: " << mApi[0].lastError << ")";


    // --- Update Permissions of an existing peer in the chat

    mApi[1].chatUpdated = false;
    mApi[0].requestFlags[MegaRequest::TYPE_CHAT_UPDATE_PERMISSIONS] = false;
    megaApi[0]->updateChatPermissions(chatid, h, PRIV_RO);
    ASSERT_TRUE( waitForResponse(&mApi[0].requestFlags[MegaRequest::TYPE_CHAT_UPDATE_PERMISSIONS]) )
            << "Update chat permissions failed after " << maxTimeout << " seconds";
    ASSERT_EQ(MegaError::API_OK, mApi[0].lastError) << "Update of chat permissions failed (error: " << mApi[0].lastError << ")";
    ASSERT_TRUE( waitForResponse(&mApi[1].chatUpdated) )   // at the target side (auxiliar account)
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
    LOG_info << "___TEST fingerprint stream/file___";
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(2));

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
    LocalPath localname = LocalPath::fromPath(name, fsa);

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

        fsa.setmtimelocal(localname, 1000000000);

        string streamfp, filefp;
        {
            m_time_t mtime = 0;
            {
                auto nfa = fsa.newfileaccess();
                nfa->fopen(localname);
                mtime = nfa->mtime;
            }

            myMIS mis(name.c_str());
            streamfp.assign(megaApi[0]->getFingerprint(&mis, mtime));
        }

        filefp = megaApi[0]->getFingerprint(name.c_str());

        ASSERT_EQ(streamfp, filefp);
        ASSERT_EQ(streamfp, expected[i]);
    }
}


static void incrementFilename(string& s)
{
    if (s.size() > 2)
    {
        if (isdigit(s[s.size() - 2]) | !isdigit(s[s.size() - 1]))
        {
            s += "00";
        }
        else
        {
            s[s.size() - 1] += 1;
            if (s[s.size() - 1] > '9')
            {
                s[s.size() - 1] -= 1;
                s[s.size() - 2] += 1;
            }
        }
    }
}

struct second_timer
{
    m_time_t t;
    m_time_t pause_t;
    second_timer() { t = m_time(); }
    void reset () { t = m_time(); }
    void pause() { pause_t = m_time(); }
    void resume() { t += m_time() - pause_t; }
    size_t elapsed() { return size_t(m_time() - t); }
};

namespace mega
{
    class DebugTestHook
    {
    public:
        static int countdownToOverquota;
        static int countdownTo404;
        static int countdownTo403;
        static int countdownToTimeout;
        static bool isRaid;
        static bool isRaidKnown;

        static void onSetIsRaid_morechunks(::mega::RaidBufferManager* tbm)
        {

            unsigned oldvalue = tbm->raidLinesPerChunk;
            tbm->raidLinesPerChunk /= 4;
            LOG_info << "adjusted raidlinesPerChunk from " << oldvalue << " to " << tbm->raidLinesPerChunk;
        }

        static bool  onHttpReqPost509(HttpReq* req)
        {
            if (req->type == REQ_BINARY)
            {
                if (countdownToOverquota-- == 0) {
                    req->httpstatus = 509;
                    req->timeleft = 30;  // in seconds
                    req->status = REQ_FAILURE;

                    LOG_info << "SIMULATING HTTP GET 509 OVERQUOTA";
                    return true;
                }
            }
            return false;
        }

        static bool  onHttpReqPost404Or403(HttpReq* req)
        {
            if (req->type == REQ_BINARY)
            {
                if (countdownTo404-- == 0) {
                    req->httpstatus = 404;
                    req->status = REQ_FAILURE;

                    LOG_info << "SIMULATING HTTP GET 404";
                    return true;
                }
                if (countdownTo403-- == 0) {
                    req->httpstatus = 403;
                    req->status = REQ_FAILURE;

                    LOG_info << "SIMULATING HTTP GET 403";
                    return true;
                }
            }
            return false;
        }


        static bool  onHttpReqPostTimeout(HttpReq* req)
        {
            if (req->type == REQ_BINARY)
            {
                if (countdownToTimeout-- == 0) {
                    req->lastdata = Waiter::ds;
                    req->status = REQ_INFLIGHT;

                    LOG_info << "SIMULATING HTTP TIMEOUT (timeout period begins now)";
                    return true;
                }
            }
            return false;
        }

        static void onSetIsRaid(::mega::RaidBufferManager* tbm)
        {
            isRaid = tbm->isRaid();
            isRaidKnown = true;
        }

        static bool resetForTests()
        {
#ifdef MEGASDK_DEBUG_TEST_HOOKS_ENABLED
            globalMegaTestHooks = MegaTestHooks(); // remove any callbacks set in other tests
            countdownToOverquota = 3;
            countdownTo404 = 5;
            countdownTo403 = 10;
            countdownToTimeout = 15;
            isRaid = false;
            isRaidKnown = false;
            return true;
#else
            return false;
#endif
        }

        static void onSetIsRaid_smallchunks10(::mega::RaidBufferManager* tbm)
        {
            tbm->raidLinesPerChunk = 10;
        }

    };

    int DebugTestHook::countdownToOverquota = 3;
    bool DebugTestHook::isRaid = false;
    bool DebugTestHook::isRaidKnown = false;
    int DebugTestHook::countdownTo404 = 5;
    int DebugTestHook::countdownTo403 = 10;
    int DebugTestHook::countdownToTimeout = 15;

}


/**
* @brief TEST_F SdkTestCloudraidTransfers
*
* - Download our well-known cloudraid file with standard settings
* - Download our well-known cloudraid file, but this time with small chunk sizes and periodically pausing and unpausing
* - Download our well-known cloudraid file, but this time with small chunk sizes and periodically destrying the megaApi object, then recreating and Resuming (with session token)
*
*/

#ifdef DEBUG
TEST_F(SdkTest, SdkTestCloudraidTransfers)
{
    LOG_info << "___TEST Cloudraid transfers___";
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(2));

    ASSERT_TRUE(DebugTestHook::resetForTests()) << "SDK test hooks are not enabled in release mode";

    MegaNode *rootnode = megaApi[0]->getRootNode();

    ASSERT_NO_FATAL_FAILURE(importPublicLink(0, "https://mega.nz/#!zAJnUTYD!8YE5dXrnIEJ47NdDfFEvqtOefhuDMphyae0KY5zrhns", rootnode));
    MegaHandle imported_file_handle = mApi[0].h;

    MegaNode *nimported = megaApi[0]->getNodeByHandle(imported_file_handle);


    string filename = DOTSLASH "cloudraid_downloaded_file.sdktest";
    deleteFile(filename.c_str());

    // plain cloudraid download
    mApi[0].transferFlags[MegaTransfer::TYPE_DOWNLOAD] = false;
    megaApi[0]->startDownload(nimported, filename.c_str());
    ASSERT_TRUE(waitForResponse(&mApi[0].transferFlags[MegaTransfer::TYPE_DOWNLOAD], 600))
        << "Download cloudraid transfer failed after " << maxTimeout << " seconds";
    ASSERT_EQ(MegaError::API_OK, mApi[0].lastError) << "Cannot download the cloudraid file (error: " << mApi[0].lastError << ")";


    // cloudraid download with periodic pause and resume

    incrementFilename(filename);
    deleteFile(filename.c_str());

    // smaller chunk sizes so we can get plenty of pauses
    #ifdef MEGASDK_DEBUG_TEST_HOOKS_ENABLED
    globalMegaTestHooks.onSetIsRaid = ::mega::DebugTestHook::onSetIsRaid_morechunks;
    #endif

    // plain cloudraid download
    {
        onTransferUpdate_progress = 0;
        onTransferUpdate_filesize = 0;
        mApi[0].transferFlags[MegaTransfer::TYPE_DOWNLOAD] = false;
        megaApi[0]->startDownload(nimported, filename.c_str());

        m_off_t lastprogress = 0, pausecount = 0;
        second_timer t;
        while (t.elapsed() < 60 && (onTransferUpdate_filesize == 0 || onTransferUpdate_progress < onTransferUpdate_filesize))
        {
            if (onTransferUpdate_progress > lastprogress)
            {
                megaApi[0]->pauseTransfers(true);
                pausecount += 1;
                WaitMillisec(100);
                megaApi[0]->pauseTransfers(false);
                lastprogress = onTransferUpdate_progress;
            }
            WaitMillisec(100);
        }
        ASSERT_LT(t.elapsed(), 60u) << "timed out downloading cloudraid file";
        ASSERT_GE(onTransferUpdate_filesize, 0u);
        ASSERT_TRUE(onTransferUpdate_progress == onTransferUpdate_filesize);
        ASSERT_GE(pausecount, 3);
        ASSERT_TRUE(waitForResponse(&mApi[0].transferFlags[MegaTransfer::TYPE_DOWNLOAD], 30))<< "Download cloudraid transfer with pauses failed";
        ASSERT_EQ(MegaError::API_OK, mApi[0].lastError) << "Cannot download the cloudraid file (error: " << mApi[0].lastError << ")";
    }


    incrementFilename(filename);
    deleteFile(filename.c_str());

    // cloudraid download with periodic full exit and resume from session ID
    // plain cloudraid download
    {
        megaApi[0]->setMaxDownloadSpeed(32 * 1024 * 1024 * 8 / 30); // should take 30 seconds, not counting exit/resume session
        mApi[0].transferFlags[MegaTransfer::TYPE_DOWNLOAD] = false;
        megaApi[0]->startDownload(nimported, filename.c_str());

        std::string sessionId = megaApi[0]->dumpSession();

        onTransferUpdate_progress = 0;// updated in callbacks
        onTransferUpdate_filesize = 0;
        m_off_t lastprogress = 0;
        unsigned exitresumecount = 0;
        second_timer t;
        auto initialOnTranferFinishedCount = onTranferFinishedCount;
        auto lastOnTranferFinishedCount = onTranferFinishedCount;
        while (t.elapsed() < 180 && onTranferFinishedCount < initialOnTranferFinishedCount + 2)
        {
            if (onTransferUpdate_progress > lastprogress + onTransferUpdate_filesize/6)
            {
                megaApi[0].reset();
                exitresumecount += 1;
                WaitMillisec(100);

                megaApi[0].reset(new MegaApi(APP_KEY.c_str(), megaApiCacheFolder(0).c_str(), USER_AGENT.c_str(), int(0), unsigned(THREADS_PER_MEGACLIENT)));
                mApi[0].megaApi = megaApi[0].get();
                megaApi[0]->addListener(this);
                megaApi[0]->setMaxDownloadSpeed(32 * 1024 * 1024 * 8 / 30); // should take 30 seconds, not counting exit/resume session

                t.pause();
                ASSERT_NO_FATAL_FAILURE(resumeSession(sessionId.c_str()));
                ASSERT_NO_FATAL_FAILURE(fetchnodes(0));
                t.resume();

                lastprogress = onTransferUpdate_progress;
            }
            else if (onTranferFinishedCount > lastOnTranferFinishedCount)
            {
                t.reset();
                lastOnTranferFinishedCount = onTranferFinishedCount;
                deleteFile(filename.c_str());
                onTransferUpdate_progress = 0;
                onTransferUpdate_filesize = 0;
                lastprogress = 0;
                mApi[0].transferFlags[MegaTransfer::TYPE_DOWNLOAD] = false;
                megaApi[0]->startDownload(nimported, filename.c_str());
            }
            WaitMillisec(1);
        }
        ASSERT_EQ(onTransferUpdate_progress, onTransferUpdate_filesize);
        ASSERT_EQ(initialOnTranferFinishedCount + 2, onTranferFinishedCount);
        ASSERT_GE(exitresumecount, 6u);
        ASSERT_TRUE(waitForResponse(&mApi[0].transferFlags[MegaTransfer::TYPE_DOWNLOAD], 1)) << "Download cloudraid transfer with pauses failed";
        ASSERT_EQ(MegaError::API_OK, mApi[0].lastError) << "Cannot download the cloudraid file (error: " << mApi[0].lastError << ")";
    }

    ASSERT_TRUE(DebugTestHook::resetForTests()) << "SDK test hooks are not enabled in release mode";
}
#endif


/**
* @brief TEST_F SdkTestCloudraidTransferWithConnectionFailures
*
* Download a cloudraid file but with a connection failing with http errors 404 and 403.   The download should recover from the problems in 5 channel mode
*
*/

#ifdef DEBUG
TEST_F(SdkTest, SdkTestCloudraidTransferWithConnectionFailures)
{
    LOG_info << "___TEST Cloudraid transfers___";
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(2));

    ASSERT_TRUE(DebugTestHook::resetForTests()) << "SDK test hooks are not enabled in release mode";

    std::unique_ptr<MegaNode> rootnode{megaApi[0]->getRootNode()};

    ASSERT_NO_FATAL_FAILURE(importPublicLink(0, "https://mega.nz/#!zAJnUTYD!8YE5dXrnIEJ47NdDfFEvqtOefhuDMphyae0KY5zrhns", rootnode.get()));
    MegaHandle imported_file_handle = mApi[0].h;
    std::unique_ptr<MegaNode> nimported{megaApi[0]->getNodeByHandle(imported_file_handle)};


    string filename = DOTSLASH "cloudraid_downloaded_file.sdktest";
    deleteFile(filename.c_str());

    // set up for 404 and 403 errors
    // smaller chunk sizes so we can get plenty of pauses
    DebugTestHook::countdownTo404 = 5;
    DebugTestHook::countdownTo403 = 12;
#ifdef MEGASDK_DEBUG_TEST_HOOKS_ENABLED
    globalMegaTestHooks.onHttpReqPost = DebugTestHook::onHttpReqPost404Or403;
    globalMegaTestHooks.onSetIsRaid = DebugTestHook::onSetIsRaid_morechunks;
#endif

    // plain cloudraid download
    {
        onTransferUpdate_progress = 0;
        onTransferUpdate_filesize = 0;
        mApi[0].transferFlags[MegaTransfer::TYPE_DOWNLOAD] = false;
        megaApi[0]->startDownload(nimported.get(), filename.c_str());

        ASSERT_TRUE(waitForResponse(&mApi[0].transferFlags[MegaTransfer::TYPE_DOWNLOAD], 180)) << "Cloudraid download with 404 and 403 errors time out (180 seconds)";
        ASSERT_EQ(MegaError::API_OK, mApi[0].lastError) << "Cannot download the cloudraid file (error: " << mApi[0].lastError << ")";
        ASSERT_GE(onTransferUpdate_filesize, 0u);
        ASSERT_TRUE(onTransferUpdate_progress == onTransferUpdate_filesize);
        ASSERT_LT(DebugTestHook::countdownTo404, 0);
        ASSERT_LT(DebugTestHook::countdownTo403, 0);
    }


    ASSERT_TRUE(DebugTestHook::resetForTests()) << "SDK test hooks are not enabled in release mode";
}
#endif


/**
* @brief TEST_F SdkTestCloudraidTransferWithConnectionFailures
*
* Download a cloudraid file but with a connection failing with http errors 404 and 403.   The download should recover from the problems in 5 channel mode
*
*/

#ifdef DEBUG
TEST_F(SdkTest, SdkTestCloudraidTransferWithSingleChannelTimeouts)
{
    LOG_info << "___TEST Cloudraid transfers___";
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(2));

    ASSERT_TRUE(DebugTestHook::resetForTests()) << "SDK test hooks are not enabled in release mode";

    std::unique_ptr<MegaNode> rootnode{megaApi[0]->getRootNode()};

    ASSERT_NO_FATAL_FAILURE(importPublicLink(0, "https://mega.nz/#!zAJnUTYD!8YE5dXrnIEJ47NdDfFEvqtOefhuDMphyae0KY5zrhns", rootnode.get()));
    MegaHandle imported_file_handle = mApi[0].h;
    std::unique_ptr<MegaNode> nimported{megaApi[0]->getNodeByHandle(imported_file_handle)};


    string filename = DOTSLASH "cloudraid_downloaded_file.sdktest";
    deleteFile(filename.c_str());

    // set up for 404 and 403 errors
    // smaller chunk sizes so we can get plenty of pauses
    DebugTestHook::countdownToTimeout = 15;
#ifdef MEGASDK_DEBUG_TEST_HOOKS_ENABLED
    globalMegaTestHooks.onHttpReqPost = DebugTestHook::onHttpReqPostTimeout;
    globalMegaTestHooks.onSetIsRaid = DebugTestHook::onSetIsRaid_morechunks;
#endif

    // plain cloudraid download
    {
        onTransferUpdate_progress = 0;
        onTransferUpdate_filesize = 0;
        mApi[0].transferFlags[MegaTransfer::TYPE_DOWNLOAD] = false;
        megaApi[0]->startDownload(nimported.get(), filename.c_str());

        ASSERT_TRUE(waitForResponse(&mApi[0].transferFlags[MegaTransfer::TYPE_DOWNLOAD], 180)) << "Cloudraid download with timeout errors timed out (180 seconds)";
        ASSERT_EQ(MegaError::API_OK, mApi[0].lastError) << "Cannot download the cloudraid file (error: " << mApi[0].lastError << ")";
        ASSERT_GE(onTransferUpdate_filesize, 0u);
        ASSERT_EQ(onTransferUpdate_progress, onTransferUpdate_filesize);
        ASSERT_LT(DebugTestHook::countdownToTimeout, 0);
    }
    ASSERT_TRUE(DebugTestHook::resetForTests()) << "SDK test hooks are not enabled in release mode";
}
#endif



/**
* @brief TEST_F SdkTestOverquotaNonCloudraid
*
* Induces a simulated overquota error during a conventional download.  Confirms the download stops, pauses, and resumes.
*
*/

#ifdef DEBUG
TEST_F(SdkTest, SdkTestOverquotaNonCloudraid)
{
    LOG_info << "___TEST SdkTestOverquotaNonCloudraid";
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(2));

    ASSERT_TRUE(DebugTestHook::resetForTests()) << "SDK test hooks are not enabled in release mode";

    // make a file to download, and upload so we can pull it down
    std::unique_ptr<MegaNode> rootnode{megaApi[0]->getRootNode()};
    deleteFile(UPFILE);
    createFile(UPFILE, true);
    mApi[0].transferFlags[MegaTransfer::TYPE_UPLOAD] = false;
    megaApi[0]->startUpload(UPFILE.c_str(), rootnode.get());
    ASSERT_TRUE(waitForResponse(&mApi[0].transferFlags[MegaTransfer::TYPE_UPLOAD], 600))
        << "Upload transfer failed after " << 600 << " seconds";
    std::unique_ptr<MegaNode> n1{megaApi[0]->getNodeByHandle(mApi[0].h)};
    ASSERT_NE(n1.get(), ((::mega::MegaNode *)NULL));

    // set up to simulate 509 error
    DebugTestHook::isRaid = false;
    DebugTestHook::isRaidKnown = false;
    DebugTestHook::countdownToOverquota = 3;
    #ifdef MEGASDK_DEBUG_TEST_HOOKS_ENABLED
    globalMegaTestHooks.onHttpReqPost = DebugTestHook::onHttpReqPost509;
    globalMegaTestHooks.onSetIsRaid = DebugTestHook::onSetIsRaid;
    #endif

    // download - we should see a 30 second pause for 509 processing in the middle
    string filename2 = DOTSLASH + DOWNFILE;
    deleteFile(filename2);
    mApi[0].transferFlags[MegaTransfer::TYPE_DOWNLOAD] = false;
    megaApi[0]->startDownload(n1.get(), filename2.c_str());

    // get to 30 sec pause point
    second_timer t;
    while (t.elapsed() < 30 && DebugTestHook::countdownToOverquota >= 0)
    {
        WaitMillisec(1000);
    }
    ASSERT_TRUE(DebugTestHook::isRaidKnown);
    ASSERT_FALSE(DebugTestHook::isRaid);

    // ok so now we should see no more http requests sent for 30 seconds. Test 20 for reliable testing
    int originalcount = DebugTestHook::countdownToOverquota;
    second_timer t2;
    while (t2.elapsed() < 20)
    {
        WaitMillisec(1000);
    }
    ASSERT_TRUE(DebugTestHook::countdownToOverquota == originalcount);

    // Now wait for the file to finish

    ASSERT_TRUE(waitForResponse(&mApi[0].transferFlags[MegaTransfer::TYPE_DOWNLOAD], 600))
        << "Download transfer failed after " << maxTimeout << " seconds";
    ASSERT_EQ(MegaError::API_OK, mApi[0].lastError) << "Cannot download the file (error: " << mApi[0].lastError << ")";

    ASSERT_LT(DebugTestHook::countdownToOverquota, 0);
    ASSERT_LT(DebugTestHook::countdownToOverquota, originalcount);  // there should have been more http activity after the wait

    ASSERT_TRUE(DebugTestHook::resetForTests()) << "SDK test hooks are not enabled in release mode";
}
#endif


/**
* @brief TEST_F SdkTestOverquotaNonCloudraid
*
* use the hooks to simulate an overquota condition while running a raid download transfer, and check the handling
*
*/

#ifdef DEBUG
TEST_F(SdkTest, SdkTestOverquotaCloudraid)
{
    LOG_info << "___TEST SdkTestOverquotaCloudraid";
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(2));

    ASSERT_TRUE(DebugTestHook::resetForTests()) << "SDK test hooks are not enabled in release mode";

    ASSERT_NO_FATAL_FAILURE(importPublicLink(0, "https://mega.nz/#!zAJnUTYD!8YE5dXrnIEJ47NdDfFEvqtOefhuDMphyae0KY5zrhns", megaApi[0]->getRootNode()));
    MegaHandle imported_file_handle = mApi[0].h;
    MegaNode *nimported = megaApi[0]->getNodeByHandle(imported_file_handle);

    // set up to simulate 509 error
    DebugTestHook::isRaid = false;
    DebugTestHook::isRaidKnown = false;
    DebugTestHook::countdownToOverquota = 8;
    #ifdef MEGASDK_DEBUG_TEST_HOOKS_ENABLED
    globalMegaTestHooks.onHttpReqPost = DebugTestHook::onHttpReqPost509;
    globalMegaTestHooks.onSetIsRaid = DebugTestHook::onSetIsRaid;
    #endif

    // download - we should see a 30 second pause for 509 processing in the middle
    string filename2 = DOTSLASH + DOWNFILE;
    deleteFile(filename2);
    mApi[0].transferFlags[MegaTransfer::TYPE_DOWNLOAD] = false;
    megaApi[0]->startDownload(nimported, filename2.c_str());

    // get to 30 sec pause point
    second_timer t;
    while (t.elapsed() < 30 && DebugTestHook::countdownToOverquota >= 0)
    {
        WaitMillisec(1000);
    }
    ASSERT_TRUE(DebugTestHook::isRaidKnown);
    ASSERT_TRUE(DebugTestHook::isRaid);

    // ok so now we should see no more http requests sent for 30 seconds.  Test 20 for reliablilty
    int originalcount = DebugTestHook::countdownToOverquota;
    second_timer t2;
    while (t2.elapsed() < 20)
    {
        WaitMillisec(1000);
    }
    ASSERT_EQ(DebugTestHook::countdownToOverquota, originalcount);

    // Now wait for the file to finish

    ASSERT_TRUE(waitForResponse(&mApi[0].transferFlags[MegaTransfer::TYPE_DOWNLOAD], 600))
        << "Download transfer failed after " << maxTimeout << " seconds";
    ASSERT_EQ(MegaError::API_OK, mApi[0].lastError) << "Cannot download the file (error: " << mApi[0].lastError << ")";

    ASSERT_LT(DebugTestHook::countdownToOverquota, 0);
    ASSERT_LT(DebugTestHook::countdownToOverquota, originalcount);  // there should have been more http activity after the wait

    ASSERT_TRUE(DebugTestHook::resetForTests()) << "SDK test hooks are not enabled in release mode";
}
#endif


struct CheckStreamedFile_MegaTransferListener : public MegaTransferListener
{
    typedef ::mega::byte byte;

    size_t reserved;
    size_t receiveBufPos;
    size_t file_start_offset;
    byte* receiveBuf;
    bool completedSuccessfully;
    bool completedUnsuccessfully;
    MegaError* completedUnsuccessfullyError;
    byte* compareDecryptedData;
    bool comparedEqual;


    CheckStreamedFile_MegaTransferListener(size_t receiveStartPoint, size_t receiveSizeExpected, byte* fileCompareData)
        : reserved(0)
        , receiveBufPos(0)
        , file_start_offset(0)
        , receiveBuf(NULL)
        , completedSuccessfully(false)
        , completedUnsuccessfully(false)
        , completedUnsuccessfullyError(NULL)
        , compareDecryptedData(fileCompareData)
        , comparedEqual(true)
    {
        file_start_offset = receiveStartPoint;
        reserved = receiveSizeExpected;
        receiveBuf = new byte[reserved];
        compareDecryptedData = fileCompareData;
    }

    ~CheckStreamedFile_MegaTransferListener()
    {
        delete[] receiveBuf;
    }

    void onTransferStart(MegaApi *api, MegaTransfer *transfer) override
    {
    }
    void onTransferFinish(MegaApi* api, MegaTransfer *transfer, MegaError* error) override
    {
        if (error && error->getErrorCode() != API_OK)
        {
            ((error->getErrorCode() == API_EARGS && reserved == 0) ? completedSuccessfully : completedUnsuccessfully) = true;
            completedUnsuccessfullyError = error->copy();
        }
        else
        {
            if (0 != memcmp(receiveBuf, compareDecryptedData + file_start_offset, receiveBufPos))
                comparedEqual = false;
            completedSuccessfully = true;
        }
    }
    void onTransferUpdate(MegaApi *api, MegaTransfer *transfer) override
    {
    }
    void onTransferTemporaryError(MegaApi *api, MegaTransfer * /*transfer*/, MegaError* error) override
    {
        ostringstream msg;
        msg << "onTransferTemporaryError: " << (error ? error->getErrorString() : "NULL") << endl;
        api->log(MegaApi::LOG_LEVEL_WARNING, msg.str().c_str());
    }
    bool onTransferData(MegaApi *api, MegaTransfer *transfer, char *buffer, size_t size) override
    {
        assert(receiveBufPos + size <= reserved);
        memcpy(receiveBuf + receiveBufPos, buffer, size);
        receiveBufPos += size;

        if (0 != memcmp(receiveBuf, compareDecryptedData + file_start_offset, receiveBufPos))
            comparedEqual = false;

        return true;
    }
};


CheckStreamedFile_MegaTransferListener* StreamRaidFilePart(MegaApi* megaApi, m_off_t start, m_off_t end, bool raid, bool smallpieces, MegaNode* raidFileNode, MegaNode*nonRaidFileNode, ::mega::byte* filecomparedata)
{
    assert(raidFileNode && nonRaidFileNode);
    LOG_info << "stream test ---------------------------------------------------" << start << " to " << end << "(len " << end - start << ") " << (raid ? " RAID " : " non-raid ") << (raid ? (smallpieces ? " smallpieces " : "normalpieces") : "");

#ifdef MEGASDK_DEBUG_TEST_HOOKS_ENABLED
    globalMegaTestHooks.onSetIsRaid = smallpieces ? &DebugTestHook::onSetIsRaid_smallchunks10 : NULL;
#endif

    CheckStreamedFile_MegaTransferListener* p = new CheckStreamedFile_MegaTransferListener(size_t(start), size_t(end - start), filecomparedata);
    megaApi->setStreamingMinimumRate(0);
    megaApi->startStreaming(raid ? raidFileNode : nonRaidFileNode, start, end - start, p);
    return p;
}



/**
* @brief TEST_F SdkCloudraidStreamingSoakTest
*
* Stream random portions of the well-known file for 10 minutes, while randomly varying
*       raid / non-raid
*       front/end/middle  (especial attention to first and last raidlines, and varying start/end within a raidline)
*       large piece / small piece
*       small raid chunk sizes (so small pieces of file don't just load in one request per connection) / normal sizes
*
*/


TEST_F(SdkTest, SdkCloudraidStreamingSoakTest)
{
    LOG_info << "___TEST SdkCloudraidStreamingSoakTest";
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(2));

#ifdef MEGASDK_DEBUG_TEST_HOOKS_ENABLED
    ASSERT_TRUE(DebugTestHook::resetForTests()) << "SDK test hooks are not enabled in release mode";
#endif

    // ensure we have our standard raid test file
    ASSERT_NO_FATAL_FAILURE(importPublicLink(0, "https://mega.nz/#!zAJnUTYD!8YE5dXrnIEJ47NdDfFEvqtOefhuDMphyae0KY5zrhns", std::unique_ptr<MegaNode>{megaApi[0]->getRootNode()}.get()));
    MegaHandle imported_file_handle = mApi[0].h;
    MegaNode *nimported = megaApi[0]->getNodeByHandle(imported_file_handle);

    MegaNode *rootnode = megaApi[0]->getRootNode();

    // get the file, and upload as non-raid
    string filename2 = DOTSLASH + DOWNFILE;
    deleteFile(filename2);

    mApi[0].transferFlags[MegaTransfer::TYPE_DOWNLOAD] = false;
    megaApi[0]->startDownload(nimported, filename2.c_str());
    ASSERT_TRUE(waitForResponse(&mApi[0].transferFlags[MegaTransfer::TYPE_DOWNLOAD])) << "Setup transfer failed after " << maxTimeout << " seconds";
    ASSERT_EQ(MegaError::API_OK, mApi[0].lastError) << "Cannot download the initial file (error: " << mApi[0].lastError << ")";

    char raidchar = 0;
    char nonraidchar = 'M';

    string filename3 = filename2;
    incrementFilename(filename3);
    filename3 += ".neverseenbefore";
    deleteFile(filename3);
    copyFile(filename2, filename3);
    {
        fstream fs(filename3.c_str(), ios::in | ios::out | ios::binary);
        raidchar = (char)fs.get();
        fs.seekg(0);
        fs.put('M');  // we have to edit the file before upload, as Mega is too clever and will skip actual upload otherwise
        fs.flush();
    }

    // actual upload
    mApi[0].transferFlags[MegaTransfer::TYPE_UPLOAD] = false;
    megaApi[0]->startUpload(filename3.data(), rootnode);
    waitForResponse(&mApi[0].transferFlags[MegaTransfer::TYPE_UPLOAD]);

    ASSERT_EQ(MegaError::API_OK, mApi[0].lastError) << "Cannot upload a test file (error: " << mApi[0].lastError << ")";

    MegaNode *nonRaidNode = megaApi[0]->getNodeByHandle(mApi[0].h);

    int64_t filesize = getFilesize(filename2);
    std::ifstream compareDecryptedFile(filename2.c_str(), ios::binary);
    std::vector<::mega::byte> compareDecryptedData(static_cast<size_t>(filesize));
    compareDecryptedFile.read((char*)compareDecryptedData.data(), filesize);

    m_time_t starttime = m_time();
    int seconds_to_test_for = 60; //gRunningInCI ? 60 : 60 * 10;

    // ok loop for 10 minutes  (one munite under jenkins)
    srand(unsigned(starttime));
    int randomRunsDone = 0;
    m_off_t randomRunsBytes = 0;
    for (; m_time() - starttime < seconds_to_test_for; ++randomRunsDone)
    {

        int testtype = rand() % 10;
        int smallpieces = rand() % 2;
        int nonraid = rand() % 4 == 1;

        compareDecryptedData[0] = ::mega::byte(nonraid ? nonraidchar : raidchar);

        m_off_t start = 0, end = 0;

        if (testtype < 3)  // front of file
        {
            start = std::max<int>(0, rand() % 5 * 10240 - 1024);
            end = start + rand() % 5 * 10240;
        }
        else if (testtype == 3)  // within 1, 2, or 3 raidlines
        {
            start = std::max<int>(0, rand() % 5 * 10240 - 1024);
            end = start + rand() % (3 * RAIDLINE);
        }
        else if (testtype < 8) // end of file
        {
            end = std::min<m_off_t>(32620740, 32620740 + RAIDLINE - rand() % (2 * RAIDLINE));
            start = end - rand() % 5 * 10240;
        }
        else if (testtype == 8) // 0 size [seems this is not allowed at intermediate layer now - EARGS]
        {
            start = rand() % 32620740;
            end = start;
        }
        else // decent piece of the file
        {
            int pieceSize = 50000; //gRunningInCI ? 50000 : 5000000;
            start = rand() % pieceSize;
            int n = pieceSize / (smallpieces ? 100 : 1);
            end = start + n + rand() % n;
        }

        // seems 0 size not allowed now - make sure we get at least 1 byte
        if (start == end)
        {
            if (start > 0) start -= 1;
            else end += 1;
        }
        randomRunsBytes += end - start;

        LOG_info << "beginning stream test, " << start << " to " << end << "(len " << end - start << ") " << (nonraid ? " non-raid " : " RAID ") << (!nonraid ? (smallpieces ? " smallpieces " : "normalpieces") : "");

        CheckStreamedFile_MegaTransferListener* p = StreamRaidFilePart(megaApi[0].get(), start, end, !nonraid, smallpieces, nimported, nonRaidNode, compareDecryptedData.data());

        for (unsigned i = 0; p->comparedEqual; ++i)
        {
            WaitMillisec(100);
            if (p->completedUnsuccessfully)
            {
                ASSERT_FALSE(p->completedUnsuccessfully) << " on random run " << randomRunsDone << ", download failed: " << start << " to " << end << ", "
                    << (nonraid?"nonraid":"raid") <<  ", " << (smallpieces?"small pieces":"normal size pieces")
                    << ", reported error: " << (p->completedUnsuccessfullyError ? p->completedUnsuccessfullyError->getErrorCode() : 0)
                    << " " << (p->completedUnsuccessfullyError ? p->completedUnsuccessfullyError->getErrorString() : "NULL");
                break;
            }
            else if (p->completedSuccessfully)
            {
                break;
            }
            else if (i > maxTimeout * 10)
            {
                ASSERT_TRUE(i <= maxTimeout * 10) << "download took too long, more than " << maxTimeout << " seconds.  Is the free transfer quota exhausted?";
                break;
            }
        }
        ASSERT_TRUE(p->comparedEqual);

        delete p;

    }

    ASSERT_GT(randomRunsDone, 10 /*(gRunningInCI ? 10 : 100)*/ );

    ostringstream msg;
    msg << "Streaming test downloaded " << randomRunsDone << " samples of the file from random places and sizes, " << randomRunsBytes << " bytes total" << endl;
    megaApi[0]->log(MegaApi::LOG_LEVEL_DEBUG, msg.str().c_str());

    delete nimported;
    delete nonRaidNode;
    delete rootnode;

#ifdef MEGASDK_DEBUG_TEST_HOOKS_ENABLED
    ASSERT_TRUE(DebugTestHook::resetForTests()) << "SDK test hooks are not enabled in release mode";
#endif
}

TEST_F(SdkTest, SdkRecentsTest)
{
    LOG_info << "___TEST SdkRecentsTest___";
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(2));

    MegaNode *rootnode = megaApi[0]->getRootNode();

    deleteFile(UPFILE);
    deleteFile(DOWNFILE);

    string filename1 = UPFILE;
    createFile(filename1, false);
    auto err = synchronousStartUpload(0, filename1.c_str(), rootnode);
    ASSERT_EQ(MegaError::API_OK, err) << "Cannot upload a test file (error: " << err << ")";

    ofstream f(filename1);
    f << "update";
    f.close();

    err = synchronousStartUpload(0, filename1.c_str(), rootnode);
    ASSERT_EQ(MegaError::API_OK, err) << "Cannot upload an updated test file (error: " << err << ")";

    synchronousCatchup(0);

    string filename2 = DOWNFILE;
    createFile(filename2, false);

    err = synchronousStartUpload(0, filename2.c_str(), rootnode);
    ASSERT_EQ(MegaError::API_OK, err) << "Cannot upload a test file2 (error: " << err << ")";

    ofstream f2(filename2);
    f2 << "update";
    f2.close();

    err = synchronousStartUpload(0, filename2.c_str(), rootnode);
    ASSERT_EQ(MegaError::API_OK, err) << "Cannot upload an updated test file2 (error: " << err << ")";

    synchronousCatchup(0);


    std::unique_ptr<MegaRecentActionBucketList> buckets{megaApi[0]->getRecentActions(1, 10)};

    ostringstream logMsg;
    for (int i = 0; i < buckets->size(); ++i)
    {
        logMsg << "bucket " << to_string(i) << endl;
        megaApi[0]->log(MegaApi::LOG_LEVEL_INFO, logMsg.str().c_str());
        auto bucket = buckets->get(i);
        for (int j = 0; j < buckets->get(i)->getNodes()->size(); ++j)
        {
            auto node = bucket->getNodes()->get(j);
            logMsg << node->getName() << " " << node->getCreationTime() << " " << bucket->getTimestamp() << " " << bucket->getParentHandle() << " " << bucket->isUpdate() << " " << bucket->isMedia() << endl;
            megaApi[0]->log(MegaApi::LOG_LEVEL_DEBUG, logMsg.str().c_str());
        }
    }

    ASSERT_TRUE(buckets != nullptr);
    ASSERT_TRUE(buckets->size() > 0);
    ASSERT_TRUE(buckets->get(0)->getNodes()->size() > 1);
    ASSERT_EQ(DOWNFILE, string(buckets->get(0)->getNodes()->get(0)->getName()));
    ASSERT_EQ(UPFILE, string(buckets->get(0)->getNodes()->get(1)->getName()));
}

TEST_F(SdkTest, SdkMediaUploadRequestURL)
{
    LOG_info << "___TEST MediaUploadRequestURL___";
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    // Create a "media upload" instance
    int apiIndex = 0;
    std::unique_ptr<MegaBackgroundMediaUpload> req(MegaBackgroundMediaUpload::createInstance(megaApi[apiIndex].get()));

    // Request a media upload URL
    int64_t dummyFileSize = 123456;
    auto err = synchronousMediaUploadRequestURL(apiIndex, dummyFileSize, req.get(), nullptr);
    ASSERT_EQ(MegaError::API_OK, err) << "Cannot request media upload URL (error: " << err << ")";

    // Get the generated media upload URL
    std::unique_ptr<char[]> url(req->getUploadURL());
    ASSERT_NE(nullptr, url) << "Got NULL media upload URL";
    ASSERT_NE(0, *url.get()) << "Got empty media upload URL";
}

TEST_F(SdkTest, SdkSimpleCommands)
{
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));
    LOG_info << "___TEST SimpleCommands___";

    // fetchTimeZone() test
    auto err = synchronousFetchTimeZone(0);
    ASSERT_EQ(MegaError::API_OK, err) << "Fetch time zone failed (error: " << err << ")";
    ASSERT_TRUE(mApi[0].tzDetails && mApi[0].tzDetails->getNumTimeZones()) << "Invalid Time Zone details"; // some simple validation

    // getUserEmail() test
    std::unique_ptr<MegaUser> user(megaApi[0]->getMyUser());
    ASSERT_TRUE(!!user); // some simple validation

    err = synchronousGetUserEmail(0, user->getHandle());
    ASSERT_EQ(MegaError::API_OK, err) << "Get user email failed (error: " << err << ")";
    ASSERT_NE(mApi[0].email.find('@'), std::string::npos); // some simple validation

    // cleanRubbishBin() test (accept both success and already empty statuses)
    err = synchronousCleanRubbishBin(0);
    ASSERT_TRUE(err == MegaError::API_OK || err == MegaError::API_ENOENT) << "Clean rubbish bin failed (error: " << err << ")";

    // getExtendedAccountDetails()
    err = synchronousGetExtendedAccountDetails(0, true);
    ASSERT_EQ(MegaError::API_OK, err) << "Get extended account details failed (error: " << err << ")";
    ASSERT_TRUE(!!mApi[0].accountDetails) << "Invalid accout details"; // some simple validation

    // killSession()
    gSessionIDs[0] = "invalid";
    int numSessions = mApi[0].accountDetails->getNumSessions();
    for (int i = 0; i < numSessions; ++i)
    {
        // look for current session
        std::unique_ptr<MegaAccountSession> session(mApi[0].accountDetails->getSession(i));
        if (session->isCurrent())
        {
            err = synchronousKillSession(0, session->getHandle());
            ASSERT_EQ(MegaError::API_OK, err) << "Kill session failed for current session (error: " << err << ")";

            break;
        }
    }

    err = synchronousKillSession(0, INVALID_HANDLE);
    ASSERT_EQ(MegaError::API_ESID, err) << "Kill session for unknown sessions shoud fail with API_ESID (error: " << err << ")";

    // getMiscFlags() -- not logged in
    logout(0);
    err = synchronousGetMiscFlags(0);
    ASSERT_EQ(MegaError::API_OK, err) << "Get misc flags failed (error: " << err << ")";
}

TEST_F(SdkTest, SdkGetCountryCallingCodes)
{
    LOG_info << "___TEST SdkGetCountryCallingCodes___";
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(2));

    getCountryCallingCodes();
    ASSERT_NE(nullptr, stringListMap);
    ASSERT_GT(stringListMap->size(), 0);
    // sanity check a few country codes
    const MegaStringList* const nz = stringListMap->get("NZ");
    ASSERT_NE(nullptr, nz);
    ASSERT_EQ(1, nz->size());
    ASSERT_EQ(0, strcmp("64", nz->get(0)));
    const MegaStringList* const de = stringListMap->get("DE");
    ASSERT_NE(nullptr, de);
    ASSERT_EQ(1, de->size());
    ASSERT_EQ(0, strcmp("49", de->get(0)));
}

TEST_F(SdkTest, SdkGetRegisteredContacts)
{
    LOG_info << "___TEST SdkGetRegisteredContacts___";
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(2));

    const std::string js1 = "+0000000010";
    const std::string js2 = "+0000000011";
    const std::map<std::string, std::string> contacts{
        {js1, "John Smith"}, // sms verified
        {js2, "John Smith"}, // sms verified
        {"+640", "John Smith"}, // not sms verified
    };
    getRegisteredContacts(contacts);
    ASSERT_NE(nullptr, stringTable);
    ASSERT_EQ(2, stringTable->size());

    // repacking and sorting result
    using row_t = std::tuple<std::string, std::string, std::string>;
    std::vector<row_t> table;
    for (int i = 0; i < stringTable->size(); ++i)
    {
        const MegaStringList* const stringList = stringTable->get(i);
        ASSERT_EQ(3, stringList->size());
        table.emplace_back(stringList->get(0), stringList->get(1), stringList->get(2));
    }

    std::sort(table.begin(), table.end(), [](const row_t& lhs, const row_t& rhs)
                                          {
                                              return std::get<0>(lhs) < std::get<0>(rhs);
                                          });

    // Check johnsmith1
    ASSERT_EQ(js1, std::get<0>(table[0])); // eud
    ASSERT_GT(std::get<1>(table[0]).size(), 0u); // id
    ASSERT_EQ(js1, std::get<2>(table[0])); // ud

    // Check johnsmith2
    ASSERT_EQ(js2, std::get<0>(table[1])); // eud
    ASSERT_GT(std::get<1>(table[1]).size(), 0u); // id
    ASSERT_EQ(js2, std::get<2>(table[1])); // ud
}

TEST_F(SdkTest, EscapesReservedCharacters)
{
    // Set up necessary accounts.
    getAccountsForTest(1);

    const string input = "%\r\\/:?\"<>|*";

    // Generate expected string.
    ostringstream osstream;

    for (auto& character : input)
    {
        osstream << "%"
                 << std::hex
                 << std::setfill('0')
                 << std::setw(2)
                 << +character;
    }

    // Escape input string.
    const char *output =
      megaApi[0]->escapeFsIncompatible(input.c_str());

    // Was the string escaped as expected?
    ASSERT_NE(output, nullptr);
    ASSERT_STREQ(output, osstream.str().c_str());

    delete[] output;
}

TEST_F(SdkTest, EscapesReservedCharactersOnDownload)
{
    // a/b/c!.txt
    static const string fileName = "a%2fb%2fc!.txt";

    // Set up necessary accounts.
    getAccountsForTest(1);

    // For convenience.
    MegaApi* api = megaApi[0].get();

    // Get root node.
    unique_ptr<MegaNode> root(api->getRootNode());
    ASSERT_NE(root, nullptr);
    
    // Create file to upload containing escaped characters.
    deleteFile(fileName);
    createFile(fileName);

    // Upload the file.
    {
        TransferTracker tracker(api);
        api->startUpload(fileName.c_str(), root.get(), &tracker);
        ASSERT_EQ(API_OK, tracker.waitForResult());
    }
    
    // Delete the file, we're done with it.
    deleteFile(fileName);

    // Check file exists in the cloud.
    root.reset(api->authorizeNode(root.get()));
    ASSERT_NE(root, nullptr);

    MegaNodeList* children = root->getChildren();
    ASSERT_NE(children, nullptr);

    MegaNode* child = children->get(0);
    ASSERT_NE(child, nullptr);
    ASSERT_STREQ(child->getName(), "a/b/c!.txt");

    // Download the file.
    {
        TransferTracker tracker(api);

        string targetPath = fs::current_path().u8string();
        targetPath.append(FileSystemAccess::getPathSeparator());

        api->startDownload(child, targetPath.c_str(), &tracker);
        ASSERT_EQ(API_OK, tracker.waitForResult());
    }

    // Was the filename correctly escaped on download?
    ASSERT_TRUE(fileexists(fileName));
    deleteFile(fileName);
}

TEST_F(SdkTest, UnescapesReservedCharacters)
{
    // Set up necessary accounts.
    getAccountsForTest(1);

    const string input = "\\/:?\"<>|*%5a%21";

    // Escape input string.
    const char* escaped =
      megaApi[0]->escapeFsIncompatible(input.c_str());

    ASSERT_NE(escaped, nullptr);

    // Unescape the escaped string.
    const char* unescaped =
      megaApi[0]->unescapeFsIncompatible(escaped);

    // Release escaped, we're done with it.
    delete[] escaped;

    // Was the string unescaped as expected?
    ASSERT_NE(unescaped, nullptr);
    ASSERT_STREQ(input.c_str(), unescaped);

    delete[] unescaped;
}

TEST_F(SdkTest, UnescapesReservedCharactersOnUpload)
{
    // a/b/c!.txt
    static const string fileName = "a%2fb%2fc!.txt";

    // Set up necessary accounts.
    getAccountsForTest(1);

    // For convenience.
    MegaApi* api = megaApi[0].get();

    // Get root node.
    unique_ptr<MegaNode> root(api->getRootNode());
    ASSERT_NE(root, nullptr);
    
    // Create file to upload containing escaped characters.
    deleteFile(fileName);
    createFile(fileName);

    // Upload the file.
    TransferTracker tracker(api);
    api->startUpload(fileName.c_str(), root.get(), &tracker);
    ASSERT_EQ(API_OK, tracker.waitForResult());
    
    // Delete the file, we're done with it.
    deleteFile(fileName);

    // Check if the file's name was correctly unescaped.
    root.reset(api->authorizeNode(root.get()));
    ASSERT_NE(root, nullptr);

    MegaNodeList* children = root->getChildren();
    ASSERT_NE(children, nullptr);

    MegaNode* child = children->get(0);
    ASSERT_NE(child, nullptr);
    ASSERT_STREQ(child->getName(), "a/b/c!.txt");
}

TEST_F(SdkTest, RecursiveUploadWithLogout)
{
    LOG_info << "___TEST RecursiveUploadWithLogout___";
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(2));

    // this one used to cause a double-delete

    // make new folders (and files) in the local filesystem - approx 90
    fs::path p = fs::current_path() / "uploadme_mega_auto_test_sdk";
    if (fs::exists(p))
    {
        fs::remove_all(p);
    }
    fs::create_directories(p);
    ASSERT_TRUE(buildLocalFolders(p.u8string().c_str(), "newkid", 3, 2, 10));

    // start uploading
    // uploadListener may have to live after this function exits if the logout test below fails
    auto uploadListener = std::make_shared<TransferTracker>(megaApi[0].get());
    uploadListener->selfDeleteOnFinalCallback = uploadListener;

    megaApi[0]->startUpload(p.u8string().c_str(), std::unique_ptr<MegaNode>{megaApi[0]->getRootNode()}.get(), uploadListener.get());
    WaitMillisec(500);

    // logout while the upload (which consists of many transfers) is ongoing
    gSessionIDs[0].clear();
    ASSERT_EQ(API_OK, doRequestLogout(0));
    int result = uploadListener->waitForResult();
    ASSERT_TRUE(result == API_EACCESS || result == API_EINCOMPLETE);
}

TEST_F(SdkTest, DISABLED_RecursiveDownloadWithLogout)
{
    LOG_info << "___TEST RecursiveDownloadWithLogout";
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(2));

    // this one used to cause a double-delete

    // make new folders (and files) in the local filesystem - approx 130 - we must upload in order to have something to download
    fs::path uploadpath = fs::current_path() / "uploadme_mega_auto_test_sdk";
    fs::path downloadpath = fs::current_path() / "downloadme_mega_auto_test_sdk";

    std::error_code ec;
    fs::remove_all(uploadpath, ec);
    fs::remove_all(downloadpath, ec);
    ASSERT_TRUE(!fs::exists(uploadpath));
    ASSERT_TRUE(!fs::exists(downloadpath));
    fs::create_directories(uploadpath);
    fs::create_directories(downloadpath);

    ASSERT_TRUE(buildLocalFolders(uploadpath.u8string().c_str(), "newkid", 3, 2, 10));

    // upload all of those
    TransferTracker uploadListener(megaApi[0].get());
    megaApi[0]->startUpload(uploadpath.u8string().c_str(), std::unique_ptr<MegaNode>{megaApi[0]->getRootNode()}.get(), &uploadListener);
    ASSERT_EQ(API_OK, uploadListener.waitForResult());

    // ok now try the download
    TransferTracker downloadListener(megaApi[0].get());
    megaApi[0]->startDownload(megaApi[0]->getNodeByPath("/uploadme_mega_auto_test_sdk"), downloadpath.u8string().c_str(), &downloadListener);
    WaitMillisec(1000);
    ASSERT_TRUE(downloadListener.started);
    ASSERT_TRUE(!downloadListener.finished);

    // logout while the download (which consists of many transfers) is ongoing

    ASSERT_EQ(API_OK, doRequestLogout(0));

    int result = downloadListener.waitForResult();
    ASSERT_TRUE(result == API_EACCESS || result == API_EINCOMPLETE);
    fs::remove_all(uploadpath, ec);
    fs::remove_all(downloadpath, ec);
}

#ifdef ENABLE_SYNC
TEST_F(SdkTest, SyncResumptionAfterFetchNodes)
{
    LOG_info << "___TEST SyncResumptionAfterFetchNodes___";
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(2));

    // This test has several issues:
    // 1. Remote nodes may not be committed to the sctable database in time for fetchnodes which
    //    then fails adding syncs because the remotes are missing. For this reason we wait until
    //    we receive the EVENT_COMMIT_DB event after transferring the nodes.
    // 2. Syncs are deleted some time later leading to error messages (like local fingerprint mismatch)
    //    if we don't wait for long enough after we get called back. A sync only gets flagged but
    //    is deleted later.

    const std::string session = dumpSession();

    const fs::path basePath = "SyncResumptionAfterFetchNodes";
    const auto sync1Path = fs::current_path() / basePath / "sync1"; // stays active
    const auto sync2Path = fs::current_path() / basePath / "sync2"; // will be made inactive
    const auto sync3Path = fs::current_path() / basePath / "sync3"; // will be deleted
    const auto sync4Path = fs::current_path() / basePath / "sync4"; // stays active

    auto cleanUp = [this, &basePath]()
    {
        std::error_code ignoredEc;
        fs::remove_all(basePath, ignoredEc);

        std::unique_ptr<MegaNode> baseNode{megaApi[0]->getNodeByPath(("/" + basePath.u8string()).c_str())};
        if (baseNode)
        {
            RequestTracker removeTracker(megaApi[0].get());
            megaApi[0]->remove(baseNode.get(), &removeTracker);
            ASSERT_EQ(API_OK, removeTracker.waitForResult());
        }
    };

    cleanUp();

    fs::create_directories(sync1Path);
    fs::create_directories(sync2Path);
    fs::create_directories(sync3Path);
    fs::create_directories(sync4Path);

    {
        std::lock_guard<std::mutex> lock{lastEventMutex};
        lastEvent.reset();
        // we're assuming we're not getting any unrelated db commits while the transfer is running
    }

    // transfer the folder and its subfolders
    TransferTracker uploadListener(megaApi[0].get());
    megaApi[0]->startUpload(basePath.u8string().c_str(), megaApi[0]->getRootNode(), &uploadListener);
    ASSERT_EQ(API_OK, uploadListener.waitForResult());

    // loop until we get a commit to the sctable to ensure we cached the new remote nodes
    for (;;)
    {
        {
            std::lock_guard<std::mutex> lock{lastEventMutex};
            if (lastEvent && lastEvent->getType() == MegaEvent::EVENT_COMMIT_DB)
            {
                // we're assuming this event is the event for the whole batch of nodes
                break;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{100});
    }

    auto megaNode = [this, &basePath](const std::string& p)
    {
        const auto path = "/" + basePath.u8string() + "/" + p;
        return std::unique_ptr<MegaNode>{megaApi[0]->getNodeByPath(path.c_str())};
    };

    auto localFp = [this, &megaNode](const fs::path& p)
    {
        auto node = megaNode(p.filename().u8string());
        auto sync = std::unique_ptr<MegaSync>{megaApi[0]->getSyncByNode(node.get())};
        return sync->getLocalFingerprint();
    };

    auto syncFolder = [this, &megaNode](const fs::path& p) -> int
    {
        RequestTracker syncTracker(megaApi[0].get());
        auto node = megaNode(p.filename().u8string());
        megaApi[0]->syncFolder(p.u8string().c_str(), node.get(), &syncTracker);
        EXPECT_EQ(API_OK, syncTracker.waitForResult());

        return syncTracker.request->getTransferTag();
    };

    auto disableSync = [this, &megaNode](const fs::path& p)
    {
        RequestTracker syncTracker(megaApi[0].get());
        auto node = megaNode(p.filename().u8string());
        megaApi[0]->disableSync(node.get(), &syncTracker);
        ASSERT_EQ(API_OK, syncTracker.waitForResult());
    };

    auto disableSyncByTag = [this](int tag)
    {
        RequestTracker syncTracker(megaApi[0].get());
        megaApi[0]->disableSync(tag, &syncTracker);
        ASSERT_EQ(API_OK, syncTracker.waitForResult());
    };

    auto resumeSync = [this](int tag)
    {
        RequestTracker syncTracker(megaApi[0].get());
        megaApi[0]->enableSync(tag, &syncTracker);
        ASSERT_EQ(API_OK, syncTracker.waitForResult());
    };

    auto removeSync = [this, &megaNode](const fs::path& p)
    {
        RequestTracker syncTracker(megaApi[0].get());
        auto node = megaNode(p.filename().u8string());
        megaApi[0]->removeSync(node.get(), &syncTracker);
        ASSERT_EQ(API_OK, syncTracker.waitForResult());
    };

    auto checkSyncOK = [this, &megaNode](const fs::path& p)
    {
        auto node = megaNode(p.filename().u8string());
        //return std::unique_ptr<MegaSync>{megaApi[0]->getSyncByNode(node.get())} != nullptr; //disabled syncs are not OK but foundable
        std::unique_ptr<MegaSync> sync{megaApi[0]->getSyncByNode(node.get())};
        if (!sync) return false;
        return sync->isEnabled();

    };

    auto checkSyncDisabled = [this, &megaNode](const fs::path& p)
    {
        auto node = megaNode(p.filename().u8string());
        std::unique_ptr<MegaSync> sync{megaApi[0]->getSyncByNode(node.get())};
        if (!sync) return false;
        return !sync->isEnabled();
    };


    auto reloginViaSession = [this, &session]()
    {
        locallogout();

        //loginBySessionId(0, session);
        auto tracker = asyncRequestFastLogin(0, session.c_str());
        ASSERT_EQ(API_OK, tracker->waitForResult()) << " Failed to establish a login/session for accout " << 0;
    };

    LOG_verbose << " SyncResumptionAfterFetchNodes : syncying folders";

    int tag1 = syncFolder(sync1Path);  tag1;
    int tag2 = syncFolder(sync2Path);
    int tag3 = syncFolder(sync3Path);  tag3;
    int tag4 = syncFolder(sync4Path);  tag4;

    ASSERT_TRUE(checkSyncOK(sync1Path));
    ASSERT_TRUE(checkSyncOK(sync2Path));
    ASSERT_TRUE(checkSyncOK(sync3Path));
    ASSERT_TRUE(checkSyncOK(sync4Path));

    LOG_verbose << " SyncResumptionAfterFetchNodes : disabling sync by path";
    disableSync(sync2Path);
    LOG_verbose << " SyncResumptionAfterFetchNodes : disabling sync by tag";
    disableSyncByTag(tag4);
    LOG_verbose << " SyncResumptionAfterFetchNodes : removing sync";
    removeSync(sync3Path);

    // wait for the sync removals to actually take place
    std::this_thread::sleep_for(std::chrono::seconds{20});

    ASSERT_TRUE(checkSyncOK(sync1Path));
    ASSERT_TRUE(checkSyncDisabled(sync2Path));
    ASSERT_FALSE(checkSyncOK(sync3Path));
    ASSERT_TRUE(checkSyncDisabled(sync4Path));

    reloginViaSession();

    ASSERT_FALSE(checkSyncOK(sync1Path));
    ASSERT_FALSE(checkSyncOK(sync2Path));
    ASSERT_FALSE(checkSyncOK(sync3Path));
    ASSERT_FALSE(checkSyncOK(sync4Path));

    fetchnodes(0, maxTimeout); // auto-resumes two active syncs

    ASSERT_TRUE(checkSyncOK(sync1Path));
    ASSERT_FALSE(checkSyncOK(sync2Path));
    ASSERT_TRUE(checkSyncDisabled(sync2Path));
    ASSERT_FALSE(checkSyncOK(sync3Path));
    ASSERT_FALSE(checkSyncOK(sync4Path));
    ASSERT_TRUE(checkSyncDisabled(sync4Path));

    // check if we can still resume manually
    LOG_verbose << " SyncResumptionAfterFetchNodes : resuming syncs";
    resumeSync(tag2);
    resumeSync(tag4);

    ASSERT_TRUE(checkSyncOK(sync1Path));
    ASSERT_TRUE(checkSyncOK(sync2Path));
    ASSERT_FALSE(checkSyncOK(sync3Path));
    ASSERT_TRUE(checkSyncOK(sync4Path));

    // check if resumeSync re-activated the sync
    reloginViaSession();

    ASSERT_FALSE(checkSyncOK(sync1Path));
    ASSERT_FALSE(checkSyncOK(sync2Path));
    ASSERT_FALSE(checkSyncOK(sync3Path));
    ASSERT_FALSE(checkSyncOK(sync4Path));

    fetchnodes(0, maxTimeout); // auto-resumes three active syncs

    ASSERT_TRUE(checkSyncOK(sync1Path));
    ASSERT_TRUE(checkSyncOK(sync2Path));
    ASSERT_FALSE(checkSyncOK(sync3Path));
    ASSERT_TRUE(checkSyncOK(sync4Path));

    LOG_verbose << " SyncResumptionAfterFetchNodes : removing syncs";
    removeSync(sync1Path);
    removeSync(sync2Path);
    removeSync(sync4Path);

    // wait for the sync removals to actually take place
    std::this_thread::sleep_for(std::chrono::seconds{5});

    cleanUp();
}
#endif
