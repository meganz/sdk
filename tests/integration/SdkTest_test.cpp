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
#include "stdfs.h"
#include "SdkTest_test.h"
#include "mega/testhooks.h"
#include "megaapi_impl.h"
#include <algorithm>

#define SSTR( x ) static_cast< const std::ostringstream & >( \
        (  std::ostringstream() << std::dec << x ) ).str()


using namespace std;


static const string APP_KEY     = "8QxzVRxD";
static const string PUBLICFILE  = "file.txt";
static const string UPFILE      = "file1.txt";
static const string DOWNFILE    = "file2.txt";
static const string EMPTYFILE   = "empty-file.txt";
static const string AVATARSRC   = "logo.png";
static const string AVATARDST   = "deleteme.png";
static const string IMAGEFILE   = "logo.png";
static const string IMAGEFILE_C = "logo.encrypted.png";
static const string THUMBNAIL   = "logo_thumbnail.png";
static const string PREVIEW     = "logo_preview.png";


MegaFileSystemAccess fileSystemAccess;

template<typename T>
class ScopedValue {
public:
    ScopedValue(T& what, T value)
      : mLastValue(std::move(what))
      , mWhat(what)
    {
        what = std::move(value);
    }

    ~ScopedValue()
    {
        mWhat = std::move(mLastValue);
    }

    MEGA_DISABLE_COPY(ScopedValue)
    MEGA_DEFAULT_MOVE(ScopedValue)

private:
    T mLastValue;
    T& mWhat;
}; // ScopedValue<T>

template<typename T>
ScopedValue<T> makeScopedValue(T& what, T value)
{
    return ScopedValue<T>(what, std::move(value));
}

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
    fs::path p = fs::u8path(fn);
    return fs::exists(p);
#else
    struct stat   buffer;
    return (stat(fn.c_str(), &buffer) == 0);
#endif
}

void copyFile(std::string& from, std::string& to)
{
    LocalPath f = LocalPath::fromAbsolutePath(from);
    LocalPath t = LocalPath::fromAbsolutePath(to);
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

    } else
    {
        std::unique_ptr<DirAccess> da(fileSystemAccess.newdiraccess());
        auto lp = LocalPath::fromAbsolutePath(p);
        if (!da->dopen(&lp, nullptr, false))
        {
            throw std::runtime_error(
                        "Cannot open existing mega API cache folder " + p
                        + " please check permissions or delete it so a new one can be created");
        }
    }
    return p;
}

template<typename Predicate>
bool WaitFor(Predicate&& predicate, unsigned timeoutMs)
{
    unsigned sleepMs = 100;
    unsigned totalMs = 0;

    do
    {
        if (predicate()) return true;

        WaitMillisec(sleepMs);
        totalMs += sleepMs;
    }
    while (totalMs < timeoutMs);

    return false;
}

MegaApi* newMegaApi(const char *appKey, const char *basePath, const char *userAgent, unsigned workerThreadCount)
{
    return new MegaApi(appKey, basePath, userAgent, workerThreadCount);
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

    bool createLocalFile(fs::path path, const char *name, int byteSize = 0)
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
        if (byteSize)
        {
            fs.seekp((byteSize << 10) - 1);
        }
        fs << name;
        return true;
    }
}

std::map<size_t, std::string> gSessionIDs;

void SdkTest::SetUp()
{
    gTestingInvalidArgs = false;
}

void SdkTest::TearDown()
{
    out() << "Test done, teardown starts";
    // do some cleanup

    gTestingInvalidArgs = false;

    LOG_info << "___ Cleaning up test (TearDown()) ___";

    Cleanup();

    releaseMegaApi(1);
    releaseMegaApi(2);
    if (!megaApi.empty() && megaApi[0])
    {
        releaseMegaApi(0);
    }
    out() << "Teardown done, test exiting";
}

void SdkTest::Cleanup()
{
    out() << "Cleaning up accounts";

    deleteFile(UPFILE);
    deleteFile(DOWNFILE);
    deleteFile(PUBLICFILE);
    deleteFile(AVATARDST);

#ifdef ENABLE_SYNC
    std::vector<std::unique_ptr<RequestTracker>> delSyncTrackers;
    for (auto &m : megaApi)
    {
        if (m)
        {
            auto syncs = unique_ptr<MegaSyncList>(m->getSyncs());
            for (int i = syncs->size(); i--; )
            {
                delSyncTrackers.push_back(std::unique_ptr<RequestTracker>(new RequestTracker(m.get())));
                m->removeSync(syncs->get(i)->getBackupId(), delSyncTrackers.back().get());
            }
        }
    }
    // wait for delsyncs to complete:
    for (auto& d : delSyncTrackers) d->waitForResult();
    WaitMillisec(2000);



#endif

    if (!megaApi.empty() && megaApi[0])
    {

        // Remove nodes in Cloud & Rubbish
        purgeTree(std::unique_ptr<MegaNode>{megaApi[0]->getRootNode()}.get(), false);
        purgeTree(std::unique_ptr<MegaNode>{megaApi[0]->getRubbishNode()}.get(), false);
        purgeVaultTree(std::unique_ptr<MegaNode>{megaApi[0]->getVaultNode()}.get());

        // Remove auxiliar contact
        std::unique_ptr<MegaUserList> ul{megaApi[0]->getContacts()};
        for (int i = 0; i < ul->size(); i++)
        {
            removeContact(ul->get(i)->getEmail());
        }
    }

    for (auto nApi = unsigned(megaApi.size()); nApi--; ) if (megaApi[nApi])
    {
        // Remove pending contact requests

        std::unique_ptr<MegaContactRequestList> crl{megaApi[nApi]->getOutgoingContactRequests()};
        for (int i = 0; i < crl->size(); i++)
        {
            MegaContactRequest *cr = crl->get(i);
            synchronousInviteContact(nApi, cr->getTargetEmail(), "Test cleanup removing outgoing contact request", MegaContactRequest::INVITE_ACTION_DELETE);
        }

        crl.reset(megaApi[nApi]->getIncomingContactRequests());
        for (int i = 0; i < crl->size(); i++)
        {
            MegaContactRequest *cr = crl->get(i);
            synchronousReplyContactRequest(nApi, cr, MegaContactRequest::REPLY_ACTION_DENY);
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

bool SdkTest::getApiIndex(MegaApi* api, size_t& apindex)
{
    for (size_t i = 0; i < megaApi.size(); i++)
    {
        if (megaApi[i].get() == api)
        {
            apindex = i;
            return true;
        }
    }

    LOG_warn << "Instance of MegaApi not recognized";  // this can occur during MegaApi deletion due to callbacks on shutdown
    return false;
}

void SdkTest::onRequestFinish(MegaApi *api, MegaRequest *request, MegaError *e)
{
    auto type = request->getType();
    if (type == MegaRequest::TYPE_DELETE)
    {
        return;
    }

    int apiIndex = getApiIndex(api);
    if (apiIndex < 0) return;
    mApi[apiIndex].lastError = e->getErrorCode();

    // there could be a race on these getting set?
    LOG_info << "lastError (by request) for MegaApi " << apiIndex << ": " << mApi[apiIndex].lastError;

    switch(type)
    {

    case MegaRequest::TYPE_GET_ATTR_USER:

        if (mApi[apiIndex].lastError == API_OK)
        {
            if (request->getParamType() == MegaApi::USER_ATTR_DEVICE_NAMES ||
                request->getParamType() == MegaApi::USER_ATTR_DRIVE_NAMES ||
                request->getParamType() == MegaApi::USER_ATTR_ALIAS)
            {
                attributeValue = request->getName() ? request->getName() : "";
            }
            else if (request->getParamType() == MegaApi::USER_ATTR_MY_BACKUPS_FOLDER)
            {
                mApi[apiIndex].lastSyncBackupId = request->getNodeHandle();
            }
            else if (request->getParamType() != MegaApi::USER_ATTR_AVATAR)
            {
                attributeValue = request->getText() ? request->getText() : "";
            }
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
            chatlink.assign(request->getLink());
        }
        break;
#endif

    case MegaRequest::TYPE_CREATE_ACCOUNT:
        if (mApi[apiIndex].lastError == API_OK)
        {
            sid = request->getSessionKey();
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

    case MegaRequest::TYPE_BACKUP_PUT:
        mBackupId = request->getParentHandle();
        break;

    case MegaRequest::TYPE_GET_ATTR_NODE:
        if (mApi[apiIndex].lastError == API_OK)
        {
            mMegaFavNodeList.reset(request->getMegaHandleList()->copy());
        }
        break;

    case MegaRequest::TYPE_GET_PRICING:
        mApi[apiIndex].mMegaPricing.reset(mApi[apiIndex].lastError == API_OK ? request->getPricing() : nullptr);
        mApi[apiIndex].mMegaCurrency.reset(mApi[apiIndex].lastError == API_OK ? request->getCurrency() : nullptr);
            break;

    }

    // set this flag always the latest, since it is used to unlock the wait
    // for requests results, so we want data to be collected first
    mApi[apiIndex].requestFlags[request->getType()] = true;
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
    size_t apiIndex = 0;
    if (getApiIndex(api, apiIndex) && mApi[apiIndex].mOnNodesUpdateCompletion)
    {
        mApi[apiIndex].mOnNodesUpdateCompletion(apiIndex, nodes); // nodes owned by SDK and valid until return
    }
}

void SdkTest::onSetsUpdate(MegaApi* api, MegaSetList* sets)
{
    int apiIndex = getApiIndex(api);
    if (apiIndex < 0 || !sets || !sets->size()) return;

    mApi[apiIndex].setUpdated = true;
}

void SdkTest::onSetElementsUpdate(MegaApi* api, MegaSetElementList* elements)
{
    int apiIndex = getApiIndex(api);
    if (apiIndex < 0 || !elements || !elements->size()) return;

    mApi[apiIndex].setElementUpdated = true;
}

void SdkTest::onContactRequestsUpdate(MegaApi* api, MegaContactRequestList* requests)
{
    int apiIndex = getApiIndex(api);
    if (apiIndex < 0) return;

    mApi[apiIndex].contactRequestUpdated = true;
}

void SdkTest::onUserAlertsUpdate(MegaApi* api, MegaUserAlertList* alerts)
{
    int apiIndex = getApiIndex(api);
    if (apiIndex < 0) return;

    mApi[apiIndex].userAlertsUpdated = true;
    mApi[apiIndex].userAlertList.reset(alerts ? alerts->copy() : nullptr);
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

    ASSERT_EQ(API_OK, mApi[apiIndex].lastError) << "Chat creation failed (error: " << mApi[apiIndex].lastError << ")";
}

#endif

void SdkTest::onEvent(MegaApi*, MegaEvent *event)
{
    std::lock_guard<std::mutex> lock{lastEventMutex};
    lastEvent.reset(event->copy());
    lastEvents.insert(event->getType());
}


void SdkTest::fetchnodes(unsigned int apiIndex, int timeout)
{
    mApi[apiIndex].requestFlags[MegaRequest::TYPE_FETCH_NODES] = false;

    mApi[apiIndex].megaApi->fetchNodes();

    ASSERT_TRUE( waitForResponse(&mApi[apiIndex].requestFlags[MegaRequest::TYPE_FETCH_NODES], timeout) )
            << "Fetchnodes failed after " << timeout  << " seconds";
    ASSERT_EQ(API_OK, mApi[apiIndex].lastError) << "Fetchnodes failed (error: " << mApi[apiIndex].lastError << ")";
}

void SdkTest::logout(unsigned int apiIndex, bool keepSyncConfigs, int timeout)
{
    mApi[apiIndex].requestFlags[MegaRequest::TYPE_LOGOUT] = false;
#ifdef ENABLE_SYNC
    mApi[apiIndex].megaApi->logout(keepSyncConfigs, this);
#else
    mApi[apiIndex].megaApi->logout(this);
#endif
    gSessionIDs[apiIndex] = "invalid";

    EXPECT_TRUE( waitForResponse(&mApi[apiIndex].requestFlags[MegaRequest::TYPE_LOGOUT], timeout) )
            << "Logout failed after " << timeout  << " seconds";

    // if the connection was closed before the response of the request was received, the result is ESID
    if (mApi[apiIndex].lastError == API_ESID) mApi[apiIndex].lastError = API_OK;

    EXPECT_EQ(API_OK, mApi[apiIndex].lastError) << "Logout failed (error: " << mApi[apiIndex].lastError << ")";
}

char* SdkTest::dumpSession()
{
    return megaApi[0]->dumpSession();
}

void SdkTest::locallogout(int timeout)
{
    auto logoutErr = doRequestLocalLogout(0);
    ASSERT_EQ(API_OK, logoutErr) << "Local logout failed (error: " << logoutErr << ")";
}

void SdkTest::resumeSession(const char *session, int timeout)
{
    int apiIndex = 0;
    ASSERT_EQ(API_OK, synchronousFastLogin(apiIndex, session, this)) << "Resume session failed (error: " << mApi[apiIndex].lastError << ")";
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

        ASSERT_EQ(API_OK, result) << "Remove node operation failed (error: " << mApi[apiIndex].lastError << ")";
    }
}

void SdkTest::purgeVaultTree(MegaNode *vault)
{
    std::unique_ptr<MegaNodeList> vc{megaApi[0]->getChildren(vault)};
    if (vc->size())
    {
        if (MegaNode* myBackups = vc->get(0))
        {
            std::unique_ptr<MegaNodeList> devices{megaApi[0]->getChildren(myBackups)};
            for (int i = 0; i < devices->size(); ++i)
            {
                std::unique_ptr<MegaNodeList> backupRoots{megaApi[0]->getChildren(devices->get(i))};
                for (int j = 0; j < backupRoots->size(); ++j)
                {
                    RequestTracker rt(megaApi[0].get());
                    megaApi[0]->moveOrRemoveDeconfiguredBackupNodes(backupRoots->get(j)->getHandle(), INVALID_HANDLE, &rt);
                    rt.waitForResult();
                }
            }
        }
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
                if (megaApi.size() > 1 && megaApi[1] && megaApi[1]->isLoggedIn())
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

void SdkTest::onNodesUpdateCheck(size_t apiIndex, MegaHandle target, MegaNodeList* nodes, int change)
{
    // if change == -1 this method just checks if we have received onNodesUpdate for the node specified in target
    ASSERT_TRUE(nodes && mApi.size() > apiIndex && (target != INVALID_HANDLE
            || (target == INVALID_HANDLE && change == MegaNode::CHANGE_TYPE_NEW)));
    for (int i = 0; i < nodes->size(); i++)
    {
        MegaNode* n = nodes->get(i);
        if ((n->getHandle() == target && (n->hasChanged(change) || change == -1))
                || (target == INVALID_HANDLE && change == MegaNode::CHANGE_TYPE_NEW && n->hasChanged(change)))
        {
            mApi[apiIndex].nodeUpdated = true;
        }
    }
    ASSERT_EQ (true, mApi[apiIndex].nodeUpdated);
};

bool SdkTest::createFile(string filename, bool largeFile)
{
    fs::path p = fs::u8path(filename);
    std::ofstream file(p,ios::out);

    if (file)
    {
        int limit = 2000;

        // create a file large enough for long upload/download times (5-10MB)
        if (largeFile)
            limit = 1000000 + rand() % 1000000;

        //limit = 5494065 / 5;

        for (int i = 0; i < limit; i++)
        {
            file << "test ";
        }

        file.close();
    }

    return file.good();
}

int64_t SdkTest::getFilesize(string filename)
{
    struct stat stat_buf;
    int rc = stat(filename.c_str(), &stat_buf);

    return rc == 0 ? int64_t(stat_buf.st_size) : int64_t(-1);
}

void SdkTest::deleteFile(string filename)
{
    fs::path p = fs::u8path(filename);
    std::error_code ignoredEc;
    fs::remove(p, ignoredEc);
}

void SdkTest::getAccountsForTest(unsigned howMany)
{
    assert(howMany > 0 && howMany <= 3);
    out() << "Test setting up for " << howMany << " accounts ";

    megaApi.resize(howMany);
    mApi.resize(howMany);

    std::vector<std::unique_ptr<RequestTracker>> trackers;
    trackers.resize(howMany);

    for (unsigned index = 0; index < howMany; ++index)
    {
        if (const char *buf = getenv(envVarAccount[index].c_str()))
        {
            mApi[index].email.assign(buf);
        }
        ASSERT_LT((size_t) 0, mApi[index].email.length()) << "Set test account " << index << " username at the environment variable $" << envVarAccount[index];

        if (const char* buf = getenv(envVarPass[index].c_str()))
        {
            mApi[index].pwd.assign(buf);
        }
        ASSERT_LT((size_t) 0, mApi[index].pwd.length()) << "Set test account " << index << " password at the environment variable $" << envVarPass[index];

        megaApi[index].reset(newMegaApi(APP_KEY.c_str(), megaApiCacheFolder(index).c_str(), USER_AGENT.c_str(), unsigned(THREADS_PER_MEGACLIENT)));
        mApi[index].megaApi = megaApi[index].get();

        // helps with restoring logging after tests that fiddle with log level
        mApi[index].megaApi->setLogLevel(MegaApi::LOG_LEVEL_MAX);

        megaApi[index]->setLoggingName(to_string(index).c_str());
        megaApi[index]->addListener(this);    // TODO: really should be per api

        if (!gResumeSessions || gSessionIDs[index].empty() || gSessionIDs[index] == "invalid")
        {
            out() << "Logging into account " << index;
            trackers[index] = asyncRequestLogin(index, mApi[index].email.c_str(), mApi[index].pwd.c_str());
        }
        else
        {
            out() << "Resuming session for account " << index;
            trackers[index] = asyncRequestFastLogin(index, gSessionIDs[index].c_str());
        }
    }

    // wait for logins to complete:
    bool anyLoginFailed = false;
    for (unsigned index = 0; index < howMany; ++index)
    {
        auto loginResult = trackers[index]->waitForResult();
        EXPECT_EQ(API_OK, loginResult) << " Failed to establish a login/session for account " << index;
        if (loginResult != API_OK) anyLoginFailed = true;
        else {
            gSessionIDs[index] = "invalid"; // default
            if (gResumeSessions && megaApi[index]->isLoggedIn() == FULLACCOUNT)
            {
                if (auto p = unique_ptr<char[]>(megaApi[index]->dumpSession()))
                {
                    gSessionIDs[index] = p.get();
                }
            }
        }
    }
    ASSERT_FALSE(anyLoginFailed);

    // perform parallel fetchnodes for each
    for (unsigned index = 0; index < howMany; ++index)
    {
        out() << "Fetching nodes for account " << index;
        trackers[index] = asyncRequestFetchnodes(index);
    }

    // wait for fetchnodes to complete:
    bool anyFetchnodesFailed = false;
    for (unsigned index = 0; index < howMany; ++index)
    {
        auto fetchnodesResult = trackers[index]->waitForResult();
        EXPECT_EQ(API_OK, fetchnodesResult) << " Failed to fetchnodes for account " << index;
        anyFetchnodesFailed = anyFetchnodesFailed || (fetchnodesResult != API_OK);
    }
    ASSERT_FALSE(anyFetchnodesFailed);

    // In case the last test exited without cleaning up (eg, debugging etc)
    Cleanup();
    out() << "Test setup done, test starts";
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
                ASSERT_NO_FATAL_FAILURE( logout(apiIndex, false, maxTimeout) );
            else
                ASSERT_NO_FATAL_FAILURE( locallogout(apiIndex) );
        }

        megaApi[apiIndex].reset();
        mApi[apiIndex].megaApi = NULL;
    }
}

void SdkTest::inviteContact(unsigned apiIndex, string email, string message, int action)
{
    ASSERT_EQ(API_OK, synchronousInviteContact(apiIndex, email.c_str(), message.c_str(), action)) << "Contact invitation failed";
}

void SdkTest::replyContact(MegaContactRequest *cr, int action)
{
    int apiIndex = 1;
    ASSERT_EQ(API_OK, synchronousReplyContactRequest(apiIndex, cr, action)) << "Contact reply failed";
}

void SdkTest::removeContact(string email, int timeout)
{
    int apiIndex = 0;
    MegaUser *u = megaApi[apiIndex]->getContact(email.c_str());
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

    ASSERT_EQ(API_OK, result) << "Contact deletion of " << email << " failed on api " << apiIndex;

    delete u;
}

void SdkTest::shareFolder(MegaNode *n, const char *email, int action, int timeout)
{
    int apiIndex = 0;
    ASSERT_EQ(API_OK, synchronousShare(apiIndex, n, email, action)) << "Folder sharing failed" << "User: " << email << " Action: " << action;
}

string SdkTest::createPublicLink(unsigned apiIndex, MegaNode *n, m_time_t expireDate, int timeout, bool isFreeAccount, bool writable, bool megaHosted)
{
    RequestTracker rt(megaApi[apiIndex].get());

    mApi[apiIndex].megaApi->exportNode(n, expireDate, writable, megaHosted, &rt);

    rt.waitForResult();

    if (!expireDate || !isFreeAccount)
    {
        EXPECT_EQ(API_OK, rt.result.load()) << "Public link creation failed (error: " << mApi[apiIndex].lastError << ")";
    }
    else
    {
        bool res = API_OK != rt.result && rt.result != -999;
        EXPECT_TRUE(res) << "Public link creation with expire time on free account (" << mApi[apiIndex].email << ") succeed, and it mustn't";
    }

    return rt.getLink();
}

MegaHandle SdkTest::importPublicLink(unsigned apiIndex, string link, MegaNode *parent)
{
    RequestTracker rt(megaApi[apiIndex].get());

    mApi[apiIndex].megaApi->importFileLink(link.c_str(), parent, &rt);

    EXPECT_EQ(API_OK, rt.waitForResult()) << "Public link import failed";

    return rt.getNodeHandle();
}

unique_ptr<MegaNode> SdkTest::getPublicNode(unsigned apiIndex, string link)
{
    RequestTracker rt(megaApi[apiIndex].get());

    mApi[apiIndex].megaApi->getPublicNode(link.c_str(), &rt);

    EXPECT_EQ(API_OK, rt.waitForResult()) << "Public link retrieval failed";

    return rt.getPublicMegaNode();
}

MegaHandle SdkTest::removePublicLink(unsigned apiIndex, MegaNode *n)
{
    RequestTracker rt(megaApi[apiIndex].get());

    mApi[apiIndex].megaApi->disableExport(n, &rt);

    EXPECT_EQ(API_OK, rt.waitForResult()) << "Public link removal failed";

    return rt.getNodeHandle();
}

void SdkTest::getContactRequest(unsigned int apiIndex, bool outgoing, int expectedSize)
{
    unique_ptr<MegaContactRequestList> crl;
    unsigned timeoutMs = 8000;

    if (outgoing)
    {
        auto predicate = [&]() {
            crl.reset(mApi[apiIndex].megaApi->getOutgoingContactRequests());
            return crl->size() == expectedSize;
        };

        ASSERT_TRUE(WaitFor(predicate, timeoutMs))
          << "Too many outgoing contact requests in account: "
          << apiIndex;
    }
    else
    {
        auto predicate = [&]() {
            crl.reset(mApi[apiIndex].megaApi->getIncomingContactRequests());
            return crl->size() == expectedSize;
        };

        ASSERT_TRUE(WaitFor(predicate, timeoutMs))
          << "Too many incoming contact requests in account: "
          << apiIndex;
    }

    if (!expectedSize) return;

    mApi[apiIndex].cr.reset(crl->get(0)->copy());
}

MegaHandle SdkTest::createFolder(unsigned int apiIndex, const char *name, MegaNode *parent, int timeout)
{
    RequestTracker tracker(megaApi[apiIndex].get());

    megaApi[apiIndex]->createFolder(name, parent, &tracker);

    if (tracker.waitForResult() != API_OK) return UNDEF;

    return tracker.request->getNodeHandle();
}

void SdkTest::getRegisteredContacts(const std::map<std::string, std::string>& contacts)
{
    int apiIndex = 0;

    auto contactsStringMap = std::unique_ptr<MegaStringMap>{MegaStringMap::createInstance()};
    for  (const auto& pair : contacts)
    {
        contactsStringMap->set(pair.first.c_str(), pair.second.c_str());
    }

    ASSERT_EQ(API_OK, synchronousGetRegisteredContacts(apiIndex, contactsStringMap.get(), this)) << "Get registered contacts failed";
}

void SdkTest::getCountryCallingCodes(const int timeout)
{
    int apiIndex = 0;
    ASSERT_EQ(API_OK, synchronousGetCountryCallingCodes(apiIndex, this)) << "Get country calling codes failed";
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
    ASSERT_EQ(API_OK, mApi[apiIndex].lastError) << "User attribute setup failed (error: " << mApi[apiIndex].lastError << ")";
}

void SdkTest::getUserAttribute(MegaUser *u, int type, int timeout, int apiIndex)
{
    mApi[apiIndex].requestFlags[MegaRequest::TYPE_GET_ATTR_USER] = false;

    int err;
    if (type == MegaApi::USER_ATTR_AVATAR)
    {
        err = synchronousGetUserAvatar(apiIndex, u, AVATARDST.c_str());
    }
    else
    {
        err = synchronousGetUserAttribute(apiIndex, u, type);
    }
    bool result = (err == API_OK) || (err == API_ENOENT);
    ASSERT_TRUE(result) << "User attribute retrieval failed (error: " << err << ")";
}

void SdkTest::synchronousMediaUpload(unsigned int apiIndex, int64_t fileSize, const char* filename, const char* fileEncrypted, const char* fileOutput, const char* fileThumbnail = nullptr, const char* filePreview = nullptr)
{
    // Create a "media upload" instance
    std::unique_ptr<MegaBackgroundMediaUpload> req(MegaBackgroundMediaUpload::createInstance(megaApi[apiIndex].get()));

    // Request a media upload URL
    auto err = synchronousMediaUploadRequestURL(apiIndex, fileSize, req.get(), nullptr);
    ASSERT_EQ(API_OK, err) << "Cannot request media upload URL (error: " << err << ")";

    // Get the generated media upload URL
    std::unique_ptr<char[]> url(req->getUploadURL());
    ASSERT_NE(nullptr, url) << "Got NULL media upload URL";
    ASSERT_NE('\0', url[0]) << "Got empty media upload URL";

    // encrypt file contents and get URL suffix
    std::unique_ptr<char[]> suffix(req->encryptFile(filename, 0, &fileSize, fileEncrypted, false));
    ASSERT_NE(nullptr, suffix) << "Got NULL suffix after encryption";

    std::unique_ptr<char[]> fingerprint(megaApi[apiIndex]->getFingerprint(fileEncrypted));
    std::unique_ptr<char[]> fingerprintOrig(megaApi[apiIndex]->getFingerprint(filename));

    // PUT thumbnail and preview if params exists
    if (fileThumbnail)
    {
        ASSERT_EQ(true, megaApi[apiIndex]->createThumbnail(filename, fileThumbnail));
        ASSERT_EQ(API_OK, doPutThumbnail(apiIndex, req.get(), fileThumbnail)) << "ERROR putting thumbnail";
    }
    if (filePreview)
    {
        ASSERT_EQ(true, megaApi[apiIndex]->createPreview(filename, filePreview));
        ASSERT_EQ(API_OK, doPutPreview(apiIndex, req.get(), filePreview)) << "ERROR putting preview";
    }

    std::unique_ptr<MegaNode> rootnode(megaApi[apiIndex]->getRootNode());

    string finalurl(url.get());
    if (suffix) finalurl.append(suffix.get());

    string binaryUploadToken;
    synchronousHttpPOSTFile(finalurl, fileEncrypted, binaryUploadToken);

    ASSERT_NE(binaryUploadToken.size(), 0u);
    ASSERT_GT(binaryUploadToken.size(), 3u) << "POST failed, fa server error: " << binaryUploadToken;

    std::unique_ptr<char[]> base64UploadToken(megaApi[0]->binaryToBase64(binaryUploadToken.data(), binaryUploadToken.length()));

    err = synchronousMediaUploadComplete(apiIndex, req.get(), fileOutput, rootnode.get(), fingerprint.get(), fingerprintOrig.get(), base64UploadToken.get(), nullptr);

    ASSERT_EQ(API_OK, err) << "Cannot complete media upload (error: " << err << ")";
}

string getLinkFromMailbox(const string& exe,         // Python
                          const string& script,      // email_processor.py
                          const string& realAccount, // user
                          const string& realPswd,    // password for user@host.domain
                          const string& toAddr,      // user+testnewaccount@host.domain
                          const string& intent,      // confirm / delete
                          const chrono::system_clock::time_point& timeOfEmail)
{
    string command = exe + " \"" + script + "\" \"" + realAccount + "\" \"" + realPswd + "\" \"" + toAddr + "\" " + intent;
    string output;

    // Wait for the link to be sent
    constexpr int deltaMs = 10000; // 10 s interval to check for the email
    for (int i = 0; ; i += deltaMs)
    {
        WaitMillisec(deltaMs);

        // get time interval to look for emails, add some seconds to account for the connection and other delays
        const auto& attemptTime = std::chrono::system_clock::now();
        auto timeSinceEmail = std::chrono::duration_cast<std::chrono::seconds>(attemptTime - timeOfEmail).count() + 20;
        output = runProgram(command + ' ' + to_string(timeSinceEmail), PROG_OUTPUT_TYPE::TEXT); // Run Python script
        if (!output.empty() || i > 180000 / deltaMs) // 3 minute maximum wait
            break;
    }

    // Print whatever was fetched from the mailbox
    LOG_debug << "Link from email (" << intent << "):" << (output.empty() ? "[empty]" : output);

    // Validate the link
    constexpr char expectedLinkPrefix[] = "https://";
    return output.substr(0, sizeof(expectedLinkPrefix) - 1) == expectedLinkPrefix ?
           output : string();
}

string getUniqueAlias()
{
    // use n random chars
    int n = 4;
    string alias;
    auto t = std::time(nullptr);
    srand((unsigned int)t);
    for (int i = 0; i < n; ++i)
    {
        alias += static_cast<char>('a' + rand() % 26);
    }

    // add a timestamp
    auto tm = *std::localtime(&t);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y%m%d%H%M%S");
    alias += oss.str();

    return alias;
}

std::string getCurrentTimestamp(bool includeDate);

///////////////////////////__ Tests using SdkTest __//////////////////////////////////

/**
 * @brief TEST_F SdkTestCreateAccount
 *
 * It tests the creation of a new account for a random user.
 *  - Create account and send confirmation link
 *  - Logout and resume the create-account process
 *  - Extract confirmation link from the mailbox
 *  - Use the link to confirm the account
 *  - Login to the new account
 *  - Request cancel account link
 *  - Extract cancel account link from the mailbox
 *  - Use the link to cancel the account
 */
TEST_F(SdkTest, SdkTestCreateAccount)
{
    LOG_info << "___TEST Create account___";

    // Make sure the new account details have been set up
    const char* bufRealEmail = getenv("MEGA_REAL_EMAIL"); // user@host.domain
    const char* bufRealPswd = getenv("MEGA_REAL_PWD"); // email password of user@host.domain
    const char* bufScript = getenv("MEGA_LINK_EXTRACT_SCRIPT"); // full path to the link extraction script
    ASSERT_TRUE(bufRealEmail && bufRealPswd && bufScript) <<
        "MEGA_REAL_EMAIL, MEGA_REAL_PWD, MEGA_LINK_EXTRACT_SCRIPT env vars must all be defined";

    // Test that Python was installed
    string pyExe = "python";
    const string pyOpt = " -V";
    const string pyExpected = "Python 3.";
    string output = runProgram(pyExe + pyOpt, PROG_OUTPUT_TYPE::TEXT);  // Python -V
    if (output.substr(0, pyExpected.length()) != pyExpected)
    {
        pyExe += "3";
        output = runProgram(pyExe + pyOpt, PROG_OUTPUT_TYPE::TEXT);  // Python3 -V
        ASSERT_EQ(pyExpected, output.substr(0, pyExpected.length())) << "Python 3 was not found.";
    }
    LOG_debug << "Using " << output;

    ASSERT_NO_FATAL_FAILURE(getAccountsForTest());

    const string realEmail(bufRealEmail); // user@host.domain
    auto pos = realEmail.find('@');
    const string realAccount = realEmail.substr(0, pos); // user
    const string newTestAcc = realAccount + '+' +
                              mApi[0].email.substr(0, mApi[0].email.find("@")) + '+' +
                              getUniqueAlias() + realEmail.substr(pos); // user+testUser+rand20210919@host.domain
    LOG_info << "Using Mega account " << newTestAcc;
    const char* newTestPwd = "TestPswd!@#$"; // maybe this should be logged too

    // save point in time for account init
    auto timeOfEmail = std::chrono::system_clock::now();

    // Create an ephemeral session internally and send a confirmation link to email
    ASSERT_EQ(API_OK, synchronousCreateAccount(0, newTestAcc.c_str(), newTestPwd, "MyFirstname", "MyLastname"));

    // Logout from ephemeral session and resume session
    ASSERT_NO_FATAL_FAILURE( locallogout() );
    ASSERT_EQ(API_OK, synchronousResumeCreateAccount(0, sid.c_str()));

    // Get confirmation link from the email
    output = getLinkFromMailbox(pyExe, bufScript, realAccount, bufRealPswd, newTestAcc, MegaClient::confirmLinkPrefix(), timeOfEmail);
    ASSERT_FALSE(output.empty()) << "Confirmation link was not found.";

    // Use confirmation link
    ASSERT_EQ(API_OK, synchronousConfirmSignupLink(0, output.c_str(), newTestPwd));

    // Create a separate megaApi instance
    std::unique_ptr<MegaApi> testMegaApi(newMegaApi(APP_KEY.c_str(), megaApiCacheFolder((int)mApi.size()).c_str(), USER_AGENT.c_str(), unsigned(THREADS_PER_MEGACLIENT)));
    testMegaApi->setLogLevel(MegaApi::LOG_LEVEL_MAX);
    testMegaApi->setLoggingName(to_string(mApi.size()).c_str());

    // Login to the new account
    auto loginTracker = ::mega::make_unique<RequestTracker>(testMegaApi.get());
    testMegaApi->login(newTestAcc.c_str(), newTestPwd, loginTracker.get());
    ASSERT_EQ(API_OK, loginTracker->waitForResult()) << " Failed to login to account " << newTestAcc.c_str();

    // fetchnodes // needed internally to fill in user details, including email
    auto fetchnodesTracker = ::mega::make_unique<RequestTracker>(testMegaApi.get());
    testMegaApi->fetchNodes(fetchnodesTracker.get());
    ASSERT_EQ(API_OK, fetchnodesTracker->waitForResult()) << " Failed to fetchnodes for account " << newTestAcc.c_str();

    // Request cancel account link
    timeOfEmail = std::chrono::system_clock::now();
    auto cancelLinkTracker = ::mega::make_unique<RequestTracker>(testMegaApi.get());
    testMegaApi->cancelAccount(cancelLinkTracker.get());
    ASSERT_EQ(API_OK, cancelLinkTracker->waitForResult()) << " Failed to request cancel link for account " << newTestAcc.c_str();

    // Get cancel account link from the mailbox
    output = getLinkFromMailbox(pyExe, bufScript, realAccount, bufRealPswd, newTestAcc, "delete", timeOfEmail);
    ASSERT_FALSE(output.empty()) << "Cancel account link was not found.";

    // Use cancel account link
    auto useCancelLinkTracker = ::mega::make_unique<RequestTracker>(testMegaApi.get());
    testMegaApi->confirmCancelAccount(output.c_str(), newTestPwd, useCancelLinkTracker.get());
    // Allow API_ESID beside API_OK, due to the race between sc and cs channels
    ASSERT_PRED3([](int t, int v1, int v2) { return t == v1 || t == v2; }, useCancelLinkTracker->waitForResult(), API_OK, API_ESID)
        << " Failed to confirm cancel account " << newTestAcc.c_str();
}

/**
 * @brief TEST_F SdkTestCreateEphmeralPlusPlusAccount
 *
 * It tests the creation of a new account for a random user.
 *  - Create account
 *  - Check existence for Welcome pdf
 */
TEST_F(SdkTest, SdkTestCreateEphmeralPlusPlusAccount)
{
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    LOG_info << "___TEST Create ephemeral account plus plus___";

    // Create an ephemeral plus plus session internally
    synchronousCreateEphemeralAccountPlusPlus(0, "MyFirstname", "MyLastname");
    ASSERT_EQ(API_OK, mApi[0].lastError) << "Account creation failed (error: " << mApi[0].lastError << ")";

    // Wait, for 10 seconds, for the pdf to be imported
    std::unique_ptr<MegaNode> rootnode{ megaApi[0]->getRootNode() };
    constexpr int deltaMs = 200;
    for (int i = 0; i <= 10000 && !megaApi[0]->getNumChildren(rootnode.get()); i += deltaMs)
    {
        WaitMillisec(deltaMs);
    }

    // Get children of rootnode
    std::unique_ptr<MegaNodeList> children{ megaApi[0]->getChildren(rootnode.get()) };

    // Test that there is only one file, with .pdf extension
    EXPECT_EQ(megaApi[0]->getNumChildren(rootnode.get()), children->size()) << "Wrong number of child nodes";
    ASSERT_EQ(1, children->size()) << "Wrong number of children nodes found";
    const char* name = children->get(0)->getName();
    size_t len = name ? strlen(name) : 0;
    ASSERT_TRUE(len > 4 && !strcasecmp(name + len - 4, ".pdf"));
    LOG_info << "Welcome pdf: " << name;

    // Logout from ephemeral plus plus session and resume session
    ASSERT_NO_FATAL_FAILURE(locallogout());
    synchronousResumeCreateAccountEphemeralPlusPlus(0, sid.c_str());
    ASSERT_EQ(API_OK, mApi[0].lastError) << "Account creation failed after resume (error: " << mApi[0].lastError << ")";

    gSessionIDs[0] = "invalid";
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

TEST_F(SdkTest, SdkTestKillSession)
{
    // Convenience.
    using MegaAccountSessionPtr =
      std::unique_ptr<MegaAccountSession>;

    // Make sure environment variable are restored.
    auto accounts = makeScopedValue(envVarAccount, string_vector(2, "MEGA_EMAIL"));
    auto passwords = makeScopedValue(envVarPass, string_vector(2, "MEGA_PWD"));

    // prevent reusing a session for the wrong client
    gSessionIDs[1] = "invalid";

    // Get two sessions for the same account.
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(2));

    // Confirm they really are using the same account
    unique_ptr<char[]> client0userhandle(megaApi[0]->getMyUserHandle());
    unique_ptr<char[]> client1userhandle(megaApi[1]->getMyUserHandle());
    ASSERT_EQ(string(client0userhandle.get()), string(client1userhandle.get()));

    // Make sure the sessions aren't reused.
    gSessionIDs[0] = "invalid";
    gSessionIDs[1] = "invalid";

    // Get our hands on the second client's session.
    MegaHandle sessionHandle = UNDEF;

    auto result = synchronousGetExtendedAccountDetails(1, true);
    ASSERT_EQ(result, API_OK)
      << "GetExtendedAccountDetails failed (error: "
      << result
      << ")";

    unique_ptr<char[]> client0session(dumpSession());

    int matches = 0;
    for (int i = 0; i < mApi[1].accountDetails->getNumSessions(); )
    {
        MegaAccountSessionPtr session;

        session.reset(mApi[1].accountDetails->getSession(i++));

        if (session->isAlive() && session->isCurrent())
        {
            sessionHandle = session->getHandle();
            matches += 1;
        }
    }

    if (matches > 1)
    {
        // kill the other sessions so that we succeed on the next test run
        synchronousKillSession(0, INVALID_HANDLE);
    }

    ASSERT_EQ(matches, 1) << "There were more alive+current sessions for client 1 than expected. Those should have been killed now for the next run";

    // Were we able to retrieve the second client's session handle?
    ASSERT_NE(sessionHandle, UNDEF)
      << "Unable to get second client's session handle.";

    // Kill the second client's session (via the first.)
    result = synchronousKillSession(0, sessionHandle);
    ASSERT_EQ(result, API_OK)
      << "Unable to kill second client's session (error: "
      << result
      << ")";

    // Wait for the second client to become logged out (to confirm it does).
    ASSERT_TRUE(WaitFor([&]()
                        {
                            return mApi[1].megaApi->isLoggedIn()  == 0;
                        },
                        80 * 1000));

    // Log out the primary account.
    logout(0, false, maxTimeout);
    gSessionIDs[0] = "invalid";
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
    ASSERT_TRUE(createFile(filename1, false)) << "Couldn't create " << UPFILE;

    FileFingerprint ffp;
    {
        auto fsa = makeFsAccess();
        auto fa = fsa->newfileaccess();
        ASSERT_TRUE(fa->fopen(LocalPath::fromAbsolutePath(filename1.c_str())));
        ASSERT_TRUE(ffp.genfingerprint(fa.get()));
    }

    MegaHandle uploadedNode = UNDEF;
    ASSERT_EQ(MegaError::API_OK, doStartUpload(0, &uploadedNode, filename1.c_str(),
                                                       rootnode.get(),
                                                       nullptr /*fileName*/,
                                                       ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
                                                       nullptr /*appData*/,
                                                       false   /*isSourceTemporary*/,
                                                       false   /*startFirst*/,
                                                       nullptr /*cancelToken*/)) << "Cannot upload a test file";

    std::unique_ptr<MegaNode> n1(megaApi[0]->getNodeByHandle(uploadedNode));
    ASSERT_TRUE(!!n1) << "Cannot initialize test scenario (error: " << mApi[0].lastError << ")";


    // ___ also try upload with the overload that specifies an mtime ___

    auto test_mtime = m_time() - 3600; // one hour ago

    MegaHandle uploadedNode_mtime = UNDEF;
    ASSERT_EQ(MegaError::API_OK, doStartUpload(0, &uploadedNode_mtime, filename1.c_str(),
        rootnode.get(),
        (filename1+"_mtime").c_str(),  // upload to a different name
        test_mtime, // specify mtime
        nullptr /*appData*/,
        false   /*isSourceTemporary*/,
        false   /*startFirst*/,
        nullptr /*cancelToken*/)) << "Cannot upload a test file 2";

    std::unique_ptr<MegaNode> n1_mtime(megaApi[0]->getNodeByHandle(uploadedNode_mtime));
    ASSERT_TRUE(!!n1_mtime) << "Cannot initialize test scenario (error: " << mApi[0].lastError << ")";
    ASSERT_EQ(test_mtime, n1_mtime->getModificationTime()) << "Could not set the mtime of a file upload";
    ASSERT_EQ(ffp.mtime, n1->getModificationTime()) << "Normal file upload did not get the right mtime of the file";


    // ___ Set invalid duration of a node ___

    gTestingInvalidArgs = true;

    ASSERT_EQ(API_EARGS, synchronousSetNodeDuration(0, n1.get(), -14)) << "Unexpected error setting invalid node duration";

    gTestingInvalidArgs = false;


    // ___ Set duration of a node ___

    ASSERT_EQ(API_OK, synchronousSetNodeDuration(0, n1.get(), 929734)) << "Cannot set node duration";


    megaApi[0]->log(2, "test postlog", __FILE__, __LINE__);

    n1.reset(megaApi[0]->getNodeByHandle(n1->getHandle()));
    ASSERT_EQ(929734, n1->getDuration()) << "Duration value does not match";


    // ___ Reset duration of a node ___

    ASSERT_EQ(API_OK, synchronousSetNodeDuration(0, n1.get(), -1)) << "Cannot reset node duration";

    n1.reset(megaApi[0]->getNodeByHandle(n1->getHandle()));
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
    ASSERT_EQ(API_OK, doSetNodeDuration(0, n1.get(), 929734)) << "Cannot set node duration";
    n1.reset(megaApi[0]->getNodeByHandle(n1->getHandle()));

    ASSERT_STREQ("value13", n1->getCustomAttr("custom1"));
    ASSERT_STREQ("value23", n1->getCustomAttr("custom2"));
    ASSERT_STREQ("value33", n1->getCustomAttr("custom3"));


    // ___ Set invalid coordinates of a node (out of range) ___

    gTestingInvalidArgs = true;

    ASSERT_EQ(API_EARGS, synchronousSetNodeCoordinates(0, n1.get(), -1523421.8719987255814, +6349.54)) << "Unexpected error setting invalid node coordinates";


    // ___ Set invalid coordinates of a node (out of range) ___

    ASSERT_EQ(API_EARGS, synchronousSetNodeCoordinates(0, n1.get(), -160.8719987255814, +49.54)) << "Unexpected error setting invalid node coordinates";


    // ___ Set invalid coordinates of a node (out of range) ___

    ASSERT_EQ(API_EARGS, synchronousSetNodeCoordinates(0, n1.get(), MegaNode::INVALID_COORDINATE, +69.54)) << "Unexpected error trying to reset only one coordinate";

    gTestingInvalidArgs = false;

    // ___ Set coordinates of a node ___

    double lat = -51.8719987255814;
    double lon = +179.54;

    ASSERT_EQ(API_OK, synchronousSetNodeCoordinates(0, n1.get(), lat, lon)) << "Cannot set node coordinates";

    n1.reset(megaApi[0]->getNodeByHandle(n1->getHandle()));

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

    ASSERT_EQ(API_OK, synchronousSetNodeCoordinates(0, n1.get(), 0, 0)) << "Cannot set node coordinates";

    n1.reset(megaApi[0]->getNodeByHandle(n1->getHandle()));

    // do same conversions to lose the same precision
    buf = int(((lat + 90) / 180) * 0xFFFFFF);
    res = -90 + 180 * (double) buf / 0xFFFFFF;

    ASSERT_EQ(res, n1->getLatitude()) << "Latitude value does not match";
    ASSERT_EQ(lon, n1->getLongitude()) << "Longitude value does not match";


    // ___ Set coordinates of a node to border values (90,180) ___

    lat = 90;
    lon = 180;

    ASSERT_EQ(API_OK, synchronousSetNodeCoordinates(0, n1.get(), lat, lon)) << "Cannot set node coordinates";

    n1.reset(megaApi[0]->getNodeByHandle(n1->getHandle()));

    ASSERT_EQ(lat, n1->getLatitude()) << "Latitude value does not match";
    bool value_ok = ((n1->getLongitude() == lon) || (n1->getLongitude() == -lon));
    ASSERT_TRUE(value_ok) << "Longitude value does not match";


    // ___ Set coordinates of a node to border values (-90,-180) ___

    lat = -90;
    lon = -180;

    ASSERT_EQ(API_OK, synchronousSetNodeCoordinates(0, n1.get(), lat, lon)) << "Cannot set node coordinates";

    n1.reset(megaApi[0]->getNodeByHandle(n1->getHandle()));

    ASSERT_EQ(lat, n1->getLatitude()) << "Latitude value does not match";
    value_ok = ((n1->getLongitude() == lon) || (n1->getLongitude() == -lon));
    ASSERT_TRUE(value_ok) << "Longitude value does not match";


    // ___ Reset coordinates of a node ___

    lat = lon = MegaNode::INVALID_COORDINATE;

    synchronousSetNodeCoordinates(0, n1.get(), lat, lon);

    n1.reset(megaApi[0]->getNodeByHandle(n1->getHandle()));
    ASSERT_EQ(lat, n1->getLatitude()) << "Latitude value does not match";
    ASSERT_EQ(lon, n1->getLongitude()) << "Longitude value does not match";


    // ******************    also test shareable / unshareable versions:

    ASSERT_EQ(API_OK, synchronousGetSpecificAccountDetails(0, true, true, true)) << "Cannot get account details";

    // ___ set the coords  (shareable)
    lat = -51.8719987255814;
    lon = +179.54;
    ASSERT_EQ(API_OK, synchronousSetNodeCoordinates(0, n1.get(), lat, lon)) << "Cannot set node coordinates";

    // ___ get a link to the file node
    string nodelink = createPublicLink(0, n1.get(), 0, maxTimeout, mApi[0].accountDetails->getProLevel() == 0);

    // ___ import the link
    auto importHandle = importPublicLink(1, nodelink, std::unique_ptr<MegaNode>{megaApi[1]->getRootNode()}.get());
    std::unique_ptr<MegaNode> nimported{megaApi[1]->getNodeByHandle(importHandle)};

    ASSERT_TRUE(veryclose(lat, nimported->getLatitude())) << "Latitude " << n1->getLatitude() << " value does not match " << lat;
    ASSERT_TRUE(veryclose(lon, nimported->getLongitude())) << "Longitude " << n1->getLongitude() << " value does not match " << lon;

    // ___ remove the imported node, for a clean next test
    ASSERT_EQ(API_OK, synchronousRemove(1, nimported.get())) << "Cannot remove a node";


    // ___ again but unshareable this time - totally separate new node - set the coords  (unshareable)

    string filename2 = "a"+UPFILE;
    ASSERT_TRUE(createFile(filename2, false)) << "Couldn't create " << filename2;
    MegaHandle uploadedNodeHande = UNDEF;
    ASSERT_EQ(MegaError::API_OK, doStartUpload(0, &uploadedNodeHande, filename2.c_str(),
                                                        rootnode.get(),
                                                        nullptr /*fileName*/,
                                                        ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
                                                        nullptr /*appData*/,
                                                        false   /*isSourceTemporary*/,
                                                        false   /*startFirst*/,
                                                        nullptr /*cancelToken*/)) << "Cannot upload a test file";
    MegaNode *n2 = megaApi[0]->getNodeByHandle(uploadedNodeHande);
    ASSERT_NE(n2, ((void*)NULL)) << "Cannot initialize second node for scenario (error: " << mApi[0].lastError << ")";

    lat = -5 + -51.8719987255814;
    lon = -5 + +179.54;
    mApi[0].requestFlags[MegaRequest::TYPE_SET_ATTR_NODE] = false;
    megaApi[0]->setUnshareableNodeCoordinates(n2, lat, lon);
    waitForResponse(&mApi[0].requestFlags[MegaRequest::TYPE_SET_ATTR_NODE]);
    ASSERT_EQ(API_OK, mApi[0].lastError) << "Cannot set unshareable node coordinates (error: " << mApi[0].lastError << ")";

    // ___ confirm this user can read them
    MegaNode* selfread = megaApi[0]->getNodeByHandle(n2->getHandle());
    ASSERT_TRUE(veryclose(lat, selfread->getLatitude())) << "Latitude " << n2->getLatitude() << " value does not match " << lat;
    ASSERT_TRUE(veryclose(lon, selfread->getLongitude())) << "Longitude " << n2->getLongitude() << " value does not match " << lon;

    // ___ get a link to the file node
    string nodelink2 = createPublicLink(0, n2, 0, maxTimeout, mApi[0].accountDetails->getProLevel() == 0);

    // ___ import the link
    importHandle = importPublicLink(1, nodelink2, std::unique_ptr<MegaNode>{megaApi[1]->getRootNode()}.get());
    nimported = std::unique_ptr<MegaNode>{megaApi[1]->getNodeByHandle(importHandle)};

    // ___ confirm other user cannot read them
    lat = nimported->getLatitude();
    lon = nimported->getLongitude();
    ASSERT_EQ(MegaNode::INVALID_COORDINATE, lat) << "Latitude value does not match";
    ASSERT_EQ(MegaNode::INVALID_COORDINATE, lon) << "Longitude value does not match";

    // exercise all the cases for 'l' command:

    // delete existing link on node
    ASSERT_EQ(API_OK, doDisableExport(0, n2));

    // create on existing node, no link yet
    ASSERT_EQ(API_OK, doExportNode(0, n2));

    // create on existing node, with link already  (different command response)
    ASSERT_EQ(API_OK, doExportNode(0, n2));

    gTestingInvalidArgs = true;
    // create on non existent node
    ASSERT_EQ(API_EARGS, doExportNode(0, nullptr));
    gTestingInvalidArgs = false;

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
    bool CommandSendSignupLink2::procresult(Result r)
    bool CommandConfirmSignupLink2::procresult(Result r)
    bool CommandSetKeyPair::procresult(Result r)
    bool CommandSubmitPurchaseReceipt::procresult(Result r)
    bool CommandCreditCardStore::procresult(Result r)
    bool CommandCreditCardQuerySubscriptions::procresult(Result r)
    bool CommandCreditCardCancelSubscriptions::procresult(Result r)
    bool CommandCopySession::procresult(Result r)
    bool CommandGetPaymentMethods::procresult(Result r)
    bool CommandSendReport::procresult(Result r)
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

    auto nh = createFolder(0, name1, rootnode);
    ASSERT_NE(nh, UNDEF);

    // --- Rename a node ---

    MegaNode *n1 = megaApi[0]->getNodeByHandle(nh);
    strcpy(name1, "Folder renamed");

    ASSERT_EQ(API_OK, doRenameNode(0, n1, name1));

    // --- Copy a node ---

    MegaNode *n2;
    char name2[64] = "Folder copy";

    MegaHandle nodeCopiedHandle = UNDEF;
    ASSERT_EQ(API_OK, doCopyNode(0, &nodeCopiedHandle, n1, rootnode, name2)) << "Cannot create a copy of a node";
    n2 = megaApi[0]->getNodeByHandle(nodeCopiedHandle);


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
    ASSERT_EQ(API_OK, doMoveNode(0, nullptr, n1, n2)) << "Cannot move node";


    // --- Get parent node ---

    MegaNode *n5;
    n5 = megaApi[0]->getParentNode(n1);

    ASSERT_EQ(n2->getHandle(), n5->getHandle()) << "Wrong parent node";


    // --- Send to Rubbish bin ---
    ASSERT_EQ(API_OK, doMoveNode(0, nullptr, n2, megaApi[0]->getRubbishNode())) << "Cannot move node to Rubbish bin";


    // --- Remove a node ---
    ASSERT_EQ(API_OK, synchronousRemove(0, n2)) << "Cannot remove a node";

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
    ASSERT_TRUE(createFile(filename1)) << "Couldn't create " << filename1;


    // --- Cancel a transfer ---
    TransferTracker ttc(megaApi[0].get());
    megaApi[0]->startUpload(filename1.c_str(),
                            rootnode,
                            nullptr /*fileName*/,
                            ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
                            nullptr /*appData*/,
                            false   /*isSourceTemporary*/,
                            false   /*startFirst*/,
                            nullptr /*cancelToken*/,
                            &ttc);

    ASSERT_EQ(API_OK, synchronousCancelTransfers(0, MegaTransfer::TYPE_UPLOAD));
    ASSERT_EQ(API_EINCOMPLETE, ttc.waitForResult());

    // --- Upload a file (part 1) ---
    TransferTracker tt(megaApi[0].get());
    mApi[0].transferFlags[MegaTransfer::TYPE_UPLOAD] = false;
    megaApi[0]->startUpload(filename1.c_str(),
                            rootnode,
                            nullptr /*fileName*/,
                            ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
                            nullptr /*appData*/,
                            false   /*isSourceTemporary*/,
                            false   /*startFirst*/,
                            nullptr /*cancelToken*/,
                            &tt);   /*MegaTransferListener*/

    // do not wait yet for completion

    // --- Pause a transfer ---

    mApi[0].requestFlags[MegaRequest::TYPE_PAUSE_TRANSFERS] = false;
    megaApi[0]->pauseTransfers(true, MegaTransfer::TYPE_UPLOAD);
    ASSERT_TRUE( waitForResponse(&mApi[0].requestFlags[MegaRequest::TYPE_PAUSE_TRANSFERS]) )
            << "Pause of transfers failed after " << maxTimeout << " seconds";
    EXPECT_EQ(API_OK, mApi[0].lastError) << "Cannot pause transfer (error: " << mApi[0].lastError << ")";
    EXPECT_TRUE(megaApi[0]->areTransfersPaused(MegaTransfer::TYPE_UPLOAD)) << "Upload transfer not paused";


    // --- Resume a transfer ---

    mApi[0].requestFlags[MegaRequest::TYPE_PAUSE_TRANSFERS] = false;
    megaApi[0]->pauseTransfers(false, MegaTransfer::TYPE_UPLOAD);
    ASSERT_TRUE( waitForResponse(&mApi[0].requestFlags[MegaRequest::TYPE_PAUSE_TRANSFERS]) )
            << "Resumption of transfers after pause has failed after " << maxTimeout << " seconds";
    EXPECT_EQ(API_OK, mApi[0].lastError) << "Cannot resume transfer (error: " << mApi[0].lastError << ")";
    EXPECT_FALSE(megaApi[0]->areTransfersPaused(MegaTransfer::TYPE_UPLOAD)) << "Upload transfer not resumed";


    // --- Upload a file (part 2) ---

    ASSERT_EQ(API_OK,tt.waitForResult()) << "Cannot upload file (error: " << mApi[0].lastError << ")";

    MegaNode *n1 = megaApi[0]->getNodeByHandle(tt.resultNodeHandle);
    bool null_pointer = (n1 == NULL);

    ASSERT_FALSE(null_pointer) << "Cannot upload file (error: " << mApi[0].lastError << ")";
    ASSERT_STREQ(filename1.c_str(), n1->getName()) << "Uploaded file with wrong name (error: " << mApi[0].lastError << ")";


    ASSERT_EQ(API_OK, doSetFileVersionsOption(0, false));  // false = not disabled

    // Upload a file over an existing one to make a version
    {
        ofstream f(filename1);
        f << "edited";
    }

    ASSERT_EQ(API_OK, doStartUpload(0, nullptr, filename1.c_str(),
                                    rootnode,
                                    nullptr /*fileName*/,
                                    ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
                                    nullptr /*appData*/,
                                    false   /*isSourceTemporary*/,
                                    false   /*startFirst*/,
                                    nullptr /*cancelToken*/));

    // Upload a file over an existing one to make a version
    {
        ofstream f(filename1);
        f << "edited2";
    }

    ASSERT_EQ(API_OK, doStartUpload(0, nullptr, filename1.c_str(),
                                    rootnode,
                                    nullptr /*fileName*/,
                                    ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
                                    nullptr /*appData*/,
                                    false   /*isSourceTemporary*/,
                                    false   /*startFirst*/,
                                    nullptr /*cancelToken*/));

    // copy a node with versions to a new name (exercises the multi node putndoes_result)
    MegaNode* nodeToCopy1 = megaApi[0]->getNodeByPath(("/" + filename1).c_str());
    ASSERT_EQ(API_OK, doCopyNode(0, nullptr, nodeToCopy1, rootnode, "some_other_name"));

    // put original filename1 back
    fs::remove(filename1);
    ASSERT_TRUE(createFile(filename1)) << "Couldn't create " << filename1;
    ASSERT_EQ(API_OK, doStartUpload(0, nullptr, filename1.c_str(),
                                    rootnode,
                                    nullptr /*fileName*/,
                                    ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
                                    nullptr /*appData*/,
                                    false   /*isSourceTemporary*/,
                                    false   /*startFirst*/,
                                    nullptr /*cancelToken*/));

    n1 = megaApi[0]->getNodeByPath(("/" + filename1).c_str());

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
    megaApi[0]->startDownload(n2,
                              filename2.c_str(),
                              nullptr  /*customName*/,
                              nullptr  /*appData*/,
                              false    /*startFirst*/,
                              nullptr  /*cancelToken*/);

    ASSERT_TRUE( waitForResponse(&mApi[0].transferFlags[MegaTransfer::TYPE_DOWNLOAD], 600) )
            << "Download transfer failed after " << maxTimeout << " seconds";
    ASSERT_EQ(API_OK, mApi[0].lastError) << "Cannot download the file (error: " << mApi[0].lastError << ")";

    MegaNode *n3 = megaApi[0]->getNodeByHandle(n2->getHandle());
    null_pointer = (n3 == NULL);

    ASSERT_FALSE(null_pointer) << "Cannot download node";
    ASSERT_EQ(n2->getHandle(), n3->getHandle()) << "Cannot download node (error: " << mApi[0].lastError << ")";


    // --- Upload a 0-bytes file ---

    string filename3 = EMPTYFILE;
    FILE *fp = fopen(filename3.c_str(), "w");
    fclose(fp);

    MegaHandle uploadedNodeHande = UNDEF;
    ASSERT_EQ(MegaError::API_OK, doStartUpload(0, &uploadedNodeHande, filename3.c_str(),
                                                        rootnode,
                                                        nullptr /*fileName*/,
                                                        ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
                                                        nullptr /*appData*/,
                                                        false   /*isSourceTemporary*/,
                                                        false   /*startFirst*/,
                                                        nullptr /*cancelToken*/)) << "Cannot upload a test file";

    MegaNode *n4 = megaApi[0]->getNodeByHandle(uploadedNodeHande);
    null_pointer = (n4 == NULL);

    ASSERT_FALSE(null_pointer) << "Cannot upload file (error: " << mApi[0].lastError << ")";
    ASSERT_STREQ(filename3.c_str(), n4->getName()) << "Uploaded file with wrong name (error: " << mApi[0].lastError << ")";


    // --- Download a 0-byte file ---

    filename3 = DOTSLASH +  EMPTYFILE;

    mApi[0].transferFlags[MegaTransfer::TYPE_DOWNLOAD] = false;
    megaApi[0]->startDownload(n4,
                              filename3.c_str(),
                              nullptr  /*customName*/,
                              nullptr  /*appData*/,
                              false    /*startFirst*/,
                              nullptr  /*cancelToken*/);

    ASSERT_TRUE( waitForResponse(&mApi[0].transferFlags[MegaTransfer::TYPE_DOWNLOAD], 600) )
            << "Download 0-byte file failed after " << maxTimeout << " seconds";
    ASSERT_EQ(API_OK, mApi[0].lastError) << "Cannot download the file (error: " << mApi[0].lastError << ")";

    MegaNode *n5 = megaApi[0]->getNodeByHandle(n4->getHandle());
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

    EXPECT_STRCASEEQ(mApi[0].email.c_str(), std::unique_ptr<char[]>{megaApi[0]->getMyEmail()}.get());
    EXPECT_STRCASEEQ(mApi[1].email.c_str(), std::unique_ptr<char[]>{megaApi[1]->getMyEmail()}.get());


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

    ASSERT_STREQ(message.c_str(), mApi[0].cr->getSourceMessage()) << "Message sent is corrupted";
    ASSERT_STRCASEEQ(mApi[0].email.c_str(), mApi[0].cr->getSourceEmail()) << "Wrong source email";
    ASSERT_STRCASEEQ(mApi[1].email.c_str(), mApi[0].cr->getTargetEmail()) << "Wrong target email";
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
        ASSERT_STREQ(message.c_str(), mApi[1].cr->getSourceMessage()) << "Message received is corrupted";
    }
    ASSERT_STRCASEEQ(mApi[0].email.c_str(), mApi[1].cr->getSourceEmail()) << "Wrong source email";
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
//    megaApi->inviteContact(mApi[1].email.c_str(), message.c_str(), MegaContactRequest::INVITE_ACTION_REMIND);
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
    ASSERT_STREQ( "Avatar changed", attributeValue.c_str()) << "Failed to change avatar";

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
    ASSERT_STREQ("Avatar not found", attributeValue.c_str()) << "Failed to remove avatar";

    delete u;


    // --- Delete an existing contact ---

    mApi[0].userUpdated = false;
    ASSERT_NO_FATAL_FAILURE( removeContact(mApi[1].email) );
    ASSERT_TRUE( waitForResponse(&mApi[0].userUpdated) )   // at the target side (main account)
            << "User attribute update not received after " << maxTimeout << " seconds";

    u = megaApi[0]->getContact(mApi[1].email.c_str());
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

bool SdkTest::checkAlert(int apiIndex, const string& title, handle h, int64_t n, MegaHandle mh)
{
    bool ok = false;
    for (int i = 0; !ok && i < 10; ++i)
    {

        MegaUserAlertList* list = megaApi[apiIndex]->getUserAlerts();
        if (list->size() > 0)
        {
            MegaUserAlert* a = list->get(list->size() - 1);
            ok = title == a->getTitle() && a->getNodeHandle() == h;
            if (n != -1)
                ok = ok && a->getNumber(0) == n;
            if (mh != INVALID_HANDLE)
                ok = ok && a->getHandle(0) == mh;

            if (!ok && i == 9)
            {
                EXPECT_STREQ(a->getTitle(), title.c_str());
                EXPECT_EQ(a->getNodeHandle(), h);
                if (n != -1)
                {
                    EXPECT_EQ(a->getNumber(0), n);
                }
                if (mh != INVALID_HANDLE)
                {
                    EXPECT_EQ(a->getHandle(0), mh);
                }
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
 * - Move a shared file (not owned) to Rubbish bin
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
    //      |--file.txt
    //    |--file.txt

    std::unique_ptr<MegaNode> rootnode{megaApi[0]->getRootNode()};
    char foldername1[64] = "Shared-folder";
    MegaHandle hfolder1 = createFolder(0, foldername1, rootnode.get());
    ASSERT_NE(hfolder1, UNDEF);

    n1 = megaApi[0]->getNodeByHandle(hfolder1);
    ASSERT_NE(n1, nullptr);

    char foldername2[64] = "subfolder";
    MegaHandle hfolder2 = createFolder(0, foldername2, std::unique_ptr<MegaNode>{megaApi[0]->getNodeByHandle(hfolder1)}.get());
    ASSERT_NE(hfolder2, UNDEF);

    MegaHandle hfile1 = UNDEF;

    // not a large file since don't need to test transfers here
    ASSERT_TRUE(createFile(PUBLICFILE.c_str(), false)) << "Couldn't create " << PUBLICFILE.c_str();

    ASSERT_EQ(MegaError::API_OK, doStartUpload(0, &hfile1, PUBLICFILE.c_str(),
                                                        std::unique_ptr<MegaNode>{megaApi[0]->getNodeByHandle(hfolder1)}.get(),
                                                        nullptr /*fileName*/,
                                                        ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
                                                        nullptr /*appData*/,
                                                        false   /*isSourceTemporary*/,
                                                        false   /*startFirst*/,
                                                        nullptr /*cancelToken*/)) << "Cannot upload a test file";

    MegaHandle hfile2 = UNDEF;
    ASSERT_EQ(MegaError::API_OK, doStartUpload(0, &hfile2, PUBLICFILE.c_str(),
                                                        std::unique_ptr<MegaNode>{megaApi[0]->getNodeByHandle(hfolder2)}.get(),
                                                        nullptr /*fileName*/,
                                                        ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
                                                        nullptr /*appData*/,
                                                        false   /*isSourceTemporary*/,
                                                        false   /*startFirst*/,
                                                        nullptr /*cancelToken*/)) << "Cannot upload a second test file";

    // --- Download authorized node from another account ---

    MegaNode *nNoAuth = megaApi[0]->getNodeByHandle(hfile1);

    int transferError = doStartDownload(1, nNoAuth,
                                                 "unauthorized_node",
                                                 nullptr  /*customName*/,
                                                 nullptr  /*appData*/,
                                                 false    /*startFirst*/,
                                                 nullptr  /*cancelToken*/);


    bool hasFailed = (transferError != API_OK);
    ASSERT_TRUE(hasFailed) << "Download of node without authorization successful! (it should fail): " << transferError;

    MegaNode *nAuth = megaApi[0]->authorizeNode(nNoAuth);

    // make sure target download file doesn't already exist:
    deleteFile("authorized_node");

    transferError = doStartDownload(1, nAuth,
                                             "authorized_node",
                                             nullptr  /*customName*/,
                                             nullptr  /*appData*/,
                                             false    /*startFirst*/,
                                             nullptr  /*cancelToken*/);

    ASSERT_EQ(API_OK, transferError) << "Cannot download authorized node (error: " << mApi[1].lastError << ")";
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
    mApi[0].nodeUpdated = mApi[1].nodeUpdated = false; // reset flags expected to be true in asserts below
    mApi[0].mOnNodesUpdateCompletion = createOnNodesUpdateLambda(hfolder1, MegaNode::CHANGE_TYPE_OUTSHARE);
    mApi[1].mOnNodesUpdateCompletion = createOnNodesUpdateLambda(hfolder1, MegaNode::CHANGE_TYPE_INSHARE);

    ASSERT_NO_FATAL_FAILURE( shareFolder(n1, mApi[1].email.c_str(), MegaShare::ACCESS_FULL) );
    ASSERT_TRUE( waitForResponse(&mApi[0].nodeUpdated) )   // at the target side (main account)
            << "Node update not received after " << maxTimeout << " seconds";
    ASSERT_TRUE( waitForResponse(&mApi[1].nodeUpdated) )   // at the target side (auxiliar account)
            << "Node update not received after " << maxTimeout << " seconds";

    // important to reset
    resetOnNodeUpdateCompletionCBs();

    // --- Check the outgoing share ---

    sl = megaApi[0]->getOutShares();
    ASSERT_EQ(1, sl->size()) << "Outgoing share failed";
    s = sl->get(0);

    n1 = megaApi[0]->getNodeByHandle(hfolder1);    // get an updated version of the node

    ASSERT_EQ(MegaShare::ACCESS_FULL, s->getAccess()) << "Wrong access level of outgoing share";
    ASSERT_EQ(hfolder1, s->getNodeHandle()) << "Wrong node handle of outgoing share";
    ASSERT_STREQ(mApi[1].email.c_str(), s->getUser()) << "Wrong email address of outgoing share";
    ASSERT_TRUE(n1->isShared()) << "Wrong sharing information at outgoing share";
    ASSERT_TRUE(n1->isOutShare()) << "Wrong sharing information at outgoing share";

    delete sl;


    // --- Check the incoming share ---

    sl = megaApi[1]->getInSharesList();
    ASSERT_EQ(1, sl->size()) << "Incoming share not received in auxiliar account";

    nl = megaApi[1]->getInShares(megaApi[1]->getContact(mApi[0].email.c_str()));
    ASSERT_EQ(1, nl->size()) << "Incoming share not received in auxiliar account";
    n = nl->get(0);

    ASSERT_EQ(hfolder1, n->getHandle()) << "Wrong node handle of incoming share";
    ASSERT_STREQ(foldername1, n->getName()) << "Wrong folder name of incoming share";
    ASSERT_EQ(API_OK, megaApi[1]->checkAccess(n, MegaShare::ACCESS_FULL).getErrorCode()) << "Wrong access level of incoming share";
    ASSERT_TRUE(n->isInShare()) << "Wrong sharing information at incoming share";
    ASSERT_TRUE(n->isShared()) << "Wrong sharing information at incoming share";

    // --- Move share file from different subtree, same file and fingerprint ---
    // Pre-requisite, the movement finds a file with same name and fp at target folder
    // Since the source and target folders belong to different trees, it will attempt to copy+delete
    // (hfile1 copied to rubbish, renamed to "copy", copied back to hfolder2, move
    // Since there is a file with same name and fingerprint, it will skip the copy and will do delete
    mApi[0].nodeUpdated = mApi[1].nodeUpdated = false; // reset flags expected to be true in asserts below
    mApi[0].mOnNodesUpdateCompletion = createOnNodesUpdateLambda(INVALID_HANDLE, MegaNode::CHANGE_TYPE_NEW);
    mApi[1].mOnNodesUpdateCompletion = createOnNodesUpdateLambda(INVALID_HANDLE, MegaNode::CHANGE_TYPE_NEW);
    MegaHandle copiedNodeHandle = INVALID_HANDLE;
    ASSERT_EQ(API_OK, doCopyNode(1, &copiedNodeHandle, megaApi[1]->getNodeByHandle(hfile2), megaApi[1]->getNodeByHandle(hfolder1), "copy")) << "Copying shared file (not owned) to same place failed";
    ASSERT_TRUE( waitForResponse(&mApi[0].nodeUpdated) )   // at the target side (main account)
            << "Node update not received after " << maxTimeout << " seconds";
    ASSERT_TRUE( waitForResponse(&mApi[1].nodeUpdated) )   // at the target side (auxiliar account)
            << "Node update not received after " << maxTimeout << " seconds";

    resetOnNodeUpdateCompletionCBs();

    mApi[1].nodeUpdated = false; // reset flags expected to be true in asserts below
    mApi[1].mOnNodesUpdateCompletion = createOnNodesUpdateLambda(INVALID_HANDLE, MegaNode::CHANGE_TYPE_NEW);
    MegaHandle copiedNodeHandleInRubbish = INVALID_HANDLE;
    ASSERT_EQ(API_OK, doCopyNode(1, &copiedNodeHandleInRubbish, megaApi[1]->getNodeByHandle(copiedNodeHandle), megaApi[1]->getRubbishNode())) << "Copying shared file (not owned) to Rubbish bin failed";
    ASSERT_TRUE( waitForResponse(&mApi[1].nodeUpdated) )   // at the target side (auxiliar account)
            << "Node update not received after " << maxTimeout << " seconds";

    resetOnNodeUpdateCompletionCBs();

    mApi[0].nodeUpdated = mApi[1].nodeUpdated = false; // reset flags expected to be true in asserts below
    mApi[0].mOnNodesUpdateCompletion = createOnNodesUpdateLambda(copiedNodeHandle, MegaNode::CHANGE_TYPE_REMOVED);
    mApi[1].mOnNodesUpdateCompletion = createOnNodesUpdateLambda(copiedNodeHandle, MegaNode::CHANGE_TYPE_REMOVED);
    MegaHandle copyAndDeleteNodeHandle = INVALID_HANDLE;
    ASSERT_EQ(API_OK, doMoveNode(1, &copyAndDeleteNodeHandle, megaApi[0]->getNodeByHandle(copiedNodeHandle), megaApi[1]->getRubbishNode())) << "Moving shared file, same name and fingerprint";
    ASSERT_EQ(megaApi[1]->getNodeByHandle(copiedNodeHandle), nullptr) << "Move didn't delete source file";
    ASSERT_TRUE( waitForResponse(&mApi[0].nodeUpdated) )   // at the target side (main account)
            << "Node update not received after " << maxTimeout << " seconds";
    ASSERT_TRUE( waitForResponse(&mApi[1].nodeUpdated) )   // at the target side (auxiliar account)
            << "Node update not received after " << maxTimeout << " seconds";

    resetOnNodeUpdateCompletionCBs();


    // --- Move shared file (not owned) to Rubbish bin ---
    MegaHandle movedNodeHandle = UNDEF;
    ASSERT_EQ(API_OK, doMoveNode(1, &movedNodeHandle, megaApi[0]->getNodeByHandle(hfile2), megaApi[1]->getRubbishNode())) << "Moving shared file (not owned) to Rubbish bin failed";

    // --- Test that file in Rubbish bin can be restored ---
    MegaNode* nodeMovedFile = megaApi[1]->getNodeByHandle(movedNodeHandle);  // Different handle! the node must have been copied due to differing accounts
    ASSERT_EQ(nodeMovedFile->getRestoreHandle(), hfolder2) << "Incorrect restore handle for file in Rubbish Bin";

    delete nl;

    // check the corresponding user alert
    ASSERT_TRUE(checkAlert(1, "New shared folder from " + mApi[0].email, mApi[0].email + ":Shared-folder"));

    // add a folder under the share
    char foldernameA[64] = "dummyname1";
    char foldernameB[64] = "dummyname2";

    MegaHandle fh = UNDEF;
    ASSERT_NE(fh = createFolder(0, foldernameA, std::unique_ptr<MegaNode>{megaApi[0]->getNodeByHandle(hfolder2)}.get()), UNDEF);
    ASSERT_NE(createFolder(0, foldernameB, std::unique_ptr<MegaNode>{megaApi[0]->getNodeByHandle(hfolder2)}.get()), UNDEF);

    // check the corresponding user alert
    ASSERT_TRUE(checkAlert(1, mApi[0].email + " added 2 folders", std::unique_ptr<MegaNode>{megaApi[0]->getNodeByHandle(hfolder2)}->getHandle(), 2, fh));

    // --- Modify the access level of an outgoing share ---
    mApi[0].nodeUpdated = mApi[1].nodeUpdated = false; // reset flags expected to be true in asserts below
    mApi[0].mOnNodesUpdateCompletion = createOnNodesUpdateLambda(hfolder1, MegaNode::CHANGE_TYPE_OUTSHARE);
    mApi[1].mOnNodesUpdateCompletion = createOnNodesUpdateLambda(hfolder1, MegaNode::CHANGE_TYPE_INSHARE);

    ASSERT_NO_FATAL_FAILURE(shareFolder(megaApi[0]->getNodeByHandle(hfolder1), mApi[1].email.c_str(), MegaShare::ACCESS_READWRITE) );
    ASSERT_TRUE( waitForResponse(&mApi[0].nodeUpdated) )   // at the target side (main account)
            << "Node update not received after " << maxTimeout << " seconds";
    ASSERT_TRUE( waitForResponse(&mApi[1].nodeUpdated) )   // at the target side (auxiliar account)
            << "Node update not received after " << maxTimeout << " seconds";

    // important to reset
    resetOnNodeUpdateCompletionCBs();

    nl = megaApi[1]->getInShares(megaApi[1]->getContact(mApi[0].email.c_str()));
    ASSERT_EQ(1, nl->size()) << "Incoming share not received in auxiliar account";
    n = nl->get(0);

    ASSERT_EQ(API_OK, megaApi[1]->checkAccess(n, MegaShare::ACCESS_READWRITE).getErrorCode()) << "Wrong access level of incoming share";

    delete nl;


    // --- Revoke access to an outgoing share ---

    mApi[0].nodeUpdated = mApi[1].nodeUpdated = false; // reset flags expected to be true in asserts below
    mApi[0].mOnNodesUpdateCompletion = createOnNodesUpdateLambda(hfolder1, MegaNode::CHANGE_TYPE_OUTSHARE);
    mApi[1].mOnNodesUpdateCompletion = createOnNodesUpdateLambda(hfolder1, MegaNode::CHANGE_TYPE_REMOVED);
    ASSERT_NO_FATAL_FAILURE( shareFolder(n1, mApi[1].email.c_str(), MegaShare::ACCESS_UNKNOWN) );
    ASSERT_TRUE( waitForResponse(&mApi[0].nodeUpdated) )   // at the target side (main account)
            << "Node update not received after " << maxTimeout << " seconds";
    ASSERT_TRUE( waitForResponse(&mApi[1].nodeUpdated) )   // at the target side (auxiliar account)
            << "Node update not received after " << maxTimeout << " seconds";

    // important to reset
    resetOnNodeUpdateCompletionCBs();

    delete sl;
    sl = megaApi[0]->getOutShares();
    ASSERT_EQ(0, sl->size()) << "Outgoing share revocation failed";
    delete sl;

    nl = megaApi[1]->getInShares(megaApi[1]->getContact(mApi[0].email.c_str()));
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
    mApi[0].nodeUpdated = false; // reset flags expected to be true in asserts below
    mApi[0].mOnNodesUpdateCompletion = createOnNodesUpdateLambda(hfolder2, MegaNode::CHANGE_TYPE_PENDINGSHARE);

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

    ASSERT_EQ(API_OK, synchronousGetSpecificAccountDetails(0, true, true, true)) << "Cannot get account details";

    std::unique_ptr<MegaNode> nfile1{megaApi[0]->getNodeByHandle(hfile1)};

    string nodelink3 = createPublicLink(0, nfile1.get(), 0, maxTimeout, mApi[0].accountDetails->getProLevel() == 0);
    // The created link is stored in this->link at onRequestFinish()

    // Get a fresh snapshot of the node and check it's actually exported
    nfile1 = std::unique_ptr<MegaNode>{megaApi[0]->getNodeByHandle(hfile1)};
    ASSERT_TRUE(nfile1->isExported()) << "Node is not exported, must be exported";
    ASSERT_FALSE(nfile1->isTakenDown()) << "Public link is taken down, it mustn't";

    // Regenerate the same link should not trigger a new request
    nfile1 = std::unique_ptr<MegaNode>{megaApi[0]->getNodeByHandle(hfile1)};
    string nodelink4 = createPublicLink(0, nfile1.get(), 0, maxTimeout, mApi[0].accountDetails->getProLevel() == 0);
    ASSERT_STREQ(nodelink3.c_str(), nodelink4.c_str()) << "Wrong public link after link update";


    // Try to update the expiration time of an existing link (only for PRO accounts are allowed, otherwise -11
    string nodelinkN = createPublicLink(0, nfile1.get(), m_time() + 30*86400, maxTimeout, mApi[0].accountDetails->getProLevel() == 0);
    nfile1 = std::unique_ptr<MegaNode>{megaApi[0]->getNodeByHandle(hfile1)};
    if (mApi[0].accountDetails->getProLevel() == 0)
    {
        ASSERT_EQ(0, nfile1->getExpirationTime()) << "Expiration time successfully set, when it shouldn't";
    }
    ASSERT_FALSE(nfile1->isExpired()) << "Public link is expired, it mustn't";


    // --- Import a file public link ---

    auto importHandle = importPublicLink(0, nodelink4, rootnode.get());

    MegaNode *nimported = megaApi[0]->getNodeByHandle(importHandle);

    ASSERT_STREQ(nfile1->getName(), nimported->getName()) << "Imported file with wrong name";
    ASSERT_EQ(rootnode->getHandle(), nimported->getParentHandle()) << "Imported file in wrong path";


    // --- Get node from file public link ---

    auto nodeUP = getPublicNode(1, nodelink4);

    ASSERT_TRUE(nodeUP && nodeUP->isPublic()) << "Cannot get a node from public link";


    // --- Remove a public link ---

    MegaHandle removedLinkHandle = removePublicLink(0, nfile1.get());

    nfile1 = std::unique_ptr<MegaNode>{megaApi[0]->getNodeByHandle(removedLinkHandle)};
    ASSERT_FALSE(nfile1->isPublic()) << "Public link removal failed (still public)";

    delete nimported;


    // --- Create a folder public link ---

    MegaNode *nfolder1 = megaApi[0]->getNodeByHandle(hfolder1);

    string nodelink5 = createPublicLink(0, nfolder1, 0, maxTimeout, mApi[0].accountDetails->getProLevel() == 0);
    // The created link is stored in this->link at onRequestFinish()

    delete nfolder1;

    // Get a fresh snapshot of the node and check it's actually exported
    nfolder1 = megaApi[0]->getNodeByHandle(hfolder1);
    ASSERT_TRUE(nfolder1->isExported()) << "Node is not exported, must be exported";
    ASSERT_FALSE(nfolder1->isTakenDown()) << "Public link is taken down, it mustn't";

    delete nfolder1;

    nfolder1 = megaApi[0]->getNodeByHandle(hfolder1);
    ASSERT_STREQ(nodelink5.c_str(), nfolder1->getPublicLink()) << "Wrong public link from MegaNode";

    // Regenerate the same link should not trigger a new request
    string nodelink6 = createPublicLink(0, nfolder1, 0, maxTimeout, mApi[0].accountDetails->getProLevel() == 0);
    ASSERT_STREQ(nodelink5.c_str(), nodelink6.c_str()) << "Wrong public link after link update";

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

    auto nh = createFolder(0, "share-folder-A", rootnodeA.get());
    ASSERT_NE(nh, UNDEF);
    unique_ptr<MegaNode> shareFolderA(megaApi[0]->getNodeByHandle(nh));
    ASSERT_TRUE(!!shareFolderA);

    nh = createFolder(0, "sub-folder-A", shareFolderA.get());
    ASSERT_NE(nh, UNDEF);
    unique_ptr<MegaNode> subFolderA(megaApi[0]->getNodeByHandle(nh));
    ASSERT_TRUE(!!subFolderA);

    // Initialize a test scenario: create a new contact to share to

    ASSERT_EQ(API_OK, synchronousInviteContact(0, mApi[1].email.c_str(), "SdkTestShareKeys contact request A to B", MegaContactRequest::INVITE_ACTION_ADD));
    ASSERT_EQ(API_OK, synchronousInviteContact(0, mApi[2].email.c_str(), "SdkTestShareKeys contact request A to C", MegaContactRequest::INVITE_ACTION_ADD));

    ASSERT_TRUE(WaitFor([this]() {return unique_ptr<MegaContactRequestList>(megaApi[1]->getIncomingContactRequests())->size() == 1
                                      && unique_ptr<MegaContactRequestList>(megaApi[2]->getIncomingContactRequests())->size() == 1;}, 60000));
    ASSERT_NO_FATAL_FAILURE(getContactRequest(1, false));
    ASSERT_NO_FATAL_FAILURE(getContactRequest(2, false));

    ASSERT_EQ(API_OK, synchronousReplyContactRequest(1, mApi[1].cr.get(), MegaContactRequest::REPLY_ACTION_ACCEPT));
    ASSERT_EQ(API_OK, synchronousReplyContactRequest(2, mApi[2].cr.get(), MegaContactRequest::REPLY_ACTION_ACCEPT));

    WaitMillisec(3000);

    ASSERT_EQ(unsigned(unique_ptr<MegaShareList>(megaApi[1]->getInSharesList())->size()), 0u);
    ASSERT_EQ(unsigned(unique_ptr<MegaShareList>(megaApi[2]->getInSharesList())->size()), 0u);

    ASSERT_EQ(API_OK, synchronousShare(0, shareFolderA.get(), mApi[1].email.c_str(), MegaShare::ACCESS_READ));
    ASSERT_TRUE(WaitFor([this]() { return unique_ptr<MegaShareList>(megaApi[1]->getInSharesList())->size() == 1; }, 60000));

    ASSERT_EQ(API_OK, synchronousShare(0, subFolderA.get(), mApi[2].email.c_str(), MegaShare::ACCESS_FULL));
    ASSERT_TRUE(WaitFor([this]() { return unique_ptr<MegaShareList>(megaApi[2]->getInSharesList())->size() == 1; }, 60000));

    ASSERT_EQ(unsigned(unique_ptr<MegaShareList>(megaApi[1]->getInSharesList())->size()), 1u);
    ASSERT_EQ(unsigned(unique_ptr<MegaShareList>(megaApi[2]->getInSharesList())->size()), 1u);

    unique_ptr<MegaNodeList> nl1(megaApi[1]->getInShares(megaApi[1]->getContact(mApi[0].email.c_str())));
    unique_ptr<MegaNodeList> nl2(megaApi[2]->getInShares(megaApi[2]->getContact(mApi[0].email.c_str())));

    ASSERT_EQ(1, nl1->size());
    ASSERT_EQ(1, nl2->size());

    MegaNode* receivedShareNodeB = nl1->get(0);
    MegaNode* receivedShareNodeC = nl2->get(0);

    ASSERT_NE(createFolder(2, "folderByC1", receivedShareNodeC), UNDEF);
    ASSERT_NE(createFolder(2, "folderByC2", receivedShareNodeC), UNDEF);

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

string localpathToUtf8Leaf(const LocalPath& itemlocalname)
{
    return itemlocalname.leafName().toPath(false);
}

LocalPath fspathToLocal(const fs::path& p)
{
    string path(p.u8string());
    return LocalPath::fromAbsolutePath(path);
}


// TODO: SDK-1505
#ifndef __APPLE__
TEST_F(SdkTest, SdkTestFolderIteration)
#else
TEST_F(SdkTest, DISABLED_SdkTestFolderIteration)
#endif
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
            nodetype_t type = nodetype_t::TYPE_UNKNOWN;
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

        auto fsa = makeFsAccess();
        auto localdir = fspathToLocal(iteratePath);

        std::unique_ptr<FileAccess> fopen_directory(fsa->newfileaccess(false));  // false = don't follow symlinks
        ASSERT_TRUE(fopen_directory->fopen(localdir, true, false));

        // now open and iterate the directory, not following symlinks (either by name or fopen'd directory)
        std::unique_ptr<DirAccess> da(fsa->newdiraccess());
        if (da->dopen(openWithNameOrUseFileAccess ? &localdir : NULL, openWithNameOrUseFileAccess ? NULL : fopen_directory.get(), false))
        {
            nodetype_t type;
            LocalPath itemlocalname;
            while (da->dnext(localdir, itemlocalname, false, &type))
            {
                string leafNameUtf8 = localpathToUtf8Leaf(itemlocalname);

                std::unique_ptr<FileAccess> plain_fopen_fa(fsa->newfileaccess(false));
                std::unique_ptr<FileAccess> iterate_fopen_fa(fsa->newfileaccess(false));

                LocalPath localpath = localdir;
                localpath.appendWithSeparator(itemlocalname, true);

                ASSERT_TRUE(plain_fopen_fa->fopen(localpath, true, false));
                plain_fopen[leafNameUtf8] = *plain_fopen_fa;

                ASSERT_TRUE(iterate_fopen_fa->fopen(localpath, true, false, da.get()));
                iterate_fopen[leafNameUtf8] = *iterate_fopen_fa;
            }
        }

        std::unique_ptr<FileAccess> fopen_directory2(fsa->newfileaccess(true));  // true = follow symlinks
        ASSERT_TRUE(fopen_directory2->fopen(localdir, true, false));

        // now open and iterate the directory, following symlinks (either by name or fopen'd directory)
        std::unique_ptr<DirAccess> da_follow(fsa->newdiraccess());
        if (da_follow->dopen(openWithNameOrUseFileAccess ? &localdir : NULL, openWithNameOrUseFileAccess ? NULL : fopen_directory2.get(), false))
        {
            nodetype_t type;
            LocalPath itemlocalname;
            while (da_follow->dnext(localdir, itemlocalname, true, &type))
            {
                string leafNameUtf8 = localpathToUtf8Leaf(itemlocalname);

                std::unique_ptr<FileAccess> plain_follow_fopen_fa(fsa->newfileaccess(true));
                std::unique_ptr<FileAccess> iterate_follow_fopen_fa(fsa->newfileaccess(true));

                LocalPath localpath = localdir;
                localpath.appendWithSeparator(itemlocalname, true);

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
        auto localdirGlob = fspathToLocal(iteratePath / "glob1*");
        std::unique_ptr<DirAccess> da2(fsa->newdiraccess());
        if (da2->dopen(&localdirGlob, NULL, true))
        {
            nodetype_t type;
            LocalPath itemlocalname;
            set<string> remainingExpected { "glob1folder", "glob1file.txt" };
            while (da2->dnext(localdir, itemlocalname, true, &type))
            {
                string leafNameUtf8 = localpathToUtf8Leaf(itemlocalname);
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
            out() << (i < s.size() ? s[i] : "") << "/" << (i < c.completions.size() ? c.completions[i].s : "");
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

    ::mega::NodeHandle megaCurDir;

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
    auto nh = createFolder(0, "test_autocomplete_megafs", rootnode);
    ASSERT_NE(nh, UNDEF);
    MegaNode *n0 = megaApi[0]->getNodeByHandle(nh);

    megaCurDir = NodeHandle().set6byte(nh);

    nh = createFolder(0, "dir1", n0);
    ASSERT_NE(nh, UNDEF);
    MegaNode *n1 = megaApi[0]->getNodeByHandle(nh);
    ASSERT_NE(createFolder(0, "sub11", n1), UNDEF);
    ASSERT_NE(createFolder(0, "sub12", n1), UNDEF);

    nh = createFolder(0, "dir2", n0);
    ASSERT_NE(nh, UNDEF);
    MegaNode *n2 = megaApi[0]->getNodeByHandle(nh);
    ASSERT_NE(createFolder(0, "sub21", n2), UNDEF);
    ASSERT_NE(createFolder(0, "sub22", n2), UNDEF);

    nh = createFolder(0, "dir2a", n0);
    ASSERT_NE(nh, UNDEF);
    MegaNode *n3 = megaApi[0]->getNodeByHandle(nh);

    nh = createFolder(0, "dir space", n3);
    ASSERT_NE(nh, UNDEF);

    MegaNode *n31 = megaApi[0]->getNodeByHandle(nh);

    ASSERT_NE(createFolder(0, "dir space2", n3), UNDEF);
    ASSERT_NE(createFolder(0, "nospace", n3), UNDEF);
    ASSERT_NE(createFolder(0, "next", n31), UNDEF);


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
    ASSERT_EQ(API_OK, mApi[0].lastError) << "Chat creation failed (error: " << mApi[0].lastError << ")";
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
    ASSERT_EQ(API_OK, mApi[0].lastError) << "Removal of chat peer failed (error: " << mApi[0].lastError << ")";
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
    ASSERT_EQ(API_OK, mApi[0].lastError) << "Invitation of chat peer failed (error: " << mApi[0].lastError << ")";
    numpeers = mApi[0].chats[chatid]->getPeerList() ? mApi[0].chats[chatid]->getPeerList()->size() : 0;
    ASSERT_EQ(numpeers, 1) << "Wrong number of peers in the list of peers";
    ASSERT_TRUE( waitForResponse(&mApi[1].chatUpdated) )   // at the target side (auxiliar account)
            << "The peer didn't receive notification of the invitation after " << maxTimeout << " seconds";


    // --- Get the user-specific URL for the chat ---

    mApi[0].requestFlags[MegaRequest::TYPE_CHAT_URL] = false;
    megaApi[0]->getUrlChat(chatid);
    ASSERT_TRUE( waitForResponse(&mApi[0].requestFlags[MegaRequest::TYPE_CHAT_URL]) )
            << "Retrieval of chat URL failed after " << maxTimeout << " seconds";
    ASSERT_EQ(API_OK, mApi[0].lastError) << "Retrieval of chat URL failed (error: " << mApi[0].lastError << ")";


    // --- Update Permissions of an existing peer in the chat

    mApi[1].chatUpdated = false;
    mApi[0].requestFlags[MegaRequest::TYPE_CHAT_UPDATE_PERMISSIONS] = false;
    megaApi[0]->updateChatPermissions(chatid, h, PRIV_RO);
    ASSERT_TRUE( waitForResponse(&mApi[0].requestFlags[MegaRequest::TYPE_CHAT_UPDATE_PERMISSIONS]) )
            << "Update chat permissions failed after " << maxTimeout << " seconds";
    ASSERT_EQ(API_OK, mApi[0].lastError) << "Update of chat permissions failed (error: " << mApi[0].lastError << ")";
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

    auto fsa = makeFsAccess();
    string name = "testfile";
    LocalPath localname = LocalPath::fromAbsolutePath(name);

    int value = 0x01020304;
    for (int i = sizeof filesizes / sizeof filesizes[0]; i--; )
    {
        {
            ofstream ofs(name.c_str(), ios::binary);
            char s[8192];
            ofs.rdbuf()->pubsetbuf(s, sizeof s);
            for (auto j = filesizes[i] / sizeof(value); j-- ; ) ofs.write((char*)&value, sizeof(value));
            ofs.write((char*)&value, filesizes[i] % sizeof(value));
        }

        fsa->setmtimelocal(localname, 1000000000);

        string streamfp, filefp;
        {
            m_time_t mtime = 0;
            {
                auto nfa = fsa->newfileaccess();
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
            s[s.size() - 1] = static_cast<string::value_type>(s[s.size()-1] + 1);
            if (s[s.size() - 1] > '9')
            {
                s[s.size() - 1] = static_cast<string::value_type>(s[s.size()-1] - 1);
                s[s.size() - 2] = static_cast<string::value_type>(s[s.size()-2] + 1);
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

    auto importHandle = importPublicLink(0, MegaClient::MEGAURL+"/#!zAJnUTYD!8YE5dXrnIEJ47NdDfFEvqtOefhuDMphyae0KY5zrhns", rootnode);
    MegaHandle imported_file_handle = importHandle;

    MegaNode *nimported = megaApi[0]->getNodeByHandle(imported_file_handle);


    string filename = DOTSLASH "cloudraid_downloaded_file.sdktest";
    deleteFile(filename.c_str());

    // plain cloudraid download
    mApi[0].transferFlags[MegaTransfer::TYPE_DOWNLOAD] = false;
    megaApi[0]->startDownload(nimported,
                              filename.c_str(),
                              nullptr  /*customName*/,
                              nullptr  /*appData*/,
                              false    /*startFirst*/,
                              nullptr  /*cancelToken*/);

    ASSERT_TRUE(waitForResponse(&mApi[0].transferFlags[MegaTransfer::TYPE_DOWNLOAD], 600))
        << "Download cloudraid transfer failed after " << maxTimeout << " seconds";
    ASSERT_EQ(API_OK, mApi[0].lastError) << "Cannot download the cloudraid file (error: " << mApi[0].lastError << ")";


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
        megaApi[0]->startDownload(nimported,
                                  filename.c_str(),
                                  nullptr  /*customName*/,
                                  nullptr  /*appData*/,
                                  false    /*startFirst*/,
                                  nullptr  /*cancelToken*/);

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
        ASSERT_EQ(API_OK, mApi[0].lastError) << "Cannot download the cloudraid file (error: " << mApi[0].lastError << ")";
    }


    incrementFilename(filename);
    deleteFile(filename.c_str());

    // cloudraid download with periodic full exit and resume from session ID
    // plain cloudraid download
    {
        megaApi[0]->setMaxDownloadSpeed(32 * 1024 * 1024 * 8 / 30); // should take 30 seconds, not counting exit/resume session
        mApi[0].transferFlags[MegaTransfer::TYPE_DOWNLOAD] = false;
        megaApi[0]->startDownload(nimported,
                                  filename.c_str(),
                                  nullptr  /*customName*/,
                                  nullptr  /*appData*/,
                                  false    /*startFirst*/,
                                  nullptr  /*cancelToken*/);

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
            if (onTranferFinishedCount > lastOnTranferFinishedCount)
            {
                t.reset();
                lastOnTranferFinishedCount = onTranferFinishedCount;
                deleteFile(filename.c_str());
                onTransferUpdate_progress = 0;
                onTransferUpdate_filesize = 0;
                lastprogress = 0;
                mApi[0].transferFlags[MegaTransfer::TYPE_DOWNLOAD] = false;
                megaApi[0]->startDownload(nimported,
                                          filename.c_str(),
                                          nullptr  /*customName*/,
                                          nullptr  /*appData*/,
                                          false    /*startFirst*/,
                                          nullptr  /*cancelToken*/);
            }
            else if (onTransferUpdate_progress > lastprogress + onTransferUpdate_filesize/10 )
            {
                if (exitresumecount < 3*(onTranferFinishedCount - initialOnTranferFinishedCount + 1))
                {
                    megaApi[0].reset();
                    exitresumecount += 1;
                    WaitMillisec(100);

                    megaApi[0].reset(newMegaApi(APP_KEY.c_str(), megaApiCacheFolder(0).c_str(), USER_AGENT.c_str(), unsigned(THREADS_PER_MEGACLIENT)));
                    mApi[0].megaApi = megaApi[0].get();
                    megaApi[0]->addListener(this);
                    megaApi[0]->setMaxDownloadSpeed(32 * 1024 * 1024 * 8 / 30); // should take 30 seconds, not counting exit/resume session

                    t.pause();
                    ASSERT_NO_FATAL_FAILURE(resumeSession(sessionId.c_str()));
                    ASSERT_NO_FATAL_FAILURE(fetchnodes(0));
                    t.resume();

                    lastprogress = onTransferUpdate_progress;
                }
            }
            WaitMillisec(1);
        }
        ASSERT_EQ(onTransferUpdate_progress, onTransferUpdate_filesize);
        ASSERT_EQ(initialOnTranferFinishedCount + 2, onTranferFinishedCount);
        ASSERT_GE(exitresumecount, 6u);
        ASSERT_TRUE(waitForResponse(&mApi[0].transferFlags[MegaTransfer::TYPE_DOWNLOAD], 1)) << "Download cloudraid transfer with pauses failed";
        ASSERT_EQ(API_OK, mApi[0].lastError) << "Cannot download the cloudraid file (error: " << mApi[0].lastError << ")";
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

    auto importHandle = importPublicLink(0, MegaClient::MEGAURL+"/#!zAJnUTYD!8YE5dXrnIEJ47NdDfFEvqtOefhuDMphyae0KY5zrhns", rootnode.get());
    std::unique_ptr<MegaNode> nimported{megaApi[0]->getNodeByHandle(importHandle)};


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
        megaApi[0]->startDownload(nimported.get(),
                                  filename.c_str(),
                                  nullptr  /*customName*/,
                                  nullptr  /*appData*/,
                                  false    /*startFirst*/,
                                  nullptr  /*cancelToken*/);

        ASSERT_TRUE(waitForResponse(&mApi[0].transferFlags[MegaTransfer::TYPE_DOWNLOAD], 180)) << "Cloudraid download with 404 and 403 errors time out (180 seconds)";
        ASSERT_EQ(API_OK, mApi[0].lastError) << "Cannot download the cloudraid file (error: " << mApi[0].lastError << ")";
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

    auto importHandle = importPublicLink(0, MegaClient::MEGAURL+"/#!zAJnUTYD!8YE5dXrnIEJ47NdDfFEvqtOefhuDMphyae0KY5zrhns", rootnode.get());
    std::unique_ptr<MegaNode> nimported{megaApi[0]->getNodeByHandle(importHandle)};


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
        megaApi[0]->startDownload(nimported.get(),
                                  filename.c_str(),
                                  nullptr  /*customName*/,
                                  nullptr  /*appData*/,
                                  false    /*startFirst*/,
                                  nullptr  /*cancelToken*/);

        ASSERT_TRUE(waitForResponse(&mApi[0].transferFlags[MegaTransfer::TYPE_DOWNLOAD], 180)) << "Cloudraid download with timeout errors timed out (180 seconds)";
        ASSERT_EQ(API_OK, mApi[0].lastError) << "Cannot download the cloudraid file (error: " << mApi[0].lastError << ")";
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

    //for (int i = 0; i < 1000; ++i) {
    ASSERT_TRUE(DebugTestHook::resetForTests()) << "SDK test hooks are not enabled in release mode";

    // make a file to download, and upload so we can pull it down
    std::unique_ptr<MegaNode> rootnode{megaApi[0]->getRootNode()};
    deleteFile(UPFILE);

    ASSERT_TRUE(createFile(UPFILE, true)) << "Couldn't create " << UPFILE;
    MegaHandle uploadedNodeHandle = UNDEF;
    ASSERT_EQ(MegaError::API_OK, doStartUpload(0, &uploadedNodeHandle, UPFILE.c_str(),
                                                        rootnode.get(),
                                                        nullptr /*fileName*/,
                                                        ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
                                                        nullptr /*appData*/,
                                                        false   /*isSourceTemporary*/,
                                                        false   /*startFirst*/,
                                                        nullptr /*cancelToken*/)) << "Upload transfer failed";

    std::unique_ptr<MegaNode> n1{megaApi[0]->getNodeByHandle(uploadedNodeHandle)};

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
    megaApi[0]->startDownload(n1.get(),
                              filename2.c_str(),
                              nullptr  /*customName*/,
                              nullptr  /*appData*/,
                              false    /*startFirst*/,
                              nullptr  /*cancelToken*/);

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
    ASSERT_EQ(API_OK, mApi[0].lastError) << "Cannot download the file (error: " << mApi[0].lastError << ")";

    ASSERT_LT(DebugTestHook::countdownToOverquota, 0);
    ASSERT_LT(DebugTestHook::countdownToOverquota, originalcount);  // there should have been more http activity after the wait

    ASSERT_TRUE(DebugTestHook::resetForTests()) << "SDK test hooks are not enabled in release mode";

    //cout << "Passed round " << i; }

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

    auto importHandle = importPublicLink(0, MegaClient::MEGAURL+"/#!zAJnUTYD!8YE5dXrnIEJ47NdDfFEvqtOefhuDMphyae0KY5zrhns", megaApi[0]->getRootNode());
    MegaNode *nimported = megaApi[0]->getNodeByHandle(importHandle);

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
    megaApi[0]->startDownload(nimported,
                              filename2.c_str(),
                              nullptr  /*customName*/,
                              nullptr  /*appData*/,
                              false    /*startFirst*/,
                              nullptr  /*cancelToken*/);

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
    ASSERT_EQ(API_OK, mApi[0].lastError) << "Cannot download the file (error: " << mApi[0].lastError << ")";

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
        msg << "onTransferTemporaryError: " << (error ? error->getErrorString() : "NULL");
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
    auto importHandle = importPublicLink(0, MegaClient::MEGAURL+"/#!zAJnUTYD!8YE5dXrnIEJ47NdDfFEvqtOefhuDMphyae0KY5zrhns", std::unique_ptr<MegaNode>{megaApi[0]->getRootNode()}.get());
    MegaNode *nimported = megaApi[0]->getNodeByHandle(importHandle);

    MegaNode *rootnode = megaApi[0]->getRootNode();

    // get the file, and upload as non-raid
    string filename2 = DOTSLASH + DOWNFILE;
    deleteFile(filename2);

    mApi[0].transferFlags[MegaTransfer::TYPE_DOWNLOAD] = false;
    megaApi[0]->startDownload(nimported,
                              filename2.c_str(),
                              nullptr  /*customName*/,
                              nullptr  /*appData*/,
                              false    /*startFirst*/,
                              nullptr  /*cancelToken*/);

    ASSERT_TRUE(waitForResponse(&mApi[0].transferFlags[MegaTransfer::TYPE_DOWNLOAD])) << "Setup transfer failed after " << maxTimeout << " seconds";
    ASSERT_EQ(API_OK, mApi[0].lastError) << "Cannot download the initial file (error: " << mApi[0].lastError << ")";

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
    MegaHandle uploadedNodeHandle = UNDEF;
    ASSERT_EQ(MegaError::API_OK, doStartUpload(0, &uploadedNodeHandle, filename3.c_str(),
                                                        rootnode,
                                                        nullptr /*fileName*/,
                                                        ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
                                                        nullptr /*appData*/,
                                                        false   /*isSourceTemporary*/,
                                                        false   /*startFirst*/,
                                                        nullptr /*cancelToken*/)) << "Upload transfer failed";

    MegaNode *nonRaidNode = megaApi[0]->getNodeByHandle(uploadedNodeHandle);

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
    msg << "Streaming test downloaded " << randomRunsDone << " samples of the file from random places and sizes, " << randomRunsBytes << " bytes total";
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
    ASSERT_TRUE(createFile(filename1, false)) << "Couldn't create " << filename1;

    auto err = doStartUpload(0, nullptr, filename1.c_str(),
                                      rootnode,
                                      nullptr /*fileName*/,
                                      ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
                                      nullptr /*appData*/,
                                      false   /*isSourceTemporary*/,
                                      false   /*startFirst*/,
                                      nullptr /*cancelToken*/);
    ASSERT_EQ(API_OK, err) << "Cannot upload a test file (error: " << err << ")";
    WaitMillisec(1000);

    ofstream f(filename1);
    f << "update";
    f.close();

    err = doStartUpload(0, nullptr, filename1.c_str(),
                                 rootnode,
                                 nullptr /*fileName*/,
                                 ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
                                 nullptr /*appData*/,
                                 false   /*isSourceTemporary*/,
                                 false   /*startFirst*/,
                                 nullptr /*cancelToken*/);

    ASSERT_EQ(API_OK, err) << "Cannot upload an updated test file (error: " << err << ")";
    WaitMillisec(1000);

    synchronousCatchup(0);

    string filename2 = DOWNFILE;
    ASSERT_TRUE(createFile(filename2, false)) << "Couldn't create " << filename2;
    err = doStartUpload(0, nullptr, filename2.c_str(),
                                 rootnode,
                                 nullptr /*fileName*/,
                                 ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
                                 nullptr /*appData*/,
                                 false   /*isSourceTemporary*/,
                                 false   /*startFirst*/,
                                 nullptr /*cancelToken*/);
    ASSERT_EQ(API_OK, err) << "Cannot upload a test file2 (error: " << err << ")";
    WaitMillisec(1000);

    ofstream f2(filename2);
    f2 << "update";
    f2.close();

    err = doStartUpload(0, nullptr, filename2.c_str(),
                                 rootnode,
                                 nullptr /*fileName*/,
                                 ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
                                 nullptr /*appData*/,
                                 false   /*isSourceTemporary*/,
                                 false   /*startFirst*/,
                                 nullptr /*cancelToken*/);

    ASSERT_EQ(API_OK, err) << "Cannot upload an updated test file2 (error: " << err << ")";

    synchronousCatchup(0);


    std::unique_ptr<MegaRecentActionBucketList> buckets{megaApi[0]->getRecentActions(1, 10)};

    ostringstream logMsg;
    for (int i = 0; i < buckets->size(); ++i)
    {
        logMsg << "bucket " << to_string(i);
        megaApi[0]->log(MegaApi::LOG_LEVEL_INFO, logMsg.str().c_str());
        auto bucket = buckets->get(i);
        for (int j = 0; j < buckets->get(i)->getNodes()->size(); ++j)
        {
            auto node = bucket->getNodes()->get(j);
            logMsg << node->getName() << " " << node->getCreationTime() << " " << bucket->getTimestamp() << " " << bucket->getParentHandle() << " " << bucket->isUpdate() << " " << bucket->isMedia();
            megaApi[0]->log(MegaApi::LOG_LEVEL_DEBUG, logMsg.str().c_str());
        }
    }

    ASSERT_TRUE(buckets != nullptr);
    ASSERT_TRUE(buckets->size() > 0);
    ASSERT_TRUE(buckets->get(0)->getNodes()->size() > 1);
    ASSERT_EQ(DOWNFILE, string(buckets->get(0)->getNodes()->get(0)->getName()));
    ASSERT_EQ(UPFILE, string(buckets->get(0)->getNodes()->get(1)->getName()));
}

#ifdef USE_FREEIMAGE
TEST_F(SdkTest, SdkHttpReqCommandPutFATest)
{
    LOG_info << "___TEST SdkHttpReqCommandPutFATest___";
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    // SCENARIO 1: Upload image file and check thumbnail and preview
    std::unique_ptr<MegaNode> rootnode(megaApi[0]->getRootNode());
    MegaHandle uploadResultHandle = UNDEF;
    ASSERT_EQ(MegaError::API_OK, doStartUpload(0, &uploadResultHandle, IMAGEFILE.c_str(),
                                                        rootnode.get(),
                                                        nullptr /*fileName*/,
                                                        ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
                                                        nullptr /*appData*/,
                                                        false   /*isSourceTemporary*/,
                                                        false   /*startFirst*/,
                                                        nullptr /*cancelToken*/))  << "Uploaded file with wrong name (error: " << mApi[0].lastError << ")";



    std::unique_ptr<MegaNode> n1(megaApi[0]->getNodeByHandle(uploadResultHandle));
    ASSERT_NE(n1, nullptr);
    ASSERT_STREQ(IMAGEFILE.c_str(), n1->getName()) << "Uploaded file with wrong name (error: " << mApi[0].lastError << ")";

    // Get the thumbnail of the uploaded image
    std::string thumbnailPath = "logo_thumbnail.png";
    ASSERT_EQ(API_OK, doGetThumbnail(0, n1.get(), thumbnailPath.c_str()));

    // Get the preview of the uploaded image
    std::string previewPath = "logo_preview.png";
    ASSERT_EQ(API_OK, doGetPreview(0, n1.get(), previewPath.c_str()));

    // SCENARIO 2: Request FA upload URLs (thumbnail and preview)
    int64_t fileSize_thumbnail = 2295;
    int64_t fileSize_preview = 2376;

    // Request a thumbnail upload URL
    std::string thumbnailURL;
    ASSERT_EQ(API_OK, doGetThumbnailUploadURL(0, thumbnailURL, n1->getHandle(), fileSize_thumbnail, true)) << "Cannot request thumbnail upload URL";
    ASSERT_FALSE(thumbnailURL.empty()) << "Got empty thumbnail upload URL";

    // Request a preview upload URL
    std::string previewURL;
    ASSERT_EQ(API_OK, doGetPreviewUploadURL(0, previewURL, n1->getHandle(), fileSize_preview, true)) << "Cannot request preview upload URL";
    ASSERT_FALSE(previewURL.empty()) << "Got empty preview upload URL";
}
#endif

#ifndef __APPLE__ // todo: enable for Mac (needs work in synchronousMediaUploadComplete)
TEST_F(SdkTest, SdkMediaImageUploadTest)
{
    LOG_info << "___TEST MediaUploadRequestURL___";
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    unsigned int apiIndex = 0;
    int64_t fileSize = 1304;
    const char* outputImage = "newlogo.png";
    synchronousMediaUpload(apiIndex, fileSize, IMAGEFILE.c_str(), IMAGEFILE_C.c_str(), outputImage, THUMBNAIL.c_str(), PREVIEW.c_str());

}

TEST_F(SdkTest, SdkMediaUploadTest)
{
    LOG_info << "___TEST MediaUploadRequestURL___";
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    unsigned int apiIndex = 0;
    int64_t fileSize = 10000;
    string filename = UPFILE;
    ASSERT_TRUE(createFile(filename, false)) << "Couldnt create " << filename;
    const char* outputFile = "newfile.txt";
    synchronousMediaUpload(apiIndex, fileSize, filename.c_str(), DOWNFILE.c_str(), outputFile);

}
#endif

TEST_F(SdkTest, SdkGetPricing)
{
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));
    LOG_info << "___TEST GetPricing___";

    auto err = synchronousGetPricing(0);
    ASSERT_TRUE(err == API_OK) << "Get pricing failed (error: " << err << ")";

    ASSERT_TRUE(strcmp(mApi[0].mMegaCurrency->getCurrencyName(), "EUR") == 0) << "Unexpected currency";
}

TEST_F(SdkTest, SdkGetBanners)
{
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));
    LOG_info << "___TEST GetBanners___";

    auto err = synchronousGetBanners(0);
    ASSERT_TRUE(err == API_OK || err == API_ENOENT) << "Get banners failed (error: " << err << ")";
}

TEST_F(SdkTest, SdkLocalPath_leafOrParentName)
{
    char pathSep = LocalPath::localPathSeparator_utf8;

    string rootName;
    string rootDrive;
#ifdef WIN32
    rootName = "D";
    rootDrive = rootName + ':';
#endif

    // "D:\\foo\\bar.txt" or "/foo/bar.txt"
    LocalPath lp = LocalPath::fromAbsolutePath(rootDrive + pathSep + "foo" + pathSep + "bar.txt");
    ASSERT_EQ(lp.leafOrParentName(), "bar.txt");

    // "D:\\foo\\" or "/foo/"
    lp = LocalPath::fromAbsolutePath(rootDrive + pathSep + "foo" + pathSep);
    ASSERT_EQ(lp.leafOrParentName(), "foo");

    // "D:\\foo" or "/foo"
    lp = LocalPath::fromAbsolutePath(rootDrive + pathSep + "foo");
    ASSERT_EQ(lp.leafOrParentName(), "foo");

    // "D:\\" or "/"
    lp = LocalPath::fromAbsolutePath(rootDrive + pathSep);
    ASSERT_EQ(lp.leafOrParentName(), rootName);

#ifdef WIN32
    // "D:"
    lp = LocalPath::fromAbsolutePath(rootDrive);
    ASSERT_EQ(lp.leafOrParentName(), rootName);

    // "D"
    lp = LocalPath::fromAbsolutePath(rootName);
    ASSERT_EQ(lp.leafOrParentName(), rootName);

    // Current implementation prevents the following from working correctly on *nix platforms

    // "D:\\foo\\bar\\.\\" or "/foo/bar/./"
    lp = LocalPath::fromAbsolutePath(rootDrive + pathSep + "foo" + pathSep + "bar" + pathSep + '.' + pathSep);
    ASSERT_EQ(lp.leafOrParentName(), "bar");

    // "D:\\foo\\bar\\." or "/foo/bar/."
    lp = LocalPath::fromAbsolutePath(rootDrive + pathSep + "foo" + pathSep + "bar" + pathSep + '.');
    ASSERT_EQ(lp.leafOrParentName(), "bar");

    // "D:\\foo\\bar\\..\\" or "/foo/bar/../"
    lp = LocalPath::fromAbsolutePath(rootDrive + pathSep + "foo" + pathSep + "bar" + pathSep + ".." + pathSep);
    ASSERT_EQ(lp.leafOrParentName(), "foo");

    // "D:\\foo\\bar\\.." or "/foo/bar/.."
    lp = LocalPath::fromAbsolutePath(rootDrive + pathSep + "foo" + pathSep + "bar" + pathSep + "..");
    ASSERT_EQ(lp.leafOrParentName(), "foo");
#endif

    // ".\\foo\\" or "./foo/"
    lp = LocalPath::fromRelativePath(string(".") + pathSep + "foo" + pathSep);
    ASSERT_EQ(lp.leafOrParentName(), "foo");

    // ".\\foo" or "./foo"
    lp = LocalPath::fromRelativePath(string(".") + pathSep + "foo");
    ASSERT_EQ(lp.leafOrParentName(), "foo");
}

TEST_F(SdkTest, SdkSimpleCommands)
{
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));
    LOG_info << "___TEST SimpleCommands___";

    // fetchTimeZone() test
    auto err = synchronousFetchTimeZone(0);
    ASSERT_EQ(API_OK, err) << "Fetch time zone failed (error: " << err << ")";
    ASSERT_TRUE(mApi[0].tzDetails && mApi[0].tzDetails->getNumTimeZones()) << "Invalid Time Zone details"; // some simple validation

    // getMiscFlags() -- not logged in
    logout(0, false, maxTimeout);
    gSessionIDs[0] = "invalid";
    err = synchronousGetMiscFlags(0);
    ASSERT_EQ(API_OK, err) << "Get misc flags failed (error: " << err << ")";

    // getUserEmail() test
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));
    std::unique_ptr<MegaUser> user(megaApi[0]->getMyUser());
    ASSERT_TRUE(!!user); // some simple validation

    err = synchronousGetUserEmail(0, user->getHandle());
    ASSERT_EQ(API_OK, err) << "Get user email failed (error: " << err << ")";
    ASSERT_NE(mApi[0].email.find('@'), std::string::npos); // some simple validation

    // cleanRubbishBin() test (accept both success and already empty statuses)
    err = synchronousCleanRubbishBin(0);
    ASSERT_TRUE(err == API_OK || err == API_ENOENT) << "Clean rubbish bin failed (error: " << err << ")";

    // getMiscFlags() -- not logged in
    logout(0, false, maxTimeout);
    gSessionIDs[0] = "invalid";
    err = synchronousGetMiscFlags(0);
    ASSERT_EQ(API_OK, err) << "Get misc flags failed (error: " << err << ")";
}

TEST_F(SdkTest, SdkHeartbeatCommands)
{
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));
    LOG_info << "___TEST HeartbeatCommands___";
    std::vector<std::pair<string, MegaHandle>> backupNameToBackupId;

    // setbackup test
    fs::path localtestroot = makeNewTestRoot();
    string localFolder = localtestroot.string();
    std::unique_ptr<MegaNode> rootnode{ megaApi[0]->getRootNode() };
    int backupType = BackupType::CAMERA_UPLOAD;
    int state = 1;
    int subState = 3;

    size_t numBackups = 3;
    vector<string> backupNames {"/SdkBackupNamesTest1", "/SdkBackupNamesTest2", "/SdkBackupNamesTest3" };
    vector<string> folderNames {"CommandBackupPutTest1", "CommandBackupPutTest2", "CommandBackupPutTest3" };
    vector<MegaHandle> targetNodes;

    // create remote folders for each backup
    for (size_t i = 0; i < numBackups; i++)
    {
        auto h = createFolder(0, folderNames[i].c_str(), rootnode.get());
        ASSERT_NE(h, UNDEF);
        targetNodes.push_back(h);
    }

    // set all backups, only wait for completion of the third one
    size_t lastIndex = numBackups - 1;
    for (size_t i = 0; i < lastIndex; i++)
    {
        megaApi[0]->setBackup(backupType, targetNodes[i], localFolder.c_str(), backupNames[i].c_str(), state, subState,
            new OneShotListener([&](MegaError& e, MegaRequest& r) {
                if (e.getErrorCode() == API_OK)
                {
                    backupNameToBackupId.emplace_back(r.getName(), r.getParentHandle());
                }
            }));
    }

    auto err = synchronousSetBackup(0,
        [&](MegaError& e, MegaRequest& r) {
            if (e.getErrorCode() == API_OK)
            {
                backupNameToBackupId.emplace_back(r.getName(), r.getParentHandle());
            }
        },
        backupType, targetNodes[lastIndex], localFolder.c_str(), backupNames[lastIndex].c_str(), state, subState);

    ASSERT_EQ(API_OK, err) << "setBackup failed (error: " << err << ")";
    ASSERT_EQ(backupNameToBackupId.size(), numBackups) << "setBackup didn't register all the backups";

    // update backup
    err = synchronousUpdateBackup(0, mBackupId, MegaApi::BACKUP_TYPE_INVALID, UNDEF, nullptr, nullptr, -1, -1);
    ASSERT_EQ(API_OK, err) << "updateBackup failed (error: " << err << ")";

    // now remove all backups, only wait for completion of the third one
    // (automatically updates the user's attribute, removing the entry for the backup id)
    for (size_t i = 0; i < lastIndex; i++)
    {
        megaApi[0]->removeBackup(backupNameToBackupId[i].second);
    }
    synchronousRemoveBackup(0, backupNameToBackupId[lastIndex].second);

    // add a backup again
    err = synchronousSetBackup(0,
            [&](MegaError& e, MegaRequest& r) {
                if (e.getErrorCode() == API_OK) backupNameToBackupId.emplace_back(r.getName(), r.getParentHandle());
            },
            backupType, targetNodes[0], localFolder.c_str(), backupNames[0].c_str(), state, subState);
    ASSERT_EQ(API_OK, err) << "setBackup failed (error: " << err << ")";

    // check heartbeat
    err = synchronousSendBackupHeartbeat(0, mBackupId, 1, 10, 1, 1, 0, targetNodes[0]);
    ASSERT_EQ(API_OK, err) << "sendBackupHeartbeat failed (error: " << err << ")";


    // --- negative test cases ---
    gTestingInvalidArgs = true;

    // register the same backup twice: should work fine
    err = synchronousSetBackup(0,
        [&](MegaError& e, MegaRequest& r) {
            if (e.getErrorCode() == API_OK) backupNameToBackupId.emplace_back(r.getName(), r.getParentHandle());
        },
        backupType, targetNodes[0], localFolder.c_str(), backupNames[0].c_str(), state, subState);

    ASSERT_EQ(API_OK, err) << "setBackup failed (error: " << err << ")";

    // update a removed backup: should throw an error
    err = synchronousRemoveBackup(0, mBackupId, nullptr);
    ASSERT_EQ(API_OK, err) << "removeBackup failed (error: " << err << ")";
    err = synchronousUpdateBackup(0, mBackupId, BackupType::INVALID, UNDEF, nullptr, nullptr, -1, -1);
    ASSERT_EQ(API_OK, err) << "updateBackup for deleted backup should succeed now, and revive the record. But, error: " << err;

    // We can't test this, as reviewer wants an assert to fire for EARGS
    //// create a backup with a big status: should report an error
    //err = synchronousSetBackup(0,
    //        nullptr,
    //        backupType, targetNodes[0], localFolder.c_str(), backupNames[0].c_str(), 255/*state*/, subState);
    //ASSERT_NE(API_OK, err) << "setBackup failed (error: " << err << ")";

    gTestingInvalidArgs = false;
}

TEST_F(SdkTest, SdkFavouriteNodes)
{
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));
    LOG_info << "___TEST SDKFavourites___";

    unique_ptr<MegaNode> rootnodeA(megaApi[0]->getRootNode());

    ASSERT_TRUE(rootnodeA);

    auto nh = createFolder(0, "folder-A", rootnodeA.get());
    ASSERT_NE(nh, UNDEF);
    unique_ptr<MegaNode> folderA(megaApi[0]->getNodeByHandle(nh));
    ASSERT_TRUE(!!folderA);

    nh = createFolder(0, "sub-folder-A", folderA.get());
    ASSERT_NE(nh, UNDEF);
    unique_ptr<MegaNode> subFolderA(megaApi[0]->getNodeByHandle(nh));
    ASSERT_TRUE(!!subFolderA);

    string filename1 = UPFILE;
    ASSERT_TRUE(createFile(filename1, false)) << "Couldn't create " << filename1;

    MegaHandle h = UNDEF;
    ASSERT_EQ(MegaError::API_OK, doStartUpload(0, &h, filename1.c_str(),
                                                        subFolderA.get(),
                                                        nullptr /*fileName*/,
                                                        ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
                                                        nullptr /*appData*/,
                                                        false   /*isSourceTemporary*/,
                                                        false   /*startFirst*/,
                                                        nullptr /*cancelToken*/)) << "Cannot upload a test file";

    std::unique_ptr<MegaNode> n1(megaApi[0]->getNodeByHandle(h));

    bool null_pointer = (n1.get() == nullptr);
    ASSERT_FALSE(null_pointer) << "Cannot initialize test scenario (error: " << mApi[0].lastError << ")";

    auto err = synchronousSetNodeFavourite(0, subFolderA.get(), true);
    err = synchronousSetNodeFavourite(0, n1.get(), true);

    err = synchronousGetFavourites(0, subFolderA.get(), 0);
    ASSERT_EQ(API_OK, err) << "synchronousGetFavourites (error: " << err << ")";
    ASSERT_EQ(mMegaFavNodeList->size(), 2u) << "synchronousGetFavourites failed...";
    err = synchronousGetFavourites(0, nullptr, 1);
    ASSERT_EQ(mMegaFavNodeList->size(), 1u) << "synchronousGetFavourites failed...";
    unique_ptr<MegaNode> favNode(megaApi[0]->getNodeByHandle(mMegaFavNodeList->get(0)));
    ASSERT_EQ(favNode->getName(), UPFILE) << "synchronousGetFavourites failed with node passed nullptr";
}

TEST_F(SdkTest, SdkDeviceNames)
{
    /// Run this before other tests that use device name, like SdkBackupFolder

    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));
    LOG_info << "___TEST SdkDeviceNames___";

    // test setter/getter
    string deviceName = string("SdkDeviceNamesTest_") + getCurrentTimestamp(true);
    auto err = synchronousSetDeviceName(0, deviceName.c_str());
    ASSERT_EQ(API_OK, err) << "setDeviceName failed (error: " << err << ")";
    err = synchronousGetDeviceName(0);
    ASSERT_EQ(API_OK, err) << "getDeviceName failed (error: " << err << ")";
    ASSERT_EQ(attributeValue, deviceName) << "getDeviceName returned incorrect value";
}


TEST_F(SdkTest, SdkBackupFolder)
{
    /// Run this after SdkDeviceNames test that changes device name.

    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));
    LOG_info << "___TEST BackupFolder___";

    // get timestamp
    string timestamp = getCurrentTimestamp(true);

    // look for Device Name attr
    string deviceName;
    bool deviceNameWasSetByCurrentTest = false;
    if (synchronousGetDeviceName(0) == API_OK && !attributeValue.empty())
    {
        deviceName = attributeValue;
    }
    else
    {
        deviceName = "Jenkins " + timestamp;
        synchronousSetDeviceName(0, deviceName.c_str());

        // make sure Device Name attr was set
        int err = synchronousGetDeviceName(0);
        ASSERT_TRUE(err == API_OK) << "Getting device name attr failed (error: " << err << ")";
        ASSERT_EQ(deviceName, attributeValue) << "Getting device name attr failed (wrong value)";
        deviceNameWasSetByCurrentTest = true;
    }

#ifdef ENABLE_SYNC
    // create My Backups folder
    syncTestMyBackupsRemoteFolder(0);
    MegaHandle mh = mApi[0].lastSyncBackupId;

    // Create a test root directory
    fs::path localBasePath = makeNewTestRoot();

    // request to backup a folder
    fs::path localFolderPath = localBasePath / "LocalBackedUpFolder";
    fs::create_directories(localFolderPath);
    const string backupNameStr = string("RemoteBackupFolder_") + timestamp;
    const char* backupName = backupNameStr.c_str();
    MegaHandle newSyncRootNodeHandle = UNDEF;
    int err = synchronousSyncFolder(0, &newSyncRootNodeHandle, MegaSync::TYPE_BACKUP, localFolderPath.u8string().c_str(), backupName, INVALID_HANDLE, nullptr);
    ASSERT_TRUE(err == API_OK) << "Backup folder failed (error: " << err << ")";

    // verify node attribute
    std::unique_ptr<MegaNode> backupNode(megaApi[0]->getNodeByHandle(newSyncRootNodeHandle));
    const char* deviceIdFromNode = backupNode->getDeviceId();
    ASSERT_TRUE(!deviceIdFromNode || !*deviceIdFromNode);

    unique_ptr<char[]> actualRemotePath{ megaApi[0]->getNodePathByNodeHandle(newSyncRootNodeHandle) };
    // TODO: always verify the remote path was created as expected,
    // even if it needs to create a new public interface that allows
    // to retrieve the handle of the device-folder
    if (deviceNameWasSetByCurrentTest)
    {
        // Verify that the remote path was created as expected.
        // Only check this if current test has actually set the device name, otherwise the device name may have changed
        // since the backup folder has been created.
        unique_ptr<char[]> myBackupsFolder{ megaApi[0]->getNodePathByNodeHandle(mh) };
        string expectedRemotePath = string(myBackupsFolder.get()) + '/' + deviceName + '/' + backupName;
        ASSERT_EQ(expectedRemotePath, actualRemotePath.get()) << "Wrong remote path for backup";
    }

    // So we can detect when the node database has been committed.
    resetlastEvent();

    // Verify that the sync was added
    unique_ptr<MegaSyncList> allSyncs{ megaApi[0]->getSyncs() };
    ASSERT_TRUE(allSyncs && allSyncs->size()) << "API reports 0 Sync instances";
    bool found = false;
    for (int i = 0; i < allSyncs->size(); ++i)
    {
        MegaSync* megaSync = allSyncs->get(i);
        if (megaSync->getType() == MegaSync::TYPE_BACKUP &&
            megaSync->getMegaHandle() == newSyncRootNodeHandle &&
            !strcmp(megaSync->getName(), backupName) &&
            !strcmp(megaSync->getLastKnownMegaFolder(), actualRemotePath.get()))
        {
            // Make sure the sync's actually active.
            ASSERT_TRUE(megaSync->isActive()) << "Sync instance found but not active.";

            found = true;
            break;
        }
    }
    ASSERT_EQ(found, true) << "Sync instance could not be found";

    // Wait for the node database to be updated.
    ASSERT_TRUE(WaitFor([&](){ return lastEventsContains(MegaEvent::EVENT_COMMIT_DB); }, 8192));

    // Verify sync after logout / login
    string session = dumpSession();
    locallogout();
    auto tracker = asyncRequestFastLogin(0, session.c_str());
    ASSERT_EQ(API_OK, tracker->waitForResult()) << " Failed to establish a login/session for account " << 0;
    fetchnodes(0, maxTimeout); // auto-resumes one active backup
    // Verify the sync again
    allSyncs.reset(megaApi[0]->getSyncs());
    ASSERT_TRUE(allSyncs && allSyncs->size()) << "API reports 0 Sync instances, after relogin";
    found = false;

    for (int i = 0; i < allSyncs->size(); ++i)
    {
        MegaSync* megaSync = allSyncs->get(i);
        if (megaSync->getType() == MegaSync::TYPE_BACKUP &&
            megaSync->getMegaHandle() == newSyncRootNodeHandle &&
            !strcmp(megaSync->getName(), backupName) &&
            !strcmp(megaSync->getLastKnownMegaFolder(), actualRemotePath.get()))
        {
            // Make sure the sync's actually active.
            ASSERT_TRUE(megaSync->isActive()) << "Sync instance found but not active.";

            found = true;
            break;
        }
    }
    ASSERT_EQ(found, true) << "Sync instance could not be found, after logout & login";

    // Remove registered backup
    RequestTracker removeTracker(megaApi[0].get());
    megaApi[0]->removeSync(allSyncs->get(0)->getBackupId(), &removeTracker);
    ASSERT_EQ(API_OK, removeTracker.waitForResult());

    RequestTracker removeNodesTracker(megaApi[0].get());
    megaApi[0]->moveOrRemoveDeconfiguredBackupNodes(allSyncs->get(0)->getMegaHandle(), INVALID_HANDLE, &removeNodesTracker);
    ASSERT_EQ(API_OK, removeNodesTracker.waitForResult());

    allSyncs.reset(megaApi[0]->getSyncs());
    ASSERT_TRUE(!allSyncs || !allSyncs->size()) << "Registered backup was not removed";

    // Request to backup another folder
    // this time, the remote folder structure is already there
    fs::path localFolderPath2 = localBasePath / "LocalBackedUpFolder2";
    fs::create_directories(localFolderPath2);
    const string backupName2Str = string("RemoteBackupFolder2_") + timestamp;
    const char* backupName2 = backupName2Str.c_str();
    err = synchronousSyncFolder(0, nullptr, MegaSync::TYPE_BACKUP, localFolderPath2.u8string().c_str(), backupName2, INVALID_HANDLE, nullptr);
    ASSERT_TRUE(err == API_OK) << "Backup folder 2 failed (error: " << err << ")";
    allSyncs.reset(megaApi[0]->getSyncs());
    ASSERT_TRUE(allSyncs && allSyncs->size() == 1) << "Sync not found for second backup";

    // Create remote folder to be used as destination when removing second backup
    std::unique_ptr<MegaNode> remoteRootNode(megaApi[0]->getRootNode());
    auto nhrb = createFolder(0, "DestinationOfRemovedBackup", remoteRootNode.get());
    ASSERT_NE(nhrb, UNDEF) << "Error creating remote DestinationOfRemovedBackup";
    std::unique_ptr<MegaNode> remoteDestNode(megaApi[0]->getNodeByHandle(nhrb));
    ASSERT_NE(remoteDestNode.get(), nullptr) << "Error getting remote node of DestinationOfRemovedBackup";
    std::unique_ptr<MegaNodeList> destChildren(megaApi[0]->getChildren(remoteDestNode.get()));
    ASSERT_TRUE(!destChildren || !destChildren->size());

    // Remove second backup, using the option to move the contents rather than delete them
    RequestTracker removeTracker2(megaApi[0].get());
    megaApi[0]->removeSync(allSyncs->get(0)->getBackupId(), &removeTracker2);
    ASSERT_EQ(API_OK, removeTracker2.waitForResult());

    RequestTracker moveNodesTracker(megaApi[0].get());
    megaApi[0]->moveOrRemoveDeconfiguredBackupNodes(allSyncs->get(0)->getMegaHandle(), nhrb, &moveNodesTracker);
    ASSERT_EQ(API_OK, moveNodesTracker.waitForResult());

    allSyncs.reset(megaApi[0]->getSyncs());
    ASSERT_TRUE(!allSyncs || !allSyncs->size()) << "Sync not removed for second backup";
    destChildren.reset(megaApi[0]->getChildren(remoteDestNode.get()));
    ASSERT_TRUE(destChildren && destChildren->size() == 1);
    ASSERT_STREQ(destChildren->get(0)->getName(), backupName2);
#endif
}

#ifdef ENABLE_SYNC
TEST_F(SdkTest, SdkExternalDriveFolder)
{
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));
    LOG_info << "___TEST SdkExternalDriveFolder___";

    // dummy path to drive
    fs::path basePath = makeNewTestRoot();
    fs::path pathToDrive = basePath / "ExtDrive";
    fs::create_directory(pathToDrive);

    // drive name
    string driveName = "SdkExternalDriveTest_" + getCurrentTimestamp(true);

    // set drive name
    const string& pathToDriveStr = pathToDrive.u8string();
    auto err = synchronousSetDriveName(0, pathToDriveStr.c_str(), driveName.c_str());
    ASSERT_EQ(API_OK, err) << "setDriveName failed (error: " << err << ")";

    // attempt to set the same name to another drive
    fs::path pathToDrive2 = basePath / "ExtDrive2";
    fs::create_directory(pathToDrive2);
    const string& pathToDriveStr2 = pathToDrive2.u8string();
    bool oldTestLogVal = gTestingInvalidArgs;
    gTestingInvalidArgs = true;
    err = synchronousSetDriveName(0, pathToDriveStr2.c_str(), driveName.c_str());
    ASSERT_EQ(API_EEXIST, err) << "setDriveName allowed duplicated name. Should not have.";
    gTestingInvalidArgs = oldTestLogVal;

    // get drive name
    err = synchronousGetDriveName(0, pathToDriveStr.c_str());
    ASSERT_EQ(API_OK, err) << "getDriveName failed (error: " << err << ")";
    ASSERT_EQ(attributeValue, driveName) << "getDriveName returned incorrect value";

    // create My Backups folder
    syncTestMyBackupsRemoteFolder(0);
    MegaHandle mh = mApi[0].lastSyncBackupId;

    // add backup
    string bkpName = "Bkp";
    const fs::path& pathToBkp = pathToDrive / bkpName;
    fs::create_directory(pathToBkp);
    const string& pathToBkpStr = pathToBkp.u8string();
    MegaHandle backupFolderHandle = UNDEF;
    err = synchronousSyncFolder(0, &backupFolderHandle, MegaSync::SyncType::TYPE_BACKUP, pathToBkpStr.c_str(), nullptr, INVALID_HANDLE, pathToDriveStr.c_str());
    ASSERT_EQ(API_OK, err) << "sync folder failed (error: " << err << ")";
    auto backupId = mApi[0].lastSyncBackupId;

    // Verify that the remote path was created as expected
    unique_ptr<char[]> myBackupsFolder{ megaApi[0]->getNodePathByNodeHandle(mh) };
    string expectedRemotePath = string(myBackupsFolder.get()) + '/' + driveName + '/' + bkpName;
    unique_ptr<char[]> actualRemotePath{ megaApi[0]->getNodePathByNodeHandle(backupFolderHandle) };
    ASSERT_EQ(expectedRemotePath, actualRemotePath.get()) << "Wrong remote path for backup";

    // disable backup
    std::unique_ptr<MegaNode> backupNode(megaApi[0]->getNodeByHandle(backupFolderHandle));
    err = synchronousDisableSync(0, backupNode.get());
    ASSERT_EQ(API_OK, err) << "Disable sync failed (error: " << err << ")";

    // remove backup
    err = synchronousRemoveSync(0, backupId);
    ASSERT_EQ(MegaError::API_OK, err) << "Remove sync failed (error: " << err << ")";

    ASSERT_EQ(MegaError::API_OK, synchronousRemoveBackupNodes(0, backupFolderHandle));

    // reset DriveName value, before a future test
    err = synchronousSetDriveName(0, pathToDriveStr.c_str(), "");
    ASSERT_EQ(API_OK, err) << "setDriveName failed when resetting (error: " << err << ")";

    // attempt to get drive name (after being deleted)
    err = synchronousGetDriveName(0, pathToDriveStr.c_str());
    ASSERT_EQ(API_ENOENT, err) << "getDriveName not failed as it should (error: " << err << ")";
}
#endif

void SdkTest::syncTestMyBackupsRemoteFolder(unsigned apiIdx)
{
    mApi[apiIdx].lastSyncBackupId = UNDEF;
    int err = synchronousGetUserAttribute(apiIdx, MegaApi::USER_ATTR_MY_BACKUPS_FOLDER);
    EXPECT_TRUE(err == MegaError::API_OK
                || err == MegaError::API_ENOENT) << "Failed to get USER_ATTR_MY_BACKUPS_FOLDER";

    if (mApi[apiIdx].lastSyncBackupId == UNDEF)
    {
        const char* folderName = "My Backups";

        mApi[apiIdx].userUpdated = false;
        int err = synchronousSetMyBackupsFolder(apiIdx, folderName);
        EXPECT_EQ(err, MegaError::API_OK) << "Failed to set backups folder to " << folderName;
        EXPECT_TRUE(waitForResponse(&mApi[apiIdx].userUpdated)) << "User attribute update not received after " << maxTimeout << " seconds";

        unique_ptr<MegaUser> myUser(megaApi[apiIdx]->getMyUser());
        err = synchronousGetUserAttribute(apiIdx, myUser.get(), MegaApi::USER_ATTR_MY_BACKUPS_FOLDER);
        EXPECT_EQ(err, MegaError::API_OK) << "Failed to get user attribute USER_ATTR_MY_BACKUPS_FOLDER";
    }

    EXPECT_NE(mApi[apiIdx].lastSyncBackupId, UNDEF);
    unique_ptr<MegaNode> n(megaApi[apiIdx]->getNodeByHandle(mApi[apiIdx].lastSyncBackupId));
    EXPECT_NE(n, nullptr);
}

void SdkTest::resetOnNodeUpdateCompletionCBs()
{
    for_each(begin(mApi), end(mApi),
             [](PerApi& api) { if (api.mOnNodesUpdateCompletion) api.mOnNodesUpdateCompletion = nullptr; });
}

onNodesUpdateCompletion_t SdkTest::createOnNodesUpdateLambda(const MegaHandle& hfolder, int change)
{
    return [this, hfolder, change](size_t apiIndex, MegaNodeList* nodes)
           { onNodesUpdateCheck(apiIndex, hfolder, nodes, change); };
}

TEST_F(SdkTest, SdkUserAlias)
{
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));
    LOG_info << "___TEST SdkUserAlias___";

    // setup
    MegaHandle uh = UNDEF;
    if (auto u = megaApi[0]->getMyUser())
    {
        uh = u->getHandle();
    }
    else
    {
        ASSERT_TRUE(false) << "Cannot find the MegaUser for email: " << mApi[0].email;
    }

    if (uh == UNDEF)
    {
        ASSERT_TRUE(false) << "failed to get user handle for email:" << mApi[0].email;
    }

    // test setter/getter
    string alias = "UserAliasTest";
    auto err = synchronousSetUserAlias(0, uh, alias.c_str());
    ASSERT_EQ(API_OK, err) << "setUserAlias failed (error: " << err << ")";
    err = synchronousGetUserAlias(0, uh);
    ASSERT_EQ(API_OK, err) << "getUserAlias failed (error: " << err << ")";
    ASSERT_EQ(attributeValue, alias) << "getUserAlias returned incorrect value";
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

TEST_F(SdkTest, DISABLED_invalidFileNames)
{
    LOG_info << "___TEST invalidFileNames___";
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(2));

    auto aux = LocalPath::fromAbsolutePath(fs::current_path().u8string());

#if defined (__linux__) || defined (__ANDROID__)
    if (fileSystemAccess.getlocalfstype(aux) == FS_EXT)
    {
        // Escape set of characters and check if it's the expected one
        const char *name = megaApi[0]->escapeFsIncompatible("!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~", fs::current_path().c_str());
        ASSERT_TRUE (!strcmp(name, "!\"#$%&'()*+,-.%2f:;<=>?@[\\]^_`{|}~"));
        delete [] name;

        // Unescape set of characters and check if it's the expected one
        name = megaApi[0]->unescapeFsIncompatible("%21%22%23%24%25%26%27%28%29%2a%2b%2c%2d"
                                                            "%2e%2f%30%31%32%33%34%35%36%37"
                                                            "%38%39%3a%3b%3c%3d%3e%3f%40%5b"
                                                            "%5c%5d%5e%5f%60%7b%7c%7d%7e",
                                                            fs::current_path().c_str());

        ASSERT_TRUE(!strcmp(name, "%21%22%23%24%25%26%27%28%29%2a%2b%2c%2d%2e"
                                  "/%30%31%32%33%34%35%36%37%38%39%3a%3b%3c%3d%3e"
                                  "%3f%40%5b%5c%5d%5e%5f%60%7b%7c%7d%7e"));
        delete [] name;
    }
#elif defined  (__APPLE__) || defined (USE_IOS)
    if (fileSystemAccess.getlocalfstype(aux) == FS_APFS
            || fileSystemAccess.getlocalfstype(aux) == FS_HFS)
    {
        // Escape set of characters and check if it's the expected one
        const char *name = megaApi[0]->escapeFsIncompatible("!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~", fs::current_path().c_str());
        ASSERT_TRUE (!strcmp(name, "!\"#$%&'()*+,-./%3a;<=>?@[\\]^_`{|}~"));
        delete [] name;

        // Unescape set of characters and check if it's the expected one
        name = megaApi[0]->unescapeFsIncompatible("%21%22%23%24%25%26%27%28%29%2a%2b%2c%2d"
                                                            "%2e%2f%30%31%32%33%34%35%36%37"
                                                            "%38%39%3a%3b%3c%3d%3e%3f%40%5b"
                                                            "%5c%5d%5e%5f%60%7b%7c%7d%7e",
                                                            fs::current_path().c_str());

        ASSERT_TRUE(!strcmp(name, "%21%22%23%24%25%26%27%28%29%2a%2b%2c%2d%2e"
                                  "%2f%30%31%32%33%34%35%36%37%38%39:%3b%3c%3d%3e"
                                  "%3f%40%5b%5c%5d%5e%5f%60%7b%7c%7d%7e"));
        delete [] name;
    }
#elif defined(_WIN32) || defined(_WIN64)
    if (fileSystemAccess.getlocalfstype(aux) == FS_NTFS)
    {
        // Escape set of characters and check if it's the expected one
        const char *name = megaApi[0]->escapeFsIncompatible("!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~", fs::current_path().u8string().c_str());
        ASSERT_TRUE (!strcmp(name, "!%22#$%&'()%2a+,-.%2f%3a;%3c=%3e%3f@[%5c]^_`{%7c}~"));
        delete [] name;

        // Unescape set of characters and check if it's the expected one
        name = megaApi[0]->unescapeFsIncompatible("%21%22%23%24%25%26%27%28%29%2a%2b%2c%2d"
                                                            "%2e%2f%30%31%32%33%34%35%36%37"
                                                            "%38%39%3a%3b%3c%3d%3e%3f%40%5b"
                                                            "%5c%5d%5e%5f%60%7b%7c%7d%7e",
                                                            fs::current_path().u8string().c_str());

        ASSERT_TRUE(!strcmp(name, "%21\"%23%24%25%26%27%28%29*%2b%2c%2d"
                                  "%2e/%30%31%32%33%34%35%36%37"
                                  "%38%39:%3b<%3d>?%40%5b"
                                  "\\%5d%5e%5f%60%7b|%7d%7e"));

        delete [] name;
    }
#endif

    // Maps filename unescaped (original) to filename escaped (expected result): f%2ff => f/f
    std::unique_ptr<MegaStringMap> fileNamesStringMap = std::unique_ptr<MegaStringMap>{MegaStringMap::createInstance()};
    fs::path uploadPath = fs::current_path() / "upload_invalid_filenames";
    if (fs::exists(uploadPath))
    {
        fs::remove_all(uploadPath);
    }
    fs::create_directories(uploadPath);

    for (int i = 0x01; i <= 0xA0; i++)
    {
        // skip [0-9] [A-Z] [a-z]
        if ((i >= 0x30 && i <= 0x39)
                || (i >= 0x41 && i <= 0x5A)
                || (i >= 0x61 && i <= 0x7A))
        {
            continue;
        }

        // Create file with unescaped character ex: f%5cf
        char unescapedName[6];
        sprintf(unescapedName, "f%%%02xf", i);
        if (createLocalFile(uploadPath, unescapedName))
        {
            const char *unescapedFileName = megaApi[0]->unescapeFsIncompatible(unescapedName, uploadPath.u8string().c_str());
            fileNamesStringMap->set(unescapedName, unescapedFileName);
            delete [] unescapedFileName;
        }

        // Create another file with the original character if supported f\f
        if ((i >= 0x01 && i <= 0x20)
                || (i >= 0x7F && i <= 0xA0))
        {
            // Skip control characters
            continue;
        }

        char escapedName[4];
        sprintf(escapedName, "f%cf", i);
        const char *escapedFileName = megaApi[0]->escapeFsIncompatible(escapedName, uploadPath.u8string().c_str());
        if (escapedFileName && !strcmp(escapedName, escapedFileName))
        {
            // Only create those files with supported characters, those ones that need unescaping
            // has been created above
            if (createLocalFile(uploadPath, escapedName))
            {
                const char * unescapedFileName = megaApi[0]->unescapeFsIncompatible(escapedName, uploadPath.u8string().c_str());
                fileNamesStringMap->set(escapedName, unescapedFileName);
                delete [] unescapedFileName;
            }
        }
        delete [] escapedFileName;
    }

    TransferTracker uploadListener(megaApi[0].get());
    megaApi[0]->startUpload(uploadPath.u8string().c_str(),
                            std::unique_ptr<MegaNode>{megaApi[0]->getRootNode()}.get(),
                            nullptr /*fileName*/,
                            ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
                            nullptr /*appData*/,
                            false   /*isSourceTemporary*/,
                            false   /*startFirst*/,
                            nullptr /*cancelToken*/,
                            &uploadListener);

    ASSERT_EQ(API_OK, uploadListener.waitForResult());

    ::mega::unique_ptr <MegaNode> n(megaApi[0]->getNodeByPath("/upload_invalid_filenames"));
    ASSERT_TRUE(n.get());
    ::mega::unique_ptr <MegaNode> authNode(megaApi[0]->authorizeNode(n.get()));
    ASSERT_TRUE(authNode.get());
    MegaNodeList *children(authNode->getChildren());
    ASSERT_TRUE(children && children->size());

    for (int i = 0; i < children->size(); i++)
    {
        MegaNode *child = children->get(i);
        const char *uploadedName = child->getName();
        const char *uploadedNameEscaped = megaApi[0]->escapeFsIncompatible(child->getName(), uploadPath.u8string().c_str());
        const char *expectedName = fileNamesStringMap->get(uploadedNameEscaped);
        delete [] uploadedNameEscaped;

        // Conditions to check if uploaded fileName is correct:
        // 1) Escaped uploaded filename must be found in fileNamesStringMap (original filename found)
        // 2) Uploaded filename must be equal than the expected value (original filename unescaped)
        ASSERT_TRUE (uploadedName && expectedName && !strcmp(uploadedName, expectedName));
    }

    // Download files
    fs::path downloadPath = fs::current_path() / "download_invalid_filenames";
    if (fs::exists(downloadPath))
    {
        fs::remove_all(downloadPath);
    }
    fs::create_directories(downloadPath);
    TransferTracker downloadListener(megaApi[0].get());
    megaApi[0]->startDownload(authNode.get(),
                              downloadPath.u8string().c_str(),
                              nullptr  /*customName*/,
                              nullptr  /*appData*/,
                              false    /*startFirst*/,
                              nullptr  /*cancelToken*/,
                              &downloadListener);

    ASSERT_EQ(API_OK, downloadListener.waitForResult());

    for (fs::directory_iterator itpath (downloadPath); itpath != fs::directory_iterator(); ++itpath)
    {
        std::string downloadedName = itpath->path().filename().u8string();
        if (!downloadedName.compare(".") || !downloadedName.compare(".."))
        {
            continue;
        }

        // Conditions to check if downloaded fileName is correct:
        // download filename must be found in fileNamesStringMap (original filename found)
        ASSERT_TRUE(fileNamesStringMap->get(downloadedName.c_str()));
    }

#ifdef WIN32
    // double check a few well known paths
    ASSERT_EQ(fileSystemAccess.getlocalfstype(LocalPath::fromAbsolutePath("c:")), FS_NTFS);
    ASSERT_EQ(fileSystemAccess.getlocalfstype(LocalPath::fromAbsolutePath("c:\\")), FS_NTFS);
    ASSERT_EQ(fileSystemAccess.getlocalfstype(LocalPath::fromAbsolutePath("C:\\")), FS_NTFS);
    ASSERT_EQ(fileSystemAccess.getlocalfstype(LocalPath::fromAbsolutePath("C:\\Program Files")), FS_NTFS);
    ASSERT_EQ(fileSystemAccess.getlocalfstype(LocalPath::fromAbsolutePath("c:\\Program Files\\Windows NT")), FS_NTFS);
#endif

}

TEST_F(SdkTest, RecursiveUploadWithLogout)
{
    LOG_info << "___TEST RecursiveUploadWithLogout___";
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    // this one used to cause a double-delete

    // make new folders (and files) in the local filesystem - approx 90
    fs::path p = fs::current_path() / "uploadme_mega_auto_test_sdk";
    if (fs::exists(p))
    {
        fs::remove_all(p);
    }
    fs::create_directories(p);
    ASSERT_TRUE(buildLocalFolders(p.u8string().c_str(), "newkid", 3, 2, 10));

    string filename1 = UPFILE;
    ASSERT_TRUE(createFile(filename1, false)) << "Couldnt create " << filename1;
    ASSERT_EQ(MegaError::API_OK, doStartUpload(0, nullptr, filename1.c_str(),
                                                                std::unique_ptr<MegaNode>{megaApi[0]->getRootNode()}.get(),
                                                                p.filename().u8string().c_str() /*fileName*/,
                                                                ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
                                                                nullptr /*appData*/,
                                                                false   /*isSourceTemporary*/,
                                                                false   /*startFirst*/,
                                                                nullptr /*cancelToken*/)) << "Cannot upload a test file";

    // first check that uploading a folder to overwrite a file fails
    auto uploadListener1 = std::make_shared<TransferTracker>(megaApi[0].get());
    uploadListener1->selfDeleteOnFinalCallback = uploadListener1;

    megaApi[0]->startUpload(p.u8string().c_str(),
                            std::unique_ptr<MegaNode>{megaApi[0]->getRootNode()}.get(),
                            nullptr /*fileName*/,
                            ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
                            nullptr /*appData*/,
                            false   /*isSourceTemporary*/,
                            false   /*startFirst*/,
                            nullptr /*cancelToken*/,
                            uploadListener1.get());

    ASSERT_EQ(uploadListener1->waitForResult(), API_EEXIST);

    // remove the file so nothing is in the way anymore

    ASSERT_EQ(MegaError::API_OK, doDeleteNode(0, std::unique_ptr<MegaNode>{megaApi[0]->getNodeByPath(("/" + p.filename().u8string()).c_str())}.get())) << "Cannot delete a test node";



    int currentMaxUploadSpeed = megaApi[0]->getMaxUploadSpeed();
    ASSERT_EQ(true, megaApi[0]->setMaxUploadSpeed(1)); // set a small value for max upload speed (bytes per second)


    // start uploading
    // uploadListener may have to live after this function exits if the logout test below fails
    auto uploadListener = std::make_shared<TransferTracker>(megaApi[0].get());
    uploadListener->selfDeleteOnFinalCallback = uploadListener;

    megaApi[0]->startUpload(p.u8string().c_str(),
                            std::unique_ptr<MegaNode>{megaApi[0]->getRootNode()}.get(),
                            nullptr /*fileName*/,
                            ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
                            nullptr /*appData*/,
                            false   /*isSourceTemporary*/,
                            false   /*startFirst*/,
                            nullptr /*cancelToken*/,
                            uploadListener.get());
    WaitMillisec(500);

    // logout while the upload (which consists of many transfers) is ongoing
    gSessionIDs[0].clear();
#ifdef ENABLE_SYNC
    ASSERT_EQ(API_OK, doRequestLogout(0, false));
#else
    ASSERT_EQ(API_OK, doRequestLogout(0));
#endif
    gSessionIDs[0] = "invalid";

    int result = uploadListener->waitForResult();
    ASSERT_TRUE(result == API_EACCESS || result == API_EINCOMPLETE);

    auto tracker = asyncRequestLogin(0, mApi[0].email.c_str(), mApi[0].pwd.c_str());
    ASSERT_EQ(API_OK, tracker->waitForResult()) << " Failed to establish a login/session for account " << 0;
    ASSERT_EQ(true, megaApi[0]->setMaxUploadSpeed(currentMaxUploadSpeed)); // restore previous max upload speed (bytes per second)
}

TEST_F(SdkTest, RecursiveDownloadWithLogout)
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


    ASSERT_TRUE(buildLocalFolders(uploadpath.u8string().c_str(), "newkid", 3, 2, 10));

    out() << " uploading tree so we can download it";

    // upload all of those
    TransferTracker uploadListener(megaApi[0].get());
    megaApi[0]->startUpload(uploadpath.u8string().c_str(), std::unique_ptr<MegaNode>{megaApi[0]->getRootNode()}.get(),
                            nullptr /*fileName*/,
                            ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
                            nullptr /*appData*/,
                            false   /*isSourceTemporary*/,
                            false   /*startFirst*/,
                            nullptr /*cancelToken*/,
                            &uploadListener);

    ASSERT_EQ(API_OK, uploadListener.waitForResult());

    int currentMaxDownloadSpeed = megaApi[0]->getMaxDownloadSpeed();
    ASSERT_EQ(true, megaApi[0]->setMaxDownloadSpeed(1)); // set a small value for max download speed (bytes per second)

    out() << " checking download of folder to overwrite file fails";

    ASSERT_TRUE(createFile(downloadpath.u8string(), false)) << "Couldn't create " << downloadpath << " as a file";

    // ok now try the download to overwrite file
    TransferTracker downloadListener1(megaApi[0].get());
    megaApi[0]->startDownload(megaApi[0]->getNodeByPath("/uploadme_mega_auto_test_sdk"),
            downloadpath.u8string().c_str(),
            nullptr  /*customName*/,
            nullptr  /*appData*/,
            false    /*startFirst*/,
            nullptr  /*cancelToken*/,
            &downloadListener1);

    ASSERT_TRUE(downloadListener1.waitForResult() == API_EEXIST);

    fs::remove_all(downloadpath, ec);

    out() << " downloading tree and logout while it's ongoing";

    // ok now try the download
    TransferTracker downloadListener2(megaApi[0].get());
    megaApi[0]->startDownload(megaApi[0]->getNodeByPath("/uploadme_mega_auto_test_sdk"),
            downloadpath.u8string().c_str(),
            nullptr  /*customName*/,
            nullptr  /*appData*/,
            false    /*startFirst*/,
            nullptr  /*cancelToken*/,
            &downloadListener2);

    for (int i = 1000; i-- && !downloadListener2.started; ) WaitMillisec(1);
    ASSERT_TRUE(downloadListener2.started);
    ASSERT_TRUE(!downloadListener2.finished);

    // logout while the download (which consists of many transfers) is ongoing

#ifdef ENABLE_SYNC
    ASSERT_EQ(API_OK, doRequestLogout(0, false));
#else
    ASSERT_EQ(API_OK, doRequestLogout(0));
#endif
    gSessionIDs[0] = "invalid";

    int result = downloadListener2.waitForResult();
    ASSERT_TRUE(result == API_EACCESS || result == API_EINCOMPLETE);
    fs::remove_all(uploadpath, ec);
    fs::remove_all(downloadpath, ec);

    auto tracker = asyncRequestLogin(0, mApi[0].email.c_str(), mApi[0].pwd.c_str());
    ASSERT_EQ(API_OK, tracker->waitForResult()) << " Failed to establish a login/session for account " << 0;
    ASSERT_EQ(true, megaApi[0]->setMaxDownloadSpeed(currentMaxDownloadSpeed)); // restore previous max download speed (bytes per second)
}

#ifdef ENABLE_SYNC

void cleanUp(::mega::MegaApi* megaApi, const fs::path &basePath)
{
    unique_ptr<MegaSyncList> allSyncs{ megaApi->getSyncs() };
    for (int i = 0; i < allSyncs->size(); ++i)
    {
        RequestTracker rt1(megaApi);
        megaApi->removeSync(allSyncs->get(i)->getBackupId(), &rt1);
        ASSERT_EQ(API_OK, rt1.waitForResult());

        if (allSyncs->get(i)->getType() == MegaSync::TYPE_BACKUP)
        {
            RequestTracker rt2(megaApi);
            megaApi->moveOrRemoveDeconfiguredBackupNodes(allSyncs->get(i)->getMegaHandle(), INVALID_HANDLE, &rt2);
            ASSERT_EQ(API_OK, rt2.waitForResult());
        }
    }

    std::unique_ptr<MegaNode> baseNode{megaApi->getNodeByPath(("/" + basePath.u8string()).c_str())};
    if (baseNode)
    {
        RequestTracker removeTracker(megaApi);
        megaApi->remove(baseNode.get(), &removeTracker);
        ASSERT_EQ(API_OK, removeTracker.waitForResult());
    }

    std::unique_ptr<MegaNode> binNode{megaApi->getNodeByPath("//bin")};
    if (binNode)
    {
        unique_ptr<MegaNodeList> cs(megaApi->getChildren(binNode.get()));
        for (int i = 0; i < (cs ? cs->size() : 0); ++i)
        {
            RequestTracker removeTracker(megaApi);
            megaApi->remove(cs->get(i), &removeTracker);
            ASSERT_EQ(API_OK, removeTracker.waitForResult());
        }
    }

    std::error_code ignoredEc;
    fs::remove_all(basePath, ignoredEc);

}

std::unique_ptr<::mega::MegaSync> waitForSyncState(::mega::MegaApi* megaApi, ::mega::MegaNode* remoteNode, bool enabled, bool active, MegaSync::Error err)
{
    std::unique_ptr<MegaSync> sync;
    WaitFor([&megaApi, &remoteNode, &sync, enabled, active, err]() -> bool
    {
        sync.reset(megaApi->getSyncByNode(remoteNode));
        return (sync && sync->isEnabled() == enabled && sync->isActive() == active && sync->getError() == err);
    }, 30*1000);

    if (sync && sync->isEnabled() == enabled && sync->isActive() == active && sync->getError() == err)
    {
        if (!sync)
        {
            LOG_debug << "sync is now null";
        }
        return sync;
    }
    else
    {
        return nullptr; // signal that the sync never reached the expected/required state
    }
}

std::unique_ptr<::mega::MegaSync> waitForSyncState(::mega::MegaApi* megaApi, handle backupID, bool enabled, bool active, MegaSync::Error err)
{
    std::unique_ptr<MegaSync> sync;
    WaitFor([&megaApi, backupID, &sync, enabled, active, err]() -> bool
    {
        sync.reset(megaApi->getSyncByBackupId(backupID));
        return (sync && sync->isEnabled() == enabled && sync->isActive() == active && sync->getError() == err);
    }, 30*1000);

    if (sync && sync->isEnabled() == enabled && sync->isActive() == active && sync->getError() == err)
    {
        return sync;
    }
    else
    {
        return nullptr; // signal that the sync never reached the expected/required state
    }
}


TEST_F(SdkTest, SyncBasicOperations)
{
    // What we are going to test here:
    // - add syncs
    // - add sync that fails
    // - disable a sync
    // - disable a sync that fails
    // - disable a disabled sync
    // - Enable a sync
    // - Enable a sync that fails
    // - Enable an enabled sync
    // - Remove a sync
    // - Remove a sync that doesn't exist
    // - Remove a removed sync

    LOG_info << "___TEST SyncBasicOperations___";
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    fs::path basePath = "SyncBasicOperations";
    std::string syncFolder1 = "sync1";
    std::string syncFolder2 = "sync2";
    std::string syncFolder3 = "sync3";
    fs::path basePath1 = basePath / syncFolder1;
    fs::path basePath2 = basePath / syncFolder2;
    fs::path basePath3 = basePath / syncFolder3;
    const auto localPath1 = fs::current_path() / basePath1;
    const auto localPath2 = fs::current_path() / basePath2;
    const auto localPath3 = fs::current_path() / basePath3;

    ASSERT_NO_FATAL_FAILURE(cleanUp(this->megaApi[0].get(), basePath));

    // Create local directories and a files.
    fs::create_directories(localPath1);
    ASSERT_TRUE(createFile((localPath1 / "fileTest1").u8string(), false));
    fs::create_directories(localPath2);
    ASSERT_TRUE(createFile((localPath2 / "fileTest2").u8string(), false));
    fs::create_directories(localPath3);

    LOG_verbose << "SyncBasicOperations :  Creating the remote folders to be synced to.";
    std::unique_ptr<MegaNode> remoteRootNode(megaApi[0]->getRootNode());
    ASSERT_NE(remoteRootNode.get(), nullptr);
    // Sync 1
    auto nh = createFolder(0, syncFolder1.c_str(), remoteRootNode.get());
    ASSERT_NE(nh, UNDEF) << "Error creating remote folders";
    std::unique_ptr<MegaNode> remoteBaseNode1(megaApi[0]->getNodeByHandle(nh));
    ASSERT_NE(remoteBaseNode1.get(), nullptr);
    // Sync 2
    nh = createFolder(0, syncFolder2.c_str(), remoteRootNode.get());
    ASSERT_NE(nh, UNDEF) << "Error creating remote folders";
    std::unique_ptr<MegaNode> remoteBaseNode2(megaApi[0]->getNodeByHandle(nh));
    ASSERT_NE(remoteBaseNode2.get(), nullptr);
    // Sync 3
    nh = createFolder(0, syncFolder3.c_str(), remoteRootNode.get());
    ASSERT_NE(nh, UNDEF) << "Error creating remote folders";
    std::unique_ptr<MegaNode> remoteBaseNode3(megaApi[0]->getNodeByHandle(nh));
    ASSERT_NE(remoteBaseNode3.get(), nullptr);

    LOG_verbose << "SyncRemoveRemoteNode :  Add syncs";
    // Sync 1
    const auto& lp1 = localPath1.u8string();
    ASSERT_EQ(API_OK, synchronousSyncFolder(0, nullptr, MegaSync::TYPE_TWOWAY, lp1.c_str(), nullptr, remoteBaseNode1->getHandle(), nullptr)) << "API Error adding a new sync";
    ASSERT_EQ(MegaSync::NO_SYNC_ERROR, mApi[0].lastSyncError);
    std::unique_ptr<MegaSync> sync = waitForSyncState(megaApi[0].get(), remoteBaseNode1.get(), true, true, MegaSync::NO_SYNC_ERROR);
    ASSERT_TRUE(sync && sync->isActive());
    ASSERT_EQ(MegaSync::NO_SYNC_ERROR, sync->getError());
    // Sync2
    const auto& lp2 = localPath2.u8string();
    ASSERT_EQ(API_OK, synchronousSyncFolder(0, nullptr, MegaSync::TYPE_TWOWAY, lp2.c_str(), nullptr, remoteBaseNode2->getHandle(), nullptr)) << "API Error adding a new sync";
    ASSERT_EQ(MegaSync::NO_SYNC_ERROR, mApi[0].lastSyncError);
    std::unique_ptr<MegaSync> sync2 = waitForSyncState(megaApi[0].get(), remoteBaseNode2.get(), true, true, MegaSync::NO_SYNC_ERROR);
    ASSERT_TRUE(sync2 && sync2->isActive());
    ASSERT_EQ(MegaSync::NO_SYNC_ERROR, sync->getError());

    LOG_verbose << "SyncRemoveRemoteNode :  Add syncs that fail";
    {
        TestingWithLogErrorAllowanceGuard g;
        const auto& lp3 = localPath3.u8string();
        ASSERT_EQ(API_EEXIST, synchronousSyncFolder(0, nullptr, MegaSync::TYPE_TWOWAY, lp3.c_str(), nullptr, remoteBaseNode1->getHandle(), nullptr)); // Remote node is currently synced.
        ASSERT_EQ(MegaSync::ACTIVE_SYNC_SAME_PATH, mApi[0].lastSyncError);
        ASSERT_EQ(API_EEXIST, synchronousSyncFolder(0, nullptr, MegaSync::TYPE_TWOWAY, lp3.c_str(), nullptr, remoteBaseNode2->getHandle(), nullptr)); // Remote node is currently synced.
        ASSERT_EQ(MegaSync::ACTIVE_SYNC_SAME_PATH, mApi[0].lastSyncError);
        const auto& lp4 = (localPath3 / fs::path("xxxyyyzzz")).u8string();
        ASSERT_EQ(API_ENOENT, synchronousSyncFolder(0, nullptr, MegaSync::TYPE_TWOWAY, lp4.c_str(), nullptr, remoteBaseNode3->getHandle(), nullptr)); // Local resource doesn't exists.
        ASSERT_EQ(MegaSync::LOCAL_PATH_UNAVAILABLE, mApi[0].lastSyncError);
    }

    LOG_verbose << "SyncRemoveRemoteNode :  Disable a sync";
    // Sync 1
    handle backupId = sync->getBackupId();
    ASSERT_EQ(API_OK, synchronousDisableSync(0, backupId));
    sync = waitForSyncState(megaApi[0].get(), remoteBaseNode1.get(), false, false, MegaSync::NO_SYNC_ERROR);
    ASSERT_TRUE(sync && !sync->isEnabled());
    ASSERT_EQ(MegaSync::NO_SYNC_ERROR, sync->getError());

    //  Sync 2
    ASSERT_EQ(API_OK, synchronousDisableSync(0, sync2.get()));
    sync2 = waitForSyncState(megaApi[0].get(), remoteBaseNode2.get(), false, false, MegaSync::NO_SYNC_ERROR);
    ASSERT_TRUE(sync2 && !sync2->isEnabled());
    ASSERT_EQ(MegaSync::NO_SYNC_ERROR, sync->getError());

    LOG_verbose << "SyncRemoveRemoteNode :  Disable disabled syncs";
    ASSERT_EQ(API_OK, synchronousDisableSync(0, sync.get())); // Currently disabled.
    ASSERT_EQ(API_OK, synchronousDisableSync(0, backupId)); // Currently disabled.
    ASSERT_EQ(API_OK, synchronousDisableSync(0, remoteBaseNode1.get())); // Currently disabled.

    LOG_verbose << "SyncRemoveRemoteNode :  Enable Syncs";
    // Sync 1
    ASSERT_EQ(API_OK, synchronousEnableSync(0, backupId));
    ASSERT_EQ(MegaSync::NO_SYNC_ERROR, mApi[0].lastSyncError);
    sync = waitForSyncState(megaApi[0].get(), remoteBaseNode1.get(), true, true, MegaSync::NO_SYNC_ERROR);
    ASSERT_TRUE(sync && sync->isActive());
    // Sync 2
    ASSERT_EQ(API_OK, synchronousEnableSync(0, sync2.get()));
    ASSERT_EQ(MegaSync::NO_SYNC_ERROR, mApi[0].lastSyncError);
    sync2 = waitForSyncState(megaApi[0].get(), remoteBaseNode2.get(), true, true, MegaSync::NO_SYNC_ERROR);
    ASSERT_TRUE(sync2 && sync2->isActive());

    LOG_verbose << "SyncRemoveRemoteNode :  Enable syncs that fail";
    {
        TestingWithLogErrorAllowanceGuard g;

        ASSERT_EQ(API_ENOENT, synchronousEnableSync(0, 999999)); // Hope it doesn't exist.
        ASSERT_EQ(MegaSync::UNKNOWN_ERROR, mApi[0].lastSyncError); // MegaApi.h specifies that this contains the error code (not the tag)
        ASSERT_EQ(API_OK, synchronousEnableSync(0, sync2.get())); // Currently enabled, already running.
        ASSERT_EQ(MegaSync::NO_SYNC_ERROR, mApi[0].lastSyncError);  // since the sync is active, we should see its real state, and it should not have had any error code stored in it
    }

    LOG_verbose << "SyncRemoveRemoteNode :  Remove Syncs";
    // Sync 1
    ASSERT_EQ(API_OK, synchronousRemoveSync(0, backupId)) << "API Error removing the sync";
    sync.reset(megaApi[0]->getSyncByNode(remoteBaseNode1.get()));
    ASSERT_EQ(nullptr, sync.get());
    // Sync 2
    ASSERT_EQ(API_OK, synchronousRemoveSync(0, sync2 ? sync2->getBackupId() : INVALID_HANDLE)) << "API Error removing the sync";
//    ASSERT_EQ(API_OK, synchronousRemoveSync(0, sync2.get())) << "API Error removing the sync";
    // Keep sync2 not updated. Will be used later to test another removal attemp using a non-updated object.

    LOG_verbose << "SyncRemoveRemoteNode :  Remove Syncs that fail";
    {
        TestingWithLogErrorAllowanceGuard g;

        ASSERT_EQ(API_ENOENT, synchronousRemoveSync(0, 9999999)); // Hope id doesn't exist
        ASSERT_EQ(API_ENOENT, synchronousRemoveSync(0, backupId)); // currently removed.
        ASSERT_EQ(API_EARGS, synchronousRemoveSync(0, sync ? sync->getBackupId() : INVALID_HANDLE)); // currently removed.
        // Wait for sync to be effectively removed.
        std::this_thread::sleep_for(std::chrono::seconds{5});
        ASSERT_EQ(API_ENOENT, synchronousRemoveSync(0, sync2 ? sync2->getBackupId() : INVALID_HANDLE)); // currently removed.
    }

    ASSERT_NO_FATAL_FAILURE(cleanUp(this->megaApi[0].get(), basePath));
}

TEST_F(SdkTest, SyncIsNodeSyncable)
{
    LOG_info << "___TEST SyncIsNodeSyncable___";
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    fs::path basePath = "SyncIsNodeSyncable";
    std::string syncFolder1 =   "sync1";
    std::string syncFolder2 =   "sync2"; // <-- synced
    std::string  syncFolder2a =   "2a";
    std::string  syncFolder2b =   "2b";
    std::string syncFolder3 =   "sync3";

    fs::path basePath1 = basePath / syncFolder1;
    fs::path basePath2 = basePath / syncFolder2;
    fs::path basePath2a = basePath / syncFolder2 / syncFolder2a;
    fs::path basePath2b = basePath / syncFolder2 / syncFolder2b;
    fs::path basePath3 = basePath / syncFolder3;
    const auto localPath1 = fs::current_path() / basePath1;
    const auto localPath2 = fs::current_path() / basePath2;
    const auto localPath2a = fs::current_path() / basePath2a;
    const auto localPath2b = fs::current_path() / basePath2b;
    const auto localPath3 = fs::current_path() / basePath3;

    ASSERT_NO_FATAL_FAILURE(cleanUp(this->megaApi[0].get(), basePath));

    // Create local directories and a files.
    fs::create_directories(localPath1);
    ASSERT_TRUE(createFile((localPath1 / "fileTest1").u8string(), false));
    fs::create_directories(localPath2);
    ASSERT_TRUE(createFile((localPath2 / "fileTest2").u8string(), false));
    fs::create_directories(localPath2a);
    ASSERT_TRUE(createFile((localPath2a / "fileTest2a").u8string(), false));
    fs::create_directories(localPath2b);
    ASSERT_TRUE(createFile((localPath2b / "fileTest2b").u8string(), false));
    fs::create_directories(localPath3);

    LOG_verbose << "Sync.IsNodeSyncable:  Creating the remote folders to be synced to.";
    std::unique_ptr<MegaNode> remoteRootNode(megaApi[0]->getRootNode());
    ASSERT_NE(remoteRootNode.get(), nullptr);

    // SyncIsNodeSyncable
    MegaHandle nh = createFolder(0, basePath.string().c_str(), remoteRootNode.get());
    ASSERT_NE(nh, UNDEF) << "Error creating remote folders";
    std::unique_ptr<MegaNode> remoteBaseNode(megaApi[0]->getNodeByHandle(nh));
    ASSERT_NE(remoteBaseNode.get(), nullptr);
    // Sync 1
    nh = createFolder(0, syncFolder1.c_str(), remoteBaseNode.get());
    ASSERT_NE(nh, UNDEF) << "Error creating remote folders";
    std::unique_ptr<MegaNode> remoteBaseNode1(megaApi[0]->getNodeByHandle(nh));
    ASSERT_NE(remoteBaseNode1.get(), nullptr);
    // Sync 2
    nh = createFolder(0, syncFolder2.c_str(), remoteBaseNode.get());
    ASSERT_NE(nh, UNDEF) << "Error creating remote folders";
    std::unique_ptr<MegaNode> remoteBaseNode2(megaApi[0]->getNodeByHandle(nh));
    ASSERT_NE(remoteBaseNode2.get(), nullptr);
    // Sync 3
    nh = createFolder(0, syncFolder3.c_str(), remoteBaseNode.get());
    ASSERT_NE(nh, UNDEF) << "Error creating remote folders";
    std::unique_ptr<MegaNode> remoteBaseNode3(megaApi[0]->getNodeByHandle(nh));
    ASSERT_NE(remoteBaseNode3.get(), nullptr);
    // Sync 2a
    nh = createFolder(0, syncFolder2a.c_str(), remoteBaseNode2.get());
    ASSERT_NE(nh, UNDEF) << "Error creating remote folders";
    std::unique_ptr<MegaNode> remoteBaseNode2a(megaApi[0]->getNodeByHandle(nh));
    ASSERT_NE(remoteBaseNode2a.get(), nullptr);
    // Sync 2b
    nh = createFolder(0, syncFolder2b.c_str(), remoteBaseNode2.get());
    ASSERT_NE(nh, UNDEF) << "Error creating remote folders";
    std::unique_ptr<MegaNode> remoteBaseNode2b(megaApi[0]->getNodeByHandle(nh));
    ASSERT_NE(remoteBaseNode2b.get(), nullptr);

    MegaHandle handle2 = INVALID_HANDLE;
    int err = synchronousSyncFolder(0, &handle2,  MegaSync::SyncType::TYPE_TWOWAY, localPath2.u8string().c_str(), "sync test", remoteBaseNode2.get()->getHandle(), nullptr);
    /// <summary>
    ASSERT_TRUE(err == API_OK) << "Backup folder 2 failed (error: " << err << ")";

    MegaNode* node3 = megaApi[0].get()->getNodeByPath((string("/") + Utils::replace(basePath3.string(), '\\', '/')).c_str());
    ASSERT_NE(node3, (MegaNode*)NULL);
    unique_ptr<MegaError> error(megaApi[0]->isNodeSyncableWithError(node3));
    ASSERT_EQ(error->getErrorCode(), API_OK);
    ASSERT_EQ(error->getSyncError(), NO_SYNC_ERROR);

    MegaNode* node2a = megaApi[0].get()->getNodeByPath((string("/") + Utils::replace(basePath2a.string(), '\\', '/')).c_str());
    // on Windows path separator is \ but API takes /
    ASSERT_NE(node2a, (MegaNode*)NULL);
    error.reset(megaApi[0]->isNodeSyncableWithError(node2a));
    ASSERT_EQ(error->getErrorCode(), API_EEXIST);
    ASSERT_EQ(error->getSyncError(), ACTIVE_SYNC_ABOVE_PATH);

    MegaNode* baseNode = megaApi[0].get()->getNodeByPath((string("/") + Utils::replace(basePath.string(), '\\', '/')).c_str());
    // on Windows path separator is \ but API takes /
    ASSERT_NE(baseNode, (MegaNode*)NULL);
    error.reset(megaApi[0]->isNodeSyncableWithError(baseNode));
    ASSERT_EQ(error->getErrorCode(), API_EEXIST);
    ASSERT_EQ(error->getSyncError(), ACTIVE_SYNC_BELOW_PATH);
}

struct SyncListener : MegaListener
{
    // make sure callbacks are consistent - added() first, nothing after deleted(), etc.

    // map by tag for now, should be backupId when that is available

    enum syncstate_t { nonexistent, added, enabled, disabled, deleted};

    std::map<handle, syncstate_t> stateMap;

    syncstate_t& state(MegaSync* sync)
    {
        if (stateMap.find(sync->getBackupId()) == stateMap.end())
        {
            stateMap[sync->getBackupId()] = nonexistent;
        }
        return stateMap[sync->getBackupId()];
    }

    std::vector<std::string> mErrors;

    bool anyErrors = false;

    bool hasAnyErrors()
    {
        for (auto &s: mErrors)
        {
            out() << "SyncListener error: " << s;
        }
        return anyErrors;
    }

    void check(bool b, std::string e = std::string())
    {
        if (!b)
        {
            anyErrors = true;
            if (!e.empty())
            {
                mErrors.push_back(e);
                out() << "SyncListener added error: " << e;
            }
        }
    }

    void clear()
    {
        // session was logged out (locally)
        stateMap.clear();
    }

    void onSyncFileStateChanged(MegaApi* api, MegaSync* sync, std::string* localPath, int newState) override
    {
        // probably too frequent to output
        //out() << "onSyncFileStateChanged " << sync << newState;
    }

    void onSyncAdded(MegaApi* api, MegaSync* sync, int additionState) override
    {
        out() << "onSyncAdded " << toHandle(sync->getBackupId());
        check(sync->getBackupId() != UNDEF, "sync added with undef backup Id");

        check(state(sync) == nonexistent);
        state(sync) = added;
    }

    void onSyncDisabled(MegaApi* api, MegaSync* sync) override
    {
        out() << "onSyncDisabled " << toHandle(sync->getBackupId());
        check(!sync->isEnabled(), "sync enabled at onSyncDisabled");
        check(!sync->isActive(), "sync active at onSyncDisabled");
        check(state(sync) == enabled || state(sync) == added);
        state(sync) = disabled;
    }

    // "onSyncStarted" would be more accurate?
    void onSyncEnabled(MegaApi* api, MegaSync* sync) override
    {
        out() << "onSyncEnabled " << toHandle(sync->getBackupId());
        check(sync->isEnabled(), "sync disabled at onSyncEnabled");
        check(sync->isActive(), "sync not active at onSyncEnabled");
        check(state(sync) == disabled || state(sync) == added);
        state(sync) = enabled;
    }

    void onSyncDeleted(MegaApi* api, MegaSync* sync) override
    {
        out() << "onSyncDeleted " << toHandle(sync->getBackupId());
        check(state(sync) == disabled || state(sync) == added || state(sync) == enabled);
        state(sync) = nonexistent;
    }

    void onSyncStateChanged(MegaApi* api, MegaSync* sync) override
    {
        out() << "onSyncStateChanged " << toHandle(sync->getBackupId());

        check(sync->getBackupId() != UNDEF, "onSyncStateChanged with undef backup Id");

        // MegaApi doco says: "Notice that adding a sync will not cause onSyncStateChanged to be called."
        // And also: "for changes that imply other callbacks, expect that the SDK
        // will call onSyncStateChanged first, so that you can update your model only using this one."
        check(state(sync) != nonexistent);
    }

    void onGlobalSyncStateChanged(MegaApi* api) override
    {
        // just too frequent for out() really
        //out() << "onGlobalSyncStateChanged ";
    }
};

struct MegaListenerDeregisterer
{
    // register the listener on constructions
    // deregister on destruction (ie, whenever we exit the function - we may exit early if a test fails

    MegaApi* api = nullptr;
    MegaListener* listener;


    MegaListenerDeregisterer(MegaApi* a, SyncListener* l)
        : api(a), listener(l)
    {
        api->addListener(listener);
    }
    ~MegaListenerDeregisterer()
    {
        api->removeListener(listener);
    }
};


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

    ASSERT_NO_FATAL_FAILURE(cleanUp(this->megaApi[0].get(), basePath));

    SyncListener syncListener0, syncListener1;
    MegaListenerDeregisterer mld1(megaApi[0].get(), &syncListener0), mld2(megaApi[1].get(), &syncListener1);

    fs::create_directories(sync1Path);
    fs::create_directories(sync2Path);
    fs::create_directories(sync3Path);
    fs::create_directories(sync4Path);

    resetlastEvent();

    // transfer the folder and its subfolders
    TransferTracker uploadListener(megaApi[0].get());
    megaApi[0]->startUpload(basePath.u8string().c_str(),
                            megaApi[0]->getRootNode(),
                            nullptr /*fileName*/,
                            ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
                            nullptr /*appData*/,
                            false   /*isSourceTemporary*/,
                            false   /*startFirst*/,
                            nullptr /*cancelToken*/,
                            &uploadListener);

    ASSERT_EQ(API_OK, uploadListener.waitForResult());

    // loop until we get a commit to the sctable to ensure we cached the new remote nodes
    ASSERT_TRUE(WaitFor([&](){ return lastEventsContains(MegaEvent::EVENT_COMMIT_DB); }, 10000));

    auto megaNode = [this, &basePath](const std::string& p)
    {
        const auto path = "/" + basePath.u8string() + "/" + p;
        return std::unique_ptr<MegaNode>{megaApi[0]->getNodeByPath(path.c_str())};
    };

    auto syncFolder = [this, &megaNode](const fs::path& p) -> handle
    {
        RequestTracker syncTracker(megaApi[0].get());
        auto node = megaNode(p.filename().u8string());
        megaApi[0]->syncFolder(MegaSync::TYPE_TWOWAY, p.u8string().c_str(), nullptr, node ? node->getHandle() : INVALID_HANDLE, nullptr, &syncTracker);
        EXPECT_EQ(API_OK, syncTracker.waitForResult());

        return syncTracker.request->getParentHandle();
    };

    auto disableSync = [this, &megaNode](const fs::path& p)
    {
        RequestTracker syncTracker(megaApi[0].get());
        auto node = megaNode(p.filename().u8string());
        megaApi[0]->disableSync(node.get(), &syncTracker);
        ASSERT_EQ(API_OK, syncTracker.waitForResult());
    };

    auto disableSyncByBackupId = [this](handle backupId)
    {
        RequestTracker syncTracker(megaApi[0].get());
        megaApi[0]->disableSync(backupId, &syncTracker);
        ASSERT_EQ(API_OK, syncTracker.waitForResult());
    };

    auto resumeSync = [this](handle backupId)
    {
        RequestTracker syncTracker(megaApi[0].get());
        megaApi[0]->enableSync(backupId, &syncTracker);
        ASSERT_EQ(API_OK, syncTracker.waitForResult());
    };

    auto removeSync = [this](handle backupId)
    {
        RequestTracker syncTracker(megaApi[0].get());
        megaApi[0]->removeSync(backupId, &syncTracker);
        ASSERT_EQ(API_OK, syncTracker.waitForResult());
    };

    auto checkSyncOK = [this, &megaNode](const fs::path& p)
    {
        auto node = megaNode(p.filename().u8string());
        //return std::unique_ptr<MegaSync>{megaApi[0]->getSyncByNode(node.get())} != nullptr; //disabled syncs are not OK but foundable

        LOG_verbose << "checkSyncOK " << p.filename().u8string() << " node found: " << bool(node);

        std::unique_ptr<MegaSync> sync{megaApi[0]->getSyncByNode(node.get())};

        LOG_verbose << "checkSyncOK " << p.filename().u8string() << " sync found: " << bool(sync);

        if (!sync) return false;

        LOG_verbose << "checkSyncOK " << p.filename().u8string() << " sync is: " << sync->getLocalFolder();


        LOG_verbose << "checkSyncOK " << p.filename().u8string() << " enabled: " << sync->isEnabled();

        return sync->isEnabled();

    };

    auto checkSyncDisabled = [this, &megaNode](const fs::path& p)
    {
        auto node = megaNode(p.filename().u8string());
        std::unique_ptr<MegaSync> sync{megaApi[0]->getSyncByNode(node.get())};
        if (!sync) return false;
        return !sync->isEnabled();
    };


    auto reloginViaSession = [this, &session, &syncListener0]()
    {
        locallogout();  // only logs out 0
        syncListener0.clear();

        //loginBySessionId(0, session);
        auto tracker = asyncRequestFastLogin(0, session.c_str());
        ASSERT_EQ(API_OK, tracker->waitForResult()) << " Failed to establish a login/session for account " << 0;
    };

    LOG_verbose << " SyncResumptionAfterFetchNodes : syncying folders";

    handle backupId1 = syncFolder(sync1Path);  (void)backupId1;
    handle backupId2 = syncFolder(sync2Path);
    handle backupId3 = syncFolder(sync3Path);  (void)backupId3;
    handle backupId4 = syncFolder(sync4Path);  (void)backupId4;

    ASSERT_TRUE(checkSyncOK(sync1Path));
    ASSERT_TRUE(checkSyncOK(sync2Path));
    ASSERT_TRUE(checkSyncOK(sync3Path));
    ASSERT_TRUE(checkSyncOK(sync4Path));

    LOG_verbose << " SyncResumptionAfterFetchNodes : disabling sync by path";
    disableSync(sync2Path);
    LOG_verbose << " SyncResumptionAfterFetchNodes : disabling sync by tag";
    disableSyncByBackupId(backupId4);
    LOG_verbose << " SyncResumptionAfterFetchNodes : removing sync";
    removeSync(backupId3);

    // wait for the sync removals to actually take place
    std::this_thread::sleep_for(std::chrono::seconds{3});

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
    resumeSync(backupId2);
    resumeSync(backupId4);

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
    removeSync(backupId1);
    removeSync(backupId2);
    removeSync(backupId4);

    // wait for the sync removals to actually take place
    std::this_thread::sleep_for(std::chrono::seconds{5});

    ASSERT_FALSE(syncListener0.hasAnyErrors());
    ASSERT_FALSE(syncListener1.hasAnyErrors());

    ASSERT_NO_FATAL_FAILURE(cleanUp(this->megaApi[0].get(), basePath));
}

/**
 * @brief TEST_F SyncRemoteNode
 *
 * Testing remote node rename, move and remove.
 */
TEST_F(SdkTest, SyncRemoteNode)
{

    // What we are going to test here:
    // - rename remote -> Sync Fail
    // - move remote -> Sync fail
    // - remove remote -> Sync fail
    // - remove a failing sync

    LOG_info << "___TEST SyncRemoteNode___";
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    fs::path basePath = "SyncRemoteNode";
    const auto localPath = fs::current_path() / basePath;

    ASSERT_NO_FATAL_FAILURE(cleanUp(this->megaApi[0].get(), basePath));

    // Create local directory and file.
    fs::create_directories(localPath);
    ASSERT_TRUE(createFile((localPath / "fileTest1").u8string(), false));

    LOG_verbose << "SyncRemoteNode :  Creating remote folder";
    std::unique_ptr<MegaNode> remoteRootNode(megaApi[0]->getRootNode());
    ASSERT_NE(remoteRootNode.get(), nullptr);
    auto nh = createFolder(0, basePath.u8string().c_str(), remoteRootNode.get());
    ASSERT_NE(nh, UNDEF) << "Error creating remote basePath";
    std::unique_ptr<MegaNode> remoteBaseNode(megaApi[0]->getNodeByHandle(nh));
    ASSERT_NE(remoteBaseNode.get(), nullptr);

    LOG_verbose << "SyncRemoteNode :  Enabling sync";
    ASSERT_EQ(API_OK, synchronousSyncFolder(0, nullptr, MegaSync::TYPE_TWOWAY, localPath.u8string().c_str(), nullptr, remoteBaseNode->getHandle(), nullptr)) << "API Error adding a new sync";
    std::unique_ptr<MegaSync> sync = waitForSyncState(megaApi[0].get(), remoteBaseNode.get(), true, true, MegaSync::NO_SYNC_ERROR);
    ASSERT_TRUE(sync && sync->isActive());
    handle backupId = sync->getBackupId();

    {
        TestingWithLogErrorAllowanceGuard g;

        // Rename remote folder --> Sync fail
        LOG_verbose << "SyncRemoteNode :  Rename remote node with sync active.";
        std::string basePathRenamed = "SyncRemoteNodeRenamed";
        ASSERT_EQ(API_OK, doRenameNode(0, remoteBaseNode.get(), basePathRenamed.c_str()));
        sync = waitForSyncState(megaApi[0].get(), backupId, false, false, MegaSync::REMOTE_PATH_HAS_CHANGED);
        ASSERT_TRUE(sync && !sync->isEnabled() && !sync->isActive());
        ASSERT_EQ(MegaSync::REMOTE_PATH_HAS_CHANGED, sync->getError());

        LOG_verbose << "SyncRemoteNode :  Restoring remote folder name.";
        ASSERT_EQ(API_OK, doRenameNode(0, remoteBaseNode.get(), basePath.u8string().c_str()));
        ASSERT_NE(remoteBaseNode.get(), nullptr);
        sync = waitForSyncState(megaApi[0].get(), backupId, false, false, MegaSync::REMOTE_PATH_HAS_CHANGED);
        ASSERT_TRUE(sync && !sync->isEnabled() && !sync->isActive());
        ASSERT_EQ(MegaSync::REMOTE_PATH_HAS_CHANGED, sync->getError());
    }

    LOG_verbose << "SyncRemoteNode :  Enabling sync again.";
    ASSERT_EQ(API_OK, synchronousEnableSync(0, backupId)) << "API Error enabling the sync";
    sync = waitForSyncState(megaApi[0].get(), remoteBaseNode.get(), true, true, MegaSync::NO_SYNC_ERROR);
    ASSERT_TRUE(sync && sync->isActive());
    ASSERT_EQ(MegaSync::NO_SYNC_ERROR, sync->getError());

    // Move remote folder --> Sync fail

    LOG_verbose << "SyncRemoteNode :  Creating secondary folder";
    std::string movedBasePath = basePath.u8string() + "Moved";
    nh = createFolder(0, movedBasePath.c_str(), remoteRootNode.get());
    ASSERT_NE(nh, UNDEF) << "Error creating remote movedBasePath";
    std::unique_ptr<MegaNode> remoteMoveNodeParent(megaApi[0]->getNodeByHandle(nh));
    ASSERT_NE(remoteMoveNodeParent.get(), nullptr);

    {
        TestingWithLogErrorAllowanceGuard g;
        LOG_verbose << "SyncRemoteNode :  Move remote node with sync active to the secondary folder.";
        ASSERT_EQ(API_OK, doMoveNode(0, nullptr, remoteBaseNode.get(), remoteMoveNodeParent.get()));
        sync = waitForSyncState(megaApi[0].get(), backupId, false, false, MegaSync::REMOTE_PATH_HAS_CHANGED);
        ASSERT_TRUE(sync && !sync->isEnabled() && !sync->isActive());
        ASSERT_EQ(MegaSync::REMOTE_PATH_HAS_CHANGED, sync->getError());

        LOG_verbose << "SyncRemoteNode :  Moving back the remote node.";
        ASSERT_EQ(API_OK, doMoveNode(0, nullptr, remoteBaseNode.get(), remoteRootNode.get()));

        WaitMillisec(1000);

        ASSERT_NE(remoteBaseNode.get(), nullptr);
        sync = waitForSyncState(megaApi[0].get(), backupId, false, false, MegaSync::REMOTE_PATH_HAS_CHANGED);
        ASSERT_TRUE(sync && !sync->isEnabled() && !sync->isActive());
        ASSERT_EQ(MegaSync::REMOTE_PATH_HAS_CHANGED, sync->getError());
    }


    LOG_verbose << "SyncRemoteNode :  Enabling sync again.";
    ASSERT_EQ(API_OK, synchronousEnableSync(0, backupId)) << "API Error enabling the sync";

    WaitMillisec(1000);

    sync = waitForSyncState(megaApi[0].get(), remoteBaseNode.get(), true, true, MegaSync::NO_SYNC_ERROR);
    ASSERT_TRUE(sync && sync->isActive());
    ASSERT_EQ(MegaSync::NO_SYNC_ERROR, sync->getError());


    // Rename remote folder --> Sync fail
    {
        TestingWithLogErrorAllowanceGuard g;
        LOG_verbose << "SyncRemoteNode :  Rename remote node.";
        std::string renamedBasePath = basePath.u8string() + "Renamed";
        ASSERT_EQ(API_OK, doRenameNode(0, remoteBaseNode.get(), renamedBasePath.c_str()));
        sync = waitForSyncState(megaApi[0].get(), backupId, false, false, MegaSync::REMOTE_PATH_HAS_CHANGED);
        ASSERT_TRUE(sync && !sync->isEnabled() && !sync->isActive());
        ASSERT_EQ(MegaSync::REMOTE_PATH_HAS_CHANGED, sync->getError());

        LOG_verbose << "SyncRemoteNode :  Renaming back the remote node.";
        ASSERT_EQ(API_OK, doRenameNode(0, remoteBaseNode.get(), basePath.u8string().c_str()));

        WaitMillisec(1000);

        ASSERT_NE(remoteBaseNode.get(), nullptr);
        sync = waitForSyncState(megaApi[0].get(), backupId, false, false, MegaSync::REMOTE_PATH_HAS_CHANGED);
        ASSERT_TRUE(sync && !sync->isEnabled() && !sync->isActive());

        unique_ptr<char[]> pathFromNode{ megaApi[0]->getNodePath(remoteBaseNode.get()) };
        string actualPath{ pathFromNode.get() };
        string pathFromSync(sync->getLastKnownMegaFolder());
        ASSERT_EQ(actualPath, pathFromSync) << "Wrong updated path";

        ASSERT_EQ(MegaSync::REMOTE_PATH_HAS_CHANGED, sync->getError()); //the error stays until re-enabled
    }

    LOG_verbose << "SyncRemoteNode :  Enabling sync again.";
    ASSERT_EQ(API_OK, synchronousEnableSync(0, backupId)) << "API Error enabling the sync";
    sync = waitForSyncState(megaApi[0].get(), remoteBaseNode.get(), true, true, MegaSync::NO_SYNC_ERROR);
    ASSERT_TRUE(sync && sync->isActive());
    ASSERT_EQ(MegaSync::NO_SYNC_ERROR, sync->getError());


    {
        TestingWithLogErrorAllowanceGuard g;
        // Remove remote folder --> Sync fail
        LOG_verbose << "SyncRemoteNode :  Removing remote node with sync active.";
        ASSERT_EQ(API_OK, doDeleteNode(0, remoteBaseNode.get()));                                //  <--- remote node deleted!!
        sync = waitForSyncState(megaApi[0].get(), backupId, false, false, MegaSync::REMOTE_NODE_NOT_FOUND);
        ASSERT_TRUE(sync && !sync->isEnabled() && !sync->isActive());
        ASSERT_EQ(MegaSync::REMOTE_NODE_NOT_FOUND, sync->getError());

        LOG_verbose << "SyncRemoteNode :  Recreating remote folder.";
        nh = createFolder(0, basePath.u8string().c_str(), remoteRootNode.get());

        WaitMillisec(1000);

        ASSERT_NE(nh, UNDEF) << "Error creating remote basePath";
        remoteBaseNode.reset(megaApi[0]->getNodeByHandle(nh));
        ASSERT_NE(remoteBaseNode.get(), nullptr);
        sync = waitForSyncState(megaApi[0].get(), backupId, false, false, MegaSync::REMOTE_NODE_NOT_FOUND);
        ASSERT_TRUE(sync && !sync->isEnabled() && !sync->isActive());
        ASSERT_EQ(MegaSync::REMOTE_NODE_NOT_FOUND, sync->getError());
    }

    {
        TestingWithLogErrorAllowanceGuard g;
        LOG_verbose << "SyncRemoteNode :  Enabling sync again.";
        ASSERT_EQ(API_ENOENT, synchronousEnableSync(0, backupId)) << "API Error enabling the sync";  //  <--- remote node has been deleted, we should not be able to resume!!
    }
    //sync = waitForSyncState(megaApi[0].get(), remoteBaseNode.get(), true, true, MegaSync::NO_SYNC_ERROR);
    //ASSERT_TRUE(sync && sync->isActive());
    //ASSERT_EQ(MegaSync::NO_SYNC_ERROR, sync->getError());

    //{
    //    TestingWithLogErrorAllowanceGuard g;

    //    // Check if a locallogout keeps the sync configuration if the remote is removed.
    //    LOG_verbose << "SyncRemoteNode :  Removing remote node with sync active.";
    //    ASSERT_NO_FATAL_FAILURE(deleteNode(0, remoteBaseNode.get())) << "Error deleting remote basePath";;
    //    sync = waitForSyncState(megaApi[0].get(), tagID, MegaSync::SYNC_FAILED);
    //    ASSERT_TRUE(sync && !sync->isEnabled() && !sync->isActive());
    //    ASSERT_EQ(MegaSync::REMOTE_NODE_NOT_FOUND, sync->getError());
    //}

    std::string session = dumpSession();
    ASSERT_NO_FATAL_FAILURE(locallogout());
    //loginBySessionId(0, session);
    auto tracker = asyncRequestFastLogin(0, session.c_str());
    ASSERT_EQ(API_OK, tracker->waitForResult()) << " Failed to establish a login/session for account " << 0;
    ASSERT_NO_FATAL_FAILURE(fetchnodes(0));

    // since the node was deleted, path is irrelevant
    //sync.reset(megaApi[0]->getSyncByBackupId(tagID));
    //ASSERT_EQ(string(sync->getLastKnownMegaFolder()), ("/" / basePath).u8string());

    // Remove a failing sync.
    LOG_verbose << "SyncRemoteNode :  Remove failed sync";
    ASSERT_EQ(API_OK, synchronousRemoveSync(0, sync->getBackupId())) << "API Error removing the sync";
    sync.reset(megaApi[0]->getSyncByBackupId(backupId));
    ASSERT_EQ(nullptr, sync.get());

    // Wait for sync to be effectively removed.
    std::this_thread::sleep_for(std::chrono::seconds{5});

    ASSERT_NO_FATAL_FAILURE(cleanUp(this->megaApi[0].get(), basePath));
}

/**
 * @brief TEST_F SyncPersistence
 *
 * Testing configured syncs persitence
 */
TEST_F(SdkTest, SyncPersistence)
{
    // What we are going to test here:
    // - locallogut -> Syncs kept
    // - logout with setKeepSyncsAfterLogout(true) -> Syncs kept
    // - logout -> Syncs removed

    LOG_info << "___TEST SyncPersistence___";
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    // Make sure session ID is invalidated.
    gSessionIDs[0] = "invalid";

    fs::path basePath = "SyncPersistence";
    const auto localPath = fs::current_path() / basePath;

    ASSERT_NO_FATAL_FAILURE(cleanUp(this->megaApi[0].get(), basePath));

    // Create local directory and file.
    fs::create_directories(localPath);
    ASSERT_TRUE(createFile((localPath / "fileTest1").u8string(), false));

    LOG_verbose << "SyncPersistence :  Creating remote folder";
    std::unique_ptr<MegaNode> remoteRootNode(megaApi[0]->getRootNode());
    ASSERT_NE(remoteRootNode.get(), nullptr);

    resetlastEvent();

    auto nh = createFolder(0, basePath.u8string().c_str(), remoteRootNode.get());
    ASSERT_NE(nh, UNDEF) << "Error creating remote basePath";
    std::unique_ptr<MegaNode> remoteBaseNode(megaApi[0]->getNodeByHandle(nh));
    ASSERT_NE(remoteBaseNode.get(), nullptr);

    // make sure there are no outstanding cs requests in case
    // "Postponing DB commit until cs requests finish"
    // means our Sync's cloud Node is not in the db
    ASSERT_TRUE(WaitFor([&](){ return lastEventsContains(MegaEvent::EVENT_COMMIT_DB); }, 10000));


    LOG_verbose << "SyncPersistence :  Enabling sync";
    ASSERT_EQ(API_OK, synchronousSyncFolder(0, nullptr, MegaSync::TYPE_TWOWAY, localPath.u8string().c_str(), nullptr, remoteBaseNode->getHandle(), nullptr)) << "API Error adding a new sync";
    std::unique_ptr<MegaSync> sync = waitForSyncState(megaApi[0].get(), remoteBaseNode.get(), true, true, MegaSync::NO_SYNC_ERROR);
    ASSERT_TRUE(sync && sync->isActive());
    handle backupId = sync->getBackupId();
    std::string remoteFolder(sync->getLastKnownMegaFolder());

    // Check if a locallogout keeps the sync configured.
    std::string session = dumpSession();
    ASSERT_NO_FATAL_FAILURE(locallogout());
    auto trackerFastLogin = asyncRequestFastLogin(0, session.c_str());
    ASSERT_EQ(API_OK, trackerFastLogin->waitForResult()) << " Failed to establish a login/session for account " << 0;

    resetlastEvent();
    ASSERT_NO_FATAL_FAILURE(fetchnodes(0));

    // wait for the event that says all syncs (if any) have been reloaded
    ASSERT_TRUE(WaitFor([&](){ return lastEventsContains(MegaEvent::EVENT_SYNCS_RESTORED); }, 10000));

    sync = waitForSyncState(megaApi[0].get(), backupId, true, true, MegaSync::NO_SYNC_ERROR);
    ASSERT_TRUE(sync && sync->isActive());
    ASSERT_EQ(remoteFolder, string(sync->getLastKnownMegaFolder()));

    // Check if a logout with keepSyncsAfterLogout keeps the sync configured.
    ASSERT_NO_FATAL_FAILURE(logout(0, true, maxTimeout));
    gSessionIDs[0] = "invalid";
    auto trackerLogin = asyncRequestLogin(0, mApi[0].email.c_str(), mApi[0].pwd.c_str());
    ASSERT_EQ(API_OK, trackerLogin->waitForResult()) << " Failed to establish a login/session for account " << 0;
    ASSERT_NO_FATAL_FAILURE(fetchnodes(0));
    sync = waitForSyncState(megaApi[0].get(), backupId, false, false, MegaSync::LOGGED_OUT);
    ASSERT_TRUE(sync && !sync->isActive());
    ASSERT_EQ(remoteFolder, string(sync->getLastKnownMegaFolder()));

    // Check if a logout without keepSyncsAfterLogout doesn't keep the sync configured.
    ASSERT_NO_FATAL_FAILURE(logout(0, false, maxTimeout));
    gSessionIDs[0] = "invalid";
    trackerLogin = asyncRequestLogin(0, mApi[0].email.c_str(), mApi[0].pwd.c_str());
    ASSERT_EQ(API_OK, trackerLogin->waitForResult()) << " Failed to establish a login/session for account " << 0;
    ASSERT_NO_FATAL_FAILURE(fetchnodes(0));
    sync.reset(megaApi[0]->getSyncByBackupId(backupId));
    ASSERT_EQ(sync, nullptr);

    ASSERT_NO_FATAL_FAILURE(cleanUp(this->megaApi[0].get(), basePath));
}

/**
 * @brief TEST_F SyncPaths
 *
 * Testing non ascii paths and symlinks
 */
TEST_F(SdkTest, SyncPaths)
{
    // What we are going to test here:
    // - Check paths with non ascii chars and check that sync works.
    // - (No WIN32) Add a sync with non canonical path and check that it works,
    //   that symlinks are not followed and that sync path collision with
    //   symlinks involved works.

    LOG_info << "___TEST SyncPaths___";
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    string basePathStr = "SyncPaths-синхронизация";
    string fileNameStr = "fileTest1-файл";

    fs::path basePath = fs::u8path(basePathStr.c_str());
    const auto localPath = fs::current_path() / basePath;
    fs::path filePath = localPath / fs::u8path(fileNameStr.c_str());
    fs::path fileDownloadPath = fs::current_path() / fs::u8path(fileNameStr.c_str());

    ASSERT_NO_FATAL_FAILURE(cleanUp(this->megaApi[0].get(), basePath));
    ASSERT_NO_FATAL_FAILURE(cleanUp(this->megaApi[0].get(), "symlink_1A"));
    deleteFile(fileDownloadPath.u8string());

    // Create local directories

    std::error_code ignoredEc;
    fs::remove_all(localPath, ignoredEc);

    fs::create_directory(localPath);
    fs::create_directory(localPath / "level_1A");
    fs::create_directory_symlink(localPath / "level_1A", localPath / "symlink_1A");
    fs::create_directory_symlink(localPath / "level_1A", fs::current_path() / "symlink_1A");

    LOG_verbose << "SyncPaths :  Creating remote folder";
    std::unique_ptr<MegaNode> remoteRootNode(megaApi[0]->getRootNode());
    ASSERT_NE(remoteRootNode.get(), nullptr);
    auto nh = createFolder(0, basePath.u8string().c_str(), remoteRootNode.get());
    ASSERT_NE(nh, UNDEF) << "Error creating remote basePath";
    std::unique_ptr<MegaNode> remoteBaseNode(megaApi[0]->getNodeByHandle(nh));
    ASSERT_NE(remoteBaseNode.get(), nullptr);

    LOG_verbose << "SyncPersistence :  Creating sync";
    ASSERT_EQ(API_OK, synchronousSyncFolder(0, nullptr, MegaSync::TYPE_TWOWAY, localPath.u8string().c_str(), nullptr, remoteBaseNode->getHandle(), nullptr)) << "API Error adding a new sync";
    std::unique_ptr<MegaSync> sync = waitForSyncState(megaApi[0].get(), remoteBaseNode.get(), true, true, MegaSync::NO_SYNC_ERROR);
    ASSERT_TRUE(sync && sync->isActive());

    LOG_verbose << "SyncPersistence :  Adding a file and checking if it is synced: " << filePath.u8string();
    ASSERT_TRUE(createFile(filePath.u8string(), false)) << "Couldnt create " << filePath.u8string();
    std::unique_ptr<MegaNode> remoteNode;
    WaitFor([this, &remoteNode, &remoteBaseNode, fileNameStr]() -> bool
    {
        remoteNode.reset(megaApi[0]->getNodeByPath(("/" + string(remoteBaseNode->getName()) + "/" + fileNameStr).c_str()));
        return (remoteNode.get() != nullptr);
    },50*1000);
    ASSERT_NE(remoteNode.get(), nullptr);
    ASSERT_EQ(MegaError::API_OK, doStartDownload(0, remoteNode.get(),
                                                         fileDownloadPath.u8string().c_str(),
                                                         nullptr  /*customName*/,
                                                         nullptr  /*appData*/,
                                                         false    /*startFirst*/,
                                                         nullptr  /*cancelToken*/));

    ASSERT_TRUE(fileexists(fileDownloadPath.u8string()));
    deleteFile(fileDownloadPath.u8string());

#if !defined(__APPLE__)
    LOG_verbose << "SyncPersistence :  Check that symlinks are not synced.";
    std::unique_ptr<MegaNode> remoteNodeSym(megaApi[0]->getNodeByPath(("/" + string(remoteBaseNode->getName()) + "/symlink_1A").c_str()));
    ASSERT_EQ(remoteNodeSym.get(), nullptr);

    nh = createFolder(0, "symlink_1A", remoteRootNode.get());
    ASSERT_NE(nh, UNDEF) << "Error creating remote basePath";
    remoteNodeSym.reset(megaApi[0]->getNodeByHandle(nh));
    ASSERT_NE(remoteNodeSym.get(), nullptr);

#ifndef WIN32
    {
        TestingWithLogErrorAllowanceGuard g;

        LOG_verbose << "SyncPersistence :  Check that symlinks are considered when creating a sync.";
        ASSERT_EQ(API_EARGS, synchronousSyncFolder(0, nullptr, MegaSync::TYPE_TWOWAY, (fs::current_path() / "symlink_1A").u8string().c_str(), nullptr, remoteNodeSym->getHandle(), nullptr)) << "API Error adding a new sync";
        ASSERT_EQ(MegaSync::LOCAL_PATH_SYNC_COLLISION, mApi[0].lastSyncError);
    }
#endif

    // Disable the first one, create again the one with the symlink, check that it is working and check if the first fails when enabled.
    auto tagID = sync->getBackupId();
    ASSERT_EQ(API_OK, synchronousDisableSync(0, tagID)) << "API Error disabling sync";
    sync = waitForSyncState(megaApi[0].get(), tagID, false, false, MegaSync::NO_SYNC_ERROR);
    ASSERT_TRUE(sync && !sync->isEnabled());

    ASSERT_EQ(API_OK, synchronousSyncFolder(0, nullptr, MegaSync::TYPE_TWOWAY, (fs::current_path() / "symlink_1A").u8string().c_str(), nullptr, remoteNodeSym->getHandle(), nullptr)) << "API Error adding a new sync";
    std::unique_ptr<MegaSync> syncSym = waitForSyncState(megaApi[0].get(), remoteNodeSym.get(), true, true, MegaSync::NO_SYNC_ERROR);
    ASSERT_TRUE(syncSym && syncSym->isActive());

    LOG_verbose << "SyncPersistence :  Adding a file and checking if it is synced,";
    ASSERT_TRUE(createFile((localPath / "level_1A" / fs::u8path(fileNameStr.c_str())).u8string(), false));
    WaitFor([this, &remoteNode, &remoteNodeSym, fileNameStr]() -> bool
    {
        remoteNode.reset(megaApi[0]->getNodeByPath(("/" + string(remoteNodeSym->getName()) + "/" + fileNameStr).c_str()));
        return (remoteNode.get() != nullptr);
    },50*1000);
    ASSERT_NE(remoteNode.get(), nullptr);
    ASSERT_EQ(MegaError::API_OK, doStartDownload(0,remoteNode.get(),
                                                         fileDownloadPath.u8string().c_str(),
                                                         nullptr  /*customName*/,
                                                         nullptr  /*appData*/,
                                                         false    /*startFirst*/,
                                                         nullptr  /*cancelToken*/));

    ASSERT_TRUE(fileexists(fileDownloadPath.u8string()));
    deleteFile(fileDownloadPath.u8string());

#ifndef WIN32
{
        TestingWithLogErrorAllowanceGuard g;

        ASSERT_EQ(API_EARGS, synchronousEnableSync(0, tagID)) << "API Error enabling a sync";
        ASSERT_EQ(MegaSync::LOCAL_PATH_SYNC_COLLISION, mApi[0].lastSyncError);
    }
#endif

#endif

    ASSERT_NO_FATAL_FAILURE(cleanUp(this->megaApi[0].get(), basePath));
    ASSERT_NO_FATAL_FAILURE(cleanUp(this->megaApi[0].get(), "symlink_1A"));
}

/**
 * @brief TEST_F SyncOQTransitions
 *
 * Testing OQ Transitions
 */
TEST_F(SdkTest, SyncOQTransitions)
{

    // What we are going to test here:
    // - Online transitions: Sync is disabled when in OQ and enabled after OQ
    // - Offline transitions: Sync is disabled when in OQ and enabled after OQ
    // - Enabling a sync temporarily disabled.

    LOG_info << "___TEST SyncOQTransitions___";
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(2));

    string fileNameStr = "fileTest";

    fs::path basePath = "SyncOQTransitions";
    fs::path fillPath = "OQFolder";

    const auto localPath = fs::current_path() / basePath;
    fs::path filePath = localPath / fs::u8path(fileNameStr.c_str());

    ASSERT_NO_FATAL_FAILURE(cleanUp(this->megaApi[0].get(), basePath));
    ASSERT_NO_FATAL_FAILURE(cleanUp(this->megaApi[0].get(), fillPath));

    // Create local directory
    fs::create_directories(localPath);

    LOG_verbose << "SyncOQTransitions :  Creating remote folder";
    std::unique_ptr<MegaNode> remoteRootNode(megaApi[0]->getRootNode());
    ASSERT_NE(remoteRootNode.get(), nullptr);
    auto nh = createFolder(0, basePath.u8string().c_str(), remoteRootNode.get());
    ASSERT_NE(nh, UNDEF) << "Error creating remote basePath";
    std::unique_ptr<MegaNode> remoteBaseNode(megaApi[0]->getNodeByHandle(nh));
    ASSERT_NE(remoteBaseNode.get(), nullptr);
    nh = createFolder(0, fillPath.u8string().c_str(), remoteRootNode.get());
    ASSERT_NE(nh, UNDEF) << "Error creating remote fillPath";
    std::unique_ptr<MegaNode> remoteFillNode(megaApi[0]->getNodeByHandle(nh));
    ASSERT_NE(remoteFillNode.get(), nullptr);

    LOG_verbose << "SyncOQTransitions :  Creating sync";
    ASSERT_EQ(API_OK, synchronousSyncFolder(0, nullptr, MegaSync::TYPE_TWOWAY, localPath.u8string().c_str(), nullptr, remoteBaseNode->getHandle(), nullptr)) << "API Error adding a new sync";
    std::unique_ptr<MegaSync> sync = waitForSyncState(megaApi[0].get(), remoteBaseNode.get(), true, true, MegaSync::NO_SYNC_ERROR);
    ASSERT_TRUE(sync && sync->isActive());
    handle backupId = sync->getBackupId();

    LOG_verbose << "SyncOQTransitions :  Filling up storage space";
    auto importHandle = importPublicLink(0, MegaClient::MEGAURL+"/file/D4AGlbqY#Ak-OW4MP7lhnQxP9nzBU1bOP45xr_7sXnIz8YYqOBUg", remoteFillNode.get());
    std::unique_ptr<MegaNode> remote1GBFile(megaApi[0]->getNodeByHandle(importHandle));

    ASSERT_NO_FATAL_FAILURE(synchronousGetSpecificAccountDetails(0, true, false, false)); // Get account size.
    ASSERT_NE(mApi[0].accountDetails, nullptr);
    int filesNeeded = int(mApi[0].accountDetails->getStorageMax() / remote1GBFile->getSize());

    for (int i=1; i < filesNeeded; i++)
    {
        ASSERT_EQ(API_OK, doCopyNode(0, nullptr, remote1GBFile.get(), remoteFillNode.get(), (remote1GBFile->getName() + to_string(i)).c_str()));
    }
    std::unique_ptr<MegaNode> last1GBFileNode(megaApi[0]->getChildNode(remoteFillNode.get(), (remote1GBFile->getName() + to_string(filesNeeded-1)).c_str()));

    {
        TestingWithLogErrorAllowanceGuard g;

        LOG_verbose << "SyncOQTransitions :  Check that Sync is disabled due to OQ.";
        ASSERT_NO_FATAL_FAILURE(synchronousGetSpecificAccountDetails(0, true, false, false)); // Needed to ensure we know we are in OQ
        sync = waitForSyncState(megaApi[0].get(), backupId, false, false, MegaSync::STORAGE_OVERQUOTA);
        ASSERT_TRUE(sync && !sync->isEnabled() && !sync->isActive());
        ASSERT_EQ(MegaSync::STORAGE_OVERQUOTA, sync->getError());

        LOG_verbose << "SyncOQTransitions :  Check that Sync could not be enabled while disabled due to OQ.";
        ASSERT_EQ(API_EFAILED, synchronousEnableSync(0, backupId))  << "API Error enabling a sync";
        sync = waitForSyncState(megaApi[0].get(), backupId, false, false, MegaSync::STORAGE_OVERQUOTA);  // fresh snapshot of sync state
        ASSERT_TRUE(sync && !sync->isEnabled() && !sync->isActive());
        ASSERT_EQ(MegaSync::STORAGE_OVERQUOTA, sync->getError());
    }

    LOG_verbose << "SyncOQTransitions :  Free up space and check that Sync is not active again.";
    ASSERT_EQ(API_OK, synchronousRemove(0, last1GBFileNode.get()));
    ASSERT_NO_FATAL_FAILURE(synchronousGetSpecificAccountDetails(0, true, false, false)); // Needed to ensure we know we are not in OQ
    sync = waitForSyncState(megaApi[0].get(), backupId, false, false, MegaSync::STORAGE_OVERQUOTA);  // of course the error stays as OverQuota.  Sync still not re-enabled.
    ASSERT_TRUE(sync && !sync->isEnabled() && !sync->isActive());

    LOG_verbose << "SyncOQTransitions :  Share big files folder with another account.";

    ASSERT_EQ(API_OK, synchronousInviteContact(0, mApi[1].email.c_str(), "SdkTestShareKeys contact request A to B", MegaContactRequest::INVITE_ACTION_ADD));
    ASSERT_TRUE(WaitFor([this]()
    {
        return unique_ptr<MegaContactRequestList>(megaApi[1]->getIncomingContactRequests())->size() == 1;
    }, 60*1000));
    ASSERT_NO_FATAL_FAILURE(getContactRequest(1, false));
    ASSERT_EQ(API_OK, synchronousReplyContactRequest(1, mApi[1].cr.get(), MegaContactRequest::REPLY_ACTION_ACCEPT));
    ASSERT_EQ(API_OK, synchronousShare(0, remoteFillNode.get(), mApi[1].email.c_str(), MegaShare::ACCESS_FULL)) << "Folder sharing failed";
    ASSERT_TRUE(WaitFor([this]()
    {
        return unique_ptr<MegaShareList>(megaApi[1]->getInSharesList())->size() == 1;
    }, 60*1000));

    unique_ptr<MegaNodeList> nodeList(megaApi[1]->getInShares(megaApi[1]->getContact(mApi[0].email.c_str())));
    ASSERT_EQ(nodeList->size(), 1);
    MegaNode* inshareNode = nodeList->get(0);

    LOG_verbose << "SyncOQTransitions :  Check for transition to OQ while offline.";
    std::string session = dumpSession();
    ASSERT_NO_FATAL_FAILURE(locallogout());

    std::unique_ptr<MegaNode> remote1GBFile2nd(megaApi[1]->getChildNode(inshareNode, remote1GBFile->getName()));
    ASSERT_EQ(API_OK, doCopyNode(1, nullptr, remote1GBFile2nd.get(), inshareNode, (remote1GBFile2nd->getName() + to_string(filesNeeded-1)).c_str()));

    {
        TestingWithLogErrorAllowanceGuard g;

        ASSERT_NO_FATAL_FAILURE(resumeSession(session.c_str()));   // sync not actually resumed here though (though it would be if it was still enabled)
        ASSERT_NO_FATAL_FAILURE(fetchnodes(0));
        ASSERT_NO_FATAL_FAILURE(synchronousGetSpecificAccountDetails(0, true, false, false)); // Needed to ensure we know we are in OQ
        sync = waitForSyncState(megaApi[0].get(), backupId, false, false, MegaSync::STORAGE_OVERQUOTA);
        ASSERT_TRUE(sync && !sync->isEnabled() && !sync->isActive());
        ASSERT_EQ(MegaSync::STORAGE_OVERQUOTA, sync->getError());
    }

    LOG_verbose << "SyncOQTransitions :  Check for transition from OQ while offline.";
    ASSERT_NO_FATAL_FAILURE(locallogout());

    std::unique_ptr<MegaNode> toRemoveNode(megaApi[1]->getChildNode(inshareNode, (remote1GBFile->getName() + to_string(filesNeeded-1)).c_str()));
    ASSERT_EQ(API_OK, synchronousRemove(1, toRemoveNode.get()));

    ASSERT_NO_FATAL_FAILURE(resumeSession(session.c_str()));
    ASSERT_NO_FATAL_FAILURE(fetchnodes(0));
    ASSERT_NO_FATAL_FAILURE(synchronousGetSpecificAccountDetails(0, true, false, false)); // Needed to ensure we know we are no longer in OQ
    sync = waitForSyncState(megaApi[0].get(), backupId, false, false, MegaSync::STORAGE_OVERQUOTA);
    ASSERT_TRUE(sync && !sync->isEnabled() && !sync->isActive());

    ASSERT_NO_FATAL_FAILURE(cleanUp(this->megaApi[0].get(), basePath));
    ASSERT_NO_FATAL_FAILURE(cleanUp(this->megaApi[0].get(), fillPath));
}

/**
 * @brief TEST_F StressTestSDKInstancesOverWritableFolders
 *
 * Testing multiple SDK instances working in parallel
 */

// dgw: This test will consistently fail on Linux unless we raise the
//      maximum number of open file descriptors.
//
//      This is necessary as a great many PosixWaiters are created for each
//      API object. Each waiter requires us to create a pipe pair.
//
//      As such, we quickly exhaust the default limit on descriptors.
//
//      If we raise the limit, the test will run but will still encounter
//      other limits, say memory exhaustion.
TEST_F(SdkTest, DISABLED_StressTestSDKInstancesOverWritableFoldersOverWritableFolders)
{
    // What we are going to test here:
    // - Creating multiple writable folders
    // - Login and fetch nodes in separated MegaApi instances
    //   and hence in multiple SDK instances running in parallel.

    LOG_info << "___TEST StressTestSDKInstancesOverWritableFolders___";
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    std::string baseFolder = "StressTestSDKInstancesOverWritableFoldersFolder";

    int numFolders = 90;

    ASSERT_NO_FATAL_FAILURE(cleanUp(this->megaApi[0].get(), baseFolder));

    LOG_verbose << "StressTestSDKInstancesOverWritableFolders :  Creating remote folder";
    std::unique_ptr<MegaNode> remoteRootNode(megaApi[0]->getRootNode());
    ASSERT_NE(remoteRootNode.get(), nullptr);
    auto nh = createFolder(0, baseFolder.c_str(), remoteRootNode.get());
    ASSERT_NE(nh, UNDEF) << "Error creating remote basePath";
    std::unique_ptr<MegaNode> remoteBaseNode(megaApi[0]->getNodeByHandle(nh));
    ASSERT_NE(remoteBaseNode.get(), nullptr);

    // create subfolders ...
    for (int index = 0 ; index < numFolders; index++ )
    {
        string subFolderPath = string("subfolder_").append(SSTR(index));
        nh = createFolder(0, subFolderPath.c_str(), remoteBaseNode.get());
        ASSERT_NE(nh, UNDEF) << "Error creating remote subfolder";
        std::unique_ptr<MegaNode> remoteSubFolderNode(megaApi[0]->getNodeByHandle(nh));
        ASSERT_NE(remoteSubFolderNode.get(), nullptr);

        // ... with a file in it
        string filename1 = UPFILE;
        ASSERT_TRUE(createFile(filename1, false)) << "Couldnt create " << filename1;
        ASSERT_EQ(MegaError::API_OK, doStartUpload(0, nullptr, filename1.c_str(),
                                                                  remoteSubFolderNode.get(),
                                                                  nullptr /*fileName*/,
                                                                  ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
                                                                  nullptr /*appData*/,
                                                                  false   /*isSourceTemporary*/,
                                                                  false   /*startFirst*/,
                                                                  nullptr /*cancelToken*/)) << "Cannot upload a test file";
    }

    auto howMany = numFolders;

    std::vector<std::unique_ptr<RequestTracker>> trackers;
    trackers.resize(howMany);

    std::vector<std::unique_ptr<MegaApi>> exportedFolderApis;
    exportedFolderApis.resize(howMany);

    std::vector<std::string> exportedLinks;
    exportedLinks.resize(howMany);

    std::vector<std::string> authKeys;
    authKeys.resize(howMany);

    // export subfolders
    for (int index = 0 ; index < howMany; index++ )
    {
        string subFolderPath = string("subfolder_").append(SSTR(index));
        std::unique_ptr<MegaNode> remoteSubFolderNode(megaApi[0]->getNodeByPath(subFolderPath.c_str(), remoteBaseNode.get()));
        ASSERT_NE(remoteSubFolderNode.get(), nullptr);

        // ___ get a link to the file node
        string nodelink = createPublicLink(0, remoteSubFolderNode.get(), 0, 0, false/*mApi[0].accountDetails->getProLevel() == 0)*/, true/*writable*/);
        // The created link is stored in this->link at onRequestFinish()
        LOG_verbose << "StressTestSDKInstancesOverWritableFolders : " << subFolderPath << " link = " << nodelink;

        exportedLinks[index] = nodelink;

        std::unique_ptr<MegaNode> nexported(megaApi[0]->getNodeByHandle(remoteSubFolderNode->getHandle()));
        ASSERT_NE(nexported.get(), nullptr);

        if (nexported)
        {
            if (nexported->getWritableLinkAuthKey())
            {
                string authKey(nexported->getWritableLinkAuthKey());
                ASSERT_FALSE(authKey.empty());
                authKeys[index] = authKey;
            }
        }
    }

    // create apis to exported folders
    for (int index = 0 ; index < howMany; index++ )
    {
        exportedFolderApis[index].reset(
            newMegaApi(APP_KEY.c_str(),
                       megaApiCacheFolder(index + 10).c_str(),
                       USER_AGENT.c_str(),
                       static_cast<unsigned>(THREADS_PER_MEGACLIENT)));

        // reduce log level to something beareable
        exportedFolderApis[index]->setLogLevel(MegaApi::LOG_LEVEL_WARNING);
    }

    // login to exported folders
    for (int index = 0 ; index < howMany; index++ )
    {
        string nodelink = exportedLinks[index];
        string authKey = authKeys[index];

        out() << "login to exported folder " << index;
        trackers[index] = asyncRequestLoginToFolder(exportedFolderApis[index].get(), nodelink.c_str(), authKey.c_str());
    }

    // wait for login to complete:
    for (int index = 0; index < howMany; ++index)
    {
        ASSERT_EQ(API_OK, trackers[index]->waitForResult()) << " Failed to fetchnodes for accout " << index;
    }

    // perform parallel fetchnodes for each
    for (int index = 0; index < howMany; ++index)
    {
        out() << "Fetching nodes for account " << index;
        trackers[index] = asyncRequestFetchnodes(exportedFolderApis[index].get());
    }

    // wait for fetchnodes to complete:
    for (int index = 0; index < howMany; ++index)
    {
        ASSERT_EQ(API_OK, trackers[index]->waitForResult()) << " Failed to fetchnodes for accout " << index;
    }

    // In case the last test exited without cleaning up (eg, debugging etc)
    Cleanup();
}

/**
 * @brief TEST_F StressTestSDKInstancesOverWritableFolders
 *
 * Testing multiple SDK instances working in parallel
 */
TEST_F(SdkTest, WritableFolderSessionResumption)
{
    // What we are going to test here:
    // - Creating multiple writable folders
    // - Login and fetch nodes in separated MegaApi instances
    //   and hence in multiple SDK instances running in parallel.

    LOG_info << "___TEST WritableFolderSessionResumption___";
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    std::string baseFolder = "WritableFolderSessionResumption";

    unsigned numFolders = 1;

    ASSERT_NO_FATAL_FAILURE(cleanUp(this->megaApi[0].get(), baseFolder));

    LOG_verbose << "WritableFolderSessionResumption :  Creating remote folder";
    std::unique_ptr<MegaNode> remoteRootNode(megaApi[0]->getRootNode());
    ASSERT_NE(remoteRootNode.get(), nullptr);
    auto nh = createFolder(0, baseFolder.c_str(), remoteRootNode.get());
    ASSERT_NE(nh, UNDEF) << "Error creating remote basePath";
    std::unique_ptr<MegaNode> remoteBaseNode(megaApi[0]->getNodeByHandle(nh));
    ASSERT_NE(remoteBaseNode.get(), nullptr);

    // create subfolders ...
    for (unsigned index = 0 ; index < numFolders; index++ )
    {
        string subFolderPath = string("subfolder_").append(SSTR(index));
        nh = createFolder(0, subFolderPath.c_str(), remoteBaseNode.get());
        ASSERT_NE(nh, UNDEF) << "Error creating remote subfolder";
        std::unique_ptr<MegaNode> remoteSubFolderNode(megaApi[0]->getNodeByHandle(nh));
        ASSERT_NE(remoteSubFolderNode.get(), nullptr);

        // ... with a file in it
        string filename1 = UPFILE;
        ASSERT_TRUE(createFile(filename1, false)) << "Couldnt create " << filename1;
        ASSERT_EQ(MegaError::API_OK, doStartUpload(0, nullptr, filename1.c_str(),
                                                            remoteSubFolderNode.get(),
                                                            nullptr /*fileName*/,
                                                            ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
                                                            nullptr /*appData*/,
                                                            false   /*isSourceTemporary*/,
                                                            false   /*startFirst*/,
                                                            nullptr /*cancelToken*/)) << "Cannot upload a test file";
    }

    auto howMany = numFolders;

    std::vector<std::unique_ptr<RequestTracker>> trackers;
    trackers.resize(howMany);

    std::vector<std::unique_ptr<MegaApi>> exportedFolderApis;
    exportedFolderApis.resize(howMany);

    std::vector<std::string> exportedLinks;
    exportedLinks.resize(howMany);

    std::vector<std::string> authKeys;
    authKeys.resize(howMany);

    std::vector<std::string> sessions;
    sessions.resize(howMany);

    // export subfolders
    for (unsigned index = 0 ; index < howMany; index++ )
    {
        string subFolderPath = string("subfolder_").append(SSTR(index));
        std::unique_ptr<MegaNode> remoteSubFolderNode(megaApi[0]->getNodeByPath(subFolderPath.c_str(), remoteBaseNode.get()));
        ASSERT_NE(remoteSubFolderNode.get(), nullptr);

        // ___ get a link to the file node
        string nodelink = createPublicLink(0, remoteSubFolderNode.get(), 0, 0, false/*mApi[0].accountDetails->getProLevel() == 0)*/, true/*writable*/);
        // The created link is stored in this->link at onRequestFinish()
        LOG_verbose << "WritableFolderSessionResumption : " << subFolderPath << " link = " << nodelink;

        exportedLinks[index] = nodelink;

        std::unique_ptr<MegaNode> nexported(megaApi[0]->getNodeByHandle(remoteSubFolderNode->getHandle()));
        ASSERT_NE(nexported.get(), nullptr);

        if (nexported)
        {
            if (nexported->getWritableLinkAuthKey())
            {
                string authKey(nexported->getWritableLinkAuthKey());
                ASSERT_FALSE(authKey.empty());
                authKeys[index] = authKey;
            }
        }
    }

    ASSERT_NO_FATAL_FAILURE( logout(0, false, maxTimeout) );
    gSessionIDs[0] = "invalid";

    // create apis to exported folders
    for (unsigned index = 0 ; index < howMany; index++ )
    {
        exportedFolderApis[index].reset(
            newMegaApi(APP_KEY.c_str(),
                       megaApiCacheFolder(static_cast<int>(index) + 10).c_str(),
                       USER_AGENT.c_str(),
                       static_cast<unsigned>(THREADS_PER_MEGACLIENT)));

        // reduce log level to something beareable
        exportedFolderApis[index]->setLogLevel(MegaApi::LOG_LEVEL_WARNING);
    }

    // login to exported folders
    for (unsigned index = 0 ; index < howMany; index++ )
    {
        string nodelink = exportedLinks[index];
        string authKey = authKeys[index];

        out() << logTime() << "login to exported folder " << index;
        trackers[index] = asyncRequestLoginToFolder(exportedFolderApis[index].get(), nodelink.c_str(), authKey.c_str());
    }

    // wait for login to complete:
    for (unsigned index = 0; index < howMany; ++index)
    {
        ASSERT_EQ(API_OK, trackers[index]->waitForResult()) << " Failed to fetchnodes for account " << index;
    }

    // perform parallel fetchnodes for each
    for (unsigned index = 0; index < howMany; ++index)
    {
        out() << logTime() << "Fetching nodes for account " << index;
        trackers[index] = asyncRequestFetchnodes(exportedFolderApis[index].get());
    }

    // wait for fetchnodes to complete:
    for (unsigned index = 0; index < howMany; ++index)
    {
        ASSERT_EQ(API_OK, trackers[index]->waitForResult()) << " Failed to fetchnodes for account " << index;
    }

    // get session
    for (unsigned index = 0 ; index < howMany; index++ )
    {
        out() << logTime() << "dump session of exported folder " << index;
        sessions[index] = exportedFolderApis[index]->dumpSession();
    }

    // local logout
    for (unsigned index = 0 ; index < howMany; index++ )
    {
        out() << logTime() << "local logout of exported folder " << index;
        trackers[index] = asyncRequestLocalLogout(exportedFolderApis[index].get());

    }
    // wait for logout to complete:
    for (unsigned index = 0; index < howMany; ++index)
    {
        ASSERT_EQ(API_OK, trackers[index]->waitForResult()) << " Failed to local logout for folder " << index;
    }

    // resume session
    for (unsigned index = 0 ; index < howMany; index++ )
    {
        out() << logTime() << "fast login to exported folder " << index;
        trackers[index] = asyncRequestFastLogin(exportedFolderApis[index].get(), sessions[index].c_str());
    }
    // wait for fast login to complete:
    for (unsigned index = 0; index < howMany; ++index)
    {
        ASSERT_EQ(API_OK, trackers[index]->waitForResult()) << " Failed to fast login for folder " << index;
    }

    // perform parallel fetchnodes for each
    for (unsigned index = 0; index < howMany; ++index)
    {
        out() << logTime() << "Fetching nodes for account " << index;
        trackers[index] = asyncRequestFetchnodes(exportedFolderApis[index].get());
    }

    // wait for fetchnodes to complete:
    for (unsigned index = 0; index < howMany; ++index)
    {
        ASSERT_EQ(API_OK, trackers[index]->waitForResult()) << " Failed to fetchnodes for account " << index;
    }

    // get root node to confirm all went well
    for (unsigned index = 0; index < howMany; ++index)
    {
        std::unique_ptr<MegaNode> root{exportedFolderApis[index]->getRootNode()};
        ASSERT_TRUE(root != nullptr);
    }

    // In case the last test exited without cleaning up (eg, debugging etc)
    Cleanup();
}

/**
 * @brief TEST_F SdkTargetOverwriteTest
 *
 * Testing to upload a file into an inshare with read only privileges.
 * API must put node into rubbish bin, instead of fail putnodes with API_EACCESS
 */
TEST_F(SdkTest, SdkTargetOverwriteTest)
{
    LOG_info << "___TEST SdkTargetOverwriteTest___";
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(2));

    //--- Add secondary account as contact ---
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

    //--- Create a new folder in cloud drive ---
    std::unique_ptr<MegaNode> rootnode{megaApi[0]->getRootNode()};
    char foldername1[64] = "Shared-folder";
    MegaHandle hfolder1 = createFolder(0, foldername1, rootnode.get());
    ASSERT_NE(hfolder1, UNDEF);
    MegaNode *n1 = megaApi[0]->getNodeByHandle(hfolder1);
    ASSERT_NE(n1, nullptr);

    // --- Create a new outgoing share ---
    mApi[0].nodeUpdated = mApi[1].nodeUpdated = false; // reset flags expected to be true in asserts below
    mApi[0].mOnNodesUpdateCompletion = createOnNodesUpdateLambda(hfolder1, MegaNode::CHANGE_TYPE_OUTSHARE);
    mApi[1].mOnNodesUpdateCompletion = createOnNodesUpdateLambda(hfolder1, MegaNode::CHANGE_TYPE_INSHARE);

    ASSERT_NO_FATAL_FAILURE(shareFolder(n1, mApi[1].email.c_str(), MegaShare::ACCESS_READWRITE));
    ASSERT_TRUE( waitForResponse(&mApi[0].nodeUpdated) )   // at the target side (main account)
            << "Node update not received after " << maxTimeout << " seconds";
    ASSERT_TRUE( waitForResponse(&mApi[1].nodeUpdated) )   // at the target side (auxiliar account)
            << "Node update not received after " << maxTimeout << " seconds";

    MegaShareList *sl = megaApi[1]->getInSharesList(::MegaApi::ORDER_NONE);
    ASSERT_EQ(1, sl->size()) << "Incoming share not received in auxiliar account";
    MegaShare *share = sl->get(0);

    ASSERT_TRUE(share->getNodeHandle() == n1->getHandle())
            << "Wrong inshare handle: " << Base64Str<MegaClient::NODEHANDLE>(share->getNodeHandle())
            << ", expected: " << Base64Str<MegaClient::NODEHANDLE>( n1->getHandle());

    ASSERT_TRUE(share->getAccess() >=::MegaShare::ACCESS_READWRITE)
             << "Insufficient permissions: " << MegaShare::ACCESS_READWRITE  << " over created share";

    // important to reset
    resetOnNodeUpdateCompletionCBs();

    // --- Create local file and start upload from secondary account into inew InShare ---
    onTransferUpdate_progress = 0;
    onTransferUpdate_filesize = 0;
    mApi[1].transferFlags[MegaTransfer::TYPE_UPLOAD] = false;
    std::string fileName = std::to_string(time(nullptr));
    ASSERT_TRUE(createLocalFile(fs::current_path(), fileName.c_str(), 1024));
    fs::path fp = fs::current_path() / fileName;

    TransferTracker tt(megaApi[1].get());
    megaApi[1]->startUpload(fp.u8string().c_str(),
                            n1,
                            nullptr /*fileName*/,
                            ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
                            nullptr /*appData*/,
                            false   /*isSourceTemporary*/,
                            false   /*startFirst*/,
                            nullptr /*cancelToken*/,
                            &tt);   /*MegaTransferListener*/

    // --- Pause transfer, revoke out-share permissions for secondary account and resume transfer ---
    megaApi[1]->pauseTransfers(true);

    mApi[0].nodeUpdated = mApi[1].nodeUpdated = false; // reset flags expected to be true in asserts below
    mApi[0].mOnNodesUpdateCompletion = createOnNodesUpdateLambda(hfolder1, MegaNode::CHANGE_TYPE_OUTSHARE);
    mApi[1].mOnNodesUpdateCompletion = createOnNodesUpdateLambda(hfolder1, MegaNode::CHANGE_TYPE_REMOVED);

    ASSERT_NO_FATAL_FAILURE(shareFolder(n1, mApi[1].email.c_str(), MegaShare::ACCESS_UNKNOWN));
    ASSERT_TRUE( waitForResponse(&mApi[0].nodeUpdated) )   // at the target side (main account)
            << "Node update not received after " << maxTimeout << " seconds";
    ASSERT_TRUE( waitForResponse(&mApi[1].nodeUpdated) )   // at the target side (auxiliar account)
            << "Node update not received after " << maxTimeout << " seconds";
    megaApi[1]->pauseTransfers(false);
    // --- Wait for transfer completion

    // in fact we get EACCESS - maybe this API feature is not migrated to live yet?
    ASSERT_EQ(API_OK, ErrorCodes(tt.waitForResult(600))) << "Upload transfer failed";

    // --- Check that node has been created in rubbish bin ---
    std::unique_ptr <MegaNode> n (mApi[1].megaApi->getNodeByHandle(tt.resultNodeHandle));
    ASSERT_TRUE(n) << "Error retrieving new created node";

    std::unique_ptr <MegaNode> rubbishNode (mApi[1].megaApi->getRubbishNode());
    ASSERT_TRUE(rubbishNode) << "Error retrieving rubbish bin node";

    ASSERT_TRUE(n->getParentHandle() == rubbishNode->getHandle())
            << "Error: new node parent handle: " << Base64Str<MegaClient::NODEHANDLE>(n->getParentHandle())
            << " doesn't match with rubbish bin node handle: " << Base64Str<MegaClient::NODEHANDLE>(rubbishNode->getHandle());

    // --- Clean rubbish bin for secondary account ---
    auto err = synchronousCleanRubbishBin(1);
    ASSERT_TRUE(err == API_OK || err == API_ENOENT) << "Clean rubbish bin failed (error: " << err << ")";
}

/**
 * @brief TEST_F SdkTestAudioFileThumbnail
 *
 * Tests extracting thumbnail for uploaded audio file.
 *
 * The file to be uploaded must exist or the test will fail.
 * If environment variable MEGA_DIR_PATH_TO_INPUT_FILES is defined, the file is expected to be in that folder. Otherwise,
 * a relative path will be checked. Currently, the relative path is dependent on the building tool
 */
#if !USE_FREEIMAGE || !USE_MEDIAINFO
TEST_F(SdkTest, DISABLED_SdkTestAudioFileThumbnail)
#else
TEST_F(SdkTest, SdkTestAudioFileThumbnail)
#endif
{
    LOG_info << "___TEST Audio File Thumbnail___";

    const char* bufPathToMp3 = getenv("MEGA_DIR_PATH_TO_INPUT_FILES"); // needs platform-specific path separators
    static const std::string AUDIO_FILENAME = "test_cover_png.mp3";

    // Attempt to get the test audio file from these locations:
    // 1. dedicated env var;
    // 2. subtree location, like the one in the repo;
    // 3. current working directory
    LocalPath mp3LP;

    if (bufPathToMp3)
    {
        mp3LP = LocalPath::fromAbsolutePath(bufPathToMp3);
        mp3LP.appendWithSeparator(LocalPath::fromRelativePath(AUDIO_FILENAME), false);
    }
    else
    {
        mp3LP.append(LocalPath::fromRelativePath("."));
        mp3LP.appendWithSeparator(LocalPath::fromRelativePath("tests"), false);
        mp3LP.appendWithSeparator(LocalPath::fromRelativePath("integration"), false);
        mp3LP.appendWithSeparator(LocalPath::fromRelativePath(AUDIO_FILENAME), false);

        if (!fileexists(mp3LP.toPath(false)))
            mp3LP = LocalPath::fromRelativePath(AUDIO_FILENAME);
    }

    const std::string& mp3 = mp3LP.toPath(false);

    ASSERT_TRUE(fileexists(mp3)) << mp3 << " file does not exist";

    ASSERT_NO_FATAL_FAILURE(getAccountsForTest());

    std::unique_ptr<MegaNode> rootnode{ megaApi[0]->getRootNode() };
    ASSERT_EQ(MegaError::API_OK, doStartUpload(0, nullptr, mp3.c_str(),
                                                       rootnode.get(),
                                                       nullptr /*fileName*/,
                                                       ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
                                                       nullptr /*appData*/,
                                                       false   /*isSourceTemporary*/,
                                                       false   /*startFirst*/,
                                                       nullptr /*cancelToken*/)) << "Cannot upload test file " << mp3;
    std::unique_ptr<MegaNode> node(megaApi[0]->getNodeByPath(AUDIO_FILENAME.c_str(), rootnode.get()));
    ASSERT_TRUE(node->hasPreview() && node->hasThumbnail());

}

#endif

/**
 * @brief TEST_F SdkTestSetsAndElements
 *
 * Tests creating, modifying and removing Sets and Elements.
 */
TEST_F(SdkTest, SdkTestSetsAndElements)
{
    LOG_info << "___TEST Sets and Elements___";
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    //  1. Create Set
    //  2. Update Set name
    //  3. Upload test file
    //  4. Add Element
    //  5. Fetch Set
    //  6. Update Element order
    //  7. Update Element name
    //  8. Remove Element
    //  9. Add another element
    // 10. Logout / login
    // 11. Remove all Sets

    // Use another connection with the same credentials
    megaApi.emplace_back(newMegaApi(APP_KEY.c_str(), megaApiCacheFolder(0).c_str(), USER_AGENT.c_str(), unsigned(THREADS_PER_MEGACLIENT)));
    auto& differentApi = *megaApi.back();
    differentApi.addListener(this);
    PerApi pa; // make a copy
    pa.email = mApi.back().email;
    pa.pwd = mApi.back().pwd;
    mApi.push_back(move(pa));
    auto& differentApiDtls = mApi.back();
    differentApiDtls.megaApi = &differentApi;
    int differentApiIdx = int(megaApi.size() - 1);

    auto loginTracker = asyncRequestLogin(differentApiIdx, differentApiDtls.email.c_str(), differentApiDtls.pwd.c_str());
    ASSERT_EQ(API_OK, loginTracker->waitForResult()) << " Failed to establish a login/session for account " << differentApiIdx;
    loginTracker = asyncRequestFetchnodes(differentApiIdx);
    ASSERT_EQ(API_OK, loginTracker->waitForResult()) << " Failed to fetch nodes for account " << differentApiIdx;

    // 1. Create Set
    string name = u8"Set name ideograms: 讓我們打破這個"; // "讓我們打破這個"
    differentApiDtls.setUpdated = false;
    MegaSet* newSet = nullptr;
    int err = doCreateSet(0, &newSet, name.c_str());
    ASSERT_EQ(err, API_OK);

    unique_ptr<MegaSet> s1p(newSet);
    ASSERT_NE(s1p, nullptr);
    ASSERT_NE(s1p->id(), INVALID_HANDLE);
    ASSERT_EQ(s1p->name(), name);
    ASSERT_NE(s1p->ts(), 0);
    ASSERT_NE(s1p->user(), INVALID_HANDLE);
    MegaHandle sh = s1p->id();

    // test action packets
    ASSERT_TRUE(waitForResponse(&differentApiDtls.setUpdated)) << "Set create AP not received after " << maxTimeout << " seconds";
    unique_ptr<MegaSet> s2p(differentApi.getSet(sh));
    ASSERT_NE(s2p, nullptr);
    ASSERT_EQ(s2p->id(), s1p->id());
    ASSERT_EQ(s2p->name(), name);
    ASSERT_EQ(s2p->ts(), s1p->ts());
    ASSERT_EQ(s2p->user(), s1p->user());

    // Clear Set name
    differentApiDtls.setUpdated = false;
    err = doUpdateSetName(0, nullptr, sh, "");
    ASSERT_EQ(err, API_OK);
    unique_ptr<MegaSet> s1clearname(megaApi[0]->getSet(sh));
    ASSERT_NE(s1clearname, nullptr);
    ASSERT_STREQ(s1clearname->name(), "");
    // test action packets
    ASSERT_TRUE(waitForResponse(&differentApiDtls.setUpdated)) << "Set update AP not received after " << maxTimeout << " seconds";
    s2p.reset(differentApi.getSet(sh));
    ASSERT_NE(s2p, nullptr);
    ASSERT_STREQ(s2p->name(), "");

    // 2. Update Set name
    MegaHandle shu = INVALID_HANDLE;
    name += u8" updated";
    differentApiDtls.setUpdated = false;
    err = doUpdateSetName(0, &shu, sh, name.c_str());
    ASSERT_EQ(err, API_OK);
    ASSERT_EQ(shu, sh);

    unique_ptr<MegaSet> s1up(megaApi[0]->getSet(shu));
    ASSERT_NE(s1up, nullptr);
    ASSERT_EQ(s1up->id(), sh);
    ASSERT_EQ(s1up->name(), name);
    ASSERT_EQ(s1up->user(), s1p->user());
    //ASSERT_NE(s1up->ts(), s1p->ts()); // apparently this is not always updated

    // test action packets
    ASSERT_TRUE(waitForResponse(&differentApiDtls.setUpdated)) << "Set update AP not received after " << maxTimeout << " seconds";
    s2p.reset(differentApi.getSet(sh));
    ASSERT_NE(s2p, nullptr);
    ASSERT_EQ(s2p->name(), name);
    ASSERT_EQ(s2p->ts(), s1up->ts());

    // 3. Upload test file
    std::unique_ptr<MegaNode> rootnode{ megaApi[0]->getRootNode() };
    ASSERT_TRUE(createFile(UPFILE, false)) << "Couldn't create " << UPFILE;
    MegaHandle uploadedNode = INVALID_HANDLE;
    ASSERT_EQ(MegaError::API_OK, doStartUpload(0, &uploadedNode, UPFILE.c_str(),
        rootnode.get(),
        nullptr /*fileName*/,
        ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
        nullptr /*appData*/,
        false   /*isSourceTemporary*/,
        false   /*startFirst*/,
        nullptr /*cancelToken*/)) << "Cannot upload a test file";

    // 4. Add Element
    string elattrs = u8"Element name emoji: 📞🎉❤️"; // "📞🎉❤️"
    differentApiDtls.setElementUpdated = false;
    MegaSetElementList* newEll = nullptr;
    err = doCreateSetElement(0, &newEll, sh, uploadedNode, elattrs.c_str());
    ASSERT_EQ(err, API_OK);

    unique_ptr<MegaSetElementList> els(newEll);
    ASSERT_NE(els, nullptr);
    ASSERT_EQ(els->size(), 1u);
    ASSERT_EQ(els->get(0)->node(), uploadedNode);
    ASSERT_EQ(els->get(0)->name(), elattrs);
    ASSERT_NE(els->get(0)->ts(), 0);
    ASSERT_EQ(els->get(0)->order(), 1000);
    MegaHandle eh = els->get(0)->id();
    unique_ptr<MegaSetElement> elp(megaApi[0]->getSetElement(sh, eh));
    ASSERT_NE(elp, nullptr);
    ASSERT_EQ(elp->id(), eh);
    ASSERT_EQ(elp->node(), uploadedNode);
    ASSERT_EQ(elp->name(), elattrs);
    ASSERT_NE(elp->ts(), 0);
    ASSERT_EQ(elp->order(), 1000); // first default value, according to specs

    // test action packets
    ASSERT_TRUE(waitForResponse(&differentApiDtls.setElementUpdated)) << "Element add AP not received after " << maxTimeout << " seconds";
    s2p.reset(differentApi.getSet(sh));
    ASSERT_NE(s2p, nullptr);
    unique_ptr<MegaSetElementList> els2(differentApi.getSetElements(sh));
    ASSERT_NE(els2, nullptr);
    ASSERT_EQ(els2->size(), els->size());
    unique_ptr<MegaSetElement> elp2(differentApi.getSetElement(sh, eh));
    ASSERT_NE(elp2, nullptr);
    ASSERT_EQ(elp2->id(), elp->id());
    ASSERT_EQ(elp2->node(), elp->node());
    ASSERT_EQ(elp2->name(), elattrs);
    ASSERT_EQ(elp2->ts(), elp->ts());
    ASSERT_EQ(elp2->order(), elp->order());

    // Clear Element name
    differentApiDtls.setElementUpdated = false;
    err = doUpdateSetElementName(0, nullptr, sh, eh, "");
    ASSERT_EQ(err, API_OK);
    unique_ptr<MegaSetElement> elclearname(megaApi[0]->getSetElement(sh, eh));
    ASSERT_NE(elclearname, nullptr);
    ASSERT_STREQ(elclearname->name(), "");
    // test action packets
    ASSERT_TRUE(waitForResponse(&differentApiDtls.setElementUpdated)) << "Element update AP not received after " << maxTimeout << " seconds";
    elp2.reset(differentApi.getSetElement(sh, eh));
    ASSERT_NE(elp2, nullptr);
    ASSERT_STREQ(elp2->name(), "");

    // Add cover to Set
    differentApiDtls.setUpdated = false;
    err = doPutSetCover(0, nullptr, sh, eh);
    ASSERT_EQ(err, API_OK);
    s1up.reset(megaApi[0]->getSet(sh));
    ASSERT_EQ(s1up->name(), name);
    ASSERT_EQ(s1up->cover(), eh);
    ASSERT_EQ(megaApi[0]->getSetCover(sh), eh);
    // test action packets
    ASSERT_TRUE(waitForResponse(&differentApiDtls.setUpdated)) << "Set cover update AP not received after " << maxTimeout << " seconds";
    s2p.reset(differentApi.getSet(sh));
    ASSERT_NE(s2p, nullptr);
    ASSERT_EQ(s2p->name(), name);
    ASSERT_EQ(s2p->cover(), eh);

    // Remove cover from Set
    differentApiDtls.setUpdated = false;
    err = doPutSetCover(0, nullptr, sh, INVALID_HANDLE);
    ASSERT_EQ(err, API_OK);
    s1up.reset(megaApi[0]->getSet(sh));
    ASSERT_EQ(s1up->name(), name);
    ASSERT_EQ(s1up->cover(), INVALID_HANDLE);
    ASSERT_EQ(megaApi[0]->getSetCover(sh), INVALID_HANDLE);
    // test action packets
    ASSERT_TRUE(waitForResponse(&differentApiDtls.setUpdated)) << "Set cover removal AP not received after " << maxTimeout << " seconds";
    s2p.reset(differentApi.getSet(sh));
    ASSERT_NE(s2p, nullptr);
    ASSERT_EQ(s2p->name(), name);
    ASSERT_EQ(s2p->cover(), INVALID_HANDLE);

    // 5. Fetch Set
    MegaSet* fetchedSet = nullptr;
    MegaSetElementList* fetchedEls = nullptr;
    err = doFetchSet(0, &fetchedSet, &fetchedEls, sh);
    ASSERT_EQ(err, API_OK);
    ASSERT_NE(fetchedSet, nullptr);
    ASSERT_NE(fetchedEls, nullptr);
    unique_ptr<MegaSet> sf(fetchedSet);
    unique_ptr<MegaSetElementList> elsf(fetchedEls);

    ASSERT_EQ(sf->id(), sh);
    ASSERT_EQ(sf->name(), name);
    ASSERT_EQ(sf->ts(), s1up->ts());
    ASSERT_EQ(sf->user(), s1up->user());

    ASSERT_EQ(elsf->size(), 1u);
    const MegaSetElement* elfp = elsf->get(0);
    ASSERT_NE(elfp, nullptr);
    ASSERT_EQ(elfp->id(), eh);
    ASSERT_EQ(elfp->node(), uploadedNode);
    ASSERT_STREQ(elfp->name(), "");
    ASSERT_EQ(elfp->ts(), elp2->ts());
    ASSERT_EQ(elfp->order(), elp2->order());

    // 6. Update Element order
    MegaHandle el1 = INVALID_HANDLE;
    int64_t order = 222;
    differentApiDtls.setElementUpdated = false;
    err = doUpdateSetElementOrder(0, &el1, sh, eh, order);
    ASSERT_EQ(err, API_OK);
    ASSERT_EQ(el1, eh);

    unique_ptr<MegaSetElement> elu1p(megaApi[0]->getSetElement(sh, eh));
    ASSERT_NE(elu1p, nullptr);
    ASSERT_EQ(elu1p->id(), eh);
    ASSERT_EQ(elu1p->node(), uploadedNode);
    ASSERT_STREQ(elu1p->name(), "");
    ASSERT_EQ(elu1p->order(), order);
    ASSERT_NE(elu1p->ts(), 0);

    // test action packets
    ASSERT_TRUE(waitForResponse(&differentApiDtls.setElementUpdated)) << "Element order change AP not received after " << maxTimeout << " seconds";
    elp2.reset(differentApi.getSetElement(sh, eh));
    ASSERT_NE(elp2, nullptr);
    ASSERT_STREQ(elp2->name(), "");
    ASSERT_EQ(elp2->order(), elu1p->order());

    // 7. Update Element name
    MegaHandle el2 = INVALID_HANDLE;
    elattrs += u8" updated";
    differentApiDtls.setElementUpdated = false;
    err = doUpdateSetElementName(0, &el2, sh, eh, elattrs.c_str());
    ASSERT_EQ(err, API_OK);
    ASSERT_EQ(el2, eh);
    elu1p.reset(megaApi[0]->getSetElement(sh, eh));
    ASSERT_EQ(elu1p->id(), eh);
    ASSERT_EQ(elu1p->name(), elattrs);

    // test action packets
    ASSERT_TRUE(waitForResponse(&differentApiDtls.setElementUpdated)) << "Element name change AP not received after " << maxTimeout << " seconds";
    elp2.reset(differentApi.getSetElement(sh, eh));
    ASSERT_NE(elp2, nullptr);
    ASSERT_EQ(elp2->name(), elattrs);

    // 8. Remove Element
    differentApiDtls.setElementUpdated = false;
    err = doRemoveSetElement(0, sh, eh);
    ASSERT_EQ(err, API_OK);

    elp.reset(megaApi[0]->getSetElement(sh, eh));
    ASSERT_EQ(elp, nullptr);

    // test action packets
    ASSERT_TRUE(waitForResponse(&differentApiDtls.setElementUpdated)) << "Element remove AP not received after " << maxTimeout << " seconds";
    s2p.reset(differentApi.getSet(sh));
    ASSERT_NE(s2p, nullptr);
    els2.reset(differentApi.getSetElements(sh));
    ASSERT_EQ(els2->size(), 0u);
    elp2.reset(differentApi.getSetElement(sh, eh));
    ASSERT_EQ(elp2, nullptr);

    // 9. Add another element
    differentApiDtls.setElementUpdated = false;
    elattrs += u8" again";
    MegaSetElementList* newEll2 = nullptr;
    err = doCreateSetElement(0, &newEll2, sh, uploadedNode, elattrs.c_str());
    ASSERT_EQ(err, API_OK);
    ASSERT_NE(newEll2, nullptr);
    ASSERT_EQ(newEll2->size(), 1u);
    ASSERT_EQ(newEll2->get(0)->name(), elattrs);
    eh = newEll2->get(0)->id();
    ASSERT_NE(eh, INVALID_HANDLE);
    delete newEll2;
    unique_ptr<MegaSetElement> elp_b4lo(megaApi[0]->getSetElement(sh, eh));
    ASSERT_NE(elp_b4lo, nullptr);
    ASSERT_EQ(elp_b4lo->id(), eh);
    ASSERT_EQ(elp_b4lo->name(), elattrs);
    // test action packets
    ASSERT_TRUE(waitForResponse(&differentApiDtls.setElementUpdated)) << "Element add AP not received after " << maxTimeout << " seconds";

    // 10. Logout / login
    unique_ptr<char[]> session(dumpSession());
    ASSERT_NO_FATAL_FAILURE(locallogout());
    s1p.reset(megaApi[0]->getSet(sh));
    ASSERT_EQ(s1p, nullptr);
    ASSERT_NO_FATAL_FAILURE(resumeSession(session.get()));
    ASSERT_NO_FATAL_FAILURE(fetchnodes(0)); // load cached Sets

    s1p.reset(megaApi[0]->getSet(sh));
    ASSERT_NE(s1p, nullptr);
    ASSERT_EQ(s1p->id(), sh);
    ASSERT_EQ(s1p->user(), s1up->user());
    ASSERT_EQ(s1p->ts(), s1up->ts());
    ASSERT_EQ(s1p->name(), name);

    unique_ptr<MegaSetElement> ellp(megaApi[0]->getSetElement(sh, eh));
    ASSERT_NE(ellp, nullptr);
    ASSERT_EQ(ellp->id(), elp_b4lo->id());
    ASSERT_EQ(ellp->node(), elp_b4lo->node());
    ASSERT_EQ(ellp->ts(), elp_b4lo->ts());
    ASSERT_EQ(ellp->name(), elattrs);

    // 11. Remove all Sets
    unique_ptr<MegaSetList> sets(megaApi[0]->getSets());
    unique_ptr<MegaSetList> sets2(differentApi.getSets());
    ASSERT_EQ(sets->size(), sets2->size());
    for (unsigned i = 0; i < sets->size(); ++i)
    {
        handle setId = sets->get(i)->id();
        differentApiDtls.setUpdated = false;
        err = doRemoveSet(0, setId);
        ASSERT_EQ(err, API_OK);

        s1p.reset(megaApi[0]->getSet(setId));
        ASSERT_EQ(s1p, nullptr);

        // test action packets
        ASSERT_TRUE(waitForResponse(&differentApiDtls.setUpdated)) << "Set remove AP not received after " << maxTimeout << " seconds";
        s2p.reset(differentApi.getSet(setId));
        ASSERT_EQ(s2p, nullptr);
    }

    sets.reset(megaApi[0]->getSets());
    ASSERT_EQ(sets->size(), 0u);

    sets2.reset(differentApi.getSets());
    ASSERT_EQ(sets2->size(), 0u);
}


/**
 * @brief TEST_F SdkUserAlerts
 *
 * Generate User Alerts and check that they are received as expected.
 *
 * Generated so far:
 *      IncomingPendingContact  --  request created
 *      ContactChange  --  contact request accepted
 *      NewShare
 *      RemovedSharedNode
 *      NewSharedNodes
 *      UpdatedSharedNode
 *      UpdatedSharedNode (combined with previous one)
 *      DeletedShare
 *      ContactChange  --  contact deleted
 *
 * Not generated:
 *      UpdatedPendingContactIncoming   skipped (requires a 2 week wait)
 *      UpdatedPendingContactOutgoing   skipped (requires a 2 week wait)
 *      Payment                         skipped (out of user control)
 *      PaymentReminder                 skipped (out of user control)
 *      Takedown                        skipped (out of user control)
 */
TEST_F(SdkTest, SdkUserAlerts)
{
    LOG_info << "___TEST SdkUserAlerts___";
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(2));

    int A1idx = 0;
    int B1idx = 1;

    // Alerts generated by the actions in this test should be compared with the ones received through sc50 request.
    // However, for now we kept code dealing with sc50 disabled because sc50 request times out very often in Jenkins.
#define MEGA_TEST_SC50_ALERTS 0

#if MEGA_TEST_SC50_ALERTS
    // B2 (uses the same credentials as B1)
    // Create this here, in order to keep valid references to the others throughout the entire test
    megaApi.emplace_back(newMegaApi(APP_KEY.c_str(), megaApiCacheFolder(B1idx).c_str(), string(USER_AGENT+"SC50").c_str(), unsigned(THREADS_PER_MEGACLIENT)));
    auto& B2 = *megaApi.back();
    B2.addListener(this);
    PerApi pa; // make a copy
    pa.email = mApi.back().email;
    pa.pwd = mApi.back().pwd;
    mApi.push_back(move(pa));
    auto& B2dtls = mApi.back();
    B2dtls.megaApi = &B2;
#endif

    // A1
    auto& A1dtls = mApi[A1idx];
    auto& A1 = *megaApi[A1idx];
    ASSERT_EQ(API_OK, doAckUserAlerts(A1idx)) << " Failed to acknowledge user alerts for A1";

    // B1
    auto& B1dtls = mApi[B1idx];
    auto& B1 = *megaApi[B1idx];
    ASSERT_EQ(API_OK, doAckUserAlerts(B1idx)) << " Failed to acknowledge user alerts for B1";

#if MEGA_TEST_SC50_ALERTS
    // B2
    B2dtls.userAlertsUpdated = false;
    B2dtls.userAlertList.reset();

    int B2idx = int(megaApi.size() - 1);
    auto loginTracker = asyncRequestLogin(B2idx, B2dtls.email.c_str(), B2dtls.pwd.c_str());
    ASSERT_EQ(API_OK, loginTracker->waitForResult()) << " Failed to establish a login/session for account " << B2idx << " (B2)";
    loginTracker = asyncRequestFetchnodes(B2idx);
    ASSERT_EQ(API_OK, loginTracker->waitForResult()) << " Failed to fetch nodes for account " << B2idx << " (B2)";
    // test sc50 after login
    unsigned sc50Timeout = 120; // seconds
    ASSERT_TRUE(waitForResponse(&B2dtls.userAlertsUpdated, sc50Timeout))
        << "sc50 alerts after login not received by B2 after " << sc50Timeout << " seconds";
    ASSERT_EQ(B2dtls.userAlertList, nullptr) << "sc50";
    unique_ptr<MegaUserAlertList> sc50List(B2.getUserAlerts());
    ASSERT_TRUE(sc50List);
    ASSERT_NE(sc50List->size(), 0);
    // save session
    unique_ptr<char[]> B2session(B2.dumpSession());
    auto logoutErr = doRequestLocalLogout(B2idx);
    ASSERT_EQ(API_OK, logoutErr) << "Local logout failed (error: " << logoutErr << ") for account " << B2idx << " (B2)";
#endif

    vector<unique_ptr<MegaUserAlert>> bkpList; // used for comparing existing alerts with ones received through sc50

    // IncomingPendingContact  --  request created
    //--------------------------------------------

    // reset User Alerts for B1
    B1dtls.userAlertsUpdated = false;
    B1dtls.userAlertList.reset();

    // --- Send a contact request ---
    A1dtls.contactRequestUpdated = B1dtls.contactRequestUpdated = false;
    ASSERT_NO_FATAL_FAILURE(inviteContact(A1idx, B1dtls.email, "test: A1 invited B1", MegaContactRequest::INVITE_ACTION_ADD));
    ASSERT_TRUE(waitForResponse(&A1dtls.contactRequestUpdated))
        << "Contact request creation not received by A1 after " << maxTimeout << " seconds";
    ASSERT_TRUE(waitForResponse(&B1dtls.contactRequestUpdated))
        << "Contact request creation not received by B1 after " << maxTimeout << " seconds";
    B1dtls.cr.reset();
    ASSERT_NO_FATAL_FAILURE(getContactRequest(B1idx, false));
    ASSERT_NE(B1dtls.cr, nullptr);

    // IncomingPendingContact  --  request created
    ASSERT_TRUE(waitForResponse(&B1dtls.userAlertsUpdated))
        << "Alert about contact request creation not received by B1 after " << maxTimeout << " seconds";
    ASSERT_NE(B1dtls.userAlertList, nullptr) << "IncomingPendingContact  --  request created";
    ASSERT_EQ(B1dtls.userAlertList->size(), 1) << "IncomingPendingContact  --  request created";
    const auto* a = B1dtls.userAlertList->get(0);
    ASSERT_STRCASEEQ(a->getEmail(), A1dtls.email.c_str()) << "IncomingPendingContact  --  request created";
    ASSERT_STRCASEEQ(a->getTitle(), "Sent you a contact request") << "IncomingPendingContact  --  request created";
    ASSERT_GT(a->getId(), 0u) << "IncomingPendingContact  --  request created";
    ASSERT_EQ(a->getType(), MegaUserAlert::TYPE_INCOMINGPENDINGCONTACT_REQUEST) << "IncomingPendingContact  --  request created";
    ASSERT_STREQ(a->getTypeString(), "NEW_CONTACT_REQUEST") << "IncomingPendingContact  --  request created";
    ASSERT_STRCASEEQ(a->getHeading(), A1dtls.email.c_str()) << "IncomingPendingContact  --  request created";
    ASSERT_NE(a->getTimestamp(0), 0) << "IncomingPendingContact  --  request created";
    ASSERT_FALSE(a->isOwnChange()) << "IncomingPendingContact  --  request created";
    ASSERT_EQ(a->getPcrHandle(), B1dtls.cr->getHandle()) << "IncomingPendingContact  --  request created";
    bkpList.emplace_back(a->copy());


    // ContactChange  --  contact request accepted
    //--------------------------------------------

    // reset User Alerts for A1
    A1dtls.userAlertsUpdated = false;
    A1dtls.userAlertList.reset();

    // --- Accept contact request ---
    A1dtls.contactRequestUpdated = B1dtls.contactRequestUpdated = false;
    ASSERT_NO_FATAL_FAILURE(replyContact(B1dtls.cr.get(), MegaContactRequest::REPLY_ACTION_ACCEPT));
    ASSERT_TRUE(waitForResponse(&A1dtls.contactRequestUpdated))
        << "Contact request accept not received by A1 after " << maxTimeout << " seconds";
    ASSERT_TRUE(waitForResponse(&B1dtls.contactRequestUpdated))
        << "Contact request accept not received by B1 after " << maxTimeout << " seconds";
    B1dtls.cr.reset();

    // ContactChange  --  contact request accepted
    ASSERT_TRUE(waitForResponse(&A1dtls.userAlertsUpdated))
        << "Alert about contact request accepted not received by A1 after " << maxTimeout << " seconds";
    ASSERT_NE(A1dtls.userAlertList, nullptr) << "ContactChange  --  contact request accepted";
    ASSERT_EQ(A1dtls.userAlertList->size(), 1) << "ContactChange  --  contact request accepted";
    a = A1dtls.userAlertList->get(0);
    ASSERT_STRCASEEQ(a->getEmail(), B1dtls.email.c_str()) << "ContactChange  --  contact request accepted";
    ASSERT_STRCASEEQ(a->getTitle(), "Contact relationship established") << "ContactChange  --  contact request accepted";
    ASSERT_GT(a->getId(), 0u) << "ContactChange  --  contact request accepted";
    ASSERT_EQ(a->getType(), MegaUserAlert::TYPE_CONTACTCHANGE_CONTACTESTABLISHED) << "ContactChange  --  contact request accepted";
    ASSERT_STREQ(a->getTypeString(), "CONTACT_ESTABLISHED") << "ContactChange  --  contact request accepted";
    ASSERT_STRCASEEQ(a->getHeading(), B1dtls.email.c_str()) << "ContactChange  --  contact request accepted";
    ASSERT_NE(a->getTimestamp(0), 0) << "ContactChange  --  contact request accepted";
    ASSERT_FALSE(a->isOwnChange()) << "ContactChange  --  contact request accepted";
    ASSERT_EQ(a->getUserHandle(), B1.getMyUserHandleBinary()) << "ContactChange  --  contact request accepted";
    // received by A1, do not keep it for comparing with B2's sc50


    // create some folders / files to share

        // Create some nodes to share
        //  |--Shared-folder
        //    |--subfolder
        //    |--file.txt       // PUBLICFILE
        //  |--file1.txt        // UPFILE

    std::unique_ptr<MegaNode> rootnode{ A1.getRootNode() };
    char sharedFolder[] = "Shared-folder";
    MegaHandle hSharedFolder = createFolder(A1idx, sharedFolder, rootnode.get());
    ASSERT_NE(hSharedFolder, UNDEF);

    std::unique_ptr<MegaNode> nSharedFolder(A1.getNodeByHandle(hSharedFolder));
    ASSERT_NE(nSharedFolder, nullptr);

    char subfolder[] = "subfolder";
    MegaHandle hSubfolder = createFolder(A1idx, subfolder, nSharedFolder.get());
    ASSERT_NE(hSubfolder, UNDEF);


    // not a large file since don't need to test transfers here
    ASSERT_TRUE(createFile(PUBLICFILE.c_str(), false)) << "Couldn't create " << PUBLICFILE.c_str();
    MegaHandle hPublicfile = UNDEF;
    ASSERT_EQ(MegaError::API_OK, doStartUpload(A1idx, &hPublicfile, PUBLICFILE.c_str(), nSharedFolder.get(),
        nullptr /*fileName*/,
        ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
        nullptr /*appData*/,
        false   /*isSourceTemporary*/,
        false   /*startFirst*/,
        nullptr /*cancelToken*/)) << "Cannot upload a test file";

    std::unique_ptr<MegaNode> nSubfolder(A1.getNodeByHandle(hSubfolder));
    ASSERT_NE(nSubfolder, nullptr);
    std::unique_ptr<MegaNode> rootA1(A1.getRootNode());
    ASSERT_NE(rootA1, nullptr);
    ASSERT_TRUE(createFile(UPFILE.c_str(), false)) << "Couldn't create " << UPFILE.c_str();
    MegaHandle hUpfile = UNDEF;
    ASSERT_EQ(MegaError::API_OK, doStartUpload(A1idx, &hUpfile, UPFILE.c_str(), rootA1.get(),
        nullptr /*fileName*/,
        ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
        nullptr /*appData*/,
        false   /*isSourceTemporary*/,
        false   /*startFirst*/,
        nullptr /*cancelToken*/)) << "Cannot upload a second test file";


    // NewShare
    //--------------------------------------------

    // reset User Alerts for B1
    B1dtls.userAlertsUpdated = false;
    B1dtls.userAlertList.reset();

    // --- Create a new outgoing share ---
    A1dtls.nodeUpdated = B1dtls.nodeUpdated = false; // reset flags expected to be true in asserts below
    A1dtls.mOnNodesUpdateCompletion = [&A1dtls, A1idx](size_t apiIndex, MegaNodeList*) { if (A1idx == int(apiIndex)) A1dtls.nodeUpdated = true; };
    B1dtls.mOnNodesUpdateCompletion = [&B1dtls, B1idx](size_t apiIndex, MegaNodeList*) { if (B1idx == int(apiIndex)) B1dtls.nodeUpdated = true; };

    ASSERT_NO_FATAL_FAILURE(shareFolder(nSharedFolder.get(), B1dtls.email.c_str(), MegaShare::ACCESS_FULL));
    ASSERT_TRUE(waitForResponse(&A1dtls.nodeUpdated))   // at the target side (main account)
        << "Node update not received by A1 after " << maxTimeout << " seconds";
    ASSERT_TRUE(waitForResponse(&B1dtls.nodeUpdated))   // at the target side (auxiliar account)
        << "Node update not received by B1 after " << maxTimeout << " seconds";
    // important to reset
    resetOnNodeUpdateCompletionCBs();

    // NewShare
    ASSERT_TRUE(waitForResponse(&B1dtls.userAlertsUpdated))
        << "Alert about new share not received by B1 after " << maxTimeout << " seconds";
    ASSERT_NE(B1dtls.userAlertList, nullptr) << "NewShare";
    ASSERT_EQ(B1dtls.userAlertList->size(), 1) << "NewShare";
    a = B1dtls.userAlertList->get(0);
    ASSERT_STRCASEEQ(a->getEmail(), A1dtls.email.c_str()) << "NewShare";
    string title = "New shared folder from " + A1dtls.email;
    ASSERT_STRCASEEQ(a->getTitle(), title.c_str()) << "NewShare";
    ASSERT_GT(a->getId(), 0u) << "NewShare";
    ASSERT_EQ(a->getType(), MegaUserAlert::TYPE_NEWSHARE) << "NewShare";
    ASSERT_STREQ(a->getTypeString(), "NEW_SHARE") << "NewShare";
    ASSERT_STRCASEEQ(a->getHeading(), A1dtls.email.c_str()) << "NewShare";
    ASSERT_NE(a->getTimestamp(0), 0) << "NewShare";
    ASSERT_FALSE(a->isOwnChange()) << "NewShare";
    ASSERT_EQ(a->getUserHandle(), A1.getMyUserHandleBinary()) << "NewShare";
    ASSERT_EQ(a->getNodeHandle(), nSharedFolder->getHandle()) << "NewShare";
    string path = A1dtls.email + ':' + sharedFolder;
    ASSERT_STRCASEEQ(a->getPath(), path.c_str()) << "NewShare";
    ASSERT_STREQ(a->getName(), sharedFolder) << "NewShare";
    bkpList.emplace_back(a->copy());


    // RemovedSharedNode
    //--------------------------------------------

    // reset User Alerts for B1
    B1dtls.userAlertsUpdated = false;
    B1dtls.userAlertList.reset();

    // --- Move shared sub-folder (owned) to Root ---
    ASSERT_EQ(API_OK, doMoveNode(A1idx, nullptr, nSubfolder.get(), rootA1.get())) << "Moving subfolder out of (owned) share failed";

    // RemovedSharedNode
    ASSERT_TRUE(waitForResponse(&B1dtls.userAlertsUpdated))
        << "Alert about removed shared node not received by B1 after " << maxTimeout << " seconds";
    ASSERT_NE(B1dtls.userAlertList, nullptr) << "RemovedSharedNode";
    ASSERT_EQ(B1dtls.userAlertList->size(), 1) << "RemovedSharedNode";
    a = B1dtls.userAlertList->get(0);
    ASSERT_STRCASEEQ(a->getEmail(), A1dtls.email.c_str()) << "RemovedSharedNode";
    title = "Removed item from shared folder";
    ASSERT_STRCASEEQ(a->getTitle(), title.c_str()) << "RemovedSharedNode";
    ASSERT_GT(a->getId(), 0u) << "RemovedSharedNode";
    ASSERT_EQ(a->getType(), MegaUserAlert::TYPE_REMOVEDSHAREDNODES) << "RemovedSharedNode";
    ASSERT_STREQ(a->getTypeString(), "NODES_IN_SHARE_REMOVED") << "RemovedSharedNode";
    ASSERT_STRCASEEQ(a->getHeading(), A1dtls.email.c_str()) << "RemovedSharedNode";
    ASSERT_NE(a->getTimestamp(0), 0) << "RemovedSharedNode";
    ASSERT_FALSE(a->isOwnChange()) << "RemovedSharedNode";
    ASSERT_EQ(a->getUserHandle(), A1.getMyUserHandleBinary()) << "RemovedSharedNode";
    ASSERT_EQ(a->getNumber(0), 1) << "RemovedSharedNode";
    //bkpList.emplace_back(a->copy());  // this wasa not received in sc50 response


    // NewSharedNodes
    //--------------------------------------------

    // reset User Alerts for B1
    B1dtls.userAlertsUpdated = false;
    B1dtls.userAlertList.reset();
    A1dtls.userAlertsUpdated = false;
    A1dtls.userAlertList.reset();

    // --- Move sub-folder from Root (owned) back to share ---
    ASSERT_EQ(API_OK, doMoveNode(A1idx, nullptr, nSubfolder.get(), nSharedFolder.get())) << "Moving sub-folder from Root (owned) to share failed";
    // NOTE: This did not create a NewSharedNodes alert (even when it contained files). Notified as a potential bug.

    // --- Move file from Root (owned) to share ---
    std::unique_ptr<MegaNode> nfile2(A1.getNodeByHandle(hUpfile));
    ASSERT_NE(nfile2, nullptr);
    ASSERT_EQ(API_OK, doMoveNode(A1idx, nullptr, nfile2.get(), nSubfolder.get())) << "Moving file from Root (owned) to shared folder failed";

    // NewSharedNodes
    ASSERT_TRUE(waitForResponse(&B1dtls.userAlertsUpdated))
        << "Alert about node added to share not received by B1 after " << maxTimeout << " seconds";
    ASSERT_NE(B1dtls.userAlertList, nullptr) << "NewSharedNodes";
    ASSERT_EQ(B1dtls.userAlertList->size(), 1) << "NewSharedNodes";
    a = B1dtls.userAlertList->get(0);
    ASSERT_STRCASEEQ(a->getEmail(), A1dtls.email.c_str()) << "NewSharedNodes";
    title = A1dtls.email + " added 1 file";
    ASSERT_STRCASEEQ(a->getTitle(), title.c_str()) << "NewSharedNodes";
    ASSERT_GT(a->getId(), 0u) << "NewSharedNodes";
    ASSERT_EQ(a->getType(), MegaUserAlert::TYPE_NEWSHAREDNODES) << "NewSharedNodes";
    ASSERT_STREQ(a->getTypeString(), "NEW_NODES_IN_SHARE") << "NewSharedNodes";
    ASSERT_STRCASEEQ(a->getHeading(), A1dtls.email.c_str()) << "NewSharedNodes";
    ASSERT_NE(a->getTimestamp(0), 0) << "NewSharedNodes";
    ASSERT_FALSE(a->isOwnChange()) << "NewSharedNodes";
    ASSERT_EQ(a->getUserHandle(), A1.getMyUserHandleBinary()) << "NewSharedNodes";
    ASSERT_EQ(a->getNumber(0), 0) << "NewSharedNodes"; // folder count
    ASSERT_EQ(a->getNumber(1), 1) << "NewSharedNodes"; // file count
    ASSERT_EQ(a->getNodeHandle(), hSubfolder) << "NewSharedNodes"; // parent handle
    ASSERT_EQ(a->getHandle(0), hUpfile) << "NewSharedNodes";
    //bkpList.emplace_back(a->copy());  // this was not received in sc50 response


    // UpdatedSharedNode
    //--------------------------------------------

    // reset User Alerts for B1
    B1dtls.userAlertsUpdated = false;
    B1dtls.userAlertList.reset();

    // --- Modify shared file ---
    {
        ofstream f(UPFILE);
        f << "edited";
    }
    // Upload a file over an existing one to update
    ASSERT_EQ(API_OK, doStartUpload(0, nullptr, UPFILE.c_str(), nSubfolder.get(),
        nullptr /*fileName*/,
        ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
        nullptr /*appData*/,
        false   /*isSourceTemporary*/,
        false   /*startFirst*/,
        nullptr /*cancelToken*/));

    // UpdatedSharedNode
    ASSERT_TRUE(waitForResponse(&B1dtls.userAlertsUpdated))
        << "Alert about node updated in share not received by B1 after " << maxTimeout << " seconds";
    ASSERT_NE(B1dtls.userAlertList, nullptr) << "UpdatedSharedNode";
    ASSERT_EQ(B1dtls.userAlertList->size(), 1) << "UpdatedSharedNode";
    a = B1dtls.userAlertList->get(0);
    ASSERT_STRCASEEQ(a->getEmail(), A1dtls.email.c_str()) << "UpdatedSharedNode";
    ASSERT_STRCASEEQ(a->getTitle(), "Updated 1 item in shared folder") << "UpdatedSharedNode";
    ASSERT_GT(a->getId(), 0u) << "UpdatedSharedNode";
    ASSERT_EQ(a->getType(), MegaUserAlert::TYPE_UPDATEDSHAREDNODES) << "UpdatedSharedNode";
    ASSERT_STREQ(a->getTypeString(), "NODES_IN_SHARE_UPDATED") << "UpdatedSharedNode";
    ASSERT_STRCASEEQ(a->getHeading(), A1dtls.email.c_str()) << "UpdatedSharedNode";
    ASSERT_NE(a->getTimestamp(0), 0) << "UpdatedSharedNode";
    ASSERT_FALSE(a->isOwnChange()) << "UpdatedSharedNode";
    ASSERT_EQ(a->getUserHandle(), A1.getMyUserHandleBinary()) << "UpdatedSharedNode";
    ASSERT_EQ(a->getNumber(0), 1) << "UpdatedSharedNode"; // item count
    // this will be combined with the next one, do not keep it for comparing with B2's sc50


    // UpdatedSharedNode  --  combined with the previous one
    //--------------------------------------------

    // reset User Alerts for B1
    B1dtls.userAlertsUpdated = false;
    B1dtls.userAlertList.reset();

    // --- Modify shared file ---
    {
        ofstream f(UPFILE);
        f << " AND edited again";
    }
    // Upload a file over an existing one to update
    ASSERT_EQ(API_OK, doStartUpload(0, nullptr, UPFILE.c_str(), nSubfolder.get(),
        nullptr /*fileName*/,
        ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
        nullptr /*appData*/,
        false   /*isSourceTemporary*/,
        false   /*startFirst*/,
        nullptr /*cancelToken*/));

    // UpdatedSharedNode (combined)
    ASSERT_TRUE(waitForResponse(&B1dtls.userAlertsUpdated))
        << "Alert about node updated, again, in share not received by B1 after " << maxTimeout << " seconds";
    ASSERT_NE(B1dtls.userAlertList, nullptr) << "UpdatedSharedNode (combined)";
    ASSERT_EQ(B1dtls.userAlertList->size(), 1) << "UpdatedSharedNode (combined)";
    a = B1dtls.userAlertList->get(0);
    ASSERT_STRCASEEQ(a->getEmail(), A1dtls.email.c_str()) << "UpdatedSharedNode (combined)";
    ASSERT_STRCASEEQ(a->getTitle(), "Updated 2 items in shared folder") << "UpdatedSharedNode (combined)";
    ASSERT_GT(a->getId(), 0u) << "UpdatedSharedNode (combined)";
    ASSERT_EQ(a->getType(), MegaUserAlert::TYPE_UPDATEDSHAREDNODES) << "UpdatedSharedNode (combined)";
    ASSERT_STREQ(a->getTypeString(), "NODES_IN_SHARE_UPDATED") << "UpdatedSharedNode (combined)";
    ASSERT_STRCASEEQ(a->getHeading(), A1dtls.email.c_str()) << "UpdatedSharedNode (combined)";
    ASSERT_NE(a->getTimestamp(0), 0) << "UpdatedSharedNode (combined)";
    ASSERT_FALSE(a->isOwnChange()) << "UpdatedSharedNode (combined)";
    ASSERT_EQ(a->getUserHandle(), A1.getMyUserHandleBinary()) << "UpdatedSharedNode (combined)";
    ASSERT_EQ(a->getNumber(0), 2) << "UpdatedSharedNode (combined)"; // item count
    //bkpList.emplace_back(a->copy());  // this was not received in sc50 response


    // DeletedShare
    //--------------------------------------------

    // reset User Alerts for B1
    B1dtls.userAlertsUpdated = false;
    B1dtls.userAlertList.reset();

    // --- Revoke access to an outgoing share ---
    A1dtls.nodeUpdated = B1dtls.nodeUpdated = false; // reset flags expected to be true in asserts below
    A1dtls.mOnNodesUpdateCompletion = [&A1dtls, A1idx](size_t apiIndex, MegaNodeList*) { if (A1idx == int(apiIndex)) A1dtls.nodeUpdated = true; };
    B1dtls.mOnNodesUpdateCompletion = [&B1dtls, B1idx](size_t apiIndex, MegaNodeList*) { if (B1idx == int(apiIndex)) B1dtls.nodeUpdated = true; };
    ASSERT_NO_FATAL_FAILURE(shareFolder(nSharedFolder.get(), B1dtls.email.c_str(), MegaShare::ACCESS_UNKNOWN));
    ASSERT_TRUE(waitForResponse(&A1dtls.nodeUpdated))   // at the target side (main account)
        << "Node update not received by A1 after " << maxTimeout << " seconds";
    ASSERT_TRUE(waitForResponse(&B1dtls.nodeUpdated))   // at the target side (auxiliar account)
        << "Node update not received by B1 after " << maxTimeout << " seconds";
    // important to reset
    resetOnNodeUpdateCompletionCBs();

    // DeletedShare
    ASSERT_TRUE(waitForResponse(&B1dtls.userAlertsUpdated))
        << "Alert about deleted share not received by B1 after " << maxTimeout << " seconds";
    ASSERT_NE(B1dtls.userAlertList, nullptr) << "DeletedShare";
    ASSERT_EQ(B1dtls.userAlertList->size(), 1) << "DeletedShare";
    a = B1dtls.userAlertList->get(0);
    ASSERT_STRCASEEQ(a->getEmail(), A1dtls.email.c_str()) << "DeletedShare";
    title = "Access to folders shared by " + A1dtls.email + " was removed";
    ASSERT_STRCASEEQ(a->getTitle(), title.c_str()) << "DeletedShare";
    ASSERT_GT(a->getId(), 0u) << "DeletedShare";
    ASSERT_EQ(a->getType(), MegaUserAlert::TYPE_DELETEDSHARE) << "DeletedShare";
    ASSERT_STREQ(a->getTypeString(), "SHARE_UNSHARED") << "DeletedShare";
    ASSERT_STRCASEEQ(a->getHeading(), A1dtls.email.c_str()) << "DeletedShare";
    ASSERT_NE(a->getTimestamp(0), 0) << "DeletedShare";
    ASSERT_FALSE(a->isOwnChange()) << "DeletedShare";
    ASSERT_EQ(a->getUserHandle(), A1.getMyUserHandleBinary()) << "DeletedShare";
    ASSERT_EQ(a->getNodeHandle(), nSharedFolder->getHandle()) << "DeletedShare";
    path = A1dtls.email + ':' + sharedFolder;
    ASSERT_STRCASEEQ(a->getPath(), path.c_str()) << "DeletedShare";
    ASSERT_STREQ(a->getName(), sharedFolder) << "DeletedShare";
    ASSERT_EQ(a->getNumber(0), 1) << "DeletedShare";
    bkpList.emplace_back(a->copy());


    // ContactChange  --  contact deleted
    //--------------------------------------------

    // reset User Alerts for B1
    B1dtls.userAlertsUpdated = false;
    B1dtls.userAlertList.reset();

    // --- Delete an existing contact ---
    A1dtls.userUpdated = false;
    ASSERT_NO_FATAL_FAILURE(removeContact(B1dtls.email));
    ASSERT_TRUE(waitForResponse(&A1dtls.userUpdated))   // at the target side (main account)
        << "Delete contact update not received by A1 after " << maxTimeout << " seconds";

    // ContactChange  --  contact deleted
    ASSERT_TRUE(waitForResponse(&B1dtls.userAlertsUpdated))
        << "Alert about contact removal not received by B1 after " << maxTimeout << " seconds";
    ASSERT_NE(B1dtls.userAlertList, nullptr) << "ContactChange  --  contact deleted";
    ASSERT_EQ(B1dtls.userAlertList->size(), 1) << "ContactChange  --  contact deleted";
    a = B1dtls.userAlertList->get(0);
    ASSERT_STRCASEEQ(a->getEmail(), A1dtls.email.c_str()) << "ContactChange  --  contact deleted";
    ASSERT_STRCASEEQ(a->getTitle(), "Deleted you as a contact") << "ContactChange  --  contact deleted";
    ASSERT_GT(a->getId(), 0u) << "ContactChange  --  contact deleted";
    ASSERT_EQ(a->getType(), MegaUserAlert::TYPE_CONTACTCHANGE_DELETEDYOU) << "ContactChange  --  contact deleted";
    ASSERT_STREQ(a->getTypeString(), "CONTACT_DISCONNECTED") << "ContactChange  --  contact deleted";
    ASSERT_STRCASEEQ(a->getHeading(), A1dtls.email.c_str()) << "ContactChange  --  contact deleted";
    ASSERT_NE(a->getTimestamp(0), 0) << "ContactChange  --  contact deleted";
    ASSERT_FALSE(a->isOwnChange()) << "ContactChange  --  contact deleted";
    ASSERT_EQ(a->getUserHandle(), A1.getMyUserHandleBinary()) << "ContactChange  --  contact deleted";
    bkpList.emplace_back(a->copy());


#if MEGA_TEST_SC50_ALERTS
    // reset User Alerts for B2
    B2dtls.userAlertsUpdated = false;
    B2dtls.userAlertList.reset();

    // resume session for B2
    ASSERT_EQ(API_OK, synchronousFastLogin(B2idx, B2session.get(), this)) << "Resume session failed for B2 (error: " << B2dtls.lastError << ")";
    ASSERT_NO_FATAL_FAILURE(fetchnodes(B2idx));
    // test sc50 after session resume
    ASSERT_TRUE(waitForResponse(&B2dtls.userAlertsUpdated, sc50Timeout))
        << "sc50 alerts after resumeSession() not received by B2 after " << sc50Timeout << " seconds";
    ASSERT_EQ(B2dtls.userAlertList, nullptr) << "sc50";
    sc50List.reset(B2.getUserAlerts());
    ASSERT_TRUE(sc50List);
    ASSERT_NE(sc50List->size(), 0);
    ASSERT_GE(sc50List->size(), (int)bkpList.size());

    // sort sc50 alerts by timestamp
    // this shoud not be needed, but is here to show that not all alerts have been received
    map<int64_t, const MegaUserAlert*> sc50alerts;
    for (int i = 0; i < sc50List->size(); ++i)
    {
        const auto* sc50a = sc50List->get(i);
        sc50alerts[sc50a->getTimestamp(0)] = sc50a;
    }

    // compare last sc50 alerts with the ones generated by the actions above
    // WARNING: At least in this scenario (resume session after the share has been removed), sc50 returned only the following alerts:
    //  - TYPE_INCOMINGPENDINGCONTACT_REQUEST (0),
    //  - TYPE_NEWSHARE (12),
    //  - TYPE_DELETEDSHARE (13)
    //  - TYPE_CONTACTCHANGE_DELETEDYOU (3),

    size_t count = 0;
    for (auto it = sc50alerts.rbegin(); it != sc50alerts.rend() && count < bkpList.size(); ++it)
    {
        const auto* sc50a = it->second;
        const auto* bkp = (bkpList.rbegin() + count)->get();

        ASSERT_EQ(bkp->getType(), sc50a->getType()) << "sc50";
        ASSERT_STREQ(bkp->getTypeString(), sc50a->getTypeString()) << "sc50";
        ASSERT_EQ(bkp->getUserHandle(), sc50a->getUserHandle()) << "sc50";
        ASSERT_EQ(bkp->getNodeHandle(), sc50a->getNodeHandle()) << "sc50";
        ASSERT_EQ(bkp->getPcrHandle(), sc50a->getPcrHandle()) << "sc50";
        ASSERT_STRCASEEQ(bkp->getEmail(), sc50a->getEmail()) << "sc50";
        if (sc50a->getPath()) // the node might not be there when sc50 alerts arrive
        {
            ASSERT_STRCASEEQ(bkp->getPath(), sc50a->getPath()) << "sc50";
        }
        if (sc50a->getName()) // the node might not be there when sc50 alerts arrive
        {
            ASSERT_STREQ(bkp->getName(), sc50a->getName()) << "sc50";
        }
        ASSERT_STRCASEEQ(bkp->getHeading(), sc50a->getHeading()) << "sc50";
        ASSERT_STRCASEEQ(bkp->getTitle(), sc50a->getTitle()) << "sc50";
        ASSERT_EQ(bkp->getNumber(0), sc50a->getNumber(0)) << "sc50";
        ASSERT_EQ(bkp->getNumber(1), sc50a->getNumber(1)) << "sc50";
        // ASSERT_EQ(bkp->getTimestamp(0), sc50a->getTimestamp(0)) << "sc50"; // timestamp will differ, because for sc50 alerts it gets calculated
        ASSERT_STRCASEEQ(bkp->getString(0), sc50a->getString(0)) << "sc50";
        ASSERT_EQ(bkp->getHandle(0), sc50a->getHandle(0)) << "sc50";
        ASSERT_EQ(bkp->getHandle(1), sc50a->getHandle(1)) << "sc50";

        ++count;
    }
#endif
}

/*
TEST_F(SdkTest, CheckRecoveryKey_MANUAL)
{
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));
    const char* email = megaApi[0]->getMyEmail();
    cout << "email: " << email << endl;
    const char* masterKey = megaApi[0]->getMyRSAPrivateKey();

    mApi[0].requestFlags[MegaRequest::TYPE_GET_RECOVERY_LINK] = false;
    megaApi[0]->resetPassword(email, true);
    ASSERT_TRUE(waitForResponse(&mApi[0].requestFlags[MegaRequest::TYPE_GET_RECOVERY_LINK]))
        << "get recovery link/reset password failed after " << maxTimeout << " seconds";
    ASSERT_EQ(mApi[0].lastError, MegaError::API_OK);

    cout << "input link: ";
    string link;
    getline(cin, link);

    cout << "input recovery key: ";
    string recoverykey;
    getline(cin, recoverykey);

    mApi[0].requestFlags[MegaRequest::TYPE_CHECK_RECOVERY_KEY] = false;
    megaApi[0]->checkRecoveryKey(link.c_str(), recoverykey.c_str());
    ASSERT_TRUE(waitForResponse(&mApi[0].requestFlags[MegaRequest::TYPE_CHECK_RECOVERY_KEY]))
        << "check recovery key failed after " << maxTimeout << " seconds";
    ASSERT_EQ(mApi[0].lastError, MegaError::API_OK); // API_EKEY

    // set to the same password
    const char* password = getenv("MEGA_PWD");
    ASSERT_TRUE(password);
    mApi[0].requestFlags[MegaRequest::TYPE_CONFIRM_RECOVERY_LINK] = false;
    megaApi[0]->confirmResetPassword(link.c_str(), password, masterKey);
    ASSERT_TRUE(waitForResponse(&mApi[0].requestFlags[MegaRequest::TYPE_CONFIRM_RECOVERY_LINK]))
        << "confirm recovery link failed after " << maxTimeout << " seconds";
    ASSERT_EQ(mApi[0].lastError, MegaError::API_OK);
}
*/
