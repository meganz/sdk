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
 * You should have received a copy of the license along with this

 * program.
 */
#include "SdkTest_test.h"

#include "../stdfs.h"
#include "env_var_accounts.h"
#include "gmock/gmock-matchers.h"
#include "gtest_common.h"
#include "integration_test_utils.h"
#include "mega/account.h"
#include "mega/scoped_helpers.h"
#include "mega/testhooks.h"
#include "mega/types.h"
#include "megaapi.h"
#include "megautils.h"
#include "mock_listeners.h"
#include "sdk_test_utils.h"
#include "test.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>

#if !defined(WIN32) && defined(ENABLE_ISOLATED_GFX)
#include "mega/posix/gfx/worker/socket_utils.h"
using ::mega::gfx::SocketUtils;
#endif

#define SSTR( x ) static_cast< const std::ostringstream & >( \
        (  std::ostringstream() << std::dec << x ) ).str()

using namespace std;

static const string APP_KEY     = "8QxzVRxD";
static const string PUBLICFILE  = "file.txt";
static const string UPFILE      = "file1.txt";
static const string DOWNFILE    = "file2.txt";
static const string EMPTYFILE = "empty-file.txt";
static const string IMAGEFILE   = "logo.png";
static const string& AVATARSRC = IMAGEFILE;
static const string AVATARDST = "deleteme.png";
static const string IMAGEFILE_C = "logo.encrypted.png";
static const string THUMBNAIL   = "logo_thumbnail.png";
static const string PREVIEW     = "logo_preview.png";
static const string PUBLIC_IMAGE_URL = "/#!zAJnUTYD!8YE5dXrnIEJ47NdDfFEvqtOefhuDMphyae0KY5zrhns"; //gitleaks:allow

std::unique_ptr<::mega::FileSystemAccess> fileSystemAccess = ::mega::createFSA();

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

const char* cwd()
{
    // for windows and linux
    static char path[1024];
    const char* ret;
#ifdef _WIN32
    #define getcwd _getcwd
    ret = _getcwd(path, sizeof path);
    #undef getcwd
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
    fileSystemAccess->copylocal(f, t, m_time());
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
        std::unique_ptr<DirAccess> da(fileSystemAccess->newdiraccess());
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

bool SdkTest::WaitFor(const std::function<bool()>& predicate, unsigned timeoutMs)
{
    const unsigned sleepMs = 100;
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

enum { USERALERT_ARRIVAL_MILLISEC = 1000 };

#ifdef _WIN32
#include "mega/autocomplete.h"
#include <filesystem>
#endif

void cleanUp(::mega::MegaApi* megaApi, const fs::path &basePath);

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
            ofstream fs(fp/*, ios::binary*/);
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
        ofstream fs(fp/*, ios::binary*/);
        if (byteSize)
        {
            fs.seekp((byteSize << 10) - 1);
        }
        fs << name;
        return true;
    }

    //
    // Get a new endpoint name without conflicts with any running instances
    // under the following situations:
    //      1. Jenkins can run multiple test jobs at the same time
    //      2. A test job can run tests in parallel
    // Use current process ID so names are unique between different jobs (processes)
    // Use a static incremental counter so names are unique in the same job (process)
    std::string newEndpointName()
    {
        static std::atomic_int counter{0};
        int current = counter++;

        std::ostringstream oss;
        oss << "test_integration_" << getCurrentPid() << "_" << current;
        return oss.str();
    }

    std::string executableName(const std::string& name)
    {
    #ifdef WIN32
        return name + ".exe";
    #else
        return name;
    #endif
    }

    MegaApiTestPointer newMegaApi(const char* appKey,
                                  const char* basePath,
                                  const char* userAgent,
                                  unsigned workerThreadCount,
                                  const int clientType = MegaApi::CLIENT_TYPE_DEFAULT)
    {
#ifdef ENABLE_ISOLATED_GFX
        const auto gfxworkerPath = sdk_test::getTestDataDir() / executableName("gfxworker");
        const auto endpointName = newEndpointName();
        std::unique_ptr<MegaGfxProvider> provider{
            MegaGfxProvider::createIsolatedInstance(endpointName.c_str(), gfxworkerPath.string().c_str())
        };
        return MegaApiTestPointer{new MegaApiTest(appKey,
                                                  provider.get(),
                                                  basePath,
                                                  userAgent,
                                                  workerThreadCount,
                                                  clientType),
                                  MegaApiTestDeleter{endpointName}};
#else
        return MegaApiTestPointer{
            new MegaApiTest(appKey, basePath, userAgent, workerThreadCount, clientType),
            MegaApiTestDeleter{""}};
#endif
    }

    enum class HasIcon
    {
        YES,
        NO
    };

    void validateNotification(const MegaNotification* notification, int64_t id, HasIcon hasIcon)
    {
        ASSERT_EQ(notification->getID(), id);
        ASSERT_STRNE(notification->getTitle(), "");
        ASSERT_STRNE(notification->getDescription(), "");
        ASSERT_STRNE(notification->getImageName(), "");
        if (hasIcon == HasIcon::NO)
        {
            ASSERT_STREQ(notification->getIconName(), "");
        }
        else
        {
            ASSERT_STRNE(notification->getIconName(), "");
        }
        ASSERT_STRNE(notification->getImagePath(), "");
        ASSERT_NE(notification->getStart(), 0);
        ASSERT_NE(notification->getEnd(), 0);
        ASSERT_THAT(notification->getCallToAction1(), ::testing::NotNull());
        ASSERT_NE(notification->getCallToAction1()->size(), 0);
        ASSERT_THAT(notification->getCallToAction2(), ::testing::NotNull());
        ASSERT_NE(notification->getCallToAction2()->size(), 0);
    };

}

namespace mega
{
std::ostream& operator<<(std::ostream& os, const ::mega::MegaNodeList& nodeList)
{
    os << "[";
    for (int i = 0; i < nodeList.size(); i++)
    {
        const char* name = nodeList.get(i)->getName();
        i == 0 ? (os << name) : (os << ", " << name);
    }
    os << "]";
    return os;
}
}

std::map<size_t, std::string> gSessionIDs;

MegaApiTest::MegaApiTest(const char* appKey,
                         const char* basePath,
                         const char* userAgent,
                         unsigned workerThreadCount,
                         const int clientType):
    MegaApi(appKey, basePath, userAgent, workerThreadCount, clientType)
{
}

MegaApiTest::MegaApiTest(const char* appKey,
                         MegaGfxProvider* provider,
                         const char* basePath,
                         const char* userAgent,
                         unsigned workerThreadCount,
                         const int clientType):
    MegaApi(appKey, provider, basePath, userAgent, workerThreadCount, clientType)
{
}

MegaClient* MegaApiTest::getClient()
{
    return pImpl->getMegaClient();
}

void MegaApiTestDeleter::operator()(MegaApiTest* p) const
{
    delete p;

    // Clean up the socket file if it has been created and only after MegaApiTest is deleted.
    // Reason: the GfxIsolatedProcess is desctructed in the subclass MegaApi
    // Another alernative is to clean up the socket file in the GfxIsolatedProcess destructor.
    // However it might clean up a socket file created by a new GfxIsolatedProcess is a same
    // name is used alghouth it seems be rare.
#if !defined(WIN32) && defined(ENABLE_ISOLATED_GFX)
    if (mEndpointName.empty())
        return;

    if (std::error_code errorCode = SocketUtils::removeSocketFile(mEndpointName))
    {
        LOG_err << "Failed to remove socket path " << mEndpointName << ": " << errorCode.message();
    }
#endif
}

void SdkTest::SetUp()
{
    SdkTestBase::SetUp();

    setTestAccountsToFree();
}

void SdkTest::TearDown()
{
    out() << "Test done, teardown starts";

    LOG_info << "# SdkTest::TearDown - resetting accounts to initial level";
    if (!mAccountsRestorer.empty())
        LOG_info << "## resetting " << mAccountsRestorer.size() << " accounts";
    mAccountsRestorer.clear();

    // do some cleanup

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

std::pair<std::string, std::string> SdkTest::getTestSuiteAndName() const
{
    const auto* testInfo = ::testing::UnitTest::GetInstance()->current_test_info();
    if (testInfo == nullptr)
    {
        assert(testInfo && "This is expected to be called from a test");
        return {};
    }
    return {testInfo->test_suite_name(), testInfo->name()};
}

std::string SdkTest::getLogPrefix() const
{
    const auto [suite, name] = getTestSuiteAndName();
    return suite + "." + name + " : ";
}

std::string SdkTest::getFilePrefix() const
{
    const auto [suite, name] = getTestSuiteAndName();
    return suite + "_" + name + "_";
}

void SdkTest::Cleanup()
{
    LOG_debug << "[SdkTest::Cleanup]";
    mCleanupSuccess = true;
    cleanupLocalFiles();

#ifdef ENABLE_CHAT
    cleanupSchedMeetingsAllAccounts();
    cleanupChatLinksAllAccounts();
#endif

#ifdef ENABLE_SYNC
    cleanupSyncsAllAccounts();
#endif

    set<string> alreadyRemoved;
    cleanupContactRequestsAllAccounts();
    cleanupContactsAllAccounts(alreadyRemoved);
    cleanupSharesAllAccounts(alreadyRemoved);
    cleanupNodeLinksAllAccounts();
    cleanupNodesAllAccounts();
    LOG_debug << "[SdkTest::Cleanup]: " << (mCleanupSuccess ? "Finished successfully" : "Failed");
    EXPECT_TRUE(mCleanupSuccess) << "[SdkTest::Cleanup]: Mark test as failed";
}

void SdkTest::setTestAccountsToFree()
{
    LOG_info << "# SdkTest::setTestAccountsToFree";
    auto totalAccounts = static_cast<unsigned int>(getEnvVarAccounts().size());
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(totalAccounts));

    auto accountsIdx = Range(0u, totalAccounts);
    std::for_each(begin(accountsIdx),
                  end(accountsIdx),
                  [this](const auto idx)
                  {
                      auto& client = megaApi[idx];
                      auto level = value(getAccountLevel(*client));
                      if (level.plan == MegaAccountDetails::ACCOUNT_TYPE_FREE)
                      {
                          LOG_info << "## Account (" << idx << ") is free already";
                          releaseMegaApi(idx);
                          return;
                      }

                      if (!gFreeAccounts)
                      {
                          mAccountsRestorer.push_back(accountLevelRestorer(megaApi, idx));
                      }

                      LOG_info << "## Force account to free status. Originally at plan: "
                               << level.plan << " months: " << level.months;
                      auto result = setAccountLevel(*client,
                                                    MegaAccountDetails::ACCOUNT_TYPE_FREE,
                                                    level.months,
                                                    nullptr);
                      EXPECT_EQ(result, API_OK) << "Couldn't reset account to free: " << result;

                      releaseMegaApi(idx);
                  });
}

int SdkTest::getApiIndex(MegaApi* api)
{
    int apiIndex = -1;
    for (int i = int(megaApi.size()); i--;)
        if (megaApi[static_cast<size_t>(i)].get() == api)
            apiIndex = i;
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

    int index = getApiIndex(api);
    if (index < 0) return;
    size_t apiIndex = static_cast<size_t>(index);
    mApi[apiIndex].lastError = e->getErrorCode();

    // there could be a race on these getting set?
    LOG_info << "lastError (by request) for MegaApi " << apiIndex << ": " << mApi[apiIndex].lastError;

    switch(type)
    {
    case MegaRequest::TYPE_GET_ATTR_USER:
        if (mApi[apiIndex].lastError == API_OK)
        {
            if (request->getParamType() == MegaApi::USER_ATTR_DEVICE_NAMES ||
                request->getParamType() == MegaApi::USER_ATTR_ALIAS)
            {
                mApi[apiIndex].setAttributeValue(request->getName() ? request->getName() : "");
            }
            else if (request->getParamType() == MegaApi::USER_ATTR_MY_BACKUPS_FOLDER)
            {
                mApi[apiIndex].lastSyncBackupId = request->getNodeHandle();
            }
            else if (request->getParamType() == MegaApi::USER_ATTR_APPS_PREFS)
            {
                mApi[apiIndex].mStringMap.reset(request->getMegaStringMap()->copy());
            }
            else if (request->getParamType() == MegaApi::USER_ATTR_CC_PREFS)
            {
                mApi[apiIndex].mStringMap.reset(request->getMegaStringMap()->copy());
            }
            else if (request->getParamType() != MegaApi::USER_ATTR_AVATAR)
            {
                mApi[apiIndex].setAttributeValue(request->getText() ? request->getText() : "");
            }
        }

        if (request->getParamType() == MegaApi::USER_ATTR_AVATAR)
        {
            if (mApi[apiIndex].lastError == API_OK)
            {
                mApi[apiIndex].setAttributeValue("Avatar changed");
            }

            if (mApi[apiIndex].lastError == API_ENOENT)
            {
                mApi[apiIndex].setAttributeValue("Avatar not found");
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
            mApi[apiIndex].setChatLink(request->getLink());
        }
        break;
#endif

    case MegaRequest::TYPE_CREATE_ACCOUNT:
        if (mApi[apiIndex].lastError == API_OK)
        {
            mApi[apiIndex].setSid(request->getSessionKey());
        }
        break;

    case MegaRequest::TYPE_GET_COUNTRY_CALLING_CODES:
        if (mApi[apiIndex].lastError == API_OK)
        {
            mApi[apiIndex].setStringLists(request->getMegaStringListMap()->copy());
        }
        break;

    case MegaRequest::TYPE_FOLDER_INFO:
        if (mApi[apiIndex].lastError == API_OK)
        {
            mApi[apiIndex].mFolderInfo.reset(request->getMegaFolderInfo()->copy());
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
        mApi[apiIndex].setBackupId(request->getParentHandle());
        break;

    case MegaRequest::TYPE_GET_ATTR_NODE:
        if (mApi[apiIndex].lastError == API_OK)
        {
            mApi[apiIndex].setFavNodes(request->getMegaHandleList()->copy());
        }
        break;

    case MegaRequest::TYPE_GET_PRICING:
        mApi[apiIndex].mMegaPricing.reset(mApi[apiIndex].lastError == API_OK ? request->getPricing() : nullptr);
        mApi[apiIndex].mMegaCurrency.reset(mApi[apiIndex].lastError == API_OK ? request->getCurrency() : nullptr);
        break;

#ifdef ENABLE_CHAT
    case MegaRequest::TYPE_ADD_UPDATE_SCHEDULED_MEETING:
        if (mApi[apiIndex].lastError == API_OK
            && request->getMegaScheduledMeetingList()
            && request->getMegaScheduledMeetingList()->size() == 1)
        {
            const auto sched = request->getMegaScheduledMeetingList()->at(0);
            mApi[apiIndex].chatid = sched->chatid();
            mApi[apiIndex].schedId = sched->schedId();
            mApi[apiIndex].schedUpdated = true;
        }
        break;

    case MegaRequest::TYPE_DEL_SCHEDULED_MEETING:
        if (mApi[apiIndex].lastError == API_OK)
        {
            mApi[apiIndex].schedUpdated = true;
            mApi[apiIndex].schedId = request->getParentHandle();
        }
        break;
#endif
    }

    // set this flag always the latest, since it is used to unlock the wait
    // for requests results, so we want data to be collected first
    mApi[apiIndex].requestFlags[request->getType()] = true;
}

void SdkTest::onTransferStart(MegaApi*, MegaTransfer* transfer)
{
    onTransferStart_progress = transfer->getTransferredBytes();
    if (onTransferStartCustomCb)
    {
        onTransferStartCustomCb(transfer);
    }
}

void SdkTest::onTransferFinish(MegaApi* api, MegaTransfer *transfer, MegaError* e)
{
    int tempApiIndex = getApiIndex(api);
    if (tempApiIndex < 0)
        return;
    size_t apiIndex = static_cast<size_t>(tempApiIndex);

    mApi[apiIndex].transferFlags[transfer->getType()] = true;
    mApi[apiIndex].lastError = e->getErrorCode(); // todo: change the rest of the transfer test code
                                                  // to use lastTransferError instead.
    mApi[apiIndex].lastTransferError = e->getErrorCode();

    // there could be a race on these getting set?
    LOG_info << "lastError (by transfer) for MegaApi " << apiIndex << ": "
             << mApi[apiIndex].lastError;

    onTranferFinishedCount += 1;

    // Transfer stats.
    // We need to access the MegaTransferPrivate because the stats
    // are not part of the public interface so we need to retrieve the Transfer object.
    if (auto transferPrivate = dynamic_cast<MegaTransferPrivate*>(transfer))
    {
        auto internalTransfer = transferPrivate->getTransfer();
        if (internalTransfer && internalTransfer->slot)
        {
            onTransferFinish_transferStats = internalTransfer->slot->tsStats;
            LOG_debug << "[SdkTest::onTransferFinish] Stats: FailedRequestRatio = "
                      << onTransferFinish_transferStats.failedRequestRatio
                      << " [totalRequests = " << onTransferFinish_transferStats.numTotalRequests
                      << ", failedRequests = " << onTransferFinish_transferStats.numFailedRequests
                      << "]";
        }
    }
}

void SdkTest::onTransferUpdate(MegaApi*, MegaTransfer* transfer)
{
    onTransferUpdate_progress = transfer->getTransferredBytes();
    onTransferUpdate_filesize = transfer->getTotalBytes();
}

void SdkTest::onAccountUpdate(MegaApi* api)
{
    int apiIndex = getApiIndex(api);
    if (apiIndex < 0) return;

    mApi[static_cast<size_t>(apiIndex)].accountUpdated = true;
}

void SdkTest::onUsersUpdate(MegaApi* api, MegaUserList *users)
{
    int apiIndex = getApiIndex(api);
    if (apiIndex < 0) return;

    if (!users)
        return;

    auto& currentPerApi = mApi[static_cast<size_t>(apiIndex)];
    for (int i = 0; i < users->size(); i++)
    {
        MegaUser *u = users->get(i);

        if (u->hasChanged(MegaUser::CHANGE_TYPE_AVATAR)
                || u->hasChanged(MegaUser::CHANGE_TYPE_FIRSTNAME)
                || u->hasChanged(MegaUser::CHANGE_TYPE_LASTNAME))
        {
            currentPerApi.userUpdated = true;
            if (u->hasChanged(MegaUser::CHANGE_TYPE_FIRSTNAME) && !u->isOwnChange())
            {
                currentPerApi.userFirstNameUpdated = true;
            }
        }
        else
        {
            // Contact is removed from main account
            currentPerApi.requestFlags[MegaRequest::TYPE_REMOVE_CONTACT] = true;
            currentPerApi.userUpdated = true;
        }
        currentPerApi.callCustomCallbackCheck(u->getHandle());
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

    mApi[static_cast<size_t>(apiIndex)].setUpdated = true;
}

void SdkTest::onSetElementsUpdate(MegaApi* api, MegaSetElementList* elements)
{
    int apiIndex = getApiIndex(api);
    if (apiIndex < 0 || !elements || !elements->size()) return;

    for (unsigned int i = 0; i < elements->size(); ++i)
    {
        if (!elements->get(i)->getChanges())
        {
            LOG_err << "GlobalListener::onSetElementsUpdate no change received for elements[" << i << "]";
            return;
        }
    }

    mApi[static_cast<size_t>(apiIndex)].setElementUpdated = true;
}

void SdkTest::onContactRequestsUpdate(MegaApi* api, MegaContactRequestList*)
{
    int apiIndex = getApiIndex(api);
    if (apiIndex < 0) return;

    mApi[static_cast<size_t>(apiIndex)].contactRequestUpdated = true;
}

void SdkTest::onUserAlertsUpdate(MegaApi* api, MegaUserAlertList* alerts)
{
    int apiIndex = getApiIndex(api);
    if (apiIndex < 0) return;

    mApi[static_cast<size_t>(apiIndex)].userAlertList.reset(alerts ? alerts->copy() : nullptr);
    mApi[static_cast<size_t>(apiIndex)].userAlertsUpdated = true;
}
#ifdef ENABLE_CHAT
void SdkTest::onChatsUpdate(MegaApi *api, MegaTextChatList *chats)
{
    int tempApiIndex = getApiIndex(api);
    if (tempApiIndex < 0)
        return;
    size_t apiIndex = static_cast<size_t>(tempApiIndex);

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
        handle chatid = list->get(static_cast<unsigned>(i))->getHandle();
        if (mApi[apiIndex].chats.find(chatid) != mApi[apiIndex].chats.end())
        {
            mApi[apiIndex].chats[chatid].reset(list->get(static_cast<unsigned>(i))->copy());
        }
        else
        {
            mApi[apiIndex].chats[chatid].reset(list->get(static_cast<unsigned>(i))->copy());
        }
    }
    delete list;

    mApi[apiIndex].chatUpdated = true;
    mApi[apiIndex].callCustomCallbackCheck(mApi[apiIndex].megaApi->getMyUserHandleBinary());
}

void SdkTest::cleanupChatLinksAllAccounts()
{
    const std::string prefix{"SdkTest::Cleanup(RemoveChatLinks)"};
    LOG_debug << "# " << prefix;
    bool localCleanupSuccess{true};
    for (auto nApi = unsigned(megaApi.size()); nApi--;)
    {
        if (!megaApi[nApi] || !megaApi[nApi]->isLoggedIn())
        {
            continue;
        }

        unique_ptr<MegaTextChatList> chats(megaApi[nApi]->getChatList());
        for (int i = 0u; i < chats->size(); ++i)
        {
            const MegaTextChat* c = chats->get(static_cast<unsigned>(i));
            const auto numPeers = c->getPeerList() ? c->getPeerList()->size() : 0;
            if (auto processChat = c->isPublicChat() && c->getOwnPrivilege() == PRIV_MODERATOR &&
                                   (numPeers || c->isGroup());
                !processChat)
            {
                continue;
            }

            RequestTracker rt(megaApi[nApi].get());
            megaApi[nApi]->chatLinkQuery(c->getHandle(), &rt);
            if (const auto e = rt.waitForResult(); e == API_OK)
            {
                RequestTracker rtD(megaApi[nApi].get());
                megaApi[nApi]->chatLinkDelete(c->getHandle(), &rtD);

                if (auto errCld = rtD.waitForResult(); errCld != API_OK)
                {
                    bool iterationCleanupSuccess{true};
                    const string errDetails =
                        "Error deleting chatlink for chat (" +
                        string{Base64Str<MegaClient::CHATHANDLE>(c->getHandle())} + ")";

                    if (errCld != API_EACCESS)
                    {
                        localCleanupSuccess = iterationCleanupSuccess = false;
                    }
                    printCleanupErrMsg(prefix,
                                       errDetails,
                                       static_cast<unsigned>(nApi),
                                       errCld,
                                       iterationCleanupSuccess);
                }
            }
            else
            {
                const string errDetails =
                    "Error getting chat link for chat (" +
                    string{Base64Str<MegaClient::CHATHANDLE>(c->getHandle())} + ")";
                printCleanupErrMsg(prefix,
                                   errDetails,
                                   static_cast<unsigned>(nApi),
                                   e,
                                   true /*localCleanupSuccess*/);
            }
        }
    }
    updateCleanupStatus(localCleanupSuccess);
    LOG_debug << "# " << prefix << (localCleanupSuccess ? ": OK" : ": Finished with errors");
}

void SdkTest::createChat(bool group, MegaTextChatPeerList *peers, int timeout)
{
    size_t apiIndex = 0;
    mApi[apiIndex].requestFlags[MegaRequest::TYPE_CHAT_CREATE] = false;
    megaApi[0]->createChat(group, peers);
    waitForResponse(&mApi[apiIndex].requestFlags[MegaRequest::TYPE_CHAT_CREATE],
                    static_cast<unsigned>(timeout));
    if (timeout)
    {
        ASSERT_TRUE(mApi[apiIndex].requestFlags[MegaRequest::TYPE_CHAT_CREATE])
            << "Chat creation not finished after " << timeout << " seconds";
    }

    ASSERT_EQ(API_OK, mApi[apiIndex].lastError)
        << "Chat creation failed (error: " << mApi[static_cast<size_t>(apiIndex)].lastError << ")";
}

/**
 * @brief Aux function to get a set of handles from a list of peers
 */
static std::set<MegaHandle> peerListToHandleSet(const MegaTextChatPeerList *peers)
{
    std::set<MegaHandle> result;
    for (int i = 0 ; i < peers->size(); ++i)
    {
        result.insert(peers->getPeerHandle(i));
    }
    return result;
}

MegaHandle SdkTest::createChatWithChecks(const unsigned int creatorIndex,
                                         const std::vector<unsigned int>& invitedIndices,
                                         const bool group,
                                         const unsigned int timeout_sec)
{
    std::unique_ptr<MegaTextChatPeerList> invitedPeers(MegaTextChatPeerList::createInstance());
    std::set<MegaHandle> allParticipantsHandles{
        mApi[creatorIndex].megaApi->getMyUserHandleBinary()};
    for (auto ind: invitedIndices)
    {
        auto uh = mApi[ind].megaApi->getMyUserHandleBinary();
        invitedPeers->addPeer(uh, PRIV_STANDARD);
        allParticipantsHandles.insert(uh);
    }
    // Function to check that a chat is created with the given specs
    auto isChatOK = [group, &allParticipantsHandles](const MegaTextChat& chat,
                                                     const MegaHandle receiverHandle) -> bool
    {
        if (chat.isGroup() != group)
        {
            return false;
        }
        const MegaTextChatPeerList* receivedPeers = chat.getPeerList();
        if (!receivedPeers ||
            static_cast<size_t>(receivedPeers->size()) != allParticipantsHandles.size() - 1)
        {
            return false;
        }
        auto participantsHandle = peerListToHandleSet(receivedPeers);
        participantsHandle.insert(receiverHandle);
        return participantsHandle == allParticipantsHandles;
    };
    // Register a callback and a boolean for each participant
    std::vector<std::pair<std::shared_ptr<std::function<void()>>, bool>> customChecksAndResults(
        allParticipantsHandles.size());
    std::vector<unsigned int> allParticipantsIndices(invitedIndices);
    allParticipantsIndices.push_back(creatorIndex);

    for (unsigned int i = 0; i < allParticipantsIndices.size(); ++i)
    {
        customChecksAndResults.push_back({std::make_shared<std::function<void()>>(), false});
        auto userInd = allParticipantsIndices[i];
        MegaHandle receiverHandle = mApi[userInd].megaApi->getMyUserHandleBinary();
        auto customCheck = customChecksAndResults.back().first;
        *customCheck = [this, userInd, i, receiverHandle, &customChecksAndResults, &isChatOK]()
        {
            const auto& chats = mApi[userInd].chats;
            customChecksAndResults[i].second = std::any_of(
                chats.begin(),
                chats.end(),
                [receiverHandle, &isChatOK](const auto& pair)
                {
                    return isChatOK(*pair.second, receiverHandle);
                });
        };
        mApi[userInd].customCallbackCheck[receiverHandle] = customCheck;
    }

    // Check that the chatid is properly set in the onRequestFinish callback. Set initial value
    mApi[creatorIndex].chatid = INVALID_HANDLE;
    megaApi[creatorIndex]->createChat(group, invitedPeers.get());

    bool hasRequestFinished = waitForEvent(
        [this, creatorIndex]()
        {
            return mApi[creatorIndex].chatid != INVALID_HANDLE;
        },
        timeout_sec);
    if (!hasRequestFinished)
    {
        EXPECT_TRUE(false) << "Chat creation onRequestFinish not called after " << timeout_sec
                           << "seconds";
        return INVALID_HANDLE;
    }

    for (unsigned int i = 0; i < allParticipantsIndices.size(); ++i)
    {
        if (!waitForResponse(&customChecksAndResults[i].second, timeout_sec))
        {
            EXPECT_TRUE(false) << "Chat update not received for user " << allParticipantsIndices[i]
                               << " after " << timeout_sec << " seconds";
            return INVALID_HANDLE;
        }
    }
    return mApi[creatorIndex].chatid;
}
#endif

void SdkTest::onEvent(MegaApi* s, MegaEvent *event)
{
    int index = getApiIndex(s);
    if (index >= 0) // it can be -1 when tests are being destroyed
    {
        mApi[static_cast<size_t>(index)].receiveEvent(event);
        LOG_debug << index << " Received event " << event->getType();
    }
}

void SdkTest::fetchnodes(unsigned int apiIndex, int timeout)
{
    RequestTracker rt(megaApi[apiIndex].get());
    mApi[apiIndex].megaApi->fetchNodes(&rt);
    ASSERT_EQ(API_OK, rt.waitForResult(timeout))
        << "Fetchnodes failed or took more than " << timeout << " seconds";
}

void SdkTest::logout(unsigned int apiIndex, [[maybe_unused]] bool keepSyncConfigs, int timeout)
{
    mApi[apiIndex].requestFlags[MegaRequest::TYPE_LOGOUT] = false;
#ifdef ENABLE_SYNC
    mApi[apiIndex].megaApi->logout(keepSyncConfigs, this);
#else
    mApi[apiIndex].megaApi->logout(this);
#endif
    gSessionIDs[apiIndex] = "invalid";

    EXPECT_TRUE(waitForResponse(&mApi[apiIndex].requestFlags[MegaRequest::TYPE_LOGOUT],
                                static_cast<unsigned>(timeout)))
        << "Logout failed after " << timeout << " seconds";

    // if the connection was closed before the response of the request was received, the result is ESID
    if (mApi[apiIndex].lastError == API_ESID) mApi[apiIndex].lastError = API_OK;

    EXPECT_EQ(API_OK, mApi[apiIndex].lastError) << "Logout failed (error: " << mApi[apiIndex].lastError << ")";
}

char* SdkTest::dumpSession(unsigned apiIndex)
{
    return megaApi[apiIndex]->dumpSession();
}

void SdkTest::locallogout(unsigned apiIndex)
{
    auto logoutErr = doRequestLocalLogout(apiIndex);
    ASSERT_EQ(API_OK, logoutErr) << "Local logout failed (error: " << logoutErr << ")";
}

void SdkTest::resumeSession(const char *session, unsigned apiIndex)
{
    ASSERT_EQ(API_OK, synchronousFastLogin(apiIndex, session, this)) << "Resume session failed (error: " << mApi[apiIndex].lastError << ")";
}

void SdkTest::purgeTree(unsigned int apiIndex, MegaNode *p, bool depthfirst)
{
    MegaHandle owner = megaApi[apiIndex]->getMyUserHandleBinary();
    std::unique_ptr<MegaNodeList> children{megaApi[apiIndex]->getChildren(p)};

    for (int i = 0; i < children->size(); i++)
    {
        MegaNode *n = children->get(i);

        // removing the folder removes the children anyway
        if (depthfirst && n->isFolder())
            purgeTree(apiIndex, n);

        if (owner != n->getOwner())
            continue;

        string nodepath = n->getName() ? n->getName() : "<no name>";
        auto result = synchronousRemove(apiIndex, n);
        if (result == API_EEXIST || result == API_ENOENT)
        {
            LOG_warn << "node " << nodepath << " was already removed in api " << apiIndex << ", detected by error code " << result;
            result = API_OK;
        }

        ASSERT_EQ(API_OK, result) << "API " << apiIndex << ": Failed to remove node " << nodepath;
    }
}

void SdkTest::cleanupContactsAllAccounts(set<string>& alreadyRemoved)
{
    const std::string prefix{"SdkTest::Cleanup(RemoveContacts)"};
    LOG_debug << "# " << prefix;
    bool localCleanupSuccess{true};
    for (auto nApi = unsigned(megaApi.size()); nApi--;)
    {
        if (!megaApi[nApi] || !megaApi[nApi]->isLoggedIn())
        {
            continue;
        }

        auto myEmail(std::unique_ptr<char[]>{megaApi[nApi]->getMyEmail()});
        if (!myEmail || !std::strlen(myEmail.get()))
        {
            const string errDetails =
                "Error retrieving email for own account(" +
                string{Base64Str<MegaClient::USERHANDLE>(megaApi[nApi]->getMyUserHandleBinary())} +
                ")";
            printCleanupErrMsg(prefix,
                               errDetails,
                               static_cast<unsigned>(nApi),
                               API_EINTERNAL,
                               true /*localCleanupSuccess*/);
            continue;
        }
        const string myEmailStr{myEmail.get()};

        std::unique_ptr<MegaUserList> contacts{megaApi[nApi]->getContacts()};
        for (int i = 0; i < contacts->size(); i++)
        {
            const auto contactEmail = contacts->get(i)->getEmail();
            if (!contactEmail || !*contactEmail)
            {
                continue;
            }

            if (areCredentialsVerified(nApi, contactEmail))
            {
                // sometimes the email is an empty string (!)
                resetCredentials(nApi, contactEmail);
            }

            // avoid removing the same contact again in a 2nd client of the same account
            // (actionpackets from the first may not have arrived yet) or removing via the other
            // account, again the original disconnection may not have arrived by actionpacket
            // yet
            const string contactEmailStr{contactEmail};
            if (alreadyRemoved.find(myEmailStr + contactEmailStr) != alreadyRemoved.end())
                continue;
            if (alreadyRemoved.find(contactEmailStr + myEmailStr) != alreadyRemoved.end())
                continue;
            alreadyRemoved.insert(myEmailStr + contactEmailStr);

            if (contacts->get(i)->getVisibility() == MegaUser::VISIBILITY_HIDDEN)
            {
                continue;
            }

            if (const auto result = synchronousRemoveContact(nApi, contacts->get(i));
                result != API_OK && result != API_EEXIST)
            {
                const string errDetails = "Could not remove contact (" + contactEmailStr + ")";
                localCleanupSuccess = false;
                printCleanupErrMsg(prefix,
                                   errDetails,
                                   static_cast<unsigned>(nApi),
                                   result,
                                   localCleanupSuccess);
            }
            LOG_debug << prefix << "Catching up with API with account index(" << nApi << ")";
            ASSERT_EQ(API_OK, synchronousCatchup(nApi)) << "Failed to catchup for account " << nApi;
        }
    }
    updateCleanupStatus(localCleanupSuccess);
    LOG_debug << "# " << prefix << (localCleanupSuccess ? ": OK" : ": Finished with errors");
}

void SdkTest::cleanupSharesAllAccounts(set<string>& alreadyRemoved)
{
    const std::string prefix{"SdkTest::Cleanup(RemoveShares)"};
    LOG_debug << "# " << prefix;
    bool localCleanupSuccess{true};
    for (auto nApi = unsigned(megaApi.size()); nApi--;)
    {
        if (!megaApi[nApi] || !megaApi[nApi]->isLoggedIn())
        {
            continue;
        }

        auto myEmail(std::unique_ptr<char[]>{megaApi[nApi]->getMyEmail()});
        if (!myEmail || !std::strlen(myEmail.get()))
        {
            const string errDetails =
                "Error retrieving email for own account(" +
                string{Base64Str<MegaClient::USERHANDLE>(megaApi[nApi]->getMyUserHandleBinary())} +
                ")";
            printCleanupErrMsg(prefix,
                               errDetails,
                               static_cast<unsigned>(nApi),
                               API_EINTERNAL,
                               true /*localCleanupSuccess*/);
            continue;
        }
        const string myEmailStr{myEmail.get()};

        // Delete any inshares
        unique_ptr<MegaShareList> inshares(megaApi[nApi]->getInSharesList());
        for (int i = 0; i < inshares->size(); ++i)
        {
            LOG_debug << prefix << "megaApi[" << nApi << "] [InShare = " << i
                      << "] Inshare detected!";
            auto inshare = inshares->get(i);
            if (!inshare)
            {
                LOG_debug << prefix << "megaApi[" << nApi << "] [InShare = " << i
                          << "] MegaShare object is null, skipping...";
                continue;
            }

            if (auto email = inshare->getUser(); email)
            {
                if ((alreadyRemoved.find(myEmailStr + email) == alreadyRemoved.end()) &&
                    (alreadyRemoved.find(email + myEmailStr) == alreadyRemoved.end()))
                {
                    LOG_debug << prefix << "megaApi[" << nApi << "] [InShare = " << i
                              << "] Removing inshare's contact (also add '"
                              << string(myEmailStr + email) << "' as alreadyRemoved)...";
                    alreadyRemoved.insert(myEmailStr + email);
                    if (unique_ptr<MegaUser> shareUser(megaApi[nApi]->getContact(email)); shareUser)
                    {
                        if (auto result = synchronousRemoveContact(nApi, shareUser.get());
                            result != API_OK)
                        {
                            const string errDetails = "[Inshare = " + std::to_string(i) +
                                                      "] Error removing inshare's contact (" +
                                                      std::string{email} + ")";
                            localCleanupSuccess = false;
                            printCleanupErrMsg(prefix,
                                               errDetails,
                                               static_cast<unsigned>(nApi),
                                               result,
                                               localCleanupSuccess);
                        }
                    }
                    else
                    {
                        const string errDetails = "[Inshare = " + std::to_string(i) +
                                                  "] InShare has user (" + std::string{email} +
                                                  ") but the corresponding user does not exist";
                        printCleanupErrMsg(prefix,
                                           errDetails,
                                           static_cast<unsigned>(nApi),
                                           API_EINTERNAL,
                                           true /*localCleanupSuccess*/);
                    }
                }
            }

            LOG_debug << prefix << "megaApi[" << nApi << "] [InShare = " << i
                      << "] Removing inshare...";
            if (unique_ptr<MegaNode> n(megaApi[nApi]->getNodeByHandle(inshare->getNodeHandle())); n)
            {
                RequestTracker rt(megaApi[nApi].get());
                megaApi[nApi]->remove(n.get(), &rt);
                if (const auto res = rt.waitForResult(300); res != API_OK && res != API_EACCESS)
                {
                    const string errDetails =
                        "Removal of inshare folder (" +
                        string{Base64Str<MegaClient::NODEHANDLE>(n->getHandle())} +
                        +") failed or took more than 5 minutes";
                    localCleanupSuccess = false;
                    printCleanupErrMsg(prefix,
                                       errDetails,
                                       static_cast<unsigned>(nApi),
                                       res,
                                       localCleanupSuccess);
                }
            }
            else
            {
                const string errDetails = "[Inshare = " + std::to_string(i) + "] No node found!!!";
                printCleanupErrMsg(prefix,
                                   errDetails,
                                   static_cast<unsigned>(nApi),
                                   API_EINTERNAL,
                                   true /*localCleanupSuccess*/);
            }
        }

        // Delete any outshares
        unique_ptr<MegaShareList> outshares(megaApi[nApi]->getOutShares());
        for (int i = 0; i < outshares->size(); ++i)
        {
            LOG_debug << prefix << "megaApi[" << nApi << "] [OutShare = " << i
                      << "] OutShare detected!";
            auto os = outshares->get(i);
            if (!os)
            {
                LOG_debug << prefix << "megaApi[" << nApi << "] [OutShare = " << i
                          << "] MegaShare object is null, skipping...";
                continue;
            }

            if (auto email = os->getUser())
            {
                if ((alreadyRemoved.find(myEmailStr + email) == alreadyRemoved.end()) &&
                    (alreadyRemoved.find(email + myEmailStr) == alreadyRemoved.end()))
                {
                    LOG_debug << prefix << "megaApi[" << nApi << "] [OutShare = " << i
                              << "] Removing outshare's contact (also add '"
                              << string(myEmailStr + email) << "' as alreadyRemoved)...";
                    alreadyRemoved.insert(myEmailStr + email);

                    if (unique_ptr<MegaUser> shareUser(megaApi[nApi]->getContact(email)); shareUser)
                    {
                        auto result = synchronousRemoveContact(nApi, shareUser.get());
                        if (result != API_OK && result)
                        {
                            const string errDetails =
                                "Removal of outshare's contact (" + string{email} + ")";
                            localCleanupSuccess = false;
                            printCleanupErrMsg(prefix,
                                               errDetails,
                                               static_cast<unsigned>(nApi),
                                               result,
                                               localCleanupSuccess);
                        }
                    }
                    else
                    {
                        const string errDetails = "[OutShare = " + std::to_string(i) +
                                                  "] OutShare has user (" + std::string{email} +
                                                  ") but the corresponding user does not exist";
                        printCleanupErrMsg(prefix,
                                           errDetails,
                                           static_cast<unsigned>(nApi),
                                           API_EINTERNAL,
                                           true /*localCleanupSuccess*/);
                    }
                }
            }

            LOG_debug << prefix << "megaApi[" << nApi << "] [OutShare = " << i
                      << "] Removing outshare...";
            if (unique_ptr<MegaNode> n(megaApi[nApi]->getNodeByHandle(os->getNodeHandle())); n)
            {
                RequestTracker rt(megaApi[nApi].get());
                megaApi[nApi]->share(n.get(), os->getUser(), MegaShare::ACCESS_UNKNOWN, &rt);
                if (const auto res = rt.waitForResult(300); res != API_OK)
                {
                    const string errDetails =
                        "Removal of outshare folder (" +
                        string{Base64Str<MegaClient::NODEHANDLE>(n->getHandle())} +
                        +") failed or took more than 5 minutes";
                    localCleanupSuccess = false;
                    printCleanupErrMsg(prefix,
                                       errDetails,
                                       static_cast<unsigned>(nApi),
                                       res,
                                       localCleanupSuccess);
                }
            }
            else
            {
                const string errDetails = "[OutShare = " + std::to_string(i) + "] No node found!!!";
                printCleanupErrMsg(prefix,
                                   errDetails,
                                   static_cast<unsigned>(nApi),
                                   API_EINTERNAL,
                                   true /*localCleanupSuccess*/);
            }
        }

        // Delete Sets and their public links
        unique_ptr<MegaSetList> sets(megaApi[nApi]->getSets());
        for (unsigned i = 0u; i < sets->size(); ++i)
        {
            const MegaSet* s = sets->get(i);
            if (s->isExported())
            {
                if (const auto resDisable = doDisableExportSet(nApi, s->id()); resDisable != API_OK)
                {
                    const string errDetails = "Stop sharing a set (" + std::to_string(i) + ")";
                    localCleanupSuccess = false;
                    printCleanupErrMsg(prefix,
                                       errDetails,
                                       static_cast<unsigned>(nApi),
                                       resDisable,
                                       localCleanupSuccess);
                }
            }

            if (const auto resRemove = doRemoveSet(nApi, s->id()); resRemove != API_OK)
            {
                const string errDetails = "Removing a set (" + std::to_string(i) + ")";
                bool iterationCleanupSuccess{true};
                if (resRemove != API_EACCESS)
                {
                    localCleanupSuccess = iterationCleanupSuccess = false;
                }
                printCleanupErrMsg(prefix,
                                   errDetails,
                                   static_cast<unsigned>(nApi),
                                   resRemove,
                                   iterationCleanupSuccess);
            }
        }
    }

    // finally, double check we got rid of all inshares and outshares
    for (auto nApi = unsigned(megaApi.size()); nApi--;)
    {
        if (megaApi[nApi])
        {
            if (!WaitFor(
                    [this, nApi]()
                    {
                        return unique_ptr<MegaShareList>(megaApi[nApi]->getOutShares())->size() ==
                               0;
                    },
                    20 * 1000))
            {
                localCleanupSuccess = false;
                printCleanupErrMsg(prefix,
                                   "Some outshares were not removed",
                                   static_cast<unsigned>(nApi),
                                   API_EINTERNAL,
                                   localCleanupSuccess);
            }

            if (!WaitFor(
                    [this, nApi]()
                    {
                        return unique_ptr<MegaShareList>(megaApi[nApi]->getPendingOutShares())
                                   ->size() == 0;
                    },
                    20 * 1000))
            {
                localCleanupSuccess = false;
                printCleanupErrMsg(prefix,
                                   "Some pending outshares were not removed",
                                   static_cast<unsigned>(nApi),
                                   API_EINTERNAL,
                                   localCleanupSuccess);
            }

            if (!WaitFor(
                    [this, nApi]()
                    {
                        return unique_ptr<MegaShareList>(megaApi[nApi]->getUnverifiedOutShares())
                                   ->size() == 0;
                    },
                    20 * 1000))
            {
                localCleanupSuccess = false;
                printCleanupErrMsg(prefix,
                                   "Some unverified outshares were not removed",
                                   static_cast<unsigned>(nApi),
                                   API_EINTERNAL,
                                   localCleanupSuccess);
            }

            if (!WaitFor(
                    [this, nApi]()
                    {
                        return unique_ptr<MegaShareList>(megaApi[nApi]->getUnverifiedInShares())
                                   ->size() == 0;
                    },
                    20 * 1000))
            {
                localCleanupSuccess = false;
                printCleanupErrMsg(prefix,
                                   "Some unverified inshares were not removed",
                                   static_cast<unsigned>(nApi),
                                   API_EINTERNAL,
                                   localCleanupSuccess);
            }

            if (!WaitFor(
                    [this, nApi]()
                    {
                        return unique_ptr<MegaShareList>(megaApi[nApi]->getInSharesList())
                                   ->size() == 0;
                    },
                    20 * 1000))
            {
                localCleanupSuccess = false;
                printCleanupErrMsg(prefix,
                                   "Some inshares were not removed",
                                   static_cast<unsigned>(nApi),
                                   API_EINTERNAL,
                                   localCleanupSuccess);
            }
        }
    }

    updateCleanupStatus(localCleanupSuccess);
    LOG_debug << "# " << prefix << (localCleanupSuccess ? ": OK" : ": Finished with errors");
}

void SdkTest::cleanupNodeLinksAllAccounts()
{
    const std::string prefix{"SdkTest::Cleanup(Remove node links)"};
    LOG_debug << "# " << prefix;
    bool localCleanupSuccess{true};
    for (auto nApi = unsigned(megaApi.size()); nApi--;)
    {
        if (!megaApi[nApi] || !megaApi[nApi]->isLoggedIn())
        {
            continue;
        }

        unique_ptr<MegaNodeList> nodeLinks(megaApi[nApi]->getPublicLinks());
        for (int i = 0; i < nodeLinks->size(); ++i)
        {
            if (auto res = doDisableExport(nApi, nodeLinks->get(i)); res != API_OK)
            {
                const string errDetails = "Disabling node public link (" + std::to_string(i) + ")";
                localCleanupSuccess = false;
                printCleanupErrMsg(prefix,
                                   errDetails,
                                   static_cast<unsigned>(i),
                                   res,
                                   localCleanupSuccess);
            }
        }
    }
    updateCleanupStatus(localCleanupSuccess);
    LOG_debug << "# " << prefix << (localCleanupSuccess ? ": OK" : ": Finished with errors");
}

void SdkTest::cleanupNodesAllAccounts()
{
    const std::string prefix{"SdkTest::Cleanup(Remove nodes)"};
    LOG_debug << "# " << prefix;
    bool localCleanupSuccess{true};
    for (auto nApi = unsigned(megaApi.size()); nApi--;)
    {
        if (!megaApi[nApi] || !megaApi[nApi]->isLoggedIn())
        {
            continue;
        }

        // Remove nodes in Cloud & Rubbish
        purgeTree(nApi, std::unique_ptr<MegaNode>{megaApi[nApi]->getRootNode()}.get(), false);
        purgeTree(nApi, std::unique_ptr<MegaNode>{megaApi[nApi]->getRubbishNode()}.get(), false);
#ifdef ENABLE_SYNC
        purgeVaultTree(nApi, std::unique_ptr<MegaNode>{megaApi[nApi]->getVaultNode()}.get());
#endif
    }

    for (auto nApi = unsigned(megaApi.size()); nApi--;)
    {
        if (megaApi[nApi] && megaApi[nApi]->isLoggedIn())
        {
            // Some tests finish logged in but without call to fetch nodes root nodes are undefined
            // yet
            uint64_t nodesInRoot = 0;
            if (std::unique_ptr<MegaNode> rootNode(megaApi[nApi]->getRootNode()); rootNode)
            {
                if (auto res = synchronousFolderInfo(nApi, rootNode.get());
                    res != MegaError::API_OK)
                {
                    const string errDetails = "Cannot get Folder Info for rootnode";
                    localCleanupSuccess = false;
                    printCleanupErrMsg(prefix,
                                       errDetails,
                                       static_cast<unsigned>(nApi),
                                       res,
                                       localCleanupSuccess);
                }
                else
                {
                    nodesInRoot = static_cast<uint64_t>(mApi[nApi].mFolderInfo->getNumFiles() +
                                                        mApi[nApi].mFolderInfo->getNumFolders() +
                                                        mApi[nApi].mFolderInfo->getNumVersions());
                }
            }

            uint64_t nodesInRubbishBin = 0;
            if (std::unique_ptr<MegaNode> rubbishbinNode(megaApi[nApi]->getRubbishNode());
                rubbishbinNode)
            {
                if (auto res = synchronousFolderInfo(nApi, rubbishbinNode.get());
                    res != MegaError::API_OK)
                {
                    const string errDetails = "Cannot get Folder Info for rubbis bin";
                    localCleanupSuccess = false;
                    printCleanupErrMsg(prefix,
                                       errDetails,
                                       static_cast<unsigned>(nApi),
                                       res,
                                       localCleanupSuccess);
                }
                else
                {
                    nodesInRubbishBin =
                        static_cast<uint64_t>(mApi[nApi].mFolderInfo->getNumFiles() +
                                              mApi[nApi].mFolderInfo->getNumFolders() +
                                              mApi[nApi].mFolderInfo->getNumVersions());
                }
            }

            uint64_t nodesInVault = 0;
            if (std::unique_ptr<MegaNode> vaultNode(megaApi[nApi]->getVaultNode()); vaultNode)
            {
                if (auto res = synchronousFolderInfo(nApi, vaultNode.get()); res != API_OK)
                {
                    const string errDetails = "Cannot get Folder Info for vault";
                    localCleanupSuccess = false;
                    printCleanupErrMsg(prefix,
                                       errDetails,
                                       static_cast<unsigned>(nApi),
                                       res,
                                       localCleanupSuccess);
                }
                else
                {
                    nodesInVault = static_cast<uint64_t>(mApi[nApi].mFolderInfo->getNumFiles() +
                                                         mApi[nApi].mFolderInfo->getNumFolders() +
                                                         mApi[nApi].mFolderInfo->getNumVersions());
                }
            }

            if (nodesInRoot > 0 || nodesInRubbishBin > 0 || nodesInVault > 0)
            {
                LOG_warn << "Clean up for instance " << nApi
                         << " hasn't finished properly. Nodes at root node: " << nodesInRoot
                         << "  Nodes at rubbish bin: " << nodesInRubbishBin
                         << "  Nodes at vault: " << nodesInVault;
            }
        }
    }

    updateCleanupStatus(localCleanupSuccess);
    LOG_debug << "# " << prefix << (localCleanupSuccess ? ": OK" : ": Finished with errors");
}

void SdkTest::cleanupContactRequestsAllAccounts()
{
    const std::string prefix{"SdkTest::Cleanup(RemoveContactRequests)"};
    LOG_debug << "# " << prefix;
    bool localCleanupSuccess{true};
    for (auto nApi = unsigned(megaApi.size()); nApi--;)
    {
        if (!megaApi[nApi] || !megaApi[nApi]->isLoggedIn())
        {
            continue;
        }

        std::unique_ptr<MegaContactRequestList> crl{megaApi[nApi]->getOutgoingContactRequests()};
        for (int i = 0; i < crl->size(); i++)
        {
            const MegaContactRequest* cr = crl->get(i);
            if (const auto targetEmail = cr->getTargetEmail();
                !targetEmail || !std::strlen(targetEmail))
            {
                continue;
            }

            if (const auto resOut =
                    synchronousInviteContact(nApi,
                                             cr->getTargetEmail(),
                                             "Test cleanup removing outgoing contact request",
                                             MegaContactRequest::INVITE_ACTION_DELETE);
                resOut != API_OK)
            {
                string errDetails;
                bool iterationCleanupSuccess{true};
                if (resOut == API_EARGS)
                {
                    errDetails = "No pending outgoing contact request exists for user (" +
                                 std::string(cr->getTargetEmail() ? cr->getTargetEmail() : "") +
                                 ")";
                }
                else
                {
                    errDetails = "Error removing outgoing contact request (" +
                                 std::string(cr->getTargetEmail() ? cr->getTargetEmail() : "") +
                                 ")";
                    localCleanupSuccess = iterationCleanupSuccess = false;
                }
                printCleanupErrMsg(prefix,
                                   errDetails,
                                   static_cast<unsigned>(nApi),
                                   resOut,
                                   iterationCleanupSuccess);
            }
        }

        crl.reset(megaApi[nApi]->getIncomingContactRequests());
        for (int i = 0; i < crl->size(); i++)
        {
            const MegaContactRequest* cr = crl->get(i);
            if (const auto sourceEmail = cr->getTargetEmail();
                !sourceEmail || !std::strlen(sourceEmail))
            {
                continue;
            }

            if (const auto resIn =
                    synchronousReplyContactRequest(nApi, cr, MegaContactRequest::REPLY_ACTION_DENY);
                resIn != API_OK)
            {
                string errDetails;
                bool iterationCleanupSuccess{true};
                if (resIn == API_EARGS)
                {
                    errDetails = "No pending incoming contact request exists for user (" +
                                 std::string(cr->getSourceEmail() ? cr->getSourceEmail() : "") +
                                 ")";
                }
                else
                {
                    errDetails = "Error removing incoming contact request (" +
                                 std::string(cr->getSourceEmail() ? cr->getSourceEmail() : "") +
                                 ")";
                    localCleanupSuccess = iterationCleanupSuccess = false;
                }

                printCleanupErrMsg(prefix,
                                   errDetails,
                                   static_cast<unsigned>(nApi),
                                   resIn,
                                   iterationCleanupSuccess);
            }
        }
    }

    updateCleanupStatus(localCleanupSuccess);
    LOG_debug << "# " << prefix << (localCleanupSuccess ? ": OK" : ": Finished with errors");
}

void SdkTest::cleanupLocalFiles()
{
    const std::string prefix{"SdkTest::Cleanup(RemoveLocalFiles)"};
    LOG_debug << "# " << prefix;
    deleteFile(UPFILE);
    deleteFile(DOWNFILE);
    deleteFile(PUBLICFILE);
    deleteFile(AVATARDST);
    LOG_debug << "# " << prefix << ": Finished]";
}

#ifdef ENABLE_SYNC
void SdkTest::cleanupSyncsAllAccounts()
{
    const std::string prefix{"SdkTest::Cleanup(RemoveSyncs)"};
    LOG_debug << "# " << prefix;
    bool localCleanupSuccess{true};
    for (auto nApi = unsigned(megaApi.size()); nApi--;)
    {
        auto& m = megaApi[nApi];
        if (!m || !m->isLoggedIn())
        {
            continue;
        }

        auto syncs = unique_ptr<MegaSyncList>(m->getSyncs());
        for (int i = syncs->size(); i--;)
        {
            std::unique_ptr<RequestTracker> syncTracker(
                std::unique_ptr<RequestTracker>(new RequestTracker(m.get())));
            m->removeSync(syncs->get(i)->getBackupId(), syncTracker.get());

            if (const auto syncRemoveResult = syncTracker->waitForResult();
                syncRemoveResult != API_OK)
            {
                const string errDetails =
                    "Failed to remove sync (" +
                    string{Base64Str<MegaClient::BACKUPHANDLE>(syncs->get(i)->getBackupId())} + ")";
                localCleanupSuccess = false;
                printCleanupErrMsg(prefix,
                                   errDetails,
                                   static_cast<unsigned>(nApi),
                                   syncRemoveResult,
                                   localCleanupSuccess);
            }
        }
    }
    updateCleanupStatus(localCleanupSuccess);
    LOG_debug << "# " << prefix << (localCleanupSuccess ? ": OK" : ": Finished with errors");
}

void SdkTest::purgeVaultTree(unsigned int apiIndex, MegaNode* vault)
{
    std::unique_ptr<MegaNodeList> vc{megaApi[apiIndex]->getChildren(vault)};
    EXPECT_LE(vc->size(), MAX_VAULT_CHILDREN)
        << "purgeVaultTree: Vault node contains more than " << MAX_VAULT_CHILDREN << " children";

    const auto getVaultNodeHandle = [this, &apiIndex](const int type) -> MegaHandle
    {
        RequestTracker rt{megaApi[apiIndex].get()};
        megaApi[apiIndex]->getUserAttribute(type, &rt);
        return rt.waitForResult() == API_OK ? rt.request->getNodeHandle() : UNDEF;
    };

    MegaHandle hBackups = getVaultNodeHandle(MegaApi::USER_ATTR_MY_BACKUPS_FOLDER);
    if (auto myBackups = std::unique_ptr<MegaNode>{megaApi[apiIndex]->getNodeByHandle(hBackups)};
        myBackups)
    {
        std::unique_ptr<MegaNodeList> devices{megaApi[apiIndex]->getChildren(myBackups.get())};
        for (int i = 0; i < devices->size(); ++i)
        {
            std::unique_ptr<MegaNodeList> backupRoots{
                megaApi[apiIndex]->getChildren(devices->get(i))};
            for (int j = 0; j < backupRoots->size(); ++j)
            {
                RequestTracker rt(megaApi[apiIndex].get());
                const auto backup = backupRoots->get(j);
                megaApi[apiIndex]->moveOrRemoveDeconfiguredBackupNodes(backup->getHandle(),
                                                                       INVALID_HANDLE,
                                                                       &rt);
                EXPECT_EQ(rt.waitForResult(), API_OK)
                    << "purgeVaultTree: Could not remove Backup, " << backup->getName() << "("
                    << Base64Str<MegaClient::NODEHANDLE>(backup->getHandle()) << ")";
            }
        }
    }

    // Get password manager base with user attribute instead of MegaApi::getPasswordManagerBase to
    // avoid create password manager base if it doesn't exist
    MegaHandle pwdBaseHandle = getVaultNodeHandle(MegaApi::USER_ATTR_PWM_BASE);
    if (auto passwordManagerBase =
            std::unique_ptr<MegaNode>{megaApi[apiIndex]->getNodeByHandle(pwdBaseHandle)};
        passwordManagerBase)
    {
        purgeTree(apiIndex, passwordManagerBase.get());
    }
}
#endif

bool SdkTest::waitForResponse(bool *responseReceived, unsigned int timeout)
{
    return waitForEvent([responseReceived]()
    {
        return *responseReceived;
    }, timeout);
}

bool SdkTest::waitForEvent(std::function<bool()> method, unsigned int timeout)
{
    timeout *= 1000000; // convert to micro-seconds
    unsigned int tWaited = 0;    // microseconds
    bool connRetried = false;
    while(!method())
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
    if (!result) mApi[apiIndex].lastError = LOCAL_ETIMEOUT; // local timeout
    if (!result) mApi[apiIndex].lastTransferError = LOCAL_ETIMEOUT; // local timeout    TODO: switch all transfer code to use lastTransferError .  Some still uses lastError
    return result;
}

bool SdkTest::synchronousRequest(unsigned apiIndex, int type, std::function<void()> f, unsigned int timeout)
{
    auto& flag = mApi[apiIndex].requestFlags[type];
    flag = false;
    f();
    auto result = waitForResponse(&flag, timeout);
    EXPECT_TRUE(result) << "Request (type " << type << ") failed after " << timeout << " seconds";
    if (!result) mApi[apiIndex].lastError = LOCAL_ETIMEOUT;
    return result;
}

void SdkTest::onNodesUpdateCheck(size_t apiIndex, MegaHandle target, MegaNodeList* nodes, int change, bool& flag)
{
    // if change == -1 this method just checks if we have received onNodesUpdate for the node specified in target
    // For CHANGE_TYPE_NEW the target is invalid handle because the handle is yet unkown
    ASSERT_TRUE(nodes && mApi.size() > apiIndex && (target != INVALID_HANDLE
            || (target == INVALID_HANDLE && change == MegaNode::CHANGE_TYPE_NEW)));
    for (int i = 0; i < nodes->size(); i++)
    {
        MegaNode* n = nodes->get(i);
        if ((n->getHandle() == target &&
             (n->hasChanged(static_cast<uint64_t>(change)) || change == -1)) ||
            (target == INVALID_HANDLE && change == MegaNode::CHANGE_TYPE_NEW &&
             n->hasChanged(static_cast<uint64_t>(change))))
        {
            flag = true;
        }
    }
};

bool SdkTest::createFile(string filename, bool largeFile, string content)
{
    // Convenience.
    constexpr auto KiB = 1024ul;
    constexpr auto MiB = 1024ul * KiB;

    auto limit = 2000ul;

    // Caller wants to generate a large file.
    if (largeFile)
        limit = MiB + static_cast<unsigned long>(rand()) % MiB;

    std::string temp;

    temp.reserve(content.size() * limit);

    // Generate file content.
    while (limit--)
        temp.append(content);

    // Write the file to disk.
    try
    {
        sdk_test::createFile(fs::u8path(filename), temp);
        return true;
    }
    catch (const std::runtime_error& err)
    {
        LOG_err << err.what();
        return false;
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
    fs::path p = fs::u8path(filename);
    std::error_code ignoredEc;
    fs::remove(p, ignoredEc);
}

void SdkTest::deleteFolder(string foldername)
{
    fs::path p = fs::u8path(foldername);
    std::error_code ignoredEc;
    fs::remove_all(p, ignoredEc);
}

void SdkTest::fetchNodesForAccountsSequentially(const unsigned howMany)
{
    for (unsigned index = 0; index < howMany; ++index)
    {
        out() << "Fetching nodes for account " << index;
        auto tracker = asyncRequestFetchnodes(index);
        ASSERT_EQ(API_OK, tracker->waitForResult())
            << " Failed to fetchnodes for account " << index;
        ASSERT_EQ(MegaError::API_OK, synchronousDoUpgradeSecurity(index));
        LOG_debug << "fetchNodesForAccountsSequentially: Catching up with API with account index("
                  << index << ")";
        ASSERT_EQ(API_OK, synchronousCatchup(index)) << "Failed to catchup for account " << index;
    }
}

void SdkTest::getAccountsForTest(const unsigned howMany,
                                 const bool fetchNodes,
                                 const int clientType)
{
    const std::string prefix{"SdkTest::getAccountsForTest()"};
    const auto maxAccounts = getEnvVarAccounts().size();
    EXPECT_TRUE(howMany > 0) << prefix << "invalid number of test account to setup " << howMany
                             << " is < 0";
    EXPECT_TRUE(howMany <= maxAccounts)
        << prefix << "too many test accounts requested " << howMany << " is > " << maxAccounts;
    megaApi.resize(howMany);
    mApi.resize(howMany);
    out() << "Test setting up for " << howMany << " accounts ";

    for (unsigned index = 0; index < howMany; ++index)
    {
        const auto [email, pass] = getEnvVarAccounts().getVarValues(index);
        ASSERT_FALSE(email.empty() || pass.empty());

        static const bool checkCredentials = true; // default value
        configureTestInstance(index, email, pass, checkCredentials, clientType);

        std::unique_ptr<RequestTracker> tracker;
        if (!gResumeSessions || gSessionIDs[index].empty() || gSessionIDs[index] == "invalid")
        {
            out() << "Logging into account #" << index << ": " << mApi[index].email;
            tracker = asyncRequestLogin(index, mApi[index].email.c_str(), mApi[index].pwd.c_str());
        }
        else
        {
            out() << "Resuming session for account #" << index;
            tracker = asyncRequestFastLogin(index, gSessionIDs[index].c_str());
        }

        auto loginResult = tracker->waitForResult();
        ASSERT_EQ(API_OK, loginResult)
            << prefix << " Failed to establish a login/session for account #" << index << ": "
            << mApi[index].email << ": " << MegaError::getErrorString(loginResult);

        gSessionIDs[index] = "invalid"; // default
        if (gResumeSessions && megaApi[index]->isLoggedIn() == FULLACCOUNT)
        {
            if (auto p = unique_ptr<char[]>(megaApi[index]->dumpSession()))
            {
                gSessionIDs[index] = p.get();
            }
        }
    }

    if (fetchNodes)
    {
        ASSERT_NO_FATAL_FAILURE(fetchNodesForAccountsSequentially(howMany));
    }

    for (unsigned index = 0; index < howMany; ++index)
    {
        auto rt = std::make_unique<RequestTracker>(megaApi[index].get());
        megaApi[index]->getUserAttribute(37 /*ATTR_KEYS*/, rt.get());
        rt->waitForResult();
        const std::string b64Value{rt->request->getText()};
        const std::string binValue = Base64::atob(b64Value);
        if (binValue.size() >
            MAX_USER_VAR_SIZE - 512) // limit almost exceeded, tests will start failing soon
        {
            out() << "Account " << std::unique_ptr<char[]>{megaApi[index]->getMyEmail()}.get()
                  << " has a ^!keys of " << binValue.size() << " bytes";
            out() << "Please, DevOps, park this account";
            ASSERT_FALSE(true);
        }
    }

    // In case the last test exited without cleaning up (eg, debugging etc)
    Cleanup();
    out() << "Test setup done, test starts";
}

void SdkTest::configureTestInstance(unsigned index,
                                    const string& email,
                                    const string& pass,
                                    bool checkCredentials,
                                    const int clientType)
{
    ASSERT_GT(mApi.size(), index) << "Invalid mApi size";
    ASSERT_GT(megaApi.size(), index) << "Invalid megaApi size";

    if(checkCredentials)
    {
        mApi[index].email = email;
        mApi[index].pwd = pass;

        const auto& [emailVarName, passVarName] = getEnvVarAccounts().getVarNames(index);
        ASSERT_FALSE(mApi[index].email.empty()) << "Set test account " << index << " username at the environment variable $" << emailVarName;
        ASSERT_FALSE(mApi[index].pwd.empty()) << "Set test account " << index << " password at the environment variable $" << passVarName;
    }

    megaApi[index] = newMegaApi(APP_KEY.c_str(),
                                megaApiCacheFolder(static_cast<int>(index)).c_str(),
                                USER_AGENT.c_str(),
                                unsigned(THREADS_PER_MEGACLIENT),
                                clientType);
    mApi[index].megaApi = megaApi[index].get();

    // helps with restoring logging after tests that fiddle with log level
    mApi[index].megaApi->setLogLevel(MegaApi::LOG_LEVEL_MAX);

    megaApi[index]->setLoggingName(to_string(index).c_str());
    megaApi[index]->addListener(this);    // TODO: really should be per api
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

void SdkTest::inviteTestAccount(const unsigned invitorIndex, const unsigned inviteIndex, const string& message)
{
    //--- Add account as contact ---
    mApi[inviteIndex].contactRequestUpdated = false;

    std::unique_ptr<MegaUser> contact(mApi[invitorIndex].megaApi->getContact(mApi[inviteIndex].email.c_str()));
    if (contact)
    {
        if (contact->getVisibility() == MegaUser::VISIBILITY_VISIBLE)
        {
            LOG_warn << mApi[inviteIndex].email.c_str() << " is inviting " << mApi[inviteIndex].email.c_str() << " but they are already contacts";
        }
        else if (contact->getVisibility() == MegaUser::VISIBILITY_HIDDEN)
        {
            LOG_info << mApi[inviteIndex].email.c_str() << " is inviting " << mApi[inviteIndex].email.c_str() << " They were contacts in the past";
        }
    }

    // Watcher for the new contact visibility
    bool contactRightVisibility = false;
    auto visibilityCheck = std::make_shared<std::function<void()>>(
        [this, &invitorIndex, &inviteIndex, &contactRightVisibility]()
        {
            std::unique_ptr<MegaUser> contact(
                mApi[invitorIndex].megaApi->getContact(mApi[inviteIndex].email.c_str()));
            contactRightVisibility =
                contact && contact->getVisibility() == MegaUser::VISIBILITY_VISIBLE;
        });
    MegaHandle invitedUserHandler = mApi[inviteIndex].megaApi->getMyUserHandleBinary();
    mApi[invitorIndex].customCallbackCheck[invitedUserHandler] = visibilityCheck;

    ASSERT_NO_FATAL_FAILURE(inviteContact(invitorIndex, mApi[inviteIndex].email, message, MegaContactRequest::INVITE_ACTION_ADD));
    ASSERT_TRUE(waitForResponse(&mApi[inviteIndex].contactRequestUpdated))   // at the target side (auxiliar account)
            << "Contact request creation not received after " << maxTimeout << " seconds";
    ASSERT_NO_FATAL_FAILURE(getContactRequest(inviteIndex, false));

    mApi[invitorIndex].contactRequestUpdated = mApi[inviteIndex].contactRequestUpdated = false;
    ASSERT_NO_FATAL_FAILURE(replyContact(mApi[inviteIndex].cr.get(), MegaContactRequest::REPLY_ACTION_ACCEPT));
    ASSERT_TRUE(waitForResponse(&mApi[inviteIndex].contactRequestUpdated))   // at the target side (auxiliar account)
            << "Contact request creation not received after " << maxTimeout << " seconds";
    ASSERT_TRUE(waitForResponse(&mApi[invitorIndex].contactRequestUpdated))   // at the source side (main account)
            << "Contact request creation not received after " << maxTimeout << " seconds";
    mApi[inviteIndex].cr.reset();

    bool hasExpectedVisibility = waitForResponse(&contactRightVisibility);
    if (!hasExpectedVisibility)
    {
        std::unique_ptr<MegaUser>
            contact(mApi[invitorIndex].megaApi->getContact(mApi[inviteIndex].email.c_str()));
        ASSERT_TRUE(contact) << "Invalid contact after " << maxTimeout << " seconds";
        ASSERT_EQ(contact->getVisibility(), MegaUser::VISIBILITY_VISIBLE)
            << "Invalid contact visibility after " << maxTimeout << " seconds";
        ASSERT_TRUE(hasExpectedVisibility) << "The contact has the correct visibility but the timeout of " << maxTimeout << " seconds was exceeded";
    }
}

void SdkTest::inviteContact(const unsigned apiIndex, const string& email, const string& message, const int action)
{
    ASSERT_EQ(API_OK, synchronousInviteContact(apiIndex, email.c_str(), message.c_str(), action)) << "Contact invitation failed";
}

void SdkTest::replyContact(MegaContactRequest* cr, int action, const unsigned apiIndex)
{
    ASSERT_EQ(API_OK, synchronousReplyContactRequest(apiIndex, cr, action))
        << "Contact reply failed";
}

int SdkTest::removeContact(unsigned apiIndex, string email)
{
    unique_ptr<MegaUser> u(megaApi[apiIndex]->getContact(email.c_str()));

    if (!u)
    {
        out() << "Trying to remove user " << email << " from contacts for megaapi " << apiIndex << " but the User does not exist";
        return API_EINTERNAL;
    }

    if (u->getVisibility() != MegaUser::VISIBILITY_VISIBLE)
    {
        out() << "Contact " << email << " was already non-visible, not sending any command to API for megaapi " << apiIndex << ".  visibility: " << u->getVisibility();
        return API_EINTERNAL;
    }

    auto result = synchronousRemoveContact(apiIndex, u.get());

    if (result == API_EEXIST)
    {
        LOG_warn << "Contact " << email << " was already removed in api " << apiIndex;
        result = API_OK;
    }

    EXPECT_EQ(API_OK, result) << "Contact deletion of " << email << " failed on api " << apiIndex;
    return result;
}

void SdkTest::verifyCredentials(unsigned apiIndex, string email)
{
    unique_ptr<MegaUser> usr(megaApi[apiIndex]->getContact(email.c_str()));
    ASSERT_NE(nullptr, usr.get()) << "User " << email << " not found at apiIndex " << apiIndex;
    ASSERT_EQ(MegaError::API_OK, synchronousVerifyCredentials(apiIndex, usr.get()));
}

void SdkTest::resetCredentials(unsigned apiIndex, string email)
{
    unique_ptr<MegaUser> usr(megaApi[apiIndex]->getContact(email.c_str()));
    ASSERT_NE(nullptr, usr.get()) << "User " << email << " not found at apiIndex " << apiIndex;
    ASSERT_EQ(MegaError::API_OK, synchronousResetCredentials(apiIndex, usr.get()));
}

bool SdkTest::areCredentialsVerified(unsigned apiIndex, string email)
{
    unique_ptr<MegaUser> usr(megaApi[apiIndex]->getContact(email.c_str()));
    EXPECT_NE(nullptr, usr.get()) << "User " << email << " not found at apiIndex " << apiIndex;
    return megaApi[apiIndex]->areCredentialsVerified(usr.get());
}

#ifdef ENABLE_CHAT
void SdkTest::createChatScheduledMeeting(const unsigned apiIndex, MegaHandle& chatid)
{
    struct SchedMeetingData
    {
        MegaHandle chatId = INVALID_HANDLE, schedId = INVALID_HANDLE;
        std::string timeZone, title, description;
        MegaTimeStamp startDate, endDate, overrides, newStartDate, newEndDate;
        bool cancelled, newCancelled;
        std::shared_ptr<MegaScheduledFlags> flags;
        std::shared_ptr<MegaScheduledRules> rules;
    } smd;

    std::unique_ptr<MegaUser> contact(mApi[0].megaApi->getContact(mApi[1].email.c_str()));
    if (!contact || contact->getVisibility() != MegaUser::VISIBILITY_VISIBLE)
    {
        inviteTestAccount(0, 1, "Hi contact. This is a test message");
    }

    std::unique_ptr<MegaUser> myUser(megaApi[apiIndex + 1]->getMyUser());
    ASSERT_TRUE(myUser) << "Cannot retrieve my own user";
    MegaHandle secondaryAccountHandle = myUser->getHandle();
    MegaHandle auxChatid = UNDEF;
    for (const auto &it: mApi[apiIndex].chats)
    {
        if (!it.second->isGroup()
            || it.second->getOwnPrivilege() != MegaTextChatPeerList::PRIV_MODERATOR
            || !it.second->getPeerList())
        {
            continue;
        }

        const auto peerList = it.second->getPeerList();
        for (int i = 0; i < peerList->size(); ++i)
        {
            if (peerList->getPeerHandle(i) == secondaryAccountHandle)
            {
                auxChatid = it.first;
                break;
            }
        }
    }

    if (auxChatid == UNDEF) // create chatroom with moderator privileges
    {
        mApi[apiIndex].chatUpdated = false;
        std::unique_ptr<MegaTextChatPeerList> peers(MegaTextChatPeerList::createInstance());
        peers->addPeer(megaApi[apiIndex + 1]->getMyUserHandleBinary(), PRIV_STANDARD);

        ASSERT_NO_FATAL_FAILURE(createChat(true, peers.get()));
        ASSERT_TRUE(waitForResponse(&mApi[apiIndex].requestFlags[MegaRequest::TYPE_CHAT_CREATE])) << "Cannot create a new chat";
        ASSERT_EQ(API_OK, mApi[apiIndex].lastError) << "Chat creation failed (error: " << mApi[apiIndex].lastError << ")";
        ASSERT_TRUE(waitForResponse(&mApi[apiIndex + 1].chatUpdated))   // at the target side (auxiliar account)
                << "Chat update not received after " << maxTimeout << " seconds";

        auxChatid = mApi[apiIndex].chatid;   // set at onRequestFinish() of chat creation request
    }

    // create MegaScheduledFlags
    std::shared_ptr<MegaScheduledFlags> flags(MegaScheduledFlags::createInstance());
    flags->importFlagsValue(1);

    // create MegaScheduledRules
    std::shared_ptr<::mega::MegaIntegerList> byWeekDay(::mega::MegaIntegerList::createInstance());
    byWeekDay->add(1); byWeekDay->add(3); byWeekDay->add(5);
    std::shared_ptr<MegaScheduledRules> rules(MegaScheduledRules::createInstance(MegaScheduledRules::FREQ_WEEKLY,
                                                                                 MegaScheduledRules::INTERVAL_INVALID,
                                                                                 MEGA_INVALID_TIMESTAMP,
                                                                                 byWeekDay.get(), nullptr, nullptr));

    smd.startDate = m_time();
    smd.endDate = m_time() + 3600;
    smd.title = "ScheduledMeeting_" + std::to_string(1);
    smd.description = "Description" + smd.title;
    smd.timeZone = "Europe/Madrid";
    smd.flags = flags;
    smd.rules = rules;

    std::unique_ptr<MegaScheduledMeeting> sm(MegaScheduledMeeting::createInstance(auxChatid, UNDEF /*schedId*/, UNDEF /*parentSchedId*/,
                                                                                  megaApi[apiIndex]->getMyUserHandleBinary() /*organizerUserId*/, false /*cancelled*/, "Europe/Madrid",
                                                                                  smd.startDate, smd.endDate, smd.title.c_str(), smd.description.c_str(),
                                                                                  nullptr /*attributes*/, MEGA_INVALID_TIMESTAMP /*overrides*/,
                                                                                  flags.get(), rules.get()));
    mApi[apiIndex].schedUpdated = false;
    mApi[apiIndex].schedId = UNDEF;
    megaApi[apiIndex]->createOrUpdateScheduledMeeting(sm.get(), nullptr/*chatTitle*/);
    ASSERT_TRUE(waitForResponse(&mApi[apiIndex].requestFlags[MegaRequest::TYPE_ADD_UPDATE_SCHEDULED_MEETING]))
            << "Cannot create a new scheduled meeting";

    ASSERT_EQ(API_OK, mApi[apiIndex].lastError) << "Scheduled meeting creation failed (error: " << mApi[apiIndex].lastError << ")";

    ASSERT_TRUE(waitForResponse(&mApi[apiIndex].schedUpdated))   // at the target side (auxiliar account)
            << "Scheduled meeting update not received after " << maxTimeout << " seconds";

    ASSERT_NE(mApi[apiIndex].schedId, UNDEF) << "Scheduled meeting id received is not valid ";
    chatid = auxChatid;
}

void SdkTest::updateScheduledMeeting(const unsigned apiIndex, MegaHandle& chatid)
{
    const auto isValidChat = [](const MegaTextChat* chat) -> bool
    {
        if (!chat) { return false; }

        return chat->isGroup()
            && chat->getOwnPrivilege() == MegaTextChatPeerList::PRIV_MODERATOR
            && chat->getScheduledMeetingList()
            && chat->getScheduledMeetingList()->size();
    };

    const MegaTextChat* chat = nullptr;
    auto it = mApi[apiIndex].chats.find(chatid);
    if (chatid == UNDEF
        || it == mApi[apiIndex].chats.end()
        || !isValidChat(it->second.get()))
    {
        for (const auto& auxit: mApi[apiIndex].chats)
        {
            if (isValidChat(auxit.second.get()))
            {
                chatid = auxit.second->getHandle();
                chat = auxit.second.get();
                break;
            }
        }
    }
    else
    {
        chat = it->second.get();
    }

    ASSERT_NE(chat, nullptr) << "Invalid chat";
    ASSERT_NE(chat->getScheduledMeetingList(), nullptr) << "Chat doesn't have scheduled meetings";
    ASSERT_NE(chat->getScheduledMeetingList()->at(0), nullptr) << "Invalid scheduled meeting";
    const MegaScheduledMeeting* aux =  chat->getScheduledMeetingList()->at(0);
    std::unique_ptr<MegaScheduledRules> rules(aux->rules());
    std::unique_ptr<MegaScheduledFlags> flags(aux->flags());
    std::unique_ptr<MegaScheduledMeeting> sm(
        MegaScheduledMeeting::createInstance(aux->chatid(),
                                             aux->schedId(),
                                             aux->parentSchedId(),
                                             aux->organizerUserid(),
                                             aux->cancelled(),
                                             aux->timezone(),
                                             aux->startDateTime(),
                                             aux->endDateTime(),
                                             (std::string(aux->title()) + "_updated").c_str(),
                                             (std::string(aux->description()) + "_updated").c_str(),
                                             aux->attributes(),
                                             MEGA_INVALID_TIMESTAMP /*overrides*/,
                                             flags.get(),
                                             rules.get()));

    std::unique_ptr<RequestTracker>tracker (new RequestTracker(megaApi[apiIndex].get()));
    megaApi[apiIndex]->createOrUpdateScheduledMeeting(sm.get(), nullptr/*chatTitle*/, tracker.get());
    tracker->waitForResult();
}
#endif

void SdkTest::shareFolder(MegaNode* n, const char* email, int action, unsigned apiIndex)
{
    auto shareFolderErr = synchronousShare(apiIndex, n, email, action);
    if (shareFolderErr == API_EKEY)
    {
        ASSERT_EQ(API_OK, doOpenShareDialog(apiIndex, n)) << "Creating new share key failed. " << "User: " << email << " Action: " << action;
        ASSERT_EQ(API_OK, synchronousShare(apiIndex, n, email, action)) << "Folder sharing failed (share key created!). " << "User: " << email << " Action: " << action;
    }
    else
    {
        ASSERT_EQ(API_OK, shareFolderErr) << "Folder sharing failed. " << "User: " << email << " Action: " << action;
    }
}

string SdkTest::createPublicLink(unsigned apiIndex, MegaNode *n, m_time_t expireDate, int timeout, bool isFreeAccount, bool writable, bool megaHosted)
{
    RequestTracker rt(megaApi[apiIndex].get());

    mApi[apiIndex].megaApi->exportNode(n, expireDate, writable, megaHosted, &rt);

    rt.waitForResult(timeout);

    if (!expireDate || !isFreeAccount)
    {
        EXPECT_EQ(API_OK, rt.result.load()) << "Public link creation failed (error: " << mApi[apiIndex].lastError << ")";
    }
    else
    {
        bool res = API_OK != rt.result && rt.result != LOCAL_ETIMEOUT;
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

    if (auto createfolderResult = tracker.waitForResult(timeout); createfolderResult != API_OK)
    {
        EXPECT_EQ(API_OK, createfolderResult)
            << "API " << apiIndex << ": Failed to create folder " << name;
        return UNDEF;
    }

    return tracker.request->getNodeHandle();
}

#if 0
// SMS verification was deprecated. This function should be removed in the future,
// along with the rest of the code dealing with the deprecated functionality.
void SdkTest::getCountryCallingCodes(const int /*timeout*/)
{
    unsigned int apiIndex = 0;
    ASSERT_EQ(API_OK, synchronousGetCountryCallingCodes(apiIndex, this)) << "Get country calling codes failed";
}
#endif

void SdkTest::getUserAttribute(MegaUser* u, int type, int /*timeout*/, int apiIndex)
{
    mApi[static_cast<size_t>(apiIndex)].requestFlags[MegaRequest::TYPE_GET_ATTR_USER] = false;

    int err;
    if (type == MegaApi::USER_ATTR_AVATAR)
    {
        err = synchronousGetUserAvatar(static_cast<unsigned>(apiIndex), u, AVATARDST.c_str());
    }
    else
    {
        err = synchronousGetUserAttribute(static_cast<unsigned>(apiIndex), u, type);
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

    // encrypt file contents with the file key and get URL suffix
    std::unique_ptr<char[]> suffix(req->encryptFile(filename, 0, &fileSize, fileEncrypted, false));
    ASSERT_NE(nullptr, suffix) << "Got NULL suffix after encryption";

    std::unique_ptr<char[]> fingerprint(megaApi[apiIndex]->getFingerprint(filename));

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

    err = synchronousMediaUploadComplete(apiIndex, req.get(), fileOutput, rootnode.get(), fingerprint.get(), nullptr, base64UploadToken.get(), nullptr);

    ASSERT_EQ(API_OK, err) << "Cannot complete media upload (error: " << err << ")";
}

void SdkTest::synchronousMediaUploadIncomplete(unsigned int apiIndex,
                                               int64_t fileSize,
                                               const char* filename,
                                               const char* fileEncrypted,
                                               std::string& fingerprint,
                                               std::string& string64UploadToken,
                                               std::string& string64FileKey)
{
    // Create a "media upload" instance
    std::unique_ptr<MegaBackgroundMediaUploadPrivate> req(
        dynamic_cast<MegaBackgroundMediaUploadPrivate*>(
            MegaBackgroundMediaUpload::createInstance(megaApi[apiIndex].get())));

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

    // generated by FileFingerprint::serializefingerprint() plus MegaNodePrivate::addAppPrefixToFingerprint()
    fingerprint = megaApi[apiIndex]->getFingerprint(fileEncrypted);
    string finalurl(url.get());
    finalurl.append(suffix.get());

    string binaryUploadToken;
    synchronousHttpPOSTFile(finalurl, fileEncrypted, binaryUploadToken);

    ASSERT_GT(binaryUploadToken.size(), 3u)
        << "POST failed, fa server error: " << binaryUploadToken;

    string64UploadToken =
        megaApi[apiIndex]->binaryToBase64(binaryUploadToken.data(), binaryUploadToken.length());

    string64FileKey = megaApi[apiIndex]->binaryToBase64(reinterpret_cast<const char*>(req->filekey),
                                                        FILENODEKEYLENGTH);
}

auto getAccountLevel(MegaApi& client) -> Expected<AccountLevel>
{
    const string prefix{"getAccountLevel"};
    // Try and retrieve the user's account details.
    auto details = getAccountDetails(client);

    // Couldn't get account details.
    if (auto result = ::result(details); result != API_OK)
    {
        LOG_err << prefix << "Unexpected error for account("
                << string{client.getMyEmail() ? client.getMyEmail() : ""}
                << ") trying to getAccountDetails. Err(" << result << ")";
        return result;
    }

    // Latch the user's plan.
    auto plan = value(details)->getProLevel();

    // User has a free account: No need to get features or months.
    if (plan == MegaAccountDetails::ACCOUNT_TYPE_FREE)
        return AccountLevel(0, plan);

    // Try and get pricing information.
    auto pricing = getPricing(client);

    // Couldn't get pricing information.
    if (auto result = ::result(pricing); result != API_OK)
    {
        LOG_err << prefix << "Unexpected error for account("
                << string{client.getMyEmail() ? client.getMyEmail() : ""}
                << ") trying to getPricing. Err(" << result << ")";
        return result;
    }

    // Convenience.
    auto& priceDetails = *value(pricing);

    // Locate the user's plan.
    for (auto i = 0, j = priceDetails.getNumProducts(); i < j; ++i)
    {
        // Found the user's plan.
        if (plan == priceDetails.getProLevel(i))
        {
            // Return plan and its length.
            return AccountLevel(priceDetails.getMonths(i), plan);
        }
    }

    // Couldn't locate the user's plan.
    return API_ENOENT;
}

auto getAccountDetails(MegaApi& client) -> Expected<std::unique_ptr<MegaAccountDetails>>
{
    // So we can wait for the client's result.
    RequestTracker tracker(&client);

    // Ask client for account details.
    client.getAccountDetails(&tracker);

    // Couldn't get the client's account details.
    if (auto result = tracker.waitForResult(); result != API_OK)
        return result;

    // Return account details to caller.
    return makeUniqueFrom(tracker.request->getMegaAccountDetails());
}

auto getPricing(MegaApi& client) -> Expected<std::unique_ptr<MegaPricing>>
{
    // So we can wait for the client's result.
    RequestTracker tracker(&client);

    // Ask client for plan pricing information,
    client.getPricing(&tracker);

    // Couldn't get pricing plans.
    if (auto result = tracker.waitForResult(); result != API_OK)
        return result;

    // Return pricing plans to caller.
    return makeUniqueFrom(tracker.request->getPricing());
}

auto accountLevelRestorer(MegaApi& client) -> ScopedDestructor
{
    // Assume we can't retrieve the account level.
    std::function<void()> destructor = []() {};

    // Try and retrieve the user's current account level.
    auto accountLevel = getAccountLevel(client);

    // Couldn't retrieve account level.
    if (auto result = ::result(accountLevel); result != API_OK)
    {
        // Leave a trail if we couldn't get the account level.
        EXPECT_EQ(result, API_OK) << "Couldn't retrieve account level: " << result;

        // Return destructor to caller.
        return destructor;
    }

    // Build a destructor that will restore the user's account level.
    destructor = [&client, level = value(accountLevel)]()
    {
        // Try and restore the user's account level.
        auto result = setAccountLevel(client, level.plan, level.months, nullptr);
        EXPECT_EQ(result, API_OK) << "Couldn't restore account level: " << result;
    };

    // Return destructor to caller.
    return destructor;
}

ScopedDestructor accountLevelRestorer(std::vector<MegaApiTestPointer>& clients, unsigned int idx)
{
    std::function<void()> destructor = []() {};

    auto accountLevel = getAccountLevel(*clients[idx]);
    if (auto result = ::result(accountLevel); result != API_OK)
    {
        EXPECT_EQ(result, API_OK) << "Couldn't retrieve account " << idx << " level";
        return destructor;
    }

    destructor = [&clients, idx, level = value(accountLevel)]()
    {
        auto result = setAccountLevel(*clients[idx], level.plan, level.months, nullptr);
        EXPECT_EQ(result, API_OK) << "Couldn't restore account " << idx << " level";
    };

    return destructor;
}

auto createDirectory(MegaApi& client, const MegaNode& parent, const std::string& name)
    -> Expected<std::unique_ptr<MegaNode>>
{
    using sdk_test::waitFor;

    RequestTracker tracker(&client);

    client.createFolder(name.c_str(), const_cast<MegaNode*>(&parent), &tracker);

    if (auto result = tracker.waitForResult(); result != API_OK)
    {
        return result;
    }

    MegaNode* directory = nullptr;
    MegaHandle directoryHandle = tracker.request->getNodeHandle();

    waitFor(
        [&]()
        {
            return (directory = client.getNodeByHandle(directoryHandle)) != nullptr;
        },
        std::chrono::milliseconds(defaultTimeoutMs));

    if (!directory)
    {
        return LOCAL_ETIMEOUT;
    }

    return makeUniqueFrom(directory);
}

auto elevateToPro(MegaApi& client) -> Expected<ScopedDestructor>
{
    // Make sure client's plan alterations are temporary.
    auto restorer = accountLevelRestorer(client);

    // Try and elevate client to a pro pricing plan.
    auto result = setAccountLevel(client, MegaAccountDetails::ACCOUNT_TYPE_PROI, 1, nullptr);

    // Couldn't elevate client to a pro pricing plan.
    if (result != API_OK)
        return result;

    // Return restorer to caller.
    return restorer;
}

auto exportNode(MegaApi& client, const MegaNode& node, std::optional<std::int64_t> expirationDate)
    -> Expected<std::string>
{
    RequestTracker tracker(&client);

    client.exportNode(const_cast<MegaNode*>(&node),
                      expirationDate.value_or(-1),
                      false,
                      false,
                      &tracker);

    if (auto result = tracker.waitForResult(); result != API_OK)
    {
        return result;
    }

    return tracker.request->getLink();
}

auto importNode(MegaApi& client, const std::string& link, const MegaNode& parent)
    -> Expected<std::unique_ptr<MegaNode>>
{
    using sdk_test::waitFor;

    RequestTracker tracker(&client);

    client.importFileLink(link.c_str(), const_cast<MegaNode*>(&parent), &tracker);

    if (auto result = tracker.waitForResult(); result != API_OK)
    {
        return result;
    }

    MegaNode* node = nullptr;

    waitFor(
        [&]() -> bool
        {
            return (node = client.getNodeByHandle(tracker.request->getNodeHandle())) != nullptr;
        },
        std::chrono::milliseconds(defaultTimeoutMs));

    if (!node)
    {
        return LOCAL_ETIMEOUT;
    }

    return makeUniqueFrom(node);
}

///////////////////////////__ Tests using SdkTest __//////////////////////////////////
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
    synchronousResumeCreateAccountEphemeralPlusPlus(0, mApi[0].getSid().c_str());
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
    auto accounts = makeScopedValue(getEnvVarAccounts(), EnvVarAccounts{2, {"MEGA_EMAIL", "MEGA_PWD"}});

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
        auto fsa = std::make_unique<FSACCESS_CLASS>();
        auto fa = fsa->newfileaccess();
        ASSERT_TRUE(fa->fopen(LocalPath::fromAbsolutePath(filename1.c_str()), FSLogging::logOnError));
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

    megaApi[0]->log(2, "test postlog", __FILE__, __LINE__);

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
    RequestTracker requestTracker(megaApi[0].get());
    megaApi[0]->setCustomNodeAttribute(n1.get(), "custom3", "value33", &requestTracker);
    // Wait for the last set node attribute request before performing the get.
    ASSERT_EQ(API_OK, requestTracker.waitForResult());

    n1.reset(megaApi[0]->getNodeByHandle(n1->getHandle()));
    ASSERT_STREQ("value13", n1->getCustomAttr("custom1"));
    ASSERT_STREQ("value23", n1->getCustomAttr("custom2"));
    ASSERT_STREQ("value33", n1->getCustomAttr("custom3"));


    // ___ Set invalid coordinates of a node (out of range) ___

    ASSERT_EQ(API_EARGS, synchronousSetNodeCoordinates(0, n1.get(), -1523421.8719987255814, +6349.54)) << "Unexpected error setting invalid node coordinates";


    // ___ Set invalid coordinates of a node (out of range) ___

    ASSERT_EQ(API_EARGS, synchronousSetNodeCoordinates(0, n1.get(), -160.8719987255814, +49.54)) << "Unexpected error setting invalid node coordinates";


    // ___ Set invalid coordinates of a node (out of range) ___

    ASSERT_EQ(API_EARGS, synchronousSetNodeCoordinates(0, n1.get(), MegaNode::INVALID_COORDINATE, +69.54)) << "Unexpected error trying to reset only one coordinate";

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
    std::unique_ptr<MegaNode> n2(megaApi[0]->getNodeByHandle(uploadedNodeHande));
    ASSERT_NE(n2.get(), ((void*)NULL)) << "Cannot initialize second node for scenario (error: " << mApi[0].lastError << ")";

    lat = -5 + -51.8719987255814;
    lon = -5 + +179.54;
    mApi[0].requestFlags[MegaRequest::TYPE_SET_ATTR_NODE] = false;
    megaApi[0]->setUnshareableNodeCoordinates(n2.get(), lat, lon);
    waitForResponse(&mApi[0].requestFlags[MegaRequest::TYPE_SET_ATTR_NODE]);
    ASSERT_EQ(API_OK, mApi[0].lastError) << "Cannot set unshareable node coordinates (error: " << mApi[0].lastError << ")";

    // ___ confirm this user can read them
    std::unique_ptr<MegaNode> selfread(megaApi[0]->getNodeByHandle(n2->getHandle()));
    ASSERT_TRUE(veryclose(lat, selfread->getLatitude())) << "Latitude " << n2->getLatitude() << " value does not match " << lat;
    ASSERT_TRUE(veryclose(lon, selfread->getLongitude())) << "Longitude " << n2->getLongitude() << " value does not match " << lon;

    // ___ get a link to the file node
    string nodelink2 = createPublicLink(0, n2.get(), 0, maxTimeout, mApi[0].accountDetails->getProLevel() == 0);

    // ___ import the link
    importHandle = importPublicLink(1, nodelink2, std::unique_ptr<MegaNode>{megaApi[1]->getRootNode()}.get());
    nimported = std::unique_ptr<MegaNode>{megaApi[1]->getNodeByHandle(importHandle)};
    ASSERT_TRUE(nimported != nullptr);

    // ___ confirm other user cannot read them
    lat = nimported->getLatitude();
    lon = nimported->getLongitude();
    ASSERT_EQ(MegaNode::INVALID_COORDINATE, lat) << "Latitude value does not match";
    ASSERT_EQ(MegaNode::INVALID_COORDINATE, lon) << "Longitude value does not match";

    // exercise all the cases for 'l' command:

    // delete existing link on node
    bool check = false;
    mApi[0].mOnNodesUpdateCompletion = createOnNodesUpdateLambda(n2->getHandle(), MegaNode::CHANGE_TYPE_PUBLIC_LINK, check);
    ASSERT_EQ(API_OK, doDisableExport(0, n2.get()));
    waitForResponse(&check);
    resetOnNodeUpdateCompletionCBs();

    // create on existing node, no link yet
    ASSERT_EQ(API_OK, doExportNode(0, n2.get(), 0, false, false));

    // create on existing node, with link already  (different command response)
    ASSERT_EQ(API_OK, doExportNode(0, n2.get(), 0, false, false));

    // create on non existent node
    ASSERT_EQ(API_EARGS, doExportNode(0, nullptr, 0, false, false));
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
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

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
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

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
    std::unique_ptr<MegaSearchFilter> filterResults(MegaSearchFilter::createInstance());
    filterResults->byName("copy");
    filterResults->byLocationHandle(rootnode->getHandle());
    std::unique_ptr<MegaNodeList> nlist(megaApi[0]->search(filterResults.get()));

    ASSERT_EQ(1, nlist->size());
    EXPECT_EQ(n4->getHandle(), nlist->get(0)->getHandle()) << "Search node by pattern failed";


    // --- Move a node ---
    ASSERT_EQ(API_OK, doMoveNode(0, nullptr, n1, n2)) << "Cannot move node";


    // --- Get parent node ---

    MegaNode *n5;
    n5 = megaApi[0]->getParentNode(n1);

    ASSERT_EQ(n2->getHandle(), n5->getHandle()) << "Wrong parent node";


    // --- Send to Rubbish bin ---
    std::unique_ptr<MegaNode> rubbishNode(megaApi[0]->getRubbishNode());
    ASSERT_EQ(API_OK, doMoveNode(0, nullptr, n2, rubbishNode.get())) << "Cannot move node to Rubbish bin";

    // -- Test node movement to Rubbish bin with a file conatining public link --
    auto sn = createFolder(0, "ShareIt", rootnode);
    ASSERT_NE(sn, UNDEF);
    sdk_test::LocalTempFile fLinkFile("testlink.txt", 1);
    MegaHandle sharedFileHandle = INVALID_HANDLE;
    std::unique_ptr<MegaNode> containerNode(megaApi[0]->getNodeByHandle(sn));

    // Upload a file to a container folder
    ASSERT_EQ(MegaError::API_OK,
              doStartUpload(0,
                            &sharedFileHandle,
                            "testlink.txt",
                            containerNode.get(),
                            "testlink.txt",
                            ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
                            nullptr,
                            false,
                            false,
                            nullptr))
        << "Cannot update the test file";
    ASSERT_NE(sharedFileHandle, INVALID_HANDLE);
    {
        // -- Test public link creation ---
        std::unique_ptr<MegaNode> fileNode(megaApi[0]->getNodeByHandle(sharedFileHandle));
        string publicLink = createPublicLink(0, fileNode.get(), 0, 1, false);
        ASSERT_TRUE(publicLink.length() > 0) << "Failed to crate public link for test file";
        std::unique_ptr<MegaNode> sharedFileNode(megaApi[0]->getNodeByHandle(sharedFileHandle));
        ASSERT_NE(INVALID_HANDLE, sharedFileNode.get()->getPublicHandle())
            << "Failed to crate public link for test file";
    }

    // -- Move the contaner folder (hence the file) to Rubbish bin --
    ASSERT_EQ(API_OK, doMoveNode(0, nullptr, containerNode.get(), rubbishNode.get()))
        << "Cannot move node to Rubbish bin";
    {
        // -- Test if link has been removed after moving to Rubbish bin --
        std::unique_ptr<MegaNode> sharedFileNode(megaApi[0]->getNodeByHandle(sharedFileHandle));
        ASSERT_TRUE((sharedFileNode.get())->getPublicLink() == nullptr)
            << "Failed to remove public link for test file after moving to Rubbish bin";
        ASSERT_EQ(INVALID_HANDLE, sharedFileNode.get()->getPublicHandle())
            << "Failed to remove public link for test file after moving to Rubbish bin";
    }

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
 * @brief TEST_F SdkTestDownloadConflictFolderExistingName
 *
 * This test tries to download a File node into a local folder, that already contains a folder with
 * the same name as downloaded file.
 *
 * Note: We call MegaApi::startDownload with collisionCheck(COLLISION_CHECK_ASSUMEDIFFERENT) and
 * collisionResolution(COLLISION_RESOLUTION_OVERWRITE), so transfer will be retried sometimes by SDK
 * and finally will fail with API_EWRITE.
 */
TEST_F(SdkTest, SdkTestDownloadConflictFolderExistingName)
{
    LOG_info << "___TEST SdkTestDownloadConflictFolderExistingName___";
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));
    LOG_info << cwd();

    fs::path basePath = fs::current_path();
    const std::string itemName{"testItem"};
    std::unique_ptr<MegaNode> rootNode{megaApi[0]->getRootNode()};

    LOG_debug << "#### TEST1: Create Folder in local FS ####";
    sdk_test::LocalTempDir d(basePath / itemName);

    LOG_debug << "#### TEST2: Create File in cloud drive ####";
    const auto newNode =
        sdk_test::uploadFile(megaApi[0].get(),
                             sdk_test::LocalTempFile{basePath / itemName / itemName, 1},
                             rootNode.get());
    ASSERT_TRUE(newNode) << "Cannot create node in Cloud Drive";

    LOG_debug << "#### TEST3: Download file at dir with Folder with same name ####";
    const auto errCode = sdk_test::downloadNode(megaApi[0].get(),
                                                newNode.get(),
                                                basePath / itemName,
                                                180s /*timeout*/,
                                                MegaTransfer::COLLISION_CHECK_ASSUMEDIFFERENT,
                                                MegaTransfer::COLLISION_RESOLUTION_OVERWRITE);

    ASSERT_TRUE(errCode.has_value()) << "test_utils(downloadFile) has returned nullopt";
    ASSERT_EQ(*errCode, API_EWRITE)
        << "test_utils(downloadFile) has returned unexpected errorCode: " << errCode.has_value();
}

/**
 * @brief TEST_F SdkTestTransfers
 *
 * It performs different operations related to transfers in both directions: up and down.
 *
 * - Uploads an empty directory
 * - Starts an upload transfer and cancel it
 * - Starts an upload transfer, pause it, check the unique id, resume it and complete it
 * - Get node by fingerprint
 * - Get size of a node
 * - Download a file
 */
TEST_F(SdkTest, SdkTestTransfers)
{
    LOG_info << "___TEST Transfers___";
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));
    LOG_info << cwd();

    // Make sure our clients are working with pro plans.
    auto accountRestorer = elevateToPro(*megaApi[0]);
    ASSERT_EQ(result(accountRestorer), API_OK);

    // --- Upload an empty folder ---
    auto createAndUploadEmptyFolder = [this](::mega::MegaTransferListener* uploadListener1) -> fs::path
    {
        if (!uploadListener1)                { return fs::path{}; }
        fs::path p = fs::current_path() / "upload_folder_mega_auto_test_sdk";
        if (fs::exists(p) && !fs::remove(p)) { return fs::path{}; }
        if (!fs::create_directory(p))        { return fs::path{}; }

        megaApi[0]->startUpload(p.u8string().c_str(),
                                std::unique_ptr<MegaNode>{megaApi[0]->getRootNode()}.get(),
                                nullptr /*fileName*/, ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
                                nullptr /*appData*/, false   /*isSourceTemporary*/,
                                false   /*startFirst*/, nullptr /*cancelToken*/, uploadListener1);
        return p;
    };
    auto uploadListener1 = std::make_shared<TransferTracker>(megaApi[0].get());
    uploadListener1->selfDeleteOnFinalCallback = uploadListener1;
    auto p = createAndUploadEmptyFolder(uploadListener1.get());
    ASSERT_FALSE(p.empty()) << "Upload empty folder: error creating local empty folder";
    ASSERT_EQ(uploadListener1->waitForResult(), API_OK) << "Upload empty folder: error uploading empty folder";
    ASSERT_NE(uploadListener1->resultNodeHandle, ::mega::INVALID_HANDLE)
        << "Upload empty folder: node handle received in onTransferFinish is invalid";
    EXPECT_TRUE(fs::remove(p)) << "Upload empty folder: error cleaning empty dir resource " << p;

    // --- Cancel a transfer ---
    MegaNode* rootnode = megaApi[0]->getRootNode();
    string filename1 = UPFILE;
    ASSERT_TRUE(createFile(filename1)) << "Couldn't create " << filename1;
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

    // --- Setup a global listener to capture dbid and tag on next transfer ---
    testing::NiceMock<MockTransferListener> mockGlobalListener{megaApi[0].get()};
    std::promise<std::tuple<uint32_t, int>> dbidAndTagOnStart;
    EXPECT_CALL(mockGlobalListener, onTransferStart)
        .WillOnce(
            [&dbidAndTagOnStart](MegaApi*, MegaTransfer* transfer)
            {
                if (transfer)
                    dbidAndTagOnStart.set_value({transfer->getUniqueId(), transfer->getTag()});
                else
                    dbidAndTagOnStart.set_value({0, -1});
            });
    megaApi[0]->addListener(&mockGlobalListener);

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

    // --- Get dbid and tag of first transfer since started listening ---
    auto transferListerResult = dbidAndTagOnStart.get_future();
    ASSERT_EQ(transferListerResult.wait_for(std::chrono::seconds(maxTimeout)),
              std::future_status::ready)
        << "Timeout for the start upload";
    megaApi[0]->removeListener(&mockGlobalListener); // not needed any longer
    const auto [transferUniqueId, transferTag] = transferListerResult.get();
    ASSERT_NE(transferTag, -1) << "Missing transfer param for onTransferStart event";
    std::unique_ptr<MegaTransfer> transferByUniqueId{
        megaApi[0]->getTransferByUniqueId(transferUniqueId)};
    ASSERT_TRUE(transferByUniqueId) << "No transfer found with unique Id " << transferUniqueId;
    EXPECT_EQ(transferTag, transferByUniqueId->getTag())
        << "Retrieved transfer doesn't match expected tag";
    transferByUniqueId.reset(megaApi[0]->getTransferByUniqueId(transferUniqueId + 1));
    EXPECT_FALSE(transferByUniqueId)
        << "This use case doesn't expect any other active or in pause transfers";

    // --- Resume a transfer ---

    mApi[0].requestFlags[MegaRequest::TYPE_PAUSE_TRANSFERS] = false;
    megaApi[0]->pauseTransfers(false, MegaTransfer::TYPE_UPLOAD);
    ASSERT_TRUE( waitForResponse(&mApi[0].requestFlags[MegaRequest::TYPE_PAUSE_TRANSFERS]) )
            << "Resumption of transfers after pause has failed after " << maxTimeout << " seconds";
    EXPECT_EQ(API_OK, mApi[0].lastError) << "Cannot resume transfer (error: " << mApi[0].lastError << ")";
    EXPECT_FALSE(megaApi[0]->areTransfersPaused(MegaTransfer::TYPE_UPLOAD)) << "Upload transfer not resumed";


    // --- Upload a file (part 2) ---

    ASSERT_EQ(API_OK,tt.waitForResult()) << "Cannot upload file (error: " << mApi[0].lastError << ")";

    std::unique_ptr<MegaNode> n1(megaApi[0]->getNodeByHandle(tt.resultNodeHandle));
    bool null_pointer = (n1.get() == NULL);

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
    std::unique_ptr<MegaNode> nodeToCopy1(megaApi[0]->getNodeByPath(("/" + filename1).c_str()));
    ASSERT_EQ(API_OK, doCopyNode(0, nullptr, nodeToCopy1.get(), rootnode, "some_other_name"));

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

    n1.reset(megaApi[0]->getNodeByPath(("/" + filename1).c_str()));

    // --- Get node by fingerprint (needs to be a file, not a folder) ---

    const char* fingerprint = n1->getFingerprint();
    MegaNode* n2 = megaApi[0]->getNodeByFingerprint(fingerprint);

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
                              nullptr  /*cancelToken*/,
                              MegaTransfer::COLLISION_CHECK_FINGERPRINT /*collisionCheck*/,
                              MegaTransfer::COLLISION_RESOLUTION_NEW_WITH_N /* collisionResolution */,
                              false    /* undelete */);

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
                              nullptr  /*cancelToken*/,
                              MegaTransfer::COLLISION_CHECK_FINGERPRINT /*collisionCheck*/,
                              MegaTransfer::COLLISION_RESOLUTION_NEW_WITH_N /* collisionResolution */,
                              false    /* undelete */);

    ASSERT_TRUE( waitForResponse(&mApi[0].transferFlags[MegaTransfer::TYPE_DOWNLOAD], 600) )
            << "Download 0-byte file failed after " << maxTimeout << " seconds";
    ASSERT_EQ(API_OK, mApi[0].lastError) << "Cannot download the file (error: " << mApi[0].lastError << ")";

    MegaNode *n5 = megaApi[0]->getNodeByHandle(n4->getHandle());
    null_pointer = (n5 == NULL);

    ASSERT_FALSE(null_pointer) << "Cannot download node";
    ASSERT_EQ(n4->getHandle(), n5->getHandle()) << "Cannot download node (error: " << mApi[0].lastError << ")";


    delete rootnode;
    delete n2;
    delete n3;
    delete n4;
    delete n5;
}


/**
 * @brief TEST_F SdkTestUndelete
 *
 * Undelete files that have been completely removed and their node no longer exists in the online account
 *
 * - Validate the account - undelete can only work with a PRO account
 * - Upload a file
 * - Unlink the file
 * - Undelete the file
 */
TEST_F(SdkTest, SdkTestUndelete)
{
    LOG_info << "___TEST Undelete___";
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    LOG_info << "# Set " << megaApi[0]->getMyEmail() << " account to Pro I plan";
    auto restorer = elevateToPro(*megaApi[0]);
    ASSERT_EQ(result(restorer), API_OK);

    LOG_info << cwd();


    // --- Upload a file ---
    ASSERT_TRUE(createFile(UPFILE, false)) << "Couldn't create " << UPFILE;
    std::unique_ptr<MegaNode> rootnode(megaApi[0]->getRootNode());
    MegaHandle uploadedNodeHande = INVALID_HANDLE;
    ASSERT_EQ(MegaError::API_OK,
        doStartUpload(0, &uploadedNodeHande, UPFILE.c_str(),
                      rootnode.get(),
                      nullptr /*fileName*/,
                      ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
                      nullptr /*appData*/,
                      false   /*isSourceTemporary*/,
                      false   /*startFirst*/,
                      nullptr /*cancelToken*/))
        << "Cannot upload " << UPFILE;

    std::unique_ptr<MegaNode> fileNode(megaApi[0]->getNodeByHandle(uploadedNodeHande));

    ASSERT_TRUE(fileNode) << "Cannot upload file (error: " << mApi[0].lastError << ")";
    ASSERT_STREQ(UPFILE.c_str(), fileNode->getName()) << "Uploaded file with wrong name";


    // --- Download the file ---
    string fileToDownload = UPFILE + "_download";
    TransferTracker downloadNodeTracker(megaApi[0].get());
    megaApi[0]->startDownload(fileNode.get(),
        fileToDownload.c_str(),
        nullptr  /*customName*/,
        nullptr  /*appData*/,
        false    /*startFirst*/,
        nullptr  /*cancelToken*/,
        MegaTransfer::COLLISION_CHECK_FINGERPRINT /*collisionCheck*/,
        MegaTransfer::COLLISION_RESOLUTION_NEW_WITH_N /* collisionResolution */,
        false    /*undelete*/,
        &downloadNodeTracker);
    EXPECT_EQ(downloadNodeTracker.waitForResult(), API_OK) << "Failed to download n2.";


    // --- Unlink the file ---
    RequestTracker removeNodeTracker(megaApi[0].get());
    megaApi[0]->remove(fileNode.get(), &removeNodeTracker);
    EXPECT_EQ(removeNodeTracker.waitForResult(), API_OK) << "Failed to remove n2.";


    // --- Undelete the file ---
    string fileToUndelete = UPFILE + "_undeleted";
    TransferTracker undeleteNodeTracker(megaApi[0].get());
    megaApi[0]->startDownload(fileNode.get(),
        fileToUndelete.c_str(),
        nullptr  /*customName*/,
        nullptr  /*appData*/,
        false    /*startFirst*/,
        nullptr  /*cancelToken*/,
        MegaTransfer::COLLISION_CHECK_FINGERPRINT /*collisionCheck*/,
        MegaTransfer::COLLISION_RESOLUTION_NEW_WITH_N /* collisionResolution */,
        true     /*undelete*/,
        &undeleteNodeTracker);
    EXPECT_EQ(undeleteNodeTracker.waitForResult(), API_OK) << "Failed to undelete n2.";
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

    ASSERT_TRUE(getFileFromArtifactory("test-data/" + AVATARSRC, AVATARSRC));

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

    string firstname1 = "My firstname1"; // change it twice to make sure we get a change notification (in case it was already the first one)
    string firstname2 = "My firstname2";

    mApi[1].userUpdated = false;
    ASSERT_EQ(API_OK, synchronousSetUserAttribute(0, MegaApi::USER_ATTR_FIRSTNAME, firstname1.c_str()));
    ASSERT_EQ(API_OK, synchronousSetUserAttribute(0, MegaApi::USER_ATTR_FIRSTNAME, firstname2.c_str()));

    // --- Check firstname of a contact

    MegaUser *u = megaApi[0]->getMyUser();

    bool null_pointer = (u == NULL);
    ASSERT_FALSE(null_pointer) << "Cannot find the MegaUser for email: " << mApi[0].email;

    ASSERT_NO_FATAL_FAILURE( getUserAttribute(u, MegaApi::USER_ATTR_FIRSTNAME));
    ASSERT_EQ(firstname2, mApi[1].getAttributeValue()) << "Firstname is wrong";

    delete u;


    // --- Set master key already as exported

    u = megaApi[0]->getMyUser();

    mApi[0].requestFlags[MegaRequest::TYPE_SET_ATTR_USER] = false;
    megaApi[0]->masterKeyExported();
    ASSERT_TRUE( waitForResponse(&mApi[0].requestFlags[MegaRequest::TYPE_SET_ATTR_USER]) );

    ASSERT_NO_FATAL_FAILURE( getUserAttribute(u, MegaApi::USER_ATTR_PWD_REMINDER, maxTimeout, 0));
    string pwdReminder = mApi[0].getAttributeValue();
    size_t offset = pwdReminder.find(':');
    offset = pwdReminder.find(':', offset+1);
    ASSERT_EQ( pwdReminder.at(offset+1), '1' ) << "Password reminder attribute not updated";

    delete u;


    // --- Get language preference

    u = megaApi[0]->getMyUser();

    string langCode = "es";
    ASSERT_EQ(API_OK, synchronousSetUserAttribute(0, MegaApi::USER_ATTR_LANGUAGE, langCode.c_str()));
    ASSERT_NO_FATAL_FAILURE( getUserAttribute(u, MegaApi::USER_ATTR_LANGUAGE, maxTimeout, 0));
    string language = mApi[0].getAttributeValue();
    ASSERT_TRUE(!strcmp(langCode.c_str(), language.c_str())) << "Language code is wrong";

    delete u;


    // --- Load avatar ---

    ASSERT_TRUE(fileexists(AVATARSRC)) <<  "File " +AVATARSRC+ " is needed in folder " << cwd();

    mApi[1].userUpdated = false;
    ASSERT_EQ(API_OK,synchronousSetAvatar(0, nullptr));
    ASSERT_EQ(API_OK,synchronousSetAvatar(0, AVATARSRC.c_str()));
    ASSERT_TRUE( waitForResponse(&mApi[1].userUpdated) )   // at the target side (auxiliar account)
            << "User attribute update not received after " << maxTimeout << " seconds";


    // --- Get avatar of a contact ---

    u = megaApi[0]->getMyUser();

    null_pointer = (u == NULL);
    ASSERT_FALSE(null_pointer) << "Cannot find the MegaUser for email: " << mApi[0].email;

    mApi[1].setAttributeValue("");

    ASSERT_NO_FATAL_FAILURE( getUserAttribute(u, MegaApi::USER_ATTR_AVATAR));
    ASSERT_EQ( "Avatar changed", mApi[1].getAttributeValue()) << "Failed to change avatar";

    int64_t filesizeSrc = getFilesize(AVATARSRC);
    int64_t filesizeDst = getFilesize(AVATARDST);
    ASSERT_EQ(filesizeDst, filesizeSrc) << "Received avatar differs from uploaded avatar";

    delete u;


    // --- Delete avatar ---

    mApi[1].userUpdated = false;
    ASSERT_EQ(API_OK, synchronousSetAvatar(0, nullptr));
    ASSERT_TRUE( waitForResponse(&mApi[1].userUpdated) )   // at the target side (auxiliar account)
            << "User attribute update not received after " << maxTimeout << " seconds";


    // --- Get non-existing avatar of a contact ---

    u = megaApi[0]->getMyUser();

    null_pointer = (u == NULL);
    ASSERT_FALSE(null_pointer) << "Cannot find the MegaUser for email: " << mApi[0].email;

    mApi[1].setAttributeValue("");

    ASSERT_NO_FATAL_FAILURE( getUserAttribute(u, MegaApi::USER_ATTR_AVATAR));
    ASSERT_EQ("Avatar not found", mApi[1].getAttributeValue()) << "Failed to remove avatar";

    delete u;


    // --- Delete an existing contact ---

    ASSERT_EQ(API_OK, removeContact(0, mApi[1].email) );

    u = megaApi[0]->getContact(mApi[1].email.c_str());
    null_pointer = (u == NULL);

    ASSERT_FALSE(null_pointer) << "Cannot find the MegaUser for email: " << mApi[1].email;
    ASSERT_EQ(MegaUser::VISIBILITY_HIDDEN, u->getVisibility()) << "New contact is still visible";

    delete u;
}

TEST_F(SdkTest, SdkTestAppsPrefs)
{
    testPrefs("___TEST AppsPrefs___", MegaApi::USER_ATTR_APPS_PREFS);
}

TEST_F(SdkTest, SdkTestCcPrefs)
{
    testPrefs("___TEST CcPrefs___", MegaApi::USER_ATTR_CC_PREFS);
}

void SdkTest::testPrefs(const std::string& title, int type)
{
    const auto comparePrefs = [](const MegaStringMap* currentMap, const MegaStringMap* testMap) -> bool
    {
        if (!currentMap || !testMap) return false;

        std::unique_ptr<MegaStringList> currentKeys(currentMap->getKeys());
        std::unique_ptr<MegaStringList> testKeys(testMap->getKeys());
        if (!currentKeys || !testKeys)
        {
            return false;
        }

        for (int i = 0; i < testMap->size(); ++i)
        {
            // search the same key in both maps to check that pair<key, value> matches with current user attr pair
            const char* testKey = currentKeys->get(i);
            const char* aVal = currentMap->get(testKey);
            const char* bVal = testMap->get(testKey);
            if (!aVal || !bVal || strcmp(aVal, bVal))
            {
                return false;
            }
        }

        return true;
    };

    const auto isPrefsUpdated = [this, &comparePrefs, type](const MegaStringMap* uprefs) -> bool
    {
        std::unique_ptr<MegaUser> u(megaApi[0]->getMyUser());
        EXPECT_TRUE(u) << "Can't get own user";
        EXPECT_NO_FATAL_FAILURE(getUserAttribute(u.get(), type, maxTimeout, 0));
        EXPECT_TRUE(comparePrefs(mApi[0].mStringMap.get(), uprefs)) << "ERR";
        return true;
    };

    const auto fetchPrefs = [this, type](const unsigned int index) -> int
    {
        std::unique_ptr<MegaUser> u(megaApi[index]->getMyUser());
        if (!u) { return API_ENOENT; }
        mApi[index].requestFlags[MegaRequest::TYPE_GET_ATTR_USER] = false;
        return synchronousGetUserAttribute(index, u.get(), type);
    };

    LOG_info << title;
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    // fetch for current attr value
    static constexpr char keyname[] = "key1";
    const unsigned int index = 0;
    const int res = fetchPrefs(index);
    ASSERT_TRUE(res == API_ENOENT || res == API_OK);

    // set value for attr (overwrite any posible value that could exists for keyname)
    std::unique_ptr <MegaStringMap> newPrefs(MegaStringMap::createInstance());
    std::string val = std::to_string(m_time());
    unique_ptr<char[]> valB64(MegaApi::binaryToBase64(val.data(), val.size()));
    newPrefs->set(keyname, valB64.get());
    ASSERT_EQ(API_OK, synchronousSetUserAttribute(index, type, newPrefs.get()));

    // logout and login
    releaseMegaApi(index);
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    // check attr value is expected after logout/login
    ASSERT_TRUE(isPrefsUpdated(newPrefs.get())) << "";

    // set value for attr again (overwrite latest value for keyname)
    val = std::to_string(m_time());
    valB64.reset(MegaApi::binaryToBase64(val.data(), val.size()));
    newPrefs->set(keyname, valB64.get());
    ASSERT_EQ(API_OK, synchronousSetUserAttribute(index, type, newPrefs.get()));

    // check attr value is expected
    ASSERT_TRUE(isPrefsUpdated(newPrefs.get())) << "";
}

bool SdkTest::checkAlert(int apiIndex, const string& title, const string& path)
{
    bool ok = false;
    for (int i = 0; !ok && i < 10; ++i)
    {
        MegaUserAlertList* list = mApi[static_cast<size_t>(apiIndex)].megaApi->getUserAlerts();
        if (list->size() > 0)
        {
            MegaUserAlert* a = list->get(list->size() - 1);
            ok = !strcasecmp(title.c_str(), a->getTitle()) && !strcasecmp(path.c_str(), a->getPath()) && !ISUNDEF(a->getNodeHandle());

            if (!ok && i == 9)
            {
                EXPECT_STRCASEEQ(title.c_str(), a->getTitle());
                EXPECT_STRCASEEQ(path.c_str(), a->getPath());
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

void SdkTest::printCleanupErrMsg(const string& prefix,
                                 const string& errDetails,
                                 const unsigned accountIdx,
                                 const int errCode,
                                 const bool localCleanupSuccess) const
{
    const string errMsg = "  " + prefix + " [CLEANUP_ERR] " +
                          (localCleanupSuccess ? "[  ] " : "[TF] ") + "Account [" +
                          std::to_string(accountIdx) + "]: " + mApi[accountIdx].email + ". " +
                          errDetails + ". ErrCode (" + std::to_string(errCode) +
                          "): " + MegaError::getErrorString(errCode);
    LOG_err << errMsg;
}

#ifdef ENABLE_CHAT
void SdkTest::cleanupSchedMeetingsAllAccounts()
{
    const std::string prefix{"SdkTest::Cleanup(CancelSchedMeetings)"};
    LOG_debug << "# " << prefix;
    bool localCleanupSuccess{true};
    for (size_t nApi = 0; nApi < mApi.size(); ++nApi)
    {
        if (!megaApi[nApi] || !megaApi[nApi]->isLoggedIn())
        {
            continue;
        }

        for (const auto& c: mApi[nApi].chats)
        {
            if (!c.second->getScheduledMeetingList() ||
                !c.second->getScheduledMeetingList()->size() ||
                c.second->getOwnPrivilege() != MegaTextChatPeerList::PRIV_MODERATOR)
            {
                continue;
            }

            const auto schedList = c.second->getScheduledMeetingList();
            for (unsigned long j = 0; j < schedList->size(); ++j)
            {
                if (const MegaScheduledMeeting* auxSm = schedList->at(j);
                    auxSm && !auxSm->cancelled())
                {
                    std::unique_ptr<MegaScheduledRules> rules(auxSm->rules());
                    std::unique_ptr<MegaScheduledFlags> flags(auxSm->flags());
                    std::unique_ptr<MegaScheduledMeeting> sm(
                        MegaScheduledMeeting::createInstance(auxSm->chatid(),
                                                             auxSm->schedId(),
                                                             auxSm->parentSchedId(),
                                                             auxSm->organizerUserid(),
                                                             true /*cancelled*/,
                                                             auxSm->timezone(),
                                                             auxSm->startDateTime(),
                                                             auxSm->endDateTime(),
                                                             auxSm->title(),
                                                             auxSm->description(),
                                                             auxSm->attributes(),
                                                             MEGA_INVALID_TIMESTAMP /*overrides*/,
                                                             flags.get(),
                                                             rules.get()));

                    std::unique_ptr<RequestTracker> tracker(
                        new RequestTracker(megaApi[nApi].get()));
                    megaApi[nApi]->createOrUpdateScheduledMeeting(sm.get(),
                                                                  c.second->getTitle(),
                                                                  tracker.get());

                    if (auto reqResult = tracker->waitForResult(); reqResult != API_OK)
                    {
                        const string errDetails =
                            "Error cancelling scheduled meeting for chat (" +
                            string{Base64Str<MegaClient::CHATHANDLE>(c.second->getHandle())} + ")";
                        localCleanupSuccess = false;
                        printCleanupErrMsg(prefix,
                                           errDetails,
                                           static_cast<unsigned>(nApi),
                                           reqResult,
                                           localCleanupSuccess);
                    }
                }
            }
        }
    }

    updateCleanupStatus(localCleanupSuccess);
    LOG_debug << "# " << prefix << (localCleanupSuccess ? ": OK" : ": Finished with errors");
}
#endif

bool SdkTest::checkAlert(int apiIndex, const string& title, handle h, int64_t n, MegaHandle mh)
{
    bool ok = false;
    for (int i = 0; !ok && i < 10; ++i)
    {
        MegaUserAlertList* list = megaApi[static_cast<size_t>(apiIndex)]->getUserAlerts();
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
                EXPECT_STRCASEEQ(a->getTitle(), title.c_str());
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

class SdkTestShares : public SdkTest
{
protected:
    void SetUp() override;

    void TearDown() override;

    void createNodeTrees();

    MegaHandle getHandle(const std::string& path) const;

    void verifyCredentials(unsigned sharerIndex,
                           const PerApi* sharer,
                           unsigned shareeIndex,
                           const PerApi* sharee);

    void createNewContactAndVerify();

    void createOutgoingShare(MegaHandle hfolder);

    void getInshare(MegaHandle hfolder);

    void createOnePublicLink(MegaHandle hfolder, std::string& nodeLink);

    void importPublicLink(const std::string& nodeLink, MegaHandle* importedNodeHandle = nullptr);

    void revokeOutShares(MegaHandle hfolder);

    void revokePublicLink(MegaHandle hfolder);

    /**
     * @brief Makes a copy of the node located at the sourceNodePath path in the inshare folder of
     * the mSharee account and puts it in destNodeName inside the mSharee root node
     *
     * NOTE: This method uses ASSERT_* macros
     * NOTE: This method assumes you have called the getInshare method
     */
    void copyNode(const unsigned int accountId,
                  const MegaHandle sourceNodeHandle,
                  const MegaHandle destNodeHandle,
                  const std::string& destName,
                  MegaHandle* copiedNodeHandle = nullptr);

    /**
     * @brief Same as copySharedFolderToOwnCloud but invoking move instead of copy on sourceNodePath
     */
    void moveNodeToOwnCloud(const std::string& sourceNodePath,
                            const std::string& destNodeName,
                            MegaHandle* copiedNodeHandle = nullptr);

    std::unordered_map<std::string, MegaHandle> mHandles;

    // Sharer account
    static constexpr unsigned   mSharerIndex{0};

    PerApi*                     mSharer{nullptr};

    MegaApiTest*                mSharerApi{nullptr};

    // Sharee account
    static constexpr unsigned   mShareeIndex{1};

    PerApi*                     mSharee{nullptr};

    MegaApiTest*                mShareeApi{nullptr};

    // Guest account
    static constexpr unsigned   mGuestIndex{2};

    PerApi*                     mGuest{nullptr};

    MegaApiTest*                mGuestApi{nullptr};

    std::string                 mGuestEmail;

    std::string                 mGuestPass;
};

void SdkTestShares::SetUp()
{
    SdkTest::SetUp();

    // Accounts for sharer and sharee
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(2));

    // Guest for accessing the public link, No login in SetUp
    const auto [email, pass] = getEnvVarAccounts().getVarValues(mGuestIndex);
    ASSERT_FALSE(email.empty() || pass.empty());
    mApi.resize(mGuestIndex + 1);
    megaApi.resize(mGuestIndex + 1);
    configureTestInstance(mGuestIndex, email, pass);

    // Convenience
    mSharer = &mApi[mSharerIndex];
    mSharee = &mApi[mShareeIndex];
    mGuest  = &mApi[mGuestIndex];
    mSharerApi = megaApi[mSharerIndex].get();
    mShareeApi = megaApi[mShareeIndex].get();
    mGuestApi  = megaApi[mGuestIndex].get();
    mGuestEmail = email;
    mGuestPass  = pass;
}

void SdkTestShares::TearDown()
{
    SdkTest::TearDown();
}

MegaHandle SdkTestShares::getHandle(const std::string& path) const
{
    return mHandles.at(path);
}

void SdkTestShares::verifyCredentials(unsigned sharerIndex,
                                      const PerApi* sharer,
                                      unsigned shareeIndex,
                                      const PerApi* sharee)
{
    if (!gManualVerification)
        return;

    if (!areCredentialsVerified(sharerIndex, sharee->email))
    {
        ASSERT_NO_FATAL_FAILURE(SdkTest::verifyCredentials(sharerIndex, sharee->email));
    }

    if (!areCredentialsVerified(shareeIndex, sharer->email))
    {
        ASSERT_NO_FATAL_FAILURE(SdkTest::verifyCredentials(shareeIndex, sharer->email));
    }
}

void SdkTestShares::createNewContactAndVerify()
{
    // Invite
    const string message = "Hi contact. Let's share some stuff";
    mSharee->contactRequestUpdated = false;
    ASSERT_NO_FATAL_FAILURE(inviteContact(mSharerIndex,
                                          mSharee->email,
                                          message,
                                          MegaContactRequest::INVITE_ACTION_ADD));
    ASSERT_TRUE(waitForResponse(&mSharee->contactRequestUpdated, 10u))
        << "Contact request creation not received by the sharee after 10 seconds";

    // Get the the contact request
    ASSERT_NO_FATAL_FAILURE(getContactRequest(mShareeIndex, false));

    // Accept the request
    mSharer->contactRequestUpdated = false;
    mSharer->contactRequestUpdated = false;
    ASSERT_NO_FATAL_FAILURE(
        replyContact(mSharee->cr.get(), MegaContactRequest::REPLY_ACTION_ACCEPT));
    ASSERT_TRUE(waitForResponse(&mSharee->contactRequestUpdated, 10u))
        << "Contact request creation not received by the sharee after 10 seconds";
    ASSERT_TRUE(waitForResponse(&mSharer->contactRequestUpdated, 10u))
        << "Contact request creation not received by the sharer after 10 seconds";
    mSharer->cr.reset();

    // Verify credential
    ASSERT_NO_FATAL_FAILURE(verifyCredentials(mSharerIndex, mSharer, mShareeIndex, mSharee));
}

void SdkTestShares::createOutgoingShare(MegaHandle hfolder)
{
    std::unique_ptr<MegaNode> node{mSharerApi->getNodeByHandle(hfolder)};
    ASSERT_TRUE(node);

    // Create a new outgoing share
    bool inshareCheck = false;
    bool outshareCheck = false;
    mSharer->mOnNodesUpdateCompletion =
        createOnNodesUpdateLambda(hfolder, MegaNode::CHANGE_TYPE_OUTSHARE, outshareCheck);
    mSharee->mOnNodesUpdateCompletion =
        createOnNodesUpdateLambda(hfolder, MegaNode::CHANGE_TYPE_INSHARE, inshareCheck);
    ASSERT_NO_FATAL_FAILURE(
        shareFolder(node.get(), mSharee->email.c_str(), MegaShare::ACCESS_FULL));
    ASSERT_TRUE(waitForResponse(&outshareCheck))
        << "Node update not received by the sharer after " << maxTimeout << " seconds";
    ASSERT_TRUE(waitForResponse(&inshareCheck))
        << "Node update not received by the sharee after " << maxTimeout << " seconds";
    resetOnNodeUpdateCompletionCBs(); // Important to reset
    ASSERT_TRUE(outshareCheck);
    ASSERT_TRUE(inshareCheck);

    // Check the outgoing share
    const std::unique_ptr<MegaShareList> shareList{mSharerApi->getOutShares()};
    ASSERT_EQ(1, shareList->size()) << "Outgoing share failed";
    const auto share = shareList->get(0);
    ASSERT_EQ(MegaShare::ACCESS_FULL, share->getAccess()) << "Wrong access level of outgoing share";
    ASSERT_EQ(hfolder, share->getNodeHandle()) << "Wrong node handle of outgoing share";
    ASSERT_STRCASEEQ(mSharee->email.c_str(), share->getUser())
        << "Wrong email address of outgoing share";

    // Get an updated version of the node
    node.reset(mSharerApi->getNodeByHandle(hfolder));
    ASSERT_TRUE(node->isShared()) << "Wrong sharing information at outgoing share";
    ASSERT_TRUE(node->isOutShare()) << "Wrong sharing information at outgoing share";

    int accessLevel = mSharerApi->getAccess(hfolder);
    ASSERT_EQ(accessLevel, MegaShare::ACCESS_OWNER)
        << "Wrong access level for the shared folder handle";
    accessLevel = mSharerApi->getAccess(node.get());
    ASSERT_EQ(accessLevel, MegaShare::ACCESS_OWNER)
        << "Wrong access level for the shared folder node";
}

// Get and Check only one incoming share
void SdkTestShares::getInshare(MegaHandle hfolder)
{
    const std::unique_ptr<MegaShareList> shareList{megaApi[1]->getInSharesList()};
    ASSERT_EQ(1, shareList->size()) << "Incoming share not received in auxiliar account";

    // Wait for the inshare node to be decrypted
    auto descryptedPred = [this, hfolder]()
    {
        return std::unique_ptr<MegaNode>(mShareeApi->getNodeByHandle(hfolder))
            ->isNodeKeyDecrypted();
    };
    ASSERT_TRUE(WaitFor(descryptedPred, 60 * 1000));

    const std::unique_ptr<MegaUser> contact{mShareeApi->getContact(mSharer->email.c_str())};
    const std::unique_ptr<MegaNodeList> inshareNodes{mShareeApi->getInShares(contact.get())};
    ASSERT_EQ(1, inshareNodes->size()) << "Incoming share not received in auxiliar account";
    const auto thisInshareNode = inshareNodes->get(0);
    ASSERT_EQ(hfolder, thisInshareNode->getHandle()) << "Wrong node handle of incoming share";
    ASSERT_STREQ("sharedfolder", thisInshareNode->getName())
        << "Wrong folder name of incoming share";
    ASSERT_EQ(API_OK,
              mShareeApi->checkAccessErrorExtended(thisInshareNode, MegaShare::ACCESS_FULL)
                  ->getErrorCode())
        << "Wrong access level of incoming share";
    ASSERT_TRUE(thisInshareNode->isInShare()) << "Wrong sharing information at incoming share";
    ASSERT_TRUE(thisInshareNode->isShared()) << "Wrong sharing information at incoming share";

    int accessLevel = mShareeApi->getAccess(hfolder);
    ASSERT_EQ(accessLevel, MegaShare::ACCESS_FULL)
        << "Wrong access level for the shared folder handle";
    accessLevel = mShareeApi->getAccess(thisInshareNode);
    ASSERT_EQ(accessLevel, MegaShare::ACCESS_FULL)
        << "Wrong access level for the shared folder node";
}

void SdkTestShares::createOnePublicLink(MegaHandle hfolder, std::string& nodeLink)
{
    std::unique_ptr<MegaNode> nfolder{mSharerApi->getNodeByHandle(hfolder)};
    ASSERT_TRUE(nfolder);
    const bool isFreeAccount =
        mSharer->accountDetails->getProLevel() == MegaAccountDetails::ACCOUNT_TYPE_FREE;

    // Create a public link
    nodeLink = createPublicLink(mSharerIndex, nfolder.get(), 0, maxTimeout, isFreeAccount);

    // Get a fresh snapshot of the node and check it's actually exported
    nfolder.reset(mSharerApi->getNodeByHandle(hfolder));
    ASSERT_TRUE(nfolder);
    ASSERT_TRUE(nfolder->isExported()) << "Node is not exported, must be exported";
    ASSERT_FALSE(nfolder->isTakenDown()) << "Public link is taken down, it mustn't";
    ASSERT_STREQ(nodeLink.c_str(), std::unique_ptr<char[]>(nfolder->getPublicLink()).get())
        << "Wrong public link from MegaNode";

    // Regenerate the same link should not trigger a new request
    ASSERT_EQ(nodeLink, createPublicLink(mSharerIndex, nfolder.get(), 0, maxTimeout, isFreeAccount))
        << "Wrong public link after link update";
}

void SdkTestShares::importPublicLink(const std::string& nodeLink, MegaHandle* importedNodeHandle)
{
    // Login to the folder and fetchnodes
    auto loginFolderTracker = asyncRequestLoginToFolder(mGuestIndex, nodeLink.c_str());
    ASSERT_EQ(loginFolderTracker->waitForResult(), API_OK)
        << "Failed to login to folder " << nodeLink;
    ASSERT_NO_FATAL_FAILURE(fetchnodes(mGuestIndex));

    // Authorize the node
    std::unique_ptr<MegaNode> folderNodeToImport{mGuestApi->getRootNode()};
    ASSERT_TRUE(folderNodeToImport) << "Failed to get folder node to import from link " << nodeLink;
    std::unique_ptr<MegaNode> authorizedFolderNode{
        mGuestApi->authorizeNode(folderNodeToImport.get())};
    ASSERT_TRUE(authorizedFolderNode) << "Failed to authorize folder node from link " << nodeLink;
    ASSERT_TRUE(authorizedFolderNode->getChildren())
        << "Authorized folder node children list is null but it should not";
    ASSERT_EQ(mGuestApi->getNumChildren(folderNodeToImport.get()),
              authorizedFolderNode->getChildren()->size())
        << "Different number of child nodes after authorizing the folder node";

    // Logout the folder
    ASSERT_NO_FATAL_FAILURE(logout(mGuestIndex, false, 20));

    // Login with guest and fetch nodes
    auto loginTracker = asyncRequestLogin(mGuestIndex, mGuestEmail.c_str(), mGuestPass.c_str());
    ASSERT_EQ(loginTracker->waitForResult(), API_OK) << "Failed to login with " << mGuestEmail;
    ASSERT_NO_FATAL_FAILURE(fetchnodes(mGuestIndex));

    // Copy(import) the public folder (authorized) to the root of the account
    std::unique_ptr<MegaNode> rootNode{mGuestApi->getRootNode()};
    RequestTracker nodeCopyTracker{mGuestApi};
    mGuestApi->copyNode(authorizedFolderNode.get(), rootNode.get(), nullptr, &nodeCopyTracker);
    ASSERT_EQ(nodeCopyTracker.waitForResult(), API_OK) << "Failed to copy node to import";
    std::unique_ptr<MegaNode> importedNode{
        mGuestApi->getNodeByPath(authorizedFolderNode->getName(), rootNode.get())};
    ASSERT_TRUE(importedNode) << "Imported node not found";
    if (authorizedFolderNode->getChildren()->size())
    {
        std::unique_ptr<MegaNode> authorizedImportedNode(
            mGuestApi->authorizeNode(importedNode.get()));
        EXPECT_TRUE(authorizedImportedNode) << "Failed to authorize imported node";
        EXPECT_TRUE(authorizedImportedNode->getChildren())
            << "Authorized imported node children list is null but it should not";
        ASSERT_EQ(authorizedFolderNode->getChildren()->size(),
                  authorizedImportedNode->getChildren()->size())
            << "Not all child nodes have been imported";
    }

    if (importedNodeHandle)
        *importedNodeHandle = importedNode->getHandle();
}

// Revoke access to an outgoing shares
void SdkTestShares::revokeOutShares(MegaHandle hfolder)
{
    const std::unique_ptr<MegaNode> node{mSharerApi->getNodeByHandle(hfolder)};
    ASSERT_TRUE(node);
    bool inshareCheck = false;
    bool outshareCheck = false;
    mSharer->mOnNodesUpdateCompletion =
        createOnNodesUpdateLambda(hfolder, MegaNode::CHANGE_TYPE_OUTSHARE, outshareCheck);
    mSharee->mOnNodesUpdateCompletion =
        createOnNodesUpdateLambda(hfolder, MegaNode::CHANGE_TYPE_REMOVED, inshareCheck);
    ASSERT_NO_FATAL_FAILURE(
        shareFolder(node.get(), mSharee->email.c_str(), MegaShare::ACCESS_UNKNOWN));
    ASSERT_TRUE(waitForResponse(&outshareCheck)) // at the target side (main account)
        << "Node update not received by the sharer after " << maxTimeout << " seconds";
    ASSERT_TRUE(waitForResponse(&inshareCheck)) // at the target side (auxiliar account)
        << "Node update not received by the sharee after " << maxTimeout << " seconds";

    // important to reset
    resetOnNodeUpdateCompletionCBs();
    ASSERT_TRUE(outshareCheck);
    ASSERT_TRUE(inshareCheck);

    const std::unique_ptr<MegaShareList> sl{mSharerApi->getOutShares()};
    ASSERT_EQ(0, sl->size()) << "Outgoing share revocation failed";
}

void SdkTestShares::revokePublicLink(MegaHandle hfolder)
{
    // Remove
    std::unique_ptr<MegaNode> node{mSharerApi->getNodeByHandle(hfolder)};
    ASSERT_TRUE(node);
    const MegaHandle removedLinkHandle = removePublicLink(mSharerIndex, node.get());

    // Get a fresh node and check
    node.reset(mSharerApi->getNodeByHandle(removedLinkHandle));
    ASSERT_TRUE(node);
    ASSERT_FALSE(node->isPublic()) << "Public link removal failed (still public)";
}

void SdkTestShares::copyNode(const unsigned int accountId,
                             const MegaHandle sourceNodeHandle,
                             const MegaHandle destNodeHandle,
                             const std::string& destName,
                             MegaHandle* copiedNodeHandle)
{
    auto& api = accountId == mShareeIndex ? mShareeApi : mSharerApi;
    std::unique_ptr<MegaNode> source = std::unique_ptr<MegaNode>(
        sourceNodeHandle == INVALID_HANDLE ? api->getRootNode() :
                                             api->getNodeByHandle(sourceNodeHandle));
    std::unique_ptr<MegaNode> dest = std::unique_ptr<MegaNode>(
        destNodeHandle == INVALID_HANDLE ? api->getRootNode() :
                                           api->getNodeByHandle(destNodeHandle));

    auto result =
        doCopyNode(accountId, copiedNodeHandle, source.get(), dest.get(), destName.c_str());
    ASSERT_EQ(result, API_OK) << "Error copying file";
    if (copiedNodeHandle)
    {
        ASSERT_NE(*copiedNodeHandle, INVALID_HANDLE)
            << "The copied file handle was not set properly";
    }
}

void SdkTestShares::moveNodeToOwnCloud(const std::string& sourceNodePath,
                                       const std::string& destNodeName,
                                       MegaHandle* movedNodeHandle)
{
    std::unique_ptr<MegaNode> source{mShareeApi->getNodeByHandle(getHandle(sourceNodePath))};
    std::unique_ptr<MegaNode> dest{mShareeApi->getRootNode()};
    auto result =
        doMoveNode(mShareeIndex, movedNodeHandle, source.get(), dest.get(), destNodeName.c_str());
    ASSERT_EQ(result, API_OK);
}

// Initialize a test scenario : create some folders/files to share
// Create some nodes to share
//  |--sharedfolder
//    |--subfolder
//      |--file.txt
//    |--file.txt
void SdkTestShares::createNodeTrees()
{
    const std::unique_ptr<MegaNode> rootnode{mSharerApi->getRootNode()};
    const MegaHandle hfolder = mHandles["/sharedfolder"] =
        createFolder(mSharerIndex, "sharedfolder", rootnode.get());
    ASSERT_NE(hfolder, UNDEF);

    const std::unique_ptr<MegaNode> node{mSharerApi->getNodeByHandle(hfolder)};
    ASSERT_TRUE(node);

    const MegaHandle subfolder = mHandles["/sharedfolder/subfolder"] =
        createFolder(mSharerIndex,
                     "subfolder",
                     node.get());
    ASSERT_NE(subfolder, UNDEF);

    // Create a local file
    ASSERT_TRUE(createFile("file.txt", false)) << "Couldn't create "
                                               << "file.txt";

    // Create a node /sharefolder/file.txt by uploading
    MegaHandle hfile = UNDEF;
    ASSERT_EQ(MegaError::API_OK,
              doStartUpload(mSharerIndex,
                            &hfile,
                            "file.txt",
                            node.get(),
                            nullptr /*fileName*/,
                            ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
                            nullptr /*appData*/,
                            false /*isSourceTemporary*/,
                            false /*startFirst*/,
                            nullptr /*cancelToken*/))
        << "Cannot upload a test file";
    mHandles["/sharedfolder/file.txt"] = hfile;

    // Create a node /sharedfolder/subfolder/file.txt by uploading
    ASSERT_EQ(MegaError::API_OK,
              doStartUpload(mSharerIndex,
                            &hfile,
                            "file.txt",
                            node.get(),
                            nullptr /*fileName*/,
                            ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
                            nullptr /*appData*/,
                            false /*isSourceTemporary*/,
                            false /*startFirst*/,
                            nullptr /*cancelToken*/))
        << "Cannot upload a second test file";
    mHandles["/sharedfolder/subfolder/file.txt"] = hfile;
}

/**
 * @brief TEST_F SdkTestShares2
 *
 * - Create and upload some folders and files to User1 account
 * - Create a new contact to share to
 * - Share a folder with User2
 * - Check the outgoing share from User1
 * - Check the incoming share to User2
 * - Check that User2 (sharee) cannot tag the incoming share as favourite
 * - Check that User1 (sharer) can tag the outgoing share as favourite
 * - Get file name and fingerprint from User1
 * - Search by file name for User2
 * - Search by fingerprint for User2
 * - User2 add file
 * - Check that User1 has received the change
 * - User1 remove file
 * - Locallogout from User2 and login with session
 * - Check that User2 no longer sees the removed file
 */
TEST_F(SdkTest, SdkTestShares2)
{
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(2));

    // --- Create some nodes to share ---
    //  |--Shared-folder
    //    |--subfolder
    //      |--file.txt
    //    |--file.txt

    std::unique_ptr<MegaNode> rootnode{ megaApi[0]->getRootNode() };
    static constexpr char foldername1[] = "Shared-folder";
    MegaHandle hfolder1 = createFolder(0, foldername1, rootnode.get());
    ASSERT_NE(hfolder1, UNDEF) << "Cannot create " << foldername1;

    std::unique_ptr<MegaNode> n1{ megaApi[0]->getNodeByHandle(hfolder1) };
    ASSERT_NE(n1, nullptr);

    static constexpr char foldername2[] = "subfolder";
    MegaHandle hfolder2 = createFolder(0, foldername2, n1.get());
    ASSERT_NE(hfolder2, UNDEF) << "Cannot create " << foldername2;

    createFile(PUBLICFILE.c_str(), false);   // not a large file since don't need to test transfers here

    MegaHandle hfile1 = 0;
    ASSERT_EQ(MegaError::API_OK, doStartUpload(0, &hfile1, PUBLICFILE.c_str(), n1.get(),
                                               nullptr /*fileName*/,
                                               ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
                                               nullptr /*appData*/,
                                               false   /*isSourceTemporary*/,
                                               false   /*startFirst*/,
                                               nullptr /*cancelToken*/)) << "Cannot upload a test file";

    MegaHandle hfile2 = 0;
    ASSERT_EQ(MegaError::API_OK, doStartUpload(0, &hfile2, PUBLICFILE.c_str(), std::unique_ptr<MegaNode>{megaApi[0]->getNodeByHandle(hfolder2)}.get(),
                                               nullptr /*fileName*/,
                                               ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
                                               nullptr /*appData*/,
                                               false   /*isSourceTemporary*/,
                                               false   /*startFirst*/,
                                               nullptr /*cancelToken*/)) << "Cannot upload a second test file";


    // --- Create a new contact to share to ---

    string message = "Hi contact. Let's share some stuff";

    mApi[1].contactRequestUpdated = false;
    ASSERT_NO_FATAL_FAILURE(inviteContact(0, mApi[1].email, message, MegaContactRequest::INVITE_ACTION_ADD));
    ASSERT_TRUE(waitForResponse(&mApi[1].contactRequestUpdated))   // at the target side (auxiliar account)
        << "Contact request creation not received after " << maxTimeout << " seconds";

    ASSERT_NO_FATAL_FAILURE(getContactRequest(1, false));

    mApi[0].contactRequestUpdated = mApi[1].contactRequestUpdated = false;
    ASSERT_NO_FATAL_FAILURE(replyContact(mApi[1].cr.get(), MegaContactRequest::REPLY_ACTION_ACCEPT));
    ASSERT_TRUE(waitForResponse(&mApi[1].contactRequestUpdated))   // at the target side (auxiliar account)
        << "Contact request creation not received after " << maxTimeout << " seconds";
    ASSERT_TRUE(waitForResponse(&mApi[0].contactRequestUpdated))   // at the source side (main account)
        << "Contact request creation not received after " << maxTimeout << " seconds";

    mApi[1].cr.reset();

    // --- Verify credentials in both accounts ---

    if (gManualVerification)
    {
        if (!areCredentialsVerified(0, mApi[1].email)) {ASSERT_NO_FATAL_FAILURE(verifyCredentials(0, mApi[1].email));}
        if (!areCredentialsVerified(1, mApi[0].email)) {ASSERT_NO_FATAL_FAILURE(verifyCredentials(1, mApi[0].email));}
    }

    // --- Share a folder with User2 ---
    MegaHandle nodeHandle = n1->getHandle();
    bool check1, check2;
    mApi[0].mOnNodesUpdateCompletion = createOnNodesUpdateLambda(nodeHandle, MegaNode::CHANGE_TYPE_OUTSHARE, check1);
    mApi[1].mOnNodesUpdateCompletion = createOnNodesUpdateLambda(nodeHandle, MegaNode::CHANGE_TYPE_INSHARE, check2);

    ASSERT_NO_FATAL_FAILURE(shareFolder(n1.get(), mApi[1].email.c_str(), MegaShare::ACCESS_FULL));
    ASSERT_TRUE(waitForResponse(&check1))   // at the target side (main account)
        << "Node update not received after " << maxTimeout << " seconds";
    ASSERT_TRUE(waitForResponse(&check2))   // at the target side (auxiliar account)
        << "Node update not received after " << maxTimeout << " seconds";
    // important to reset
    resetOnNodeUpdateCompletionCBs();
    ASSERT_EQ(check1, true);
    ASSERT_EQ(check2, true);


    // --- Check the outgoing share from User1 ---

    std::unique_ptr<MegaShareList> sl{ megaApi[0]->getOutShares() };
    ASSERT_EQ(1, sl->size()) << "Outgoing share failed";
    MegaShare* s = sl->get(0);

    n1.reset(megaApi[0]->getNodeByHandle(hfolder1));    // get an updated version of the node

    ASSERT_EQ(MegaShare::ACCESS_FULL, s->getAccess()) << "Wrong access level of outgoing share";
    ASSERT_EQ(hfolder1, s->getNodeHandle()) << "Wrong node handle of outgoing share";
    ASSERT_STRCASEEQ(mApi[1].email.c_str(), s->getUser()) << "Wrong email address of outgoing share";
    ASSERT_TRUE(n1->isShared()) << "Wrong sharing information at outgoing share";
    ASSERT_TRUE(n1->isOutShare()) << "Wrong sharing information at outgoing share";


    // --- Check the incoming share to User2 ---

    sl.reset(megaApi[1]->getInSharesList());
    ASSERT_EQ(1, sl->size()) << "Incoming share not received in auxiliar account";

    // Wait for the inshare node to be decrypted
    ASSERT_TRUE(WaitFor([this, &n1]() { return unique_ptr<MegaNode>(megaApi[1]->getNodeByHandle(n1->getHandle()))->isNodeKeyDecrypted(); }, 60*1000));

    std::unique_ptr<MegaUser> contact(megaApi[1]->getContact(mApi[0].email.c_str()));
    std::unique_ptr<MegaNodeList> nl(megaApi[1]->getInShares(contact.get()));
    ASSERT_EQ(1, nl->size()) << "Incoming share not received in auxiliar account";
    MegaNode* n = nl->get(0);

    ASSERT_EQ(hfolder1, n->getHandle()) << "Wrong node handle of incoming share";
    ASSERT_STREQ(foldername1, n->getName()) << "Wrong folder name of incoming share";
    ASSERT_EQ(MegaError::API_OK,
              megaApi[1]->checkAccessErrorExtended(n, MegaShare::ACCESS_FULL)->getErrorCode())
        << "Wrong access level of incoming share";
    ASSERT_TRUE(n->isInShare()) << "Wrong sharing information at incoming share";
    ASSERT_TRUE(n->isShared()) << "Wrong sharing information at incoming share";


    // --- Check that User2 (sharee) cannot tag the incoming share as favourite ---

    auto errU2SetFavourite = synchronousSetNodeFavourite(1, n, true);
    ASSERT_EQ(API_EACCESS, errU2SetFavourite) << " synchronousSetNodeFavourite by the sharee should return API_EACCESS (returned error: " << errU2SetFavourite << ")";

    // --- Check that User2 (sharee) cannot tag an inner inshare folder as favourite ---

    std::unique_ptr<MegaNode> subfolderNode {megaApi[1]->getNodeByHandle(hfolder2)};
    auto errU2SetFavourite2 = synchronousSetNodeFavourite(1, subfolderNode.get(), true);
    ASSERT_EQ(API_EACCESS, errU2SetFavourite2) << " synchronousSetNodeFavourite by the sharee should return API_EACCESS (returned error: " << errU2SetFavourite << ")";

    // --- Check that User1 (sharer) can tag the outgoing share as favourite ---

    auto errU1SetFavourite = synchronousSetNodeFavourite(0, n, true);
    ASSERT_EQ(API_OK, errU1SetFavourite) << " synchronousSetNodeFavourite by the sharer failed (error: " << errU1SetFavourite << ")";

    // --- Check that User1 (sharer) can tag an inner outshare folder as favourite ---

    auto errU1SetFavourite2 = synchronousSetNodeFavourite(0, subfolderNode.get(), true);
    ASSERT_EQ(API_OK, errU1SetFavourite2) << " synchronousSetNodeFavourite by the sharer failed (error: " << errU1SetFavourite << ")";


    // --- Get file name and fingerprint from User1 account ---

    unique_ptr<MegaNode> nfile2(megaApi[0]->getNodeByHandle(hfile2));
    ASSERT_NE(nfile2, nullptr) << "Cannot initialize second node for scenario (error: " << mApi[0].lastError << ")";
    const char* fileNameToSearch = nfile2->getName();
    const char* fingerPrintToSearch = nfile2->getFingerprint();


    // --- Search by fingerprint for User2 ---

    unique_ptr<MegaNodeList> fingerPrintList(megaApi[1]->getNodesByFingerprint(fingerPrintToSearch));
    ASSERT_EQ(fingerPrintList->size(), 2) << "Node count by fingerprint is wrong"; // the same file was uploaded twice, with differernt paths
    bool found = false;
    for (int i = 0; i < fingerPrintList->size(); i++)
    {
        if (fingerPrintList->get(i)->getHandle() == hfile2)
        {
            found = true;
            break;
        }
    }

    ASSERT_TRUE(found);


    // --- Search by file name for User2 ---

    std::unique_ptr<MegaSearchFilter> filterResults(MegaSearchFilter::createInstance());
    filterResults->byName(fileNameToSearch);
    std::unique_ptr<MegaNodeList> searchList(megaApi[1]->search(filterResults.get()));
    ASSERT_EQ(searchList->size(), 2) << "Node count by file name is wrong"; // the same file was uploaded twice, to differernt paths
    ASSERT_TRUE((searchList->get(0)->getHandle() == hfile1 && searchList->get(1)->getHandle() == hfile2) ||
                (searchList->get(0)->getHandle() == hfile2 && searchList->get(1)->getHandle() == hfile1))
                << "Node handles are not the expected ones";


    // --- User2 add file ---
    //  |--Shared-folder
    //    |--subfolder
    //      |--by_user_2.txt

    static constexpr char fileByUser2[] = "by_user_2.txt";
    createFile(fileByUser2, false);   // not a large file since don't need to test transfers here
    MegaHandle hfile2U2 = 0;
    mApi[1].mOnNodesUpdateCompletion = createOnNodesUpdateLambda(INVALID_HANDLE, MegaNode::CHANGE_TYPE_NEW, check1);
    mApi[0].mOnNodesUpdateCompletion = createOnNodesUpdateLambda(INVALID_HANDLE, MegaNode::CHANGE_TYPE_NEW, check2);
    ASSERT_EQ(MegaError::API_OK, doStartUpload(1, &hfile2U2, fileByUser2, std::unique_ptr<MegaNode>{megaApi[1]->getNodeByHandle(hfolder2)}.get(),
                                               nullptr /*fileName*/,
                                               ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
                                               nullptr /*appData*/,
                                               false   /*isSourceTemporary*/,
                                               false   /*startFirst*/,
                                               nullptr /*cancelToken*/)) << "Cannot upload a second test file";

    ASSERT_TRUE(waitForResponse(&check1)) << "Node update not received on client 1 after " << maxTimeout << " seconds";
    ASSERT_TRUE(waitForResponse(&check2)) << "Node update not received on client 0 after " << maxTimeout << " seconds";
    // important to reset
    resetOnNodeUpdateCompletionCBs();
    ASSERT_EQ(check1, true);
    ASSERT_EQ(check2, true);



    // --- Check that User1 has received the change ---

    std::unique_ptr<MegaNode>nU2{ megaApi[0]->getNodeByHandle(hfile2U2) };    // get an updated version of the node
    ASSERT_TRUE(nU2 && string(fileByUser2) == nU2->getName()) << "Finding node by handle failed";


    // --- Locallogout from User1 and login with session ---

    string session = unique_ptr<char[]>(dumpSession()).get();
    locallogout();
    auto tracker = asyncRequestFastLogin(0, session.c_str());
    PerApi& target0 = mApi[0];
    target0.resetlastEvent();
    ASSERT_EQ(API_OK, tracker->waitForResult()) << " Failed to establish a login/session for account " << 0;
    fetchnodes(0, maxTimeout);
    ASSERT_TRUE(WaitFor([&target0](){ return target0.lastEventsContain(MegaEvent::EVENT_NODES_CURRENT); }, 10000))
        << "Timeout expired to receive actionpackets";


    // --- User1 remove file ---

    ASSERT_EQ(MegaError::API_OK, synchronousRemove(0, nfile2.get())) << "Error while removing file " << nfile2->getName();


    // --- Locallogout from User2 and login with session ---

    session = unique_ptr<char[]>(megaApi[1]->dumpSession()).get();

    auto logoutErr = doRequestLocalLogout(1);
    ASSERT_EQ(MegaError::API_OK, logoutErr) << "Local logout failed (error: " << logoutErr << ")";
    PerApi& target1 = mApi[1];
    target1.resetlastEvent();   // clear any previous EVENT_NODES_CURRENT
    auto trackerU2 = asyncRequestFastLogin(1, session.c_str());
    ASSERT_EQ(API_OK, trackerU2->waitForResult()) << " Failed to establish a login/session for account " << 1;
    fetchnodes(1, maxTimeout);

    // make sure that client is up to date (upon logout, recent changes might not be committed to DB)
    ASSERT_TRUE(WaitFor([&target1](){ return target1.lastEventsContain(MegaEvent::EVENT_NODES_CURRENT); }, 10000))
        << "Timeout expired to receive actionpackets";

    // --- Check that User2 no longer sees the removed file ---

    std::unique_ptr<MegaNode> nremoved{ megaApi[1]->getNodeByHandle(hfile2) };    // get an updated version of the node
    ASSERT_EQ(nremoved, nullptr) << " Failed to see the file was removed";
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
 * - Add some subfolders
 * - Share a nested folder with same contact
 * - Check the reception and correctness of the incoming nested share
 * - Stop share main in share
 * - Check correctness of the account size
 * - Share the main in share again
 * - Check correctness of the account size
 * - Stop share nested inshare
 * - Check correctness of the account size
 * - Modify the access level
 * - Sharee leaves the inshare
 * - Share again the main folder
 * - Revoke the access to the share
 * - Share a folder with a non registered email
 * - Check the correctness of the pending outgoing share
 * - Create a file public link
 * - Import a file public link
 * - Get a node from a file public link
 * - Remove a public link
 * - Create a folder public link
 * - Import folder public link
 */
TEST_F(SdkTest, SdkTestShares)
{
    LOG_info << "___TEST Shares___";
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(2));

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

    std::unique_ptr<MegaNode> n1(megaApi[0]->getNodeByHandle(hfolder1));
    ASSERT_NE(n1.get(), nullptr);
    unsigned long long inSharedNodeCount = 1;

    char foldername2[64] = "subfolder";
    MegaHandle hfolder2 = createFolder(0, foldername2, std::unique_ptr<MegaNode>{megaApi[0]->getNodeByHandle(hfolder1)}.get());
    ASSERT_NE(hfolder2, UNDEF);
    ++inSharedNodeCount;

    // not a large file since don't need to test transfers here
    ASSERT_TRUE(createFile(PUBLICFILE.c_str(), false)) << "Couldn't create " << PUBLICFILE.c_str();

    MegaHandle hfile1 = UNDEF;
    ASSERT_EQ(MegaError::API_OK, doStartUpload(0, &hfile1, PUBLICFILE.c_str(),
                                                        std::unique_ptr<MegaNode>{megaApi[0]->getNodeByHandle(hfolder1)}.get(),
                                                        nullptr /*fileName*/,
                                                        ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
                                                        nullptr /*appData*/,
                                                        false   /*isSourceTemporary*/,
                                                        false   /*startFirst*/,
                                                        nullptr /*cancelToken*/)) << "Cannot upload a test file";

    ++inSharedNodeCount;

    MegaHandle hfile2 = UNDEF;
    ASSERT_EQ(MegaError::API_OK, doStartUpload(0, &hfile2, PUBLICFILE.c_str(),
                                                        std::unique_ptr<MegaNode>{megaApi[0]->getNodeByHandle(hfolder2)}.get(),
                                                        nullptr /*fileName*/,
                                                        ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
                                                        nullptr /*appData*/,
                                                        false   /*isSourceTemporary*/,
                                                        false   /*startFirst*/,
                                                        nullptr /*cancelToken*/)) << "Cannot upload a second test file";

    ++inSharedNodeCount;

    // --- Download authorized node from another account ---

    MegaNode *nNoAuth = megaApi[0]->getNodeByHandle(hfile1);

    int transferError = doStartDownload(1, nNoAuth,
                                                 "unauthorized_node",
                                                 nullptr  /*customName*/,
                                                 nullptr  /*appData*/,
                                                 false    /*startFirst*/,
                                                 nullptr  /*cancelToken*/,
                                                 MegaTransfer::COLLISION_CHECK_FINGERPRINT /*collisionCheck*/,
                                                 MegaTransfer::COLLISION_RESOLUTION_NEW_WITH_N /* collisionResolution */,
                                                 false    /*undelete*/);


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
                                             nullptr  /*cancelToken*/,
                                             MegaTransfer::COLLISION_CHECK_FINGERPRINT /*collisionCheck*/,
                                             MegaTransfer::COLLISION_RESOLUTION_NEW_WITH_N /* collisionResolution */,
                                             false    /*undelete*/);

    ASSERT_EQ(API_OK, transferError) << "Cannot download authorized node (error: " << mApi[1].lastError << ")";
    delete nNoAuth;
    delete nAuth;

    // Initialize a test scenario: create a new contact to share to and verify credentials

    string message = "Hi contact. Let's share some stuff";

    mApi[1].contactRequestUpdated = false;
    ASSERT_NO_FATAL_FAILURE( inviteContact(0, mApi[1].email, message, MegaContactRequest::INVITE_ACTION_ADD) );
    EXPECT_TRUE( waitForResponse(&mApi[1].contactRequestUpdated, 10u) )   // at the target side (auxiliar account)
            << "Contact request creation not received after 10 seconds";


    EXPECT_NO_FATAL_FAILURE( getContactRequest(1, false) );

    mApi[0].contactRequestUpdated = mApi[1].contactRequestUpdated = false;
    EXPECT_NO_FATAL_FAILURE( replyContact(mApi[1].cr.get(), MegaContactRequest::REPLY_ACTION_ACCEPT) );
    EXPECT_TRUE( waitForResponse(&mApi[1].contactRequestUpdated, 10u) )   // at the target side (auxiliar account)
            << "Contact request creation not received after 10 seconds";
    EXPECT_TRUE( waitForResponse(&mApi[0].contactRequestUpdated, 10u) )   // at the source side (main account)
            << "Contact request creation not received after 10 seconds";

    mApi[1].cr.reset();

    if (gManualVerification)
    {
        if (!areCredentialsVerified(0, mApi[1].email)) {ASSERT_NO_FATAL_FAILURE(verifyCredentials(0, mApi[1].email));}
        if (!areCredentialsVerified(1, mApi[0].email)) {ASSERT_NO_FATAL_FAILURE(verifyCredentials(1, mApi[0].email));}
    }

    auto ownedNodeCount = megaApi[1]->getAccurateNumNodes();

    // upload a file, just to test node counters
    bool check1;
    mApi[1].mOnNodesUpdateCompletion = createOnNodesUpdateLambda(INVALID_HANDLE, MegaNode::CHANGE_TYPE_NEW, check1);
    ASSERT_EQ(MegaError::API_OK, doStartUpload(1, nullptr, PUBLICFILE.data(), std::unique_ptr<MegaNode>{megaApi[1]->getRootNode()}.get(),
                                               nullptr /*fileName*/,
                                               ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
                                               nullptr /*appData*/,
                                               false   /*isSourceTemporary*/,
                                               false   /*startFirst*/,
                                               nullptr /*cancelToken*/)) << "Cannot upload a second test file";

    ASSERT_TRUE(waitForResponse(&check1)) << "Node update not received after " << maxTimeout << " seconds";
    // important to reset
    resetOnNodeUpdateCompletionCBs();
    auto nodeCountAfterNewOwnedFile = megaApi[1]->getAccurateNumNodes();
    ASSERT_EQ(ownedNodeCount + 1, nodeCountAfterNewOwnedFile);
    ownedNodeCount = nodeCountAfterNewOwnedFile;
    ASSERT_EQ(check1, true);


    // --- Create a new outgoing share ---
    bool check2;
    mApi[0].mOnNodesUpdateCompletion = createOnNodesUpdateLambda(hfolder1, MegaNode::CHANGE_TYPE_OUTSHARE, check1);
    mApi[1].mOnNodesUpdateCompletion = createOnNodesUpdateLambda(hfolder1, MegaNode::CHANGE_TYPE_INSHARE, check2);

    ASSERT_NO_FATAL_FAILURE( shareFolder(n1.get(), mApi[1].email.c_str(), MegaShare::ACCESS_FULL) );
    ASSERT_TRUE( waitForResponse(&check1) )   // at the target side (main account)
            << "Node update not received after " << maxTimeout << " seconds";
    ASSERT_TRUE( waitForResponse(&check2) )   // at the target side (auxiliar account)
            << "Node update not received after " << maxTimeout << " seconds";

    // important to reset
    resetOnNodeUpdateCompletionCBs();
    ASSERT_EQ(check1, true);
    ASSERT_EQ(check2, true);

    // --- Check the outgoing share ---

    auto sl = std::unique_ptr<MegaShareList>{megaApi[0]->getOutShares()};
    ASSERT_EQ(1, sl->size()) << "Outgoing share failed";
    // Test another interface
    sl.reset(megaApi[0]->getOutShares(n1.get()));
    ASSERT_EQ(1, sl->size()) << "Outgoing share failed";

    MegaShare* s = sl->get(0);

    n1.reset(megaApi[0]->getNodeByHandle(hfolder1));    // get an updated version of the node

    ASSERT_EQ(MegaShare::ACCESS_FULL, s->getAccess()) << "Wrong access level of outgoing share";
    ASSERT_EQ(hfolder1, s->getNodeHandle()) << "Wrong node handle of outgoing share";
    ASSERT_STRCASEEQ(mApi[1].email.c_str(), s->getUser()) << "Wrong email address of outgoing share";
    ASSERT_TRUE(n1->isShared()) << "Wrong sharing information at outgoing share";
    ASSERT_TRUE(n1->isOutShare()) << "Wrong sharing information at outgoing share";

    // --- Check the incoming share ---

    sl.reset(megaApi[1]->getInSharesList());
    ASSERT_EQ(1, sl->size()) << "Incoming share not received in auxiliar account";

    // Wait for the inshare node to be decrypted
    ASSERT_TRUE(WaitFor([this, &n1]() { return unique_ptr<MegaNode>(megaApi[1]->getNodeByHandle(n1->getHandle()))->isNodeKeyDecrypted(); }, 60*1000));

    std::unique_ptr<MegaUser> contact(megaApi[1]->getContact(mApi[0].email.c_str()));
    auto nl = std::unique_ptr<MegaNodeList>{megaApi[1]->getInShares(contact.get())};
    ASSERT_EQ(1, nl->size()) << "Incoming share not received in auxiliar account";
    MegaNode* n = nl->get(0);

    ASSERT_EQ(hfolder1, n->getHandle()) << "Wrong node handle of incoming share";
    ASSERT_STREQ(foldername1, n->getName()) << "Wrong folder name of incoming share";
    ASSERT_EQ(API_OK,
              megaApi[1]->checkAccessErrorExtended(n, MegaShare::ACCESS_FULL)->getErrorCode())
        << "Wrong access level of incoming share";
    ASSERT_TRUE(n->isInShare()) << "Wrong sharing information at incoming share";
    ASSERT_TRUE(n->isShared()) << "Wrong sharing information at incoming share";

    auto nodeCountAfterInShares = megaApi[1]->getAccurateNumNodes();
    ASSERT_EQ(ownedNodeCount + inSharedNodeCount, nodeCountAfterInShares);

    // --- Move share file from different subtree, same file and fingerprint ---
    // Pre-requisite, the movement finds a file with same name and fp at target folder
    // Since the source and target folders belong to different trees, it will attempt to copy+delete
    // (hfile1 copied to rubbish, renamed to "copy", copied back to hfolder2, move
    // Since there is a file with same name and fingerprint, it will skip the copy and will do delete
    mApi[0].mOnNodesUpdateCompletion = createOnNodesUpdateLambda(INVALID_HANDLE, MegaNode::CHANGE_TYPE_NEW, check1);
    mApi[1].mOnNodesUpdateCompletion = createOnNodesUpdateLambda(INVALID_HANDLE, MegaNode::CHANGE_TYPE_NEW, check2);
    MegaHandle copiedNodeHandle = INVALID_HANDLE;
    ASSERT_EQ(API_OK, doCopyNode(1, &copiedNodeHandle, std::unique_ptr<MegaNode>(megaApi[1]->getNodeByHandle(hfile2)).get(),
              std::unique_ptr<MegaNode>(megaApi[1]->getNodeByHandle(hfolder1)).get(), "copy")) << "Copying shared file (not owned) to same place failed";
    EXPECT_TRUE( waitForResponse(&check1, 10u) )   // at the target side (main account)
            << "Node update not received after 10 seconds";
    ASSERT_TRUE( waitForResponse(&check2) )   // at the target side (auxiliar account)
            << "Node update not received after " << maxTimeout << " seconds";

    resetOnNodeUpdateCompletionCBs();
    ++inSharedNodeCount;
    EXPECT_EQ(check1, true);
    ASSERT_EQ(check2, true);


    mApi[1].mOnNodesUpdateCompletion = createOnNodesUpdateLambda(INVALID_HANDLE, MegaNode::CHANGE_TYPE_NEW, check1);
    MegaHandle copiedNodeHandleInRubbish = INVALID_HANDLE;
    std::unique_ptr<MegaNode> rubbishNode(megaApi[1]->getRubbishNode());
    std::unique_ptr<MegaNode> copiedNode(megaApi[1]->getNodeByHandle(copiedNodeHandle));
    ASSERT_EQ(API_OK, doCopyNode(1, &copiedNodeHandleInRubbish, copiedNode.get(), rubbishNode.get())) << "Copying shared file (not owned) to Rubbish bin failed";
    ASSERT_TRUE( waitForResponse(&check1) )   // at the target side (auxiliar account)
            << "Node update not received after " << maxTimeout << " seconds";

    resetOnNodeUpdateCompletionCBs();
    ++ownedNodeCount;
    ASSERT_EQ(check1, true);

    mApi[0].mOnNodesUpdateCompletion = createOnNodesUpdateLambda(copiedNodeHandle, MegaNode::CHANGE_TYPE_REMOVED, check1);
    mApi[1].mOnNodesUpdateCompletion = createOnNodesUpdateLambda(copiedNodeHandle, MegaNode::CHANGE_TYPE_REMOVED, check2);
    MegaHandle copyAndDeleteNodeHandle = INVALID_HANDLE;

    copiedNode.reset(megaApi[0]->getNodeByHandle(copiedNodeHandle));
    EXPECT_EQ(API_OK, doMoveNode(1, &copyAndDeleteNodeHandle, copiedNode.get(), rubbishNode.get())) << "Moving shared file, same name and fingerprint";

    ASSERT_EQ(megaApi[1]->getNodeByHandle(copiedNodeHandle), nullptr) << "Move didn't delete source file";
    ASSERT_TRUE( waitForResponse(&check1) )   // at the target side (main account)
            << "Node update not received after " << maxTimeout << " seconds";
    ASSERT_TRUE( waitForResponse(&check2) )   // at the target side (auxiliar account)
            << "Node update not received after " << maxTimeout << " seconds";

    resetOnNodeUpdateCompletionCBs();
    --inSharedNodeCount;
    ASSERT_EQ(check1, true);
    ASSERT_EQ(check2, true);


    // --- Move shared file (not owned) to Rubbish bin ---
    MegaHandle movedNodeHandle = UNDEF;
    mApi[0].mOnNodesUpdateCompletion = createOnNodesUpdateLambda(hfile2, MegaNode::CHANGE_TYPE_REMOVED, check1);
    mApi[1].mOnNodesUpdateCompletion = createOnNodesUpdateLambda(hfile2, MegaNode::CHANGE_TYPE_REMOVED, check2);
    ASSERT_EQ(API_OK, doMoveNode(1, &movedNodeHandle, std::unique_ptr<MegaNode>(megaApi[0]->getNodeByHandle(hfile2)).get(), rubbishNode.get()))
            << "Moving shared file (not owned) to Rubbish bin failed";
    ASSERT_TRUE( waitForResponse(&check1) )   // at the target side (main account)
            << "Node update not received after " << maxTimeout << " seconds";
    ASSERT_TRUE( waitForResponse(&check2) )   // at the target side (auxiliar account)
            << "Node update not received after " << maxTimeout << " seconds";
    // important to reset
    resetOnNodeUpdateCompletionCBs();
    --inSharedNodeCount;
    ++ownedNodeCount;
    ASSERT_EQ(check1, true);
    ASSERT_EQ(check2, true);

    // --- Test that file in Rubbish bin can be restored ---

    // Different handle! the node must have been copied due to differing accounts
    std::unique_ptr<MegaNode> nodeMovedFile(megaApi[1]->getNodeByHandle(movedNodeHandle));
    ASSERT_EQ(nodeMovedFile->getRestoreHandle(), hfolder2) << "Incorrect restore handle for file in Rubbish Bin";

    // check the corresponding user alert
    ASSERT_TRUE(checkAlert(1, "New shared folder from " + mApi[0].email, mApi[0].email + ":Shared-folder"));

    // add folders under the share
    char foldernameA[64] = "dummyname1";
    mApi[0].mOnNodesUpdateCompletion = createOnNodesUpdateLambda(INVALID_HANDLE, MegaNode::CHANGE_TYPE_NEW, check1);
    mApi[1].mOnNodesUpdateCompletion = createOnNodesUpdateLambda(INVALID_HANDLE, MegaNode::CHANGE_TYPE_NEW, check2);
    MegaHandle dummyhandle1 = createFolder(0, foldernameA, std::unique_ptr<MegaNode>{megaApi[0]->getNodeByHandle(hfolder2)}.get());
    ASSERT_NE(dummyhandle1, UNDEF);
    ASSERT_TRUE(waitForResponse(&check1))   // at the target side (main account)
        << "Node update not received after " << maxTimeout << " seconds";
    ASSERT_TRUE(waitForResponse(&check2))   // at the target side (auxiliar account)
        << "Node update not received after " << maxTimeout << " seconds";
    // important to reset
    resetOnNodeUpdateCompletionCBs();
    ASSERT_EQ(check1, true);
    ASSERT_EQ(check2, true);

    char foldernameB[64] = "dummyname2";
    mApi[0].mOnNodesUpdateCompletion = createOnNodesUpdateLambda(INVALID_HANDLE, MegaNode::CHANGE_TYPE_NEW, check1);
    mApi[1].mOnNodesUpdateCompletion = createOnNodesUpdateLambda(INVALID_HANDLE, MegaNode::CHANGE_TYPE_NEW, check2);
    MegaHandle dummyhandle2 = createFolder(0, foldernameB, std::unique_ptr<MegaNode>{megaApi[0]->getNodeByHandle(hfolder2)}.get());
    ASSERT_NE(dummyhandle2, UNDEF);
    ASSERT_TRUE(waitForResponse(&check1))   // at the target side (main account)
        << "Node update not received after " << maxTimeout << " seconds";
    ASSERT_TRUE(waitForResponse(&check2))   // at the target side (auxiliar account)
        << "Node update not received after " << maxTimeout << " seconds";
    // important to reset
    resetOnNodeUpdateCompletionCBs();
    inSharedNodeCount += 2;
    unsigned long long nodesAtFolderDummyname2 = 1; // Take account own node
    ASSERT_EQ(check1, true);
    ASSERT_EQ(check2, true);


    auto nodeCountAfterInSharesAddedDummyFolders = megaApi[1]->getAccurateNumNodes();
    ASSERT_EQ(ownedNodeCount + inSharedNodeCount, nodeCountAfterInSharesAddedDummyFolders);

    // check the corresponding user alert
    EXPECT_TRUE(checkAlert(1, mApi[0].email + " added 2 folders", std::unique_ptr<MegaNode>{megaApi[0]->getNodeByHandle(hfolder2)}->getHandle(), 2, dummyhandle1));

    // add 2 more files to the share
    mApi[0].mOnNodesUpdateCompletion = createOnNodesUpdateLambda(INVALID_HANDLE, MegaNode::CHANGE_TYPE_NEW, check1);
    mApi[1].mOnNodesUpdateCompletion = createOnNodesUpdateLambda(INVALID_HANDLE, MegaNode::CHANGE_TYPE_NEW, check2);
    ASSERT_EQ(MegaError::API_OK, doStartUpload(0, nullptr, PUBLICFILE.data(), std::unique_ptr<MegaNode>{megaApi[0]->getNodeByHandle(dummyhandle1)}.get(),
                                               nullptr /*fileName*/,
                                               ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
                                               nullptr /*appData*/,
                                               false   /*isSourceTemporary*/,
                                               false   /*startFirst*/,
                                               nullptr /*cancelToken*/)) << "Cannot upload a test file";

    ASSERT_TRUE(waitForResponse(&check1))   // at the target side (main account)
        << "Node update not received after " << maxTimeout << " seconds";
    ASSERT_TRUE(waitForResponse(&check2))   // at the target side (auxiliar account)
        << "Node update not received after " << maxTimeout << " seconds";
    // important to reset
    resetOnNodeUpdateCompletionCBs();
    ASSERT_EQ(check1, true);
    ASSERT_EQ(check2, true);

    ++inSharedNodeCount;
    mApi[0].mOnNodesUpdateCompletion = createOnNodesUpdateLambda(INVALID_HANDLE, MegaNode::CHANGE_TYPE_NEW, check1);
    mApi[1].mOnNodesUpdateCompletion = createOnNodesUpdateLambda(INVALID_HANDLE, MegaNode::CHANGE_TYPE_NEW, check2);
    ASSERT_EQ(MegaError::API_OK, doStartUpload(0, nullptr, PUBLICFILE.data(), std::unique_ptr<MegaNode>{megaApi[0]->getNodeByHandle(dummyhandle2)}.get(),
                                               nullptr /*fileName*/,
                                               ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
                                               nullptr /*appData*/,
                                               false   /*isSourceTemporary*/,
                                               false   /*startFirst*/,
                                               nullptr /*cancelToken*/)) << "Cannot upload a test file";

    ASSERT_TRUE(waitForResponse(&check1))   // at the target side (main account)
        << "Node update not received after " << maxTimeout << " seconds";
    ASSERT_TRUE(waitForResponse(&check2))   // at the target side (auxiliar account)
        << "Node update not received after " << maxTimeout << " seconds";
    // important to reset
    resetOnNodeUpdateCompletionCBs();
    ++inSharedNodeCount;
    ++nodesAtFolderDummyname2;
    ASSERT_EQ(check1, true);
    ASSERT_EQ(check2, true);

    auto nodeCountAfterInSharesAddedDummyFile = megaApi[1]->getAccurateNumNodes();
    ASSERT_EQ(ownedNodeCount + inSharedNodeCount, nodeCountAfterInSharesAddedDummyFile);

    // move a folder outside share
    mApi[0].mOnNodesUpdateCompletion = createOnNodesUpdateLambda(dummyhandle1, MegaNode::CHANGE_TYPE_PARENT, check1);
    mApi[1].mOnNodesUpdateCompletion = createOnNodesUpdateLambda(dummyhandle1, MegaNode::CHANGE_TYPE_REMOVED, check2);
    std::unique_ptr<MegaNode> dummyNode1(megaApi[0]->getNodeByHandle(dummyhandle1));
    megaApi[0]->moveNode(dummyNode1.get(), rootnode.get());
    ASSERT_TRUE(waitForResponse(&check1))   // at the target side (main account)
        << "Node update not received after " << maxTimeout << " seconds";
    ASSERT_TRUE(waitForResponse(&check2))   // at the target side (auxiliar account)
        << "Node update not received after " << maxTimeout << " seconds";
    // important to reset
    resetOnNodeUpdateCompletionCBs();
    inSharedNodeCount -= 2;
    ASSERT_EQ(check1, true);
    ASSERT_EQ(check2, true);

    auto nodeCountAfterInSharesRemovedDummyFolder1 = megaApi[1]->getAccurateNumNodes();
    ASSERT_EQ(ownedNodeCount + inSharedNodeCount, nodeCountAfterInSharesRemovedDummyFolder1);

    // add a nested share
    std::unique_ptr<MegaNode> dummyNode2(megaApi[0]->getNodeByHandle(dummyhandle2));
    mApi[0].mOnNodesUpdateCompletion = createOnNodesUpdateLambda(dummyhandle2, MegaNode::CHANGE_TYPE_OUTSHARE, check1);
    mApi[1].mOnNodesUpdateCompletion = createOnNodesUpdateLambda(dummyhandle2, MegaNode::CHANGE_TYPE_INSHARE, check2);
    ASSERT_NO_FATAL_FAILURE(shareFolder(dummyNode2.get(), mApi[1].email.data(), MegaShare::ACCESS_FULL));
    ASSERT_TRUE(waitForResponse(&check1))   // at the target side (main account)
        << "Node update not received after " << maxTimeout << " seconds";
    ASSERT_TRUE(waitForResponse(&check2))   // at the target side (auxiliar account)
        << "Node update not received after " << maxTimeout << " seconds";
    // important to reset
    resetOnNodeUpdateCompletionCBs();
    ASSERT_EQ(check1, true);
    ASSERT_EQ(check2, true);

    // number of nodes should not change, because this node is a nested share
    auto nodeCountAfterInSharesAddedNestedSubfolder = megaApi[1]->getAccurateNumNodes();
    ASSERT_EQ(ownedNodeCount + inSharedNodeCount, nodeCountAfterInSharesAddedNestedSubfolder);

    // Stop share main folder (Shared-folder)
    mApi[0].mOnNodesUpdateCompletion = createOnNodesUpdateLambda(n1->getHandle(), MegaNode::CHANGE_TYPE_OUTSHARE, check1);
    mApi[1].mOnNodesUpdateCompletion = createOnNodesUpdateLambda(n1->getHandle(), MegaNode::CHANGE_TYPE_REMOVED, check2);
    ASSERT_NO_FATAL_FAILURE(shareFolder(n1.get(), mApi[1].email.data(), MegaShare::ACCESS_UNKNOWN));
    ASSERT_TRUE(waitForResponse(&check1))   // at the target side (main account)
        << "Node update not received after " << maxTimeout << " seconds";
    ASSERT_TRUE(waitForResponse(&check2))   // at the target side (auxiliar account)
        << "Node update not received after " << maxTimeout << " seconds";
    // important to reset
    resetOnNodeUpdateCompletionCBs();
    ASSERT_EQ(check1, true);
    ASSERT_EQ(check2, true);

    // number of nodes own cloud + nodes at nested in-share
    auto nodeCountAfterRemoveMainInshare = megaApi[1]->getAccurateNumNodes();
    ASSERT_EQ(ownedNodeCount + nodesAtFolderDummyname2, nodeCountAfterRemoveMainInshare);

    // Share again main folder (Shared-folder)
    mApi[0].mOnNodesUpdateCompletion = createOnNodesUpdateLambda(n1->getHandle(), MegaNode::CHANGE_TYPE_OUTSHARE, check1);
    mApi[1].mOnNodesUpdateCompletion = createOnNodesUpdateLambda(n1->getHandle(), MegaNode::CHANGE_TYPE_INSHARE, check2);
    ASSERT_NO_FATAL_FAILURE(shareFolder(n1.get(), mApi[1].email.data(), MegaShare::ACCESS_FULL));
    ASSERT_TRUE(waitForResponse(&check1))   // at the target side (main account)
        << "Node update not received after " << maxTimeout << " seconds";
    ASSERT_TRUE(waitForResponse(&check2))   // at the target side (auxiliar account)
        << "Node update not received after " << maxTimeout << " seconds";
    // important to reset
    resetOnNodeUpdateCompletionCBs();
    ASSERT_EQ(check1, true);
    ASSERT_EQ(check2, true);

    // number of nodes own cloud + nodes at nested in-share
    auto nodeCountAfterShareN1 = megaApi[1]->getAccurateNumNodes();
    ASSERT_EQ(ownedNodeCount + inSharedNodeCount, nodeCountAfterShareN1);

    // remove nested share
    mApi[0].mOnNodesUpdateCompletion = createOnNodesUpdateLambda(dummyNode2->getHandle(), MegaNode::CHANGE_TYPE_OUTSHARE, check1);
    mApi[1].mOnNodesUpdateCompletion = createOnNodesUpdateLambda(dummyNode2->getHandle(), MegaNode::CHANGE_TYPE_INSHARE, check2);
    ASSERT_NO_FATAL_FAILURE(shareFolder(dummyNode2.get(), mApi[1].email.data(), MegaShare::ACCESS_UNKNOWN));
    ASSERT_TRUE(waitForResponse(&check1))   // at the target side (main account)
        << "Node update not received after " << maxTimeout << " seconds";
    ASSERT_TRUE(waitForResponse(&check2))   // at the target side (auxiliar account)
        << "Node update not received after " << maxTimeout << " seconds";

    // important to reset
    resetOnNodeUpdateCompletionCBs();
    ASSERT_EQ(check1, true);
    ASSERT_EQ(check2, true);

    // number of nodes should not change, because this node was a nested share
    auto nodeCountAfterInSharesRemovedNestedSubfolder = megaApi[1]->getAccurateNumNodes();
    ASSERT_EQ(ownedNodeCount + inSharedNodeCount, nodeCountAfterInSharesRemovedNestedSubfolder);

    // --- Modify the access level of an outgoing share ---
    mApi[0].mOnNodesUpdateCompletion = createOnNodesUpdateLambda(hfolder1, MegaNode::CHANGE_TYPE_OUTSHARE, check1);
    mApi[1].mOnNodesUpdateCompletion = createOnNodesUpdateLambda(hfolder1, MegaNode::CHANGE_TYPE_INSHARE, check2);

    ASSERT_NO_FATAL_FAILURE(shareFolder(std::unique_ptr<MegaNode>(megaApi[0]->getNodeByHandle(hfolder1)).get(),
                            mApi[1].email.c_str(), MegaShare::ACCESS_READWRITE) );
    ASSERT_TRUE( waitForResponse(&check1) )   // at the target side (main account)
            << "Node update not received after " << maxTimeout << " seconds";
    ASSERT_TRUE( waitForResponse(&check2) )   // at the target side (auxiliar account)
            << "Node update not received after " << maxTimeout << " seconds";

    // important to reset
    resetOnNodeUpdateCompletionCBs();
    ASSERT_EQ(check1, true);
    ASSERT_EQ(check2, true);

    contact.reset(megaApi[1]->getContact(mApi[0].email.c_str()));
    nl.reset(megaApi[1]->getInShares(contact.get()));
    ASSERT_EQ(1, nl->size()) << "Incoming share not received in auxiliar account";
    n = nl->get(0);

    ASSERT_EQ(API_OK,
              megaApi[1]->checkAccessErrorExtended(n, MegaShare::ACCESS_READWRITE)->getErrorCode())
        << "Wrong access level of incoming share";

    // --- Sharee leaves the inshare ---
    // Testing APs caused by actions done in the sharee account.
    unique_ptr<MegaNode> inshareRootNode(megaApi[1]->getNodeByHandle(hfolder1));

    mApi[0].mOnNodesUpdateCompletion = createOnNodesUpdateLambda(hfolder1, MegaNode::CHANGE_TYPE_OUTSHARE, check1);
    mApi[1].mOnNodesUpdateCompletion = createOnNodesUpdateLambda(hfolder1, MegaNode::CHANGE_TYPE_REMOVED, check2);
    ASSERT_NO_FATAL_FAILURE(doDeleteNode(1, inshareRootNode.get())); // Delete an inshare root node to leave the inconming share
    ASSERT_TRUE( waitForResponse(&check1) )   // at the target side (main account)
        << "Node update not received after " << maxTimeout << " seconds";
    ASSERT_TRUE( waitForResponse(&check2) )   // at the target side (auxiliar account)
        << "Node update not received after " << maxTimeout << " seconds";
    // important to reset
    resetOnNodeUpdateCompletionCBs();
    ASSERT_EQ(check1, true);
    ASSERT_EQ(check2, true);

    sl.reset(megaApi[0]->getOutShares());
    ASSERT_EQ(0, sl->size())
        << "Leaving the inshare failed. Outshare is still active in the first account.";

    contact.reset(megaApi[1]->getContact(mApi[0].email.c_str()));
    nl.reset(megaApi[1]->getInShares(contact.get()));
    ASSERT_EQ(0, nl->size())
        << "Leaving the inshare failed. Inshare is still active in the second account.";

    // Number of nodes should be the ones in the account only.
    auto nodeCountAfterShareeLeavesShare = megaApi[1]->getNumNodes();
    ASSERT_EQ(ownedNodeCount, nodeCountAfterShareeLeavesShare);

    // --- Share again the main folder ---
    mApi[0].mOnNodesUpdateCompletion = createOnNodesUpdateLambda(hfolder1, MegaNode::CHANGE_TYPE_OUTSHARE, check1);
    mApi[1].mOnNodesUpdateCompletion = createOnNodesUpdateLambda(hfolder1, MegaNode::CHANGE_TYPE_INSHARE, check2);
    ASSERT_NO_FATAL_FAILURE(shareFolder(n1.get(), mApi[1].email.data(), MegaShare::ACCESS_FULL));
    ASSERT_TRUE(waitForResponse(&check1))   // at the target side (main account)
        << "Node update not received after " << maxTimeout << " seconds";
    ASSERT_TRUE(waitForResponse(&check2))   // at the target side (auxiliar account)
        << "Node update not received after " << maxTimeout << " seconds";
    // important to reset
    resetOnNodeUpdateCompletionCBs();
    ASSERT_EQ(check1, true);
    ASSERT_EQ(check2, true);

    sl.reset(megaApi[0]->getOutShares());
    ASSERT_EQ(1, sl->size()) << "Outgoing share failed. Sharing again after sharee left the share.";

    // Wait for the inshare node to be decrypted
    ASSERT_TRUE(WaitFor([this, &n1]() { return unique_ptr<MegaNode>(megaApi[1]->getNodeByHandle(n1->getHandle()))->isNodeKeyDecrypted(); }, 60*1000));

    contact.reset(megaApi[1]->getContact(mApi[0].email.c_str()));
    nl.reset(megaApi[1]->getInShares(contact.get()));
    ASSERT_EQ(1, nl->size()) << "Incoming share failed. Sharing again after sharee left the share.";

    // Number of nodes restored after sharing again.
    auto nodeCountAfterShareAgainIfShareeLeaves = megaApi[1]->getNumNodes();
    ASSERT_EQ(ownedNodeCount + inSharedNodeCount, nodeCountAfterShareAgainIfShareeLeaves);

    // --- Revoke access to an outgoing share ---

    mApi[0].mOnNodesUpdateCompletion = createOnNodesUpdateLambda(hfolder1, MegaNode::CHANGE_TYPE_OUTSHARE, check1);
    mApi[1].mOnNodesUpdateCompletion = createOnNodesUpdateLambda(hfolder1, MegaNode::CHANGE_TYPE_REMOVED, check2);
    ASSERT_NO_FATAL_FAILURE( shareFolder(n1.get(), mApi[1].email.c_str(), MegaShare::ACCESS_UNKNOWN) );
    ASSERT_TRUE( waitForResponse(&check1) )   // at the target side (main account)
            << "Node update not received after " << maxTimeout << " seconds";
    ASSERT_TRUE( waitForResponse(&check2) )   // at the target side (auxiliar account)
            << "Node update not received after " << maxTimeout << " seconds";

    // important to reset
    resetOnNodeUpdateCompletionCBs();
    ASSERT_EQ(check1, true);
    ASSERT_EQ(check2, true);

    sl.reset(megaApi[0]->getOutShares());
    ASSERT_EQ(0, sl->size()) << "Outgoing share revocation failed";
    // Test another interface
    sl.reset(megaApi[0]->getOutShares(n1.get()));
    ASSERT_EQ(0, sl->size()) << "Outgoing share revocation failed";

    contact.reset(megaApi[1]->getContact(mApi[0].email.c_str()));
    nl.reset(megaApi[1]->getInShares(contact.get()));
    ASSERT_EQ(0, nl->size()) << "Incoming share revocation failed";

    // check the corresponding user alert
    {
        MegaUserAlertList* list = megaApi[1]->getUserAlerts();
        ASSERT_TRUE(list->size() > 0);
        MegaUserAlert* a = list->get(list->size() - 1);
        ASSERT_STRCASEEQ(a->getTitle(), ("Access to folders shared by " + mApi[0].email + " was removed").c_str());
        ASSERT_STRCASEEQ(a->getPath(), (mApi[0].email + ":Shared-folder").c_str());
        ASSERT_NE(a->getNodeHandle(), UNDEF);
        delete list;
    }

    auto nodeCountAfterRevokedSharesAccess = megaApi[1]->getAccurateNumNodes();
    ASSERT_EQ(ownedNodeCount, nodeCountAfterRevokedSharesAccess);

    // --- Get pending outgoing shares ---

    char emailfake[64];
    srand(unsigned(time(NULL)));
    snprintf(emailfake, sizeof(emailfake), "%d@nonexistingdomain.com", rand()%1000000);
    // carefull, antispam rejects too many tries without response for the same address

    auto node = std::unique_ptr<MegaNode>{megaApi[0]->getNodeByHandle(hfolder2)};

    mApi[0].contactRequestUpdated = false;
    mApi[0].mOnNodesUpdateCompletion = createOnNodesUpdateLambda(hfolder2, MegaNode::CHANGE_TYPE_PENDINGSHARE, check1);

    ASSERT_NO_FATAL_FAILURE(shareFolder(node.get(), emailfake, MegaShare::ACCESS_FULL));
    ASSERT_TRUE( waitForResponse(&check1) )   // at the target side (main account)
            << "Node update not received after " << maxTimeout << " seconds";
    ASSERT_TRUE( waitForResponse(&mApi[0].contactRequestUpdated) )   // at the target side (main account)
            << "Contact request update not received after " << maxTimeout << " seconds";

    // important to reset
    resetOnNodeUpdateCompletionCBs();
    ASSERT_EQ(check1, true);

    sl.reset(megaApi[0]->getPendingOutShares(node.get()));
    ASSERT_EQ(1, sl->size()) << "Pending outgoing share failed";
    // Test another interface
    sl.reset(megaApi[0]->getOutShares(node.get()));
    ASSERT_EQ(1, sl->size()) << "Pending outgoing share failed";
    s = sl->get(0);
    node.reset(megaApi[0]->getNodeByHandle(s->getNodeHandle()));

    ASSERT_FALSE(node->isShared()) << "Node is already shared, must be pending";
    ASSERT_FALSE(node->isOutShare()) << "Node is already shared, must be pending";
    ASSERT_FALSE(node->isInShare()) << "Node is already shared, must be pending";

    mApi[0].mOnNodesUpdateCompletion = createOnNodesUpdateLambda(dummyNode1->getHandle(), MegaNode::CHANGE_TYPE_PENDINGSHARE, check1);
    ASSERT_NO_FATAL_FAILURE( shareFolder(dummyNode1.get(), emailfake, MegaShare::ACCESS_FULL) );
    ASSERT_TRUE( waitForResponse(&check1) )   // at the target side (main account)
            << "Node update not received after " << maxTimeout << " seconds";

    // important to reset
    resetOnNodeUpdateCompletionCBs();
    ASSERT_EQ(check1, true);

    sl.reset(megaApi[0]->getPendingOutShares());
    ASSERT_EQ(2, sl->size()) << "Pending outgoing share failed";
    // Test another interface
    sl.reset(megaApi[0]->getOutShares());
    ASSERT_EQ(2, sl->size()) << "Pending outgoing share failed";

    // --- Create a file public link ---

    ASSERT_EQ(API_OK, synchronousGetSpecificAccountDetails(0, true, true, true)) << "Cannot get account details";

    std::unique_ptr<MegaNode> nfile1{megaApi[0]->getNodeByHandle(hfile1)};

    string nodelink3 = createPublicLink(0, nfile1.get(), 0, maxTimeout, mApi[0].accountDetails->getProLevel() == 0);
    // The created link is stored in this->link at onRequestFinish()

    // Get a fresh snapshot of the node and check it's actually exported
    nfile1 = std::unique_ptr<MegaNode>{megaApi[0]->getNodeByHandle(hfile1)};
    ASSERT_TRUE(nfile1->isExported()) << "Node is not exported, must be exported";
    ASSERT_FALSE(nfile1->isTakenDown()) << "Public link is taken down, it mustn't";

    // Make sure that search functionality finds it
    std::unique_ptr<MegaSearchFilter> filterResults(MegaSearchFilter::createInstance());
    filterResults->byName(nfile1->getName());
    filterResults->byLocation(MegaApi::SEARCH_TARGET_PUBLICLINK);
    std::unique_ptr<MegaNodeList> foundByLink(megaApi[0]->search(filterResults.get()));
    ASSERT_TRUE(foundByLink);
    ASSERT_EQ(foundByLink->size(), 1);
    ASSERT_EQ(foundByLink->get(0)->getHandle(), nfile1->getHandle());

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

    std::unique_ptr<MegaNode> nfolder1(megaApi[0]->getNodeByHandle(hfolder1));

    string nodelink5 = createPublicLink(0, nfolder1.get(), 0, maxTimeout, mApi[0].accountDetails->getProLevel() == 0);
    // The created link is stored in this->link at onRequestFinish()

    // Get a fresh snapshot of the node and check it's actually exported
    nfolder1.reset(megaApi[0]->getNodeByHandle(hfolder1));
    ASSERT_TRUE(nfolder1->isExported()) << "Node is not exported, must be exported";
    ASSERT_FALSE(nfolder1->isTakenDown()) << "Public link is taken down, it mustn't";

    nfolder1.reset(megaApi[0]->getNodeByHandle(hfolder1));
    ASSERT_STREQ(nodelink5.c_str(), std::unique_ptr<char[]>(nfolder1->getPublicLink()).get()) << "Wrong public link from MegaNode";

    // Make sure that search functionality finds it
    filterResults.reset(MegaSearchFilter::createInstance());
    filterResults->byName(nfolder1->getName());
    filterResults->byLocation(MegaApi::SEARCH_TARGET_PUBLICLINK);
    foundByLink.reset(megaApi[0]->search(filterResults.get()));
    ASSERT_TRUE(foundByLink);
    ASSERT_EQ(foundByLink->size(), 1);
    ASSERT_EQ(foundByLink->get(0)->getHandle(), nfolder1->getHandle());

    // Regenerate the same link should not trigger a new request
    string nodelink6 = createPublicLink(0, nfolder1.get(), 0, maxTimeout, mApi[0].accountDetails->getProLevel() == 0);
    ASSERT_STREQ(nodelink5.c_str(), nodelink6.c_str()) << "Wrong public link after link update";


    // --- Import folder public link ---
    const auto [email, pass] = getEnvVarAccounts().getVarValues(2);
    ASSERT_FALSE(email.empty() || pass.empty());
    mApi.resize(3);
    megaApi.resize(3);
    configureTestInstance(2, email, pass);
    auto loginFolderTracker = asyncRequestLoginToFolder(2, nodelink6.c_str());
    ASSERT_EQ(loginFolderTracker->waitForResult(), API_OK) << "Failed to login to folder " << nodelink6;
    ASSERT_NO_FATAL_FAILURE(fetchnodes(2));
    std::unique_ptr<MegaNode> folderNodeToImport(megaApi[2]->getRootNode());
    ASSERT_TRUE(folderNodeToImport) << "Failed to get folder node to import from link " << nodelink6;
    std::unique_ptr<MegaNode> authorizedFolderNode(megaApi[2]->authorizeNode(folderNodeToImport.get()));
    ASSERT_TRUE(authorizedFolderNode) << "Failed to authorize folder node from link " << nodelink6;
    ASSERT_TRUE(authorizedFolderNode->getChildren())
        << "Authorized folder node children list is null but it should not";
    ASSERT_EQ(megaApi[2]->getNumChildren(folderNodeToImport.get()),
              authorizedFolderNode->getChildren()->size())
        << "Different number of child nodes after authorizing the folder node";
    logout(2, false, 20);

    auto loginTracker = asyncRequestLogin(2, email.c_str(), pass.c_str());
    ASSERT_EQ(loginTracker->waitForResult(), API_OK) << "Failed to login with " << email;
    ASSERT_NO_FATAL_FAILURE(fetchnodes(2));
    std::unique_ptr<MegaNode> rootNode2(megaApi[2]->getRootNode());
    RequestTracker nodeCopyTracker(megaApi[2].get());
    megaApi[2]->copyNode(authorizedFolderNode.get(), rootNode2.get(), nullptr, &nodeCopyTracker);
    EXPECT_EQ(nodeCopyTracker.waitForResult(), API_OK) << "Failed to copy node to import";
    std::unique_ptr<MegaNode> importedNode(megaApi[2]->getNodeByPath(authorizedFolderNode->getName(), rootNode2.get()));
    EXPECT_TRUE(importedNode) << "Imported node not found";
    std::unique_ptr<MegaNode> authorizedImportedNode(megaApi[2]->authorizeNode(importedNode.get()));
    EXPECT_TRUE(authorizedImportedNode) << "Failed to authorize imported node";
    EXPECT_TRUE(authorizedImportedNode->getChildren())
        << "Authorized imported node children list is null but it should not";
    ASSERT_EQ(authorizedFolderNode->getChildren()->size(),
              authorizedImportedNode->getChildren()->size())
        << "Not all child nodes have been imported";
}


/**
 * @brief TEST_F SdkTestShares3
 *
 * - Login 3 account
 * - Create tree
 * - Create new UserB and UserC contacts for UserA to share to
 * - User1 shares Folder1 with UserB, and Folder1_1 with UserC
 * - User1 locallogout
 * - User3 add File1 to Folder1_1
 * - Check that UserB sees File1 as NO_KEY
 * - User2 locallogout and login with session
 * - Check that UserB still sees File1 as NO_KEY
 * - UserA login
 * - Check that UserB sees File1 with its real name
 * - UserB locallogout and login with session
 * - UserB load File1 undecrypted
 */
TEST_F(SdkTest, DISABLED_SdkTestShares3)
{
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(3));

    // --- Create tree ---
    //  |--Folder1
    //    |--Folder1_1

    std::unique_ptr<MegaNode> rootnode{ megaApi[0]->getRootNode() };
    char foldername1[64] = "Folder1";
    MegaHandle hfolder1 = createFolder(0, foldername1, rootnode.get());
    ASSERT_NE(hfolder1, UNDEF);

    std::unique_ptr<MegaNode> n1{ megaApi[0]->getNodeByHandle(hfolder1) };
    ASSERT_NE(n1, nullptr);

    char foldername1_1[64] = "Folder1_1";
    MegaHandle hfolder1_1 = createFolder(0, foldername1_1, std::unique_ptr<MegaNode>{megaApi[0]->getNodeByHandle(hfolder1)}.get());
    ASSERT_NE(hfolder1_1, UNDEF);

    std::unique_ptr<MegaNode> n1_1{ megaApi[0]->getNodeByHandle(hfolder1_1) };
    ASSERT_NE(n1_1, nullptr);


    // --- Create new contacts to share to and verify credentials ---

    ASSERT_EQ(MegaError::API_OK, synchronousInviteContact(0, mApi[1].email.c_str(), "Contact request A to B", MegaContactRequest::INVITE_ACTION_ADD));
    ASSERT_EQ(MegaError::API_OK, synchronousInviteContact(0, mApi[2].email.c_str(), "Contact request A to C", MegaContactRequest::INVITE_ACTION_ADD));

    ASSERT_TRUE(WaitFor([this]() {return unique_ptr<MegaContactRequestList>(megaApi[1]->getIncomingContactRequests())->size() == 1
                                      && unique_ptr<MegaContactRequestList>(megaApi[2]->getIncomingContactRequests())->size() == 1; }, 60000));
    ASSERT_NO_FATAL_FAILURE(getContactRequest(1, false));
    ASSERT_NO_FATAL_FAILURE(getContactRequest(2, false));

    ASSERT_EQ(MegaError::API_OK, synchronousReplyContactRequest(1, mApi[1].cr.get(), MegaContactRequest::REPLY_ACTION_ACCEPT));
    ASSERT_EQ(MegaError::API_OK, synchronousReplyContactRequest(2, mApi[2].cr.get(), MegaContactRequest::REPLY_ACTION_ACCEPT));

    WaitMillisec(3000);

    if (gManualVerification)
    {
        if (!areCredentialsVerified(0, mApi[1].email)) {ASSERT_NO_FATAL_FAILURE(verifyCredentials(0, mApi[1].email));}
        if (!areCredentialsVerified(0, mApi[2].email)) {ASSERT_NO_FATAL_FAILURE(verifyCredentials(0, mApi[2].email));}
        if (!areCredentialsVerified(1, mApi[0].email)) {ASSERT_NO_FATAL_FAILURE(verifyCredentials(1, mApi[0].email));}
        if (!areCredentialsVerified(2, mApi[0].email)) {ASSERT_NO_FATAL_FAILURE(verifyCredentials(2, mApi[0].email));}
    }

    // --- User1 shares Folder1 with UserB, and Folder1_1 with UserC ---

    ASSERT_NO_FATAL_FAILURE(shareFolder(n1.get(), mApi[1].email.c_str(), MegaShare::ACCESS_FULL));
    ASSERT_NO_FATAL_FAILURE(shareFolder(n1_1.get(), mApi[2].email.c_str(), MegaShare::ACCESS_FULL));

    ASSERT_TRUE(WaitFor([this]() { return unique_ptr<MegaShareList>(megaApi[1]->getInSharesList())->size() == 1
                                       && unique_ptr<MegaShareList>(megaApi[2]->getInSharesList())->size() == 1; }, 60000));

    // Wait for the inshare nodes to be decrypted
    ASSERT_TRUE(WaitFor([this, &n1]() { return unique_ptr<MegaNode>(megaApi[1]->getNodeByHandle(n1->getHandle()))->isNodeKeyDecrypted(); }, 60*1000));
    ASSERT_TRUE(WaitFor([this, &n1_1]() { return unique_ptr<MegaNode>(megaApi[2]->getNodeByHandle(n1_1->getHandle()))->isNodeKeyDecrypted(); }, 60*1000));

    unique_ptr<MegaNodeList> nl2(megaApi[1]->getInShares(megaApi[1]->getContact(mApi[0].email.c_str())));
    unique_ptr<MegaNodeList> nl3(megaApi[2]->getInShares(megaApi[2]->getContact(mApi[0].email.c_str())));

    ASSERT_EQ(1, nl2->size());
    ASSERT_EQ(1, nl3->size());


    // --- UserA locallogout ---

    string sessionA = unique_ptr<char[]>(dumpSession()).get();
    locallogout();


    // --- UserC add File1 to Folder1_1 ---

    static constexpr char file1[] = "File1.txt";
    createFile(file1, false);   // not a large file since don't need to test transfers here
    ASSERT_EQ(MegaError::API_OK, doStartUpload(2, nullptr, file1, n1_1.get(),
                                               nullptr /*fileName*/,
                                               ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
                                               nullptr /*appData*/,
                                               false   /*isSourceTemporary*/,
                                               false   /*startFirst*/,
                                               nullptr /*cancelToken*/)) << "Cannot upload test file";

    // --- Check that UserB sees File1 as NO_KEY ---

    ASSERT_TRUE(WaitFor([this, &n1_1]()
        {
            unique_ptr<MegaNodeList> aView(megaApi[1]->getChildren(n1_1.get()));
            return aView->size() == 1;
        },
        60000));

    unique_ptr<MegaNodeList> aView(megaApi[1]->getChildren(n1_1.get()));
    ASSERT_EQ(1, aView->size());
    const char* file1Name = aView->get(0)->getName(); // for debug
    ASSERT_STREQ(file1Name, "NO_KEY");


    // --- UserB locallogout and login with session ---

    string sessionB = unique_ptr<char[]>(megaApi[1]->dumpSession()).get();
    auto logoutErr = doRequestLocalLogout(1);
    ASSERT_EQ(MegaError::API_OK, logoutErr) << "Local logout failed (error: " << logoutErr << ")";
    PerApi& target1 = mApi[1];
    target1.resetlastEvent();
    auto trackerB = asyncRequestFastLogin(1, sessionB.c_str());
    ASSERT_EQ(API_OK, trackerB->waitForResult()) << " Failed to establish a login/session for account B";


    // --- Check that UserB still sees File1 as NO_KEY ---

    ASSERT_NO_FATAL_FAILURE(fetchnodes(1)); // different behavior whether ENABLE_SYNC is On or Off
    // make sure that client is up to date (upon logout, recent changes might not be committed to DB)
    ASSERT_TRUE(WaitFor([&target1](){ return target1.lastEventsContain(MegaEvent::EVENT_NODES_CURRENT); }, 10000))
        << "Timeout expired to receive actionpackets";
    aView.reset(megaApi[1]->getChildren(n1_1.get()));
    ASSERT_STREQ(aView->get(0)->getName(), "NO_KEY");


    // --- UserA login ---

    auto trackerA = asyncRequestFastLogin(0, sessionA.c_str());
    ASSERT_EQ(API_OK, trackerA->waitForResult()) << " Failed to establish a login/session for account A";
    PerApi& target0 = mApi[0];
    target0.resetlastEvent();
    ASSERT_NO_FATAL_FAILURE(fetchnodes(0));
    ASSERT_TRUE(WaitFor([&target0](){ return target0.lastEventsContain(MegaEvent::EVENT_NODES_CURRENT); }, 10000))
        << "Timeout expired to receive actionpackets";


    // --- Check that UserB sees File1 with its real name ---

    aView.reset(megaApi[1]->getChildren(n1_1.get()));
    ASSERT_EQ(1, aView->size());
    ASSERT_STREQ(aView->get(0)->getName(), file1);


    // --- UserB locallogout and login with session ---

    sessionB = unique_ptr<char[]>(megaApi[1]->dumpSession()).get();
    logoutErr = doRequestLocalLogout(1);
    ASSERT_EQ(MegaError::API_OK, logoutErr) << "Local logout failed (error: " << logoutErr << ")";
    trackerB = asyncRequestFastLogin(1, sessionB.c_str());
    ASSERT_EQ(API_OK, trackerB->waitForResult()) << " Failed to establish a login/session for account B";


    // --- UserB load File1 undecrypted ---
    target1.resetlastEvent();
    ASSERT_NO_FATAL_FAILURE(fetchnodes(1));
    ASSERT_TRUE(WaitFor([&target1](){ return target1.lastEventsContain(MegaEvent::EVENT_NODES_CURRENT); }, 10000))
        << "Timeout expired to receive actionpackets";
    std::unique_ptr<MegaNode> nFile1{ megaApi[1]->getChildNode(n1_1.get(), file1Name) };
    ASSERT_NE(nFile1, nullptr);
}

/**
 * @brief TEST_F SdkTest.LoginToWritableFolderThenCreateSubfolder
 *
 * - Login 1 account
 * - Create a folder
 * - Create a public writable link to folder
 * - Setup guest account without login for accessing the public link
 * - Login guest account to public link
 * - Check for user alerts (should not be any, including from sc50)
 * - Create subfolder in the folder with writable link
 * - Confirm that guest account has seen the newly created subfolder
 * - Check again for user alerts (should still not be any)
 */
TEST_F(SdkTest, LoginToWritableFolderThenCreateSubfolder)
{
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    // Host: create a folder
    std::unique_ptr<MegaNode> hostRoot{megaApi[0]->getRootNode()};
    static constexpr char folderName[] = "Writable-link-folder";
    MegaHandle folderHandle = createFolder(0, folderName, hostRoot.get());
    ASSERT_NE(UNDEF, folderHandle) << "API 0: Failed to create " << folderName;
    std::unique_ptr<MegaNode> folderNode{megaApi[0]->getNodeByHandle(folderHandle)};
    ASSERT_THAT(folderNode, ::testing::NotNull());

    // Host: get acount details for Pro level
    RequestTracker accountDetailsTracker{megaApi[0].get()};
    megaApi[0]->getSpecificAccountDetails(false, false, true, -1, &accountDetailsTracker);
    ASSERT_EQ(API_OK, accountDetailsTracker.waitForResult())
        << "API 0: Failed to get account details";

    // Host: create a writable link to folder
    const bool isFreeAccount =
        mApi[0].accountDetails->getProLevel() == MegaAccountDetails::ACCOUNT_TYPE_FREE;
    string nodeLink =
        createPublicLink(0, folderNode.get(), 0, maxTimeout, isFreeAccount, true /*writable*/);

    // Guest: setup without login for accessing the public link
    unsigned guestIdx = 1;
    const auto [email, pass] = getEnvVarAccounts().getVarValues(guestIdx);
    ASSERT_FALSE(email.empty() || pass.empty());
    mApi.resize(guestIdx + 1);
    megaApi.resize(guestIdx + 1);
    configureTestInstance(guestIdx, email, pass);

    // Guest: login to writable folder
    RequestTracker loginToFolderTracker{megaApi[guestIdx].get()};
    megaApi[guestIdx]->loginToFolder(nodeLink.c_str(), &loginToFolderTracker);
    ASSERT_EQ(API_OK, loginToFolderTracker.waitForResult())
        << "API 1 (guest): Failed to login to folder " << nodeLink;
    ASSERT_NO_FATAL_FAILURE(fetchnodes(guestIdx));

    // Guest: make sure it got no user alerts, including any from sc50
    unsigned sc50Timeout = 10; // seconds
    ASSERT_FALSE(waitForResponse(&mApi[guestIdx].userAlertsUpdated, sc50Timeout))
        << "API 1 (guest): sc50 alerts after login received";
    ASSERT_EQ(mApi[guestIdx].userAlertList, nullptr) << "sc50 of guest logged into folder";
    unique_ptr<MegaUserAlertList> userAlerts(megaApi[guestIdx]->getUserAlerts());
    ASSERT_TRUE(userAlerts);
    ASSERT_EQ(userAlerts->size(), 0);

    // Guest: confirm root node of folder link
    std::unique_ptr<MegaNode> guestRoot{megaApi[guestIdx]->getRootNode()};
    ASSERT_EQ(folderHandle, guestRoot->getHandle());

    // Guest: attempt to create subfolder in writable folder
    static constexpr char subfolderName[] = "Writable-link-subfolder";
    RequestTracker createSubfolderTracker(megaApi[guestIdx].get());
    megaApi[guestIdx]->createFolder(subfolderName, guestRoot.get(), &createSubfolderTracker);
    ASSERT_EQ(API_EACCESS, createSubfolderTracker.waitForResult())
        << "API 1 (guest): Managed to create " << subfolderName;

    // Guest: reset node updates
    mApi[guestIdx].nodeUpdated = false;
    mApi[guestIdx].mOnNodesUpdateCompletion =
        [guestIdx, &guest = mApi[guestIdx]](size_t apiIndex, MegaNodeList*)
    {
        if (guestIdx == apiIndex)
            guest.nodeUpdated = true;
    };

    // Host: create subfolder in writable folder
    MegaHandle subfolderHandle = createFolder(0, subfolderName, folderNode.get());
    ASSERT_NE(UNDEF, subfolderHandle) << "API 0: Failed to create " << subfolderName;

    // Guest: Wait for node update (replacement for fetchnodes())
    ASSERT_TRUE(waitForResponse(&mApi[guestIdx].nodeUpdated))
        << "API 1 (guest): Node update not received after " << maxTimeout << " seconds";
    resetOnNodeUpdateCompletionCBs();

    // Guest: confirm the newly created node
    std::unique_ptr<MegaNode> subfolder{megaApi[guestIdx]->getNodeByHandle(subfolderHandle)};
    ASSERT_THAT(subfolder, ::testing::NotNull())
        << "API 1 (guest): Failed to find " << subfolderName;

    // Guest: check again that it got no user alerts
    ASSERT_FALSE(mApi[guestIdx].userAlertsUpdated) << "API 1 (guest): alerts received";
    ASSERT_EQ(mApi[guestIdx].userAlertList, nullptr) << "sc50";
    userAlerts.reset(megaApi[guestIdx]->getUserAlerts());
    ASSERT_TRUE(userAlerts);
    ASSERT_EQ(userAlerts->size(), 0);
}

/**
 * @brief TEST_F TestPublicFolderLinksWithShares
 *
 * 1 - create share
 * 2 - create folder link on same share
 * 3 - remove folder link
 * 4 - remove share
 * 5 - create folder link
 * 6 - remove folder link
 *
 */
TEST_F(SdkTestShares, TestPublicFolderLinksWithShares)
{
    LOG_info << "___TEST TestPublicFolderLinksWithShares";

    ASSERT_NO_FATAL_FAILURE(createNodeTrees());

    const MegaHandle hfolder = getHandle("/sharedfolder");

    // Create share on the folder
    ASSERT_NO_FATAL_FAILURE(createNewContactAndVerify());

    ASSERT_NO_FATAL_FAILURE(createOutgoingShare(hfolder));

    ASSERT_NO_FATAL_FAILURE(getInshare(hfolder));

    // Create a folder public link on the shared folder
    ASSERT_EQ(API_OK, synchronousGetSpecificAccountDetails(mSharerIndex, true, true, true))
        << "Cannot get account details";

    std::string nodeLink;
    ASSERT_NO_FATAL_FAILURE(createOnePublicLink(hfolder, nodeLink));

    ASSERT_NO_FATAL_FAILURE(importPublicLink(nodeLink));

    ASSERT_NO_FATAL_FAILURE(revokePublicLink(hfolder));

    // Revoke share on the folder
    ASSERT_NO_FATAL_FAILURE(revokeOutShares(hfolder));

    // Create the folder public link on the folder after revoking
    ASSERT_NO_FATAL_FAILURE(createOnePublicLink(hfolder, nodeLink));

    ASSERT_NO_FATAL_FAILURE(importPublicLink(nodeLink));

    ASSERT_NO_FATAL_FAILURE(revokePublicLink(hfolder));
}

/**
 * @brief TEST_F SdkTestShares.TestForeingNodeImportRemoveSensitiveFlag
 *
 * 1 - User 0 creates node tree and marks one file as sensitive
 * 2 - User 1 imports that folder via meeting link -> No sensitive expected
 * 3 - User 0 shares folder with User 1 -> User 1 sees sensitive node
 * 4 - User 1 copies to own cloud -> No sensitive in the copy
 * 5 - User 0 copies sensitive file with other name in the shared -> Copy keeps sensitive.
 * 6 - User 1 does the same -> Copy removes sensitive
 * 7 - User 1 moves to own cloud -> No sensitive expected
 * 8 - User 1 tags the moved node as sensitive and copies back to shared -> No sensitive expected
 *
 */
TEST_F(SdkTestShares, TestForeingNodeImportRemoveSensitiveFlag)
{
    const auto getSensNodes = [](const auto& api, MegaHandle handle)
    {
        std::unique_ptr<MegaSearchFilter> filter(MegaSearchFilter::createInstance());
        filter->bySensitivity(MegaSearchFilter::BOOL_FILTER_ONLY_FALSE);
        filter->byLocationHandle(handle);
        std::unique_ptr<MegaNodeList> sensNodes(api->search(filter.get()));
        return sensNodes;
    };

    LOG_info << "___TEST TestForeingNodeImportRemoveSensitiveFlag";

    LOG_debug << "## Creating node tree in user 0 cloud";
    ASSERT_NO_FATAL_FAILURE(createNodeTrees());

    LOG_debug << "## Marking node as sensitive";
    // Mark one file as sensitive
    std::unique_ptr<MegaNode> sensFile{
        mSharerApi->getNodeByHandle(getHandle("/sharedfolder/file.txt"))};
    ASSERT_EQ(API_OK, synchronousSetNodeSensitive(mSharerIndex, sensFile.get(), true));

    // We test first the share via public link to ensure we go through the code path where the node
    // to import is not already in our cloud
    LOG_debug << "## User 0 creates a public link to share";
    const MegaHandle hfolder = getHandle("/sharedfolder");
    ASSERT_EQ(API_OK, synchronousGetSpecificAccountDetails(mSharerIndex, true, true, true))
        << "Cannot get account details";
    std::string nodeLink;
    ASSERT_NO_FATAL_FAILURE(createOnePublicLink(hfolder, nodeLink));

    LOG_debug << "## User 1 imports public link";
    MegaHandle importedNodeHandle = INVALID_HANDLE;
    ASSERT_NO_FATAL_FAILURE(importPublicLink(nodeLink, &importedNodeHandle));
    ASSERT_NE(importedNodeHandle, INVALID_HANDLE);

    // Check there is no sensitive nodes in the imported node
    LOG_debug << "## Checking user 1 sees no sensitive files in the imported folder";
    std::unique_ptr<MegaNodeList> sensNodes = getSensNodes(mShareeApi, importedNodeHandle);
    EXPECT_EQ(sensNodes->size(), 0)
        << "Got sensitive nodes after importing from public link while this property is expected "
           "to be cleared in the process";

    LOG_debug << "## Sharing the folder with user 1";
    ASSERT_NO_FATAL_FAILURE(createNewContactAndVerify());
    ASSERT_NO_FATAL_FAILURE(createOutgoingShare(hfolder));
    ASSERT_NO_FATAL_FAILURE(getInshare(hfolder));

    LOG_debug << "## Checking user 1 sees a sensitive file";
    sensNodes = getSensNodes(mShareeApi, hfolder);
    ASSERT_EQ(sensNodes->size(), 1);
    ASSERT_STREQ(sensNodes->get(0)->getName(), "file.txt");

    LOG_debug << "## User 1 copies folder with sensitive file into own cloud";
    MegaHandle copyHandle = INVALID_HANDLE;
    ASSERT_NO_FATAL_FAILURE(copyNode(mShareeIndex,
                                     getHandle("/sharedfolder"),
                                     INVALID_HANDLE,
                                     "copied_shared",
                                     &copyHandle));

    LOG_debug << "## Checking user 1 sees no sensitive files in the copied node";
    sensNodes = getSensNodes(mShareeApi, copyHandle);
    EXPECT_EQ(sensNodes->size(), 0)
        << "Got sensitive nodes after importing from shared folder while this property is expected "
           "to be cleared in the process";

    LOG_debug << "## User 0 copies the sensitive file into the same folder with different name";
    MegaHandle sharerCopyHandle = INVALID_HANDLE;
    ASSERT_NO_FATAL_FAILURE(copyNode(mSharerIndex,
                                     getHandle("/sharedfolder/file.txt"),
                                     getHandle("/sharedfolder"),
                                     "file_copied_by_sharer.txt",
                                     &sharerCopyHandle));

    LOG_debug << "## Checking the copy keeps the sensitive flag";
    std::unique_ptr<MegaNode> dest{mSharerApi->getNodeByHandle(sharerCopyHandle)};
    ASSERT_TRUE(dest->isMarkedSensitive())
        << "Copying a sensitive node within a shared folder by the owner resets the attribute";

    LOG_debug << "## User 1 copies the sensitive file into the same folder with different name";
    MegaHandle shareeCopyHandle = INVALID_HANDLE;
    ASSERT_NO_FATAL_FAILURE(copyNode(mShareeIndex,
                                     getHandle("/sharedfolder/file.txt"),
                                     getHandle("/sharedfolder"),
                                     "file_copied_by_sharee.txt",
                                     &shareeCopyHandle));

    LOG_debug << "## Checking the copy resets the sensitive flag";
    dest.reset(mShareeApi->getNodeByHandle(shareeCopyHandle));
    ASSERT_FALSE(dest->isMarkedSensitive())
        << "Copying a sensitive node within a shared folder by the sharee must reset sensitive";

    LOG_debug << "## User 1 copies sens to exact same place and name";
    ASSERT_NO_FATAL_FAILURE(copyNode(mShareeIndex,
                                     sharerCopyHandle,
                                     getHandle("/sharedfolder"),
                                     "file_copied_by_sharer.txt",
                                     &copyHandle));

    LOG_debug << "## Checking the copy resets the sensitive flag";
    dest.reset(mShareeApi->getNodeByHandle(shareeCopyHandle));
    EXPECT_FALSE(dest->isMarkedSensitive())
        << "Copying a sensitive node to the same place by the sharee must reset sensitive";

    LOG_debug << "## User 1 moves sensitive file from shared folder to own cloud";
    MegaHandle movedHandle = INVALID_HANDLE;
    ASSERT_NO_FATAL_FAILURE(
        moveNodeToOwnCloud("/sharedfolder/file.txt", "moved_file.txt", &movedHandle));
    ASSERT_NE(movedHandle, INVALID_HANDLE);

    LOG_debug << "## Checking the move resets the sensitive flag";
    std::unique_ptr<MegaNode> movedNode{mShareeApi->getNodeByHandle(movedHandle)};
    ASSERT_FALSE(movedNode->isMarkedSensitive())
        << "Moved node from shared folder kept the sensitive label";

    LOG_debug << "## User 1 marks it again as sensitive and copies it back to the shared folder";
    ASSERT_EQ(API_OK, synchronousSetNodeSensitive(mShareeIndex, movedNode.get(), true));
    movedNode.reset(mShareeApi->getNodeByHandle(movedHandle));
    ASSERT_TRUE(movedNode->isMarkedSensitive()) << "There was an error setting sensitive node";
    ASSERT_NO_FATAL_FAILURE(copyNode(mShareeIndex,
                                     movedHandle,
                                     getHandle("/sharedfolder"),
                                     "copied_back_sensitive_file.txt",
                                     &copyHandle));
    LOG_debug << "## Checking the copy resets the sensitive flag";
    dest.reset(mShareeApi->getNodeByHandle(copyHandle));
    ASSERT_FALSE(dest->isMarkedSensitive())
        << "The copy from sharee cloud to shared folder does nor reset the sensitive attribute";
}

TEST_F(SdkTest, SdkTestShareKeys)
{
    LOG_info << "___TEST ShareKeys___";
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(3));

    // make sure users B and C have no inshares (since before this test was started)
    for (unsigned apiIdx = 1; apiIdx <= 2; ++apiIdx)
    {
        unique_ptr<MegaShareList> inShares(megaApi[apiIdx]->getInSharesList());
        for (int i = 0; i < inShares->size(); ++i)
        {
            // leave share
            MegaShare* s = inShares->get(i);
            unique_ptr<MegaNode> n(megaApi[apiIdx]->getNodeByHandle(s->getNodeHandle()));
            ASSERT_EQ(API_OK, synchronousRemove(apiIdx, n.get()));
        }
    }

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

    // Initialize a test scenario: create a new contact to share to and verify credentials

    ASSERT_EQ(API_OK, synchronousInviteContact(0, mApi[1].email.c_str(), "SdkTestShareKeys contact request A to B", MegaContactRequest::INVITE_ACTION_ADD));
    ASSERT_EQ(API_OK, synchronousInviteContact(0, mApi[2].email.c_str(), "SdkTestShareKeys contact request A to C", MegaContactRequest::INVITE_ACTION_ADD));

    ASSERT_TRUE(WaitFor([this]() {return unique_ptr<MegaContactRequestList>(megaApi[1]->getIncomingContactRequests())->size() == 1
                                      && unique_ptr<MegaContactRequestList>(megaApi[2]->getIncomingContactRequests())->size() == 1;}, 60000));
    ASSERT_NO_FATAL_FAILURE(getContactRequest(1, false));
    ASSERT_NO_FATAL_FAILURE(getContactRequest(2, false));

    ASSERT_EQ(API_OK, synchronousReplyContactRequest(1, mApi[1].cr.get(), MegaContactRequest::REPLY_ACTION_ACCEPT));
    ASSERT_EQ(API_OK, synchronousReplyContactRequest(2, mApi[2].cr.get(), MegaContactRequest::REPLY_ACTION_ACCEPT));

    WaitMillisec(3000);

    if (gManualVerification)
    {
        if (!areCredentialsVerified(0, mApi[1].email)) {ASSERT_NO_FATAL_FAILURE(verifyCredentials(0, mApi[1].email));}
        if (!areCredentialsVerified(0, mApi[2].email)) {ASSERT_NO_FATAL_FAILURE(verifyCredentials(0, mApi[2].email));}
        if (!areCredentialsVerified(1, mApi[0].email)) {ASSERT_NO_FATAL_FAILURE(verifyCredentials(1, mApi[0].email));}
        if (!areCredentialsVerified(2, mApi[0].email)) {ASSERT_NO_FATAL_FAILURE(verifyCredentials(2, mApi[0].email));}
    }

    ASSERT_EQ(unsigned(unique_ptr<MegaShareList>(megaApi[1]->getInSharesList())->size()), 0u);
    ASSERT_EQ(unsigned(unique_ptr<MegaShareList>(megaApi[2]->getInSharesList())->size()), 0u);

    ASSERT_NO_FATAL_FAILURE(shareFolder(shareFolderA.get(), mApi[1].email.c_str(), MegaShare::ACCESS_READ));
    ASSERT_TRUE(WaitFor([this]() { return unique_ptr<MegaShareList>(megaApi[1]->getInSharesList())->size() == 1; }, 60000));

    ASSERT_NO_FATAL_FAILURE(shareFolder(subFolderA.get(), mApi[2].email.c_str(), MegaShare::ACCESS_FULL));
    ASSERT_TRUE(WaitFor([this]() { return unique_ptr<MegaShareList>(megaApi[2]->getInSharesList())->size() == 1; }, 60000));

    ASSERT_EQ(unsigned(unique_ptr<MegaShareList>(megaApi[1]->getInSharesList())->size()), 1u);
    ASSERT_EQ(unsigned(unique_ptr<MegaShareList>(megaApi[2]->getInSharesList())->size()), 1u);

    // Wait for the inshare nodes to be decrypted
    ASSERT_TRUE(WaitFor([this, &shareFolderA]() { return unique_ptr<MegaNode>(megaApi[1]->getNodeByHandle(shareFolderA->getHandle()))->isNodeKeyDecrypted(); }, 60*1000));
    ASSERT_TRUE(WaitFor([this, &subFolderA]() { return unique_ptr<MegaNode>(megaApi[2]->getNodeByHandle(subFolderA->getHandle()))->isNodeKeyDecrypted(); }, 60*1000));

    unique_ptr<MegaUser> c1(megaApi[1]->getContact(mApi[0].email.c_str()));
    unique_ptr<MegaUser> c2(megaApi[2]->getContact(mApi[0].email.c_str()));
    unique_ptr<MegaNodeList> nl1(megaApi[1]->getInShares(c1.get()));
    unique_ptr<MegaNodeList> nl2(megaApi[2]->getInShares(c2.get()));

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

        auto fsa = std::make_unique<FSACCESS_CLASS>();
        auto localdir = fspathToLocal(iteratePath);

        std::unique_ptr<FileAccess> fopen_directory(fsa->newfileaccess(false));  // false = don't follow symlinks
        ASSERT_TRUE(fopen_directory->fopen(localdir, true, false, FSLogging::logOnError));

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

                ASSERT_TRUE(plain_fopen_fa->fopen(localpath, true, false, FSLogging::logOnError));
                plain_fopen[leafNameUtf8] = *plain_fopen_fa;

                ASSERT_TRUE(iterate_fopen_fa->fopen(localpath, true, false, FSLogging::logOnError, da.get()));
                iterate_fopen[leafNameUtf8] = *iterate_fopen_fa;
            }
        }

        std::unique_ptr<FileAccess> fopen_directory2(fsa->newfileaccess(true));  // true = follow symlinks
        ASSERT_TRUE(fopen_directory2->fopen(localdir, true, false, FSLogging::logOnError));

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

                ASSERT_TRUE(plain_follow_fopen_fa->fopen(localpath, true, false, FSLogging::logOnError));
                plain_follow_fopen[leafNameUtf8] = *plain_follow_fopen_fa;

                ASSERT_TRUE(iterate_follow_fopen_fa->fopen(localpath, true, false, FSLogging::logOnError, da_follow.get()));
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
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));
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

    auto client{megaApi[0]->getClient()};

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

    handle h = megaApi[1]->getMyUserHandleBinary();
    MegaTextChatPeerList* peers =
        MegaTextChatPeerList::createInstance(); // new MegaTextChatPeerListPrivate();
    peers->addPeer(h, PRIV_STANDARD);
    bool group = true;

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

    // --- Create 1on1 chat with self
    megaApi[0]->changeApiUrl("https://staging.api.mega.co.nz/");
    mApi[0].chatUpdated = false;
    mApi[0].requestFlags[MegaRequest::TYPE_CHAT_CREATE] = false;
    ASSERT_NO_FATAL_FAILURE(createChat(false, nullptr));
    ASSERT_TRUE(waitForResponse(&mApi[0].requestFlags[MegaRequest::TYPE_CHAT_CREATE]))
        << "Cannot create a new chat with self";
    ASSERT_EQ(API_OK, mApi[0].lastError)
        << "Chat-with-self creation failed (error: " << mApi[0].lastError << ")";
    ASSERT_TRUE(waitForResponse(&mApi[0].chatUpdated)) // at the target side (auxiliar account)
        << "Chat update not received after " << maxTimeout << " seconds";
}
#endif

TEST_F(SdkTest, SdkTestFolderInfo)
{
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));
    std::unique_ptr<MegaNode> rootNode(megaApi[0]->getRootNode());
    ASSERT_TRUE(rootNode);
    auto f1node = createDirectory(*megaApi[0], *rootNode, "folder1");
    ASSERT_EQ(result(f1node), API_OK);
    ASSERT_TRUE(value(f1node));
    auto node = createDirectory(*megaApi[0], *value(f1node), "folder1.1");
    ASSERT_EQ(result(node), API_OK);
    ASSERT_TRUE(value(node));
    ASSERT_TRUE(createFile(UPFILE, false)); // local file
    MegaHandle fileHande = INVALID_HANDLE;
    ASSERT_EQ(MegaError::API_OK,
              doStartUpload(0,
                            &fileHande,
                            UPFILE.c_str(),
                            value(node).get(),
                            nullptr /*fileName*/,
                            ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
                            nullptr /*appData*/,
                            false /*isSourceTemporary*/,
                            false /*startFirst*/,
                            nullptr /*cancelToken*/));
    ASSERT_EQ(MegaError::API_OK,
              doStartUpload(0,
                            &fileHande,
                            UPFILE.c_str(),
                            value(f1node).get(),
                            nullptr /*fileName*/,
                            ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
                            nullptr /*appData*/,
                            false /*isSourceTemporary*/,
                            false /*startFirst*/,
                            nullptr /*cancelToken*/));
    ASSERT_EQ(MegaError::API_OK, synchronousFolderInfo(0, value(f1node).get()));
    auto info = mApi[0].mFolderInfo.get();
    ASSERT_EQ(info->getNumFiles(), 2);
    ASSERT_EQ(info->getNumFolders(), 1);
}

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
            ifs.read(buffer, static_cast<streamsize>(size));
        }
        else
        {
            ifs.seekg(static_cast<streamoff>(size), ios::cur);
        }
        return !ifs.fail();
    }
};


TEST_F(SdkTest, SdkTestFingerprint)
{
    LOG_info << "___TEST fingerprint stream/file___";
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    int filesizes[] = { 10, 100, 1000, 10000, 100000, 10000000 };
    string expected[] = {
        "DAQoBAMCAQQDAgEEAwAAAAAAAAQAypo7",
        "DAWQjMO2LBXoNwH_agtF8CX73QQAypo7",
        "EAugDFlhW_VTCMboWWFb9VMIxugQAypo7",
        "EAhAnWCqOGBx0gGOWe7N6wznWRAQAypo7",
        "GA6CGAQFLOwb40BGchttx22PvhZ5gQAypo7",
        "GA4CWmAdW1TwQ-bddEIKTmSDv0b2QQAypo7",
    };

    auto fsa = std::make_unique<FSACCESS_CLASS>();
    string name = "testfile";
    LocalPath localname = LocalPath::fromAbsolutePath(name);

    int value = 0x01020304;
    for (int i = sizeof filesizes / sizeof filesizes[0]; i--; )
    {
        {
            ofstream ofs(name.c_str(), ios::binary);
            char s[8192];
            ofs.rdbuf()->pubsetbuf(s, sizeof s);
            for (auto j = static_cast<size_t>(filesizes[i]) / sizeof(value); j--;)
                ofs.write((char*)&value, sizeof(value));
            ofs.write((char*)&value, static_cast<size_t>(filesizes[i]) % sizeof(value));
        }

        fsa->setmtimelocal(localname, 1000000000);

        string streamfp, filefp;
        {
            m_time_t mtime = 0;
            {
                auto nfa = fsa->newfileaccess();
                nfa->fopen(localname, FSLogging::logOnError);
                mtime = nfa->mtime;
            }

            myMIS mis(name.c_str());
            streamfp.assign(std::unique_ptr<char[]>(megaApi[0]->getFingerprint(&mis, mtime)).get());
        }
        filefp = std::unique_ptr<char[]>(megaApi[0]->getFingerprint(name.c_str())).get();

        ASSERT_EQ(streamfp, filefp);
        ASSERT_EQ(streamfp, expected[i]);
    }
}


static void incrementFilename(string& s)
{
    if (s.size() > 2)
    {
        if (is_digit(static_cast<unsigned>(s[s.size() - 2])) ||
            !is_digit(static_cast<unsigned>(s[s.size() - 1])))
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
        static int countdownTo429;
        static int countdownTo503;
        static int countdownToTimeout;
        static bool isRaid;
        static bool isRaidKnown;
        static m_off_t testProgressCompleted;
        static m_off_t testProgressContiguous;

        static void onSetIsRaid_morechunks(::mega::RaidBufferManager* tbm)
        {

            unsigned oldvalue = tbm->raidLinesPerChunk;
            unsigned minDivisorSize = 4 * 1024 * 1024; //  raidLinesPerChunk is defined by MAX_REQ_SIZE value, which depends on the system -> division factor of 4 for different max_req_sizes
            unsigned divideBy = std::max<unsigned>(static_cast<unsigned>(TransferSlot::MAX_REQ_SIZE / minDivisorSize), static_cast<unsigned>(1));
            tbm->raidLinesPerChunk = tbm->raidLinesPerChunk / divideBy;
            tbm->disableAvoidSmallLastRequest();
            LOG_info << "adjusted raidlinesPerChunk from " << oldvalue << " to " << tbm->raidLinesPerChunk << " and set AvoidSmallLastRequest flag to false";
        }

        static bool onHttpReqPost509(HttpReq* req)
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

        static bool onHttpReqPostError(HttpReq* req)
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
                if (countdownTo429-- == 0)
                {
                    req->httpstatus = 429;
                    req->status = REQ_FAILURE;

                    LOG_info << "SIMULATING HTTP GET 429";
                    return true;
                }
                if (countdownTo503-- == 0)
                {
                    req->httpstatus = 503;
                    req->status = REQ_FAILURE;

                    LOG_info << "SIMULATING HTTP GET 503";
                    return true;
                }
            }
            return false;
        }


        static bool onHttpReqPostTimeout(HttpReq* req)
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
            isRaid = tbm->isRaid() || tbm->isNewRaid();
            isRaidKnown = true;
            onSetIsRaid_morechunks(tbm);
        }

        static void onLimitMaxReqSize(m_off_t& maxReqSize) // Only valid for TransferBufferManager
        {
            auto oldMaxRequestSize = maxReqSize;
            maxReqSize = std::min<m_off_t>(maxReqSize, 1024 * 1024);
            LOG_info << "onLimitMaxReqSize: adjusted maxRequestSize from " << oldMaxRequestSize << " to " << maxReqSize;
        }

        static void onHookNumberOfConnections(int& connections, int clientNumberOfConnections) // Only valid for TransferBufferManager
        {
            LOG_info << "onLimitMaxReqSize: adjusted number of connections from " << connections << " to " << clientNumberOfConnections;
            connections = clientNumberOfConnections;
        }

        static void onHookDownloadRequestSingleUrl(bool& singleUrl)
        {
            LOG_info << "onHookDownloadRequestSingleUrl: set current singleUrl value (" << singleUrl
                     << ") to true";
            singleUrl = true;
        }

        static void onHookResetTransferLastAccessTime(m_time_t& lastAccessTime)
        {
            LOG_info << "onHookResetTransferLastAccessTime: reset current lastAccessTime value ("
                     << lastAccessTime << ") to 0";
            lastAccessTime = 0;
        }

        static void onProgressCompletedUpdate(const m_off_t p)
        {
            if (p)
            {
                // ignore ProgressCompleted reset(0)
                testProgressCompleted = p;
            }
            LOG_info << "onProgressCompletedUpdate:(" << p << ")";
        }

        static void onProgressContiguousUpdate(const m_off_t p)
        {
            if (p)
            {
                // ignore ProgressContiguous reset(0)
                testProgressContiguous = p;
            }
            LOG_info << "onProgressContiguousUpdate:(" << p << ")";
        }

        static bool resetForTests()
        {
#ifdef MEGASDK_DEBUG_TEST_HOOKS_ENABLED
            globalMegaTestHooks = MegaTestHooks(); // remove any callbacks set in other tests
            countdownToOverquota = -1;
            countdownTo404 = -1;
            countdownTo403 = -1;
            countdownTo429 = -1;
            countdownTo503 = -1;
            countdownToTimeout = -1;
            isRaid = false;
            isRaidKnown = false;
            testProgressCompleted = 0;
            testProgressContiguous = 0;
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

    int DebugTestHook::countdownToOverquota = -1;
    bool DebugTestHook::isRaid = false;
    bool DebugTestHook::isRaidKnown = false;
    int DebugTestHook::countdownTo404 = -1;
    int DebugTestHook::countdownTo403 = -1;
    int DebugTestHook::countdownTo429 = -1;
    int DebugTestHook::countdownTo503 = -1;
    int DebugTestHook::countdownToTimeout = -1;
    m_off_t DebugTestHook::testProgressCompleted = 0;
    m_off_t DebugTestHook::testProgressContiguous = 0;
}

/**
 * @brief TEST_F SdkTestCloudraidTransfers
 *
 * - # Test1: Download our well-known cloudraid file with standard settings
 * - # Test2: Download our well-known cloudraid file, but this time with small chunk sizes and
 * periodically pausing and unpausing
 * - # Test3: Download our well-known cloudraid file, but this time with small chunk sizes and
 * periodically destrying the megaApi object, then recreating and Resuming (with session token)
 */
#ifdef DEBUG
TEST_F(SdkTest, SdkTestCloudraidTransfers)
{
    LOG_info << "___TEST Cloudraid transfers___";
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    // Try and retrieve the user's current account level.
    auto accountLevel = getAccountLevel(*megaApi[0]);
    // Couldn't retrieve account level.
    if (auto result = ::result(accountLevel); result != API_OK)
    {
        // Leave a trail if we couldn't get the account level.
        ASSERT_EQ(result, API_OK) << "Couldn't retrieve account level: " << result;
    }

    AccountLevel level = value(accountLevel);
    // Try and elevate client to a pro pricing plan.
    auto result = setAccountLevel(*megaApi[0], MegaAccountDetails::ACCOUNT_TYPE_PROI, 1, nullptr);
    // Couldn't elevate client to a pro pricing plan.
    ASSERT_EQ(result, API_OK) << "Unable to upgrade the account" << result;

    // Restore the account back to normal once test completed.
    std::shared_ptr<AccountLevel> restorer(
        &level,
        [this](AccountLevel* level)
        {
            // Try and restore the user's account level.
            auto result = setAccountLevel(*megaApi[0], level->plan, level->months, nullptr);
            EXPECT_EQ(result, API_OK) << "Couldn't restore account level: " << result;
        });

    ASSERT_TRUE(DebugTestHook::resetForTests()) << "SDK test hooks are not enabled in release mode";

    std::unique_ptr<MegaNode> rootnode(megaApi[0]->getRootNode());

    auto importHandle = importPublicLink(0, MegaClient::MEGAURL+PUBLIC_IMAGE_URL, rootnode.get());
    MegaHandle imported_file_handle = importHandle;

    std::unique_ptr<MegaNode> nimported(megaApi[0]->getNodeByHandle(imported_file_handle));


    string filename = DOTSLASH "cloudraid_downloaded_file.sdktest";
    deleteFile(filename.c_str());

    LOG_debug << "#### Test1: Download our well-known cloudraid file with standard settings ####";
    mApi[0].transferFlags[MegaTransfer::TYPE_DOWNLOAD] = false;
    megaApi[0]->startDownload(nimported.get(),
                              filename.c_str(),
                              nullptr  /*customName*/,
                              nullptr  /*appData*/,
                              false    /*startFirst*/,
                              nullptr  /*cancelToken*/,
                              MegaTransfer::COLLISION_CHECK_FINGERPRINT /*collisionCheck*/,
                              MegaTransfer::COLLISION_RESOLUTION_NEW_WITH_N /* collisionResolution */,
                              false    /* undelete */);

    ASSERT_TRUE(waitForResponse(&mApi[0].transferFlags[MegaTransfer::TYPE_DOWNLOAD], 600))
        << "Download cloudraid transfer failed after " << maxTimeout << " seconds";
    ASSERT_EQ(API_OK, mApi[0].lastError) << "Cannot download the cloudraid file (error: " << mApi[0].lastError << ")";

    LOG_debug << "#### Test2(SdkTestCloudraidTransfers): Download our well-known cloudraid file, "
                 "but this time with small chunk sizes and periodically pausing and unpausing ####";
    incrementFilename(filename);
    deleteFile(filename.c_str());

    // smaller chunk sizes so we can get plenty of pauses
    #ifdef MEGASDK_DEBUG_TEST_HOOKS_ENABLED
    globalMegaTestHooks.onSetIsRaid = ::mega::DebugTestHook::onSetIsRaid_morechunks;
    globalMegaTestHooks.onLimitMaxReqSize = ::mega::DebugTestHook::onLimitMaxReqSize;
    globalMegaTestHooks.onHookNumberOfConnections = ::mega::DebugTestHook::onHookNumberOfConnections;
    #endif

    ASSERT_EQ(API_OK, doSetMaxConnections(0, 2)) << "doSetMaxConnections failed or took more than 1 minute";
    LOG_debug << "For raidTests: client max connections set to 2";

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
                                  nullptr  /*cancelToken*/,
                                  MegaTransfer::COLLISION_CHECK_FINGERPRINT /*collisionCheck*/,
                                  MegaTransfer::COLLISION_RESOLUTION_NEW_WITH_N /* collisionResolution */,
                                  false    /* undelete */);

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

    LOG_debug << "#### Test3(SdkTestCloudraidTransfers): Download our well-known cloudraid file, "
                 "but this time with small "
                 "chunk sizes and periodically destrying the megaApi object, then recreating and "
                 "Resuming (with session token)####";
    // plain cloudraid download
    {
        megaApi[0]->setMaxDownloadSpeed(1024 * 1024);
        mApi[0].transferFlags[MegaTransfer::TYPE_DOWNLOAD] = false;
        megaApi[0]->startDownload(nimported.get(),
                                  filename.c_str(),
                                  nullptr  /*customName*/,
                                  nullptr  /*appData*/,
                                  false    /*startFirst*/,
                                  nullptr  /*cancelToken*/,
                                  MegaTransfer::COLLISION_CHECK_FINGERPRINT /*collisionCheck*/,
                                  MegaTransfer::COLLISION_RESOLUTION_NEW_WITH_N /* collisionResolution */,
                                  false    /* undelete */);

        std::string sessionId = unique_ptr<char[]>(megaApi[0]->dumpSession()).get();

        onTransferUpdate_progress = 0;// updated in callbacks
        onTransferUpdate_filesize = 0;
        m_off_t lastprogress = 0;
        unsigned exitresumecount = 0;
        second_timer t;
        auto initialOnTranferFinishedCount = onTranferFinishedCount;
        auto lastOnTranferFinishedCount = onTranferFinishedCount;
        while (t.elapsed() < static_cast<int>(maxTimeout / 2) &&
               onTranferFinishedCount < initialOnTranferFinishedCount + 2)
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
                megaApi[0]->startDownload(nimported.get(),
                                          filename.c_str(),
                                          nullptr  /*customName*/,
                                          nullptr  /*appData*/,
                                          false    /*startFirst*/,
                                          nullptr  /*cancelToken*/,
                                          MegaTransfer::COLLISION_CHECK_FINGERPRINT /*collisionCheck*/,
                                          MegaTransfer::COLLISION_RESOLUTION_NEW_WITH_N /* collisionResolution */,
                                          false    /* undelete */);
            }
            else if (onTransferUpdate_progress > lastprogress + onTransferUpdate_filesize/10 )
            {
                if (exitresumecount < 3*(onTranferFinishedCount - initialOnTranferFinishedCount + 1))
                {
                    megaApi[0].reset();
                    exitresumecount += 1;
                    WaitMillisec(100);

                    megaApi[0] = newMegaApi(APP_KEY.c_str(),
                                            megaApiCacheFolder(0).c_str(),
                                            USER_AGENT.c_str(),
                                            unsigned(THREADS_PER_MEGACLIENT));
                    mApi[0].megaApi = megaApi[0].get();
                    megaApi[0]->addListener(this);
                    megaApi[0]->setMaxDownloadSpeed(1024 * 1024);
                    ASSERT_EQ(API_OK, doSetMaxConnections(0, 2)) << "doSetMaxConnections failed or took more than 1 minute";
                    LOG_debug << "For raidTests: client max connections set to 2";

                    t.pause();
                    ASSERT_NO_FATAL_FAILURE(resumeSession(sessionId.c_str()));
                    ASSERT_NO_FATAL_FAILURE(fetchnodes(0));
                    t.resume();

                    lastprogress = onTransferUpdate_progress;
                }
            }
            WaitMillisec(1);
        }
        ASSERT_EQ(initialOnTranferFinishedCount + 2, onTranferFinishedCount)
            << onTranferFinishedCount << "transfers finished, but we expected "
            << initialOnTranferFinishedCount + 2;
        ASSERT_EQ(onTransferUpdate_progress, onTransferUpdate_filesize)
            << "Expected onTransferUpdate_progress: " << onTransferUpdate_progress
            << ", doesn't match with onTransferUpdate_filesize: " << onTransferUpdate_filesize;
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
* Download a cloudraid file but with a connection failing with http errors 404 and 403. The download should recover from the problems in 5 channel mode
*
*/

#ifdef DEBUG
TEST_F(SdkTest, SdkTestCloudraidTransferWithConnectionFailures)
{
    LOG_info << "___TEST Cloudraid transfers with connection failures___";
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));
    // Make sure our clients are working with pro plans.
    auto restorer0 = elevateToPro(*megaApi[0]);
    ASSERT_EQ(result(restorer0), API_OK);

    ASSERT_TRUE(DebugTestHook::resetForTests()) << "SDK test hooks are not enabled in release mode";

    std::unique_ptr<MegaNode> rootnode{megaApi[0]->getRootNode()};

    auto importHandle = importPublicLink(0, MegaClient::MEGAURL+PUBLIC_IMAGE_URL, rootnode.get());
    std::unique_ptr<MegaNode> nimported{megaApi[0]->getNodeByHandle(importHandle)};


    string filename = DOTSLASH "cloudraid_downloaded_file.sdktest";
    deleteFile(filename.c_str());

    // set up for 404 and 403 errors
    // smaller chunk sizes so we can get plenty of pauses
    DebugTestHook::countdownTo404 = 5;
    DebugTestHook::countdownTo403 = 12;
#ifdef MEGASDK_DEBUG_TEST_HOOKS_ENABLED
    globalMegaTestHooks.onHttpReqPost = DebugTestHook::onHttpReqPostError;
    globalMegaTestHooks.onSetIsRaid = DebugTestHook::onSetIsRaid_morechunks;
    globalMegaTestHooks.onLimitMaxReqSize = ::mega::DebugTestHook::onLimitMaxReqSize;
    globalMegaTestHooks.onHookNumberOfConnections = ::mega::DebugTestHook::onHookNumberOfConnections;
#endif

    megaApi[0]->setMaxDownloadSpeed(1024 * 1024);
    ASSERT_EQ(API_OK, doSetMaxConnections(0, 2)) << "doSetMaxConnections failed or took more than 1 minute";
    LOG_debug << "For raidTests: client max connections set to 2";

    // plain cloudraid download
    {
        onTransferUpdate_progress = 0;
        onTransferUpdate_filesize = 0;
        mApi[0].transferFlags[MegaTransfer::TYPE_DOWNLOAD] = false;
        const auto& downloadStartTime = std::chrono::system_clock::now();
        megaApi[0]->startDownload(nimported.get(),
                                  filename.c_str(),
                                  nullptr  /*customName*/,
                                  nullptr  /*appData*/,
                                  false    /*startFirst*/,
                                  nullptr  /*cancelToken*/,
                                  MegaTransfer::COLLISION_CHECK_FINGERPRINT /*collisionCheck*/,
                                  MegaTransfer::COLLISION_RESOLUTION_NEW_WITH_N /* collisionResolution */,
                                  false    /* undelete */);

        unsigned int transfer_timeout_in_seconds = 180;
        ASSERT_TRUE(waitForResponse(&mApi[0].transferFlags[MegaTransfer::TYPE_DOWNLOAD], transfer_timeout_in_seconds)) << "Cloudraid download with 404 and 403 errors time out (180 seconds)";
        ASSERT_EQ(API_OK, mApi[0].lastError) << "Cannot download the cloudraid file (error: " << mApi[0].lastError << ")";
        const auto& downloadEndTime = std::chrono::system_clock::now();
        auto downloadTime = std::chrono::duration_cast<std::chrono::milliseconds>(downloadEndTime - downloadStartTime).count();
        LOG_debug << "[SdkTestCloudRaidTransferWithConnectionFailures] downloadTime = " << downloadTime << " ms, size = " << nimported->getSize() << "" << " [speed = " << (nimported->getSize() / downloadTime) << " B/s]";
        ASSERT_GE(onTransferUpdate_filesize, 0u);
        ASSERT_TRUE(onTransferUpdate_progress == onTransferUpdate_filesize);
        ASSERT_LT(DebugTestHook::countdownTo404, 0);
        ASSERT_LT(DebugTestHook::countdownTo403, 0);
        ASSERT_EQ(onTransferFinish_transferStats.numFailedRequests, 2); // One 404 and one 403
        ASSERT_GT(onTransferFinish_transferStats.failedRequestRatio, 0.0);
        ASSERT_LT(onTransferFinish_transferStats.failedRequestRatio, 1.0);
        ASSERT_EQ(onTransferFinish_transferStats.numTotalRequests,
                  35 + 2); // 35 is the calculated number of requests for this file and chunk size
                           // (+2 after 2 failed requests)
    }

    ASSERT_TRUE(DebugTestHook::resetForTests()) << "SDK test hooks are not enabled in release mode";
}

TEST_F(SdkTest, SdkTestCloudraidTransferBestCase)
{
    LOG_info << "___TEST Cloudraid transfers best case___";
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(2));

    // Make sure our clients are working with pro plans.
    auto restorer0 = elevateToPro(*megaApi[0]);
    ASSERT_EQ(result(restorer0), API_OK);
    auto restorer1 = elevateToPro(*megaApi[1]);
    ASSERT_EQ(result(restorer1), API_OK);

    std::unique_ptr<MegaNode> rootnode{megaApi[0]->getRootNode()};

    std::string url100MB = "/#!JzckQJ6L!X_p0u26-HOTenAG0rATFhKdxYx-rOV1U6YHYhnz2nsA"; //https://mega.nz/file/JzckQJ6L#X_p0u26-HOTenAG0rATFhKdxYx-rOV1U6YHYhnz2nsA
    auto importHandle = importPublicLink(0, MegaClient::MEGAURL+url100MB, rootnode.get());
    std::unique_ptr<MegaNode> nimported{megaApi[0]->getNodeByHandle(importHandle)};


    string filename = DOTSLASH "cloudraid_downloaded_file.sdktest";
    deleteFile(filename.c_str());

    // plain cloudraid download
    {
        onTransferUpdate_progress = 0;
        onTransferUpdate_filesize = 0;
        mApi[0].transferFlags[MegaTransfer::TYPE_DOWNLOAD] = false;
        const auto& downloadStartTime = std::chrono::system_clock::now();
        megaApi[0]->startDownload(nimported.get(),
                                  filename.c_str(),
                                  nullptr  /*customName*/,
                                  nullptr  /*appData*/,
                                  false    /*startFirst*/,
                                  nullptr  /*cancelToken*/,
                                  MegaTransfer::COLLISION_CHECK_FINGERPRINT /*collisionCheck*/,
                                  MegaTransfer::COLLISION_RESOLUTION_NEW_WITH_N /* collisionResolution */,
                                  false /* undelete */);

        unsigned int transfer_timeout_in_seconds = 180;
        ASSERT_TRUE(waitForResponse(&mApi[0].transferFlags[MegaTransfer::TYPE_DOWNLOAD], transfer_timeout_in_seconds)) << "Cloudraid download Best Case (without forced errors) time out (180 seconds)";
        ASSERT_EQ(API_OK, mApi[0].lastError) << "Cannot download the cloudraid file (error: " << mApi[0].lastError << ")";
        const auto& downloadEndTime = std::chrono::system_clock::now();
        auto downloadTime = std::chrono::duration_cast<std::chrono::milliseconds>(downloadEndTime - downloadStartTime).count();
        LOG_debug << "[SdkTestCloudRaidTransferBestCase] downloadTime = " << downloadTime << " ms, size = " << nimported->getSize() << "" << " [speed = " << (((nimported->getSize() / downloadTime) * 1000) / 1024) << " KB/s]";
        ASSERT_GE(onTransferUpdate_filesize, 0u);
        ASSERT_TRUE(onTransferUpdate_progress == onTransferUpdate_filesize);
    }
}
#endif


/**
* @brief TEST_F SdkTestCloudraidTransferWithSingleChannelTimeouts
*
* Download a cloudraid file but with a connection failing after a timeout. The download should recover from the problems in 5 channel mode
*
*/

#ifdef DEBUG
TEST_F(SdkTest, SdkTestCloudraidTransferWithSingleChannelTimeouts)
{
    LOG_info << "___TEST Cloudraid transfers with single channel timeouts___";
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));
    // Make sure our clients are working with pro plans.
    auto restorer0 = elevateToPro(*megaApi[0]);
    ASSERT_EQ(result(restorer0), API_OK);

    ASSERT_TRUE(DebugTestHook::resetForTests()) << "SDK test hooks are not enabled in release mode";

    std::unique_ptr<MegaNode> rootnode{megaApi[0]->getRootNode()};

    auto importHandle = importPublicLink(0, MegaClient::MEGAURL+PUBLIC_IMAGE_URL, rootnode.get());
    std::unique_ptr<MegaNode> nimported{megaApi[0]->getNodeByHandle(importHandle)};


    string filename = DOTSLASH "cloudraid_downloaded_file.sdktest";
    deleteFile(filename.c_str());

    // set up for timeout
    // smaller chunk sizes so we can get plenty of pauses
    DebugTestHook::countdownToTimeout = 15;
#ifdef MEGASDK_DEBUG_TEST_HOOKS_ENABLED
    globalMegaTestHooks.onHttpReqPost = DebugTestHook::onHttpReqPostTimeout;
    globalMegaTestHooks.onSetIsRaid = DebugTestHook::onSetIsRaid_morechunks;
    globalMegaTestHooks.onLimitMaxReqSize = ::mega::DebugTestHook::onLimitMaxReqSize;
    globalMegaTestHooks.onHookNumberOfConnections = ::mega::DebugTestHook::onHookNumberOfConnections;
#endif

    ASSERT_EQ(API_OK, doSetMaxConnections(0, 2)) << "doSetMaxConnections failed or took more than 1 minute";
    LOG_info << "For raidTests: client max connections set to 3";

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
                                  nullptr  /*cancelToken*/,
                                  MegaTransfer::COLLISION_CHECK_FINGERPRINT /*collisionCheck*/,
                                  MegaTransfer::COLLISION_RESOLUTION_NEW_WITH_N /* collisionResolution */,
                                  false    /* undelete */);

        ASSERT_TRUE(waitForResponse(&mApi[0].transferFlags[MegaTransfer::TYPE_DOWNLOAD], 180)) << "Cloudraid download with timeout errors timed out (180 seconds)";
        ASSERT_EQ(API_OK, mApi[0].lastError) << "Cannot download the cloudraid file (error: " << mApi[0].lastError << ")";
        ASSERT_GE(onTransferUpdate_filesize, 0u);
        ASSERT_EQ(onTransferUpdate_progress, onTransferUpdate_filesize);
        ASSERT_LT(DebugTestHook::countdownToTimeout, 0);
        ASSERT_EQ(onTransferFinish_transferStats.numFailedRequests,
                  0); // This "timeout" does not imply a request failure because it is detected as a
                      // hanging source
        ASSERT_EQ(onTransferFinish_transferStats.failedRequestRatio, 0.0);
        ASSERT_EQ(onTransferFinish_transferStats.numTotalRequests,
                  35 + 1); // 35 is the calculated number of requests for this file and chunk size
    }
    ASSERT_TRUE(DebugTestHook::resetForTests()) << "SDK test hooks are not enabled in release mode";
}

void SdkTest::testCloudRaidTransferResume(const bool fromNonRaid, const std::string& logPre)
{
    LOG_info << logPre << "BEGIN";

    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    LOG_debug << logPre << "Promote account to PRO plan";
    const auto restorer0 = elevateToPro(*megaApi[0]);
    ASSERT_EQ(result(restorer0), API_OK);

    ASSERT_TRUE(DebugTestHook::resetForTests()) << "SDK test hooks are not enabled in release mode";

    std::unique_ptr<MegaNode> rootnode{megaApi[0]->getRootNode()};

    LOG_debug << logPre << "Get CloudRAID file from public link";
    const auto importRaidHandle =
        importPublicLink(0, MegaClient::MEGAURL + PUBLIC_IMAGE_URL, rootnode.get());

    unique_ptr<MegaNode> cloudRaidNode{megaApi[0]->getNodeByHandle(importRaidHandle)};
#ifdef MEGASDK_DEBUG_TEST_HOOKS_ENABLED
    [[maybe_unused]] m_off_t tProgressCompletedPreResume{0};
    [[maybe_unused]] m_off_t tProgressContiguousPreResume{0};
    globalMegaTestHooks.onProgressCompletedUpdate =
        ::mega::DebugTestHook::onProgressCompletedUpdate;
    globalMegaTestHooks.onProgressContiguousUpdate =
        ::mega::DebugTestHook::onProgressContiguousUpdate;
    globalMegaTestHooks.onSetIsRaid = DebugTestHook::onSetIsRaid_morechunks;
    globalMegaTestHooks.onLimitMaxReqSize = DebugTestHook::onLimitMaxReqSize;
    globalMegaTestHooks.onHookNumberOfConnections = DebugTestHook::onHookNumberOfConnections;
    if (fromNonRaid)
    {
        globalMegaTestHooks.onHookDownloadRequestSingleUrl =
            DebugTestHook::onHookDownloadRequestSingleUrl;
    }
#endif

    ASSERT_EQ(API_OK, doSetMaxConnections(0, 2))
        << "doSetMaxConnections failed or took more than 1 minute";
    LOG_debug << logPre << "Client max connections set to 2";

    LOG_debug << logPre << "Clean up any existing file, limit speed, start download";
    const auto downloadedFile = std::string(DOTSLASH "cloudraid_downloaded_file.sdktest");
    deleteFile(downloadedFile.c_str());
    megaApi[0]->setMaxDownloadSpeed(2000000);

    onTransferUpdate_progress = 0;
    TransferTracker rdt(megaApi[0].get());
    megaApi[0]->startDownload(cloudRaidNode.get(),
                              downloadedFile.c_str(),
                              nullptr /*customName*/,
                              nullptr /*appData*/,
                              false /*startFirst*/,
                              nullptr /*cancelToken*/,
                              MegaTransfer::COLLISION_CHECK_FINGERPRINT,
                              MegaTransfer::COLLISION_RESOLUTION_NEW_WITH_N,
                              false /* undelete */,
                              &rdt /*listener*/);

    second_timer timer;
    static constexpr m_off_t pauseThreshold{9000000};
    // Wait until partial download or timeout
    while (!rdt.finished && timer.elapsed() < 120 && onTransferUpdate_progress < pauseThreshold)
    {
        WaitMillisec(200);
    }

    ASSERT_FALSE(rdt.finished) << "Download ended too early, with " << rdt.waitForResult();
    ASSERT_GT(onTransferUpdate_progress, 0) << "Nothing was downloaded";

    // 2. Logout
    LOG_debug << logPre << "Local logout while the transfer is in flight";
    std::unique_ptr<char[]> session(dumpSession());
    ASSERT_NO_FATAL_FAILURE(locallogout());

    const auto result = rdt.waitForResult();
    ASSERT_TRUE(result == API_EACCESS || result == API_EINCOMPLETE)
        << "Download interrupted with unexpected code: " << result;

#ifdef MEGASDK_DEBUG_TEST_HOOKS_ENABLED
    tProgressCompletedPreResume = DebugTestHook::testProgressCompleted;
    tProgressContiguousPreResume = DebugTestHook::testProgressContiguous;

    [[maybe_unused]] m_off_t tProgressCompletedAfterResume{0};
    [[maybe_unused]] m_off_t tProgressContiguousAfterResume{0};
    [[maybe_unused]] std::atomic<bool> exitFlagAfterResume{false};
    DebugTestHook::testProgressCompleted = 0;
    DebugTestHook::testProgressContiguous = 0;
    onTransferStartCustomCb = [&tProgressCompletedAfterResume,
                               &tProgressContiguousAfterResume,
                               &exitFlagAfterResume](MegaTransfer* t) -> void
    {
        if (t)
        {
            tProgressCompletedAfterResume = t->getTransferredBytes();
        }
        tProgressContiguousAfterResume = DebugTestHook::testProgressContiguous;
        exitFlagAfterResume = true;
    };

    if (fromNonRaid)
    {
        globalMegaTestHooks.onHookDownloadRequestSingleUrl = nullptr;
        globalMegaTestHooks.onHookResetTransferLastAccessTime =
            ::mega::DebugTestHook::onHookResetTransferLastAccessTime;
    }
#endif

    LOG_debug << logPre << "Resume session";
    onTransferStart_progress = 0;
    ASSERT_NO_FATAL_FAILURE(resumeSession(session.get()));
    ASSERT_NO_FATAL_FAILURE(fetchnodes(0));

#ifdef MEGASDK_DEBUG_TEST_HOOKS_ENABLED
    ASSERT_TRUE(WaitFor(std::bind(
                            [](const std::atomic<bool>& flag)
                            {
                                // wait for onTransferStart after login + fetchnodes
                                return flag.load();
                            },
                            std::cref(exitFlagAfterResume)),
                        60000));

    ASSERT_EQ(tProgressCompletedPreResume, tProgressCompletedAfterResume)
        << "Progress complete mismatch between logout and onTransferStart values (it shouldn't "
           "have changed)";

    ASSERT_EQ(tProgressContiguousPreResume, tProgressContiguousAfterResume)
        << "Progress contiguous mismatch between logout and onTransferStart values (it shouldn't "
           "have changed)";
#endif

    ASSERT_EQ(API_OK, doSetMaxConnections(0, 4))
        << "doSetMaxConnections failed or took more than 1 minute";
    LOG_debug << logPre << "Client max connections set to 4";

    LOG_debug << logPre << "Check transfer resumption after resuming session";
    timer.reset();
    unique_ptr<MegaTransferList> transfers(megaApi[0]->getTransfers(MegaTransfer::TYPE_DOWNLOAD));

    while ((!transfers || !transfers->size()) && timer.elapsed() < 20)
    {
        WaitMillisec(100);
        transfers.reset(megaApi[0]->getTransfers(MegaTransfer::TYPE_DOWNLOAD));
    }
    ASSERT_EQ(transfers->size(), 1U) << "Download ended before resumption was checked, "
                                        "or was not resumed after 20 seconds";
    ASSERT_GT(onTransferStart_progress, 0)
        << "Download appears to have been restarted instead of resumed";

#ifdef MEGASDK_DEBUG_TEST_HOOKS_ENABLED
    if (fromNonRaid)
    {
        globalMegaTestHooks.onHookResetTransferLastAccessTime = nullptr;
    }
#endif

    LOG_debug << logPre << "Let the download finish completely";
    megaApi[0]->setMaxDownloadSpeed(-1);
    static constexpr size_t maxAllowedToFinishDownload{120};
    while (transfers && transfers->size() && timer.elapsed() < maxAllowedToFinishDownload)
    {
        WaitMillisec(500);
        transfers.reset(megaApi[0]->getTransfers(MegaTransfer::TYPE_DOWNLOAD));
    }
    ASSERT_TRUE(!transfers || !transfers->size())
        << "Download did not finish after " << maxAllowedToFinishDownload << " seconds";

    // Test hooks must be reset
    ASSERT_TRUE(DebugTestHook::resetForTests()) << "SDK test hooks are not enabled in release mode";
    LOG_info << logPre << "FINISH";
}

/**
 * @brief TEST_F SdkTestCloudraidTransferResume
 *
 * Tests resumption for raid file download.
 */
TEST_F(SdkTest, SdkTestCloudraidTransferResume)
{
    static const auto logPre = getLogPrefix();
    constexpr bool fromNonRaid{false};
    testCloudRaidTransferResume(fromNonRaid, logPre);
}

/**
 * @brief TEST_F SdkTestCloudraidTransferResumeFromNonRaid
 *
 * Tests resumption from a non-raided download that is now raided and resumed with CloudRAID logic.
 */
TEST_F(SdkTest, SdkTestCloudraidTransferResumeFromNonRaid)
{
    static const auto logPre = getLogPrefix();
    constexpr bool fromNonRaid{true};
    testCloudRaidTransferResume(fromNonRaid, logPre);
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
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

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
                              nullptr  /*cancelToken*/,
                              MegaTransfer::COLLISION_CHECK_FINGERPRINT /*collisionCheck*/,
                              MegaTransfer::COLLISION_RESOLUTION_NEW_WITH_N /* collisionResolution */,
                              false    /* undelete */);

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
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    // Make sure our clients are working with pro plans.
    auto accountRestorer = elevateToPro(*megaApi[0]);
    ASSERT_EQ(result(accountRestorer), API_OK);

    ASSERT_TRUE(DebugTestHook::resetForTests()) << "SDK test hooks are not enabled in release mode";

    auto importHandle = importPublicLink(0, MegaClient::MEGAURL+PUBLIC_IMAGE_URL,
                                         std::unique_ptr<MegaNode>(megaApi[0]->getRootNode()).get());
    std::unique_ptr<MegaNode> nimported(megaApi[0]->getNodeByHandle(importHandle));

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
    megaApi[0]->startDownload(nimported.get(),
                              filename2.c_str(),
                              nullptr  /*customName*/,
                              nullptr  /*appData*/,
                              false    /*startFirst*/,
                              nullptr  /*cancelToken*/,
                              MegaTransfer::COLLISION_CHECK_FINGERPRINT /*collisionCheck*/,
                              MegaTransfer::COLLISION_RESOLUTION_NEW_WITH_N /* collisionResolution */,
                              false    /* undelete */);

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

    std::atomic<bool> mFinished{false};
    MegaApi* mApi;
    size_t reserved;
    size_t receiveBufPos;
    size_t file_start_offset;
    byte* receiveBuf;
    bool completedSuccessfully;
    bool completedUnsuccessfully;
    MegaError* completedUnsuccessfullyError;
    byte* compareDecryptedData;
    bool comparedEqual;
    m_off_t numFailedRequests{};

    CheckStreamedFile_MegaTransferListener(MegaApi* const megaApi,
                                           const size_t receiveStartPoint,
                                           const size_t receiveSizeExpected,
                                           byte* const fileCompareData):
        mApi(megaApi),
        reserved(0),
        receiveBufPos(0),
        file_start_offset(0),
        receiveBuf(NULL),
        completedSuccessfully(false),
        completedUnsuccessfully(false),
        completedUnsuccessfullyError(NULL),
        compareDecryptedData(fileCompareData),
        comparedEqual(true)
    {
        file_start_offset = receiveStartPoint;
        reserved = receiveSizeExpected;
        receiveBuf = new byte[reserved];
        compareDecryptedData = fileCompareData;
    }

    ~CheckStreamedFile_MegaTransferListener()
    {
        if (!mFinished)
        {
            assert(mApi);
            mApi->removeTransferListener(this);
        }

        delete[] receiveBuf;
    }

    void onTransferStart(MegaApi*, MegaTransfer*) override {}

    void onTransferFinish(MegaApi*, MegaTransfer*, MegaError* error) override
    {
        if (error && error->getErrorCode() != API_OK)
        {
            ((error->getErrorCode() == API_EARGS && reserved == 0) ? completedSuccessfully : completedUnsuccessfully) = true;
            completedUnsuccessfullyError = error->copy();
        }
        else
        {
            if (compareDecryptedData && 0 != memcmp(receiveBuf, compareDecryptedData + file_start_offset, receiveBufPos))
                comparedEqual = false;
            completedSuccessfully = true;
        }
        mFinished = true;
    }

    void onTransferUpdate(MegaApi*, MegaTransfer*) override {}

    void onTransferTemporaryError(MegaApi *api, MegaTransfer * /*transfer*/, MegaError* error) override
    {
        ++numFailedRequests;
        ostringstream msg;
        msg << "onTransferTemporaryError: " << (error ? error->getErrorString() : "NULL");
        api->log(MegaApi::LOG_LEVEL_WARNING, msg.str().c_str());
    }

    bool onTransferData(MegaApi*, MegaTransfer*, char* buffer, size_t size) override
    {
        assert(receiveBufPos + size <= reserved);
        memcpy(receiveBuf + receiveBufPos, buffer, size);
        receiveBufPos += size;

        if (compareDecryptedData && 0 != memcmp(receiveBuf, compareDecryptedData + file_start_offset, receiveBufPos))
            comparedEqual = false;

        return true;
    }
};


CheckStreamedFile_MegaTransferListener* StreamRaidFilePart(MegaApi* megaApi, m_off_t start, m_off_t end, bool raid, bool smallpieces, MegaNode* raidFileNode, MegaNode*nonRaidFileNode, ::mega::byte* filecomparedata)
{
    assert((raid ? raidFileNode : nonRaidFileNode));
    LOG_info << "stream test ---------------------------------------------------" << start << " to " << end << "(len " << end - start << ") " << (raid ? " RAID " : " non-raid ") << (raid ? (smallpieces ? " smallpieces " : "normalpieces") : "");

#ifdef MEGASDK_DEBUG_TEST_HOOKS_ENABLED
    globalMegaTestHooks.onSetIsRaid = smallpieces ? &DebugTestHook::onSetIsRaid_smallchunks10 : NULL;
#endif

    CheckStreamedFile_MegaTransferListener* p =
        new CheckStreamedFile_MegaTransferListener(megaApi,
                                                   size_t(start),
                                                   size_t(end - start),
                                                   filecomparedata);
    megaApi->setStreamingMinimumRate(0);
    megaApi->startStreaming(raid ? raidFileNode : nonRaidFileNode, start, end - start, p);
    return p;
}


/**
* @brief TEST_F SdkTestCloudraidStreamingSoakTest
*
* Stream random portions of the well-known file for 10 minutes, while randomly varying
*       raid / non-raid
*       front/end/middle  (especial attention to first and last raidlines, and varying start/end within a raidline)
*       large piece / small piece
*       small raid chunk sizes (so small pieces of file don't just load in one request per connection) / normal sizes
*
*/

TEST_F(SdkTest, SdkTestCloudraidStreamingSoakTest)
{
    LOG_info << "___TEST SdkTestCloudraidStreamingSoakTest";
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    // Make sure our clients are working with pro plans.
    auto accountRestorer = elevateToPro(*megaApi[0]);
    ASSERT_EQ(result(accountRestorer), API_OK);

#ifdef MEGASDK_DEBUG_TEST_HOOKS_ENABLED
    ASSERT_TRUE(DebugTestHook::resetForTests()) << "SDK test hooks are not enabled in release mode";
#endif

    // ensure we have our standard raid test file
    auto importHandle = importPublicLink(0, MegaClient::MEGAURL+PUBLIC_IMAGE_URL, std::unique_ptr<MegaNode>{megaApi[0]->getRootNode()}.get());
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
                              nullptr  /*cancelToken*/,
                              MegaTransfer::COLLISION_CHECK_FINGERPRINT /*collisionCheck*/,
                              MegaTransfer::COLLISION_RESOLUTION_NEW_WITH_N /* collisionResolution */,
                              false    /* undelete */);

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
        megaApi[0]->setStreamingMinimumRate(0);
        CheckStreamedFile_MegaTransferListener* p = StreamRaidFilePart(megaApi[0].get(),
                                                                       start,
                                                                       end,
                                                                       !nonraid,
                                                                       smallpieces != 0,
                                                                       nimported,
                                                                       nonRaidNode,
                                                                       compareDecryptedData.data());

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
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    const auto updloadFile =
        [this, rootnode = std::unique_ptr<MegaNode>(megaApi[0]->getRootNode())](
            const std::string& fname,
            const std::string_view contents)
    {
        deleteFile(fname);
        sdk_test::LocalTempFile f(fname, contents);
        auto err = doStartUpload(0,
                                 nullptr,
                                 fname.c_str(),
                                 rootnode.get(),
                                 nullptr /*fileName*/,
                                 ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
                                 nullptr /*appData*/,
                                 false /*isSourceTemporary*/,
                                 false /*startFirst*/,
                                 nullptr /*cancelToken*/);
        ASSERT_EQ(API_OK, err) << "Cannot upload test file [" << fname << "] (error: " << err
                               << ")";
    };

    const std::string filename1 = UPFILE;
    const std::string filename1bkp1 = filename1 + ".bkp1";
    const std::string filename1bkp2 = filename1 + ".bkp2";
    const std::string filename2 = DOWNFILE;
    // Delays are added to ensure ordering in recent actions
    LOG_debug << "# SdkRecentsTest: uploading file " << filename1;
    updloadFile(filename1, "");
    WaitMillisec(1000);

    LOG_debug << "# SdkRecentsTest: uploading file " << filename1bkp1;
    updloadFile(filename1bkp1, "");
    WaitMillisec(1000);

    LOG_debug << "# SdkRecentsTest: uploading file " << filename1bkp2;
    updloadFile(filename1bkp2, "");
    WaitMillisec(1000);

    LOG_debug << "# SdkRecentsTest: updating file " << filename1;
    updloadFile(filename1, "update");
    WaitMillisec(1000);

    synchronousCatchup(0);

    LOG_debug << "# SdkRecentsTest: Marking file " << filename1 << " as sensitive";
    std::unique_ptr<MegaNode> f1node(megaApi[0]->getNodeByPath(("/" + filename1).c_str()));
    ASSERT_NE(f1node, nullptr);
    ASSERT_EQ(synchronousSetNodeSensitive(0, f1node.get(), true), API_OK)
        << "Error marking file as sensitive";

    LOG_debug << "# SdkRecentsTest: uploading file " << filename2;
    updloadFile(filename2, "");
    WaitMillisec(1000);

    LOG_debug << "# SdkRecentsTest: updating file " << filename2;
    updloadFile(filename2, "update");

    synchronousCatchup(0);

    LOG_debug << "# SdkRecentsTest: Get all recent actions (no exclusion)";
    RequestTracker trackerAll(megaApi[0].get());
    megaApi[0]->getRecentActionsAsync(1, 10, false, &trackerAll);

    ASSERT_EQ(trackerAll.waitForResult(), API_OK);
    std::unique_ptr<MegaRecentActionBucketList> buckets{
        trackerAll.request->getRecentActions()->copy()};

    ASSERT_TRUE(buckets != nullptr);
    auto bucketsVec = bucketsToVector(*buckets);
    ASSERT_TRUE(bucketsVec.size() > 1);
    EXPECT_THAT(bucketsVec[0], testing::ElementsAre(filename2, filename1));
    EXPECT_THAT(bucketsVec[1], testing::ElementsAre(filename1bkp2, filename1bkp1));

    LOG_debug << "# SdkRecentsTest: Get recent actions excluding sensitive nodes";
    RequestTracker trackerExclude(megaApi[0].get());
    megaApi[0]->getRecentActionsAsync(1, 10, true, &trackerExclude);

    ASSERT_EQ(trackerExclude.waitForResult(), API_OK);
    buckets.reset(trackerExclude.request->getRecentActions()->copy());

    ASSERT_TRUE(buckets != nullptr);
    bucketsVec = bucketsToVector(*buckets);
    ASSERT_TRUE(bucketsVec.size() > 1);
    EXPECT_THAT(bucketsVec[0], testing::ElementsAre(filename2));
    EXPECT_THAT(bucketsVec[1], testing::ElementsAre(filename1bkp2, filename1bkp1));
}

TEST_F(SdkTest, SdkTestStreamingRaidedTransferWithConnectionFailures)
{
    LOG_info << "___TEST Streaming Raided Transfer With Connection Failures___";
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    // Make sure our clients are working with pro plans.
    auto restorer0 = elevateToPro(*megaApi[0]);
    ASSERT_EQ(result(restorer0), API_OK);

    std::unique_ptr<MegaNode> rootnode{megaApi[0]->getRootNode()};
    ASSERT_NE(rootnode.get(), nullptr) << "Cannot retrieve RootNode";
    auto importRaidHandle =
        importPublicLink(0, MegaClient::MEGAURL + PUBLIC_IMAGE_URL, rootnode.get());
    std::shared_ptr<MegaNode> cloudRaidNode{megaApi[0]->getNodeByHandle(importRaidHandle)};
    ASSERT_NE(rootnode.get(), nullptr) << "Cannot get CloudRaidNode node from public link";

    megaApi[0]->setMaxDownloadSpeed(0);
    auto startStreaming = [cloudRaidNode, this](int cd404,
                                                int cd403,
                                                int cd429,
                                                int cd503,
                                                m_off_t nFailedReqs,
                                                const int streamingMinimumRateBps = 0,
                                                const long long downloadLimitBps = -1,
                                                unsigned int transfer_timeout_in_seconds = 180)
    {
        ASSERT_TRUE(DebugTestHook::resetForTests())
            << "SDK test hooks are not enabled in release mode";
#ifdef MEGASDK_DEBUG_TEST_HOOKS_ENABLED
        globalMegaTestHooks.onHttpReqPost = DebugTestHook::onHttpReqPostError;
        globalMegaTestHooks.onSetIsRaid = DebugTestHook::onSetIsRaid_morechunks;
        globalMegaTestHooks.onLimitMaxReqSize = ::mega::DebugTestHook::onLimitMaxReqSize;
        globalMegaTestHooks.onHookNumberOfConnections =
            ::mega::DebugTestHook::onHookNumberOfConnections;
#endif

        megaApi[0]->setStreamingMinimumRate(streamingMinimumRateBps);
        megaApi[0]->setMaxDownloadSpeed(downloadLimitBps);
        mApi[0].transferFlags[MegaTransfer::TYPE_DOWNLOAD] = false;
        DebugTestHook::countdownTo404 = cd404;
        DebugTestHook::countdownTo403 = cd403;
        DebugTestHook::countdownTo429 = cd429;
        DebugTestHook::countdownTo503 = cd503;
        std::unique_ptr<CheckStreamedFile_MegaTransferListener> p(
            StreamRaidFilePart(megaApi[0].get(),
                               0,
                               cloudRaidNode->getSize(),
                               true /*raid*/,
                               false,
                               cloudRaidNode.get(),
                               nullptr,
                               nullptr));

        ASSERT_TRUE(waitForResponse(&mApi[0].transferFlags[MegaTransfer::TYPE_DOWNLOAD],
                                    transfer_timeout_in_seconds))
            << "Cloudraid download with 404 and 403 errors time out (180 seconds)";
        ASSERT_EQ(API_OK, mApi[0].lastError)
            << "Cannot finish streaming download for the cloudraid file (error: "
            << mApi[0].lastError << ")";
        ASSERT_TRUE(cd404 < 0 || DebugTestHook::countdownTo404 < 0) << "";
        ASSERT_TRUE(cd403 < 0 || DebugTestHook::countdownTo403 < 0) << "";
        ASSERT_TRUE(cd429 < 0 || DebugTestHook::countdownTo404 < 0) << "";
        ASSERT_EQ(p->numFailedRequests, nFailedReqs)
            << "Unexpected number of retries for streaming download";
    };

    LOG_debug << "#### Test1: Streaming Download, no forced errors. No transfer retry ####";
    startStreaming(-1 /*cd404*/,
                   -1 /*cd403*/,
                   -1 /*cd429*/,
                   -1 /*cd503*/,
                   0 /*nFailedReqs*/,
                   0 /*streamingMinimumRateBps*/,
                   -1 /*downloadLimitBps*/,
                   180 /*timeout*/);

    LOG_debug << "#### Test2: Streaming Download, forcing 1 Raided Part Failure (404). No transfer "
                 "retry ####";
    startStreaming(2 /*cd404*/,
                   -1 /*cd403*/,
                   -1 /*cd429*/,
                   -1 /*cd503*/,
                   0 /*nFailedReqs*/,
                   0 /*streamingMinimumRateBps*/,
                   -1 /*downloadLimitBps*/,
                   180 /*timeout*/);

    LOG_debug << "#### Test3: Streaming Download forcing 2 Raided Parts Failures(403 | 503)."
                 "Transfer will be retried (onTransferTemporaryError received) ####";
    startStreaming(-1 /*cd404*/,
                   2 /*cd403*/,
                   -1 /*cd429*/,
                   2 /*cd503*/,
                   1 /*nFailedReqs*/,
                   0 /*streamingMinimumRateBps*/,
                   -1 /*downloadLimitBps*/,
                   180 /*timeout*/);

    LOG_debug << "#### Test4: Streaming Download limiting min streaming rate and max download "
                 "speed, no forced errors. No transfer retry ####";
    startStreaming(-1 /*cd404*/,
                   -1 /*cd403*/,
                   -1 /*cd429*/,
                   -1 /*cd503*/,
                   0 /*nFailedReqs*/,
                   0 /*streamingMinimumRateBps*/,
                   -1 /*downloadLimitBps*/,
                   180 /*timeout*/);

    LOG_debug << "#### Test5: Streaming Download limiting min streaming rate and max download "
                 "speed, forcing 1 Raided Part Failure (429). No transfer retry ####";
    startStreaming(-1 /*cd404*/,
                   -1 /*cd403*/,
                   2 /*cd429*/,
                   -1 /*cd503*/,
                   0 /*nFailedReqs*/,
                   0 /*streamingMinimumRateBps*/,
                   -1 /*downloadLimitBps*/,
                   180 /*timeout*/);

    LOG_debug << "#### Test6: Streaming Download limiting min streaming rate and max download "
                 "speed, forcing 2 Raided Parts Failures (403 | 503). Transfer will be retried "
                 "(onTransferTemporaryError received) ####";
    startStreaming(-1 /*cd404*/,
                   2 /*cd403*/,
                   -1 /*cd429*/,
                   2 /*cd503*/,
                   1 /*nFailedReqs*/,
                   30000 /*streamingMinimumRateBps*/,
                   300000 /*downloadLimitBps*/,
                   180 /*timeout*/);

    LOG_info
        << "___TEST Streaming Raided Transfer With Connection Failures. Tests cases completed___";
    ASSERT_TRUE(DebugTestHook::resetForTests()) << "SDK test hooks are not enabled in release mode";
}

TEST_F(SdkTest, SdkTestStreamingRaidedTransferBestCase)
{
    LOG_info << "___TEST Streaming Raided Transfer Best Case___";
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    // Make sure our clients are working with pro plans.
    auto restorer0 = elevateToPro(*megaApi[0]);
    ASSERT_EQ(result(restorer0), API_OK);

    std::unique_ptr<MegaNode> rootnode{megaApi[0]->getRootNode()};
    ASSERT_NE(rootnode.get(), nullptr) << "Cannot retrieve RootNode";
    std::string url100MB =
        "/#!JzckQJ6L!X_p0u26-HOTenAG0rATFhKdxYx-rOV1U6YHYhnz2nsA"; // https://mega.nz/file/JzckQJ6L#X_p0u26-HOTenAG0rATFhKdxYx-rOV1U6YHYhnz2nsA
    auto importRaidHandle = importPublicLink(0, MegaClient::MEGAURL + url100MB, rootnode.get());
    std::shared_ptr<MegaNode> cloudRaidNode{megaApi[0]->getNodeByHandle(importRaidHandle)};
    ASSERT_NE(rootnode.get(), nullptr) << "Cannot get CloudRaidNode node from public link";

    ASSERT_TRUE(DebugTestHook::resetForTests()) << "SDK test hooks are not enabled in release mode";
    mApi[0].transferFlags[MegaTransfer::TYPE_DOWNLOAD] = false;
    std::unique_ptr<CheckStreamedFile_MegaTransferListener> p(
        StreamRaidFilePart(megaApi[0].get(),
                           0,
                           cloudRaidNode->getSize(),
                           true /*raid*/,
                           false,
                           cloudRaidNode.get(),
                           nullptr,
                           nullptr));

    ASSERT_TRUE(waitForResponse(&mApi[0].transferFlags[MegaTransfer::TYPE_DOWNLOAD], 180))
        << "Cloudraid download with 404 and 403 errors time out (180 seconds)";
    ASSERT_EQ(API_OK, mApi[0].lastError)
        << "Cannot finish streaming download for the cloudraid file (error: " << mApi[0].lastError
        << ")";

    LOG_info << "___TEST Streaming Raided Transfer Best Case. Tests cases completed___";
    ASSERT_TRUE(DebugTestHook::resetForTests()) << "SDK test hooks are not enabled in release mode";
}

#if !USE_FREEIMAGE
TEST_F(SdkTest, DISABLED_SdkHttpReqCommandPutFATest)
#else
TEST_F(SdkTest, SdkHttpReqCommandPutFATest)
#endif
{
    LOG_info << "___TEST SdkHttpReqCommandPutFATest___";
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));
    ASSERT_TRUE(getFileFromArtifactory("test-data/" + IMAGEFILE, IMAGEFILE));

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
    std::string thumbnailPath = THUMBNAIL;
    ASSERT_EQ(API_OK, doGetThumbnail(0, n1.get(), thumbnailPath.c_str()));

    // Get the preview of the uploaded image
    std::string previewPath = PREVIEW;
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

TEST_F(SdkTest, SdkMediaImageUploadTest)
{
    LOG_info << "___TEST MediaUploadRequestURL___";
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));
    ASSERT_TRUE(getFileFromArtifactory("test-data/" + IMAGEFILE, IMAGEFILE));

    unsigned int apiIndex = 0;
    int64_t fileSize = 1304;
    const char* outputImage = "newlogo.png";
    synchronousMediaUpload(apiIndex, fileSize, IMAGEFILE.c_str(), IMAGEFILE_C.c_str(), outputImage
#if USE_FREEIMAGE
            ,THUMBNAIL.c_str(), PREVIEW.c_str()
#endif
            );

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

TEST_F(SdkTest, SdkGetPricing)
{
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));
    LOG_info << "___TEST GetPricing___";

    auto err = synchronousGetPricing(0);
    ASSERT_TRUE(err == API_OK) << "Get pricing failed (error: " << err << ")";

    ASSERT_TRUE(strcmp(mApi[0].mMegaCurrency->getCurrencyName(), "EUR") == 0) << "Unexpected currency";
    ASSERT_STREQ(mApi[0].mMegaCurrency->getLocalCurrencyName(), "")
        << "Local currency was not expected";

    ASSERT_GT(mApi[0].mMegaPricing->getNumProducts(), 0) << "No products available";
    for (int i = 0; i < mApi[0].mMegaPricing->getNumProducts(); ++i)
    {
        ASSERT_TRUE(mApi[0].mMegaPricing->getDescription(i)) << "Product description is empty";
        ASSERT_GT(mApi[0].mMegaPricing->getTestCategory(i), 0) << "Invalid value for test category in product \""
                                                              << mApi[0].mMegaPricing->getDescription(i) << "\"";
    }

    // Foce local currency to USD.
    err = synchronousGetPricing(0, "US");
    ASSERT_TRUE(err == API_OK) << "Get pricing in USD failed (error: " << err << ")";
    ASSERT_STREQ(mApi[0].mMegaCurrency->getLocalCurrencyName(), "USD")
        << "No USD local currency found.";
}

TEST_F(SdkTest, SdkGetBanners)
{
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));
    LOG_info << "___TEST GetBanners___";

    auto err = synchronousGetBanners(0);
    ASSERT_TRUE(err == API_OK || err == API_ENOENT) << "Get banners failed (error: " << err << ")";
}

TEST_F(SdkTest, SdkSimpleCommands)
{
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));
    LOG_info << "___TEST SimpleCommands___";

    // fetchTimeZone() test
    auto err = synchronousFetchTimeZone(0);
    ASSERT_EQ(API_OK, err) << "Fetch time zone failed (error: " << err << ")";
    ASSERT_TRUE(mApi[0].tzDetails && mApi[0].tzDetails->getNumTimeZones()) << "Invalid Time Zone details"; // some simple validation

    // getABTestValue() -- logged in.
    ASSERT_GE(megaApi[0]->getABTestValue("devtest"), 1u);
    ASSERT_EQ(megaApi[0]->getABTestValue("devtest_inexistent_flag"), 0u);

    // getFlag()
    std::unique_ptr<MegaFlag> flagAB{ megaApi[0]->getFlag("devtest") };
    ASSERT_EQ(flagAB->getType(), static_cast<decltype(flagAB->getType())>(MegaFlag::FLAG_TYPE_AB_TEST));
    ASSERT_GE(flagAB->getGroup(), 1u);
    std::unique_ptr<MegaFlag> flagF{ megaApi[0]->getFlag("dmca") };
    ASSERT_THAT(
        flagF->getType(),
        ::testing::AnyOf(static_cast<decltype(flagF->getType())>(MegaFlag::FLAG_TYPE_AB_TEST),
                         static_cast<decltype(flagF->getType())>(MegaFlag::FLAG_TYPE_FEATURE)));
    ASSERT_GE(flagF->getGroup(), 1u);

    logout(0, false, maxTimeout);
    gSessionIDs[0] = "invalid";

    // getMiscFlags() -- not logged in
    err = synchronousGetMiscFlags(0);
    ASSERT_EQ(API_OK, err) << "Get misc flags failed (error: " << err << ")";

    // getABTestValue() -- not logged in
    ASSERT_EQ(megaApi[0]->getABTestValue("devtest"), 0u);

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

    auto validateString = [](const char* value)
    {
        ASSERT_NE(value, nullptr);
        ASSERT_NE(*value, '\0');
    };

    RequestTracker listener{megaApi[0].get()};
    megaApi[0]->getMyIp(&listener);
    ASSERT_EQ(listener.waitForResult(), API_OK);
    ASSERT_NO_FATAL_FAILURE(validateString(listener.request->getName())); // Country code
    ASSERT_NO_FATAL_FAILURE(validateString(listener.request->getText())); // IP address}
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
    err = synchronousUpdateBackup(0, mApi[0].getBackupId(), MegaApi::BACKUP_TYPE_INVALID, UNDEF, nullptr, nullptr, -1, -1);
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
    err = synchronousSendBackupHeartbeat(0, mApi[0].getBackupId(), 1, 10, 1, 1, 0, targetNodes[0]);
    ASSERT_EQ(API_OK, err) << "sendBackupHeartbeat failed (error: " << err << ")";


    // --- negative test cases ---

    // register the same backup twice: should work fine
    err = synchronousSetBackup(0,
        [&](MegaError& e, MegaRequest& r) {
            if (e.getErrorCode() == API_OK) backupNameToBackupId.emplace_back(r.getName(), r.getParentHandle());
        },
        backupType, targetNodes[0], localFolder.c_str(), backupNames[0].c_str(), state, subState);

    ASSERT_EQ(API_OK, err) << "setBackup failed (error: " << err << ")";

    // update a removed backup: should throw an error
    err = synchronousRemoveBackup(0, mApi[0].getBackupId(), nullptr);
    ASSERT_EQ(API_OK, err) << "removeBackup failed (error: " << err << ")";
    err = synchronousUpdateBackup(0, mApi[0].getBackupId(), BackupType::INVALID, UNDEF, nullptr, nullptr, -1, -1);
    ASSERT_EQ(API_OK, err) << "updateBackup for deleted backup should succeed now, and revive the record. But, error: " << err;

    // We can't test this, as reviewer wants an assert to fire for EARGS
    //// create a backup with a big status: should report an error
    //err = synchronousSetBackup(0,
    //        nullptr,
    //        backupType, targetNodes[0], localFolder.c_str(), backupNames[0].c_str(), 255/*state*/, subState);
    //ASSERT_NE(API_OK, err) << "setBackup failed (error: " << err << ")";
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

    std::string subFolder = "sub-folder-A";
    nh = createFolder(0, subFolder.c_str(), folderA.get());
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
    ASSERT_EQ(mApi[0].getFavNodeCount(), 2u) << "synchronousGetFavourites failed...";
    err = synchronousGetFavourites(0, nullptr, 1);
    ASSERT_EQ(mApi[0].getFavNodeCount(), 1u) << "synchronousGetFavourites failed...";
    unique_ptr<MegaNode> favNode(megaApi[0]->getNodeByHandle(mApi[0].getFavNode(0)));
    ASSERT_EQ(favNode->getName(), subFolder) << "synchronousGetFavourites failed with node passed nullptr";


    LOG_debug << "\t# Set versioned node as favourite";
    std::set<std::string> tmpFileNames = {"n1", "n2", "n3", "n4"};
    MegaHandle fileHandle = INVALID_HANDLE;
    std::for_each(std::begin(tmpFileNames), std::end(tmpFileNames),
       [this, &fileHandle, folder = std::unique_ptr<MegaNode>(megaApi[0]->getRootNode())]
       (const auto& localFileName)
    {
        static int vNum = 1;
        createFile(localFileName, false, std::to_string(vNum++));

        const auto prevHandle = fileHandle;
        ASSERT_EQ(MegaError::API_OK,
                  doStartUpload(0,
                                &fileHandle,
                                localFileName.c_str(),
                                folder.get(),
                                "versionedFileName.txt",
                                ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
                                nullptr /*appData*/,
                                false /*isSourceTemporary*/,
                                false /*startFirst*/,
                                nullptr /*cancelToken*/))
            << "Cannot upload test file version #" << vNum;
        if (prevHandle == INVALID_HANDLE)
        {
            ASSERT_NE(fileHandle, INVALID_HANDLE) << "Invalid handle retrieved for newly uploaded file";
        }
        else
        {
            ASSERT_NE(fileHandle, prevHandle) << "Already existing handle received";
        }

        deleteFile(localFileName);
    });
    unique_ptr<MegaNode> versionedFileNode(megaApi[0]->getNodeByHandle(fileHandle));
    ASSERT_TRUE(versionedFileNode);
    unique_ptr<MegaNodeList> allVersions(megaApi[0]->getVersions(versionedFileNode.get()));
    ASSERT_EQ(allVersions->size(), tmpFileNames.size());

    ASSERT_EQ(MegaError::API_OK, synchronousSetNodeFavourite(0, versionedFileNode.get(), true))
        << "Setting favourite attribute for versioned file failed";

    const int howMany = 0; // all nodes
    ASSERT_EQ(MegaError::API_OK, synchronousGetFavourites(0, nullptr /*from Root*/, howMany));
    ASSERT_EQ(mApi[0].getFavNodeCount(), 3u) << "Error counting new versioned node set as favourite";
}

// tests for Sensntive files flag on files and folders
// includes tests of MegaApi::search() with filters
TEST_F(SdkTest, SdkSensitiveNodes)
{
    LOG_info << "___TEST SDKSensitive___";
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(2));

    ASSERT_TRUE(getFileFromArtifactory("test-data/" + IMAGEFILE, IMAGEFILE));

    unique_ptr<MegaNode> rootnodeA(megaApi[0]->getRootNode());

    ASSERT_TRUE(rootnodeA);

    // /
    //    folder-A/              // top shared
    //        abFile1.png
    //        acSensitiveFile.png  <- sensitive
    //        sub-folder-A/       <- sensitive
    //             aaLogo.png

    string folderAName = "folder-A";
    MegaHandle nh = createFolder(0, folderAName.c_str(), rootnodeA.get());
    ASSERT_NE(nh, UNDEF);
    unique_ptr<MegaNode> folderA(megaApi[0]->getNodeByHandle(nh));
    ASSERT_TRUE(!!folderA);

    string subFolderAName = "sub-folder-A";
    MegaHandle snh = createFolder(0, subFolderAName.c_str(), folderA.get());
    ASSERT_NE(snh, UNDEF);
    unique_ptr<MegaNode> subFolderA(megaApi[0]->getNodeByHandle(snh));
    ASSERT_TRUE(!!subFolderA);

    // all 3 files have "a" in the name
    string filename1 = "aaLogo.png";
    MegaHandle fh = UNDEF;
    ASSERT_EQ(MegaError::API_OK, doStartUpload(0, &fh, IMAGEFILE.c_str(),
        subFolderA.get(),
        filename1.c_str() /*fileName*/,
        ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
        nullptr /*appData*/,
        false   /*isSourceTemporary*/,
        false   /*startFirst*/,
        nullptr /*cancelToken*/)) << "Cannot upload a test file";
    std::unique_ptr<MegaNode> thefile(megaApi[0]->getNodeByHandle(fh));
    bool null_pointer = (thefile.get() == nullptr);
    ASSERT_FALSE(null_pointer) << "Cannot initialize test scenario (error: " << mApi[0].lastError << ")";

    string nsfilename = "abFile1.png";
    MegaHandle fh2 = UNDEF;
    ASSERT_EQ(MegaError::API_OK, doStartUpload(0, &fh2, IMAGEFILE.c_str(),
        folderA.get(),
        nsfilename.c_str() /*fileName*/,
        ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
        nullptr /*appData*/,
        false   /*isSourceTemporary*/,
        false   /*startFirst*/,
        nullptr /*cancelToken*/)) << "Cannot upload a test file";
    std::unique_ptr<MegaNode> nsfile(megaApi[0]->getNodeByHandle(fh2));

    string sfilename = "acSensitiveFile.png";
    MegaHandle fh3 = UNDEF;
    ASSERT_EQ(MegaError::API_OK, doStartUpload(0, &fh3, IMAGEFILE.c_str(),
        folderA.get(),
        sfilename.c_str() /*fileName*/,
        ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
        nullptr /*appData*/,
        false   /*isSourceTemporary*/,
        false   /*startFirst*/,
        nullptr /*cancelToken*/)) << "Cannot upload a test file";
    std::unique_ptr<MegaNode> sfile(megaApi[0]->getNodeByHandle(fh3));

    // setuip sharing from
    ASSERT_EQ(API_OK, synchronousInviteContact(0, mApi[1].email.c_str(), "SdkSensitiveNodes contact request A to B", MegaContactRequest::INVITE_ACTION_ADD));
    ASSERT_TRUE(WaitFor([this]() {return unique_ptr<MegaContactRequestList>(megaApi[1]->getIncomingContactRequests())->size() == 1; }, 60000));
    ASSERT_NO_FATAL_FAILURE(getContactRequest(1, false));
    ASSERT_EQ(API_OK, synchronousReplyContactRequest(1, mApi[1].cr.get(), MegaContactRequest::REPLY_ACTION_ACCEPT));

    // Verify credentials in both accounts
    if (gManualVerification)
    {
        if (!areCredentialsVerified(0, mApi[1].email)) {ASSERT_NO_FATAL_FAILURE(verifyCredentials(0, mApi[1].email));}
        if (!areCredentialsVerified(1, mApi[0].email)) {ASSERT_NO_FATAL_FAILURE(verifyCredentials(1, mApi[0].email));}
    }

    ASSERT_EQ(unsigned(unique_ptr<MegaShareList>(megaApi[1]->getInSharesList())->size()), 0u);
    unique_ptr<MegaUser> user1(megaApi[1]->getContact(mApi[0].email.c_str()));
    {
        unique_ptr<MegaNodeList> nl2(megaApi[1]->getInShares(user1.get()));
        ASSERT_EQ(nl2->size(), 0); // should be no shares
    }
    ASSERT_NO_FATAL_FAILURE(shareFolder(folderA.get(), mApi[1].email.c_str(), MegaShare::ACCESS_READ));
    ASSERT_TRUE(WaitFor([this]() { return unique_ptr<MegaShareList>(megaApi[1]->getInSharesList())->size() == 1; }, 60*1000));
    ASSERT_EQ(unsigned(unique_ptr<MegaShareList>(megaApi[1]->getInSharesList())->size()), 1u);

    // Wait for the inshare node to be decrypted
    ASSERT_TRUE(WaitFor([this, &folderA]() { return unique_ptr<MegaNode>(megaApi[1]->getNodeByHandle(folderA->getHandle()))->isNodeKeyDecrypted(); }, 60*1000));

    unique_ptr<MegaUser> user(megaApi[1]->getContact(mApi[0].email.c_str()));
    unique_ptr<MegaNodeList> nl1(megaApi[1]->getInShares(user.get()));

    synchronousSetNodeSensitive(0, sfile.get(), true);
    synchronousSetNodeSensitive(0, subFolderA.get(), true);

    function<bool()> anyShares = [&]() {
        unique_ptr<MegaNodeList> nl2(megaApi[1]->getInShares(user1.get()));
        return nl2->size() != 0;
    };
    ASSERT_TRUE(WaitFor(anyShares, 30 * 1000)); // 30 sec

    unique_ptr<MegaNodeList> nl2(megaApi[1]->getInShares(user1.get()));
    ASSERT_EQ(nl2->size(), 1);

    ASSERT_EQ(nl2->get(0)->isMarkedSensitive(), false);
    function<bool()> sharedSubFolderSensitive = [&]() {
        unique_ptr <MegaNode> sharedSubFolderA(megaApi[1]->getNodeByPath(subFolderAName.c_str(), nl2->get(0)));
        if (!sharedSubFolderA.get())
            return false;
        return sharedSubFolderA->isMarkedSensitive();
    };
    ASSERT_TRUE(WaitFor(sharedSubFolderSensitive, 60 * 1000)); // share has gained attributes

    unique_ptr <MegaNode> sharedSubFolderA(megaApi[1]->getNodeByPath(subFolderAName.c_str(), nl2->get(0)));
    ASSERT_TRUE(sharedSubFolderA) << "Share " << nl2->get(0)->getName() << '/' << subFolderAName << " not found";
    ASSERT_EQ(sharedSubFolderA->isMarkedSensitive(), true) << "Share " << nl2->get(0)->getName() << '/' << subFolderAName << " found but not sensitive";

    // ---------------------------------------------------------------------------------------------------------------------------

    subFolderA.reset(megaApi[0]->getNodeByPath((string("/") + folderAName + "/" + subFolderAName).c_str(), unique_ptr<MegaNode>(megaApi[0]->getRootNode()).get()));
    ASSERT_TRUE(!!subFolderA);
    ASSERT_TRUE(subFolderA->isMarkedSensitive());

    bool msen = subFolderA->isMarkedSensitive();
    ASSERT_EQ(msen, true);
    bool sen = megaApi[0]->isSensitiveInherited(subFolderA.get());
    ASSERT_EQ(sen, true);
    sen = megaApi[0]->isSensitiveInherited(thefile.get());
    ASSERT_EQ(sen, true);
    sen = megaApi[0]->isSensitiveInherited(sfile.get());
    ASSERT_EQ(sen, true);
    sen = megaApi[0]->isSensitiveInherited(nsfile.get());
    ASSERT_EQ(sen, false);
    sen = megaApi[0]->isSensitiveInherited(folderA.get());
    ASSERT_EQ(sen, false);
    sen = megaApi[0]->isSensitiveInherited(rootnodeA.get());
    ASSERT_EQ(sen, false);

    // inherited sensitive flag
    // specifeid searh string
    std::unique_ptr<MegaSearchFilter> filterResults(MegaSearchFilter::createInstance());
    filterResults->byName("logo");
    filterResults->byLocationHandle(rootnodeA->getHandle());
    std::unique_ptr<MegaNodeList> list(megaApi[0]->search(filterResults.get(), MegaApi::ORDER_DEFAULT_ASC));
    ASSERT_EQ(list->size(), 1);

    filterResults.reset(MegaSearchFilter::createInstance());
    filterResults->byName("logo");
    filterResults->byLocationHandle(rootnodeA->getHandle());
    filterResults->bySensitivity(MegaSearchFilter::BOOL_FILTER_ONLY_TRUE);
    list.reset(megaApi[0]->search(filterResults.get(), MegaApi::ORDER_DEFAULT_ASC));
    ASSERT_EQ(list->size(), 0);

    // inherited sensitive flag
    // no specifeid searh string
    filterResults.reset(MegaSearchFilter::createInstance());
    filterResults->byLocationHandle(rootnodeA->getHandle());
    filterResults->byCategory(MegaApi::FILE_TYPE_PHOTO);
    list.reset(megaApi[0]->search(filterResults.get(), MegaApi::ORDER_DEFAULT_ASC));
    ASSERT_EQ(list->size(), 3);
    ASSERT_EQ(list->get(0)->getName(), filename1);
    ASSERT_EQ(list->get(1)->getName(), nsfilename);
    ASSERT_EQ(list->get(2)->getName(), sfilename);

    filterResults.reset(MegaSearchFilter::createInstance());
    filterResults->byLocationHandle(rootnodeA->getHandle());
    filterResults->byCategory(MegaApi::FILE_TYPE_PHOTO);
    filterResults->bySensitivity(MegaSearchFilter::BOOL_FILTER_ONLY_TRUE);
    list.reset(megaApi[0]->search(filterResults.get(), MegaApi::ORDER_DEFAULT_ASC));
    ASSERT_EQ(list->size(), 1);
    ASSERT_EQ(list->get(0)->getName(), nsfilename);

    // no node, specifeid searh string: SEARCH_TARGET_ALL: getNodesByMimeType()
    filterResults.reset(MegaSearchFilter::createInstance());
    filterResults->byLocation(MegaApi::SEARCH_TARGET_ROOTNODE);
    filterResults->byCategory(MegaApi::FILE_TYPE_PHOTO);
    list.reset(megaApi[0]->search(filterResults.get(), MegaApi::ORDER_DEFAULT_ASC));
    ASSERT_EQ(list->size(), 3);
    ASSERT_EQ(list->get(0)->getName(), filename1);
    ASSERT_EQ(list->get(1)->getName(), nsfilename);
    ASSERT_EQ(list->get(2)->getName(), sfilename);

    filterResults.reset(MegaSearchFilter::createInstance());
    filterResults->byLocation(MegaApi::SEARCH_TARGET_ROOTNODE);
    filterResults->byCategory(MegaApi::FILE_TYPE_PHOTO);
    filterResults->bySensitivity(MegaSearchFilter::BOOL_FILTER_ONLY_TRUE);
    list.reset(megaApi[0]->search(filterResults.get(), MegaApi::ORDER_DEFAULT_ASC));
    ASSERT_EQ(list->size(), 1); // non sensitive files (recursive exclude)
    ASSERT_EQ(list->get(0)->getName(), nsfilename);

    filterResults.reset(MegaSearchFilter::createInstance());
    filterResults->byLocation(MegaApi::SEARCH_TARGET_ROOTNODE);
    filterResults->byCategory(MegaApi::FILE_TYPE_AUDIO);
    filterResults->bySensitivity(MegaSearchFilter::BOOL_FILTER_ONLY_TRUE);
    list.reset(megaApi[0]->search(filterResults.get(), MegaApi::ORDER_DEFAULT_ASC));
    ASSERT_EQ(list->size(), 0);

    // no node, specifid search string: SEARCH_TARGET_ROOTNODE
    filterResults.reset(MegaSearchFilter::createInstance());
    filterResults->byName("a");
    filterResults->byLocation(MegaApi::SEARCH_TARGET_ROOTNODE);
    filterResults->byCategory(MegaApi::FILE_TYPE_PHOTO);
    list.reset(megaApi[0]->search(filterResults.get(), MegaApi::ORDER_DEFAULT_ASC));
    ASSERT_EQ(list->size(), 3);
    ASSERT_EQ(list->get(0)->getName(), filename1);
    ASSERT_EQ(list->get(1)->getName(), nsfilename);
    ASSERT_EQ(list->get(2)->getName(), sfilename);

    filterResults.reset(MegaSearchFilter::createInstance());
    filterResults->byName("a");
    filterResults->byLocation(MegaApi::SEARCH_TARGET_ROOTNODE);
    filterResults->byCategory(MegaApi::FILE_TYPE_PHOTO);
    filterResults->bySensitivity(MegaSearchFilter::BOOL_FILTER_ONLY_TRUE);
    list.reset(megaApi[0]->search(filterResults.get(), MegaApi::ORDER_DEFAULT_ASC));
    ASSERT_EQ(list->size(), 1); // non sensitive files (recursive exclude)
    ASSERT_EQ(list->get(0)->getName(), nsfilename);

    // no node, specified search string: SEARCH_TARGET_ALL main non recursive
    // folderA
    filterResults.reset(MegaSearchFilter::createInstance());
    filterResults->byName("a");
    filterResults->byLocationHandle(folderA->getHandle());
    filterResults->byCategory(MegaApi::FILE_TYPE_PHOTO);
    list.reset(megaApi[0]->getChildren(filterResults.get(), MegaApi::ORDER_DEFAULT_ASC));
    ASSERT_EQ(list->size(), 2);
    ASSERT_EQ(list->get(0)->getName(), nsfilename);
    ASSERT_EQ(list->get(1)->getName(), sfilename);

    filterResults.reset(MegaSearchFilter::createInstance());
    filterResults->byName("a");
    filterResults->byLocationHandle(folderA->getHandle());
    filterResults->byCategory(MegaApi::FILE_TYPE_PHOTO);
    filterResults->bySensitivity(MegaSearchFilter::BOOL_FILTER_ONLY_TRUE);
    list.reset(megaApi[0]->getChildren(filterResults.get(), MegaApi::ORDER_DEFAULT_ASC));
    ASSERT_EQ(list->size(), 1); // non sensitive files (recursive exclude)
    ASSERT_EQ(list->get(0)->getName(), nsfilename);

    // no node, specified search string: main non recursive
    // subfolderA
    filterResults.reset(MegaSearchFilter::createInstance());
    filterResults->byName("a");
    filterResults->byLocationHandle(subFolderA->getHandle());
    filterResults->byCategory(MegaApi::FILE_TYPE_PHOTO);
    list.reset(megaApi[0]->getChildren(filterResults.get(), MegaApi::ORDER_DEFAULT_ASC));
    ASSERT_EQ(list->size(), 1);
    ASSERT_EQ(list->get(0)->getName(), filename1);

    filterResults.reset(MegaSearchFilter::createInstance());
    filterResults->byName("a");
    filterResults->byLocationHandle(subFolderA->getHandle());
    filterResults->byCategory(MegaApi::FILE_TYPE_PHOTO);
    filterResults->bySensitivity(MegaSearchFilter::BOOL_FILTER_ONLY_TRUE);
    list.reset(megaApi[0]->getChildren(filterResults.get(), MegaApi::ORDER_DEFAULT_ASC));
    ASSERT_EQ(list->size(), 0); // non sensitive files (recursive exclude)

    // no node, specifid search string: SEARCH_TARGET_INSHARE
    filterResults.reset(MegaSearchFilter::createInstance());
    filterResults->byName("a");
    filterResults->byLocation(MegaApi::SEARCH_TARGET_INSHARE);
    filterResults->byCategory(MegaApi::FILE_TYPE_PHOTO);
    list.reset(megaApi[1]->search(filterResults.get(), MegaApi::ORDER_DEFAULT_ASC));
    ASSERT_EQ(list->size(), 3);
    ASSERT_EQ(list->get(0)->getName(), filename1);
    ASSERT_EQ(list->get(1)->getName(), nsfilename);
    ASSERT_EQ(list->get(2)->getName(), sfilename);

    filterResults.reset(MegaSearchFilter::createInstance());
    filterResults->byName("a");
    filterResults->byLocation(MegaApi::SEARCH_TARGET_INSHARE);
    filterResults->byCategory(MegaApi::FILE_TYPE_PHOTO);
    filterResults->bySensitivity(MegaSearchFilter::BOOL_FILTER_ONLY_TRUE);
    list.reset(megaApi[1]->search(filterResults.get(), MegaApi::ORDER_DEFAULT_ASC));
    ASSERT_EQ(list->size(), 1); // non sensitive files (recursive exclude)
    ASSERT_EQ(list->get(0)->getName(), nsfilename);

    // no node, specifid search string: SEARCH_TARGET_OUTSHARE
    filterResults.reset(MegaSearchFilter::createInstance());
    filterResults->byName("a");
    filterResults->byLocation(MegaApi::SEARCH_TARGET_OUTSHARE);
    filterResults->byCategory(MegaApi::FILE_TYPE_PHOTO);
    list.reset(megaApi[0]->search(filterResults.get(), MegaApi::ORDER_DEFAULT_ASC));
    ASSERT_EQ(list->size(), 3);
    ASSERT_EQ(list->get(0)->getName(), filename1);
    ASSERT_EQ(list->get(1)->getName(), nsfilename);
    ASSERT_EQ(list->get(2)->getName(), sfilename);

    filterResults.reset(MegaSearchFilter::createInstance());
    filterResults->byName("a");
    filterResults->byLocation(MegaApi::SEARCH_TARGET_OUTSHARE);
    filterResults->byCategory(MegaApi::FILE_TYPE_PHOTO);
    filterResults->bySensitivity(MegaSearchFilter::BOOL_FILTER_ONLY_TRUE);
    list.reset(megaApi[0]->search(filterResults.get(), MegaApi::ORDER_DEFAULT_ASC));
    ASSERT_EQ(list->size(), 1); // non sensitive files (recursive exclude)
    ASSERT_EQ(list->get(0)->getName(), nsfilename);

    filterResults.reset(MegaSearchFilter::createInstance());
    filterResults->byCategory(MegaApi::FILE_TYPE_OTHERS);
    list.reset(megaApi[0]->search(filterResults.get(), MegaApi::ORDER_DEFAULT_ASC));
    ASSERT_EQ(list->size(), 0);

    filterResults.reset(MegaSearchFilter::createInstance());
    filterResults->bySensitivity(MegaSearchFilter::BOOL_FILTER_ONLY_FALSE);
    filterResults->byLocationHandle(subFolderA->getHandle());
    list.reset(megaApi[0]->getChildren(filterResults.get(), MegaApi::ORDER_DEFAULT_ASC));
    ASSERT_EQ(list->size(), 0);

    filterResults.reset(MegaSearchFilter::createInstance());
    filterResults->bySensitivity(MegaSearchFilter::BOOL_FILTER_ONLY_FALSE);
    filterResults->byLocationHandle(folderA->getHandle());
    list.reset(megaApi[0]->getChildren(filterResults.get(), MegaApi::ORDER_DEFAULT_ASC));
    ASSERT_EQ(list->size(), 2);

    filterResults.reset(MegaSearchFilter::createInstance());
    filterResults->byLocation(MegaApi::SEARCH_TARGET_ROOTNODE);
    filterResults->bySensitivity(MegaSearchFilter::BOOL_FILTER_ONLY_FALSE);
    list.reset(megaApi[0]->search(filterResults.get(), MegaApi::ORDER_DEFAULT_ASC));
    ASSERT_EQ(list->size(), 2);

    filterResults.reset(MegaSearchFilter::createInstance());
    filterResults->byLocationHandle(folderA->getHandle());
    filterResults->bySensitivity(MegaSearchFilter::BOOL_FILTER_ONLY_TRUE);
    list.reset(megaApi[0]->search(filterResults.get(), MegaApi::ORDER_DEFAULT_ASC));
    ASSERT_EQ(list->size(), 1);
}

TEST_F(SdkTest, SdkDeviceNames)
{
    /// Run this before other tests that use device name, like SdkBackupFolder

    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));
    LOG_info << "___TEST SdkDeviceNames___";

    // test setter/getter for current device name
    string deviceName = string("SdkDeviceNamesTest_") + getCurrentTimestamp(true);
    ASSERT_EQ(API_OK, doSetDeviceName(0, nullptr, deviceName.c_str())) << "setDeviceName failed";
    RequestTracker getDeviceNameTracker1(megaApi[0].get());
    megaApi[0]->getDeviceName(nullptr, &getDeviceNameTracker1);
    ASSERT_EQ(getDeviceNameTracker1.waitForResult(), API_OK);
    ASSERT_TRUE(getDeviceNameTracker1.request->getName());
    ASSERT_EQ(deviceName, getDeviceNameTracker1.request->getName());
    ASSERT_TRUE(getDeviceNameTracker1.request->getMegaStringMap());

    // test getting current device name when it was not set
    ASSERT_EQ(API_OK, doSetDeviceName(0, nullptr, "")) << "removing current device name failed";
    RequestTracker getDeviceNameTracker2(megaApi[0].get());
    megaApi[0]->getDeviceName(nullptr, &getDeviceNameTracker2);
    ASSERT_EQ(getDeviceNameTracker2.waitForResult(), API_ENOENT);
    ASSERT_FALSE(getDeviceNameTracker2.request->getName());
    ASSERT_TRUE(getDeviceNameTracker2.request->getMegaStringMap());

    // test getting all device names, when current device name was not set
    RequestTracker noNameTracker(megaApi[0].get());
    megaApi[0]->getUserAttribute(MegaApi::USER_ATTR_DEVICE_NAMES, &noNameTracker);
    ASSERT_EQ(API_OK, noNameTracker.waitForResult()) << "getUserAttribute failed when name of current device was not set";
    ASSERT_FALSE(noNameTracker.request->getName()) << "getUserAttribute set some bogus name for current device";
    ASSERT_TRUE(noNameTracker.request->getMegaStringMap());

    // test getting all device names, when current device name was set
    ASSERT_EQ(API_OK, doSetDeviceName(0, nullptr, deviceName.c_str())) << "setDeviceName failed";
    RequestTracker getDeviceNameTracker3(megaApi[0].get());
    megaApi[0]->getUserAttribute(MegaApi::USER_ATTR_DEVICE_NAMES, &getDeviceNameTracker3);
    ASSERT_EQ(API_OK, getDeviceNameTracker3.waitForResult());
    ASSERT_FALSE(getDeviceNameTracker3.request->getName());
    ASSERT_TRUE(getDeviceNameTracker3.request->getMegaStringMap());
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
    [[maybe_unused]] bool deviceNameWasSetByCurrentTest = false;
    if (doGetDeviceName(0, &deviceName, nullptr) != API_OK || deviceName.empty())
    {
        deviceName = "Jenkins " + timestamp;
        doSetDeviceName(0, nullptr, deviceName.c_str());

        // make sure Device Name attr was set
        string deviceNameInCloud;
        ASSERT_EQ(doGetDeviceName(0, &deviceNameInCloud, nullptr), API_OK) << "Getting device name attr failed";
        ASSERT_EQ(deviceName, deviceNameInCloud) << "Getting device name attr failed (wrong value)";
        deviceNameWasSetByCurrentTest = true;
    }

#ifdef ENABLE_SYNC
    // Make sure My Backups folder was created
    ASSERT_NO_FATAL_FAILURE(syncTestEnsureMyBackupsRemoteFolderExists(0));
    MegaHandle mh = mApi[0].lastSyncBackupId;

    // Create a test root directory
    fs::path localBasePath = makeNewTestRoot();

    ASSERT_NO_FATAL_FAILURE(cleanUp(megaApi[0].get(), localBasePath));

    // request to backup a folder
    fs::path localFolderPath = localBasePath / "LocalBackedUpFolder";
    fs::create_directories(localFolderPath);
    const auto testFile = localFolderPath / UPFILE;
    ASSERT_TRUE(createFile(testFile.string(), false)) << "Failed to create file " << testFile.string();
    const string backupNameStr = string("RemoteBackupFolder_") + timestamp;
    const char* backupName = backupNameStr.c_str();
    MegaHandle newSyncRootNodeHandle = UNDEF;
    int err = synchronousSyncFolder(0, &newSyncRootNodeHandle, MegaSync::TYPE_BACKUP, localFolderPath.u8string().c_str(), backupName, INVALID_HANDLE, nullptr);
    ASSERT_TRUE(err == API_OK) << "Backup folder failed (error: " << err << ")";
    handle bkpId = mApi[0].lastSyncBackupId;

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
    PerApi& target = mApi[0];
    target.resetlastEvent();

    // Verify that the sync was added
    unique_ptr<MegaSync> newBkp(megaApi[0]->getSyncByBackupId(bkpId));
    ASSERT_TRUE(newBkp);
    ASSERT_EQ(newBkp->getType(), MegaSync::TYPE_BACKUP);
    ASSERT_EQ(newBkp->getMegaHandle(), newSyncRootNodeHandle);
    ASSERT_STREQ(newBkp->getName(), backupName);
    ASSERT_STREQ(newBkp->getLastKnownMegaFolder(), actualRemotePath.get());
    ASSERT_TRUE(newBkp->getRunState() == MegaSync::RUNSTATE_RUNNING) << "Backup instance found but not active.";

    // Wait for the node database to be updated.
    // If nothing changed, there won't be an update
    //ASSERT_TRUE(WaitFor([&target](){ return target.lastEventsContain(MegaEvent::EVENT_COMMIT_DB); }, 8192));

    // Verify sync after logout / login
    string session = unique_ptr<char[]>(dumpSession()).get();
    locallogout();
    auto tracker = asyncRequestFastLogin(0, session.c_str());
    ASSERT_EQ(API_OK, tracker->waitForResult()) << " Failed to establish a login/session for account " << 0;

    target.resetlastEvent();

    fetchnodes(0, maxTimeout); // auto-resumes one active backup

    ASSERT_TRUE(WaitFor([&target](){ return target.lastEventsContain(MegaEvent::EVENT_SYNCS_RESTORED); }, 10000));

    // Verify the sync again
    newBkp.reset(megaApi[0]->getSyncByBackupId(bkpId));
    ASSERT_TRUE(newBkp);
    ASSERT_EQ(newBkp->getType(), MegaSync::TYPE_BACKUP);
    ASSERT_EQ(newBkp->getMegaHandle(), newSyncRootNodeHandle);
    ASSERT_STREQ(newBkp->getName(), backupName);
    ASSERT_STREQ(newBkp->getLastKnownMegaFolder(), actualRemotePath.get());
    ASSERT_TRUE(newBkp->getRunState() == MegaSync::RUNSTATE_RUNNING) << "Backup instance found but not active after logout & login.";

    // make sure that client is up to date (upon logout, recent changes might not be committed to DB,
    // which may result on the new node not being available yet).
    size_t times = 10;
    while (times--)
    {
        if (target.lastEventsContain(MegaEvent::EVENT_NODES_CURRENT)) break;
        std::this_thread::sleep_for(std::chrono::seconds{1});
    }
    ASSERT_TRUE(target.lastEventsContain(MegaEvent::EVENT_NODES_CURRENT))
        << "Timeout expired to receive actionpackets";

    // disable backup
    RequestTracker disableBkpTracker(megaApi[0].get());
    megaApi[0]->setSyncRunState(bkpId, MegaSync::RUNSTATE_DISABLED, &disableBkpTracker);
    ASSERT_EQ(API_OK, disableBkpTracker.waitForResult());
    // remove local file from backup
    EXPECT_TRUE(fs::remove(testFile)) << "Failed to remove file " << testFile.string();
    // enable backup
    RequestTracker enableBkpTracker(megaApi[0].get());
    megaApi[0]->setSyncRunState(bkpId, MegaSync::RUNSTATE_RUNNING, &enableBkpTracker);
    ASSERT_EQ(API_OK, enableBkpTracker.waitForResult());

    // Remove registered backup
    RequestTracker removeTracker(megaApi[0].get());
    megaApi[0]->removeSync(bkpId, &removeTracker);
    ASSERT_EQ(API_OK, removeTracker.waitForResult());

    RequestTracker removeNodesTracker(megaApi[0].get());
    megaApi[0]->moveOrRemoveDeconfiguredBackupNodes(newBkp->getMegaHandle(), INVALID_HANDLE, &removeNodesTracker);
    ASSERT_EQ(API_OK, removeNodesTracker.waitForResult());

    newBkp.reset(megaApi[0]->getSyncByBackupId(bkpId));
    ASSERT_FALSE(newBkp) << "Registered backup was not removed";

    // Request to backup another folder
    // this time, the remote folder structure is already there
    fs::path localFolderPath2 = localBasePath / "LocalBackedUpFolder2";
    fs::create_directories(localFolderPath2);
    const string backupName2Str = string("RemoteBackupFolder2_") + timestamp;
    const char* backupName2 = backupName2Str.c_str();
    err = synchronousSyncFolder(0, nullptr, MegaSync::TYPE_BACKUP, localFolderPath2.u8string().c_str(), backupName2, INVALID_HANDLE, nullptr);
    ASSERT_TRUE(err == API_OK) << "Backup folder 2 failed (error: " << err << ")";
    bkpId = mApi[0].lastSyncBackupId;
    newBkp.reset(megaApi[0]->getSyncByBackupId(bkpId));
    ASSERT_TRUE(newBkp) << "Sync not found for second backup";

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
    megaApi[0]->removeSync(bkpId, &removeTracker2);
    ASSERT_EQ(API_OK, removeTracker2.waitForResult());

    RequestTracker moveNodesTracker(megaApi[0].get());
    megaApi[0]->moveOrRemoveDeconfiguredBackupNodes(newBkp->getMegaHandle(), nhrb, &moveNodesTracker);
    ASSERT_EQ(API_OK, moveNodesTracker.waitForResult());

    newBkp.reset(megaApi[0]->getSyncByBackupId(bkpId));
    ASSERT_FALSE(newBkp) << "Sync not removed for second backup";
    destChildren.reset(megaApi[0]->getChildren(remoteDestNode.get()));
    ASSERT_TRUE(destChildren && destChildren->size() == 1);
    ASSERT_STREQ(destChildren->get(0)->getName(), backupName2);
#endif
}

#ifdef ENABLE_SYNC
/**
 * @brief TEST_F SdkBackupMoveOrDelete
 *
 * It tests the creation and removal of Backups
 *
 * Pre-requisites:
 *  - This test will use 2 clients (C0 and C1) logged in to the same account
 *
 * Test cases:
 *  - Test1(SdkBackupMoveOrDelete). Create a backup from C0
 *  - Test2(SdkBackupMoveOrDelete). Request backup removal (and delete its contents) from C1
 *  - Test3(SdkBackupMoveOrDelete). Create a backup from C0
 *  - Test4(SdkBackupMoveOrDelete). Request backup removal (and move its contents) from C1
 *  - Test5(SdkBackupMoveOrDelete). Create a sync from C0
 *  - Test6(SdkBackupMoveOrDelete). Request sync stop from C1
 */
TEST_F(SdkTest, SdkBackupMoveOrDelete)
{
    using Sl = SyncListener;
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));
    LOG_info << "___TEST BackupMoveOrDelete___";

    Sl sl0;
    MegaListenerDeregisterer mld0(megaApi[0].get(), &sl0);

    string timestamp = getCurrentTimestamp(true);

    // Set device name if missing
    string deviceName;
    if (doGetDeviceName(0, &deviceName, nullptr) != API_OK || deviceName.empty())
    {
        string newDeviceName = "Jenkins " + timestamp;
        ASSERT_EQ(doSetDeviceName(0, nullptr, newDeviceName.c_str()), API_OK) << "Setting device name failed";
        // make sure Device Name attr was set
        ASSERT_EQ(doGetDeviceName(0, &deviceName, nullptr), API_OK) << "Getting device name failed";
        ASSERT_EQ(deviceName, newDeviceName) << "Getting device name failed (wrong value)";
    }
    // Make sure My Backups folder was created
    ASSERT_NO_FATAL_FAILURE(syncTestEnsureMyBackupsRemoteFolderExists(0));

    LOG_debug << "### Test1(SdkBackupMoveOrDelete). Create a backup from C1 ###";
    // Create local contents to back up
    fs::path localFolderPath = fs::current_path() / "LocalBackupFolder";
    std::error_code ec;
    fs::remove_all(localFolderPath, ec);
    ASSERT_FALSE(fs::exists(localFolderPath));
    fs::create_directories(localFolderPath);
    string bkpFile = "bkpFile";
    ASSERT_TRUE(createLocalFile(localFolderPath, bkpFile.c_str()));

    // Create a backup
    const string backupNameStr = string("RemoteBackupFolder_") + timestamp;
    MegaHandle backupRootNodeHandle = INVALID_HANDLE;
    int err = synchronousSyncFolder(0, &backupRootNodeHandle, MegaSync::TYPE_BACKUP, localFolderPath.u8string().c_str(), backupNameStr.c_str(), INVALID_HANDLE, nullptr);
    ASSERT_EQ(err, API_OK) << "Backup failed";
    ASSERT_NE(backupRootNodeHandle, INVALID_HANDLE) << "Invalid root handle for backup";

    // Get backup id
    unique_ptr<MegaSyncList> allSyncs{ megaApi[0]->getSyncs() };
    MegaHandle backupId = INVALID_HANDLE;
    for (int i = 0; allSyncs && i < allSyncs->size(); ++i)
    {
        MegaSync* megaSync = allSyncs->get(i);
        if (megaSync->getType() == MegaSync::TYPE_BACKUP &&
            megaSync->getMegaHandle() == backupRootNodeHandle)
        {
            ASSERT_STREQ(megaSync->getName(), backupNameStr.c_str()) << "New backup had wrong name";
            // Make sure the sync's actually active.
            ASSERT_EQ(megaSync->getRunState(), MegaSync::RUNSTATE_RUNNING) << "Backup found but not active.";
            backupId = megaSync->getBackupId();
            break;
        }
    }
    ASSERT_NE(backupId, INVALID_HANDLE) << "Backup could not be found";

    LOG_debug << "### Test2(SdkBackupMoveOrDelete). Request backup removal (and delete its "
                 "contents) from C2 ###";
    // Use another connection with the same credentials
    megaApi.emplace_back(newMegaApi(APP_KEY.c_str(), megaApiCacheFolder(1).c_str(), USER_AGENT.c_str(), unsigned(THREADS_PER_MEGACLIENT)));
    auto& differentApi = *megaApi.back();
    differentApi.addListener(this);
    PerApi pa; // make a copy
    pa.email = mApi.back().email;
    pa.pwd = mApi.back().pwd;
    mApi.push_back(std::move(pa));
    auto& differentApiDtls = mApi.back();
    differentApiDtls.megaApi = &differentApi;
    size_t differentApiIdx = megaApi.size() - 1;

    auto loginTracker = asyncRequestLogin(static_cast<unsigned>(differentApiIdx),
                                          differentApiDtls.email.c_str(),
                                          differentApiDtls.pwd.c_str());
    ASSERT_EQ(API_OK, loginTracker->waitForResult()) << " Failed to establish a login/session for account " << differentApiIdx;
    loginTracker = asyncRequestFetchnodes(static_cast<unsigned>(differentApiIdx));
    ASSERT_EQ(API_OK, loginTracker->waitForResult()) << " Failed to fetch nodes for account " << differentApiIdx;

    sl0.mRecvCbs[Sl::SyncDeleted] = false;
    RequestTracker removeBackupTracker(megaApi[differentApiIdx].get());
    megaApi[differentApiIdx]->removeFromBC(backupId, INVALID_HANDLE, &removeBackupTracker);
    ASSERT_EQ(removeBackupTracker.waitForResult(), API_OK) << "Failed to remove backup and delete its contents";
    ASSERT_TRUE(WaitFor(
        [&sl0]()
        {
            return sl0.mRecvCbs[Sl::SyncDeleted].load();
        },
        120000))
        << "onSyncDeleted not received for C0";

    // Wait for this client to receive the backup removal request
    auto syncCfgRemoved = [this, &backupId]()
    {
        unique_ptr<MegaSync> s(megaApi[0]->getSyncByBackupId(backupId));
        return !s;
    };
    ASSERT_TRUE(syncCfgRemoved()) << "Original API could still see the removed backup";

    // Wait for the backup to be removed from remote storage
    auto bkpDeleted = [this, backupRootNodeHandle]()
    {
        std::unique_ptr<MegaNode> deletedNode(megaApi[0]->getNodeByHandle(backupRootNodeHandle));
        return !deletedNode;
    };
    ASSERT_TRUE(WaitFor(bkpDeleted, 60000)) << "Backup not removed after 60 seconds";

    LOG_debug << "### Test3(SdkBackupMoveOrDelete). Create a backup from C1 ###";
    backupRootNodeHandle = INVALID_HANDLE;
    err = synchronousSyncFolder(0, &backupRootNodeHandle, MegaSync::TYPE_BACKUP, localFolderPath.u8string().c_str(), backupNameStr.c_str(), INVALID_HANDLE, nullptr);
    ASSERT_EQ(err, API_OK) << "Second backup failed";
    ASSERT_NE(backupRootNodeHandle, INVALID_HANDLE) << "Invalid root handle for 2nd backup";

    // Create move destination
    MegaNode *rootnode = megaApi[0]->getRootNode();
    string moveDstName = "bkpMoveDest";
    MegaHandle moveDest = createFolder(0, moveDstName.c_str(), rootnode);
    ASSERT_NE(moveDest, INVALID_HANDLE);
    std::unique_ptr<MegaNode> moveDestNode(megaApi[0]->getNodeByHandle(moveDest));
    ASSERT_TRUE(moveDestNode) << "Node missing for remote folder " << moveDstName;

    // Get 2nd backup id
    allSyncs.reset(megaApi[0]->getSyncs());
    backupId = INVALID_HANDLE;
    for (int i = 0; allSyncs && i < allSyncs->size(); ++i)
    {
        MegaSync* megaSync = allSyncs->get(i);
        if (megaSync->getType() == MegaSync::TYPE_BACKUP &&
            megaSync->getMegaHandle() == backupRootNodeHandle)
        {
            ASSERT_STREQ(megaSync->getName(), backupNameStr.c_str()) << "2nd backup had wrong name";
            // Make sure the sync's actually active.
            ASSERT_TRUE(megaSync->getRunState() == MegaSync::RUNSTATE_RUNNING) << "2nd backup found but not active.";
            backupId = megaSync->getBackupId();
            break;
        }
    }
    ASSERT_NE(backupId, INVALID_HANDLE) << "2nd backup could not be found";

    // Wait for other API to see the backup destination
    auto bkpDestOK = [this, differentApiIdx, &moveDest]()
    {
        unique_ptr<MegaNode> bd(megaApi[differentApiIdx]->getNodeByHandle(moveDest));
        return bd != nullptr;
    };
    ASSERT_TRUE(WaitFor(bkpDestOK, 60000)) << "Other API could not see the backup destination after 60 seconds";

    LOG_debug << "### Test4(SdkBackupMoveOrDelete). Request backup removal (and move its contents) "
                 "from C2 ###";
    sl0.mRecvCbs[Sl::SyncDeleted] = false;
    // Request backup removal (and move its contents) from a different connection
    RequestTracker removeBackupTracker2(megaApi[static_cast<size_t>(differentApiIdx)].get());
    megaApi[differentApiIdx]->removeFromBC(backupId, moveDest, &removeBackupTracker2);
    ASSERT_EQ(removeBackupTracker2.waitForResult(), API_OK) << "Failed to remove 2nd backup and move its contents";

    ASSERT_TRUE(WaitFor(
        [&sl0]()
        {
            return sl0.mRecvCbs[Sl::SyncDeleted].load();
        },
        120000))
        << "onSyncDeleted not received for C0";

    ASSERT_TRUE(syncCfgRemoved()) << "Original API could still see the 2nd removed backup";

    // Wait for the contents of the 2nd backup to be moved in remote storage
    auto bkpMoved = [this, backupRootNodeHandle, &moveDestNode]()
    {
        std::unique_ptr<MegaNodeList> destChildren(megaApi[0]->getChildren(moveDestNode.get()));
        return destChildren && destChildren->size() == 1 &&
               destChildren->get(0)->getHandle() == backupRootNodeHandle;
    };
    ASSERT_TRUE(WaitFor(bkpMoved, 60000)) << "2nd backup not moved after 60 seconds";

    LOG_debug << "### Test5(SdkBackupMoveOrDelete). Create a sync from C1 ###";
    // Create a sync
    backupRootNodeHandle = INVALID_HANDLE;
    err = synchronousSyncFolder(0, &backupRootNodeHandle, MegaSync::TYPE_TWOWAY, localFolderPath.u8string().c_str(), nullptr, moveDest, nullptr);
    ASSERT_EQ(err, API_OK) << "Sync failed";
    ASSERT_NE(backupRootNodeHandle, INVALID_HANDLE) << "Invalid root handle for sync";

    // Get backup id of the sync
    allSyncs.reset(megaApi[0]->getSyncs());
    backupId = INVALID_HANDLE;
    for (int i = 0; allSyncs && i < allSyncs->size(); ++i)
    {
        MegaSync* megaSync = allSyncs->get(i);
        if (megaSync->getType() == MegaSync::TYPE_TWOWAY &&
            megaSync->getMegaHandle() == backupRootNodeHandle)
        {
            // Make sure the sync's actually active.
            ASSERT_TRUE(megaSync->getRunState() == MegaSync::RUNSTATE_RUNNING) << "Sync found but not active.";
            backupId = megaSync->getBackupId();
            break;
        }
    }
    ASSERT_NE(backupId, INVALID_HANDLE) << "Sync could not be found";

    LOG_debug << "### Test6(SdkBackupMoveOrDelete). Request sync stop from C2 ###";
    sl0.mRecvCbs[Sl::SyncDeleted] = false;
    RequestTracker stopSyncTracker(megaApi[differentApiIdx].get());
    megaApi[differentApiIdx]->removeFromBC(backupId, INVALID_HANDLE, &stopSyncTracker);
    ASSERT_EQ(stopSyncTracker.waitForResult(), API_OK) << "Failed to stop sync";

    ASSERT_TRUE(WaitFor(
        [&sl0]()
        {
            return sl0.mRecvCbs[Sl::SyncDeleted].load();
        },
        120000))
        << "onSyncDeleted not received for C0";

    ASSERT_TRUE(syncCfgRemoved()) << "Original API could still see the removed sync";
    fs::remove_all(localFolderPath, ec);
}

TEST_F(SdkTest, SdkBackupPauseResume)
{
    LOG_info << "___TEST BackupPauseResume___";
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    const string timestamp = getCurrentTimestamp(true);

    // Set device name if missing
    string deviceName;
    if (doGetDeviceName(0, &deviceName, nullptr) != API_OK || deviceName.empty())
    {
        const string newDeviceName = "Jenkins " + timestamp;
        ASSERT_EQ(doSetDeviceName(0, nullptr, newDeviceName.c_str()), API_OK)
            << "Setting device name failed";
        // make sure Device Name attr was set
        ASSERT_EQ(doGetDeviceName(0, &deviceName, nullptr), API_OK) << "Getting device name failed";
        ASSERT_EQ(deviceName, newDeviceName) << "Getting device name failed (wrong value)";
    }
    // Make sure My Backups folder was created
    ASSERT_NO_FATAL_FAILURE(syncTestEnsureMyBackupsRemoteFolderExists(0));

    // Create local contents
    vector<fs::path> folders = {fs::current_path() / "LocalFolderPauseResume",
                                fs::current_path() / "LocalSyncFolder"};
    for (const auto& localFolder: folders)
    {
        std::error_code ec;
        fs::remove_all(localFolder, ec);
        ASSERT_FALSE(fs::exists(localFolder));
        fs::create_directories(localFolder);
        ASSERT_TRUE(createLocalFile(localFolder, "bkpFile"));
    }
    const string localBackupFolder = folders[0].u8string();
    const string localSyncFolder = folders[1].u8string();

    // Create a backup, and get its id
    const string backupNameStr = string("RemoteBackupFolder_") + timestamp;
    ASSERT_EQ(API_OK,
              synchronousSyncFolder(0,
                                    nullptr,
                                    MegaSync::TYPE_BACKUP,
                                    localBackupFolder.c_str(),
                                    backupNameStr.c_str(),
                                    INVALID_HANDLE,
                                    nullptr))
        << "Initial connection: Failed to create a Backup";
    MegaHandle idOfBackup = mApi[0].lastSyncBackupId;
    ASSERT_NE(idOfBackup, INVALID_HANDLE) << "Initial connection: invalid Backup id";

    // Create a sync, and get its id
    unique_ptr<MegaNode> rootnode{megaApi[0]->getRootNode()};
    MegaHandle syncDest = createFolder(0, "syncDest", rootnode.get());
    ASSERT_NE(syncDest, INVALID_HANDLE);
    ASSERT_EQ(API_OK,
              synchronousSyncFolder(0,
                                    nullptr,
                                    MegaSync::TYPE_TWOWAY,
                                    localSyncFolder.c_str(),
                                    nullptr,
                                    syncDest,
                                    nullptr))
        << "Initial connection: Failed to create a Sync";
    MegaHandle idOfSync = mApi[0].lastSyncBackupId;
    ASSERT_NE(idOfSync, INVALID_HANDLE) << "Initial connection: invalid Sync id";

    auto testRunState = [&apis = megaApi](MegaHandle backupId, int desiredState)
    {
        unique_ptr<MegaSync> s(apis[0]->getSyncByBackupId(backupId));
        return s && s->getRunState() == desiredState;
    };

    // Wait for the backup to be in RUNNING state
    ASSERT_TRUE(WaitFor(
        [idOfBackup, desiredState = MegaSync::RUNSTATE_RUNNING, testRunState]()
        {
            return testRunState(idOfBackup, desiredState);
        },
        60000))
        << "Initial connection: backup not Running (started) after 60 seconds";

    // Wait for the sync to be in RUNNING state
    ASSERT_TRUE(WaitFor(
        [idOfSync, desiredState = MegaSync::RUNSTATE_RUNNING, testRunState]()
        {
            return testRunState(idOfSync, desiredState);
        },
        60000))
        << "Initial connection: sync not Running (started) after 60 seconds";

    // Create a second connection with the same credentials
    megaApi.emplace_back(newMegaApi(APP_KEY.c_str(),
                                    megaApiCacheFolder(1).c_str(),
                                    USER_AGENT.c_str(),
                                    unsigned(THREADS_PER_MEGACLIENT)));
    megaApi.back()->addListener(this);
    PerApi pa; // make a copy
    pa.email = mApi.back().email;
    pa.pwd = mApi.back().pwd;
    mApi.push_back(std::move(pa));
    auto& differentApiDtls = mApi.back();
    differentApiDtls.megaApi = megaApi.back().get();

    {
        bool& fetchNodesDone = mApi[1].requestFlags[MegaRequest::TYPE_FETCH_NODES] = false;
        unique_ptr<RequestTracker> loginTracker =
            std::make_unique<RequestTracker>(megaApi[1].get());
        megaApi[1]->login(differentApiDtls.email.c_str(),
                          differentApiDtls.pwd.c_str(),
                          loginTracker.get());
        ASSERT_EQ(API_OK, loginTracker->waitForResult()) << "Second connection: Failed to login";
        ASSERT_NO_FATAL_FAILURE(fetchnodes(1))
            << "Second connection: Failed to request fetch nodes";
        ASSERT_TRUE(WaitFor(
            [fetchNodesDone]()
            {
                return fetchNodesDone;
            },
            60000))
            << "Second connection: fetch nodes not done after 60 seconds";
    }

    // Commands for the Backup
    {
        // Second connection: Pause backup
        RequestTracker pauseBackupTracker(megaApi[1].get());
        megaApi[1]->pauseFromBC(idOfBackup, &pauseBackupTracker);
        ASSERT_EQ(pauseBackupTracker.waitForResult(), API_OK)
            << "Second connection: Failed to Pause backup";

        // Initial connection: wait for backup to be Paused
        ASSERT_TRUE(WaitFor(
            [idOfBackup, testRunState]()
            {
                return testRunState(idOfBackup, MegaSync::RUNSTATE_SUSPENDED);
            },
            120000))
            << "Initial connection: backup not Paused after 120 seconds";

        // Wait a while (for the sds attr to be updated and propagated).
        // Without this, resuming will fail sometimes.
        std::this_thread::sleep_for(std::chrono::seconds{5});

        // Second connection: Resume backup
        RequestTracker resumeBackupTracker(megaApi[1].get());
        megaApi[1]->resumeFromBC(idOfBackup, &resumeBackupTracker);
        ASSERT_EQ(resumeBackupTracker.waitForResult(), API_OK)
            << "Second connection: Failed to Resume backup";

        // Initial connection: wait for backup to be Resumed
        ASSERT_TRUE(WaitFor(
            [idOfBackup, testRunState]()
            {
                return testRunState(idOfBackup, MegaSync::RUNSTATE_RUNNING);
            },
            120000))
            << "Initial connection: backup not Running (resumed) after 120 seconds";

        // Clean-up
        RequestTracker removeBackupTracker(megaApi[0].get());
        megaApi[0]->removeSync(idOfBackup, &removeBackupTracker);
        ASSERT_EQ(removeBackupTracker.waitForResult(), API_OK)
            << "Initial connection: Failed to remove backup";
    }

    // Commands for the Sync
    {
        // Second connection: Pause sync
        RequestTracker pauseSyncTracker(megaApi[1].get());
        megaApi[1]->pauseFromBC(idOfSync, &pauseSyncTracker);
        ASSERT_EQ(pauseSyncTracker.waitForResult(), API_OK)
            << "Second connection: Failed to Pause sync";

        // Initial connection: wait for sync to be Paused
        ASSERT_TRUE(WaitFor(
            [idOfSync, testRunState]()
            {
                return testRunState(idOfSync, MegaSync::RUNSTATE_SUSPENDED);
            },
            120000))
            << "Initial connection: sync not Paused after 120 seconds";

        // Wait a while (for the sds attr to be updated and propagated).
        // Without this, resuming will fail sometimes.
        std::this_thread::sleep_for(std::chrono::seconds{5});

        // Second connection: Resume sync
        RequestTracker resumeSyncTracker(megaApi[1].get());
        megaApi[1]->resumeFromBC(idOfSync, &resumeSyncTracker);
        ASSERT_EQ(resumeSyncTracker.waitForResult(), API_OK)
            << "Second connection: Failed to Resume sync";

        // Initial connection: wait for sync to be Resumed
        ASSERT_TRUE(WaitFor(
            [idOfSync, testRunState]()
            {
                return testRunState(idOfSync, MegaSync::RUNSTATE_RUNNING);
            },
            120000))
            << "Initial connection: sync not Running (resumed) after 120 seconds";

        // Clean-up
        RequestTracker removeSyncTracker(megaApi[0].get());
        megaApi[0]->removeSync(idOfSync, &removeSyncTracker);
        ASSERT_EQ(removeSyncTracker.waitForResult(), API_OK)
            << "Initial connection: Failed to remove sync";
    }

    std::error_code ec;
    fs::remove_all(folders[0], ec);
    fs::remove_all(folders[1], ec);
}

TEST_F(SdkTest, SdkExternalDriveFolder)
{
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));
    LOG_info << "___TEST SdkExternalDriveFolder___";

    // dummy path to drive
    fs::path basePath = makeNewTestRoot();
    fs::path pathToDrive = basePath / "ExtDrive";
    fs::create_directory(pathToDrive);
    const string& pathToDriveStr = pathToDrive.u8string();

    // attempt to set the name of an external drive to the name of current device (if the latter was already set)
    string deviceName;
    if (doGetDeviceName(0, &deviceName, nullptr) == API_OK && !deviceName.empty())
    {
        ASSERT_EQ(API_EEXIST, doSetDriveName(0, pathToDriveStr.c_str(), deviceName.c_str()))
            << "Ext-drive name was set to current device name: " << deviceName;
    }

    // drive name
    string driveName = "SdkExternalDriveTest_" + getCurrentTimestamp(true);

    // set drive name
    auto err = doSetDriveName(0, pathToDriveStr.c_str(), driveName.c_str());
    ASSERT_EQ(API_OK, err) << "setDriveName failed (error: " << err << ")";

    // attempt to set the same name to another drive
    fs::path pathToDrive2 = basePath / "ExtDrive2";
    fs::create_directory(pathToDrive2);
    const string& pathToDriveStr2 = pathToDrive2.u8string();
    err = doSetDriveName(0, pathToDriveStr2.c_str(), driveName.c_str());
    ASSERT_EQ(API_EEXIST, err) << "setDriveName allowed duplicated name " << driveName << ". Should not have.";

    // get drive name
    string driveNameFromCloud;
    err = doGetDriveName(0, &driveNameFromCloud, pathToDriveStr.c_str());
    ASSERT_EQ(API_OK, err) << "getDriveName failed (error: " << err << ")";
    ASSERT_EQ(driveNameFromCloud, driveName) << "getDriveName returned incorrect value";

    // Make sure My Backups folder was created
    ASSERT_NO_FATAL_FAILURE(syncTestEnsureMyBackupsRemoteFolderExists(0));
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
    err = synchronousSetSyncRunState(0, backupId, MegaSync::RUNSTATE_DISABLED);
    ASSERT_EQ(API_OK, err) << "Disable sync failed (error: " << err << ")";

    // remove backup
    err = synchronousRemoveSync(0, backupId);
    ASSERT_EQ(MegaError::API_OK, err) << "Remove sync failed (error: " << err << ")";

    ASSERT_EQ(MegaError::API_OK, synchronousRemoveBackupNodes(0, backupFolderHandle));

    // reset DriveName value, before a future test
    err = doSetDriveName(0, pathToDriveStr.c_str(), "");
    ASSERT_EQ(API_OK, err) << "setDriveName failed when resetting (error: " << err << ")";

    // attempt to get drive name (after being deleted)
    err = doGetDriveName(0, nullptr, pathToDriveStr.c_str());
    ASSERT_EQ(API_ENOENT, err) << "getDriveName not failed as it should (error: " << err << ")";
}
#endif

void SdkTest::syncTestEnsureMyBackupsRemoteFolderExists(unsigned apiIdx)
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
    ASSERT_NE(n, nullptr)
        << "syncTestMyBackupsRemoteFolder: My Backups Folder could not be retrieved";
}

void SdkTest::resetOnNodeUpdateCompletionCBs()
{
    for_each(begin(mApi), end(mApi),
             [](PerApi& api) { if (api.mOnNodesUpdateCompletion) api.mOnNodesUpdateCompletion = nullptr; });
}

onNodesUpdateCompletion_t SdkTest::createOnNodesUpdateLambda(const MegaHandle& hfolder, int change, bool& flag)
{
    flag = false;
    return [this, hfolder, change, &flag](size_t apiIndex, MegaNodeList* nodes)
           { onNodesUpdateCheck(apiIndex, hfolder, nodes, change, flag); };
}

TEST_F(SdkTest, SdkUserAlias)
{
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));
    LOG_info << "___TEST SdkUserAlias___";

    // setup
    MegaHandle uh = UNDEF;
    if (auto u = std::unique_ptr<MegaUser>(megaApi[0]->getMyUser()))
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
    ASSERT_EQ(mApi[0].getAttributeValue(), alias) << "getUserAlias returned incorrect value";

    // test setter/getter for different value
    alias = "UserAliasTest_changed";
    err = synchronousSetUserAlias(0, uh, alias.c_str());
    ASSERT_EQ(API_OK, err) << "setUserAlias failed (error: " << err << ")";
    err = synchronousGetUserAlias(0, uh);
    ASSERT_EQ(API_OK, err) << "getUserAlias failed (error: " << err << ")";
    ASSERT_EQ(mApi[0].getAttributeValue(), alias) << "getUserAlias returned incorrect value";
}

#if 0
// SMS verification was deprecated. This test should be removed in the future,
// along with the rest of the code dealing with the deprecated functionality.
TEST_F(SdkTest, SdkGetCountryCallingCodes)
{
    LOG_info << "___TEST SdkGetCountryCallingCodes___";
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    getCountryCallingCodes();
    ASSERT_GT(mApi[0].getStringListCount(), 0u);
    // sanity check a few country codes
    const MegaStringList* const nz = mApi[0].getStringList("NZ");
    ASSERT_NE(nullptr, nz);
    ASSERT_EQ(1, nz->size());
    ASSERT_EQ(0, strcmp("64", nz->get(0)));
    const MegaStringList* const de = mApi[0].getStringList("DE");
    ASSERT_NE(nullptr, de);
    ASSERT_EQ(1, de->size());
    ASSERT_EQ(0, strcmp("49", de->get(0)));
}
#endif

TEST_F(SdkTest, DISABLED_invalidFileNames)
{
    LOG_info << "___TEST invalidFileNames___";
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    auto aux = LocalPath::fromAbsolutePath(fs::current_path().u8string());

#if defined (__linux__) || defined (__ANDROID__)
    if (fileSystemAccess->getlocalfstype(aux) == FS_EXT)
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
    if (fileSystemAccess->getlocalfstype(aux) == FS_APFS ||
        fileSystemAccess->getlocalfstype(aux) == FS_HFS)
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
    if (fileSystemAccess->getlocalfstype(aux) == FS_NTFS)
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
        snprintf(unescapedName, sizeof(unescapedName), "f%%%02xf", i);
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
        snprintf(escapedName, sizeof(escapedName), "f%cf", i);
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
                              MegaTransfer::COLLISION_CHECK_FINGERPRINT /*collisionCheck*/,
                              MegaTransfer::COLLISION_RESOLUTION_NEW_WITH_N /* collisionResolution */,
                              false    /* undelete */,
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
    ASSERT_EQ(fileSystemAccess->getlocalfstype(LocalPath::fromAbsolutePath("c:")), FS_NTFS);
    ASSERT_EQ(fileSystemAccess->getlocalfstype(LocalPath::fromAbsolutePath("c:\\")), FS_NTFS);
    ASSERT_EQ(fileSystemAccess->getlocalfstype(LocalPath::fromAbsolutePath("C:\\")), FS_NTFS);
    ASSERT_EQ(fileSystemAccess->getlocalfstype(LocalPath::fromAbsolutePath("C:\\Program Files")),
              FS_NTFS);
    ASSERT_EQ(fileSystemAccess->getlocalfstype(
                  LocalPath::fromAbsolutePath("c:\\Program Files\\Windows NT")),
              FS_NTFS);
#endif
}
TEST_F(SdkTest, EscapesReservedCharacters)
{
    // Set up necessary accounts.
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    const string input = "\r\\/:?\"<>|*";

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
    unique_ptr<char[]> output(megaApi[0]->escapeFsIncompatible(input.c_str(), nullptr));

    // Was the string escaped as expected?
    ASSERT_NE(output.get(), nullptr);
    ASSERT_STREQ(output.get(), osstream.str().c_str());
}

TEST_F(SdkTest, EscapesReservedCharactersOnDownload)
{
    // a/b/c!.txt
    static const string fileName = "a%2fb%2fc!.txt";

    // Set up necessary accounts.
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    // For convenience.
    MegaApi* api = megaApi[0].get();

    // Get root node.
    unique_ptr<MegaNode> root(api->getRootNode());
    ASSERT_NE(root, nullptr);

    // Create file to upload containing escaped characters.
    deleteFile(fileName);
    createFile(fileName);

    // Upload the file.
    ASSERT_EQ(API_OK, doStartUpload(0, nullptr, fileName.c_str(), root.get(), nullptr, 0, nullptr, false, false, nullptr));

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
    string targetPath = fs::current_path().u8string();
    targetPath.append(1, LocalPath::localPathSeparator_utf8);
    ASSERT_EQ(API_OK, doStartDownload(0, child,
                                         targetPath.c_str(),
                                         nullptr  /*customName*/,
                                         nullptr  /*appData*/,
                                         false    /*startFirst*/,
                                         nullptr  /*cancelToken*/,
                                         MegaTransfer::COLLISION_CHECK_ASSUMEDIFFERENT /*collisionCheck*/,
                                         MegaTransfer::COLLISION_RESOLUTION_OVERWRITE /*collisionResolution*/,
                                         false    /*undelete*/));

    // Was the filename correctly escaped on download?
    ASSERT_TRUE(fileexists(fileName));
    deleteFile(fileName);
}

TEST_F(SdkTest, UnescapesReservedCharacters)
{
    // Set up necessary accounts.
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    string input = "\\/:?\"<>|*%5a%21";
    string input_unescaped = "\\/:?\"<>|*Z!";

    // Escape input string.
    unique_ptr<char[]> escaped(megaApi[0]->escapeFsIncompatible(input.c_str(), nullptr));

    ASSERT_NE(escaped.get(), nullptr);

    // Unescape the escaped string.
    unique_ptr<char[]> unescaped(megaApi[0]->unescapeFsIncompatible(escaped.get(), nullptr));

    // Was the string unescaped as expected?  (round trip causes %5a to be unescaped now)
    ASSERT_NE(unescaped.get(), nullptr);
    ASSERT_STREQ(input_unescaped.c_str(), unescaped.get());
}

TEST_F(SdkTest, UnescapesReservedCharactersOnUpload)
{
    // a/b/c!.txt
    static const string fileName = "a%2fb%2fc!.txt";

    // Set up necessary accounts.
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    // For convenience.
    MegaApi* api = megaApi[0].get();

    // Get root node.
    unique_ptr<MegaNode> root(api->getRootNode());
    ASSERT_NE(root, nullptr);

    // Create file to upload containing escaped characters.
    deleteFile(fileName);
    createFile(fileName);

    // Upload the file.
    ASSERT_EQ(API_OK, doStartUpload(0, nullptr, fileName.c_str(), root.get(), nullptr, 0, nullptr, false, false, nullptr));

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
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    // Make sure our clients are working with pro plans.
    auto restorer0 = elevateToPro(*megaApi[0]);
    ASSERT_EQ(result(restorer0), API_OK);

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
    std::unique_ptr<MegaNode> nodeToDownload(megaApi[0]->getNodeByPath("/uploadme_mega_auto_test_sdk"));
    megaApi[0]->startDownload(nodeToDownload.get(),
            downloadpath.u8string().c_str(),
            nullptr  /*customName*/,
            nullptr  /*appData*/,
            false    /*startFirst*/,
            nullptr  /*cancelToken*/,
            MegaTransfer::COLLISION_CHECK_FINGERPRINT /*collisionCheck*/,
            MegaTransfer::COLLISION_RESOLUTION_NEW_WITH_N /* collisionResolution */,
            false    /* undelete */,
            &downloadListener1);

    ASSERT_TRUE(downloadListener1.waitForResult() == API_EEXIST);

    fs::remove_all(downloadpath, ec);

    out() << " downloading tree and logout while it's ongoing";

    // ok now try the download
    TransferTracker downloadListener2(megaApi[0].get());
    nodeToDownload.reset(megaApi[0]->getNodeByPath("/uploadme_mega_auto_test_sdk"));
    megaApi[0]->startDownload(nodeToDownload.get(),
            downloadpath.u8string().c_str(),
            nullptr  /*customName*/,
            nullptr  /*appData*/,
            false    /*startFirst*/,
            nullptr  /*cancelToken*/,
            MegaTransfer::COLLISION_CHECK_FINGERPRINT /*collisionCheck*/,
            MegaTransfer::COLLISION_RESOLUTION_NEW_WITH_N /* collisionResolution */,
            false    /* undelete */,
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

TEST_F(SdkTest, QueryAds)
{
    LOG_info << "___TEST QueryAds";
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));
    std::unique_ptr<RequestTracker> tr = asyncQueryAds(0, MegaApi::ADS_FORCE_ADS, INVALID_HANDLE);
    ASSERT_EQ(API_OK, tr->waitForResult()) << "Query Ads failed";
}

TEST_F(SdkTest, FetchAds)
{
    LOG_info << "___TEST FetchAds";
    LOG_debug << "\t# Test suite 1: Fetching ads with non-ads account";
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));
    std::unique_ptr<MegaStringList> stringList =
        std::unique_ptr<MegaStringList>(MegaStringList::createInstance());
    std::unique_ptr<RequestTracker> tr =
        asyncFetchAds(0, MegaApi::ADS_FORCE_ADS, stringList.get(), INVALID_HANDLE);
    ASSERT_EQ(API_EARGS, tr->waitForResult()) << "Fetch Ads succeeded with invalid arguments";

    // Test the invalid Ad code
    const std::string dummyAd{"dummyAdUnit"};
    stringList->add(dummyAd.c_str());
    tr = asyncFetchAds(0, MegaApi::ADS_FORCE_ADS, stringList.get(), INVALID_HANDLE);
    ASSERT_EQ(API_OK, tr->waitForResult()) << "Fetch Ads request failed when it wasn't expected";
    ASSERT_TRUE(tr->request);
    auto adsLink = tr->request->getMegaStringMap();
    ASSERT_TRUE(adsLink && adsLink->size() == 1);
    ASSERT_STREQ(adsLink->get(dummyAd.c_str()), "-9")
        << "Fetch Ads should have received -9 for dummy Ad case";

    tr = asyncQueryAds(0, MegaApi::ADS_DEFAULT, INVALID_HANDLE);
    ASSERT_EQ(API_OK, tr->waitForResult()) << "Query Ads request failed when it wasn't expected";
    const int showAd = tr->request->getNumDetails();
    LOG_debug << "Account 0 " << megaApi[0]->getMyUserHandle() << " (" << megaApi[0]->getMyEmail()
              << ") Show Ads (QueryAds, 0- Should show Ads, 1-Should not show ads): " << showAd;
    if (showAd == 0)
    {
        // Show Ads
        const char valiAdSlot[] = "ANDFB";
        stringList.reset(MegaStringList::createInstance());
        stringList->add(valiAdSlot);
        tr = asyncFetchAds(0, MegaApi::ADS_DEFAULT, stringList.get(), INVALID_HANDLE);
        ASSERT_EQ(API_OK, tr->waitForResult())
            << "Fetch Ads failed when it was expected to receive Ads";
        const MegaStringMap* ads = tr->request->getMegaStringMap();
        ASSERT_TRUE(ads) << "Fetch Ads should have received Ads link";
    }
    else
    {
        // Do not show ad - Try fetching ad with default flag
        tr = asyncFetchAds(0, MegaApi::ADS_DEFAULT, stringList.get(), INVALID_HANDLE);
        ASSERT_EQ(API_ENOENT, tr->waitForResult())
            << "Fetch Ads didn't fail when it was expected to (correct Ad case)";
        const MegaStringMap* ads = tr->request->getMegaStringMap();
        ASSERT_FALSE(ads)
            << "Fetch Ads should have been nullptr to expected error code in `request`";
    }

    stringList.reset(MegaStringList::createInstance());
    stringList->add(dummyAd.c_str());
    tr = asyncFetchAds(0, MegaApi::ADS_DEFAULT, stringList.get(), INVALID_HANDLE);
    const auto ab_adse = megaApi[0]->getFlag("adse", false);
    const auto ab_adsi = megaApi[0]->getFlag("adsi", false);
    LOG_debug << "Account 0 " << megaApi[0]->getMyUserHandle() << " (" << megaApi[0]->getMyEmail()
              << ") ab_adse: " << ab_adse->getGroup() << " ab_adsi: " << ab_adsi->getGroup();
    const bool isUserAllowedToFetchAds = ab_adse->getGroup() > 0u || ab_adsi->getGroup() > 0u;
    // Check if ads are enable for the account by default or through AB test/feature flags.
    if (isUserAllowedToFetchAds || (showAd == 0))
    {
        ASSERT_EQ(API_OK, tr->waitForResult())
            << "Fetch Ads request failed when it wasn't expected";
        ASSERT_TRUE(tr->request);
        auto ads = tr->request->getMegaStringMap();
        ASSERT_TRUE(ads && ads->size() == 1);
        ASSERT_STREQ(ads->get(dummyAd.c_str()), "-9")
            << "Fetch Ads should have received -9 for dummy Ad case";
    }
    else
    {
        ASSERT_EQ(API_ENOENT, tr->waitForResult())
            << "Fetch Ads didn't fail when it was expected to (dummy Ad case)";
        const MegaStringMap* ads = tr->request->getMegaStringMap();
        ASSERT_FALSE(ads)
            << "Fetch Ads should have been nullptr due to expected error code `request`";
        const char valiAdSlot[] = "ANDFB";
        stringList.reset(MegaStringList::createInstance());
        stringList->add(valiAdSlot);
        tr = asyncFetchAds(0, MegaApi::ADS_DEFAULT, stringList.get(), INVALID_HANDLE);
        ASSERT_EQ(API_ENOENT, tr->waitForResult())
            << "Fetch Ads didn't fail when it was expected to (correct Ad case)";
        ads = tr->request->getMegaStringMap();
        ASSERT_FALSE(ads)
            << "Fetch Ads should have been nullptr to expected error code in `request`";
    }

    // TODO: LOG_debug << "\t# Test suite 2: Fetching ads with containing-ads account";
}

void cleanUp(::mega::MegaApi* megaApi, const fs::path &basePath)
{
#ifdef ENABLE_SYNC
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
#endif

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

#ifdef ENABLE_SYNC

using sdk_test::waitForSyncState;

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
    std::unique_ptr<MegaSync> sync = waitForSyncState(megaApi[0].get(), remoteBaseNode1.get(), MegaSync::RUNSTATE_RUNNING, MegaSync::NO_SYNC_ERROR);
    ASSERT_TRUE(sync && sync->getRunState() == MegaSync::RUNSTATE_RUNNING);
    ASSERT_EQ(MegaSync::NO_SYNC_ERROR, sync->getError());
    // Sync2
    const auto& lp2 = localPath2.u8string();
    ASSERT_EQ(API_OK, synchronousSyncFolder(0, nullptr, MegaSync::TYPE_TWOWAY, lp2.c_str(), nullptr, remoteBaseNode2->getHandle(), nullptr)) << "API Error adding a new sync";
    ASSERT_EQ(MegaSync::NO_SYNC_ERROR, mApi[0].lastSyncError);
    std::unique_ptr<MegaSync> sync2 = waitForSyncState(megaApi[0].get(), remoteBaseNode2.get(), MegaSync::RUNSTATE_RUNNING, MegaSync::NO_SYNC_ERROR);
    ASSERT_TRUE(sync2 && sync2->getRunState() == MegaSync::RUNSTATE_RUNNING);
    ASSERT_EQ(MegaSync::NO_SYNC_ERROR, sync->getError());

    handle backupId = sync->getBackupId();
    handle backupId2 = sync2->getBackupId();

    LOG_verbose << "SyncRemoveRemoteNode :  Add syncs that fail";
    {
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
    ASSERT_EQ(API_OK, synchronousSetSyncRunState(0, backupId, MegaSync::RUNSTATE_DISABLED));
    sync = waitForSyncState(megaApi[0].get(), remoteBaseNode1.get(), MegaSync::RUNSTATE_DISABLED, MegaSync::NO_SYNC_ERROR);
    ASSERT_TRUE(sync && sync->getRunState() == MegaSync::RUNSTATE_DISABLED);
    ASSERT_EQ(MegaSync::NO_SYNC_ERROR, sync->getError());

    //  Sync 2
    ASSERT_EQ(API_OK,
              synchronousSetSyncRunState(0, sync2->getBackupId(), MegaSync::RUNSTATE_DISABLED));
    sync2 = waitForSyncState(megaApi[0].get(), remoteBaseNode2.get(), MegaSync::RUNSTATE_DISABLED, MegaSync::NO_SYNC_ERROR);
    ASSERT_TRUE(sync2 && sync2->getRunState() == MegaSync::RUNSTATE_DISABLED);
    ASSERT_EQ(MegaSync::NO_SYNC_ERROR, sync->getError());

    LOG_verbose << "SyncRemoveRemoteNode :  Disable disabled syncs";
    ASSERT_EQ(API_OK,
              synchronousSetSyncRunState(0,
                                         sync->getBackupId(),
                                         MegaSync::RUNSTATE_DISABLED)); // Currently disabled.
    ASSERT_EQ(API_OK,
              synchronousSetSyncRunState(0,
                                         backupId,
                                         MegaSync::RUNSTATE_DISABLED)); // Currently disabled.

    LOG_verbose << "SyncRemoveRemoteNode :  Enable Syncs";
    // Sync 1
    ASSERT_EQ(API_OK, synchronousSetSyncRunState(0, backupId, MegaSync::RUNSTATE_RUNNING));
    ASSERT_EQ(MegaSync::NO_SYNC_ERROR, mApi[0].lastSyncError);
    sync = waitForSyncState(megaApi[0].get(), remoteBaseNode1.get(), MegaSync::RUNSTATE_RUNNING, MegaSync::NO_SYNC_ERROR);
    ASSERT_TRUE(sync && sync->getRunState() == MegaSync::RUNSTATE_RUNNING);
    // Sync 2
    ASSERT_EQ(API_OK,
              synchronousSetSyncRunState(0, sync2->getBackupId(), MegaSync::RUNSTATE_RUNNING));
    ASSERT_EQ(MegaSync::NO_SYNC_ERROR, mApi[0].lastSyncError);
    sync2 = waitForSyncState(megaApi[0].get(), remoteBaseNode2.get(), MegaSync::RUNSTATE_RUNNING, MegaSync::NO_SYNC_ERROR);
    ASSERT_TRUE(sync2 && sync2->getRunState() == MegaSync::RUNSTATE_RUNNING);

    LOG_verbose << "SyncRemoveRemoteNode :  Enable syncs that fail";
    {
        ASSERT_EQ(API_ENOENT,
                  synchronousSetSyncRunState(0,
                                             static_cast<MegaHandle>(999999),
                                             MegaSync::RUNSTATE_RUNNING)); // Hope it doesn't exist.
        ASSERT_EQ(MegaSync::UNKNOWN_ERROR, mApi[0].lastSyncError); // MegaApi.h specifies that this contains the error code (not the tag)
        ASSERT_EQ(API_OK,
                  synchronousSetSyncRunState(
                      0,
                      sync2->getBackupId(),
                      MegaSync::RUNSTATE_RUNNING)); // Currently enabled, already running.
        ASSERT_EQ(MegaSync::NO_SYNC_ERROR, mApi[0].lastSyncError);  // since the sync is active, we should see its real state, and it should not have had any error code stored in it
    }

    LOG_verbose << "SyncRemoveRemoteNode :  Remove Syncs";
    // Sync 1
    ASSERT_EQ(API_OK, synchronousRemoveSync(0, backupId)) << "API Error removing the sync";
    sync.reset(megaApi[0]->getSyncByNode(remoteBaseNode1.get()));
    ASSERT_EQ(nullptr, sync.get());
    // Sync 2
    ASSERT_EQ(API_OK, synchronousRemoveSync(0, sync2->getBackupId()))
        << "API Error removing the sync";
    // Keep sync2 not updated. Will be used later to test another removal attemp using a non-updated object.

    LOG_verbose << "SyncRemoveRemoteNode :  Remove Syncs that fail";
    {
        ASSERT_EQ(
            API_ENOENT,
            synchronousRemoveSync(0, static_cast<MegaHandle>(9999999))); // Hope id doesn't exist
        ASSERT_EQ(API_ENOENT, synchronousRemoveSync(0, backupId)); // already removed.
        ASSERT_EQ(API_ENOENT, synchronousRemoveSync(0, backupId2)); // already removed.
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

    std::unique_ptr<MegaNode> node3(megaApi[0].get()->getNodeByPath((string("/") + Utils::replace(basePath3.string(), '\\', '/')).c_str()));
    ASSERT_NE(node3.get(), (MegaNode*)NULL);
    unique_ptr<MegaError> error(megaApi[0]->isNodeSyncableWithError(node3.get()));
    ASSERT_EQ(error->getErrorCode(), API_OK);
    ASSERT_EQ(error->getSyncError(), NO_SYNC_ERROR);

    std::unique_ptr<MegaNode> node2a(megaApi[0].get()->getNodeByPath((string("/") + Utils::replace(basePath2a.string(), '\\', '/')).c_str()));
    // on Windows path separator is \ but API takes /
    ASSERT_NE(node2a.get(), (MegaNode*)NULL);
    error.reset(megaApi[0]->isNodeSyncableWithError(node2a.get()));
    ASSERT_EQ(error->getErrorCode(), API_EEXIST);
    ASSERT_EQ(error->getSyncError(), ACTIVE_SYNC_ABOVE_PATH);

    std::unique_ptr<MegaNode> baseNode(megaApi[0].get()->getNodeByPath((string("/") + Utils::replace(basePath.string(), '\\', '/')).c_str()));
    // on Windows path separator is \ but API takes /
    ASSERT_NE(baseNode.get(), (MegaNode*)NULL);
    error.reset(megaApi[0]->isNodeSyncableWithError(baseNode.get()));
    ASSERT_EQ(error->getErrorCode(), API_EEXIST);
    ASSERT_EQ(error->getSyncError(), ACTIVE_SYNC_BELOW_PATH);
}

TEST_F(SdkTest, SyncResumptionAfterFetchNodes)
{
    LOG_info << "___TEST SyncResumptionAfterFetchNodes___";
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    // This test has several issues:
    // 1. Remote nodes may not be committed to the sctable database in time for fetchnodes which
    //    then fails adding syncs because the remotes are missing. For this reason we wait until
    //    we receive the EVENT_COMMIT_DB event after transferring the nodes.
    // 2. Syncs are deleted some time later leading to error messages (like local fingerprint mismatch)
    //    if we don't wait for long enough after we get called back. A sync only gets flagged but
    //    is deleted later.

    const std::string session = unique_ptr<char[]>(dumpSession()).get();

    const fs::path basePath = "SyncResumptionAfterFetchNodes";
    const auto sync1Path = fs::current_path() / basePath / "sync1"; // stays active
    const auto sync2Path = fs::current_path() / basePath / "sync2"; // will be made inactive
    const auto sync3Path = fs::current_path() / basePath / "sync3"; // will be deleted
    const auto sync4Path = fs::current_path() / basePath / "sync4"; // stays active

    ASSERT_NO_FATAL_FAILURE(cleanUp(this->megaApi[0].get(), basePath));

    SyncListener syncListener0;
    MegaListenerDeregisterer mld1(megaApi[0].get(), &syncListener0);

    fs::create_directories(sync1Path);
    fs::create_directories(sync2Path);
    fs::create_directories(sync3Path);
    fs::create_directories(sync4Path);

    PerApi& target = mApi[0];
    target.resetlastEvent();

    // transfer the folder and its subfolders
    TransferTracker uploadListener(megaApi[0].get());
    megaApi[0]->startUpload(basePath.u8string().c_str(),
                            std::unique_ptr<MegaNode>(megaApi[0]->getRootNode()).get(),
                            nullptr /*fileName*/,
                            ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
                            nullptr /*appData*/,
                            false   /*isSourceTemporary*/,
                            false   /*startFirst*/,
                            nullptr /*cancelToken*/,
                            &uploadListener);

    ASSERT_EQ(API_OK, uploadListener.waitForResult());

    // loop until we get a commit to the sctable to ensure we cached the new remote nodes
    ASSERT_TRUE(WaitFor([&target](){ return target.lastEventsContain(MegaEvent::EVENT_COMMIT_DB); }, 10000));

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

    auto disableSyncByBackupId = [this](handle backupId)
    {
        RequestTracker syncTracker(megaApi[0].get());
        megaApi[0]->setSyncRunState(backupId, MegaSync::RUNSTATE_DISABLED, &syncTracker);
        ASSERT_EQ(API_OK, syncTracker.waitForResult());
    };

    auto resumeSyncByBackupId = [this](handle backupId)
    {
        RequestTracker syncTracker(megaApi[0].get());
        megaApi[0]->setSyncRunState(backupId, MegaSync::RUNSTATE_RUNNING, &syncTracker);
        ASSERT_EQ(API_OK, syncTracker.waitForResult());
    };

    auto removeSyncByBackupId = [this](handle backupId)
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


        LOG_verbose << "checkSyncOK " << p.filename().u8string() << " runState: " << sync->getRunState();

        return sync->getRunState() == MegaSync::RUNSTATE_RUNNING;

    };

    auto checkSyncDisabled = [this, &megaNode](const fs::path& p)
    {
        auto node = megaNode(p.filename().u8string());
        std::unique_ptr<MegaSync> sync{megaApi[0]->getSyncByNode(node.get())};
        if (!sync) return false;
        return sync->getRunState() == MegaSync::RUNSTATE_DISABLED;
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

    handle backupId1 = syncFolder(sync1Path);
    handle backupId2 = syncFolder(sync2Path);
    handle backupId3 = syncFolder(sync3Path);
    handle backupId4 = syncFolder(sync4Path);

    ASSERT_TRUE(checkSyncOK(sync1Path));
    ASSERT_TRUE(checkSyncOK(sync2Path));
    ASSERT_TRUE(checkSyncOK(sync3Path));
    ASSERT_TRUE(checkSyncOK(sync4Path));

    LOG_verbose << " SyncResumptionAfterFetchNodes : disabling sync 2";
    disableSyncByBackupId(backupId2);
    LOG_verbose << " SyncResumptionAfterFetchNodes : disabling sync 4";
    disableSyncByBackupId(backupId4);
    LOG_verbose << " SyncResumptionAfterFetchNodes : removing sync";
    removeSyncByBackupId(backupId3);

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

    target.resetlastEvent();
    fetchnodes(0, maxTimeout); // auto-resumes two active syncs
    ASSERT_TRUE(WaitFor([&target](){ return target.lastEventsContain(MegaEvent::EVENT_SYNCS_RESTORED); }, 10000));

    WaitMillisec(1000); // give them a chance to start on the sync thread

    ASSERT_TRUE(checkSyncOK(sync1Path));
    ASSERT_FALSE(checkSyncOK(sync2Path));
    ASSERT_TRUE(checkSyncDisabled(sync2Path));
    ASSERT_FALSE(checkSyncOK(sync3Path));
    ASSERT_FALSE(checkSyncOK(sync4Path));
    ASSERT_TRUE(checkSyncDisabled(sync4Path));

    // check if we can still resume manually
    LOG_verbose << " SyncResumptionAfterFetchNodes : resuming syncs";
    resumeSyncByBackupId(backupId2);
    resumeSyncByBackupId(backupId4);

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

    target.resetlastEvent();
    fetchnodes(0, maxTimeout); // auto-resumes three active syncs
    ASSERT_TRUE(WaitFor([&target](){ return target.lastEventsContain(MegaEvent::EVENT_SYNCS_RESTORED); }, 10000));

    WaitMillisec(1000); // give them a chance to start on the sync thread

    ASSERT_TRUE(checkSyncOK(sync1Path));
    ASSERT_TRUE(checkSyncOK(sync2Path));
    ASSERT_FALSE(checkSyncOK(sync3Path));
    ASSERT_TRUE(checkSyncOK(sync4Path));

    LOG_verbose << " SyncResumptionAfterFetchNodes : removing syncs";
    removeSyncByBackupId(backupId1);
    removeSyncByBackupId(backupId2);
    removeSyncByBackupId(backupId4);

    // wait for the sync removals to actually take place
    std::this_thread::sleep_for(std::chrono::seconds{5});

    ASSERT_FALSE(syncListener0.hasAnyErrors());

    ASSERT_NO_FATAL_FAILURE(cleanUp(this->megaApi[0].get(), basePath));
}

TEST_F(SdkTest, MidSessionEtoomanyWithSync)
{
    LOG_info << "___TEST MidSessionEtoomanyWithSync___";
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    fs::path basePath = "MidSessionEtoomanyWithSync";
    std::string syncFolder = "sync";
    fs::path syncPath = basePath / syncFolder;
    const auto localPath = fs::current_path() / syncPath;
    fs::create_directories(localPath);

    // Creating the remote folder to be synced to
    std::unique_ptr<MegaNode> remoteRootNode(megaApi[0]->getRootNode());
    ASSERT_NE(remoteRootNode.get(), nullptr);
    auto nh = createFolder(0, syncFolder.c_str(), remoteRootNode.get());
    ASSERT_NE(nh, UNDEF) << "Error creating remote folder";
    std::unique_ptr<MegaNode> remoteSyncNode(megaApi[0]->getNodeByHandle(nh));
    ASSERT_NE(remoteSyncNode.get(), nullptr);

    // Start the sync
    const auto& lp = localPath.u8string();
    ASSERT_EQ(API_OK, synchronousSyncFolder(0, nullptr, MegaSync::TYPE_TWOWAY, lp.c_str(), nullptr, remoteSyncNode->getHandle(), nullptr)) << "API Error adding a new sync";
    ASSERT_EQ(MegaSync::NO_SYNC_ERROR, mApi[0].lastSyncError);
    std::unique_ptr<MegaSync> sync = waitForSyncState(megaApi[0].get(), remoteSyncNode.get(), MegaSync::RUNSTATE_RUNNING, MegaSync::NO_SYNC_ERROR);
    ASSERT_TRUE(sync && sync->getRunState() == MegaSync::RUNSTATE_RUNNING);
    ASSERT_EQ(MegaSync::NO_SYNC_ERROR, sync->getError());

    // Create a local folder and wait for the propagation of the change
    mApi[0].mOnNodesUpdateCompletion = createOnNodesUpdateLambda(INVALID_HANDLE, MegaNode::CHANGE_TYPE_NEW, mApi[0].nodeUpdated);
    std::string folderName = "new-folder-for-test";
    const auto folderPath = fs::current_path() / syncPath / folderName;
    fs::create_directories(folderPath);
    ASSERT_TRUE(waitForResponse(&mApi[0].nodeUpdated)) << "Node update not received after " << maxTimeout << " seconds";
    resetOnNodeUpdateCompletionCBs();
    mApi[0].nodeUpdated = false;

    // Check that the new remote folder has been created
    std::unique_ptr<MegaNode> remoteFolderNode(megaApi[0]->getNodeByPath(folderName.c_str(), remoteSyncNode.get()));
    ASSERT_NE(remoteFolderNode.get(), nullptr);
    nh = remoteFolderNode->getHandle();
    fs::path localFolderPath = syncPath / folderName;
    ASSERT_EQ(fs::exists(localFolderPath), true);

    // Secondary instance with the same account to force an ETOOMANY action packet
    const auto [email, pass] = getEnvVarAccounts().getVarValues(0);
    ASSERT_FALSE(email.empty() || pass.empty());
    mApi.resize(2);
    megaApi.resize(2);
    configureTestInstance(1, email, pass);

    // The secondary instance needs to use staging to send a devcommand
    megaApi[1]->changeApiUrl("https://staging.api.mega.co.nz/");
    auto loginTracker = std::make_unique<RequestTracker>(megaApi[1].get());
    megaApi[1]->login(email.c_str(), pass.c_str(), loginTracker.get());
    ASSERT_EQ(API_OK, loginTracker->waitForResult()) << " Failed to login to account " << email;

    PerApi& target = mApi[0];
    target.resetlastEvent();

    auto devCommandTracker = std::make_unique<RequestTracker>(megaApi[1].get());
    megaApi[1]->sendDevCommand("fr", nullptr, devCommandTracker.get());
    auto errorCode = devCommandTracker->waitForResult();
    ASSERT_TRUE(errorCode == API_OK || errorCode == API_EACCESS) << " Error in devcommand " << errorCode;
    if (errorCode == API_EACCESS)
    {
        LOG_warn << "Devcommand not authorized for your IP, using fetchnodes() instead";
        megaApi[0]->fetchNodes();
    }
    else
    {
        LOG_verbose << "Devcommand succeeded";
        ASSERT_TRUE(WaitFor([&target](){ return target.lastEventsContain(MegaEvent::EVENT_RELOADING); }, 10000));
    }

    // The standard timeout for a fetchnodes is currently 5 minutes (see SdkTest::fetchnodes)
    ASSERT_TRUE(WaitFor([&target](){ return target.lastEventsContain(MegaEvent::EVENT_NODES_CURRENT); }, 300000));

    // Check that the synced node is still there after the reload
    std::unique_ptr<MegaNode> previousFolder(megaApi[0]->getNodeByHandle(nh));
    ASSERT_TRUE(previousFolder != nullptr);

    // Delete the local folder and wait for the propagation of the change
    mApi[0].mOnNodesUpdateCompletion = createOnNodesUpdateLambda(nh, MegaNode::CHANGE_TYPE_PARENT, mApi[0].nodeUpdated);
    deleteFolder(folderPath.u8string());
    ASSERT_TRUE(waitForResponse(&mApi[0].nodeUpdated)) << "Node update not received after " << maxTimeout << " seconds";
    resetOnNodeUpdateCompletionCBs();
    mApi[0].nodeUpdated = false;

    // Check that the sync is still active
    sync = waitForSyncState(megaApi[0].get(), remoteSyncNode.get(), MegaSync::RUNSTATE_RUNNING, MegaSync::NO_SYNC_ERROR);
    ASSERT_TRUE(sync && sync->getRunState() == MegaSync::RUNSTATE_RUNNING);
    ASSERT_EQ(MegaSync::NO_SYNC_ERROR, sync->getError());

    // Check that the deletion of the node has been propagated to the remote filesystem
    std::unique_ptr<MegaNode> remoteFolderNodeCheck(megaApi[0]->getNodeByPath(folderName.c_str(), remoteSyncNode.get()));
    ASSERT_EQ(remoteFolderNodeCheck.get(), nullptr);
}

TEST_F(SdkTest, MidSessionFetchnodes)
{
    LOG_info << "___TEST MidSessionFetchnodes___";
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    for (int i = 0; i < 5; ++i)
    {
        MegaNode *rootnode = megaApi[0]->getRootNode();

        auto nh = createFolder(0, ("new-folder-for-test" + std::to_string(i)).c_str(), rootnode);

        megaApi[0]->invalidateCache();
        ASSERT_NO_FATAL_FAILURE(fetchnodes(0));

        // we should have caught up on actionpackets to the point that this one is present again
        std::unique_ptr<MegaNode> justCreatedNode(megaApi[0]->getNodeByHandle(nh));
        ASSERT_TRUE(justCreatedNode != nullptr);
    }
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

    PerApi& target = mApi[0];
    target.resetlastEvent();

    auto nh = createFolder(0, basePath.u8string().c_str(), remoteRootNode.get());
    ASSERT_NE(nh, UNDEF) << "Error creating remote basePath";
    std::unique_ptr<MegaNode> remoteBaseNode(megaApi[0]->getNodeByHandle(nh));
    ASSERT_NE(remoteBaseNode.get(), nullptr);

    // make sure there are no outstanding cs requests in case
    // "Postponing DB commit until cs requests finish"
    // means our Sync's cloud Node is not in the db
    ASSERT_TRUE(WaitFor([&target](){ return target.lastEventsContain(MegaEvent::EVENT_COMMIT_DB); }, 10000));


    LOG_verbose << "SyncPersistence :  Enabling sync";
    ASSERT_EQ(API_OK, synchronousSyncFolder(0, nullptr, MegaSync::TYPE_TWOWAY, localPath.u8string().c_str(), nullptr, remoteBaseNode->getHandle(), nullptr)) << "API Error adding a new sync";
    std::unique_ptr<MegaSync> sync = waitForSyncState(megaApi[0].get(), remoteBaseNode.get(), MegaSync::RUNSTATE_RUNNING, MegaSync::NO_SYNC_ERROR);
    ASSERT_TRUE(sync && sync->getRunState() == MegaSync::RUNSTATE_RUNNING);
    handle backupId = sync->getBackupId();
    ASSERT_NE(backupId, UNDEF);
    std::string remoteFolder(sync->getLastKnownMegaFolder());

    // Check if a locallogout keeps the sync configured.
    std::string session = unique_ptr<char[]>(dumpSession()).get();
    ASSERT_NO_FATAL_FAILURE(locallogout());
    auto trackerFastLogin = asyncRequestFastLogin(0, session.c_str());
    ASSERT_EQ(API_OK, trackerFastLogin->waitForResult()) << " Failed to establish a login/session for account " << 0;

    target.resetlastEvent();
    ASSERT_NO_FATAL_FAILURE(fetchnodes(0));

    // wait for the event that says all syncs (if any) have been reloaded
    ASSERT_TRUE(WaitFor([&target](){ return target.lastEventsContain(MegaEvent::EVENT_SYNCS_RESTORED); }, 40000));  // 40 seconds because we've seen the first `sc` not respond for 10 seconds

    sync = waitForSyncState(megaApi[0].get(), backupId, MegaSync::RUNSTATE_RUNNING, MegaSync::NO_SYNC_ERROR);
    ASSERT_TRUE(sync && sync->getRunState() == MegaSync::RUNSTATE_RUNNING);
    ASSERT_EQ(remoteFolder, string(sync->getLastKnownMegaFolder()));

    // perform fetchnodes (via megaapi_impl) while nodes are already loaded
    // and a sync is running
    // and check that the Nodes don't seem to disappear while it happens
    // (similar dealing with an ETOOMANY error)
    // just so we are exercising most of that code path somewhere

    RequestTracker fnrt(megaApi[0].get());
    megaApi[0]->invalidateCache();
    megaApi[0]->fetchNodes(&fnrt);

    while (!fnrt.finished)
    {
        // actually we can't check for the node yet - we may load a treecache that
        // doesn't include it.  We have to wait until actionpackets catch up
        //std::unique_ptr<MegaNode> remoteBaseNode2(megaApi[0]->getNodeByHandle(nh));
        //if (!remoteBaseNode2.get())
        //{
        //    remoteBaseNode2.reset();
        //}
        //ASSERT_NE(remoteBaseNode2.get(), (MegaNode*)nullptr);
        WaitMillisec(10);
    }
    // fetchnodes result is only called after statecurrent, which should mean
    // the last actionpacket indicated it was the last.
    megaApi[0]->removeRequestListener(&fnrt);

    std::unique_ptr<MegaNode> remoteBaseNode2(megaApi[0]->getNodeByHandle(nh));
    if (!remoteBaseNode2)
    {
        // see if more actionpackets bring it back (even though the last one did not have ir:1)
        for (int i = 0; i < 10; ++i)
        {
            WaitMillisec(1000);
            remoteBaseNode2.reset(megaApi[0]->getNodeByHandle(nh));
            if (remoteBaseNode2)
            {
                // this does currently occur. commenting for now but we should bring it back once the API delivers ir:1 correctly
                //ASSERT_FALSE(true) << "extra actionpackets delivered missing node after the server said there were no more";

                // at least we are now up to date
                break;
            }
        }
    }

    remoteBaseNode2.reset(megaApi[0]->getNodeByHandle(nh));
    ASSERT_NE(remoteBaseNode2.get(), (MegaNode*)nullptr);


    // Check if a logout with keepSyncsAfterLogout keeps the sync configured.
    ASSERT_NO_FATAL_FAILURE(logout(0, true, maxTimeout));
    size_t syncCount =
        static_cast<size_t>(unique_ptr<MegaSyncList>(megaApi[0]->getSyncs())->size());
    ASSERT_EQ(syncCount, 0u);
    gSessionIDs[0] = "invalid";
    auto trackerLogin = asyncRequestLogin(0, mApi[0].email.c_str(), mApi[0].pwd.c_str());
    ASSERT_EQ(API_OK, trackerLogin->waitForResult()) << " Failed to establish a login/session for account " << 0;

    target.resetlastEvent();
    ASSERT_NO_FATAL_FAILURE(fetchnodes(0));
    ASSERT_TRUE(WaitFor([&target](){ return target.lastEventsContain(MegaEvent::EVENT_SYNCS_RESTORED); }, 10000));

    //sync = waitForSyncState(megaApi[0].get(), backupId, MegaSync::RUNSTATE_DISABLED, MegaSync::LOGGED_OUT);
    sync.reset(megaApi[0]->getSyncByBackupId(backupId));
    ASSERT_TRUE(sync != nullptr);
    ASSERT_EQ(MegaSync::SyncRunningState(sync->getRunState()), MegaSync::RUNSTATE_DISABLED);
    ASSERT_EQ(MegaSync::Error(sync->getError()), MegaSync::LOGGED_OUT);
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

    LOG_verbose << "SyncPaths :  Creating remote folder " << basePath.u8string();
    std::unique_ptr<MegaNode> remoteRootNode(megaApi[0]->getRootNode());
    ASSERT_NE(remoteRootNode.get(), nullptr);
    auto nh = createFolder(0, basePath.u8string().c_str(), remoteRootNode.get());
    ASSERT_NE(nh, UNDEF) << "Error creating remote folder " << basePath.u8string();
    std::unique_ptr<MegaNode> remoteBaseNode(megaApi[0]->getNodeByHandle(nh));
    ASSERT_NE(remoteBaseNode.get(), nullptr) << "Error getting node for remote folder " << basePath.u8string();

    LOG_verbose << "SyncPaths :  Creating sync with local path " << localPath.u8string() << " and remote " << basePath.u8string();
    ASSERT_EQ(API_OK, synchronousSyncFolder(0, nullptr, MegaSync::TYPE_TWOWAY, localPath.u8string().c_str(), nullptr, remoteBaseNode->getHandle(), nullptr))
            << "SyncPaths :  Error creating sync with local path " << localPath.u8string() << " and remote " << basePath.u8string();
    std::unique_ptr<MegaSync> sync = waitForSyncState(megaApi[0].get(), remoteBaseNode.get(), MegaSync::RUNSTATE_RUNNING, MegaSync::NO_SYNC_ERROR);
    ASSERT_TRUE(sync)<< "SyncPaths :  Error reaching RUNNING state for sync with local path " << localPath.u8string() << " and remote " << basePath.u8string();
    ASSERT_EQ(sync->getRunState(), MegaSync::RUNSTATE_RUNNING);

    LOG_verbose << "SyncPaths : Give the sync a few seconds before adding a new file (1)";
    WaitMillisec(waitForSyncsMs);

    LOG_verbose << "SyncPaths :  Adding a file and checking if it is synced: " << filePath.u8string();
    ASSERT_TRUE(createFile(filePath.u8string(), false)) << "Couldn't create " << filePath.u8string();
    auto remoteFile = "/" + string(remoteBaseNode->getName()) + "/" + fileNameStr;
    std::unique_ptr<MegaNode> remoteNode;
    WaitFor([this, &remoteNode, &remoteFile]() -> bool
    {
        remoteNode.reset(megaApi[0]->getNodeByPath(remoteFile.c_str()));
        return (remoteNode.get() != nullptr);
    },50*1000);
    ASSERT_NE(remoteNode.get(), nullptr) << "Failed (1) to get node for " << remoteFile << ", uploaded from " << filePath.u8string();
    LOG_verbose << "SyncPaths :  File " << filePath.u8string() << " is successfully synced to " << remoteFile << ". Downloading the remote file";
    ASSERT_EQ(MegaError::API_OK, doStartDownload(0, remoteNode.get(),
                                                         fileDownloadPath.u8string().c_str(),
                                                         nullptr  /*customName*/,
                                                         nullptr  /*appData*/,
                                                         false    /*startFirst*/,
                                                         nullptr  /*cancelToken*/,
                                                         MegaTransfer::COLLISION_CHECK_FINGERPRINT /*collisionCheck*/,
                                                         MegaTransfer::COLLISION_RESOLUTION_NEW_WITH_N /* collisionResolution */,
                                                         false    /*undelete*/));

    ASSERT_TRUE(fileexists(fileDownloadPath.u8string()));
    deleteFile(fileDownloadPath.u8string());

#if !defined(__APPLE__)
    LOG_verbose << "SyncPaths :  Check that symlinks are not synced";
    const string remotePathOfSymlink("/" + string(remoteBaseNode->getName()) + "/symlink_1A");
    std::unique_ptr<MegaNode> remoteNodeSym(megaApi[0]->getNodeByPath(remotePathOfSymlink.c_str()));
    ASSERT_FALSE(remoteNodeSym) << "Remote node found for symlink, at " << remotePathOfSymlink;

    nh = createFolder(0, "symlink_1A", remoteRootNode.get());
    const string& folderNamedLikeSymlink = remotePathOfSymlink;
    ASSERT_NE(nh, UNDEF) << "Error creating remote folder " << folderNamedLikeSymlink;
    remoteNodeSym.reset(megaApi[0]->getNodeByHandle(nh));
    ASSERT_TRUE(remoteNodeSym) << "Error getting node of " << folderNamedLikeSymlink;

#ifndef WIN32
    {
        LOG_verbose << "SyncPaths :  Check that symlinks are considered when creating a sync";
        ASSERT_EQ(API_EARGS, synchronousSyncFolder(0, nullptr, MegaSync::TYPE_TWOWAY, (fs::current_path() / "symlink_1A").u8string().c_str(), nullptr, remoteNodeSym->getHandle(), nullptr))
                << "Sync with local path being a symlink to a folder already synced should have failed";
        ASSERT_EQ(MegaSync::LOCAL_PATH_SYNC_COLLISION, mApi[0].lastSyncError) << "Sync with local path in another sync should have ended with " << MegaSync::LOCAL_PATH_SYNC_COLLISION;
    }
#endif

    // Disable the first one, create again the one with the symlink, check that it is working and check if the first fails when enabled.
    LOG_verbose << "SyncPaths :  Disable sync with local path " << localPath.u8string() << " and remote " << basePath.u8string();
    auto tagID = sync->getBackupId();
    ASSERT_EQ(API_OK, synchronousSetSyncRunState(0, tagID, MegaSync::RUNSTATE_DISABLED))
        << "API Error disabling sync";
    sync = waitForSyncState(megaApi[0].get(), tagID, MegaSync::RUNSTATE_DISABLED, MegaSync::NO_SYNC_ERROR);
    ASSERT_TRUE(sync);
    ASSERT_EQ(sync->getRunState(), MegaSync::RUNSTATE_DISABLED);

    string localSymlinkToSync((fs::current_path() / "symlink_1A").u8string());
    LOG_verbose << "SyncPaths :  Create sync from: " << localSymlinkToSync << " to remote path: " << remoteNodeSym->getName();
    ASSERT_EQ(API_OK, synchronousSyncFolder(0, nullptr, MegaSync::TYPE_TWOWAY, localSymlinkToSync.c_str(), nullptr, remoteNodeSym->getHandle(), nullptr))
            << "Error adding sync with local path " << localSymlinkToSync << " and remote " << folderNamedLikeSymlink;
    std::unique_ptr<MegaSync> syncSym = waitForSyncState(megaApi[0].get(), remoteNodeSym.get(), MegaSync::RUNSTATE_RUNNING, MegaSync::NO_SYNC_ERROR);
    ASSERT_TRUE(syncSym) << "Error getting sync in RUNNING state; local path " << localSymlinkToSync << " and remote " << folderNamedLikeSymlink;;
    ASSERT_EQ(syncSym->getRunState(), MegaSync::RUNSTATE_RUNNING);

    LOG_verbose << "SyncPaths : Give the sync a few seconds before adding a new file (2)";
    WaitMillisec(waitForSyncsMs);

    // Now that we have a sync whose root folder is a symlink, add a file to the path that the symlink points to, and check if it is synced
    auto fileToCreate = localPath / "level_1A" / fs::u8path(fileNameStr.c_str());
    LOG_verbose << "SyncPaths :  Adding a file and checking if it is synced: " << fileToCreate.u8string();
    ASSERT_TRUE(createFile(fileToCreate.u8string(), false)) << "failed to create local file " << fileToCreate;
    remoteFile = "/" + string(remoteNodeSym->getName()) + "/" + fileNameStr;
    WaitFor([this, &remoteNode, &remoteFile]() -> bool
    {
        remoteNode.reset(megaApi[0]->getNodeByPath(remoteFile.c_str()));
        return (remoteNode.get() != nullptr);
    },50*1000);
    ASSERT_TRUE(remoteNode) << "Failed (2) to get remote node for " << remoteFile << " uploaded from " << fileToCreate.u8string();
    LOG_verbose << "SyncPaths :  File " << fileToCreate.u8string() << " is successfully synced to " << remoteFile << ". Downloading the remote file";
    ASSERT_EQ(MegaError::API_OK, doStartDownload(0,remoteNode.get(),
                                                         fileDownloadPath.u8string().c_str(),
                                                         nullptr  /*customName*/,
                                                         nullptr  /*appData*/,
                                                         false    /*startFirst*/,
                                                         nullptr  /*cancelToken*/,
                                                         MegaTransfer::COLLISION_CHECK_FINGERPRINT /*collisionCheck*/,
                                                         MegaTransfer::COLLISION_RESOLUTION_NEW_WITH_N /* collisionResolution */,
                                                         false    /*undelete*/));

    ASSERT_TRUE(fileexists(fileDownloadPath.u8string()));
    deleteFile(fileDownloadPath.u8string());

#ifndef WIN32
    {
        LOG_verbose << "SyncPaths :  Check that we cannot enable again the no-symlink sync with local path " << localPath.u8string() << " and remote " << basePath.u8string();
        ASSERT_EQ(API_EARGS, synchronousSetSyncRunState(0, tagID, MegaSync::RUNSTATE_RUNNING))
            << "API Error enabling a sync";
        ASSERT_EQ(MegaSync::LOCAL_PATH_SYNC_COLLISION, mApi[0].lastSyncError);
    }
#endif

#endif

    LOG_verbose << "SyncPaths :  All done. Cleaning up";
    ASSERT_NO_FATAL_FAILURE(cleanUp(this->megaApi[0].get(), basePath));
    ASSERT_NO_FATAL_FAILURE(cleanUp(this->megaApi[0].get(), "symlink_1A"));
}

/**
 * @brief TEST_F SearchByPathOfType
 *
 * Testing search nodes by path of specified type
 */
TEST_F(SdkTest, SearchByPathOfType)
{
    LOG_info << "___TEST SearchByPathOfType___";
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    std::unique_ptr<MegaNode> rootNode{ megaApi[0]->getRootNode() };
    string duplicateName = "fileAndFolderName";

    // Upload test file
    MegaHandle fileInRoot = INVALID_HANDLE;
    ASSERT_TRUE(createFile(duplicateName, false)) << "Couldn't create file " << duplicateName;
    ASSERT_EQ(MegaError::API_OK, doStartUpload(0, &fileInRoot, duplicateName.c_str(),
        rootNode.get(),
        nullptr /*fileName*/,
        ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
        nullptr /*appData*/,
        false   /*isSourceTemporary*/,
        false   /*startFirst*/,
        nullptr /*cancelToken*/)) << "Cannot upload a test file";

    // Test not found cases:
    {
        for (auto type : {MegaNode::TYPE_FILE, MegaNode::TYPE_FOLDER, MegaNode::TYPE_UNKNOWN})
        {
            for (auto pathToNonExisting : {"this/does/not/exist", "/this/does/not/exist", "./thisdoesnotexist", "thisdoesnotexist"})
            {
                std::unique_ptr<MegaNode> fileNode{ megaApi[0]->getNodeByPathOfType(pathToNonExisting, nullptr, type)};
                ASSERT_FALSE(fileNode);
            }
        }
    }

    // Test file search using relative path
    std::unique_ptr<MegaNode> fileNode{ megaApi[0]->getNodeByPathOfType(duplicateName.c_str(), rootNode.get()) };
    ASSERT_TRUE(fileNode) << "Could not find node for file " << duplicateName;
    ASSERT_EQ(fileNode->getHandle(), fileInRoot);
    ASSERT_EQ(fileNode->getType(), MegaNode::TYPE_FILE);
    ASSERT_STREQ(fileNode->getName(), duplicateName.c_str());

    fileNode.reset(megaApi[0]->getNodeByPathOfType(duplicateName.c_str(), rootNode.get(), MegaNode::TYPE_FILE));
    ASSERT_TRUE(fileNode) << "Could not find node for file " << duplicateName;
    ASSERT_EQ(fileNode->getHandle(), fileInRoot);
    ASSERT_EQ(fileNode->getType(), MegaNode::TYPE_FILE);
    ASSERT_STREQ(fileNode->getName(), duplicateName.c_str());

    fileNode.reset(megaApi[0]->getNodeByPathOfType(duplicateName.c_str(), rootNode.get(), MegaNode::TYPE_FOLDER));
    ASSERT_FALSE(fileNode) << "Found node for file while explicitly searching for folder " << duplicateName;

    // Create test folder
    auto folderInRoot = createFolder(0, duplicateName.c_str(), rootNode.get());
    ASSERT_NE(folderInRoot, INVALID_HANDLE) << "Error creating remote folder " << duplicateName;

    // Test search using relative path
    std::unique_ptr<MegaNode> folderNode{ megaApi[0]->getNodeByPathOfType(duplicateName.c_str(), rootNode.get()) };
    ASSERT_TRUE(folderNode) << "Could not find node for folder " << duplicateName;
    ASSERT_EQ(folderNode->getHandle(), folderInRoot);
    ASSERT_EQ(folderNode->getType(), MegaNode::TYPE_FOLDER);
    ASSERT_STREQ(folderNode->getName(), duplicateName.c_str());

    fileNode.reset(megaApi[0]->getNodeByPathOfType(duplicateName.c_str(), rootNode.get(), MegaNode::TYPE_FILE));
    ASSERT_TRUE(fileNode) << "Could not find node for file " << duplicateName;
    ASSERT_EQ(fileNode->getHandle(), fileInRoot);
    ASSERT_EQ(fileNode->getType(), MegaNode::TYPE_FILE);
    ASSERT_STREQ(fileNode->getName(), duplicateName.c_str());

    folderNode.reset(megaApi[0]->getNodeByPathOfType(duplicateName.c_str(), rootNode.get(), MegaNode::TYPE_FOLDER));
    ASSERT_TRUE(folderNode) << "Could not find node for folder " << duplicateName;
    ASSERT_EQ(folderNode->getHandle(), folderInRoot);
    ASSERT_EQ(folderNode->getType(), MegaNode::TYPE_FOLDER);
    ASSERT_STREQ(folderNode->getName(), duplicateName.c_str());

    // Test search using absolute path
    string absolutePath = '/' + duplicateName;
    folderNode.reset(megaApi[0]->getNodeByPathOfType(absolutePath.c_str()));
    ASSERT_TRUE(folderNode) << "Could not find node for folder " << absolutePath;
    ASSERT_EQ(folderNode->getHandle(), folderInRoot);
    ASSERT_EQ(folderNode->getType(), MegaNode::TYPE_FOLDER);
    ASSERT_STREQ(folderNode->getName(), duplicateName.c_str());

    fileNode.reset(megaApi[0]->getNodeByPathOfType(absolutePath.c_str(), nullptr, MegaNode::TYPE_FILE));
    ASSERT_TRUE(fileNode) << "Could not find node for file " << absolutePath;
    ASSERT_EQ(fileNode->getHandle(), fileInRoot);
    ASSERT_EQ(fileNode->getType(), MegaNode::TYPE_FILE);
    ASSERT_STREQ(fileNode->getName(), duplicateName.c_str());

    folderNode.reset(megaApi[0]->getNodeByPathOfType(absolutePath.c_str(), nullptr, MegaNode::TYPE_FOLDER));
    ASSERT_TRUE(folderNode) << "Could not find node for folder " << absolutePath;
    ASSERT_EQ(folderNode->getHandle(), folderInRoot);
    ASSERT_EQ(folderNode->getType(), MegaNode::TYPE_FOLDER);
    ASSERT_STREQ(folderNode->getName(), duplicateName.c_str());
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
    std::unique_ptr<MegaSync> sync = waitForSyncState(megaApi[0].get(), remoteBaseNode.get(), MegaSync::RUNSTATE_RUNNING, MegaSync::NO_SYNC_ERROR);
    ASSERT_TRUE(sync && sync->getRunState() == MegaSync::RUNSTATE_RUNNING);
    handle backupId = sync->getBackupId();

    LOG_verbose << "SyncOQTransitions :  Filling up storage space";
    auto importHandle = importPublicLink(0, MegaClient::MEGAURL+"/file/D4AGlbqY#Ak-OW4MP7lhnQxP9nzBU1bOP45xr_7sXnIz8YYqOBUg", remoteFillNode.get());
    std::unique_ptr<MegaNode> remote1GBFile(megaApi[0]->getNodeByHandle(importHandle));

    ASSERT_NO_FATAL_FAILURE(synchronousGetSpecificAccountDetails(0, true, false, false)); // Get account size.
    ASSERT_NE(mApi[0].accountDetails, nullptr);
    auto filesNeeded =
        static_cast<int>(mApi[0].accountDetails->getStorageMax() / remote1GBFile->getSize()) + 1;

    for (int i=1; i < filesNeeded; i++)
    {
        ASSERT_EQ(API_OK, doCopyNode(0, nullptr, remote1GBFile.get(), remoteFillNode.get(), (remote1GBFile->getName() + to_string(i)).c_str()));
    }
    std::unique_ptr<MegaNode> last1GBFileNode(megaApi[0]->getChildNode(remoteFillNode.get(), (remote1GBFile->getName() + to_string(filesNeeded-1)).c_str()));

    {
        LOG_verbose << "SyncOQTransitions :  Check that Sync is disabled due to OQ.";
        ASSERT_NO_FATAL_FAILURE(synchronousGetSpecificAccountDetails(0, true, false, false)); // Needed to ensure we know we are in OQ
        sync = waitForSyncState(megaApi[0].get(), backupId, MegaSync::RUNSTATE_SUSPENDED, MegaSync::STORAGE_OVERQUOTA);
        ASSERT_TRUE(sync);
        ASSERT_EQ(sync->getRunState(), MegaSync::RUNSTATE_SUSPENDED);
        ASSERT_EQ(MegaSync::STORAGE_OVERQUOTA, sync->getError());

        LOG_verbose << "SyncOQTransitions :  Check that Sync could not be enabled while disabled due to OQ.";
        ASSERT_EQ(API_EFAILED, synchronousSetSyncRunState(0, backupId, MegaSync::RUNSTATE_RUNNING))
            << "API Error enabling a sync";
        sync = waitForSyncState(megaApi[0].get(), backupId, MegaSync::RUNSTATE_SUSPENDED, MegaSync::STORAGE_OVERQUOTA);  // fresh snapshot of sync state
        ASSERT_TRUE(sync && sync->getRunState() == MegaSync::RUNSTATE_SUSPENDED);
        ASSERT_EQ(MegaSync::STORAGE_OVERQUOTA, sync->getError());
    }

    LOG_verbose << "SyncOQTransitions :  Free up space and check that Sync is not active again.";
    ASSERT_EQ(API_OK, synchronousRemove(0, last1GBFileNode.get()));
    ASSERT_NO_FATAL_FAILURE(synchronousGetSpecificAccountDetails(0, true, false, false)); // Needed to ensure we know we are not in OQ
    sync = waitForSyncState(megaApi[0].get(), backupId, MegaSync::RUNSTATE_SUSPENDED, MegaSync::STORAGE_OVERQUOTA);  // of course the error stays as OverQuota.  Sync still not re-enabled.
    ASSERT_TRUE(sync && sync->getRunState() == MegaSync::RUNSTATE_SUSPENDED);

    LOG_verbose << "SyncOQTransitions :  Share big files folder with another account.";

    ASSERT_EQ(API_OK, synchronousInviteContact(0, mApi[1].email.c_str(), "SyncOQTransitions contact request A to B", MegaContactRequest::INVITE_ACTION_ADD));
    ASSERT_TRUE(WaitFor([this]()
    {
        return unique_ptr<MegaContactRequestList>(megaApi[1]->getIncomingContactRequests())->size() == 1;
    }, 60*1000));
    ASSERT_NO_FATAL_FAILURE(getContactRequest(1, false));
    ASSERT_EQ(API_OK, synchronousReplyContactRequest(1, mApi[1].cr.get(), MegaContactRequest::REPLY_ACTION_ACCEPT));

    if (gManualVerification)
    {
        if (!areCredentialsVerified(0, mApi[1].email)) {ASSERT_NO_FATAL_FAILURE(verifyCredentials(0, mApi[1].email));}
        if (!areCredentialsVerified(1, mApi[0].email)) {ASSERT_NO_FATAL_FAILURE(verifyCredentials(1, mApi[0].email));}
    }

    ASSERT_NO_FATAL_FAILURE(shareFolder(remoteFillNode.get(), mApi[1].email.c_str(), MegaShare::ACCESS_FULL));
    ASSERT_TRUE(WaitFor([this]()
    {
        return unique_ptr<MegaShareList>(megaApi[1]->getInSharesList())->size() == 1;
    }, 60*1000));

    // Wait for the inshare node to be decrypted
    ASSERT_TRUE(WaitFor([this, &remoteFillNode]() { return unique_ptr<MegaNode>(megaApi[1]->getNodeByHandle(remoteFillNode->getHandle()))->isNodeKeyDecrypted(); }, 60*1000));

    unique_ptr<MegaUser> contact(megaApi[1]->getContact(mApi[0].email.c_str()));
    unique_ptr<MegaNodeList> nodeList(megaApi[1]->getInShares(contact.get()));
    ASSERT_EQ(nodeList->size(), 1);
    MegaNode* inshareNode = nodeList->get(0);

    // Wait for the outshare to be added to the sharer's node by the action packets
    ASSERT_TRUE(WaitFor([this, &remoteFillNode]() { return unique_ptr<MegaNode>(megaApi[0]->getNodeByHandle(remoteFillNode->getHandle()))->isOutShare(); }, 60*1000));

    // Make sure that search functionality finds them
    std::unique_ptr<MegaSearchFilter> filterResults(MegaSearchFilter::createInstance());
    filterResults->byName(fillPath.u8string().c_str());
    filterResults->byLocation(MegaApi::SEARCH_TARGET_OUTSHARE);
    std::unique_ptr<MegaNodeList> outShares(megaApi[0]->search(filterResults.get()));
    ASSERT_TRUE(outShares);
    ASSERT_EQ(outShares->size(), 1);
    ASSERT_EQ(outShares->get(0)->getHandle(), remoteFillNode->getHandle());

    filterResults.reset(MegaSearchFilter::createInstance());
    filterResults->byName(fillPath.u8string().c_str());
    filterResults->byLocation(MegaApi::SEARCH_TARGET_INSHARE);
    std::unique_ptr<MegaNodeList> inShares(megaApi[1]->search(filterResults.get()));
    ASSERT_TRUE(inShares);
    ASSERT_EQ(inShares->size(), 1);
    ASSERT_EQ(inShares->get(0)->getHandle(), remoteFillNode->getHandle());

    LOG_verbose << "SyncOQTransitions :  Check for transition to OQ while offline.";
    std::string session = unique_ptr<char[]>(dumpSession()).get();
    ASSERT_NO_FATAL_FAILURE(locallogout());

    std::unique_ptr<MegaNode> remote1GBFile2nd(megaApi[1]->getChildNode(inshareNode, remote1GBFile->getName()));
    ASSERT_EQ(API_OK, doCopyNode(1, nullptr, remote1GBFile2nd.get(), inshareNode, (remote1GBFile2nd->getName() + to_string(filesNeeded-1)).c_str()));

    {
        ASSERT_NO_FATAL_FAILURE(resumeSession(session.c_str()));   // sync not actually resumed here though (though it would be if it was still enabled)
        ASSERT_NO_FATAL_FAILURE(fetchnodes(0));
        ASSERT_NO_FATAL_FAILURE(synchronousGetSpecificAccountDetails(0, true, false, false)); // Needed to ensure we know we are in OQ
        sync = waitForSyncState(megaApi[0].get(), backupId, MegaSync::RUNSTATE_SUSPENDED, MegaSync::STORAGE_OVERQUOTA);
        ASSERT_TRUE(sync && sync->getRunState() == MegaSync::RUNSTATE_SUSPENDED);
        ASSERT_EQ(MegaSync::STORAGE_OVERQUOTA, sync->getError());
    }

    LOG_verbose << "SyncOQTransitions :  Check for transition from OQ while offline.";
    ASSERT_NO_FATAL_FAILURE(locallogout());

    std::unique_ptr<MegaNode> toRemoveNode(megaApi[1]->getChildNode(inshareNode, (remote1GBFile->getName() + to_string(filesNeeded-1)).c_str()));
    ASSERT_EQ(API_OK, synchronousRemove(1, toRemoveNode.get()));

    ASSERT_NO_FATAL_FAILURE(resumeSession(session.c_str()));
    ASSERT_NO_FATAL_FAILURE(fetchnodes(0));
    ASSERT_NO_FATAL_FAILURE(synchronousGetSpecificAccountDetails(0, true, false, false)); // Needed to ensure we know we are no longer in OQ
    sync = waitForSyncState(megaApi[0].get(), backupId, MegaSync::RUNSTATE_SUSPENDED, MegaSync::STORAGE_OVERQUOTA);
    ASSERT_TRUE(sync && sync->getRunState() == MegaSync::RUNSTATE_SUSPENDED);

    ASSERT_NO_FATAL_FAILURE(cleanUp(this->megaApi[0].get(), basePath));
    ASSERT_NO_FATAL_FAILURE(cleanUp(this->megaApi[0].get(), fillPath));
}

/**
 * @brief TEST_F SyncImage
 *
 * Testing the upsync of an image and verifying that we can successfully retrieve the thumbnail and preview.
 */
#if !USE_FREEIMAGE
TEST_F(SdkTest, DISABLED_SyncImage)
#else
TEST_F(SdkTest, SyncImage)
#endif
{
    LOG_info << "___TEST SyncImage___";
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    string basePathStr = "SyncImage";
    string fileNameStr = IMAGEFILE;

    fs::path basePath = fs::u8path(basePathStr.c_str());
    const auto localPath = fs::current_path() / basePath;
    fs::path filePath = localPath / fs::u8path(fileNameStr.c_str());
    ASSERT_NO_FATAL_FAILURE(cleanUp(this->megaApi[0].get(), basePath));

    // Create local directories
    std::error_code ignoredEc;
    fs::remove_all(localPath, ignoredEc);
    fs::create_directory(localPath);

    LOG_verbose << "SyncImage :  Creating remote folder " << basePath.u8string();
    std::unique_ptr<MegaNode> remoteRootNode(megaApi[0]->getRootNode());
    ASSERT_NE(remoteRootNode.get(), nullptr);
    auto nh = createFolder(0, basePath.u8string().c_str(), remoteRootNode.get());
    ASSERT_NE(nh, UNDEF) << "Error creating remote folder " << basePath.u8string();
    std::unique_ptr<MegaNode> remoteBaseNode(megaApi[0]->getNodeByHandle(nh));
    ASSERT_NE(remoteBaseNode.get(), nullptr) << "Error getting node for remote folder " << basePath.u8string();

    LOG_verbose << "SyncImage :  Creating sync with local path " << localPath.u8string() << " and remote " << basePath.u8string();
    ASSERT_EQ(API_OK, synchronousSyncFolder(0, nullptr, MegaSync::TYPE_TWOWAY, localPath.u8string().c_str(), nullptr, remoteBaseNode->getHandle(), nullptr))
            << "SyncImage :  Error creating sync with local path " << localPath.u8string() << " and remote " << basePath.u8string();
    std::unique_ptr<MegaSync> sync = waitForSyncState(megaApi[0].get(), remoteBaseNode.get(), MegaSync::RUNSTATE_RUNNING, MegaSync::NO_SYNC_ERROR);
    ASSERT_TRUE(sync)<< "SyncImage :  Error reaching RUNNING state for sync with local path " << localPath.u8string() << " and remote " << basePath.u8string();
    ASSERT_EQ(sync->getRunState(), MegaSync::RUNSTATE_RUNNING);

    LOG_verbose << "SyncImage :  Give the sync a few seconds before adding a new file";
    WaitMillisec(waitForSyncsMs);

    LOG_verbose << "SyncImage :  Adding the image file and checking if it is synced: " << filePath.u8string();
    ASSERT_TRUE(getFileFromArtifactory("test-data/" + fileNameStr, filePath));
    auto remoteFile = "/" + string(remoteBaseNode->getName()) + "/" + fileNameStr;
    std::unique_ptr<MegaNode> remoteNode;
    WaitFor([this, &remoteNode, &remoteFile]() -> bool
    {
        remoteNode.reset(megaApi[0]->getNodeByPath(remoteFile.c_str()));
        return (remoteNode.get() != nullptr);
    },50*1000);
    ASSERT_NE(remoteNode.get(), nullptr) << "Failed to get node for " << remoteFile << ", uploaded from " << filePath.u8string();

    // Get the thumbnail and preview of the uploaded image
    LOG_verbose << "SyncImage :  Image file " << filePath.u8string() << " is successfully synced to " << remoteFile << ". Checking the thumbnail and preview";
    ASSERT_EQ(API_OK, doGetThumbnail(0, remoteNode.get(), THUMBNAIL.c_str()));
    ASSERT_EQ(API_OK, doGetPreview(0, remoteNode.get(), PREVIEW.c_str()));

    LOG_verbose << "SyncImage :  All done. Cleaning up";
    ASSERT_NO_FATAL_FAILURE(cleanUp(this->megaApi[0].get(), basePath));
}
#endif // ENABLE_SYNC

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

    unsigned numFolders = 90;

    ASSERT_NO_FATAL_FAILURE(cleanUp(this->megaApi[0].get(), baseFolder));

    LOG_verbose << "StressTestSDKInstancesOverWritableFolders :  Creating remote folder";
    std::unique_ptr<MegaNode> remoteRootNode(megaApi[0]->getRootNode());
    ASSERT_NE(remoteRootNode.get(), nullptr);
    auto nh = createFolder(0, baseFolder.c_str(), remoteRootNode.get());
    ASSERT_NE(nh, UNDEF) << "Error creating remote basePath";
    std::unique_ptr<MegaNode> remoteBaseNode(megaApi[0]->getNodeByHandle(nh));
    ASSERT_NE(remoteBaseNode.get(), nullptr);

    // create subfolders ...
    for (unsigned index = 0; index < numFolders; index++)
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

    std::vector<MegaApiTestPointer> exportedFolderApis;
    exportedFolderApis.resize(howMany);

    std::vector<std::string> exportedLinks;
    exportedLinks.resize(howMany);

    std::vector<std::string> authKeys;
    authKeys.resize(howMany);

    // export subfolders
    for (unsigned index = 0; index < howMany; index++)
    {
        string subFolderPath = string("subfolder_").append(SSTR(index));
        std::unique_ptr<MegaNode> remoteSubFolderNode(megaApi[0]->getNodeByPath(subFolderPath.c_str(), remoteBaseNode.get()));
        ASSERT_NE(remoteSubFolderNode.get(), nullptr);

        // ___ get a link to the file node
        string nodelink = createPublicLink(0,
                                           remoteSubFolderNode.get(),
                                           0,
                                           maxTimeout,
                                           false /*mApi[0].accountDetails->getProLevel() == 0)*/,
                                           true /*writable*/);
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
    for (unsigned index = 0; index < howMany; index++)
    {
        exportedFolderApis[static_cast<size_t>(index)] =
            newMegaApi(APP_KEY.c_str(),
                       megaApiCacheFolder(static_cast<int>(index) + 10).c_str(),
                       USER_AGENT.c_str(),
                       static_cast<unsigned>(THREADS_PER_MEGACLIENT));

        // reduce log level to something beareable
        exportedFolderApis[static_cast<size_t>(index)]->setLogLevel(MegaApi::LOG_LEVEL_WARNING);
    }

    // login to exported folders
    for (unsigned index = 0; index < howMany; index++)
    {
        string nodelink = exportedLinks[index];
        string authKey = authKeys[index];

        out() << "login to exported folder " << index;
        trackers[static_cast<size_t>(index)] =
            asyncRequestLoginToFolder(exportedFolderApis[static_cast<size_t>(index)].get(),
                                      nodelink.c_str(),
                                      authKey.c_str());
    }

    // wait for login to complete:
    for (unsigned index = 0; index < howMany; ++index)
    {
        ASSERT_EQ(API_OK, trackers[index]->waitForResult())
            << " Failed to fetchnodes for accout " << index;
    }

    // perform parallel fetchnodes for each
    for (unsigned index = 0; index < howMany; ++index)
    {
        out() << "Fetching nodes for account " << index;
        trackers[index] =
            asyncRequestFetchnodes(exportedFolderApis[static_cast<size_t>(index)].get());
    }

    // wait for fetchnodes to complete:
    for (unsigned index = 0; index < howMany; ++index)
    {
        ASSERT_EQ(API_OK, trackers[index]->waitForResult())
            << " Failed to fetchnodes for accout " << index;
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

    std::vector<MegaApiTestPointer> exportedFolderApis;
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
        string nodelink = createPublicLink(0,
                                           remoteSubFolderNode.get(),
                                           0,
                                           maxTimeout,
                                           false /*mApi[0].accountDetails->getProLevel() == 0)*/,
                                           true /*writable*/);
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
        exportedFolderApis[index] =
            newMegaApi(APP_KEY.c_str(),
                       megaApiCacheFolder(static_cast<int>(index) + 10).c_str(),
                       USER_AGENT.c_str(),
                       static_cast<unsigned>(THREADS_PER_MEGACLIENT));

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
        sessions[index] = unique_ptr<char[]>(exportedFolderApis[index]->dumpSession()).get();
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

    if (gManualVerification)
    {
        if (!areCredentialsVerified(0, mApi[1].email)) {ASSERT_NO_FATAL_FAILURE(verifyCredentials(0, mApi[1].email));}
        if (!areCredentialsVerified(1, mApi[0].email)) {ASSERT_NO_FATAL_FAILURE(verifyCredentials(1, mApi[0].email));}
    }

    //--- Create a new folder in cloud drive ---
    std::unique_ptr<MegaNode> rootnode{megaApi[0]->getRootNode()};
    char foldername1[64] = "Shared-folder";
    MegaHandle hfolder1 = createFolder(0, foldername1, rootnode.get());
    ASSERT_NE(hfolder1, UNDEF);
    std::unique_ptr<MegaNode> n1(megaApi[0]->getNodeByHandle(hfolder1));
    ASSERT_NE(n1.get(), nullptr);

    // --- Create a new outgoing share ---
    bool check1, check2; // reset flags expected to be true in asserts below
    mApi[0].mOnNodesUpdateCompletion = createOnNodesUpdateLambda(hfolder1, MegaNode::CHANGE_TYPE_OUTSHARE, check1);
    mApi[1].mOnNodesUpdateCompletion = createOnNodesUpdateLambda(hfolder1, MegaNode::CHANGE_TYPE_INSHARE, check2);

    ASSERT_NO_FATAL_FAILURE(shareFolder(n1.get(), mApi[1].email.c_str(), MegaShare::ACCESS_READWRITE));
    ASSERT_TRUE( waitForResponse(&check1) )   // at the target side (main account)
            << "Node update not received after " << maxTimeout << " seconds";
    ASSERT_TRUE( waitForResponse(&check2) )   // at the target side (auxiliar account)
            << "Node update not received after " << maxTimeout << " seconds";
    ASSERT_EQ(check1, true);
    ASSERT_EQ(check2, true);

    // Wait for the inshare node to be decrypted
    ASSERT_TRUE(WaitFor([this, &n1]() { return unique_ptr<MegaNode>(megaApi[1]->getNodeByHandle(n1->getHandle()))->isNodeKeyDecrypted(); }, 60*1000));

    std::unique_ptr<MegaShareList> sl(megaApi[1]->getInSharesList(::MegaApi::ORDER_NONE));
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
                            n1.get(),
                            nullptr /*fileName*/,
                            ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
                            nullptr /*appData*/,
                            false   /*isSourceTemporary*/,
                            false   /*startFirst*/,
                            nullptr /*cancelToken*/,
                            &tt);   /*MegaTransferListener*/

    // --- Pause transfer, revoke out-share permissions for secondary account and resume transfer ---
    megaApi[1]->pauseTransfers(true);

    mApi[0].mOnNodesUpdateCompletion = createOnNodesUpdateLambda(hfolder1, MegaNode::CHANGE_TYPE_OUTSHARE, check1);
    mApi[1].mOnNodesUpdateCompletion = createOnNodesUpdateLambda(hfolder1, MegaNode::CHANGE_TYPE_REMOVED, check2);

    ASSERT_NO_FATAL_FAILURE(shareFolder(n1.get(), mApi[1].email.c_str(), MegaShare::ACCESS_UNKNOWN));
    ASSERT_TRUE( waitForResponse(&check1) )   // at the target side (main account)
            << "Node update not received after " << maxTimeout << " seconds";
    ASSERT_TRUE( waitForResponse(&check2) )   // at the target side (auxiliar account)
            << "Node update not received after " << maxTimeout << " seconds";
    megaApi[1]->pauseTransfers(false);
    // --- Wait for transfer completion

    // in fact we get EACCESS - maybe this API feature is not migrated to live yet?
    ASSERT_EQ(API_OK, ErrorCodes(tt.waitForResult(600))) << "Upload transfer failed";
    ASSERT_EQ(check1, true);
    ASSERT_EQ(check2, true);

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
 * File is expected at the directory returned by getTestDataDir().
 */
#if !USE_FREEIMAGE || !USE_MEDIAINFO
TEST_F(SdkTest, DISABLED_SdkTestAudioFileThumbnail)
#else
TEST_F(SdkTest, SdkTestAudioFileThumbnail)
#endif
{
    LOG_info << "___TEST Audio File Thumbnail___";

    static const std::string AUDIO_FILENAME = "test_cover_png.mp3";
    ASSERT_TRUE(getFileFromArtifactory("test-data/" + AUDIO_FILENAME, AUDIO_FILENAME));

    ASSERT_NO_FATAL_FAILURE(getAccountsForTest());

    std::unique_ptr<MegaNode> rootnode{ megaApi[0]->getRootNode() };
    ASSERT_EQ(MegaError::API_OK,
              doStartUpload(0,
                            nullptr,
                            AUDIO_FILENAME.c_str(),
                            rootnode.get(),
                            nullptr /*fileName*/,
                            ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
                            nullptr /*appData*/,
                            false /*isSourceTemporary*/,
                            false /*startFirst*/,
                            nullptr /*cancelToken*/))
        << "Cannot upload test file " << AUDIO_FILENAME;
    std::unique_ptr<MegaNode> node(megaApi[0]->getNodeByPath(AUDIO_FILENAME.c_str(), rootnode.get()));
    ASSERT_TRUE(node->hasPreview() && node->hasThumbnail());
}

/**
 * @brief TEST_F SearchNodesByCreationTime
 *
 * Test filtering nodes by ctime in search() and getChildren()
 *
 */
TEST_F(SdkTest, SearchNodesByCreationTime)
{
    LOG_info << "___TEST SearchNodesByCreationTime___";

    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    unique_ptr<MegaNode> rootnode(megaApi[0]->getRootNode());
    ASSERT_TRUE(rootnode);

    string folderName = "TestCTime_Folder.Foo";
    MegaHandle folderHandle = createFolder(0, folderName.c_str(), rootnode.get());
    ASSERT_NE(folderHandle, INVALID_HANDLE);
    unique_ptr<MegaNode> folderNode(megaApi[0]->getNodeByHandle(folderHandle));
    ASSERT_TRUE(folderNode);
    int64_t folderCTime = folderNode->getCreationTime();

    std::this_thread::sleep_for(std::chrono::milliseconds{1500}); // avoid nodes having identical CTime

    string fileName = "TestCTime_File.bar";
    ASSERT_TRUE(createFile(fileName, false));
    MegaHandle fileHandle = 0;
    ASSERT_EQ(MegaError::API_OK, doStartUpload(0, &fileHandle, fileName.c_str(), folderNode.get(),
                                               nullptr /*fileName*/,
                                               ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
                                               nullptr /*appData*/,
                                               false   /*isSourceTemporary*/,
                                               false   /*startFirst*/,
                                               nullptr /*cancelToken*/)) << "Cannot upload " << fileName;
    ASSERT_NE(fileHandle, INVALID_HANDLE);
    unique_ptr<MegaNode> fileNode(megaApi[0]->getNodeByHandle(fileHandle));
    ASSERT_TRUE(fileNode);
    int64_t fileCTime = fileNode->getCreationTime();

    ASSERT_NE(folderCTime, fileCTime) << "Test file and folder have the same creation time";

    // getChildren()
    unique_ptr<MegaSearchFilter> f(MegaSearchFilter::createInstance());
    f->byName("TestCTime_*");
    f->byLocationHandle(folderHandle);
    unique_ptr<MegaNodeList> results(megaApi[0]->getChildren(f.get()));
    ASSERT_EQ(results->size(), 1);
    ASSERT_EQ(results->get(0)->getName(), fileName);

    f->byCreationTime(fileCTime, fileCTime);
    results.reset(megaApi[0]->getChildren(f.get()));
    ASSERT_EQ(results->size(), 0) << results->get(0)->getName();

    f->byCreationTime(fileCTime - 1, fileCTime + 1);
    results.reset(megaApi[0]->getChildren(f.get()));
    ASSERT_EQ(results->size(), 1);
    ASSERT_EQ(results->get(0)->getName(), fileName);

    // cope with time differences in remote FS and local FS
    const int64_t& olderCTime = folderCTime < fileCTime ? folderCTime : fileCTime;
    const string& olderName = folderCTime < fileCTime ? folderName : fileName;
    const int64_t& newerCTime = folderCTime > fileCTime ? folderCTime : fileCTime;
    const string& newerName = folderCTime > fileCTime ? folderName : fileName;

    // search()
    f->byLocationHandle(INVALID_HANDLE);
    f->byCreationTime(0, 0);
    results.reset(megaApi[0]->search(f.get()));
    ASSERT_EQ(results->size(), 2);

    f->byCreationTime(olderCTime, newerCTime);
    results.reset(megaApi[0]->search(f.get()));
    ASSERT_EQ(results->size(), 0);

    f->byCreationTime(olderCTime - 1, newerCTime + 1);
    results.reset(megaApi[0]->search(f.get()));
    ASSERT_EQ(results->size(), 2);

    f->byCreationTime(0, newerCTime);
    results.reset(megaApi[0]->search(f.get()));
    ASSERT_EQ(results->size(), 1);
    ASSERT_EQ(results->get(0)->getName(), olderName);

    f->byCreationTime(olderCTime, 0);
    results.reset(megaApi[0]->search(f.get()));
    ASSERT_EQ(results->size(), 1);
    ASSERT_EQ(results->get(0)->getName(), newerName);

    deleteFile(fileName);
}


/**
 * @brief TEST_F SearchNodesByModificationTime
 *
 * Test filtering nodes by mtime in search() and getChildren()
 *
 */
TEST_F(SdkTest, SearchNodesByModificationTime)
{
    LOG_info << "___TEST SearchNodesByModificationTime___";

    /// /
    ///     TestMTime_Folder.Foo/
    ///         TestMTime_File1.bar
    ///         TestMTime_File2.bar
    ///     TestMTime_FileAtRoot.bar
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    // TestCTime_Folder.Foo
    unique_ptr<MegaNode> rootnode(megaApi[0]->getRootNode());
    ASSERT_TRUE(rootnode);

    string folderName = "TestMTime_Folder.Foo";
    MegaHandle folderHandle = createFolder(0, folderName.c_str(), rootnode.get());
    ASSERT_NE(folderHandle, INVALID_HANDLE);
    unique_ptr<MegaNode> folderNode(megaApi[0]->getNodeByHandle(folderHandle));
    ASSERT_TRUE(folderNode);

    // TestCTime_Folder.Foo / TestMTime_File1.bar
    string fileName1 = "TestMTime_File1.bar";
    ASSERT_TRUE(createFile(fileName1, false));

    MegaHandle fileHandle1 = 0;
    ASSERT_EQ(MegaError::API_OK, doStartUpload(0, &fileHandle1, fileName1.c_str(), folderNode.get(),
                                               nullptr /*fileName*/,
                                               ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
                                               nullptr /*appData*/,
                                               false   /*isSourceTemporary*/,
                                               false   /*startFirst*/,
                                               nullptr /*cancelToken*/)) << "Cannot upload " << fileName1;

    ASSERT_NE(fileHandle1, INVALID_HANDLE);
    unique_ptr<MegaNode> fileNode1(megaApi[0]->getNodeByHandle(fileHandle1));
    ASSERT_TRUE(fileNode1);
    int64_t fileMTime1Old = fileNode1->getModificationTime();
    ASSERT_NE(fileMTime1Old, 0) << "Invalid modification time for file " << fileName1;

    std::this_thread::sleep_for(std::chrono::milliseconds{1500}); // avoid nodes having identical MTime
    // modify file
    {
    ofstream f(fileName1);
    f << "update ";
    f.close();
    }

    ASSERT_EQ(MegaError::API_OK, doStartUpload(0, &fileHandle1, fileName1.c_str(), folderNode.get(),
                                               nullptr /*fileName*/,
                                               ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
                                               nullptr /*appData*/,
                                               false   /*isSourceTemporary*/,
                                               false   /*startFirst*/,
                                               nullptr /*cancelToken*/)) << "Cannot upload modified " << fileName1;
    ASSERT_NE(fileHandle1, INVALID_HANDLE);
    fileNode1.reset(megaApi[0]->getNodeByHandle(fileHandle1));
    ASSERT_TRUE(fileNode1);
    int64_t fileMTime1 = fileNode1->getModificationTime();
    ASSERT_NE(fileMTime1, 0) << "Invalid modification time after update for file " << fileName1;
    ASSERT_NE(fileMTime1Old, fileMTime1) << "Test file has the same Modification time after being updated";

    std::this_thread::sleep_for(std::chrono::milliseconds{1500}); // avoid nodes having identical MTime
    // modify file
    {
    ofstream f(fileName1);
    f << "update ";
    f.close();
    }

    // TestCTime_Folder.Foo / TestMTime_File2.bar
    string fileName2 = "TestMTime_File2.bar";
    MegaHandle fileHandle2 = 0;
    ASSERT_EQ(MegaError::API_OK, doStartUpload(0, &fileHandle2, fileName1.c_str(), folderNode.get(),
                                               fileName2.c_str() /*fileName*/,
                                               ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
                                               nullptr /*appData*/,
                                               false   /*isSourceTemporary*/,
                                               false   /*startFirst*/,
                                               nullptr /*cancelToken*/)) << "Cannot upload " << fileName2;

    ASSERT_NE(fileHandle2, INVALID_HANDLE);
    unique_ptr<MegaNode> fileNode2(megaApi[0]->getNodeByHandle(fileHandle2));
    ASSERT_TRUE(fileNode2);
    int64_t fileMTime2 = fileNode2->getModificationTime();
    ASSERT_NE(fileMTime2, 0) << "Invalid modification time for file 2 " << fileName2;

    std::this_thread::sleep_for(std::chrono::milliseconds{2200}); // avoid nodes having identical MTime
    // modify file
    {
    ofstream f(fileName1);
    f << "update ";
    f.close();
    }

    // TestMTime_FileAtRoot.bar
    string fileNameAtRoot = "TestMTime_FileAtRoot.bar";
    MegaHandle fileHandleAtRoot = 0;
    ASSERT_EQ(MegaError::API_OK, doStartUpload(0, &fileHandleAtRoot, fileName1.c_str(), rootnode.get(),
                                               fileNameAtRoot.c_str() /*fileName*/,
                                               ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
                                               nullptr /*appData*/,
                                               false   /*isSourceTemporary*/,
                                               false   /*startFirst*/,
                                               nullptr /*cancelToken*/)) << "Cannot upload " << fileName1 << " at root";

    ASSERT_NE(fileHandleAtRoot, INVALID_HANDLE);
    unique_ptr<MegaNode> fileNodeAtRoot(megaApi[0]->getNodeByHandle(fileHandleAtRoot));
    ASSERT_TRUE(fileNodeAtRoot);
    int64_t fileMTimeR = fileNodeAtRoot->getModificationTime();
    ASSERT_NE(fileMTimeR, 0) << "Invalid modification time for file at root " << fileName1;

    // getChildren()
    unique_ptr<MegaSearchFilter> f(MegaSearchFilter::createInstance());
    f->byName("TestMTime_*");
    f->byLocationHandle(rootnode->getHandle());
    unique_ptr<MegaNodeList> results(megaApi[0]->getChildren(f.get(), MegaApi::ORDER_DEFAULT_ASC));
    ASSERT_EQ(results->size(), 2);
    ASSERT_EQ(results->get(0)->getName(), folderName); // folders come first
    ASSERT_EQ(results->get(1)->getName(), fileNameAtRoot);

    f->byModificationTime(fileMTimeR - 1, 0);
    results.reset(megaApi[0]->getChildren(f.get()));
    ASSERT_EQ(results->size(), 1);
    ASSERT_EQ(results->get(0)->getName(), fileNameAtRoot);

    f->byLocationHandle(folderHandle);
    f->byModificationTime(fileMTime1 - 1, 0);
    results.reset(megaApi[0]->getChildren(f.get(), MegaApi::ORDER_DEFAULT_ASC));
    ASSERT_EQ(results->size(), 2);
    ASSERT_EQ(results->get(0)->getName(), fileName1);
    ASSERT_EQ(results->get(1)->getName(), fileName2);

    f->byModificationTime(0, fileMTime2);
    results.reset(megaApi[0]->getChildren(f.get()));
    ASSERT_EQ(results->size(), 1);
    ASSERT_EQ(results->get(0)->getName(), fileName1);

    f->byModificationTime(fileMTime1, fileMTime1);
    results.reset(megaApi[0]->getChildren(f.get()));
    ASSERT_EQ(results->size(), 0) << "Found " << results->get(0)->getName();

    f->byModificationTime(fileMTime1 - 1, fileMTime2 + 1);
    results.reset(megaApi[0]->getChildren(f.get(), MegaApi::ORDER_DEFAULT_ASC));
    ASSERT_EQ(results->size(), 2);
    ASSERT_EQ(results->get(0)->getName(), fileName1);
    ASSERT_EQ(results->get(1)->getName(), fileName2);

    // getChildren(), repeat last using pagination
    unique_ptr<MegaSearchPage> p(MegaSearchPage::createInstance(0, 1));
    results.reset(megaApi[0]->getChildren(f.get(), MegaApi::ORDER_DEFAULT_ASC, nullptr, p.get()));
    ASSERT_EQ(results->size(), 1);
    ASSERT_EQ(results->get(0)->getName(), fileName1);

    p.reset(MegaSearchPage::createInstance(0, 2));
    results.reset(megaApi[0]->getChildren(f.get(), MegaApi::ORDER_DEFAULT_ASC, nullptr, p.get()));
    ASSERT_EQ(results->size(), 2);
    ASSERT_EQ(results->get(0)->getName(), fileName1);
    ASSERT_EQ(results->get(1)->getName(), fileName2);

    p.reset(MegaSearchPage::createInstance(0, 3));
    results.reset(megaApi[0]->getChildren(f.get(), MegaApi::ORDER_DEFAULT_ASC, nullptr, p.get()));
    ASSERT_EQ(results->size(), 2);
    ASSERT_EQ(results->get(0)->getName(), fileName1);
    ASSERT_EQ(results->get(1)->getName(), fileName2);

    p.reset(MegaSearchPage::createInstance(1, 1));
    results.reset(megaApi[0]->getChildren(f.get(), MegaApi::ORDER_DEFAULT_ASC, nullptr, p.get()));
    ASSERT_EQ(results->size(), 1);
    ASSERT_EQ(results->get(0)->getName(), fileName2);

    p.reset(MegaSearchPage::createInstance(1, 2));
    results.reset(megaApi[0]->getChildren(f.get(), MegaApi::ORDER_DEFAULT_ASC, nullptr, p.get()));
    ASSERT_EQ(results->size(), 1);
    ASSERT_EQ(results->get(0)->getName(), fileName2);

    p.reset(MegaSearchPage::createInstance(2, 14));
    results.reset(megaApi[0]->getChildren(f.get(), MegaApi::ORDER_DEFAULT_ASC, nullptr, p.get()));
    ASSERT_EQ(results->size(), 0);

    // search()
    f->byLocationHandle(INVALID_HANDLE);
    f->byModificationTime(fileMTime1 - 100, fileMTimeR + 1);
    results.reset(megaApi[0]->search(f.get(), MegaApi::ORDER_DEFAULT_ASC));
    ASSERT_EQ(results->size(), 3);
    ASSERT_EQ(results->get(0)->getName(), fileName1);
    ASSERT_EQ(results->get(1)->getName(), fileName2);
    ASSERT_EQ(results->get(2)->getName(), fileNameAtRoot);

    f->byModificationTime(fileMTimeR, fileMTimeR);
    results.reset(megaApi[0]->search(f.get()));
    ASSERT_EQ(results->size(), 0);

    f->byModificationTime(fileMTimeR - 1, fileMTimeR + 1);
    results.reset(megaApi[0]->search(f.get()));
    ASSERT_EQ(results->size(), 1);
    ASSERT_EQ(results->get(0)->getName(), fileNameAtRoot);

    f->byModificationTime(0, fileMTime2);
    results.reset(megaApi[0]->search(f.get()));
    ASSERT_EQ(results->size(), 1);
    ASSERT_EQ(results->get(0)->getName(), fileName1);

    f->byModificationTime(fileMTime1, 0);
    results.reset(megaApi[0]->search(f.get(), MegaApi::ORDER_DEFAULT_ASC));
    ASSERT_EQ(results->size(), 2);
    ASSERT_EQ(results->get(0)->getName(), fileName2);
    ASSERT_EQ(results->get(1)->getName(), fileNameAtRoot);

    // search(), repeat last using pagination
    p.reset(MegaSearchPage::createInstance(0, 1));
    results.reset(megaApi[0]->search(f.get(), MegaApi::ORDER_DEFAULT_ASC, nullptr, p.get()));
    ASSERT_EQ(results->size(), 1);
    ASSERT_EQ(results->get(0)->getName(), fileName2);

    p.reset(MegaSearchPage::createInstance(0, 2));
    results.reset(megaApi[0]->search(f.get(), MegaApi::ORDER_DEFAULT_ASC, nullptr, p.get()));
    ASSERT_EQ(results->size(), 2);
    ASSERT_EQ(results->get(0)->getName(), fileName2);
    ASSERT_EQ(results->get(1)->getName(), fileNameAtRoot);

    p.reset(MegaSearchPage::createInstance(0, 3));
    results.reset(megaApi[0]->search(f.get(), MegaApi::ORDER_DEFAULT_ASC, nullptr, p.get()));
    ASSERT_EQ(results->size(), 2);
    ASSERT_EQ(results->get(0)->getName(), fileName2);
    ASSERT_EQ(results->get(1)->getName(), fileNameAtRoot);

    p.reset(MegaSearchPage::createInstance(1, 1));
    results.reset(megaApi[0]->search(f.get(), MegaApi::ORDER_DEFAULT_ASC, nullptr, p.get()));
    ASSERT_EQ(results->size(), 1);
    ASSERT_EQ(results->get(0)->getName(), fileNameAtRoot);

    p.reset(MegaSearchPage::createInstance(1, 2));
    results.reset(megaApi[0]->search(f.get(), MegaApi::ORDER_DEFAULT_ASC, nullptr, p.get()));
    ASSERT_EQ(results->size(), 1);
    ASSERT_EQ(results->get(0)->getName(), fileNameAtRoot);

    p.reset(MegaSearchPage::createInstance(2, 14));
    results.reset(megaApi[0]->search(f.get(), MegaApi::ORDER_DEFAULT_ASC, nullptr, p.get()));
    ASSERT_EQ(results->size(), 0);

    deleteFile(fileName1);
}


/**
 * @brief TEST_F SearchNodesByNodeType
 *
 * Test filtering nodes by node type in search() and getChildren()
 *
 */
TEST_F(SdkTest, SearchNodesByNodeType)
{
    LOG_info << "___TEST SearchNodesByNodeType___";

    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    unique_ptr<MegaNode> rootnode(megaApi[0]->getRootNode());
    ASSERT_TRUE(rootnode);

    string folderName = "SearchByNodeType_Folder.Foo";
    MegaHandle folderHandle = createFolder(0, folderName.c_str(), rootnode.get());
    ASSERT_NE(folderHandle, INVALID_HANDLE);
    unique_ptr<MegaNode> folderNode(megaApi[0]->getNodeByHandle(folderHandle));
    ASSERT_TRUE(folderNode);

    string fileName = "SearchByNodeType_File.bar";
    ASSERT_TRUE(createFile(fileName, false));
    MegaHandle fileHandle1 = 0;
    ASSERT_EQ(MegaError::API_OK, doStartUpload(0, &fileHandle1, fileName.c_str(), rootnode.get(),
                                               nullptr /*fileName*/,
                                               ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
                                               nullptr /*appData*/,
                                               false   /*isSourceTemporary*/,
                                               false   /*startFirst*/,
                                               nullptr /*cancelToken*/)) << "Cannot upload " << fileName << " to root";
    ASSERT_NE(fileHandle1, INVALID_HANDLE);
    unique_ptr<MegaNode> fileNode1(megaApi[0]->getNodeByHandle(fileHandle1));
    ASSERT_TRUE(fileNode1);

    MegaHandle fileHandle2 = 0;
    ASSERT_EQ(MegaError::API_OK, doStartUpload(0, &fileHandle2, fileName.c_str(), folderNode.get(),
                                               nullptr /*fileName*/,
                                               ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
                                               nullptr /*appData*/,
                                               false   /*isSourceTemporary*/,
                                               false   /*startFirst*/,
                                               nullptr /*cancelToken*/)) << "Cannot upload " << fileName << " to " << folderName;
    ASSERT_NE(fileHandle2, INVALID_HANDLE);
    unique_ptr<MegaNode> fileNode2(megaApi[0]->getNodeByHandle(fileHandle2));
    ASSERT_TRUE(fileNode2);

    // getChildren()
    unique_ptr<MegaSearchFilter> f(MegaSearchFilter::createInstance());
    f->byName("SearchByNodeType_*");
    f->byLocationHandle(rootnode->getHandle());
    unique_ptr<MegaNodeList> results(megaApi[0]->getChildren(f.get()));
    ASSERT_EQ(results->size(), 2);

    f->byNodeType(MegaNode::TYPE_FOLDER);
    results.reset(megaApi[0]->getChildren(f.get()));
    ASSERT_EQ(results->size(), 1);
    ASSERT_EQ(results->get(0)->getName(), folderName);

    f->byNodeType(MegaNode::TYPE_FILE);
    results.reset(megaApi[0]->getChildren(f.get()));
    ASSERT_EQ(results->size(), 1);
    ASSERT_EQ(results->get(0)->getHandle(), fileHandle1);

    f->byLocationHandle(folderHandle);
    f->byNodeType(MegaNode::TYPE_UNKNOWN);
    results.reset(megaApi[0]->getChildren(f.get()));
    ASSERT_EQ(results->size(), 1);
    ASSERT_EQ(results->get(0)->getHandle(), fileHandle2);

    f->byNodeType(MegaNode::TYPE_FOLDER);
    results.reset(megaApi[0]->getChildren(f.get()));
    ASSERT_EQ(results->size(), 0);

    f->byNodeType(MegaNode::TYPE_FILE);
    results.reset(megaApi[0]->getChildren(f.get()));
    ASSERT_EQ(results->size(), 1);
    ASSERT_EQ(results->get(0)->getHandle(), fileHandle2);

    // search()
    f->byLocationHandle(INVALID_HANDLE);
    f->byNodeType(MegaNode::TYPE_UNKNOWN);
    results.reset(megaApi[0]->search(f.get()));
    ASSERT_EQ(results->size(), 3);

    f->byNodeType(MegaNode::TYPE_FOLDER);
    results.reset(megaApi[0]->search(f.get()));
    ASSERT_EQ(results->size(), 1);
    ASSERT_EQ(results->get(0)->getName(), folderName);

    f->byNodeType(MegaNode::TYPE_FILE);
    results.reset(megaApi[0]->search(f.get()));
    ASSERT_EQ(results->size(), 2);
    ASSERT_EQ(results->get(0)->getName(), fileName);
    ASSERT_EQ(results->get(1)->getName(), fileName);

    f->byLocationHandle(folderHandle);
    f->byNodeType(MegaNode::TYPE_UNKNOWN);
    results.reset(megaApi[0]->search(f.get()));
    ASSERT_EQ(results->size(), 1);
    ASSERT_EQ(results->get(0)->getHandle(), fileHandle2);

    f->byNodeType(MegaNode::TYPE_FOLDER);
    results.reset(megaApi[0]->search(f.get()));
    ASSERT_EQ(results->size(), 0);

    f->byNodeType(MegaNode::TYPE_FILE);
    results.reset(megaApi[0]->search(f.get()));
    ASSERT_EQ(results->size(), 1);
    ASSERT_EQ(results->get(0)->getHandle(), fileHandle2);

    deleteFile(fileName);
}


/**
 * ___SdkNodesOnDemand___
 * Steps:
 *  - Configure variables to set Account2 data equal to Account1
 *  - login in both clients
 *  - Client1 creates tree directory with 2 levels and some files at last level
 *  - Check Folder info of root node from client 1 and client 2
 *  - Look for fingerprint and name in both clients
 *  - Locallogout from client 1
 *  - Client 2 remove a node
 *  - Client 2 check if node is present by fingerprint
 *  - Client 1 login with session
 *  - Check nodes by fingerprint
 *  - Check folder info of root node from client 1
 *  - Check if we recover children correctly
 *  - Remove a folder with some files
 *  - Check Folder info of root node from client 1 and client 2
 *  - Move a folder to rubbish bin
 *  - Check Folder info for root node and rubbish bin
 *  - Locallogout and login from client 1
 *  - Check nodes by fingerprint without nodes in RAM
 */
TEST_F(SdkTest, SdkNodesOnDemand)
{
    LOG_info << "___TEST SdkNodesOnDemand___";

    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));
    // --- Load User B as account 1
    const auto [email, pass] = getEnvVarAccounts().getVarValues(0);
    ASSERT_FALSE(email.empty() || pass.empty());
    mApi.resize(2);
    megaApi.resize(2);
    configureTestInstance(1, email, pass); // index 1 = User B
    auto loginTracker = std::make_unique<RequestTracker>(megaApi[1].get());
    megaApi[1]->login(email.c_str(), pass.c_str(), loginTracker.get());
    ASSERT_EQ(API_OK, loginTracker->waitForResult()) << " Failed to login to account " << email;
    ASSERT_NO_FATAL_FAILURE(fetchnodes(1));

    unique_ptr<MegaNode> rootnodeA(megaApi[0]->getRootNode());
    ASSERT_TRUE(rootnodeA);

    unique_ptr<MegaNode> rootnodeB(megaApi[1]->getRootNode());
    ASSERT_TRUE(rootnodeB);
    ASSERT_EQ(rootnodeA->getHandle(), rootnodeB->getHandle());

    ASSERT_EQ(MegaError::API_OK, synchronousFolderInfo(0, rootnodeA.get())) << "Cannot get Folder Info";
    std::unique_ptr<MegaFolderInfo> initialFolderInfo1(mApi[0].mFolderInfo->copy());

    ASSERT_EQ(MegaError::API_OK, synchronousFolderInfo(1, rootnodeB.get())) << "Cannot get Folder Info";
    std::unique_ptr<MegaFolderInfo> initialFolderInfo2(mApi[1].mFolderInfo->copy());

    ASSERT_EQ(initialFolderInfo1->getNumFiles(), initialFolderInfo2->getNumFiles());
    ASSERT_EQ(initialFolderInfo1->getNumFolders(), initialFolderInfo2->getNumFolders());
    ASSERT_EQ(initialFolderInfo1->getCurrentSize(), initialFolderInfo2->getCurrentSize());
    ASSERT_EQ(initialFolderInfo1->getNumVersions(), initialFolderInfo2->getNumVersions());
    ASSERT_EQ(initialFolderInfo1->getVersionsSize(), initialFolderInfo2->getVersionsSize());

    // --- UserA Create tree directory ---
    // 3 Folders in level 1
    // 4 Folders in level 2 for every folder from level 1
    // 5 files in every folders from level 2
    std::string folderLevel1 = "Folder";
    int numberFolderLevel1 = 3;
    std::string folderLevel2 = "SubFolder";
    int numberFolderLevel2 = 4;
    std::string fileName = "File";
    int numberFiles = 5;
    std::string fileNameToSearch;
    std::string fingerPrintToSearch;
    std::string fingerPrintToRemove;
    MegaHandle nodeHandle = INVALID_HANDLE;
    MegaHandle parentHandle = INVALID_HANDLE;
    std::set<MegaHandle> childrenHandles;
    MegaHandle nodeToRemove = INVALID_HANDLE;
    int indexFolderToMove = 0;
    MegaHandle handleFolderToMove = INVALID_HANDLE;
    int64_t accountSize = 0;
    bool check1, check2;
    mApi[1].mOnNodesUpdateCompletion = createOnNodesUpdateLambda(INVALID_HANDLE, MegaNode::CHANGE_TYPE_NEW, check1);

    for (int i = 0; i < numberFolderLevel1; i++)
    {
        check1 = false;
        std::string folderName = folderLevel1 + "_" + std::to_string(i);
        auto nodeFirstLevel = createFolder(0, folderName.c_str(), rootnodeA.get());
        ASSERT_NE(nodeFirstLevel, UNDEF);
        unique_ptr<MegaNode> folderFirstLevel(megaApi[0]->getNodeByHandle(nodeFirstLevel));
        ASSERT_TRUE(folderFirstLevel);
        waitForResponse(&check1); // Wait until receive nodes updated at client 2

        // Save handle from folder that it's going to move to rubbish bin
        if (i == indexFolderToMove)
        {
            handleFolderToMove = nodeFirstLevel;
        }

        for (int j = 0; j < numberFolderLevel2; j++)
        {
            check1 = false;
            std::string subFolder = folderLevel2 +"_" + std::to_string(i) + "_" + std::to_string(j);
            auto nodeSecondLevel = createFolder(0, subFolder.c_str(), folderFirstLevel.get());
            ASSERT_NE(nodeSecondLevel, UNDEF);
            unique_ptr<MegaNode> subFolderSecondLevel(megaApi[0]->getNodeByHandle(nodeSecondLevel));
            ASSERT_TRUE(subFolderSecondLevel);
            waitForResponse(&check1); // Wait until receive nodes updated at client 2

            // Save handle from folder that it's going to be request children
            if (j == numberFolderLevel2 - 2)
            {
               parentHandle = subFolderSecondLevel->getHandle();
            }

            // Save handle from folder that it's going to be removed
            if (j == numberFolderLevel2 - 3)
            {
               nodeToRemove = subFolderSecondLevel->getHandle();
            }

            for (int k = 0; k < numberFiles; k++)
            {
                check1 = false;
                string filename2 = fileName + "_" + std::to_string(i) + "_" + std::to_string(j) + "_" + std::to_string(k);
                string content = "test_" + std::to_string(i) + "_" + std::to_string(j) + "_" + std::to_string(k);
                createFile(filename2, false, content);
                MegaHandle mh = 0;
                ASSERT_EQ(MegaError::API_OK, doStartUpload(0, &mh, filename2.data(), subFolderSecondLevel.get(),
                                                           nullptr /*fileName*/,
                                                           ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
                                                           nullptr /*appData*/,
                                                           false   /*isSourceTemporary*/,
                                                           false   /*startFirst*/,
                                                           nullptr /*cancelToken*/)) << "Cannot upload a test file";

                unique_ptr<MegaNode> nodeFile(megaApi[0]->getNodeByHandle(mh));
                ASSERT_NE(nodeFile, nullptr) << "Cannot initialize second node for scenario (error: " << mApi[0].lastError << ")";
                waitForResponse(&check1); // Wait until receive nodes updated at client 2

                // Save fingerprint, name and handle for a file
                if (i == (numberFolderLevel1 - 1) && j == (numberFolderLevel2 - 1) && k == (numberFiles -1))
                {
                    fileNameToSearch = nodeFile->getName();
                    fingerPrintToSearch = nodeFile->getFingerprint();
                    nodeHandle = nodeFile->getHandle();
                }

                if (i == (numberFolderLevel1 - 1) && j == (numberFolderLevel2 - 1) && k == (numberFiles - 2))
                {
                    fingerPrintToRemove = nodeFile->getFingerprint();
                }

                // Save children handle from a folder
                if (j == numberFolderLevel2 - 2)
                {
                    childrenHandles.insert(nodeFile->getHandle());
                }

                accountSize += nodeFile->getSize();

                deleteFile(filename2);
            }
        }
    }

    // important to reset
    resetOnNodeUpdateCompletionCBs();

    accountSize += initialFolderInfo1->getCurrentSize();

    ASSERT_NE(nodeToRemove, INVALID_HANDLE) << "nodeToRemove is not set";
    ASSERT_NE(handleFolderToMove, INVALID_HANDLE) << "folderToMove is not set";

    // --- UserA and UserB check number of files
    std::unique_ptr<MegaNode> parent(megaApi[0]->getNodeByHandle(parentHandle));
    ASSERT_NE(parent.get(), nullptr);
    ASSERT_EQ(numberFiles, megaApi[0]->getNumChildFiles(parent.get()));

    parent.reset(megaApi[1]->getNodeByHandle(parentHandle));
    ASSERT_NE(parent.get(), nullptr);
    ASSERT_EQ(numberFiles, megaApi[1]->getNumChildFiles(parent.get()));

    // --- UserA and UserB check number of folders
    ASSERT_EQ(numberFolderLevel1, megaApi[0]->getNumChildFolders(rootnodeA.get()));
    ASSERT_EQ(numberFolderLevel1, megaApi[1]->getNumChildFolders(rootnodeB.get()));

    std::unique_ptr<MegaSearchFilter> filterResults(MegaSearchFilter::createInstance());
    filterResults->byLocationHandle(rootnodeA->getHandle());
    filterResults->byNodeType(MegaNode::TYPE_FOLDER);
    std::unique_ptr<MegaNodeList> rootChildrenList(megaApi[0]->getChildren(filterResults.get()));
    ASSERT_EQ(rootChildrenList->size(), numberFolderLevel1);

    // --- UserA Check folder info from root node ---
    ASSERT_EQ(MegaError::API_OK, synchronousFolderInfo(0, rootnodeA.get())) << "Cannot get Folder Info";
    int numberTotalOfFiles = numberFolderLevel1 * numberFolderLevel2 * numberFiles + initialFolderInfo1->getNumFiles();
    ASSERT_EQ(mApi[0].mFolderInfo->getNumFiles(), numberTotalOfFiles) << "Incorrect number of Files";
    int numberTotalOfFolders = numberFolderLevel1 * numberFolderLevel2 + numberFolderLevel1 + initialFolderInfo1->getNumFolders();
    ASSERT_EQ(mApi[0].mFolderInfo->getNumFolders(), numberTotalOfFolders) << "Incorrect number of Folders";
    ASSERT_EQ(mApi[0].mFolderInfo->getCurrentSize(), accountSize) << "Incorrect account Size";

    // --- UserB Check folder info from root node ---
    ASSERT_EQ(MegaError::API_OK, synchronousFolderInfo(1, rootnodeB.get())) << "Cannot get Folder Info";
    ASSERT_EQ(mApi[1].mFolderInfo->getNumFiles(), numberTotalOfFiles) << "Incorrect number of Files";
    ASSERT_EQ(mApi[1].mFolderInfo->getNumFolders(), numberTotalOfFolders) << "Incorrect number of Folders";
    ASSERT_EQ(mApi[1].mFolderInfo->getCurrentSize(), accountSize) << "Incorrect account Size";

    // --- UserA get node by fingerprint ---
    unique_ptr<MegaNodeList> fingerPrintList(megaApi[0]->getNodesByFingerprint(fingerPrintToSearch.c_str()));
    ASSERT_NE(fingerPrintList->size(), 0);
    bool found = false;
    for (int i = 0; i < fingerPrintList->size(); i++)
    {
        if (fingerPrintList->get(i)->getHandle() == nodeHandle)
        {
            found = true;
            break;
        }
    }

    ASSERT_TRUE(found);

    // --- UserA get node by fingerprint (loaded in RAM) ---
    unique_ptr<MegaNode> nodeSameFingerPrint(megaApi[0]->getNodeByFingerprint(fingerPrintToSearch.c_str()));
    ASSERT_NE(nodeSameFingerPrint.get(), nullptr);

    // --- UserA get node by name ---
    filterResults.reset(MegaSearchFilter::createInstance());
    filterResults->byName(fileNameToSearch.c_str());
    std::unique_ptr<MegaNodeList> searchList(megaApi[0]->search(filterResults.get()));
    ASSERT_EQ(searchList->size(), 1);
    ASSERT_EQ(searchList->get(0)->getHandle(), nodeHandle);

    // --- UserB get node by fingerprint ---
    fingerPrintList.reset(megaApi[1]->getNodesByFingerprint(fingerPrintToSearch.c_str()));
    ASSERT_NE(fingerPrintList->size(), 0);
    found = false;
    for (int i = 0; i < fingerPrintList->size(); i++)
    {
        if (fingerPrintList->get(i)->getHandle() == nodeHandle)
        {
            found = true;
            break;
        }
    }

    ASSERT_TRUE(found);

    // --- UserB get node by name ---
    filterResults.reset(MegaSearchFilter::createInstance());
    filterResults->byName(fileNameToSearch.c_str());
    searchList.reset(megaApi[1]->search(filterResults.get()));
    ASSERT_EQ(searchList->size(), 1);
    ASSERT_EQ(searchList->get(0)->getHandle(), nodeHandle);

    // --- UserA logout
    std::unique_ptr<char[]> session(megaApi[0]->dumpSession());
    ASSERT_NO_FATAL_FAILURE(locallogout());

    // --- UserB remove a node and try to find it by fingerprint
    check1 = false;
    ASSERT_GT(fingerPrintToRemove.size(), 0u);
    fingerPrintList.reset(megaApi[1]->getNodesByFingerprint(fingerPrintToRemove.c_str()));
    int nodesWithFingerPrint = fingerPrintList->size(); // Number of nodes with same fingerprint
    ASSERT_GT(nodesWithFingerPrint, 0);
    MegaHandle handleFingerprintRemove = fingerPrintList->get(0)->getHandle();
    mApi[1].mOnNodesUpdateCompletion = createOnNodesUpdateLambda(handleFingerprintRemove, MegaNode::CHANGE_TYPE_REMOVED, check1);
    unique_ptr<MegaNode>node(megaApi[1]->getNodeByHandle(handleFingerprintRemove));
    ASSERT_EQ(API_OK, synchronousRemove(1, node.get()));
    waitForResponse(&check1); // Wait until receive nodes updated at client 2
    nodesWithFingerPrint--; // Decrease the number of nodes with same fingerprint
    fingerPrintList.reset(megaApi[1]->getNodesByFingerprint(fingerPrintToRemove.c_str()));
    ASSERT_EQ(fingerPrintList->size(), nodesWithFingerPrint);
    // important to reset
    resetOnNodeUpdateCompletionCBs();

    numberTotalOfFiles--;
    accountSize -= node->getSize();

    PerApi& target = mApi[0];
    target.resetlastEvent();   // clear any previous EVENT_NODES_CURRENT

    // --- UserA login with session
    ASSERT_NO_FATAL_FAILURE(resumeSession(session.get()));
    ASSERT_NO_FATAL_FAILURE(fetchnodes(0));

    // make sure that client is up to date (upon logout, recent changes might not be committed to DB)
    ASSERT_TRUE(WaitFor([&target](){ return target.lastEventsContain(MegaEvent::EVENT_NODES_CURRENT); }, 10000))
        << "Timeout expired to receive actionpackets";

    // --- UserA Check if find removed node by fingerprint
    fingerPrintList.reset(megaApi[0]->getNodesByFingerprint(fingerPrintToRemove.c_str()));
    ASSERT_EQ(fingerPrintList->size(), nodesWithFingerPrint);

    // --- UserA Check folder info from root node ---
    rootnodeA.reset(megaApi[0]->getRootNode());
    ASSERT_TRUE(rootnodeA);
    ASSERT_EQ(MegaError::API_OK, synchronousFolderInfo(0, rootnodeA.get())) << "Cannot get Folder Info";
    ASSERT_EQ(mApi[0].mFolderInfo->getNumFiles(), numberTotalOfFiles) << "Incorrect number of Files";
    ASSERT_EQ(mApi[0].mFolderInfo->getNumFolders(), numberTotalOfFolders) << "Incorrect number of Folders";
    ASSERT_EQ(mApi[0].mFolderInfo->getCurrentSize(), accountSize) << "Incorrect account Size";

    // --- UserA get node by fingerprint (Without nodes in RAM) ---
    nodeSameFingerPrint.reset(megaApi[0]->getNodeByFingerprint(fingerPrintToSearch.c_str()));
    ASSERT_NE(nodeSameFingerPrint.get(), nullptr);

    // --- UserA get nodes by fingerprint, some of them are loaded in RAM
    fingerPrintList.reset(megaApi[0]->getNodesByFingerprint(fingerPrintToSearch.c_str()));
    ASSERT_NE(fingerPrintList->size(), 0);
    found = false;
    for (int i = 0; i < fingerPrintList->size(); i++)
    {
        if (fingerPrintList->get(i)->getHandle() == nodeHandle)
        {
            found = true;
            break;
        }
    }

    ASSERT_TRUE(found);


    // --- UserA get nodes by fingerprint, all of them are loaded in RAM
    fingerPrintList.reset(megaApi[0]->getNodesByFingerprint(fingerPrintToSearch.c_str()));
    ASSERT_NE(fingerPrintList->size(), 0);
    found = false;
    for (int i = 0; i < fingerPrintList->size(); i++)
    {
        if (fingerPrintList->get(i)->getHandle() == nodeHandle)
        {
            found = true;
            break;
        }
    }

    ASSERT_TRUE(found);


    // --- UserA check children ---
    if (parentHandle != INVALID_HANDLE)  // Get children
    {
        unique_ptr<MegaNode>node(megaApi[0]->getNodeByHandle(parentHandle));
        ASSERT_NE(node, nullptr);
        std::unique_ptr<MegaNodeList> childrenList(megaApi[0]->getChildren(node.get()));
        ASSERT_GT(childrenList->size(), 0);
        for (int childIndex = 0; childIndex < childrenList->size(); childIndex++)
        {
            ASSERT_NE(childrenHandles.find(childrenList->get(childIndex)->getHandle()), childrenHandles.end());
        }

        filterResults.reset(MegaSearchFilter::createInstance());
        filterResults->byLocationHandle(node->getHandle());
        filterResults->byNodeType(MegaNode::TYPE_FILE);
        std::unique_ptr<MegaNodeList> fileChildrenList(
            megaApi[0]->getChildren(filterResults.get()));
        ASSERT_EQ(fileChildrenList->size(), childrenList->size());

        filterResults.reset(MegaSearchFilter::createInstance());
        filterResults->byLocationHandle(node->getHandle());
        filterResults->byNodeType(MegaNode::TYPE_FOLDER);
        std::unique_ptr<MegaNodeList> folderChildrenList(
            megaApi[0]->getChildren(filterResults.get()));
        ASSERT_EQ(folderChildrenList->size(), 0);
    }

    // --- UserA remove a folder ---
    mApi[0].mOnNodesUpdateCompletion = createOnNodesUpdateLambda(nodeToRemove, MegaNode::CHANGE_TYPE_REMOVED, check1);
    mApi[1].mOnNodesUpdateCompletion = createOnNodesUpdateLambda(nodeToRemove, MegaNode::CHANGE_TYPE_REMOVED, check2);
    node.reset(megaApi[0]->getNodeByHandle(nodeToRemove));
    ASSERT_NE(node, nullptr);
    ASSERT_EQ(MegaError::API_OK, synchronousFolderInfo(0, node.get())) << "Cannot get Folder Info";
    std::unique_ptr<MegaFolderInfo> removedFolder(mApi[0].mFolderInfo->copy());
    ASSERT_EQ(API_OK, synchronousRemove(0, node.get()));
    node.reset(megaApi[0]->getNodeByHandle(nodeToRemove));
    ASSERT_EQ(node, nullptr);


    waitForResponse(&check1); // Wait until receive nodes updated at client 1

    // --- UserA Check folder info from root node ---
    ASSERT_EQ(MegaError::API_OK, synchronousFolderInfo(0, rootnodeA.get())) << "Cannot get Folder Info";
    ASSERT_EQ(mApi[0].mFolderInfo->getNumFiles(), numberTotalOfFiles - removedFolder->getNumFiles()) << "Incorrect number of Files";
    ASSERT_EQ(mApi[0].mFolderInfo->getNumFolders(),
              numberTotalOfFolders - (removedFolder->getNumFolders() + 1))
        << "Incorrect number of Folders";
    ASSERT_EQ(mApi[0].mFolderInfo->getCurrentSize(), accountSize - removedFolder->getCurrentSize()) << "Incorrect account Size";

    waitForResponse(&check2); // Wait until receive nodes updated at client 2

    // important to reset
    resetOnNodeUpdateCompletionCBs();
    ASSERT_EQ(check1, true);
    ASSERT_EQ(check2, true);

    // --- UserB Check folder info from root node ---
    ASSERT_EQ(MegaError::API_OK, synchronousFolderInfo(1, rootnodeB.get())) << "Cannot get Folder Info";
    ASSERT_EQ(mApi[1].mFolderInfo->getNumFiles(), numberTotalOfFiles - removedFolder->getNumFiles()) << "Incorrect number of Files";
    ASSERT_EQ(mApi[1].mFolderInfo->getNumFolders(),
              numberTotalOfFolders - (removedFolder->getNumFolders() + 1))
        << "Incorrect number of Folders";
    ASSERT_EQ(mApi[1].mFolderInfo->getCurrentSize(), accountSize - removedFolder->getCurrentSize()) << "Incorrect account Size";

    unique_ptr<MegaNode> nodeToMove(megaApi[0]->getNodeByHandle(handleFolderToMove));
    ASSERT_EQ(MegaError::API_OK, synchronousFolderInfo(0, nodeToMove.get())) << "Cannot get Folder Info from node to Move";
    std::unique_ptr<MegaFolderInfo> movedFolder(mApi[0].mFolderInfo->copy());

    unique_ptr<MegaNode> rubbishBinA(megaApi[1]->getRubbishNode());
    ASSERT_TRUE(rubbishBinA);

    mApi[0].mOnNodesUpdateCompletion = createOnNodesUpdateLambda(handleFolderToMove, MegaNode::CHANGE_TYPE_PARENT, check1);
    mApi[1].mOnNodesUpdateCompletion = createOnNodesUpdateLambda(handleFolderToMove, MegaNode::CHANGE_TYPE_PARENT, check2);
    mApi[0].requestFlags[MegaRequest::TYPE_MOVE] = false;
    megaApi[0]->moveNode(nodeToMove.get(), rubbishBinA.get());
    ASSERT_TRUE( waitForResponse(&mApi[0].requestFlags[MegaRequest::TYPE_MOVE]) )
            << "Move operation failed after " << maxTimeout << " seconds";
    ASSERT_EQ(MegaError::API_OK, mApi[0].lastError) << "Cannot move node (error: " << mApi[0].lastError << ")";
    waitForResponse(&check1); // Wait until receive nodes updated at client 1
    waitForResponse(&check2); // Wait until receive nodes updated at client 2
    // important to reset
    resetOnNodeUpdateCompletionCBs();
    ASSERT_EQ(check1, true);
    ASSERT_EQ(check2, true);


    // --- UserA Check folder info from root node ---
    ASSERT_EQ(MegaError::API_OK, synchronousFolderInfo(0, rootnodeA.get())) << "Cannot get Folder Info";
    ASSERT_EQ(mApi[0].mFolderInfo->getNumFiles(), numberTotalOfFiles - removedFolder->getNumFiles() - movedFolder->getNumFiles()) << "Incorrect number of Files";
    ASSERT_EQ(mApi[0].mFolderInfo->getNumFolders(),
              numberTotalOfFolders - (removedFolder->getNumFolders() + 1) -
                  (movedFolder->getNumFolders() + 1))
        << "Incorrect number of Folders";
    ASSERT_EQ(mApi[0].mFolderInfo->getCurrentSize(), accountSize - removedFolder->getCurrentSize() - movedFolder->getCurrentSize()) << "Incorrect account Size";

    // --- UserB Check folder info from root node ---
    ASSERT_EQ(MegaError::API_OK, synchronousFolderInfo(1, rootnodeB.get())) << "Cannot get Folder Info";
    ASSERT_EQ(mApi[1].mFolderInfo->getNumFiles(), numberTotalOfFiles - removedFolder->getNumFiles() - movedFolder->getNumFiles()) << "Incorrect number of Files";
    ASSERT_EQ(mApi[1].mFolderInfo->getNumFolders(),
              numberTotalOfFolders - (removedFolder->getNumFolders() + 1) -
                  (movedFolder->getNumFolders() + 1))
        << "Incorrect number of Folders";
    ASSERT_EQ(mApi[1].mFolderInfo->getCurrentSize(), accountSize - removedFolder->getCurrentSize() - movedFolder->getCurrentSize()) << "Incorrect account Size";

    // --- UserA Check folder info from rubbish node ---
    ASSERT_EQ(MegaError::API_OK, synchronousFolderInfo(0, rubbishBinA.get())) << "Cannot get Folder Info";
    ASSERT_EQ(mApi[0].mFolderInfo->getNumFiles(), movedFolder->getNumFiles()) << "Incorrect number of Files";
    ASSERT_EQ(mApi[0].mFolderInfo->getNumFolders(), movedFolder->getNumFolders() + 1)
        << "Incorrect number of Folders";
    ASSERT_EQ(mApi[0].mFolderInfo->getCurrentSize(), movedFolder->getCurrentSize()) << "Incorrect account Size";

    // --- UserB Check folder info from rubbish node ---
    unique_ptr<MegaNode> rubbishBinB(megaApi[1]->getRubbishNode());
    ASSERT_TRUE(rubbishBinB);
    ASSERT_EQ(MegaError::API_OK, synchronousFolderInfo(1, rubbishBinB.get())) << "Cannot get Folder Info";
    ASSERT_EQ(mApi[1].mFolderInfo->getNumFiles(), movedFolder->getNumFiles()) << "Incorrect number of Files";
    ASSERT_EQ(mApi[1].mFolderInfo->getNumFolders(), movedFolder->getNumFolders() + 1)
        << "Incorrect number of Folders";
    ASSERT_EQ(mApi[1].mFolderInfo->getCurrentSize(), movedFolder->getCurrentSize()) << "Incorrect account Size";

    ASSERT_NO_FATAL_FAILURE(locallogout());
    // --- UserA login with session
    target.resetlastEvent();   // clear any previous EVENT_NODES_CURRENT
    ASSERT_NO_FATAL_FAILURE(resumeSession(session.get()));
    ASSERT_NO_FATAL_FAILURE(fetchnodes(0));

    // make sure that client is up to date (upon logout, recent changes might not be committed to DB)
    ASSERT_TRUE(WaitFor([&target](){ return target.lastEventsContain(MegaEvent::EVENT_NODES_CURRENT); }, 10000))
        << "Timeout expired to receive actionpackets";

    // --- UserA get nodes by fingerprint, none of them are loaded in RAM
    fingerPrintList.reset(megaApi[0]->getNodesByFingerprint(fingerPrintToSearch.c_str()));
    ASSERT_NE(fingerPrintList->size(), 0);
    found = false;
    for (int i = 0; i < fingerPrintList->size(); i++)
    {
        if (fingerPrintList->get(i)->getHandle() == nodeHandle)
        {
            found = true;
            break;
        }
    }

    ASSERT_TRUE(found);
}

/**
 * SdkNodesOnDemandVersions
 * Steps:
 *  - Configure variables to set Account2 data equal to Account1
 *  - login in both clients
 *  - Client 1 File and after add a modification of that file (version)
 *  - Check Folder info of root node from client 1 and client 2
 */
TEST_F(SdkTest, SdkNodesOnDemandVersions)
{
    LOG_info << "___TEST SdkNodesOnDemandVersions";

    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));
    // --- Load User B as account 1
    const auto [email, pass] = getEnvVarAccounts().getVarValues(0);
    ASSERT_FALSE(email.empty() || pass.empty());
    mApi.resize(2);
    megaApi.resize(2);
    configureTestInstance(1, email, pass); // index 1 = User B
    auto loginTracker = std::make_unique<RequestTracker>(megaApi[1].get());
    megaApi[1]->login(email.c_str(), pass.c_str(), loginTracker.get());
    ASSERT_EQ(API_OK, loginTracker->waitForResult()) << " Failed to login to account " << email;
    ASSERT_NO_FATAL_FAILURE(fetchnodes(1));

    unique_ptr<MegaNode> rootnodeA(megaApi[0]->getRootNode());
    ASSERT_TRUE(rootnodeA);
    unique_ptr<MegaNode> rootnodeB(megaApi[1]->getRootNode());
    ASSERT_TRUE(rootnodeB);
    ASSERT_EQ(rootnodeA->getHandle(), rootnodeB->getHandle());

    std::string fileName = "file";
    bool check1, check2;
    mApi[0].mOnNodesUpdateCompletion = createOnNodesUpdateLambda(INVALID_HANDLE, MegaNode::CHANGE_TYPE_NEW, check1);
    mApi[1].mOnNodesUpdateCompletion = createOnNodesUpdateLambda(INVALID_HANDLE, MegaNode::CHANGE_TYPE_NEW, check2);
    string content1 = "test_1";
    createFile(fileName, false, content1);
    MegaHandle fh = 0;
    ASSERT_EQ(MegaError::API_OK, doStartUpload(0, &fh, fileName.data(), rootnodeA.get(),
                                               nullptr /*fileName*/,
                                               ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
                                               nullptr /*appData*/,
                                               false   /*isSourceTemporary*/,
                                               false   /*startFirst*/,
                                               nullptr /*cancelToken*/)) << "Cannot upload a test file";
    unique_ptr<MegaNode> nodeFile(megaApi[0]->getNodeByHandle(fh));
    synchronousSetNodeSensitive(0, nodeFile.get(), true);
    synchronousSetNodeFavourite(0, nodeFile.get(), true);

    ASSERT_NE(nodeFile, nullptr) << "Cannot initialize second node for scenario (error: " << mApi[0].lastError << ")";
    long long size1 = nodeFile->getSize();
    waitForResponse(&check1); // Wait until receive nodes updated at client 1
    waitForResponse(&check2); // Wait until receive nodes updated at client 2
    ASSERT_EQ(check1, true);
    ASSERT_EQ(check2, true);
    deleteFile(fileName);
    // important to reset
    resetOnNodeUpdateCompletionCBs();

    // check no versions exist yet in either client
    {
        unique_ptr<MegaNode> n1(megaApi[0]->getNodeByPath(("/" + fileName).c_str()));
        unique_ptr<MegaNode> n2(megaApi[1]->getNodeByPath(("/" + fileName).c_str()));
        ASSERT_TRUE(n1 && !megaApi[0]->hasVersions(n1.get()));
        ASSERT_TRUE(n2 && !megaApi[1]->hasVersions(n2.get()));
        ASSERT_TRUE(n1 && 1 == megaApi[0]->getNumVersions(n1.get()));
        ASSERT_TRUE(n2 && 1 == megaApi[1]->getNumVersions(n2.get()));
    }

    // upload a file to replace the last one in the root of client 0
    // of course client 1 will see the same new file (and the old file becomes a version, if versioning is on.  Built with ENABLE_SYNC or not is irrelevant)
    mApi[0].mOnNodesUpdateCompletion = createOnNodesUpdateLambda(INVALID_HANDLE, MegaNode::CHANGE_TYPE_NEW, check1);
    mApi[1].mOnNodesUpdateCompletion = createOnNodesUpdateLambda(INVALID_HANDLE, MegaNode::CHANGE_TYPE_NEW, check2);

    string content2 = "test_2";
    createFile(fileName, false, content2);
    fh = 0;
    ASSERT_EQ(MegaError::API_OK, doStartUpload(0, &fh, fileName.data(), rootnodeA.get(),
                                               nullptr /*fileName*/,
                                               ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
                                               nullptr /*appData*/,
                                               false   /*isSourceTemporary*/,
                                               false   /*startFirst*/,
                                               nullptr /*cancelToken*/)) << "Cannot upload a test file";

    nodeFile.reset(megaApi[0]->getNodeByHandle(fh));
    long long size2 = nodeFile->getSize();
    ASSERT_NE(nodeFile, nullptr) << "Cannot initialize second node for scenario (error: " << mApi[0].lastError << ")";
    waitForResponse(&check1); // Wait until receive nodes updated at client 1
    waitForResponse(&check2); // Wait until receive nodes updated at client 2
    ASSERT_EQ(check1, true);
    ASSERT_EQ(check2, true);

    deleteFile(fileName);
    // important to reset
    resetOnNodeUpdateCompletionCBs();

    // check both client now know the file has versons
    {
        unique_ptr<MegaNode> n1(megaApi[0]->getNodeByPath(("/" + fileName).c_str()));
        unique_ptr<MegaNode> n2(megaApi[1]->getNodeByPath(("/" + fileName).c_str()));
        ASSERT_TRUE(n1 && megaApi[0]->hasVersions(n1.get()));
        ASSERT_TRUE(n2 && megaApi[1]->hasVersions(n2.get()));
        ASSERT_TRUE(n1 && 2 == megaApi[0]->getNumVersions(n1.get()));
        ASSERT_TRUE(n2 && 2 == megaApi[1]->getNumVersions(n2.get()));
    }

    nodeFile.reset(megaApi[0]->getNodeByHandle(fh));
    unique_ptr<MegaNodeList> list(megaApi[0]->getChildren(nodeFile.get())); // null
    unique_ptr<MegaNodeList> vlist(megaApi[0]->getVersions(nodeFile.get()));
    MegaNode *n0 = vlist->get(0);
    MegaNode* n1 = vlist->get(1);
    ASSERT_TRUE(n0->isFavourite());
    ASSERT_TRUE(n0->isMarkedSensitive());
    ASSERT_TRUE(n1->isFavourite());
    ASSERT_TRUE(n1->isMarkedSensitive());

    ASSERT_EQ(MegaError::API_OK, synchronousFolderInfo(0, rootnodeA.get())) << "Cannot get Folder Info";
    std::unique_ptr<MegaFolderInfo> initialFolderInfo1(mApi[0].mFolderInfo->copy());
    ASSERT_EQ(initialFolderInfo1->getNumFiles(), 1);
    ASSERT_EQ(initialFolderInfo1->getNumFolders(), 0);
    ASSERT_EQ(initialFolderInfo1->getNumVersions(), 1);
    ASSERT_EQ(initialFolderInfo1->getCurrentSize(), size2);
    ASSERT_EQ(initialFolderInfo1->getVersionsSize(), size1);

    ASSERT_EQ(MegaError::API_OK, synchronousFolderInfo(1, rootnodeB.get())) << "Cannot get Folder Info";
    std::unique_ptr<MegaFolderInfo> initialFolderInfo2(mApi[1].mFolderInfo->copy());

    ASSERT_EQ(initialFolderInfo1->getNumFiles(), initialFolderInfo2->getNumFiles());
    ASSERT_EQ(initialFolderInfo1->getNumFolders(), initialFolderInfo2->getNumFolders());
    ASSERT_EQ(initialFolderInfo1->getNumVersions(), initialFolderInfo2->getNumVersions());
    ASSERT_EQ(initialFolderInfo1->getCurrentSize(), initialFolderInfo2->getCurrentSize());
    ASSERT_EQ(initialFolderInfo1->getVersionsSize(), initialFolderInfo2->getVersionsSize());
}

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
    //  3. Upload test files
    //  4. Add Element
    //  5. Update Element order
    //  6. Update Element name
    //  7. Add an element with an already added node (-12 expected)
    //  8. Remove Element
    //  9. Add/remove bulk elements
    // 10. Logout / login
    // 11. Remove all Sets

    // Use another connection with the same credentials
    megaApi.emplace_back(newMegaApi(APP_KEY.c_str(), megaApiCacheFolder(1).c_str(), USER_AGENT.c_str(), unsigned(THREADS_PER_MEGACLIENT)));
    auto& differentApi = *megaApi.back();
    differentApi.addListener(this);
    PerApi pa; // make a copy
    pa.email = mApi.back().email;
    pa.pwd = mApi.back().pwd;
    mApi.push_back(std::move(pa));
    auto& differentApiDtls = mApi.back();
    differentApiDtls.megaApi = &differentApi;
    int differentApiIdx = int(megaApi.size() - 1);

    auto loginTracker = asyncRequestLogin(static_cast<unsigned>(differentApiIdx),
                                          differentApiDtls.email.c_str(),
                                          differentApiDtls.pwd.c_str());
    ASSERT_EQ(API_OK, loginTracker->waitForResult()) << " Failed to establish a login/session for account " << differentApiIdx;
    loginTracker = asyncRequestFetchnodes(static_cast<unsigned>(differentApiIdx));
    ASSERT_EQ(API_OK, loginTracker->waitForResult()) << " Failed to fetch nodes for account " << differentApiIdx;

    // 1. Create Set
    string name = u8"Set name ideograms: 讓我們打破這個"; // "讓我們打破這個"
    differentApiDtls.setUpdated = false;
    MegaSet* newSet = nullptr;
    int err = doCreateSet(0, &newSet, name.c_str(), MegaSet::SET_TYPE_ALBUM);
    ASSERT_EQ(err, API_OK);

    unique_ptr<MegaSet> s1p(newSet);
    ASSERT_NE(s1p, nullptr);
    ASSERT_NE(s1p->id(), INVALID_HANDLE);
    ASSERT_EQ(s1p->name(), name);
    ASSERT_NE(s1p->ts(), 0);
    ASSERT_NE(s1p->cts(), 0) << "Create-timestamp of a Set was not set";
    ASSERT_NE(s1p->user(), INVALID_HANDLE);
    MegaHandle sh = s1p->id();
    int64_t setCrTs = s1p->cts();

    // test action packets
    ASSERT_TRUE(waitForResponse(&differentApiDtls.setUpdated)) << "Set create AP not received after " << maxTimeout << " seconds";
    unique_ptr<MegaSet> s2p(differentApi.getSet(sh));
    ASSERT_NE(s2p, nullptr);
    ASSERT_EQ(s2p->id(), s1p->id());
    ASSERT_EQ(s2p->name(), name);
    ASSERT_EQ(s2p->ts(), s1p->ts());
    ASSERT_EQ(s2p->cts(), s1p->cts()) << "Create-timestamp of a Set differed in Action Packet";
    ASSERT_EQ(s2p->user(), s1p->user());

    // Clear Set name
    differentApiDtls.setUpdated = false;
    err = doUpdateSetName(0, nullptr, sh, "");
    ASSERT_EQ(err, API_OK);
    unique_ptr<MegaSet> s1clearname(megaApi[0]->getSet(sh));
    ASSERT_NE(s1clearname, nullptr);
    ASSERT_STREQ(s1clearname->name(), "");
    ASSERT_EQ(s1clearname->cts(), setCrTs) << "Create-timestamp of a Set has changed after name change";
    // test action packets
    ASSERT_TRUE(waitForResponse(&differentApiDtls.setUpdated)) << "Set update AP not received after " << maxTimeout << " seconds";
    s2p.reset(differentApi.getSet(sh));
    ASSERT_NE(s2p, nullptr);
    ASSERT_STREQ(s2p->name(), "");
    ASSERT_EQ(s2p->cts(), setCrTs) << "Create-timestamp of a Set has changed after name change AP";

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
    ASSERT_EQ(s2p->cts(), s1up->cts());

    // 3. Upload test files
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
    string filename2 = UPFILE + "2";
    ASSERT_TRUE(createFile(filename2, false)) << "Couldn't create " << filename2;
    MegaHandle uploadedNode2 = INVALID_HANDLE;
    ASSERT_EQ(MegaError::API_OK, doStartUpload(0, &uploadedNode2, filename2.c_str(),
        rootnode.get(),
        nullptr /*fileName*/,
        ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
        nullptr /*appData*/,
        false   /*isSourceTemporary*/,
        false   /*startFirst*/,
        nullptr /*cancelToken*/)) << "Could not upload test file " << filename2;

    // 4. Add Element
    string elattrs = u8"Element name emoji: 📞🎉❤️"; // "📞🎉❤️"
    differentApiDtls.setElementUpdated = false;
    MegaSetElementList* newElls = nullptr;
    err = doCreateSetElement(0, &newElls, sh, uploadedNode, elattrs.c_str());
    ASSERT_EQ(err, API_OK);

    unique_ptr<MegaSetElementList> els(newElls);
    ASSERT_NE(els, nullptr);
    ASSERT_EQ(els->size(), 1u);
    ASSERT_EQ(els->get(0)->node(), uploadedNode);
    ASSERT_EQ(els->get(0)->setId(), sh);
    ASSERT_EQ(els->get(0)->name(), elattrs);
    ASSERT_NE(els->get(0)->ts(), 0);
    ASSERT_EQ(els->get(0)->order(), 1000);
    MegaHandle eh = els->get(0)->id();
    unique_ptr<MegaSetElement> elp(megaApi[0]->getSetElement(sh, eh));
    ASSERT_NE(elp, nullptr);
    ASSERT_EQ(elp->id(), eh);
    ASSERT_EQ(elp->node(), uploadedNode);
    ASSERT_EQ(elp->setId(), sh);
    ASSERT_EQ(elp->name(), elattrs);
    ASSERT_NE(elp->ts(), 0);
    ASSERT_EQ(elp->order(), 1000); // first default value, according to specs
    unsigned elCount = megaApi[0]->getSetElementCount(sh);
    ASSERT_EQ(elCount, 1u);

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
    ASSERT_EQ(elp2->setId(), elp->setId());
    ASSERT_EQ(elp2->name(), elattrs);
    ASSERT_EQ(elp2->ts(), elp->ts());
    ASSERT_EQ(elp2->order(), elp->order());
    elCount = differentApi.getSetElementCount(sh);
    ASSERT_EQ(elCount, 1u);

    // Move element's file to Rubbish Bin
    std::unique_ptr<MegaNode> elementNode(megaApi[0]->getNodeByHandle(uploadedNode));
    ASSERT_TRUE(elementNode) << "File node of Element not found";
    std::unique_ptr<MegaNode> rubbishNode(megaApi[0]->getRubbishNode());
    ASSERT_TRUE(rubbishNode) << "Rubbish Bin node not found";
    ASSERT_EQ(API_OK, doMoveNode(0, nullptr, elementNode.get(), rubbishNode.get())) << "Couldn't move node to Rubbish Bin";
    els2.reset(megaApi[0]->getSetElements(sh));
    ASSERT_EQ(els2->size(), 1u) << "Wrong all Element-s, including Rubbish Bin (1 file moved to Rubbish)";
    elCount = megaApi[0]->getSetElementCount(sh);
    ASSERT_EQ(elCount, 1u) << "Wrong Element count, including Rubbish Bin (1 file moved to Rubbish)";
    els2.reset(megaApi[0]->getSetElements(sh, false));
    ASSERT_EQ(els2->size(), 0u) << "Wrong all Element-s, excluding Rubbish Bin (1 file moved to Rubbish)";
    elCount = megaApi[0]->getSetElementCount(sh, false);
    ASSERT_EQ(elCount, 0u) << "Wrong Element count, excluding Rubbish Bin (1 file moved to Rubbish)";

    // Restore Element's file from Rubbish Bin
    ASSERT_EQ(API_OK, doMoveNode(0, nullptr, elementNode.get(), rootnode.get())) << "Couldn't restore node from Rubbish Bin";
    els2.reset(megaApi[0]->getSetElements(sh));
    ASSERT_EQ(els2->size(), 1u) << "Wrong all Element-s, including Rubbish Bin (no files in Rubbish)";
    elCount = megaApi[0]->getSetElementCount(sh);
    ASSERT_EQ(elCount, 1u) << "Wrong Element count, including Rubbish Bin (no files in Rubbish)";
    els2.reset(megaApi[0]->getSetElements(sh, false));
    ASSERT_EQ(els2->size(), 1u) << "Wrong all Element-s, excluding Rubbish Bin (no files in Rubbish)";
    elCount = megaApi[0]->getSetElementCount(sh, false);
    ASSERT_EQ(elCount, 1u) << "Wrong Element count, excluding Rubbish Bin (no files in Rubbish)";

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

    // 5. Update Element order
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
    ASSERT_EQ(elu1p->setId(), sh);
    ASSERT_STREQ(elu1p->name(), "");
    ASSERT_EQ(elu1p->order(), order);
    ASSERT_NE(elu1p->ts(), 0);

    // test action packets
    ASSERT_TRUE(waitForResponse(&differentApiDtls.setElementUpdated)) << "Element order change AP not received after " << maxTimeout << " seconds";
    elp2.reset(differentApi.getSetElement(sh, eh));
    ASSERT_NE(elp2, nullptr);
    ASSERT_STREQ(elp2->name(), "");
    ASSERT_EQ(elp2->order(), elu1p->order());

    // 6. Update Element name
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

    // 7. Add an element with an already added node (-12 expected)
    string elattrs1b = u8"Another element name emoji: 📞🎉❤️"; // "📞🎉❤️"
    newElls = nullptr;
    err = doCreateSetElement(0, &newElls, sh, uploadedNode, elattrs1b.c_str());
    ASSERT_EQ(err, API_EEXIST) << "Adding another SetElement with the same node as already existing SetElement";

    elCount = megaApi[0]->getSetElementCount(sh);
    ASSERT_EQ(elCount, 1u);

    // 8. Remove Element
    differentApiDtls.setElementUpdated = false;
    err = doRemoveSetElement(0, sh, eh);
    ASSERT_EQ(err, API_OK);
    elCount = megaApi[0]->getSetElementCount(sh);
    ASSERT_EQ(elCount, 0u);

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

    // 9. Add/remove bulk elements
    // Add 3; only the first will succeed
    differentApiDtls.setElementUpdated = false;
    string elattrs2 = elattrs + u8" bulk2";
    elattrs += u8" bulk1";
    newElls = nullptr;
    MegaIntegerList* newElErrs = nullptr;
    unique_ptr<MegaHandleList> newElFileHandles(MegaHandleList::createInstance());
    newElFileHandles->addMegaHandle(uploadedNode);
    newElFileHandles->addMegaHandle(INVALID_HANDLE);
    newElFileHandles->addMegaHandle(uploadedNode);
    unique_ptr<MegaStringList> newElNames(MegaStringList::createInstance());
    newElNames->add(elattrs.c_str());
    newElNames->add(elattrs2.c_str());
    newElNames->add(elattrs.c_str());
    err = doCreateBulkSetElements(0, &newElls, &newElErrs, sh, newElFileHandles.get(), newElNames.get());
    els.reset(newElls);
    unique_ptr<MegaIntegerList> elErrs(newElErrs);
    ASSERT_EQ(err, API_OK);
    ASSERT_NE(newElls, nullptr);
    ASSERT_EQ(newElls->size(), 1u);
    ASSERT_EQ(newElls->get(0)->name(), elattrs);
    eh = newElls->get(0)->id();
    ASSERT_NE(eh, INVALID_HANDLE);
    ASSERT_NE(newElErrs, nullptr);
    ASSERT_EQ(newElErrs->size(), 3);
    ASSERT_EQ(newElErrs->get(0), API_OK);
    ASSERT_EQ(newElErrs->get(1), API_EARGS); // API_EARGS because sending an empty key error takes precedence over sending INVALID_HANDLE for eid error (API_ENOENT)
    ASSERT_EQ(newElErrs->get(2), API_EEXIST);
    unique_ptr<MegaSetElement> newEl(megaApi[0]->getSetElement(sh, eh));
    ASSERT_NE(newEl, nullptr);
    ASSERT_EQ(newEl->id(), eh);
    ASSERT_EQ(newEl->name(), elattrs);
    // test action packets
    ASSERT_TRUE(waitForResponse(&differentApiDtls.setElementUpdated)) << "Element add AP not received after " << maxTimeout << " seconds";

    // Remove 2, only the first one will succeed
    differentApiDtls.setElementUpdated = false;
    unique_ptr<MegaHandleList> removedElIds(MegaHandleList::createInstance());
    removedElIds->addMegaHandle(eh);
    removedElIds->addMegaHandle(INVALID_HANDLE);
    MegaIntegerList* removedElErrs = nullptr;
    err = doRemoveBulkSetElements(0, &removedElErrs, sh, removedElIds.get());
    elErrs.reset(removedElErrs);
    ASSERT_EQ(err, API_OK);
    ASSERT_NE(removedElErrs, nullptr);
    ASSERT_EQ(removedElErrs->size(), 2);
    ASSERT_EQ(removedElErrs->get(0), API_OK);
    ASSERT_EQ(removedElErrs->get(1), API_ENOENT);

    // test action packets
    ASSERT_TRUE(waitForResponse(&differentApiDtls.setElementUpdated)) << "Element remove AP not received after " << maxTimeout << " seconds";

    // Add 2 more; both will succeed
    differentApiDtls.setElementUpdated = false;
    newElls = nullptr;
    newElErrs = nullptr;
    newElFileHandles.reset(MegaHandleList::createInstance());
    newElFileHandles->addMegaHandle(uploadedNode);
    newElFileHandles->addMegaHandle(uploadedNode2);
    newElNames.reset(MegaStringList::createInstance());
    string namebulk11 = elattrs + "1";
    newElNames->add(namebulk11.c_str());
    string namebulk12 = elattrs + "2";
    newElNames->add(namebulk12.c_str());
    err = doCreateBulkSetElements(0, &newElls, &newElErrs, sh, newElFileHandles.get(), newElNames.get());
    els.reset(newElls);
    ASSERT_EQ(err, API_OK);
    ASSERT_NE(newElls, nullptr);
    ASSERT_EQ(newElls->size(), 2u);
    ASSERT_EQ(newElls->get(1)->name(), namebulk12);
    MegaHandle ehBulk = newElls->get(1)->id();
    ASSERT_NE(ehBulk, INVALID_HANDLE);
    const MegaSetElement* elp_b4lo = newElls->get(0);
    ASSERT_NE(elp_b4lo, nullptr);
    ASSERT_EQ(elp_b4lo->name(), namebulk11);
    ehBulk = elp_b4lo->id();
    ASSERT_NE(ehBulk, INVALID_HANDLE);
    ASSERT_NE(newElErrs, nullptr);
    ASSERT_EQ(newElErrs->size(), 2);
    ASSERT_EQ(newElErrs->get(0), API_OK);
    ASSERT_EQ(newElErrs->get(1), API_OK);
    elCount = megaApi[0]->getSetElementCount(sh);
    ASSERT_EQ(elCount, 2u);
    // test action packets
    ASSERT_TRUE(waitForResponse(&differentApiDtls.setElementUpdated)) << "Element add AP not received after " << maxTimeout << " seconds";

    // create a dummy folder, just to trigger a local db commit before locallogout (which triggers a ROLLBACK)
    PerApi& target = mApi[0];
    target.resetlastEvent();
    MegaHandle hDummyFolder = createFolder(0, "DummyFolder_TriggerDbCommit", rootnode.get());
    ASSERT_NE(hDummyFolder, INVALID_HANDLE);
    ASSERT_TRUE(WaitFor([&target]() { return target.lastEventsContain(MegaEvent::EVENT_COMMIT_DB); }, 8192));

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
    ASSERT_EQ(s1p->cts(), s1up->cts());
    ASSERT_EQ(s1p->name(), name);
    elCount = megaApi[0]->getSetElementCount(sh);
    ASSERT_EQ(elCount, 2u) << "Wrong Element count after resumeSession";

    unique_ptr<MegaSetElement> ellp(megaApi[0]->getSetElement(sh, ehBulk));
    ASSERT_NE(ellp, nullptr);
    ASSERT_EQ(ellp->id(), elp_b4lo->id());
    ASSERT_EQ(ellp->node(), elp_b4lo->node());
    ASSERT_EQ(ellp->setId(), elp_b4lo->setId());
    ASSERT_EQ(ellp->ts(), elp_b4lo->ts());
    ASSERT_EQ(ellp->name(), namebulk11);

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
 * @brief TEST_F SdkTestSetsAndElementsPublicLink
 *
 * Tests creating, modifying and removing Sets and Elements.
 */
TEST_F(SdkTest, SdkTestSetsAndElementsPublicLink)
{
    LOG_info << "___TEST Sets and Elements Public Link___";

    // U1: Create set
    // U1: Upload test file
    // U1: Add Element to Set
    // U1: Check if Set is exported
    // U1: Enable Set export (creates public link)
    // U1: Check if Set is exported
    // U1: Update Set name and verify Set is still exported
    // U1: Logout / login to retrieve Set
    // U1: Check if Set is exported
    // U1: Get public Set URL
    // U1: Fetch Public Set and start Public Set preview mode
    // U1: Stop Public Set preview mode
    // U2: Attempt to fetch public Set using wrong key
    // U2: Fetch public Set and start preview mode
    // U2: Download foreign Set Element in preview set mode
    // U2: Stop Public Set preview mode
    // U2: Download foreign Set Element not in preview set mode (-11 and -9 expected)
    // U1: Disable Set export (invalidates public link)
    // U1: Get public Set URL (-9 expected)
    // U1: Sync fetch public Set on non-exported Set (using previously valid link), nullptr expected
    // U1: Remove all Sets

    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(2));

    // Use another connection with the same credentials as U1
    MegaApi* differentApiPtr = nullptr;
    PerApi* differentApiDtlsPtr = nullptr;
    unsigned userIdx = 0;
    megaApi.emplace_back(newMegaApi(APP_KEY.c_str(),
                                    megaApiCacheFolder(static_cast<int>(userIdx)).c_str(),
                                    USER_AGENT.c_str(),
                                    unsigned(THREADS_PER_MEGACLIENT)));
    differentApiPtr = &(*megaApi.back());
    differentApiPtr->addListener(this);
    PerApi pa; // make a copy
    auto& aux = mApi[userIdx];
    pa.email = aux.email;
    pa.pwd = aux.pwd;
    mApi.push_back(std::move(pa));
    differentApiDtlsPtr = &(mApi.back());
    differentApiDtlsPtr->megaApi = differentApiPtr;
    unsigned difApiIdx = static_cast<unsigned>(megaApi.size() - 1);

    auto loginTracker = asyncRequestLogin(difApiIdx,
                                          differentApiDtlsPtr->email.c_str(),
                                          differentApiDtlsPtr->pwd.c_str());
    ASSERT_EQ(API_OK, loginTracker->waitForResult()) << " Failed to establish a login/session for account " << difApiIdx;
    loginTracker = asyncRequestFetchnodes(difApiIdx);
    ASSERT_EQ(API_OK, loginTracker->waitForResult()) << " Failed to fetch nodes for account " << difApiIdx;


    LOG_debug << "# U1: Create set";
    const string name = u8"qq-001";
    MegaSet* newSet = nullptr;
    differentApiDtlsPtr->setUpdated = false;
    ASSERT_EQ(API_OK, doCreateSet(0, &newSet, name.c_str(), MegaSet::SET_TYPE_ALBUM));
    ASSERT_NE(newSet, nullptr);
    const unique_ptr<MegaSet> s1p(newSet);
    const MegaHandle sh = s1p->id();
    ASSERT_TRUE(waitForResponse(&differentApiDtlsPtr->setUpdated))
        << "Failed to receive create Set AP on U1's secondary client";


    LOG_debug << "# U1: Upload test file";
    userIdx = 0;
    unique_ptr<MegaNode> rootnode{megaApi[userIdx]->getRootNode()};
    ASSERT_TRUE(createFile(UPFILE, false)) << "Couldn't create " << UPFILE;
    MegaHandle uploadedNode = INVALID_HANDLE;
    ASSERT_EQ(API_OK,
              doStartUpload(userIdx,
                            &uploadedNode,
                            UPFILE.c_str(),
                            rootnode.get(),
                            nullptr /*fileName*/,
                            ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
                            nullptr /*appData*/,
                            false /*isSourceTemporary*/,
                            false /*startFirst*/,
                            nullptr /*cancelToken*/))
        << "Cannot upload a test file";

    LOG_debug << "# U1: Add Element to Set";
    userIdx = 0;
    differentApiDtlsPtr->setElementUpdated = false;
    const string elattrs = u8"Element name emoji: 🐧";
    MegaSetElementList* newEll = nullptr;
    ASSERT_EQ(API_OK, doCreateSetElement(userIdx, &newEll, sh, uploadedNode, elattrs.c_str()));
    ASSERT_NE(newEll, nullptr);
    const unique_ptr<MegaSetElementList> els(newEll);
    const MegaHandle eh = els->get(0)->id();
    const unique_ptr<MegaSetElement> elp(megaApi[userIdx]->getSetElement(sh, eh));
    ASSERT_NE(elp, nullptr);
    ASSERT_TRUE(waitForResponse(&differentApiDtlsPtr->setElementUpdated))
        << "Failed to receive add Element update AP on U1's secondary";


    LOG_debug << "# U1: Check if Set is exported";
    ASSERT_FALSE(megaApi[0]->isExportedSet(sh)) << "Set should not be public yet";


    LOG_debug << "# U1: Enable Set export (creates public link)";
    userIdx = 0;
    ASSERT_FALSE(megaApi[userIdx]->isExportedSet(sh));
    MegaSet* exportedSet = nullptr;
    string exportedSetURL;
    differentApiDtlsPtr->setUpdated = false;
    ASSERT_EQ(API_OK, doExportSet(userIdx, &exportedSet, exportedSetURL, sh));
    bool isExpectedToBeExported = true;
    ASSERT_FALSE(exportedSetURL.empty());
    unique_ptr<MegaSet> s1pEnabledExport(exportedSet);
    LOG_debug << "\tChecking Set from export request";
    const auto lIsSameSet = [&s1p](const MegaSet* s, bool isExported)
    {
        ASSERT_NE(s, nullptr);
        ASSERT_EQ(s1p->id(), s->id());
        ASSERT_STREQ(s1p->name(), s->name());
        ASSERT_EQ(isExported, s->isExported());
        ASSERT_NE(s->ts(), 0);
    };
    ASSERT_NO_FATAL_FAILURE(lIsSameSet(s1pEnabledExport.get(), isExpectedToBeExported));
    s1pEnabledExport.reset(megaApi[userIdx]->getSet(sh));
    LOG_debug << "\tChecking Set from MegaApi::getSet";
    ASSERT_NO_FATAL_FAILURE(lIsSameSet(s1pEnabledExport.get(), isExpectedToBeExported));
    // test action packets
    ASSERT_TRUE(waitForResponse(&differentApiDtlsPtr->setUpdated))
        << "Failed to receive export Set update AP on U1's secondary client";
    s1pEnabledExport.reset(differentApiPtr->getSet(sh));
    LOG_debug << "\tChecking Set from MegaApi::getSet for differentApi (AKA U1 in a different client)";
    ASSERT_NO_FATAL_FAILURE(lIsSameSet(s1pEnabledExport.get(), isExpectedToBeExported));
    // test shortcut
    LOG_debug << "\tChecking export enable shortcut";
    exportedSet = nullptr;
    ASSERT_EQ(API_OK, doExportSet(userIdx, &exportedSet, exportedSetURL, sh));
    s1pEnabledExport.reset(exportedSet);
    ASSERT_NO_FATAL_FAILURE(lIsSameSet(s1pEnabledExport.get(), isExpectedToBeExported));


    LOG_debug << "# U1: Check if Set is exported";
    ASSERT_TRUE(megaApi[0]->isExportedSet(sh)) << "Set should already be public";


    LOG_debug << "# U1: Update Set name and verify Set is still exported";
    userIdx = 0;
    differentApiDtlsPtr->setUpdated = false;
    const string updatedName = name + u8" 手";
    ASSERT_EQ(API_OK, doUpdateSetName(userIdx, nullptr, sh, updatedName.c_str()));
    ASSERT_TRUE(waitForResponse(&differentApiDtlsPtr->setUpdated))
        << "Failed to receive shared Set name updated AP on U1's secondary client";
    ASSERT_TRUE(megaApi[userIdx]->isExportedSet(sh))
        << "Set should still be public after the update";
    // reset to previous name to keep using existing original cached Set for validation
    differentApiDtlsPtr->setUpdated = false;
    PerApi& target = mApi[userIdx];
    target.resetlastEvent();     // So we can detect when the node database has been committed.
    ASSERT_EQ(API_OK, doUpdateSetName(userIdx, nullptr, sh, name.c_str()));
    ASSERT_TRUE(waitForResponse(&differentApiDtlsPtr->setUpdated))
        << "Failed to receive shared Set name reset updated AP on U1's secondary client";
    // Wait for the database to be updated (note: even if commit is triggered by 1st update, the 2nd update
    // has been applied already, so the DB will store the final value)
    ASSERT_TRUE(WaitFor([&target](){ return target.lastEventsContain(MegaEvent::EVENT_COMMIT_DB); }, maxTimeout*1000))
        << "Failed to receive commit to DB event related to Set name update";


    LOG_debug << "# U1: Logout / login to retrieve Set";
    userIdx = 0;
    isExpectedToBeExported = true;
    unique_ptr<char[]> session(dumpSession());
    ASSERT_NO_FATAL_FAILURE(locallogout());
    ASSERT_NO_FATAL_FAILURE(resumeSession(session.get()));
    ASSERT_NO_FATAL_FAILURE(fetchnodes(userIdx)); // load cached Sets

    unique_ptr<MegaSet> reloadedSessionSet(megaApi[userIdx]->getSet(sh));
    ASSERT_NO_FATAL_FAILURE(lIsSameSet(reloadedSessionSet.get(), isExpectedToBeExported));
    const auto lIsSameElement = [&elp](const MegaSetElement* el)
    {
        ASSERT_EQ(el->id(), elp->id());
        ASSERT_EQ(el->node(), elp->node());
        ASSERT_STREQ(el->name(), elp->name());
        ASSERT_EQ(el->ts(), elp->ts());
        ASSERT_EQ(el->order(), elp->order());
    };
    unique_ptr<MegaSetElement> reloadedSessionElement(megaApi[userIdx]->getSetElement(sh, eh));
    ASSERT_NO_FATAL_FAILURE(lIsSameElement(reloadedSessionElement.get()));


    LOG_debug << "# U1: Check if Set is exported";
    ASSERT_TRUE(megaApi[0]->isExportedSet(sh)) << "Set should still be public after session resumption";


    LOG_debug << "# U1: Get public Set URL";
    const auto lCheckSetLink = [this, sh](int expectedResult)
    {
        bool isSuccessExpected = expectedResult == API_OK;
        unique_ptr<const char[]> publicSetLink(megaApi[0]->getPublicLinkForExportedSet(sh));
        if (isSuccessExpected)
            ASSERT_NE(publicSetLink.get(), nullptr);
        else
            ASSERT_EQ(publicSetLink.get(), nullptr);
    };

    ASSERT_NO_FATAL_FAILURE(lCheckSetLink(API_OK));


    LOG_debug << "# U1: Fetch Public Set and start Public Set preview mode";
    userIdx = 0;
    isExpectedToBeExported = true;
    const auto lIsSameElementList = [&els, &lIsSameElement](const MegaSetElementList* ell)
    {
        ASSERT_NE(ell, nullptr);
        ASSERT_EQ(ell->size(), els->size());
        ASSERT_NO_FATAL_FAILURE(lIsSameElement(ell->get(0)));
    };
    const auto lFetchCurrentSetInPreviewMode =
        [this, &lIsSameSet, &lIsSameElementList](int apiIdx, int isSuccessExpected)
    {
        unique_ptr<MegaSet> s(megaApi[static_cast<size_t>(apiIdx)]->getPublicSetInPreview());
        unique_ptr<MegaSetElementList> ell(
            megaApi[static_cast<size_t>(apiIdx)]->getPublicSetElementsInPreview());

        if (isSuccessExpected)
        {
            ASSERT_NO_FATAL_FAILURE(lIsSameSet(s.get(), true));
            ASSERT_NO_FATAL_FAILURE(lIsSameElementList(ell.get()));
        }
        else
        {
            ASSERT_EQ(s, nullptr);
            ASSERT_EQ(ell, nullptr);
        }
    };
    const auto lFetchPublicSet =
    [this, &exportedSetURL, &lIsSameSet, &lIsSameElementList, &lFetchCurrentSetInPreviewMode]
    (int apiIdx, bool isSetExportExpected)
    {
        MegaSet* exportedSet = nullptr;
        MegaSetElementList* exportedEls = nullptr;
        const auto reqResult = doFetchPublicSet(static_cast<unsigned>(apiIdx),
                                                &exportedSet,
                                                &exportedEls,
                                                exportedSetURL.c_str());
        unique_ptr<MegaSet> s(exportedSet);
        unique_ptr<MegaSetElementList> els(exportedEls);

        if (isSetExportExpected)
        {
            ASSERT_EQ(reqResult, API_OK);
            ASSERT_NO_FATAL_FAILURE(lIsSameSet(s.get(), isSetExportExpected));
            ASSERT_NO_FATAL_FAILURE(lIsSameElementList(els.get()));
        }
        else
        {
            ASSERT_NE(reqResult, API_OK);
            ASSERT_EQ(s.get(), nullptr);
            ASSERT_EQ(els.get(), nullptr);
        }

        ASSERT_EQ(megaApi[static_cast<size_t>(apiIdx)]->inPublicSetPreview(), isSetExportExpected);
        ASSERT_NO_FATAL_FAILURE(lFetchCurrentSetInPreviewMode(apiIdx, isSetExportExpected));
    };

    ASSERT_NO_FATAL_FAILURE(lFetchPublicSet(0, isExpectedToBeExported));


    LOG_debug << "# U1: Stop Public Set preview mode";
    userIdx = 0;
    megaApi[userIdx]->stopPublicSetPreview();
    ASSERT_FALSE(megaApi[userIdx]->inPublicSetPreview());
    ASSERT_NO_FATAL_FAILURE(lFetchCurrentSetInPreviewMode(static_cast<int>(userIdx), false));

    LOG_debug << "# U2: Attempt to fetch public Set using wrong key";
    string exportedSetURL_wrongKey = exportedSetURL;
    exportedSetURL_wrongKey.replace(exportedSetURL_wrongKey.find('#'), string::npos, "#aaaaaaaaaaaaaaaaaaaaaa");
    ASSERT_EQ(doFetchPublicSet(1, nullptr, nullptr, exportedSetURL_wrongKey.c_str()), API_EKEY);


    LOG_debug << "# U2: Fetch public Set and start preview mode";
    userIdx = 1;
    ASSERT_NO_FATAL_FAILURE(lFetchPublicSet(static_cast<int>(userIdx), isExpectedToBeExported));
    // test shortcut
    LOG_debug << "\tTesting fetch shortcut (same public Set in a row)";
    ASSERT_NO_FATAL_FAILURE(lFetchPublicSet(static_cast<int>(userIdx), isExpectedToBeExported));

    LOG_debug << "# U2: Download foreign Set Element in preview set mode";
    unique_ptr<MegaNode> foreignNode;
    const auto lFetchForeignNode = [this, &foreignNode, &uploadedNode, &elp](int expectedResult)
    {
        ASSERT_EQ(elp->node(), uploadedNode);
        MegaNode* fNode = nullptr;

        ASSERT_EQ(expectedResult, doGetPreviewElementNode(1, &fNode, elp->id()));

        foreignNode.reset(fNode);
        if (expectedResult == API_OK)
        {
            ASSERT_NE(foreignNode, nullptr);
            unique_ptr<MegaNode> sourceNode(megaApi[0]->getNodeByHandle(uploadedNode));
            ASSERT_TRUE(sourceNode) << "Failed to find source file (should never happed)";
            ASSERT_STREQ(foreignNode->getName(), sourceNode->getName()) << "File names did not match";
            ASSERT_EQ(foreignNode->getSize(), sourceNode->getSize()) << "File size did not match";
            ASSERT_EQ(foreignNode->getOwner(), sourceNode->getOwner()) << "File owner did not match";
            ASSERT_STREQ(foreignNode->getFingerprint(), sourceNode->getFingerprint()) << "File fingerprint did not match";
            ASSERT_STREQ(foreignNode->getFileAttrString(), sourceNode->getFileAttrString()) << "File attrs did not match";
            ASSERT_EQ(foreignNode->getCreationTime(), sourceNode->getCreationTime()) << "Node creation time did not match";
            ASSERT_EQ(foreignNode->getModificationTime(), sourceNode->getModificationTime())
                << "\tForeign node's mtime inconsistent";
        }
        else
        {
            ASSERT_EQ(foreignNode, nullptr);
        }
    };
    const auto lDownloadForeignElement = [this] (int expectedResult, MegaNode* validForeignNode)
    {
        string downloadPath = (fs::current_path() / UPFILE.c_str()).u8string();
        if (fs::exists(downloadPath)) fs::remove(downloadPath);
        ASSERT_EQ(expectedResult,
                  doStartDownload(1, validForeignNode,
                                  downloadPath.c_str(), // trims from end to first separator
                                  nullptr  /*customName*/,
                                  nullptr  /*appData*/,
                                  false    /*startFirst*/,
                                  nullptr  /*cancelToken*/,
                                  MegaTransfer::COLLISION_CHECK_FINGERPRINT /*collisionCheck*/,
                                  MegaTransfer::COLLISION_RESOLUTION_NEW_WITH_N /* collisionResolution */,
                                  false    /*undelete*/));
        fs::remove(downloadPath);
    };

    ASSERT_NO_FATAL_FAILURE(lFetchForeignNode(API_OK));
    ASSERT_NO_FATAL_FAILURE(lDownloadForeignElement(API_OK, foreignNode.get()));


    LOG_debug << "# U2: Stop Public Set preview mode";
    userIdx = 1;
    megaApi[userIdx]->stopPublicSetPreview();
    ASSERT_FALSE(megaApi[userIdx]->inPublicSetPreview());
    ASSERT_NO_FATAL_FAILURE(lFetchCurrentSetInPreviewMode(static_cast<int>(userIdx), false));

    LOG_debug << "# U2: Download foreign Set Element not in preview set mode (-11 and -9 expected)";
    ASSERT_NO_FATAL_FAILURE(lFetchForeignNode(API_EACCESS));
    ASSERT_NO_FATAL_FAILURE(lDownloadForeignElement(API_ENOENT, foreignNode.get()));


    LOG_debug << "# U1: Disable Set export (invalidates public link)";
    userIdx = 0;
    ASSERT_TRUE(megaApi[userIdx]->isExportedSet(sh));
    differentApiDtlsPtr->setUpdated = false;
    ASSERT_EQ(API_OK, doDisableExportSet(userIdx, sh));
    isExpectedToBeExported = false;
    unique_ptr<MegaSet> s1pDisabledExport(megaApi[userIdx]->getSet(sh));
    ASSERT_NO_FATAL_FAILURE(lIsSameSet(s1pDisabledExport.get(), isExpectedToBeExported));
    // wait for action packets on both APIs (disable updates through APs)
    ASSERT_TRUE(waitForResponse(&differentApiDtlsPtr->setUpdated))
        << "Failed to receive disable export Set update AP on U1's secondary client";
    s1pDisabledExport.reset(differentApiPtr->getSet(sh));
    ASSERT_NO_FATAL_FAILURE(lIsSameSet(s1pDisabledExport.get(), isExpectedToBeExported));
    // test shortcut on disable export
    LOG_debug << "\tChecking export disable shortcut";
    exportedSet = nullptr;
    ASSERT_EQ(API_OK, doDisableExportSet(userIdx, sh));
    s1pDisabledExport.reset(megaApi[userIdx]->getSet(sh));
    ASSERT_NO_FATAL_FAILURE(lIsSameSet(s1pDisabledExport.get(), isExpectedToBeExported));


    LOG_debug << "# U1: Check if Set is exported";
    ASSERT_FALSE(megaApi[0]->isExportedSet(sh));


    LOG_debug << "# U1: Get public Set URL (expect -9)";
    ASSERT_NO_FATAL_FAILURE(lCheckSetLink(API_ENOENT));


    LOG_debug << "# U1: Fetch public Set on non-exported Set (using previously valid link)";
    userIdx = 0;
    ASSERT_NO_FATAL_FAILURE(lFetchPublicSet(static_cast<int>(userIdx), isExpectedToBeExported));
    ASSERT_FALSE(megaApi[userIdx]->inPublicSetPreview())
        << "Public Set preview mode should not be active";

    LOG_debug << "# U1: Remove all Sets";
    userIdx = 0;
    unique_ptr<MegaSetList> sets(megaApi[userIdx]->getSets());
    for (unsigned i = 0; i < sets->size(); ++i)
    {
        differentApiDtlsPtr->setUpdated = false;
        handle setId = sets->get(i)->id();
        ASSERT_EQ(API_OK, doRemoveSet(userIdx, setId));

        unique_ptr<MegaSet> s(megaApi[userIdx]->getSet(setId));
        ASSERT_EQ(s, nullptr);
        ASSERT_TRUE(waitForResponse(&differentApiDtlsPtr->setUpdated))
            << "Failed to receive deleted Set AP on U1's secondary client";
    }
    sets.reset(megaApi[static_cast<size_t>(userIdx)]->getSets());
    ASSERT_EQ(sets->size(), 0u);
}

/**
 * @brief TEST_F SdkTestSetsAndElementsSetTypes
 *
 * Tests creating all possible Set types.
 */
TEST_F(SdkTest, SdkTestSetsAndElementsSetTypes)
{
    LOG_info << "___TEST Sets and Elements Set Types___";

    // U1: Create set with type MegaSet::SET_TYPE_PHOTOS
    // U1: Create set with type MegaSet::SET_TYPE_VIDEOS
    // U1: Create set with a type out of range
    // U1: Logout / login to retrieve Sets
    // U1: Check existing Sets and types
    // U1: Remove all Sets

    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    // Use another connection with the same credentials as U1 (i.e. fake a different client for AP processing)
    MegaApi* differentApiPtr = nullptr;
    PerApi* differentApiDtlsPtr = nullptr;
    const int userIdx = 0;
    megaApi.emplace_back(newMegaApi(APP_KEY.c_str(), megaApiCacheFolder(userIdx).c_str(), USER_AGENT.c_str(), unsigned(THREADS_PER_MEGACLIENT)));
    differentApiPtr = &(*megaApi.back());
    differentApiPtr->addListener(this);
    PerApi pa; // make a copy
    auto& aux = mApi[userIdx];
    pa.email = aux.email;
    pa.pwd = aux.pwd;
    mApi.push_back(std::move(pa));
    differentApiDtlsPtr = &(mApi.back());
    differentApiDtlsPtr->megaApi = differentApiPtr;
    const unsigned int difApiIdx = static_cast<unsigned>(megaApi.size() - 1);
    differentApiPtr->setLoggingName(to_string(difApiIdx).c_str());

    auto loginTracker = asyncRequestLogin(difApiIdx, differentApiDtlsPtr->email.c_str(), differentApiDtlsPtr->pwd.c_str());
    ASSERT_EQ(API_OK, loginTracker->waitForResult()) << " Failed to establish a login/session for account " << difApiIdx;
    loginTracker = asyncRequestFetchnodes(difApiIdx);
    ASSERT_EQ(API_OK, loginTracker->waitForResult()) << " Failed to fetch nodes for account " << difApiIdx;


    LOG_debug << "# U1: Create set with type MegaSet::SET_TYPE_PHOTOS";
    const string albumName = u8"qq-001";
    MegaSet* newSet = nullptr;
    differentApiDtlsPtr->setUpdated = false;
    ASSERT_EQ(API_OK, doCreateSet(0, &newSet, albumName.c_str(), MegaSet::SET_TYPE_ALBUM));
    ASSERT_NE(newSet, nullptr);
    const unique_ptr<MegaSet> setAsPhotoAlbum(newSet);
    const MegaHandle albumHandle = setAsPhotoAlbum->id();
    ASSERT_TRUE(waitForResponse(&differentApiDtlsPtr->setUpdated))
        << "Failed to receive create Set AP on U1's secondary client for photo album creation";


    LOG_debug << "# U1: Create set with type MegaSet::SET_TYPE_VIDEOS";
    const string playlistName = u8"gg-001";
    newSet = nullptr;
    differentApiDtlsPtr->setUpdated = false;
    PerApi& target = mApi[userIdx];
    target.resetlastEvent();     // So we can detect when the node database has been committed.
    ASSERT_EQ(API_OK, doCreateSet(0, &newSet, playlistName.c_str(), MegaSet::SET_TYPE_PLAYLIST));
    ASSERT_NE(newSet, nullptr);
    const unique_ptr<MegaSet> setAsVideoPlaylist(newSet);
    const MegaHandle playlistHandle = setAsVideoPlaylist->id();
    ASSERT_TRUE(waitForResponse(&differentApiDtlsPtr->setUpdated))
        << "Failed to receive create Set AP on U1's secondary client for video playlist creation";
    // Wait for the database to be updated (note: even if commit is triggered by 1st update, the 2nd update
    // has been applied already, so the DB will store the final value)
    ASSERT_TRUE(WaitFor([&target](){ return target.lastEventsContain(MegaEvent::EVENT_COMMIT_DB); }, maxTimeout*1000))
        << "Failed to receive commit to DB event related to latest Set stored";


    LOG_debug << "# U1: Create Sets with invalid types";
    const string invalidSetName = u8"failure-001";
    newSet = nullptr;
    const int maxRange = 255;  // std::numeric_limits<uint8_t>::max()
    const int minRange = 0;    // std::numeric_limits<uint8_t>::min()
    ASSERT_NE(API_OK, doCreateSet(0, &newSet, invalidSetName.c_str(), maxRange));
    ASSERT_EQ(newSet, nullptr);
    ASSERT_NE(API_OK, doCreateSet(0, &newSet, invalidSetName.c_str(), maxRange + 1));
    ASSERT_EQ(newSet, nullptr);
    ASSERT_NE(API_OK, doCreateSet(0, &newSet, invalidSetName.c_str(), minRange - 1));
    ASSERT_EQ(newSet, nullptr);


    LOG_debug << "# U1: Logout / login to retrieve Sets";
    unique_ptr<char[]> session(dumpSession());
    ASSERT_NO_FATAL_FAILURE(locallogout());
    ASSERT_NO_FATAL_FAILURE(resumeSession(session.get()));
    ASSERT_NO_FATAL_FAILURE(fetchnodes(userIdx)); // load cached Sets

    LOG_debug << "# U1: Check Sets types loaded from local cache";
    constexpr std::string_view noLocalDBMsg{"The set was not in the cached memory"};

    unique_ptr<MegaSet> reloadedSessionAlbumSet(megaApi[userIdx]->getSet(albumHandle));
    ASSERT_NE(reloadedSessionAlbumSet, nullptr) << "Photo album: " << noLocalDBMsg;
    ASSERT_EQ(reloadedSessionAlbumSet->type(), setAsPhotoAlbum->type());

    unique_ptr<MegaSet> reloadedSessionPlaylistSet(megaApi[userIdx]->getSet(playlistHandle));
    ASSERT_NE(reloadedSessionPlaylistSet, nullptr) << "Playlist: " << noLocalDBMsg;
    ASSERT_EQ(reloadedSessionPlaylistSet->type(), setAsVideoPlaylist->type());


    LOG_debug << "# U1: Remove all Sets";
    unique_ptr<MegaSetList> sets(megaApi[userIdx]->getSets());
    for (unsigned i = 0; i < sets->size(); ++i)
    {
        differentApiDtlsPtr->setUpdated = false;
        handle setId = sets->get(i)->id();
        ASSERT_EQ(API_OK, doRemoveSet(userIdx, setId));

        unique_ptr<MegaSet> s(megaApi[userIdx]->getSet(setId));
        ASSERT_EQ(s, nullptr);
        ASSERT_TRUE(waitForResponse(&differentApiDtlsPtr->setUpdated))
            << "Failed to receive deleted Set AP on U1's secondary client";
    }
    sets.reset(megaApi[userIdx]->getSets());
    ASSERT_EQ(sets->size(), 0u);
}

/**
 * @brief TEST_F SdkUserAlerts
 *
 * Generate User Alerts and check that they are received and acknowledged as expected.
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
 *      NewScheduledMeeting
 *      UpdatedScheduledMeeting
 *
 * Not generated:
 *      UpdatedPendingContactIncoming   skipped (requires a 2 week wait)
 *      UpdatedPendingContactOutgoing   skipped (requires a 2 week wait)
 *      Payment                         skipped (out of user control)
 *      PaymentReminder                 skipped (out of user control)
 *      Takedown                        skipped (out of user control)
 *      SetTakedown                     skipped (out of user control)
 */
TEST_F(SdkTest, SdkUserAlerts)
{
    LOG_info << "___TEST SdkUserAlerts___";
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(2));

    unsigned A1idx = 0;
    unsigned B1idx = 1;

    // Alerts generated in this test should be compared with:
    // - db persisted ones;
    // - the ones received through sc50 request.
    // However, for now we kept code dealing with sc50 disabled because sc50 request times out very often in Jenkins,
    // and has various inconsistencies (some depending on the shard handling the sc50 request).
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
    mApi.push_back(std::move(pa));
    auto& B2dtls = mApi.back();
    B2dtls.megaApi = &B2;
#endif

    // A1
    auto& A1dtls = mApi[A1idx];
    auto& A1 = *megaApi[A1idx];

    // B1
    auto& B1dtls = mApi[B1idx];
    auto& B1 = *megaApi[B1idx];

    vector<unique_ptr<MegaUserAlert>> bkpAlerts; // used for comparing existing alerts with persisted ones
    vector<unique_ptr<MegaUserAlert>> bkpSc50Alerts; // used for comparing existing alerts with ones received through sc50 (useful only when enabled)

    // IncomingPendingContact  --  request created
    //--------------------------------------------

    // reset User Alerts for B1
    B1dtls.userAlertsUpdated = false;
    B1dtls.userAlertList.reset();

    // --- Send a contact request ---
    A1dtls.contactRequestUpdated = B1dtls.contactRequestUpdated = false;
    ASSERT_NO_FATAL_FAILURE(inviteContact(A1idx,
                                          B1dtls.email,
                                          "test: A1 invited B1",
                                          MegaContactRequest::INVITE_ACTION_ADD));
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
    int count = 0;
    const MegaUserAlert* a = nullptr;
    for (int i = 0; i < B1dtls.userAlertList->size(); ++i)
    {
        if (B1dtls.userAlertList->get(i)->isRemoved()) continue;
        a = B1dtls.userAlertList->get(i);
        count++;
    }
    ASSERT_EQ(count, 1) << "IncomingPendingContact  --  request created";
    ASSERT_STRCASEEQ(a->getEmail(), A1dtls.email.c_str()) << "IncomingPendingContact  --  request created";
    ASSERT_STRCASEEQ(a->getTitle(), "Sent you a contact request") << "IncomingPendingContact  --  request created";
    ASSERT_GT(a->getId(), 0u) << "IncomingPendingContact  --  request created";
    ASSERT_EQ(a->getType(), MegaUserAlert::TYPE_INCOMINGPENDINGCONTACT_REQUEST) << "IncomingPendingContact  --  request created";
    ASSERT_STREQ(a->getTypeString(), "NEW_CONTACT_REQUEST") << "IncomingPendingContact  --  request created";
    ASSERT_STRCASEEQ(a->getHeading(), A1dtls.email.c_str()) << "IncomingPendingContact  --  request created";
    ASSERT_NE(a->getTimestamp(0), 0) << "IncomingPendingContact  --  request created";
    ASSERT_FALSE(a->isOwnChange()) << "IncomingPendingContact  --  request created";
    ASSERT_EQ(a->getPcrHandle(), B1dtls.cr->getHandle()) << "IncomingPendingContact  --  request created";
    bkpAlerts.emplace_back(a->copy());
    bkpSc50Alerts.emplace_back(a->copy());
    ASSERT_FALSE(a->getSeen());

    B1dtls.userAlertsUpdated = false;
    B1dtls.userAlertList.reset();
    ASSERT_EQ(doAckUserAlerts(B1idx), API_OK);
    ASSERT_TRUE(waitForResponse(&B1dtls.userAlertsUpdated))
        << "Alert about contact request creation not received by B1 after " << maxTimeout
        << " seconds";
    ASSERT_NE(B1dtls.userAlertList, nullptr) << "IncomingPendingContact  --  request created";
    for (int i = 0; i < B1dtls.userAlertList->size(); ++i)
    {
        if (B1dtls.userAlertList->get(i)->isRemoved()) continue;
        ASSERT_TRUE(B1dtls.userAlertList->get(i)->getSeen());
    }

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
    count = 0; a = nullptr;
    for (int i = 0; i < A1dtls.userAlertList->size(); ++i)
    {
        if (A1dtls.userAlertList->get(i)->isRemoved()) continue;
        a = A1dtls.userAlertList->get(i);
        count++;
    }
    ASSERT_EQ(count, 1) << "ContactChange  --  contact request accepted";
    ASSERT_STRCASEEQ(a->getEmail(), B1dtls.email.c_str()) << "ContactChange  --  contact request accepted";
    ASSERT_STRCASEEQ(a->getTitle(), "Contact relationship established") << "ContactChange  --  contact request accepted";
    ASSERT_GT(a->getId(), 0u) << "ContactChange  --  contact request accepted";
    ASSERT_EQ(a->getType(), MegaUserAlert::TYPE_CONTACTCHANGE_CONTACTESTABLISHED) << "ContactChange  --  contact request accepted";
    ASSERT_STREQ(a->getTypeString(), "CONTACT_ESTABLISHED") << "ContactChange  --  contact request accepted";
    ASSERT_STRCASEEQ(a->getHeading(), B1dtls.email.c_str()) << "ContactChange  --  contact request accepted";
    ASSERT_NE(a->getTimestamp(0), 0) << "ContactChange  --  contact request accepted";
    ASSERT_FALSE(a->isOwnChange()) << "ContactChange  --  contact request accepted";
    ASSERT_EQ(a->getUserHandle(), B1.getMyUserHandleBinary()) << "ContactChange  --  contact request accepted";
    ASSERT_FALSE(a->getSeen());

    A1dtls.userAlertsUpdated = false;
    A1dtls.userAlertList.reset();
    ASSERT_EQ(doAckUserAlerts(A1idx), API_OK);
    ASSERT_TRUE(waitForResponse(&A1dtls.userAlertsUpdated))
        << "Alert about contact request change not received by A1 after " << maxTimeout
        << " seconds";
    ASSERT_NE(A1dtls.userAlertList, nullptr) << "Contact change  --  contact request accepted";
    for (int i = 0; i < A1dtls.userAlertList->size(); ++i)
    {
        if (A1dtls.userAlertList->get(i)->isRemoved()) continue;
        ASSERT_TRUE(A1dtls.userAlertList->get(i)->getSeen());
    }
    // received by A1, do not keep it for comparing with B2's sc50

    if (gManualVerification)
    {
        if (!areCredentialsVerified(0, mApi[1].email)) {ASSERT_NO_FATAL_FAILURE(verifyCredentials(0, mApi[1].email));}
        if (!areCredentialsVerified(1, mApi[0].email)) {ASSERT_NO_FATAL_FAILURE(verifyCredentials(1, mApi[0].email));}
    }

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
    ASSERT_EQ(MegaError::API_OK,
              doStartUpload(A1idx,
                            &hPublicfile,
                            PUBLICFILE.c_str(),
                            nSharedFolder.get(),
                            nullptr /*fileName*/,
                            ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
                            nullptr /*appData*/,
                            false /*isSourceTemporary*/,
                            false /*startFirst*/,
                            nullptr /*cancelToken*/))
        << "Cannot upload a test file";

    std::unique_ptr<MegaNode> nSubfolder(A1.getNodeByHandle(hSubfolder));
    ASSERT_NE(nSubfolder, nullptr);
    std::unique_ptr<MegaNode> rootA1(A1.getRootNode());
    ASSERT_NE(rootA1, nullptr);
    ASSERT_TRUE(createFile(UPFILE.c_str(), false)) << "Couldn't create " << UPFILE.c_str();
    MegaHandle hUpfile = UNDEF;
    ASSERT_EQ(MegaError::API_OK,
              doStartUpload(A1idx,
                            &hUpfile,
                            UPFILE.c_str(),
                            rootA1.get(),
                            nullptr /*fileName*/,
                            ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
                            nullptr /*appData*/,
                            false /*isSourceTemporary*/,
                            false /*startFirst*/,
                            nullptr /*cancelToken*/))
        << "Cannot upload a second test file";

#ifdef ENABLE_CHAT
    // NewScheduledMeeting
    //--------------------------------------------
    // reset User Alerts for B1
    B1dtls.userAlertsUpdated = false;
    B1dtls.userAlertList.reset();
    A1dtls.schedId = UNDEF;
    A1dtls.chatid = UNDEF;
    A1dtls.requestFlags[MegaRequest::TYPE_ADD_UPDATE_SCHEDULED_MEETING] = false;
    MegaHandle chatid = UNDEF;
    createChatScheduledMeeting(0, chatid);
    ASSERT_NE(chatid, UNDEF) << "Invalid chat";
    waitForResponse(&A1dtls.requestFlags[MegaRequest::TYPE_ADD_UPDATE_SCHEDULED_MEETING], maxTimeout);

    ASSERT_TRUE(waitForResponse(&B1dtls.userAlertsUpdated))
        << "Alert about scheduled meeting creation not received by B1 after " << maxTimeout << " seconds";
    ASSERT_NE(B1dtls.userAlertList, nullptr) << "Scheduled meeting created";

    count = 0;
    for (int i = 0; i < B1dtls.userAlertList->size(); ++i)
    {
        a = B1dtls.userAlertList->get(i);
        if (a->isRemoved()) continue;
        count++;
    }
    ASSERT_EQ(count, 1) << "NewScheduledMeeting";
    ASSERT_EQ(A1dtls.chatid, chatid) << "Scheduled meeting could not be created, unexpected chatid";
    ASSERT_NE(A1dtls.schedId, UNDEF) << "Scheduled meeting could not be created, invalid scheduled meeting id";
    bkpAlerts.emplace_back(a->copy());

    // UpdateScheduledMeeting
    //--------------------------------------------
    // reset User Alerts for B1
    B1dtls.userAlertsUpdated = false;
    B1dtls.userAlertList.reset();
    A1dtls.schedId = UNDEF;
    A1dtls.chatid = UNDEF;
    A1dtls.requestFlags[MegaRequest::TYPE_ADD_UPDATE_SCHEDULED_MEETING] = false;
    updateScheduledMeeting(0, chatid);
    ASSERT_NE(chatid, UNDEF) << "Invalid chat";
    waitForResponse(&A1dtls.requestFlags[MegaRequest::TYPE_ADD_UPDATE_SCHEDULED_MEETING], maxTimeout);

    ASSERT_TRUE(waitForResponse(&B1dtls.userAlertsUpdated))
        << "Alert about scheduled meeting update not received by B1 after " << maxTimeout << " seconds";
    ASSERT_NE(B1dtls.userAlertList, nullptr) << "Scheduled meeting created";

    count = 0;
    for (int i = 0; i < B1dtls.userAlertList->size(); ++i)
    {
        a = B1dtls.userAlertList->get(i);
        if (a->isRemoved()) continue;
        count++;
    }
    ASSERT_EQ(count, 1) << "UpdateScheduledMeeting";
    ASSERT_EQ(A1dtls.chatid, chatid) << "Scheduled meeting could not be updated, unexpected chatid";
    ASSERT_NE(A1dtls.schedId, UNDEF)
        << "Scheduled meeting could not be updated, invalid scheduled meeting id";
    bkpAlerts.emplace_back(a->copy());
#endif

    // NewShare
    //--------------------------------------------

    // reset User Alerts for B1
    B1dtls.userAlertsUpdated = false;
    B1dtls.userAlertList.reset();

    // --- Create a new outgoing share ---
    A1dtls.nodeUpdated = B1dtls.nodeUpdated = false; // reset flags expected to be true in asserts below
    A1dtls.mOnNodesUpdateCompletion = [&A1dtls, A1idx](size_t apiIndex, MegaNodeList*)
    {
        if (A1idx == apiIndex)
            A1dtls.nodeUpdated = true;
    };
    B1dtls.mOnNodesUpdateCompletion = [&B1dtls, B1idx](size_t apiIndex, MegaNodeList*)
    {
        if (B1idx == apiIndex)
            B1dtls.nodeUpdated = true;
    };

    ASSERT_NO_FATAL_FAILURE(shareFolder(nSharedFolder.get(), B1dtls.email.c_str(), MegaShare::ACCESS_FULL));
    ASSERT_TRUE(waitForResponse(&A1dtls.nodeUpdated))   // at the target side (main account)
        << "Node update not received by A1 after " << maxTimeout << " seconds";
    ASSERT_TRUE(waitForResponse(&B1dtls.nodeUpdated))   // at the target side (auxiliar account)
        << "Node update not received by B1 after " << maxTimeout << " seconds";
    // important to reset
    resetOnNodeUpdateCompletionCBs();
    // Wait for node to be decrypted in B account
    ASSERT_TRUE(WaitFor([&B1, hSharedFolder]()
    {
        std::unique_ptr<MegaNode> inshareNode(B1.getNodeByHandle(hSharedFolder));
        return inshareNode && inshareNode->isNodeKeyDecrypted();
    }, 60*1000)) << "Cannot decrypt inshare in B account.";

    // NewShare
    ASSERT_TRUE(waitForResponse(&B1dtls.userAlertsUpdated))
        << "Alert about new share not received by B1 after " << maxTimeout << " seconds";
    ASSERT_NE(B1dtls.userAlertList, nullptr) << "NewShare";
    count = 0; a = nullptr;
    for (int i = 0; i < B1dtls.userAlertList->size(); ++i)
    {
        if (B1dtls.userAlertList->get(i)->isRemoved()) continue;
        a = B1dtls.userAlertList->get(i);
        count++;
    }
    ASSERT_EQ(B1dtls.userAlertList->size(), 1) << "NewShare";
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
    if (string(a->getName()) != "NO_KEY") // Share key may not yet be available when the user alert is created
    {
        ASSERT_STRCASEEQ(a->getPath(), string(A1dtls.email + ':' + sharedFolder).c_str()) << "NewShare";
        ASSERT_STREQ(a->getName(), sharedFolder) << "NewShare";
    }
    else
    {
        ASSERT_STRCASEEQ(a->getPath(), string(A1dtls.email + ":NO_KEY").c_str()) << "NewShare";
    }
    bkpAlerts.emplace_back(a->copy());
    bkpSc50Alerts.emplace_back(a->copy());
    ASSERT_FALSE(a->getSeen());

    B1dtls.userAlertsUpdated = false;
    B1dtls.userAlertList.reset();
    ASSERT_EQ(doAckUserAlerts(B1idx), API_OK);
    ASSERT_TRUE(waitForResponse(&B1dtls.userAlertsUpdated))
        << "Alert about new share creation not received by B1 after " << maxTimeout << " seconds";
    ASSERT_NE(B1dtls.userAlertList, nullptr) << "NewShare";
    for (int i = 0; i < B1dtls.userAlertList->size(); ++i)
    {
        if (B1dtls.userAlertList->get(i)->isRemoved()) continue;
        ASSERT_TRUE(B1dtls.userAlertList->get(i)->getSeen());
    }

    // RemovedSharedNode
    //--------------------------------------------

    // reset User Alerts for B1
    B1dtls.userAlertsUpdated = false;
    B1dtls.userAlertList.reset();

    // --- Move shared sub-folder (owned) to Root ---
    ASSERT_EQ(API_OK, doMoveNode(A1idx, nullptr, nSubfolder.get(), rootA1.get()))
        << "Moving subfolder out of (owned) share failed";

    // RemovedSharedNode
    ASSERT_TRUE(waitForResponse(&B1dtls.userAlertsUpdated))
        << "Alert about removed shared node not received by B1 after " << maxTimeout << " seconds";
    ASSERT_NE(B1dtls.userAlertList, nullptr) << "RemovedSharedNode";
    count = 0; a = nullptr;
    for (int i = 0; i < B1dtls.userAlertList->size(); ++i)
    {
        if (B1dtls.userAlertList->get(i)->isRemoved()) continue;
        a = B1dtls.userAlertList->get(i);
        count++;
    }
    ASSERT_EQ(count, 1) << "RemovedSharedNode";
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
    //bkpAlerts.emplace_back(a->copy()); // removed internally (combined to "update" later?)
    bkpSc50Alerts.emplace_back(a->copy());
    ASSERT_FALSE(a->getSeen());

    B1dtls.userAlertsUpdated = false;
    B1dtls.userAlertList.reset();
    ASSERT_EQ(doAckUserAlerts(B1idx), API_OK);
    ASSERT_TRUE(waitForResponse(&B1dtls.userAlertsUpdated))
        << "Alert about remove share not received by B1 after " << maxTimeout << " seconds";
    ASSERT_NE(B1dtls.userAlertList, nullptr) << "RemovedSharedNode";
    for (int i = 0; i < B1dtls.userAlertList->size(); ++i)
    {
        if (B1dtls.userAlertList->get(i)->isRemoved()) continue;
        ASSERT_TRUE(B1dtls.userAlertList->get(i)->getSeen());
    }

    // NewSharedNodes
    //--------------------------------------------

    // reset User Alerts for B1
    B1dtls.userAlertsUpdated = false;
    B1dtls.userAlertList.reset();

    // --- Move sub-folder from Root (owned) back to share ---
    ASSERT_EQ(API_OK, doMoveNode(A1idx, nullptr, nSubfolder.get(), nSharedFolder.get()))
        << "Moving sub-folder from Root (owned) to share failed";
    // Notifies and suppresses the previous RemovedSharedNode alert
    ASSERT_TRUE(waitForResponse(&B1dtls.userAlertsUpdated))
        << "Suppressed remove share alert not received by B1 after " << maxTimeout << " seconds";
    ASSERT_NE(B1dtls.userAlertList, nullptr) << "Suppressed RemovedSharedNode";
    ASSERT_EQ(B1dtls.userAlertList->size(), 1) << "Suppressed RemovedSharedNode";
    ASSERT_EQ(B1dtls.userAlertList->get(0)->getType(), MegaUserAlert::TYPE_REMOVEDSHAREDNODES)
        << "Suppressed RemovedSharedNode";
    ASSERT_TRUE(B1dtls.userAlertList->get(0)->isRemoved()) << "Suppressed RemovedSharedNode";

    // --- Move file from Root (owned) to share ---
    B1dtls.userAlertsUpdated = false;
    B1dtls.userAlertList.reset();
    std::unique_ptr<MegaNode> nfile2(A1.getNodeByHandle(hUpfile));
    ASSERT_NE(nfile2, nullptr);
    ASSERT_EQ(API_OK, doMoveNode(A1idx, nullptr, nfile2.get(), nSubfolder.get()))
        << "Moving file from Root (owned) to shared folder failed";

    // NewSharedNodes
    ASSERT_TRUE(waitForResponse(&B1dtls.userAlertsUpdated))
        << "Alert about node added to share not received by B1 after " << maxTimeout << " seconds";
    ASSERT_NE(B1dtls.userAlertList, nullptr) << "NewSharedNodes";
    count = 0; a = nullptr;
    for (int i = 0; i < B1dtls.userAlertList->size(); ++i)
    {
        a = B1dtls.userAlertList->get(i);
        if (a->isRemoved())
            continue;
        count++;
    }
    ASSERT_EQ(count, 1) << "NewSharedNodes";
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
    //bkpAlerts.emplace_back(a->copy()); // removed internally (combined to "update" later?)
    bkpSc50Alerts.emplace_back(a->copy());
    ASSERT_FALSE(a->getSeen());

    B1dtls.userAlertsUpdated = false;
    B1dtls.userAlertList.reset();
    ASSERT_EQ(doAckUserAlerts(B1idx), API_OK);
    ASSERT_TRUE(waitForResponse(&B1dtls.userAlertsUpdated))
        << "Alert about new share creation not received by B1 after " << maxTimeout << " seconds";
    ASSERT_NE(B1dtls.userAlertList, nullptr) << "NewSharedNodes";
    for (int i = 0; i < B1dtls.userAlertList->size(); ++i)
    {
        if (B1dtls.userAlertList->get(i)->isRemoved()) continue;
        ASSERT_TRUE(B1dtls.userAlertList->get(i)->getSeen());
    }

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
    count = 0; a = nullptr;
    for (int i = 0; i < B1dtls.userAlertList->size(); ++i)
    {
        if (B1dtls.userAlertList->get(i)->isRemoved()) continue;
        a = B1dtls.userAlertList->get(i);
        count++;
    }
    ASSERT_EQ(count, 1) << "UpdatedSharedNode";
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
    ASSERT_FALSE(a->getSeen());

    B1dtls.userAlertsUpdated = false;
    B1dtls.userAlertList.reset();
    ASSERT_EQ(doAckUserAlerts(B1idx), API_OK);
    ASSERT_TRUE(waitForResponse(&B1dtls.userAlertsUpdated))
        << "Alert about update share node not received by B1 after " << maxTimeout << " seconds";
    ASSERT_NE(B1dtls.userAlertList, nullptr) << "UpdatedSharedNode";
    for (int i = 0; i < B1dtls.userAlertList->size(); ++i)
    {
        if (B1dtls.userAlertList->get(i)->isRemoved()) continue;
        ASSERT_TRUE(B1dtls.userAlertList->get(i)->getSeen());
    }
    // this will be combined with the next one, do not keep it for comparison or validation


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
    count = 0;
    for (int i = 0; i < B1dtls.userAlertList->size(); ++i)
    {
        if (B1dtls.userAlertList->get(i)->isRemoved()) continue;
        a = B1dtls.userAlertList->get(i);
        count++;
    }
    ASSERT_EQ(count, 1) << "UpdatedSharedNode (combined)";
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
    bkpAlerts.emplace_back(a->copy());
    ASSERT_FALSE(a->getSeen());

    B1dtls.userAlertsUpdated = false;
    B1dtls.userAlertList.reset();
    ASSERT_EQ(doAckUserAlerts(B1idx), API_OK);
    ASSERT_TRUE(waitForResponse(&B1dtls.userAlertsUpdated))
        << "Alert about update share node not received by B1 after " << maxTimeout << " seconds";
    ASSERT_NE(B1dtls.userAlertList, nullptr) << "UpdatedSharedNode";
    for (int i = 0; i < B1dtls.userAlertList->size(); ++i)
    {
        if (B1dtls.userAlertList->get(i)->isRemoved()) continue;
        ASSERT_TRUE(B1dtls.userAlertList->get(i)->getSeen());
    }
    //bkpSc50Alerts.emplace_back(a->copy()); // not generated by API, which sends only [combined] NewSharedNodes


#if MEGA_TEST_SC50_ALERTS
    // B2
    B2dtls.userAlertsUpdated = false;
    B2dtls.userAlertList.reset();

    int B2idx = int(megaApi.size() - 1);
    auto loginTrackerB2 = asyncRequestLogin(B2idx, B2dtls.email.c_str(), B2dtls.pwd.c_str());
    ASSERT_EQ(API_OK, loginTrackerB2->waitForResult()) << " Failed to establish a login/session for account " << B2idx << " (B2)";
    fetchnodes(B2idx);
    // get sc50 alerts after login
    unsigned sc50Timeout = 120; // seconds
    ASSERT_TRUE(waitForResponse(&B2dtls.userAlertsUpdated, sc50Timeout))
        << "sc50 alerts after login not received by B2 after " << sc50Timeout << " seconds";
    ASSERT_EQ(B2dtls.userAlertList, nullptr) << "sc50";
    unique_ptr<MegaUserAlertList> sc50Alerts(B2.getUserAlerts());
    ASSERT_TRUE(sc50Alerts);
    ASSERT_GT(sc50Alerts->size(), 0);

    // validate sc50 alerts (go backwards)
    // assumption was that sc50 alerts are ordered as they were generated
    int skipped = 0; // there are more sc50 alerts than kept locally
    for (size_t j = bkpSc50Alerts.size(); j ; --j)
    {
        const auto* bkp = bkpSc50Alerts[j - 1].get();
        const auto* sal = sc50Alerts->get(sc50Alerts->size() - 1 - (int)bkpSc50Alerts.size() + (int)j - skipped);

        // apparently, sc50 will contain some extra alerts that are not part of the backup, so skip them (for now)
        if (bkp->getType() == MegaUserAlert::TYPE_NEWSHARE && sal->getType() == MegaUserAlert::TYPE_NEWSHAREDNODES)
        {
            // skip this sc50 alert
            // TODO: after sc50 inconsistencies have been addressed in API, make sure this is the expected behavior, and not a bug in the sc50 alerts
            skipped++;
            sal = sc50Alerts->get(sc50Alerts->size() - 1 - (int)(bkpSc50Alerts.size() - j) - skipped);
        }
        if (bkp->getType() == MegaUserAlert::TYPE_NEWSHARE && sal->getType() == MegaUserAlert::TYPE_REMOVEDSHAREDNODES)
        {
            // skip this sc50 alert
            // TODO: after sc50 inconsistencies have been addressed in API, make sure this is the expected behavior, and not a bug in the sc50 alerts
            skipped++;
            sal = sc50Alerts->get(sc50Alerts->size() - 1 - (int)(bkpSc50Alerts.size() - j) - skipped);
        }
        ASSERT_EQ(bkp->getType(), sal->getType()) << "sc50 alerts: " << bkp->getTypeString() << " vs " << sal->getTypeString();
        ASSERT_STREQ(bkp->getTypeString(), sal->getTypeString()) << "sc50 alerts: " << sal->getTypeString();
        ASSERT_EQ(bkp->getUserHandle(), sal->getUserHandle()) << "sc50 alerts: " << sal->getTypeString();
        ASSERT_EQ(bkp->getNodeHandle(), sal->getNodeHandle()) << "sc50 alerts: " << sal->getTypeString();
        ASSERT_EQ(bkp->getPcrHandle(), sal->getPcrHandle()) << "sc50 alerts: " << sal->getTypeString();
        ASSERT_STRCASEEQ(bkp->getEmail(), sal->getEmail()) << "sc50 alerts: " << sal->getTypeString();
        if (sal->getPath()) // the node might not be there when sc50 alerts arrive
        {
            ASSERT_STRCASEEQ(bkp->getPath(), sal->getPath()) << "sc50 alerts: " << sal->getTypeString();
        }
        if (sal->getName()) // the node might not be there when sc50 alerts arrive
        {
            ASSERT_STREQ(bkp->getName(), sal->getName()) << "sc50 alerts: " << sal->getTypeString();
        }
        ASSERT_STRCASEEQ(bkp->getHeading(), sal->getHeading()) << "sc50 alerts: " << sal->getTypeString();
        if (sal->getType() == MegaUserAlert::TYPE_NEWSHAREDNODES)
        {
            // this is a special case, as API does not generate UpdatedSharedNode alerts,
            // but only [combines them into previous] NewSharedNodes
            string ttl = bkp->getEmail() + string(" added 3 files");
            ASSERT_STRCASEEQ(sal->getTitle(), ttl.c_str()) << "sc50 alerts: " << sal->getTypeString();
            ASSERT_EQ(sal->getNumber(0), 0) << "sc50 alerts: " << sal->getTypeString();
            ASSERT_EQ(sal->getNumber(1), 3) << "sc50 alerts: " << sal->getTypeString();
            ASSERT_NE(sal->getHandle(0), UNDEF) << "sc50 alerts: " << sal->getTypeString();
            ASSERT_NE(sal->getHandle(1), UNDEF) << "sc50 alerts: " << sal->getTypeString();
            ASSERT_EQ(sal->getHandle(2), bkp->getHandle(0)) << "sc50 alerts: " << sal->getTypeString();
        }
        else
        {
            ASSERT_STRCASEEQ(bkp->getTitle(), sal->getTitle()) << "sc50 alerts: " << sal->getTypeString();
            if (sal->getType() == MegaUserAlert::TYPE_REMOVEDSHAREDNODES)
            {
                // this is another special caase
                // TODO: after sc50 inconsistencies have been addressed in API, make sure this is the expected behavior, and not a bug in the sc50 alert
                ASSERT_EQ(0, sal->getNumber(0)) << "sc50 alerts: " << sal->getTypeString();
            }
            else
            {
                ASSERT_EQ(bkp->getNumber(0), sal->getNumber(0)) << "sc50 alerts: " << sal->getTypeString();
            }
            ASSERT_EQ(bkp->getNumber(1), sal->getNumber(1)) << "sc50 alerts: " << sal->getTypeString();
            ASSERT_EQ(bkp->getHandle(0), sal->getHandle(0)) << "sc50 alerts: " << sal->getTypeString();
            ASSERT_EQ(bkp->getHandle(1), sal->getHandle(1)) << "sc50 alerts: " << sal->getTypeString();
        }
        //ASSERT_EQ(bkp->getTimestamp(0), sal->getTimestamp(0)) << "sc50 alerts"; // this will not match; sc50 timestamp is calculated
        ASSERT_STRCASEEQ(bkp->getString(0), sal->getString(0)) << "sc50 alerts: " << sal->getTypeString();
    }
#endif


    // DeletedShare
    //--------------------------------------------

    // reset User Alerts for B1
    B1dtls.userAlertsUpdated = false;
    B1dtls.userAlertList.reset();

    // --- Revoke access to an outgoing share ---
    A1dtls.nodeUpdated = B1dtls.nodeUpdated = false; // reset flags expected to be true in asserts below
    A1dtls.mOnNodesUpdateCompletion = [&A1dtls, A1idx](size_t apiIndex, MegaNodeList*)
    {
        if (A1idx == apiIndex)
            A1dtls.nodeUpdated = true;
    };
    B1dtls.mOnNodesUpdateCompletion = [&B1dtls, B1idx](size_t apiIndex, MegaNodeList*)
    {
        if (B1idx == apiIndex)
            B1dtls.nodeUpdated = true;
    };
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
    count = 0;
    for (int i = 0; i < B1dtls.userAlertList->size(); ++i)
    {
        if (B1dtls.userAlertList->get(i)->isRemoved()) continue;
        a = B1dtls.userAlertList->get(i);
        count++;
    }
    ASSERT_EQ(count, 1) << "DeletedShare";
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
    string path = A1dtls.email + ':' + sharedFolder;
    ASSERT_STRCASEEQ(a->getPath(), path.c_str()) << "DeletedShare";
    ASSERT_STREQ(a->getName(), sharedFolder) << "DeletedShare";
    ASSERT_EQ(a->getNumber(0), 1) << "DeletedShare";
    bkpAlerts.emplace_back(a->copy());
    ASSERT_FALSE(a->getSeen());

    B1dtls.userAlertsUpdated = false;
    B1dtls.userAlertList.reset();
    ASSERT_EQ(doAckUserAlerts(B1idx), API_OK);
    ASSERT_TRUE(waitForResponse(&B1dtls.userAlertsUpdated))
        << "Alert about delete share not received by B1 after " << maxTimeout << " seconds";
    ASSERT_NE(B1dtls.userAlertList, nullptr) << "DeletedShare";
    for (int i = 0; i < B1dtls.userAlertList->size(); ++i)
    {
        if (B1dtls.userAlertList->get(i)->isRemoved()) continue;
        ASSERT_TRUE(B1dtls.userAlertList->get(i)->getSeen());
    }

    // Reset credentials before removing contacts
    if (gManualVerification)
    {
        if (areCredentialsVerified(0, mApi[1].email)) {ASSERT_NO_FATAL_FAILURE(resetCredentials(0, mApi[1].email));}
        if (areCredentialsVerified(1, mApi[0].email)) {ASSERT_NO_FATAL_FAILURE(resetCredentials(1, mApi[0].email));}
    }

    // ContactChange  --  contact deleted
    //--------------------------------------------

    // reset User Alerts for B1
    B1dtls.userAlertsUpdated = false;
    B1dtls.userAlertList.reset();

    // --- Delete an existing contact ---
    ASSERT_EQ(API_OK, removeContact(0, B1dtls.email));

    // ContactChange  --  contact deleted
    ASSERT_TRUE(waitForResponse(&B1dtls.userAlertsUpdated))
        << "Alert about contact removal not received by B1 after " << maxTimeout << " seconds";
    ASSERT_NE(B1dtls.userAlertList, nullptr) << "ContactChange  --  contact deleted";
    count = 0;
    for (int i = 0; i < B1dtls.userAlertList->size(); ++i)
    {
        if (B1dtls.userAlertList->get(i)->isRemoved()) continue;
        a = B1dtls.userAlertList->get(i);
        count++;
    }
    ASSERT_EQ(count, 1) << "ContactChange  --  contact deleted";
    ASSERT_STRCASEEQ(a->getEmail(), A1dtls.email.c_str()) << "ContactChange  --  contact deleted";
    ASSERT_STRCASEEQ(a->getTitle(), "Deleted you as a contact") << "ContactChange  --  contact deleted";
    ASSERT_GT(a->getId(), 0u) << "ContactChange  --  contact deleted";
    ASSERT_EQ(a->getType(), MegaUserAlert::TYPE_CONTACTCHANGE_DELETEDYOU) << "ContactChange  --  contact deleted";
    ASSERT_STREQ(a->getTypeString(), "CONTACT_DISCONNECTED") << "ContactChange  --  contact deleted";
    ASSERT_STRCASEEQ(a->getHeading(), A1dtls.email.c_str()) << "ContactChange  --  contact deleted";
    ASSERT_NE(a->getTimestamp(0), 0) << "ContactChange  --  contact deleted";
    ASSERT_FALSE(a->isOwnChange()) << "ContactChange  --  contact deleted";
    ASSERT_EQ(a->getUserHandle(), A1.getMyUserHandleBinary()) << "ContactChange  --  contact deleted";
    bkpAlerts.emplace_back(a->copy());
    ASSERT_FALSE(a->getSeen());

    B1dtls.userAlertsUpdated = false;
    B1dtls.userAlertList.reset();
    ASSERT_EQ(doAckUserAlerts(B1idx), API_OK);
    ASSERT_TRUE(waitForResponse(&B1dtls.userAlertsUpdated))
        << "Alert about contact deleted not received by B1 after " << maxTimeout << " seconds";
    ASSERT_NE(B1dtls.userAlertList, nullptr) << "ContactChange  --  contact deleted";
    for (int i = 0; i < B1dtls.userAlertList->size(); ++i)
    {
        if (B1dtls.userAlertList->get(i)->isRemoved()) continue;
        ASSERT_TRUE(B1dtls.userAlertList->get(i)->getSeen());
    }

    // create a dummy folder, just to trigger a local db commit before locallogout (which triggers a ROLLBACK)
    std::unique_ptr<MegaNode> rootnodeB1{ B1.getRootNode() };
    PerApi& target = mApi[static_cast<size_t>(B1idx)];
    target.resetlastEvent();
    MegaHandle hDummyFolder = createFolder(B1idx, "DummyFolder_TriggerDbCommit", rootnodeB1.get());
    ASSERT_NE(hDummyFolder, INVALID_HANDLE);
    ASSERT_TRUE(WaitFor([&target]() { return target.lastEventsContain(MegaEvent::EVENT_COMMIT_DB); }, 8192));

    // save session for B1
    unique_ptr<char[]> B1session(B1.dumpSession());
    auto logoutErr = doRequestLocalLogout(B1idx);
    ASSERT_EQ(API_OK, logoutErr) << "Local logout failed (error: " << logoutErr << ") for account " << B1idx << " (B1)";

    // resume session for B1
    ASSERT_EQ(API_OK, synchronousFastLogin(B1idx, B1session.get(), this))
        << "Resume session failed for B1 (error: " << B1dtls.lastError << ")";
    ASSERT_NO_FATAL_FAILURE(fetchnodes(B1idx));
    unique_ptr<MegaUserAlertList> persistedAlerts(B1.getUserAlerts());
    ASSERT_TRUE(persistedAlerts);
    if (persistedAlerts->size() != (int)bkpAlerts.size())
    {
        // for debugging purpose
        string alertTypes = "\nPersisted Alerts: { ";
        for (int i = 0; i < persistedAlerts->size(); ++i)
            alertTypes += std::to_string(persistedAlerts->get(i)->getType()) + ' ';
        alertTypes += " }\nBacked up Alerts: { ";
        for (size_t i = 0u; i < bkpAlerts.size(); ++i)
            alertTypes += std::to_string(bkpAlerts[i]->getType()) + ' ';
        alertTypes += " }";
        LOG_err << "Persisted Alerts differ from Backed up ones:" << alertTypes;
    }
    ASSERT_EQ(persistedAlerts->size(), (int)bkpAlerts.size()); // B1 will not get sc50 alerts, due to its useragent

    // sort persisted alerts in the same order as backed up ones; timestamp is not enough because there can be clashes
    vector<const MegaUserAlert*> sortedPersistedAlerts(bkpAlerts.size(), nullptr);
    for (int i = 0; i < persistedAlerts->size(); ++i)
    {
        const auto* pal = persistedAlerts->get(i);

        auto it = find_if(bkpAlerts.begin(), bkpAlerts.end(), [pal](const unique_ptr<MegaUserAlert>& a)
            {
                return a->getTimestamp(0) == pal->getTimestamp(0) && a->getType() == pal->getType();
            });

        if (it != bkpAlerts.end())
        {
            sortedPersistedAlerts[static_cast<size_t>(it - bkpAlerts.begin())] = pal;
        }
    }

    // validate persisted alerts
    for (size_t j = 0; j < bkpAlerts.size(); ++j)
    {
        const auto* bkp = bkpAlerts[j].get();
        const auto* pal = sortedPersistedAlerts[j];
        ASSERT_TRUE(pal) << "Test error: some alerts were not persited or got lost while sorting: " << bkp->getTypeString();

        ASSERT_EQ(bkp->getType(), pal->getType()) << "persisted alerts: " << bkp->getTypeString() << " vs " << pal->getTypeString();
        ASSERT_STREQ(bkp->getTypeString(), pal->getTypeString()) << "persisted alerts: " << pal->getTypeString();
        ASSERT_EQ(bkp->getUserHandle(), pal->getUserHandle()) << "persisted alerts: " << pal->getTypeString();
        ASSERT_EQ(bkp->getNodeHandle(), pal->getNodeHandle()) << "persisted alerts: " << pal->getTypeString();
        ASSERT_EQ(bkp->getPcrHandle(), pal->getPcrHandle()) << "persisted alerts: " << pal->getTypeString();
        ASSERT_STRCASEEQ(bkp->getEmail(), pal->getEmail()) << "persisted alerts: " << pal->getTypeString();
        if (pal->getPath()) // the node might no longer be there after persisted alerts have been loaded
        {
            ASSERT_STRCASEEQ(bkp->getPath(), pal->getPath()) << "persisted alerts: " << pal->getTypeString();
        }
        if (pal->getName()) // the node might no longer be there after persisted alerts have been loaded
        {
            ASSERT_STREQ(bkp->getName(), pal->getName()) << "persisted alerts: " << pal->getTypeString();
        }
        ASSERT_STRCASEEQ(bkp->getHeading(), pal->getHeading()) << "persisted alerts: " << pal->getTypeString();
        ASSERT_STRCASEEQ(bkp->getTitle(), pal->getTitle()) << "persisted alerts: " << pal->getTypeString();
        ASSERT_EQ(bkp->getNumber(0), pal->getNumber(0)) << "persisted alerts: " << pal->getTypeString();
        ASSERT_EQ(bkp->getNumber(1), pal->getNumber(1)) << "persisted alerts: " << pal->getTypeString();
        ASSERT_EQ(bkp->getTimestamp(0), pal->getTimestamp(0)) << "persisted alerts: " << pal->getTypeString();
        ASSERT_STRCASEEQ(bkp->getString(0), pal->getString(0)) << "persisted alerts: " << pal->getTypeString();
        ASSERT_EQ(bkp->getHandle(0), pal->getHandle(0)) << "persisted alerts: " << pal->getTypeString();
        ASSERT_EQ(bkp->getHandle(1), pal->getHandle(1)) << "persisted alerts: " << pal->getTypeString();
    }
}

/**
 * ___SdkVersionManagement___
 * Steps:
 * - Create 2 folders
 * - Upload several versions of the same file to first folder
 * - Move file with versions to second folder
 * - Move second folder to first folder
 * - Remove current version
 * - Remove oldest version
 * - Remove version in the middle
 * - Remove node in the middle (and all previous versions)
 * - Remove all versions across entire account; will keep only last version
 * - Delete a version by the API when limit was reached (chain must have 100 versions)
 */
TEST_F(SdkTest, SdkVersionManagement)
{
    LOG_info << "___TEST SdkVersionManagement";

    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    doSetFileVersionsOption(0, false); // enable versioning
    auto& api = megaApi[0];
    unique_ptr<MegaNode> rootNode(api->getRootNode());

    //  Create 2 folders
    bool check = false;
    mApi[0].mOnNodesUpdateCompletion = createOnNodesUpdateLambda(INVALID_HANDLE, MegaNode::CHANGE_TYPE_NEW, check);
    std::string folder1 = "Folder1";
    auto folder1Handle = createFolder(0, folder1.c_str(), rootNode.get());
    ASSERT_NE(folder1Handle, UNDEF);
    waitForResponse(&check);
    unique_ptr<MegaNode> folder1Node(api->getNodeByHandle(folder1Handle));
    ASSERT_TRUE(folder1Node);
    check = false;
    std::string folder2 = "Folder2";
    auto folder2Handle = createFolder(0, folder2.c_str(), rootNode.get());
    ASSERT_NE(folder2Handle, UNDEF);
    waitForResponse(&check);
    unique_ptr<MegaNode> folder2Node(api->getNodeByHandle(folder2Handle));
    ASSERT_TRUE(folder2Node);
    resetOnNodeUpdateCompletionCBs();

    auto upldSingleVersion = [this](const string& name, int version, MegaNode* folderNode, MegaHandle* fh)
    {
        string localName = name + '_' + std::to_string(version);
        createFile(localName, false, std::to_string(version));

        int result = doStartUpload(0, fh, localName.c_str(), folderNode,
                             name.c_str() /*fileName*/,
                             ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
                             nullptr /*appData*/,
                             false   /*isSourceTemporary*/,
                             false   /*startFirst*/,
                             nullptr /*cancelToken*/);
        deleteFile(localName);
        return result;
    };

    auto upldVersions = [upldSingleVersion](const string& name, int versions, MegaNode* folderNode, MegaHandle* fh)
    {
#define UPLOAD_SINGLE_THREAD 1
#if UPLOAD_SINGLE_THREAD
        for (int i = 0; i < versions - 1; ++i)
        {
            ASSERT_EQ(upldSingleVersion(name, i + 1, folderNode, nullptr), API_OK);
        }
        ASSERT_EQ(upldSingleVersion(name, versions, folderNode, fh), API_OK);
#else
        // This would be very nice to have. Unfortunately crashes occur while running multiple threads.
        assert(versions);

        std::vector<std::thread> tpool(std::min(6u, std::thread::hardware_concurrency()));
        std::vector<int> results(tpool.size(), 0);
        for (size_t i = 0; i < versions - 1; ++i)
        {
            if (i >= tpool.size())
            {
                tpool[i%tpool.size()].join();
                if (results[i] != API_OK)
                {
                    // retry another version?
                    //++versions;
                }
            }

            tpool[i % tpool.size()] = std::thread([&name, i, folderNode, &r = results[i], upldSingleVersion]()
            {
                r = upldSingleVersion(name, i + 1, folderNode, nullptr);
            });
        }

        for (size_t i = 0; i < (versions - 1) % tpool.size(); ++i)
        {
            tpool[i].join();
            EXPECT_EQ(results[i], API_OK) << "Version upload failed";
        }

        int r = upldSingleVersion(name, versions, folderNode, fh);
        EXPECT_EQ(r, API_OK) << "Version upload failed";
#endif
    };

    //  Upload several versions of the same file to first folder
    const int verCount = 10;
    MegaHandle fileHandle = 0;
    ASSERT_NO_FATAL_FAILURE(upldVersions(UPFILE, verCount, folder1Node.get(), &fileHandle));
    ASSERT_NE(fileHandle, INVALID_HANDLE);
    unique_ptr<MegaNode> fileNode(api->getNodeByHandle(fileHandle));
    ASSERT_TRUE(fileNode);
    unique_ptr<MegaNodeList> allVersions(api->getVersions(fileNode.get()));
    ASSERT_EQ(allVersions->size(), verCount);
    ASSERT_EQ(fileNode->getHandle(), allVersions->get(0)->getHandle());

    //  Move file with versions to second folder
    ASSERT_EQ(API_OK, doMoveNode(0, &fileHandle, fileNode.get(), folder2Node.get())) << "Cannot move file";
    string destinationPath = '/' + folder2 + '/' + UPFILE;
    for (int i = 0; i < allVersions->size(); ++i)
    {
        unique_ptr<char> filePath(api->getNodePath(allVersions->get(i)));
        ASSERT_STREQ(destinationPath.c_str(), filePath.get()) << "Wrong file path (1) for version " << (i + 1);
        destinationPath += '/' + UPFILE;
    }

    //  Move second folder to first folder
    ASSERT_EQ(API_OK, doMoveNode(0, &folder2Handle, folder2Node.get(), folder1Node.get())) << "Cannot move folder";
    destinationPath = '/' + folder1 + '/' + folder2 + '/' + UPFILE;
    for (int i = 0; i < allVersions->size(); ++i)
    {
        unique_ptr<char> filePath(api->getNodePath(allVersions->get(i)));
        ASSERT_STREQ(destinationPath.c_str(), filePath.get()) << "Wrong file path (2) for version " << (i + 1);
        destinationPath += '/' + UPFILE;
    }
    folder2Node.reset(api->getNodeByHandle(folder2Handle));
    ASSERT_TRUE(folder2Node);

    //  Remove current version
    ASSERT_EQ(API_OK, doRemoveVersion(0, allVersions->get(0)));
    int verRemoved = 1;
    unique_ptr<MegaNodeList> versionsAfterRemoval(api->getVersions(allVersions->get(1)));
    ASSERT_EQ(versionsAfterRemoval->size(), verCount - verRemoved);
    for (int i = 0; i < versionsAfterRemoval->size(); ++i)
    {
        ASSERT_EQ(versionsAfterRemoval->get(i)->getHandle(), allVersions->get(i + 1)->getHandle()) << "i = " << i;
    }

    //  Remove oldest version
    ASSERT_EQ(API_OK, doRemoveVersion(0, allVersions->get(verCount - 1)));
    ++verRemoved;
    versionsAfterRemoval.reset(api->getVersions(allVersions->get(1)));
    ASSERT_EQ(versionsAfterRemoval->size(), verCount - verRemoved);
    for (int i = 0; i < versionsAfterRemoval->size(); ++i)
    {
        ASSERT_EQ(versionsAfterRemoval->get(i)->getHandle(), allVersions->get(i + 1)->getHandle()) << "i = " << i;
    }

    //  Remove version in the middle
    ASSERT_GT(versionsAfterRemoval->size(), 2) << "Not enough versions to test further";
    int middle = (verCount + 1) / 2;
    ASSERT_EQ(API_OK, doRemoveVersion(0, allVersions->get(middle)));
    ++verRemoved;
    versionsAfterRemoval.reset(api->getVersions(allVersions->get(1)));
    ASSERT_EQ(versionsAfterRemoval->size(), verCount - verRemoved);
    for (int i = 0; i < versionsAfterRemoval->size(); ++i)
    {
        int j = i < middle - 1 ? 1 : 2;
        ASSERT_EQ(versionsAfterRemoval->get(i)->getHandle(), allVersions->get(i + j)->getHandle()) << "i = " << i;
    }

    //  Remove node in the middle (and all previous versions)
    ASSERT_GT(versionsAfterRemoval->size(), 2) << "Not enough versions to test further";
    middle = (versionsAfterRemoval->size() + 1) / 2;
    ASSERT_EQ(API_OK, doDeleteNode(0, versionsAfterRemoval->get(middle)));
    versionsAfterRemoval.reset(api->getVersions(allVersions->get(1)));
    ASSERT_EQ(versionsAfterRemoval->size(), middle);
    for (int i = 0; i < versionsAfterRemoval->size(); ++i)
    {
        ASSERT_EQ(versionsAfterRemoval->get(i)->getHandle(), allVersions->get(i + 1)->getHandle()) << "i = " << i;
    }

    //  Remove all versions across entire account; will keep only last version
    ASSERT_GT(versionsAfterRemoval->size(), 1) << "Not enough versions to test further";
    mApi[0].mOnNodesUpdateCompletion = createOnNodesUpdateLambda(versionsAfterRemoval->get(1)->getHandle(), MegaNode::CHANGE_TYPE_REMOVED, check);
    ASSERT_EQ(API_OK, doRemoveVersions(0));
    waitForResponse(&check);
    resetOnNodeUpdateCompletionCBs();
    versionsAfterRemoval.reset(api->getVersions(allVersions->get(1)));
    ASSERT_EQ(versionsAfterRemoval->size(), 1);
    ASSERT_EQ(versionsAfterRemoval->get(0)->getHandle(), allVersions->get(1)->getHandle());


    //  Delete a version by the API when limit was reached (chain must have 100 versions)
    doSetFileVersionsOption(0, false); // enable versioning
    int verLimit = 102;
    ASSERT_NO_FATAL_FAILURE(upldVersions(UPFILE, verLimit, folder1Node.get(), &fileHandle));
    fileNode.reset(api->getNodeByHandle(fileHandle));
    allVersions.reset(api->getVersions(fileNode.get()));
    ASSERT_EQ(allVersions->size(), verLimit);
    // upload one more version
    ASSERT_EQ(upldSingleVersion(UPFILE, verLimit + 1, folder1Node.get(), &fileHandle), API_OK);
    fileNode.reset(api->getNodeByHandle(fileHandle));
    allVersions.reset(api->getVersions(fileNode.get()));
    ASSERT_EQ(allVersions->size(), verLimit);
}

/**
 * ___SdkGetNodesByName___
 * Steps:
 * - Create tree structure
 * - Get node with exact name (all cloud)
 * - Get node with name in upper case (all cloud)
 * - Get node with a string with wild cards (all cloud)
 * - Get node with a string with wild cards and lower case (all cloud)
 * - Get node with a string with wild card  (all cloud)
 * - Get node with exact name searching in a folder
 * - Get node with exact name searching in a folder (no recursive)
 * - Get node with exact name searching in a folder (no recursive) -> mismatch
 * - Get node with exact name searching in a folder (node is a folder)
 * - Get node with a string with wild cards in a folder (no recursive)
 * - Create contacts
 * - Share folder
 * - Get nodes by name recursively inside in share
 * - Get in shares by name (no recursive)
 * - Get in shares by name (no recursive) -> mismatch
 * - Get nodes by name recursively inside out share
 * - Get out share by name (no recursive)
 * - Get out share by name (no recursive) -> mismatch
 * - Get nodes with utf8 characters insensitive case
 * - Get nodes with accent insensitive case
 */
TEST_F(SdkTest, SdkGetNodesByName)
{
    LOG_info << "___TEST SdkGetNodesByName";

    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(2));
    unique_ptr<MegaNode> rootnode(megaApi[0]->getRootNode());
    ASSERT_TRUE(rootnode);

    // Check if exists nodes with that name in the cloud
    std::string stringSearch = "check";
    std::unique_ptr<MegaSearchFilter> filterResults(MegaSearchFilter::createInstance());
    filterResults->byName("check");
    std::unique_ptr<MegaNodeList> nodeList(megaApi[0]->search(filterResults.get()));
    int nodesWithTest = nodeList->size();

    bool check = false;
    mApi[0].mOnNodesUpdateCompletion = createOnNodesUpdateLambda(INVALID_HANDLE, MegaNode::CHANGE_TYPE_NEW, check);
    std::string folder = "Folder1";
    auto folderHandle = createFolder(0, folder.c_str(), rootnode.get());
    ASSERT_NE(folderHandle, UNDEF);
    waitForResponse(&check);
    unique_ptr<MegaNode> folder1(megaApi[0]->getNodeByHandle(folderHandle));
    ASSERT_TRUE(folder1);
    resetOnNodeUpdateCompletionCBs();

    mApi[0].mOnNodesUpdateCompletion = createOnNodesUpdateLambda(INVALID_HANDLE, MegaNode::CHANGE_TYPE_NEW, check);
    std::string folder1_1 = "Folder1_1Check";
    auto folder1_1Handle = createFolder(0, folder1_1.c_str(), folder1.get());
    ASSERT_NE(folderHandle, UNDEF);
    waitForResponse(&check);
    unique_ptr<MegaNode> folder1_1Test(megaApi[0]->getNodeByHandle(folder1_1Handle));
    ASSERT_TRUE(folder1_1Test);
    resetOnNodeUpdateCompletionCBs();
    mApi[0].mOnNodesUpdateCompletion = createOnNodesUpdateLambda(INVALID_HANDLE, MegaNode::CHANGE_TYPE_NEW, check);
    std::string file1 = "file1Check";
    createFile(file1, false);
    MegaHandle file1Handle = 0;
    ASSERT_EQ(MegaError::API_OK, doStartUpload(0, &file1Handle, file1.data(), folder1.get(),
                                               nullptr /*fileName*/,
                                               ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
                                               nullptr /*appData*/,
                                               false   /*isSourceTemporary*/,
                                               false   /*startFirst*/,
                                               nullptr /*cancelToken*/)) << "Cannot upload a test file";

    waitForResponse(&check);
    deleteFile(file1);
    // important to reset
    resetOnNodeUpdateCompletionCBs();
    unique_ptr<MegaNode> nodeFile(megaApi[0]->getNodeByHandle(file1Handle));
    ASSERT_NE(nodeFile, nullptr) << "Cannot initialize 1 node for scenario (error: " << mApi[0].lastError << ")";
    mApi[0].mOnNodesUpdateCompletion = createOnNodesUpdateLambda(INVALID_HANDLE, MegaNode::CHANGE_TYPE_NEW, check);
    std::string file2 = "file2Check";
    createFile(file2, false);
    MegaHandle file2Handle = 0;
    ASSERT_EQ(MegaError::API_OK, doStartUpload(0, &file2Handle, file2.data(), folder1.get(),
                                               nullptr /*fileName*/,
                                               ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
                                               nullptr /*appData*/,
                                               false   /*isSourceTemporary*/,
                                               false   /*startFirst*/,
                                               nullptr /*cancelToken*/)) << "Cannot upload a test file";

    waitForResponse(&check);
    deleteFile(file2);
    // important to reset
    resetOnNodeUpdateCompletionCBs();
    nodeFile.reset(megaApi[0]->getNodeByHandle(file2Handle));
    ASSERT_NE(nodeFile, nullptr) << "Cannot initialize 2 node for scenario (error: " << mApi[0].lastError << ")";

    mApi[0].mOnNodesUpdateCompletion = createOnNodesUpdateLambda(INVALID_HANDLE, MegaNode::CHANGE_TYPE_NEW, check);
    std::string file3 = "file3Check";
    createFile(file3, false);
    MegaHandle file3Handle = 0;
    ASSERT_EQ(MegaError::API_OK, doStartUpload(0, &file3Handle, file3.data(), folder1.get(),
                                               nullptr /*fileName*/,
                                               ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
                                               nullptr /*appData*/,
                                               false   /*isSourceTemporary*/,
                                               false   /*startFirst*/,
                                               nullptr /*cancelToken*/)) << "Cannot upload a test file";

    waitForResponse(&check);
    deleteFile(file3);
    // important to reset
    resetOnNodeUpdateCompletionCBs();
    nodeFile.reset(megaApi[0]->getNodeByHandle(file3Handle));
    ASSERT_NE(nodeFile, nullptr) << "Cannot initialize 3 node for scenario (error: " << mApi[0].lastError << ")";

    mApi[0].mOnNodesUpdateCompletion = createOnNodesUpdateLambda(INVALID_HANDLE, MegaNode::CHANGE_TYPE_NEW, check);
    std::string file4 = "file4Check";
    createFile(file4, false);
    MegaHandle file4Handle = 0;
    ASSERT_EQ(MegaError::API_OK, doStartUpload(0, &file4Handle, file4.data(), folder1.get(),
                                               nullptr /*fileName*/,
                                               ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
                                               nullptr /*appData*/,
                                               false   /*isSourceTemporary*/,
                                               false   /*startFirst*/,
                                               nullptr /*cancelToken*/)) << "Cannot upload a test file";

    waitForResponse(&check);
    deleteFile(file4);
    // important to reset
    resetOnNodeUpdateCompletionCBs();
    nodeFile.reset(megaApi[0]->getNodeByHandle(file4Handle));
    ASSERT_NE(nodeFile, nullptr) << "Cannot initialize 4 node for scenario (error: " << mApi[0].lastError << ")";

    mApi[0].nodeUpdated = false;
    mApi[0].mOnNodesUpdateCompletion = createOnNodesUpdateLambda(INVALID_HANDLE, MegaNode::CHANGE_TYPE_NEW, mApi[0].nodeUpdated);
    std::string file5 = "file5Check";
    createFile(file5, false);
    MegaHandle file5Handle = 0;
    ASSERT_EQ(MegaError::API_OK, doStartUpload(0, &file5Handle, file5.data(), folder1.get(),
                                               nullptr /*fileName*/,
                                               ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
                                               nullptr /*appData*/,
                                               false   /*isSourceTemporary*/,
                                               false   /*startFirst*/,
                                               nullptr /*cancelToken*/)) << "Cannot upload a test file";

    waitForResponse(&check);
    deleteFile(file5);
    // important to reset
    resetOnNodeUpdateCompletionCBs();
    nodeFile.reset(megaApi[0]->getNodeByHandle(file5Handle));
    ASSERT_NE(nodeFile, nullptr) << "Cannot initialize 5 node for scenario (error: " << mApi[0].lastError << ")";

    mApi[0].mOnNodesUpdateCompletion = createOnNodesUpdateLambda(INVALID_HANDLE, MegaNode::CHANGE_TYPE_NEW, check);
    std::string file6 = "file6Check";
    createFile(file6, false);
    MegaHandle file6Handle = 0;
    ASSERT_EQ(MegaError::API_OK, doStartUpload(0, &file6Handle, file6.data(), rootnode.get(),
                                               nullptr /*fileName*/,
                                               ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
                                               nullptr /*appData*/,
                                               false   /*isSourceTemporary*/,
                                               false   /*startFirst*/,
                                               nullptr /*cancelToken*/)) << "Cannot upload a test file";

    waitForResponse(&check);
    deleteFile(file6);
    // important to reset
    resetOnNodeUpdateCompletionCBs();
    nodeFile.reset(megaApi[0]->getNodeByHandle(file6Handle));
    ASSERT_NE(nodeFile, nullptr) << "Cannot initialize 6 node for scenario (error: " << mApi[0].lastError << ")";

    mApi[0].mOnNodesUpdateCompletion = createOnNodesUpdateLambda(INVALID_HANDLE, MegaNode::CHANGE_TYPE_NEW, check);
    std::string file7 = "file7Check";
    createFile(file7, false);
    MegaHandle file7Handle = 0;
    ASSERT_EQ(MegaError::API_OK, doStartUpload(0, &file7Handle, file7.data(), folder1_1Test.get(),
                                               nullptr /*fileName*/,
                                               ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
                                               nullptr /*appData*/,
                                               false   /*isSourceTemporary*/,
                                               false   /*startFirst*/,
                                               nullptr /*cancelToken*/)) << "Cannot upload a test file";

    waitForResponse(&check);
    deleteFile(file7);
    // important to reset
    resetOnNodeUpdateCompletionCBs();
    nodeFile.reset(megaApi[0]->getNodeByHandle(file7Handle));
    ASSERT_NE(nodeFile, nullptr) << "Cannot initialize 7 node for scenario (error: " << mApi[0].lastError << ")";

    mApi[0].mOnNodesUpdateCompletion = createOnNodesUpdateLambda(INVALID_HANDLE, MegaNode::CHANGE_TYPE_NEW, check);
    std::string file8 = "file8Check";
    createFile(file8, false);
    MegaHandle file8Handle = 0;
    ASSERT_EQ(MegaError::API_OK, doStartUpload(0, &file8Handle, file8.data(), folder1_1Test.get(),
                                               nullptr /*fileName*/,
                                               ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
                                               nullptr /*appData*/,
                                               false   /*isSourceTemporary*/,
                                               false   /*startFirst*/,
                                               nullptr /*cancelToken*/)) << "Cannot upload a test file";

    waitForResponse(&check);
    deleteFile(file8);
    // important to reset
    resetOnNodeUpdateCompletionCBs();
    nodeFile.reset(megaApi[0]->getNodeByHandle(file8Handle));
    ASSERT_NE(nodeFile, nullptr) << "Cannot initialize 8 node for scenario (error: " << mApi[0].lastError << ")";

    mApi[0].nodeUpdated = false;
    mApi[0].mOnNodesUpdateCompletion = createOnNodesUpdateLambda(INVALID_HANDLE, MegaNode::CHANGE_TYPE_NEW, mApi[0].nodeUpdated);
    std::string fileUtf8 = "ñ01amÚ";
    createFile(fileUtf8, false);
    MegaHandle fileUtf8Handle = 0;
    ASSERT_EQ(MegaError::API_OK, doStartUpload(0, &fileUtf8Handle, fileUtf8.data(), folder1.get(),
                                               nullptr /*fileName*/,
                                               ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
                                               nullptr /*appData*/,
                                               false   /*isSourceTemporary*/,
                                               false   /*startFirst*/,
                                               nullptr /*cancelToken*/)) << "Cannot upload a test file";

    waitForResponse(&check);
    deleteFile(fileUtf8);
    // important to reset
    resetOnNodeUpdateCompletionCBs();
    nodeFile.reset(megaApi[0]->getNodeByHandle(fileUtf8Handle));
    ASSERT_NE(nodeFile, nullptr) << "Cannot initialize 5 node for scenario (error: " << mApi[0].lastError << ")";

    // Tree structure
    // Root node
    //   - Folder1
    //       - Folder1_1Check
    //            - file7Check
    //            - file8Check
    //       - file1Check
    //       - file2Check
    //       - file3Check
    //       - file4Check
    //       - file5Check
    //       - ñ01amÚ
    //   - file6Test

    stringSearch = file1;
    filterResults.reset(MegaSearchFilter::createInstance());
    filterResults->byName(stringSearch.c_str());
    nodeList.reset(megaApi[0]->search(filterResults.get()));
    ASSERT_EQ(nodeList->size(), 1) << *nodeList;
    ASSERT_EQ(nodeList->get(0)->getHandle(), file1Handle);

    filterResults.reset(MegaSearchFilter::createInstance());
    filterResults->byName("FILE2CHECK");
    nodeList.reset(megaApi[0]->search(filterResults.get()));
    ASSERT_EQ(nodeList->size(), 1) << *nodeList;
    ASSERT_EQ(nodeList->get(0)->getHandle(), file2Handle);

    stringSearch = "file*Check";
    filterResults.reset(MegaSearchFilter::createInstance());
    filterResults->byName(stringSearch.c_str());
    nodeList.reset(megaApi[0]->search(filterResults.get()));
    ASSERT_EQ(nodeList->size(), 8) << *nodeList;

    stringSearch = "file*check";
    filterResults.reset(MegaSearchFilter::createInstance());
    filterResults->byName(stringSearch.c_str());
    nodeList.reset(megaApi[0]->search(filterResults.get()));
    ASSERT_EQ(nodeList->size(), 8) << *nodeList;

    stringSearch = "*check";
    filterResults.reset(MegaSearchFilter::createInstance());
    filterResults->byName(stringSearch.c_str());
    nodeList.reset(megaApi[0]->search(filterResults.get()));
    ASSERT_EQ(nodeList->size(), 9 + nodesWithTest) << *nodeList;

    stringSearch = file1;
    filterResults.reset(MegaSearchFilter::createInstance());
    filterResults->byLocationHandle(folder1->getHandle());
    filterResults->byName(stringSearch.c_str());
    nodeList.reset(megaApi[0]->search(filterResults.get()));
    ASSERT_EQ(nodeList->size(), 1) << *nodeList;
    ASSERT_EQ(nodeList->get(0)->getHandle(), file1Handle);

    stringSearch = file1;
    filterResults.reset(MegaSearchFilter::createInstance());
    filterResults->byName(stringSearch.c_str());
    filterResults->byLocationHandle(folder1->getHandle());
    nodeList.reset(megaApi[0]->getChildren(filterResults.get()));
    ASSERT_EQ(nodeList->size(), 1) << *nodeList;
    ASSERT_EQ(nodeList->get(0)->getHandle(), file1Handle);

    stringSearch = file7;
    filterResults.reset(MegaSearchFilter::createInstance());
    filterResults->byLocationHandle(folder1->getHandle());
    filterResults->byName(stringSearch.c_str());
    nodeList.reset(megaApi[0]->getChildren(filterResults.get()));
    ASSERT_EQ(nodeList->size(), 0) << *nodeList;

    stringSearch = folder1_1;
    filterResults.reset(MegaSearchFilter::createInstance());
    filterResults->byLocationHandle(folder1->getHandle());
    filterResults->byName(stringSearch.c_str());
    nodeList.reset(megaApi[0]->getChildren(filterResults.get()));
    ASSERT_EQ(nodeList->size(), 1) << *nodeList;
    ASSERT_EQ(nodeList->get(0)->getHandle(), folder1_1Handle);

    stringSearch = std::string("file*check");
    filterResults.reset(MegaSearchFilter::createInstance());
    filterResults->byLocationHandle(folder1->getHandle());
    filterResults->byName(stringSearch.c_str());
    nodeList.reset(megaApi[0]->getChildren(filterResults.get()));
    ASSERT_EQ(nodeList->size(), 5) << *nodeList;

    // --- Create contact relationship ---
    std::string message = "Hi contact. Let's share some stuff";
    mApi[1].contactRequestUpdated = false;
    ASSERT_NO_FATAL_FAILURE(inviteContact(0, mApi[1].email, message, MegaContactRequest::INVITE_ACTION_ADD));
    ASSERT_TRUE(waitForResponse(&mApi[1].contactRequestUpdated))   // at the target side (auxiliar account)
        << "Contact request creation not received after " << maxTimeout << " seconds";

    ASSERT_NO_FATAL_FAILURE(getContactRequest(1, false));

    mApi[0].contactRequestUpdated = mApi[1].contactRequestUpdated = false;
    ASSERT_NO_FATAL_FAILURE(replyContact(mApi[1].cr.get(), MegaContactRequest::REPLY_ACTION_ACCEPT));
    ASSERT_TRUE(waitForResponse(&mApi[1].contactRequestUpdated))   // at the target side (auxiliar account)
        << "Contact request creation not received after " << maxTimeout << " seconds";
    ASSERT_TRUE(waitForResponse(&mApi[0].contactRequestUpdated))   // at the source side (main account)
        << "Contact request creation not received after " << maxTimeout << " seconds";

    mApi[1].cr.reset();

    if (gManualVerification)
    {
        if (!areCredentialsVerified(0, mApi[1].email)) {ASSERT_NO_FATAL_FAILURE(verifyCredentials(0, mApi[1].email));}
        if (!areCredentialsVerified(1, mApi[0].email)) {ASSERT_NO_FATAL_FAILURE(verifyCredentials(1, mApi[0].email));}
    }

    // --- Share a folder with User2 ---
    bool check1, check2;
    mApi[0].mOnNodesUpdateCompletion = createOnNodesUpdateLambda(folderHandle, MegaNode::CHANGE_TYPE_OUTSHARE, check1);
    mApi[1].mOnNodesUpdateCompletion = createOnNodesUpdateLambda(folderHandle, MegaNode::CHANGE_TYPE_INSHARE, check2);

    ASSERT_NO_FATAL_FAILURE(shareFolder(folder1.get(), mApi[1].email.c_str(), MegaShare::ACCESS_FULL));
    ASSERT_TRUE(waitForResponse(&check1))   // at the target side (main account)
        << "Node update not received after " << maxTimeout << " seconds";
    ASSERT_TRUE(waitForResponse(&check2))   // at the target side (auxiliar account)
        << "Node update not received after " << maxTimeout << " seconds";
    // important to reset
    resetOnNodeUpdateCompletionCBs();
    ASSERT_EQ(check1, true);
    ASSERT_EQ(check2, true);

    // Wait for the inshare node to be decrypted
    ASSERT_TRUE(WaitFor([this, &folder1]() { return unique_ptr<MegaNode>(megaApi[1]->getNodeByHandle(folder1->getHandle()))->isNodeKeyDecrypted(); }, 60*1000));

    // --- Test search in shares ---
    stringSearch = file8;
    filterResults.reset(MegaSearchFilter::createInstance());
    filterResults->byName(stringSearch.c_str());
    filterResults->byLocation(MegaApi::SEARCH_TARGET_INSHARE);
    nodeList.reset(megaApi[1]->search(filterResults.get()));
    ASSERT_EQ(nodeList->size(), 1) << *nodeList;

    stringSearch = "FILE*check";
    filterResults.reset(MegaSearchFilter::createInstance());
    filterResults->byName(stringSearch.c_str());
    filterResults->byLocation(MegaApi::SEARCH_TARGET_INSHARE);
    nodeList.reset(megaApi[1]->search(filterResults.get()));
    ASSERT_EQ(nodeList->size(), 7) << *nodeList;

    stringSearch = folder1_1;
    filterResults.reset(MegaSearchFilter::createInstance());
    filterResults->byName(stringSearch.c_str());
    filterResults->byLocation(MegaApi::SEARCH_TARGET_INSHARE);
    nodeList.reset(megaApi[1]->search(filterResults.get()));
    ASSERT_EQ(nodeList->size(), 1) << *nodeList;
    ASSERT_EQ(nodeList->get(0)->getHandle(), folder1_1Handle);

    stringSearch = "folder*";
    filterResults.reset(MegaSearchFilter::createInstance());
    filterResults->byName(stringSearch.c_str());
    filterResults->byLocation(MegaApi::SEARCH_TARGET_INSHARE);
    nodeList.reset(megaApi[1]->search(filterResults.get()));
    ASSERT_EQ(nodeList->size(), 2) << *nodeList;

    // --- Test search out shares ---
    stringSearch = file8;
    filterResults.reset(MegaSearchFilter::createInstance());
    filterResults->byName(stringSearch.c_str());
    filterResults->byLocation(MegaApi::SEARCH_TARGET_OUTSHARE);
    nodeList.reset(megaApi[0]->search(filterResults.get()));
    ASSERT_EQ(nodeList->size(), 1) << *nodeList;

    stringSearch = "FILE*check";
    filterResults.reset(MegaSearchFilter::createInstance());
    filterResults->byName(stringSearch.c_str());
    filterResults->byLocation(MegaApi::SEARCH_TARGET_OUTSHARE);
    nodeList.reset(megaApi[0]->search(filterResults.get()));
    ASSERT_EQ(nodeList->size(), 7) << *nodeList;

    stringSearch = folder1_1;
    filterResults.reset(MegaSearchFilter::createInstance());
    filterResults->byName(stringSearch.c_str());
    filterResults->byLocation(MegaApi::SEARCH_TARGET_OUTSHARE);
    nodeList.reset(megaApi[0]->search(filterResults.get()));
    ASSERT_EQ(nodeList->size(), 1) << *nodeList;
    ASSERT_EQ(nodeList->get(0)->getHandle(), folder1_1Handle);

    stringSearch = "folder*";
    filterResults.reset(MegaSearchFilter::createInstance());
    filterResults->byName(stringSearch.c_str());
    filterResults->byLocation(MegaApi::SEARCH_TARGET_OUTSHARE);
    nodeList.reset(megaApi[0]->search(filterResults.get()));
    ASSERT_EQ(nodeList->size(), 2) << *nodeList;

    // --- Test strings with UTF-8 characters
    stringSearch = "Ñ01am";
    filterResults.reset(MegaSearchFilter::createInstance());
    filterResults->byName(stringSearch.c_str());
    nodeList.reset(megaApi[0]->search(filterResults.get()));
    ASSERT_EQ(nodeList->size(), 1) << *nodeList;
    ASSERT_EQ(nodeList->get(0)->getHandle(), fileUtf8Handle);

    stringSearch = "ñ01am";
    filterResults.reset(MegaSearchFilter::createInstance());
    filterResults->byName(stringSearch.c_str());
    nodeList.reset(megaApi[0]->search(filterResults.get()));
    ASSERT_EQ(nodeList->size(), 1) << *nodeList;
    ASSERT_EQ(nodeList->get(0)->getHandle(), fileUtf8Handle);

    // No recursive search
    stringSearch = "Ñ01am";
    filterResults.reset(MegaSearchFilter::createInstance());
    filterResults->byName(stringSearch.c_str());
    filterResults->byLocationHandle(folder1->getHandle());
    nodeList.reset(megaApi[0]->getChildren(filterResults.get()));
    ASSERT_EQ(nodeList->size(), 1) << *nodeList;
    ASSERT_EQ(nodeList->get(0)->getHandle(), fileUtf8Handle);

    stringSearch = "01amú";
    filterResults.reset(MegaSearchFilter::createInstance());
    filterResults->byName(stringSearch.c_str());
    nodeList.reset(megaApi[0]->search(filterResults.get()));
    ASSERT_EQ(nodeList->size(), 1) << *nodeList;
    ASSERT_EQ(nodeList->get(0)->getHandle(), fileUtf8Handle);

    // --- Test strings with UTF-8 characters
    stringSearch = "n01am";
    filterResults.reset(MegaSearchFilter::createInstance());
    filterResults->byName(stringSearch.c_str());
    nodeList.reset(megaApi[0]->search(filterResults.get()));
    ASSERT_EQ(nodeList->size(), 1) << *nodeList;
    ASSERT_EQ(nodeList->get(0)->getHandle(), fileUtf8Handle);

    filterResults.reset(MegaSearchFilter::createInstance());
    filterResults->byName("FILè2CHèCK");
    nodeList.reset(megaApi[0]->search(filterResults.get()));
    ASSERT_EQ(nodeList->size(), 1) << *nodeList;
    ASSERT_EQ(nodeList->get(0)->getHandle(), file2Handle);
}

void SdkTest::testResumableTrasfers(const std::string& data, const size_t timeoutInSecs)
{
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    // Make sure our clients are working with pro plans.
    auto accountRestorer = elevateToPro(*megaApi[0]);
    ASSERT_EQ(result(accountRestorer), API_OK);

#ifdef MEGASDK_DEBUG_TEST_HOOKS_ENABLED
    [[maybe_unused]] m_off_t tProgressCompletedPreResume{0};
    [[maybe_unused]] m_off_t tProgressContiguousPreResume{0};
    globalMegaTestHooks.onProgressCompletedUpdate =
        ::mega::DebugTestHook::onProgressCompletedUpdate;
    globalMegaTestHooks.onProgressContiguousUpdate =
        ::mega::DebugTestHook::onProgressContiguousUpdate;
#endif

    // 1. Create ~16 MB file
    // 2. Upload file, with speed limit
    // 3. Logout / Login
    // 4. Check upload resumption
    // 5. Finish upload
    // 6. Download file, with speed limit
    // 7. Logout / Login
    // 8. Check download resumption

    // 1. Create ~data size MB file
    std::ofstream file(fs::u8path(UPFILE), ios::out);
    ASSERT_TRUE(file) << "Couldn't create " << UPFILE;
    for (int i = 0; i < 1000000; i++)
    {
        file << data;
    }
    ASSERT_EQ(file.tellp(), data.size() * 1000000) << "Wrong size for test file";
    file.close();

    // 2. Upload file, with speed limit
    RequestTracker ct(megaApi[0].get());
    megaApi[0]->setMaxConnections(1, &ct);
    ASSERT_EQ(API_OK, ct.waitForResult(60))
        << "setMaxConnections() failed or took more than 1 minute";

    std::unique_ptr<MegaNode> rootnode{megaApi[0]->getRootNode()};

    megaApi[0]->setMaxUploadSpeed(2000000);
    onTransferUpdate_progress = 0;
    TransferTracker ut(megaApi[0].get());
    megaApi[0]->startUpload(UPFILE.c_str(),
                            rootnode.get(),
                            nullptr /*fileName*/,
                            ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
                            nullptr /*appData*/,
                            false /*isSourceTemporary*/,
                            false /*startFirst*/,
                            nullptr /*cancelToken*/,
                            &ut /*listener*/);

    second_timer timer;
    m_off_t pauseThreshold = 9000000;
    while (!ut.finished && timer.elapsed() < timeoutInSecs &&
           onTransferUpdate_progress < pauseThreshold)
    {
        WaitMillisec(200);
    }

    ASSERT_FALSE(ut.finished) << "Upload ended too early, with " << ut.waitForResult();
    ASSERT_GT(onTransferUpdate_progress, 0) << "Nothing was uploaded";

    // 3. Logout / Login
    unique_ptr<char[]> session(dumpSession());
    ASSERT_NO_FATAL_FAILURE(locallogout());
    int result = ut.waitForResult();
    ASSERT_TRUE(result == API_EACCESS || result == API_EINCOMPLETE)
        << "Upload interrupted with unexpected code: " << result;

#ifdef MEGASDK_DEBUG_TEST_HOOKS_ENABLED
    tProgressCompletedPreResume = DebugTestHook::testProgressCompleted;
    tProgressContiguousPreResume = DebugTestHook::testProgressContiguous;

    [[maybe_unused]] m_off_t tProgressCompletedAfterResume{0};
    [[maybe_unused]] m_off_t tProgressContiguousAfterResume{0};
    [[maybe_unused]] std::atomic<bool> exitFlagAfterResume{false};
    DebugTestHook::testProgressCompleted = 0;
    DebugTestHook::testProgressContiguous = 0;
    onTransferStartCustomCb = [&tProgressCompletedAfterResume,
                               &tProgressContiguousAfterResume,
                               &exitFlagAfterResume](MegaTransfer* t) -> void
    {
        if (t)
        {
            tProgressCompletedAfterResume = t->getTransferredBytes();
        }
        tProgressContiguousAfterResume = DebugTestHook::testProgressContiguous;
        exitFlagAfterResume = true;
    };
#endif

    ASSERT_NO_FATAL_FAILURE(resumeSession(session.get()));
    ASSERT_NO_FATAL_FAILURE(fetchnodes(0));

#ifdef MEGASDK_DEBUG_TEST_HOOKS_ENABLED
    ASSERT_TRUE(WaitFor(std::bind(
                            [](const std::atomic<bool>& flag)
                            {
                                // wait for onTransferStart after login + fetchnodes
                                return flag.load();
                            },
                            std::cref(exitFlagAfterResume)),
                        60000));

    ASSERT_EQ(tProgressCompletedPreResume, tProgressCompletedAfterResume)
        << "Progress complete mismatch between logout and onTransferStart values (it shouldn't "
           "have changed)";

    ASSERT_EQ(tProgressContiguousPreResume, tProgressContiguousAfterResume)
        << "Progress contiguous mismatch between logout and onTransferStart values (it shouldn't "
           "have changed)";
#endif

    // 4. Check upload resumption
    timer.reset();
    unique_ptr<MegaTransferList> transfers(megaApi[0]->getTransfers(MegaTransfer::TYPE_UPLOAD));
    while ((!transfers || !transfers->size()) && timer.elapsed() < 20)
    {
        WaitMillisec(100);
        transfers.reset(megaApi[0]->getTransfers(MegaTransfer::TYPE_UPLOAD));
    }
    ASSERT_EQ(transfers->size(), 1)
        << "Upload ended before resumption was checked, or was not resumed after 20 seconds";
    MegaTransfer* upl = transfers->get(0);
    long long uplBytes = upl->getTransferredBytes();
    ASSERT_GT(uplBytes, pauseThreshold / 2)
        << "Upload appears to have been restarted instead of resumed";

    // 5. Finish upload
    megaApi[0]->setMaxUploadSpeed(-1);
    timer.reset();
    unique_ptr<MegaNode> cloudNode(
        megaApi[0]->getNodeByPathOfType(UPFILE.c_str(), rootnode.get(), MegaNode::TYPE_FILE));
    size_t maxAllowedToFinishUpload = timeoutInSecs;
    while (!cloudNode && timer.elapsed() < maxAllowedToFinishUpload)
    {
        WaitMillisec(500);
        cloudNode.reset(
            megaApi[0]->getNodeByPathOfType(UPFILE.c_str(), rootnode.get(), MegaNode::TYPE_FILE));
    }
    ASSERT_TRUE(cloudNode) << "Upload did not finish after " << maxAllowedToFinishUpload
                           << " seconds";

#ifdef MEGASDK_DEBUG_TEST_HOOKS_ENABLED
    tProgressCompletedPreResume = 0;
    tProgressContiguousPreResume = 0;
#endif

    // 6. Download file, with speed limit
    string downloadedFile = DOTSLASH + DOWNFILE;
    megaApi[0]->setMaxDownloadSpeed(2000000);
    onTransferUpdate_progress = 0;
    timer.reset();
    TransferTracker dt(megaApi[0].get());
    megaApi[0]->startDownload(
        cloudNode.get(),
        downloadedFile.c_str(),
        nullptr /*fileName*/,
        nullptr /*appData*/,
        false /*startFirst*/,
        nullptr /*cancelToken*/,
        MegaTransfer::COLLISION_CHECK_FINGERPRINT /*collisionCheck*/,
        MegaTransfer::COLLISION_RESOLUTION_NEW_WITH_N /* collisionResolution */,
        false /* undelete */,
        &dt /*listener*/);

    while (!dt.finished && timer.elapsed() < timeoutInSecs &&
           onTransferUpdate_progress < pauseThreshold)
    {
        WaitMillisec(200);
    }

    ASSERT_FALSE(dt.finished) << "Download ended too early, with " << dt.waitForResult();
    ASSERT_GT(onTransferUpdate_progress, 0) << "Nothing was downloaded";

    // 7. Logout / Login
    session.reset(dumpSession());
    ASSERT_NO_FATAL_FAILURE(locallogout());
    result = dt.waitForResult();
    ASSERT_TRUE(result == API_EACCESS || result == API_EINCOMPLETE)
        << "Download interrupted with unexpected code: " << result;

#ifdef MEGASDK_DEBUG_TEST_HOOKS_ENABLED
    tProgressCompletedPreResume = DebugTestHook::testProgressCompleted;
    tProgressContiguousPreResume = DebugTestHook::testProgressContiguous;

    tProgressCompletedAfterResume = 0;
    tProgressContiguousAfterResume = 0;
    exitFlagAfterResume = false;
    DebugTestHook::testProgressCompleted = 0;
    DebugTestHook::testProgressContiguous = 0;
    onTransferStartCustomCb = [&tProgressCompletedAfterResume,
                               &tProgressContiguousAfterResume,
                               &exitFlagAfterResume](MegaTransfer* t) -> void
    {
        if (t)
        {
            tProgressCompletedAfterResume = t->getTransferredBytes();
        }
        tProgressContiguousAfterResume = DebugTestHook::testProgressContiguous;
        exitFlagAfterResume = true;
    };
#endif

    ASSERT_NO_FATAL_FAILURE(resumeSession(session.get()));
    ASSERT_NO_FATAL_FAILURE(fetchnodes(0));

#ifdef MEGASDK_DEBUG_TEST_HOOKS_ENABLED
    ASSERT_TRUE(WaitFor(std::bind(
                            [](const std::atomic<bool>& flag)
                            {
                                // wait for onTransferStart after login + fetchnodes
                                return flag.load();
                            },
                            std::cref(exitFlagAfterResume)),
                        60000));

    ASSERT_EQ(tProgressCompletedPreResume, tProgressCompletedAfterResume)
        << "Progress complete mismatch between logout and onTransferStart values (it shouldn't "
           "have changed)";

    ASSERT_EQ(tProgressContiguousPreResume, tProgressContiguousAfterResume)
        << "Progress contiguous mismatch between logout and onTransferStart values (it shouldn't "
           "have changed)";
#endif

    // 8. Check download resumption
    timer.reset();
    transfers.reset(megaApi[0]->getTransfers(MegaTransfer::TYPE_DOWNLOAD));
    while ((!transfers || !transfers->size()) && timer.elapsed() < 20)
    {
        WaitMillisec(100);
        transfers.reset(megaApi[0]->getTransfers(MegaTransfer::TYPE_DOWNLOAD));
    }
    ASSERT_EQ(transfers->size(), 1)
        << "Download ended before resumption was checked, or was not resumed after 20 seconds";
    MegaTransfer* dnl = transfers->get(0);
    long long dnlBytes = dnl->getTransferredBytes();
    ASSERT_GT(dnlBytes, pauseThreshold / 2)
        << "Download appears to have been restarted instead of resumed";

    megaApi[0]->setMaxDownloadSpeed(-1);
}

TEST_F(SdkTest, SdkTestUploads)
{
    LOG_info << "___TEST Test Uploads___";
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    // Make sure our clients are working with pro plans.
    auto accountRestorer = elevateToPro(*megaApi[0]);
    ASSERT_EQ(result(accountRestorer), API_OK);

    const auto rootnode = std::unique_ptr<MegaNode>{megaApi[0]->getRootNode()};

    constexpr auto fileSize = 160000000;

    const auto create16MBFile = [&]()
    {
        deleteFile(UPFILE.c_str());
        std::ofstream file(fs::u8path(UPFILE), ios::out);
        ASSERT_TRUE(file) << "Couldn't create " << UPFILE;
        constexpr auto numLines = 10000000;
        constexpr auto lineStr = "160MB test file "; // 16 characters
        for (int l = 0; l < numLines; ++l)
        {
            file << lineStr;
        }
        const auto filePos = file.tellp();
        ASSERT_EQ(filePos, fileSize) << "Wrong size for test file";
        file.close();
    };

    const auto setMaxConnections = [&](const auto maxConnections)
    {
        ASSERT_EQ(API_OK, doSetMaxConnections(0, maxConnections));

        int gMaxConnections{-1};
        int gDirection{-1};
        ASSERT_EQ(API_OK, doGetMaxUploadConnections(0, gDirection, gMaxConnections));
        ASSERT_EQ(gMaxConnections, maxConnections);
        ASSERT_EQ(gDirection, PUT);
    };

    const auto uploadFile = [&](const int maxConnections)
    {
        LOG_debug << "[SdkTestUploads] Test run with maxConnections: " << maxConnections;

        ASSERT_NO_FATAL_FAILURE(create16MBFile());
        ASSERT_NO_FATAL_FAILURE(setMaxConnections(maxConnections));

        onTransferUpdate_progress = 0;
        onTransferUpdate_filesize = 0;
        mApi[0].transferFlags[MegaTransfer::TYPE_UPLOAD] = false;
        const auto& uploadStartTime = std::chrono::system_clock::now();
        TransferTracker ut(megaApi[0].get());
        megaApi[0]->startUpload(UPFILE.c_str(),
                                rootnode.get(),
                                nullptr /*fileName*/,
                                ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
                                nullptr /*appData*/,
                                false /*isSourceTemporary*/,
                                false /*startFirst*/,
                                nullptr /*cancelToken*/,
                                &ut /*listener*/);

        unsigned int transfer_timeout_in_seconds = 180;
        ASSERT_TRUE(waitForResponse(&mApi[0].transferFlags[MegaTransfer::TYPE_UPLOAD],
                                    transfer_timeout_in_seconds))
            << "Transfer upload time out (180 seconds)";
        ASSERT_EQ(API_OK, mApi[0].lastError)
            << "Cannot upload the test file (error: " << mApi[0].lastError << ")";
        const auto& uploadEndTime = std::chrono::system_clock::now();
        auto uploadTime =
            std::chrono::duration_cast<std::chrono::milliseconds>(uploadEndTime - uploadStartTime)
                .count();
        LOG_debug << "[SdkTestUploads] uploadTime = " << uploadTime << " ms, size = " << fileSize
                  << ", maxConnections = " << maxConnections
                  << " [speed = " << (((fileSize / uploadTime) * 1000) / 1024) << " KB/s]";
        ASSERT_GE(onTransferUpdate_filesize, 0u);
        ASSERT_TRUE(onTransferUpdate_progress == onTransferUpdate_filesize);
    };

    const std::vector<int> maxConnectionsVector = {6, 12, 16};

    ASSERT_NO_FATAL_FAILURE(
        std::for_each(maxConnectionsVector.begin(), maxConnectionsVector.end(), uploadFile));
}

/**
 * @brief TEST_F SdkResumableTrasfers
 *
 * Tests resumption for file upload and download.
 */
TEST_F(SdkTest, SdkResumableTrasfers)
{
    auto genStr = [](const size_t len) -> std::string
    {
        const std::string base = std::to_string(len) + " MB test file. ";
        std::string result;
        result.reserve(len);

        while (result.size() < len)
        {
            result += base;
        }

        result.resize(len);
        return result;
    };

    // Note: testResumableTrasfers limits maxConnections and max Upload/Download speed
    auto i = 0;
    const std::map<size_t, size_t> files = {{16, 120}, {19, 240}, {24, 300}};
    for (const auto& [fileSize, timeout]: files)
    {
        auto data = genStr(fileSize);
        LOG_info << "___TEST Resumable Trasfers. Iteration (" << ++i << ") FileSize ("
                 << data.size() << " MB)___";
        ASSERT_NO_FATAL_FAILURE(testResumableTrasfers(data, timeout));
    }
}

auto makeScopedDefaultPermissions(MegaApi& api, int directory, int file)
{
    auto previousDirectory = api.getDefaultFolderPermissions();
    auto previousFile = api.getDefaultFilePermissions();

    api.setDefaultFolderPermissions(directory);
    api.setDefaultFilePermissions(file);

    return makeScopedDestructor(
        [=, &api]()
        {
            api.setDefaultFolderPermissions(previousDirectory);
            api.setDefaultFilePermissions(previousFile);
        });
}

auto makeScopedMinimumPermissions(int directory, int file)
{
    using FSA = FileSystemAccess;

    FSA::setMinimumDirectoryPermissions(directory);
    FSA::setMinimumFilePermissions(file);

    return makeScopedDestructor(
        []()
        {
            FSA::setMinimumDirectoryPermissions(0700);
            FSA::setMinimumFilePermissions(0600);
        });
}

/**
 * @brief Test file permissions for a download when using megaApi->setDefaultFilePermissions.
 *
 * - Test 1: Control test. Default file permissions (0600).
 *         Expected: successful download and successul file opening for reading and writing.
 * - Test 2: Change file permissions: 0400. Only for reading.
 *         Expected successful download, unsuccessful file opening for reading and writing (only for reading)
 * - Test 3: Change file permissions: 0700. Read, write and execute.
 *         Expected: successful download and successul file opening for reading and writing.
 */
TEST_F(SdkTest, SdkTestFilePermissions)
{
    LOG_info << "___TEST SdkTestFilePermissions___";
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    std::unique_ptr<MegaNode> rootnode(megaApi[0]->getRootNode());

    // Create a new file
    string filename = "file_permissions_test.sdktest";
    ASSERT_TRUE(createFile(filename, false)) << "Couldn't create test file: '" << filename << "'";

    // Upload the file
    fs::path uploadPath = fs::current_path() / filename;
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
    std::unique_ptr<MegaNode> nimported(megaApi[0]->getNodeByHandle(uploadListener.resultNodeHandle));

    // Delete the local file
    deleteFile(filename.c_str());

    auto downloadFile = [this, &nimported, &filename]()
    {
        TransferTracker downloadListener(megaApi[0].get());
        megaApi[0]->startDownload(nimported.get(),
                                filename.c_str(),
                                nullptr  /*customName*/,
                                nullptr  /*appData*/,
                                false    /*startFirst*/,
                                nullptr  /*cancelToken*/,
                                MegaTransfer::COLLISION_CHECK_FINGERPRINT /*collisionCheck*/,
                                MegaTransfer::COLLISION_RESOLUTION_NEW_WITH_N /* collisionResolution */,
                                false    /* undelete */,
                                &downloadListener);
        return downloadListener.waitForResult();
    };

    auto openFile = [&filename](bool readF, bool writeF) -> bool
    {
        auto fsa = std::make_unique<FSACCESS_CLASS>();
        fs::path filePath = fs::current_path() / filename.c_str();
        LocalPath localfilePath = fspathToLocal(filePath);

        std::unique_ptr<FileAccess> plain_fopen_fa(fsa->newfileaccess(false));
        return plain_fopen_fa->fopen(localfilePath, readF, writeF, FSLogging::logOnError);
    };

    // TEST 1: Control test. Default file permissions (0600).
    // Expected: successful download and successul file opening for reading and writing.
    ASSERT_EQ(API_OK, downloadFile());
    ASSERT_TRUE(openFile(true, true)) << "Couldn't open file for read|write";
    deleteFile(filename.c_str());

    auto minimumPermissions = makeScopedMinimumPermissions(0700, 0400);

    // TEST 2: Change file permissions: 0400. Only for reading.
    // Expected successful download, unsuccessful file opening for reading and writing (only for reading)
    {
        auto permissions = makeScopedDefaultPermissions(*megaApi[0], 0700, 0400);
        ASSERT_EQ(API_OK, downloadFile());
        ASSERT_TRUE(openFile(true, false)) << "Couldn't open file for read";
#ifdef _WIN32
        // Files should be able to be opened: posix file permissions don't have any effect on
        // Windows.
        ASSERT_TRUE(openFile(true, true)) << "Couldn't open files for read|write";
#else
        ASSERT_FALSE(openFile(true, true))
            << "Could open files for read|write, while it shouldn't due to permissions";
#endif
        deleteFile(filename.c_str());
    }

    // TEST 3: Change file permissions: 0700. Read, write and execute.
    // Expected: successful download and successul file opening for reading and writing.
    {
        auto permissions = makeScopedDefaultPermissions(*megaApi[0], 0700, 0700);
        ASSERT_EQ(API_OK, downloadFile());
        ASSERT_TRUE(openFile(true, true)) << "Couldn't open files for read|write";
        deleteFile(filename.c_str());
    }
}

/**
 * @brief Test folder permissions for a download when using megaApi->setDefaultFolderPermissions.
 *
 *  Note: folder downloads use MegaFolderDownloadController, which has its own FileAccess object.
 *
 * - Test 1. Control test. Default folder permissions. Default file permissions.
 *           Expected a successful download and no issues when accessing the folder.
 * - Test 2. TEST 2. Change folder permissions: only read (0400). Default file permissions (0600).
 *           Folder permissions: 0400. Expected to fail with API_EINCOMPLETE (-13): request incomplete because it can't write on resource (affecting children, not the parent folder downloaded).
 *           Still, if there is any file children inside the folder, it won't be able able to be opened for reading and writing or even for reading only (because of the folder permissions: lack of the execution perm).
 * - Test 3: Restore folder permissions. Change file permissions: only read.
 *           Folder permissions: 0700. Expected a successful download and no issues when accessing the folder.
 *           File permissions: 0400. Expected result: cannot open files for R and W (perm: 0400 -> only read).
 * - Test 4: Default folder permissions. Restore file permissions.
 *           Folder permissions: 0700. Expected a successful download and no issues when accessing the folder.
 *           File permissions: 0600. Expected result: Can open files for R and W (perm: 0600 -> r and w).
 */
TEST_F(SdkTest, SdkTestFolderPermissions)
{
    LOG_info << "___TEST SdkTestFolderPermissions___";
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    // Create a new folder
    string foldername = "folder_permissions_test.sdktest.folder";
    fs::path folderpath = fs::current_path() / foldername;
    std::error_code ec;
    fs::remove_all(folderpath, ec);
    ASSERT_TRUE(!fs::exists(folderpath)) << "Directory already exists (and still exists after trying to remove it): '" << folderpath.u8string() << "'";
    fs::create_directories(folderpath);

    // Create a new file inside the new directory
    string filename = "file_permissions_test.sdktest";
    fs::path fileInFolderPath = folderpath / filename;
    ASSERT_TRUE(createFile(fileInFolderPath.u8string(), false)) << "Couldn't create test file in directory: '" << fileInFolderPath.u8string() << "'";

    // Upload the folder
    TransferTracker uploadListener(megaApi[0].get());
    megaApi[0]->startUpload(folderpath.u8string().c_str(),
                            std::unique_ptr<MegaNode>{megaApi[0]->getRootNode()}.get(),
                            nullptr /*fileName*/,
                            ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
                            nullptr /*appData*/,
                            false   /*isSourceTemporary*/,
                            false   /*startFirst*/,
                            nullptr /*cancelToken*/,
                            &uploadListener);
    ASSERT_EQ(API_OK, uploadListener.waitForResult());
    std::unique_ptr<MegaNode> nimported(megaApi[0]->getNodeByHandle(uploadListener.resultNodeHandle));
    int nimportedNumChildren = megaApi[0]->getNumChildren(nimported.get());
    EXPECT_EQ(nimportedNumChildren, 1) << "This folder should have 1 children (the file inside the folder) but it doesn't. Num children: '" << nimportedNumChildren << "'";

    // Delete the local folder
    deleteFolder(foldername);

    auto downloadFolder = [this, &nimported, &foldername]()
    {
        TransferTracker downloadListener(megaApi[0].get());
        megaApi[0]->startDownload(nimported.get(),
                                foldername.c_str(),
                                nullptr  /*customName*/,
                                nullptr  /*appData*/,
                                false    /*startFirst*/,
                                nullptr  /*cancelToken*/,
                                MegaTransfer::COLLISION_CHECK_FINGERPRINT /*collisionCheck*/,
                                MegaTransfer::COLLISION_RESOLUTION_NEW_WITH_N /* collisionResolution */,
                                false    /* undelete */,
                                &downloadListener);
        return downloadListener.waitForResult();
    };

    auto openFolderAndFiles = [this, &foldername, &nimported](bool readF, bool writeF) -> bool
    {
        auto fsa = std::make_unique<FSACCESS_CLASS>();
        fs::path dirPath = fs::current_path() / foldername.c_str();
        auto localDirPath = fspathToLocal(dirPath);

        bool openResult = true;
        std::unique_ptr<DirAccess> diropen_da(fsa->newdiraccess());
        if (diropen_da->dopen(&localDirPath, nullptr, false))
        {
            std::unique_ptr<MegaNodeList> childrenList(megaApi[0]->getChildren(nimported.get()));
            for (int childIndex = 0; (childIndex < childrenList->size()); childIndex++)
            {
                if (childrenList->get(childIndex)->isFile())
                {
                    auto filesa = std::make_unique<FSACCESS_CLASS>();
                    fs::path filePath = dirPath / childrenList->get(childIndex)->getName();
                    auto localfilePath = fspathToLocal(filePath);
                    std::unique_ptr<FileAccess> plain_fopen_fa(filesa->newfileaccess(false));
                    openResult &= plain_fopen_fa->fopen(localfilePath, readF, writeF, FSLogging::logOnError);
                }
            }
        }
        return openResult;
    };

    // TEST 1. Control test. Default folder permissions. Default file permissions.
    // Expected a successful download and no issues when accessing the folder.
    ASSERT_EQ(API_OK, downloadFolder());
    ASSERT_TRUE(openFolderAndFiles(true, true)) << "Couldn't open files for read|write";
    deleteFolder(foldername.c_str());

    auto minimumPermissions = makeScopedMinimumPermissions(0400, 0400);

    // TEST 2. Change folder permissions: only read (0400). Default file permissions (0600).
    // Folder permissions: 0400. Expected to fail with API_EINCOMPLETE (-13): request incomplete because it can't write on resource (affecting children, not the parent folder downloaded).
    // Still, if there is any file children inside the folder, it won't be able able to be opened for reading and writing or even for reading only (because of the folder permissions: lack of the execution perm).
    {
        auto permissions = makeScopedDefaultPermissions(*megaApi[0], 0400, 0600);
#ifdef _WIN32
        // Folder and files should be able to be opened: posix file/folder permissions don't have
        // any effect on Windows.
        ASSERT_EQ(API_OK, downloadFolder());
        ASSERT_TRUE(openFolderAndFiles(true, false)) << "Couldn't open files for read";
        ASSERT_TRUE(openFolderAndFiles(true, true)) << "Couldn't open files for read|write";
#else
        ASSERT_EQ(API_EINCOMPLETE, downloadFolder())
            << "Download should have failed as there are not enough permissions to write in the "
               "folder";
        ASSERT_FALSE(openFolderAndFiles(true, false))
            << "Could open files for read, while it shouldn't due to permissions";
        ASSERT_FALSE(openFolderAndFiles(true, true))
            << "Could open files for read|write, while it shouldn't due to permissions";
#endif
        deleteFolder(foldername.c_str());
    }

    // TEST 3. Restore folder permissions. Change file permissions: only read.
    // Folder permissions: 0700. Expected a successful download and no issues when accessing the folder.
    // File permissions: 0400. Expected result: cannot open files for R and W (perm: 0400 -> only read).
    {
        auto permissions = makeScopedDefaultPermissions(*megaApi[0], 0700, 0400);
        ASSERT_EQ(API_OK, downloadFolder());
        ASSERT_TRUE(openFolderAndFiles(true, false)) << "Couldn't open files for read";
#ifdef _WIN32
        ASSERT_TRUE(openFolderAndFiles(true, true)) << "Couldn't open files for read|write";
#else
        ASSERT_FALSE(openFolderAndFiles(true, true))
            << "Could open files for read|write, while it shouldn't due to permissions";
#endif
        deleteFolder(foldername.c_str());
    }

    // TEST 4. Default folder permissions. Restore file permissions.
    // Folder permissions: 0700. Expected a successful download and no issues when accessing the folder.
    // File permissions: 0600. Expected result: Can open files for R and W (perm: 0600 -> r and w).
    {
        auto permissions = makeScopedDefaultPermissions(*megaApi[0], 0700, 0600);
        ASSERT_EQ(API_OK, downloadFolder());
        ASSERT_TRUE(openFolderAndFiles(true, true)) << "Couldn't open files for read|write";
        deleteFolder(foldername.c_str());
    }
}

TEST_F(SdkTest, GetRecommendedProLevel)
{
    // see also unit test MegaApi.MegaApiImpl_calcRecommendedProLevel in ..>unit>MegaApi_test.cpp

    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    int level = -1;
    int err = synchronousGetRecommendedProLevel(0, level);
    ASSERT_EQ(err, API_OK) << "Get Recommended Pro Level failed: " << MegaError::getErrorString(err);
    err = synchronousGetPricing(0);
    ASSERT_EQ(err, API_OK) << "Get Pricing failed: " << MegaError::getErrorString(err);

    bool starterAvailable = false;
    for (int i = 0; i < mApi[0].mMegaPricing->getNumProducts(); ++i)
    {
        if (mApi[0].mMegaPricing->getProLevel(i) == MegaAccountDetails::ACCOUNT_TYPE_STARTER)
        {
            starterAvailable = true;
            break;
        }
    }

    ASSERT_TRUE(starterAvailable) << "Starter plan not available !";
    ASSERT_EQ(level, MegaAccountDetails::ACCOUNT_TYPE_STARTER);
}

/**
 * @brief Test JourneyID Tracking support and ViewID generation
 *
 * - Test JourneyID functionality (obtained from "ug"/"gmf" commands)
 *   and ViewID generation - Values used for tracking on API requests.
 * - Ref: SDK-2768 - User Journey Tracking Support
 *
 * TEST ViewID: Generate a ViewID and check the hex string obtained.
 *
 * Tests JourneyID:
 * Test 1: JourneyID before login (retrieved from "gmf" command)
 * Test 2: JourneyID after login (must be the same as the one loaded and cached from "gmf" command)
 * TEST 3: Full logout and login from a fresh instance - cache file is deleted - new JourneyID retrieved from "ug" command
 * TEST 4:  Unset tracking flag.
 * TEST 5:  Update journeyID with a new hex string - must keep the previous one - must set tracking flag.
 * TEST 6:  Update journeyID with another hex string - must keep the previous one - no values must be updated.
 * TEST 7:  Update journeyID with an empty string - tracking flag must be unset.
 * TEST 8:  Local logout, resume session and do a fetch nodes request - New JID value from Api: JourneyID must be the same, but now tracking flag must be set
 * TEST 9:  Full logout and login from the same instance - JourneyID is reset -
 *          A new JourneyID should be retrieved from the next "ug" command after login: must be different from the original retrieved on TEST 1A.
 *
 */
TEST_F(SdkTest, SdkTestJourneyTracking)
{
    LOG_info << "___TEST SdkTestJourneyTracking___";
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    // We need to access the MegaClient to test JourneyID functionality according to specifications.
    // JID values from ug/gmf commands affect the behavior (set/unset tracking flag, update JourneyID::mJidValue if it's empty, etc.)
    // We don't have TestInstruments or any other mechanism to change the command response results, so we cannot test this just with regular requests on the intermmediate layer.
    // Finally, JourneyID is used internally on the MegaClient, it's never shared with the apps, so we need to check its value directly from MegaClient.
    auto client{megaApi[0]->getClient()};

    //==================||
    //    Test ViewID   ||
    //==================||

    // Generate a ViewID (16-char hex string)
    auto viewIdCstr = megaApi[0]->generateViewId();
    auto viewId = string(viewIdCstr);
    ASSERT_FALSE (viewId.empty()) << "Invalid hex string for generated viewId - it's empty";
    constexpr size_t HEX_STRING_SIZE = 16;
    ASSERT_TRUE (viewId.size() == HEX_STRING_SIZE) << "Invalid hex string size for generated viewId (" << viewId.size() << ") - expected (" << HEX_STRING_SIZE << ") [ViewID: '" << viewId << "']";
    delete viewIdCstr;

    //=====================||
    //    Test JourneyID   ||
    //=====================||

    // TEST 1: JourneyID before login (retrieved from "gmf" command)
    auto initialJourneyId = client->getJourneyId(); // Check the actual JourneyID before the logout
    ASSERT_FALSE(initialJourneyId.empty()) << "There should be a valid initial JourneyID from the first login";;

    // Logout and GetMiscFlags()
    logout(0, false, maxTimeout);
    ASSERT_TRUE(client->getJourneyId().empty()) << "There shouldn't be any valid journeyId value after a full logout - values must be reset and cache file deleted";
    gSessionIDs[0] = "invalid";
    auto err = synchronousGetMiscFlags(0);
    ASSERT_EQ(API_OK, err) << "Get misc flags failed (error: " << err << ")";

    // Get a new JourneyId from "gmf" command
    auto journeyIdGmf = client->getJourneyId();
    ASSERT_FALSE(initialJourneyId == journeyIdGmf) << "The initial JourneyId (loaded from cache) cannot be equal to the new Journeyid (obtained from \"gmf\" command)";

    // TEST 2: JourneyID after login - must be the same than the one loaded and cached after "gmf" command
    auto trackerLogin1 = asyncRequestLogin(0, mApi[0].email.c_str(), mApi[0].pwd.c_str());
    ASSERT_EQ(API_OK, trackerLogin1->waitForResult()) << " Failed to establish a login/session for account " << 0;
    auto journeyIdAfterFirstLogin = client->getJourneyId();
    ASSERT_TRUE(journeyIdGmf == journeyIdAfterFirstLogin) << "JourneyId value after login must be the same than the previous journeyId loaded from cache";

    // Full logout and login from a fresh instance
    logout(0, false, maxTimeout);
    auto journeyIdAfterLogout = client->getJourneyId();
    ASSERT_TRUE(journeyIdAfterLogout.empty()) << "Wrong value returned from client->getJourneyId(). It should be empty, as JourneyID values are reset and cached file is deleted";
    gSessionIDs[0] = "invalid";
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));
    // New impl and client - we need to update pointers
    client = megaApi[0]->getClient();

    // TEST 3: Check JourneyID after login from a fresh instance with no cache file
    // A new JourneyID should have been retrieved from the initial "ug" command
    auto journeyId = client->getJourneyId();
    ASSERT_FALSE (journeyId.empty()) << "Invalid hex string for generated journeyId - it's empty";
    ASSERT_TRUE (journeyId.size() == MegaClient::JourneyID::HEX_STRING_SIZE) << "Invalid hex string size for generated journeyId (" << journeyId.size() << ") - expected (" << MegaClient::JourneyID::HEX_STRING_SIZE << ")";
    ASSERT_TRUE (client->trackJourneyId()) << "Wrong value for client->trackJourneyId() (false) - expected true: it has a value and tracking flag is ON";
    ASSERT_FALSE(journeyId == journeyIdAfterFirstLogin) << "JourneyId value after second login (obtained from \"ug\" command) cannot be the same as the previous journeyId value (obtained from \"gmf\" command - reset from cache)";

        // Lambda function for checking JourneyID values after updates
        // ref_id: A numeric value to identify the call
        // trackJourneyId: Whether client->trackJourneyId() should return true (stored value, tracking flag is set) or false (tracking flag is unset)
        auto checkJourneyId = [client, &journeyId] (int ref_id, bool trackJourneyId)
        {
            auto actualJourneyId = client->getJourneyId();
            ASSERT_TRUE (actualJourneyId == journeyId) << "[Jid " << ref_id << "] Wrong value for actual journeyId" << "(" << actualJourneyId << "). Expected to be equal to original journeyID (" << journeyId << ")";
            if (trackJourneyId)
            {
                ASSERT_TRUE (client->trackJourneyId()) << "[Jid " << ref_id << "] Wrong value for client->trackJourneyId() - expected TRUE: it has a value and tracking flag must be set";
            }
            else
            {
                ASSERT_FALSE (client->trackJourneyId()) << "[Jid " << ref_id << "] Wrong value for client->trackJourneyId() - expected FALSE: it has a value, but tracking flag must be unset";
            }
        };

    // TEST 4: Unset tracking flag
    // JourneyID must still be valid, but tracking flag must be set
    ASSERT_TRUE (client->setJourneyId("")) << "Wrong returned value for setJourneyId(\"\") - expected TRUE (updated): tracking flag should've been unset";
    checkJourneyId(4, false);

    // TEST 5: Update journeyID with a new hex string - must keep the previous one - must set tracking flag
    ASSERT_TRUE (client->setJourneyId("FF00FF00FF00FF00")) << "Wrong result for client->setJourneyId(\"FF00FF00FF00FF00\") - expected TRUE (updated): tracking flag should've been set";
    checkJourneyId(5, true);

    // TEST 6: Update journeyID with a another hex string - must keep the previous one - no changes, no cache updates -> setJourneyId should return false
    ASSERT_FALSE (client->setJourneyId("0000000000000001")) << "Wrong result for client->setJourneyId(\"0000000000000001\") - expected FALSE (not updated): neither journeyId value nor tracking flag should've been updated";
    checkJourneyId(6, true);

    // TEST 7: Update journeyID with an empty string - tracking flag must be unset
    ASSERT_TRUE (client->setJourneyId("")) << "Wrong result for client->setJourneyId(\"\") - expected TRUE: tracking flag should've been unset";
    checkJourneyId(7, false);

    // TEST 8: Locallogout, resume session and request to fetch nodes - New JID value from Api: JourneyID must be the same, but now tracking flag must be set
    unique_ptr<char[]> session(dumpSession());
    ASSERT_NO_FATAL_FAILURE(locallogout());
    ASSERT_NO_FATAL_FAILURE(resumeSession(session.get()));
    ASSERT_NO_FATAL_FAILURE(fetchnodes(0));
    checkJourneyId(8, true);

    // TEST 9: Full logout and login from the same instance - values must be reset and cache file deleted
    // A new JourneyID value should be retrieved from the next "ug" command after login
    ASSERT_NO_FATAL_FAILURE(logout(0, false, maxTimeout));
    gSessionIDs[0] = "invalid";
    auto trackerLogin2 = asyncRequestLogin(0, mApi[0].email.c_str(), mApi[0].pwd.c_str());
    ASSERT_EQ(API_OK, trackerLogin2->waitForResult()) << " Failed to establish a login/session for account " << 0;
    ASSERT_TRUE(client->getJourneyId().empty()) << "Wrong value returned from client->getJourneyId(). It should be empty, as JourneyID values are reset and cached file is deleted";

    ASSERT_NO_FATAL_FAILURE(fetchnodes(0)); // This will get a new JourneyID
    auto newJourneyId = client->getJourneyId();

    // New value should be different from the original JourneyID
    ASSERT_FALSE(newJourneyId == journeyId) << "Wrong result when comparing newJourneyId and old JourneyId. They should be different after a full logout - values are reset and cached file is deleted";
    journeyId = newJourneyId; // Update journeyId reference (captured in lambda functions)
    checkJourneyId(9, true);
}

/**
 * Make sure instances of RequestTracker disconnect themselves from the API.
 *
 * FIXME: Should be a unit test rather than an integration test.
 */
TEST_F(SdkTest, SdkTestListenerRemovedWhenRequestTrackerDestroyed)
{
    // Get our hands on a client we can play with.
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    // Convenience.
    auto* api = megaApi[0].get();

    // Instantiate request tracker.
    auto* tracker = new RequestTracker(api, nullptr);

    // Register tracker with the API.
    api->addRequestListener(tracker);

    // Destroy tracker.
    delete tracker;

    // Tracker should no longer be associated with the API.
    ASSERT_FALSE(api->removeRequestListener(tracker));
}

/**
 * SdkTestGetNodeByMimetype
 * Steps:
 * - Create files (test.txt, test.sh, test.pdf, test.json, test.ods, test.doc, test.org)
 * - Search for files of type text
 * - Search for files of type program
 * - Search for files of type pdf
 * - Search for files of type document
 * - Search for files of type misc
 * - Search for files of type spreadsheet
 */
TEST_F(SdkTest, SdkTestGetNodeByMimetype)
{
    LOG_info << "___TEST Get Node By Mimetypes___";
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    std::unique_ptr<MegaNode> rootnode{megaApi[0]->getRootNode()};
    ASSERT_NE(rootnode.get(), nullptr);

    ASSERT_TRUE(createFile(PUBLICFILE.c_str(), false)) << "Couldn't create " << PUBLICFILE;

    const char txtFile[] = "test.txt";
    MegaHandle handleTxtFile = UNDEF;
    ASSERT_EQ(MegaError::API_OK, doStartUpload(0, &handleTxtFile, PUBLICFILE.c_str(),
                                               rootnode.get(),
                                               txtFile /*fileName*/,
                                               ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
                                               nullptr /*appData*/,
                                               false   /*isSourceTemporary*/,
                                               false   /*startFirst*/,
                                               nullptr /*cancelToken*/)) << "Cannot upload " << PUBLICFILE << " as " << txtFile;

    const char progFile[] = "test.sh";
    MegaHandle handleCodeFile = UNDEF;
    ASSERT_EQ(MegaError::API_OK, doStartUpload(0, &handleCodeFile, PUBLICFILE.c_str(),
                                               rootnode.get(),
                                               progFile /*fileName*/,
                                               ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
                                               nullptr /*appData*/,
                                               false   /*isSourceTemporary*/,
                                               false   /*startFirst*/,
                                               nullptr /*cancelToken*/)) << "Cannot upload " << PUBLICFILE << " as " << progFile;

    const char pdfFile[] = "test.pdf";
    MegaHandle handlePdfFile = UNDEF;
    ASSERT_EQ(MegaError::API_OK, doStartUpload(0, &handlePdfFile, PUBLICFILE.c_str(),
                                               rootnode.get(),
                                               pdfFile /*fileName*/,
                                               ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
                                               nullptr /*appData*/,
                                               false   /*isSourceTemporary*/,
                                               false   /*startFirst*/,
                                               nullptr /*cancelToken*/)) << "Cannot upload " << PUBLICFILE << " as " << pdfFile;

    const char jsonFile[] = "test.json";
    MegaHandle handleJsonFile = UNDEF;
    ASSERT_EQ(MegaError::API_OK, doStartUpload(0, &handleJsonFile, PUBLICFILE.c_str(),
                                               rootnode.get(),
                                               jsonFile /*fileName*/,
                                               ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
                                               nullptr /*appData*/,
                                               false   /*isSourceTemporary*/,
                                               false   /*startFirst*/,
                                               nullptr /*cancelToken*/)) << "Cannot upload " << PUBLICFILE << " as " << jsonFile;

    const char spreadsheetFile[] = "test.ods";
    MegaHandle handleSpreadsheetFile = UNDEF;
    ASSERT_EQ(MegaError::API_OK, doStartUpload(0, &handleSpreadsheetFile, PUBLICFILE.c_str(),
                                               rootnode.get(),
                                               spreadsheetFile /*fileName*/,
                                               ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
                                               nullptr /*appData*/,
                                               false   /*isSourceTemporary*/,
                                               false   /*startFirst*/,
                                               nullptr /*cancelToken*/)) << "Cannot upload " << PUBLICFILE << " as " << spreadsheetFile;

    const char documentFile[] = "test.doc";
    MegaHandle handleDocumentFile = UNDEF;
    ASSERT_EQ(MegaError::API_OK, doStartUpload(0, &handleDocumentFile, PUBLICFILE.c_str(),
                                               rootnode.get(),
                                               documentFile /*fileName*/,
                                               ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
                                               nullptr /*appData*/,
                                               false   /*isSourceTemporary*/,
                                               false   /*startFirst*/,
                                               nullptr /*cancelToken*/)) << "Cannot upload " << PUBLICFILE << " as " << documentFile;

    const char orgFile[] = "test.org";
    MegaHandle handleOrgFile = UNDEF;
    ASSERT_EQ(MegaError::API_OK, doStartUpload(0, &handleOrgFile, PUBLICFILE.c_str(),
                                               rootnode.get(),
                                               orgFile /*fileName*/,
                                               ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
                                               nullptr /*appData*/,
                                               false   /*isSourceTemporary*/,
                                               false   /*startFirst*/,
                                               nullptr /*cancelToken*/)) << "Cannot upload " << PUBLICFILE << " as " << orgFile;

    const char unkownExtensionFile[] = "test.err";
    MegaHandle handleUnkownExtensionFile = INVALID_HANDLE;
    ASSERT_EQ(MegaError::API_OK, doStartUpload(0, &handleUnkownExtensionFile, PUBLICFILE.c_str(),
                                               rootnode.get(),
                                               unkownExtensionFile /*fileName*/,
                                               ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
                                               nullptr /*appData*/,
                                               false   /*isSourceTemporary*/,
                                               false   /*startFirst*/,
                                               nullptr /*cancelToken*/)) << "Cannot upload " << PUBLICFILE << " as " << unkownExtensionFile;
    std::unique_ptr<MegaNode> unkownExtensionNode(megaApi[0]->getNodeByHandle(handleUnkownExtensionFile));
    ASSERT_THAT(unkownExtensionNode, ::testing::NotNull());

    const char withouExtensionFile[] = "test";
    RequestTracker nodeCopyTracker(megaApi[0].get());
    megaApi[0]->copyNode(unkownExtensionNode.get(), rootnode.get(), withouExtensionFile, &nodeCopyTracker);
    ASSERT_EQ(API_OK, nodeCopyTracker.waitForResult())
        << "Could not copy " << unkownExtensionFile << " as " << withouExtensionFile;
    MegaHandle handleWithoutExtensionFile = nodeCopyTracker.getNodeHandle();

    std::unique_ptr<MegaSearchFilter> filterResults(MegaSearchFilter::createInstance());

    filterResults->byCategory(MegaApi::FILE_TYPE_PROGRAM);
    std::unique_ptr<MegaNodeList> nodeList(megaApi[0]->search(filterResults.get()));
    ASSERT_EQ(nodeList->size(), 1) << *nodeList;
    ASSERT_EQ(nodeList->get(0)->getHandle(), handleCodeFile);

    filterResults->byCategory(MegaApi::FILE_TYPE_PDF);
    nodeList.reset(megaApi[0]->search(filterResults.get()));
    ASSERT_EQ(nodeList->size(), 1) << *nodeList;
    ASSERT_EQ(nodeList->get(0)->getHandle(), handlePdfFile);

    filterResults->byCategory(MegaApi::FILE_TYPE_DOCUMENT);
    nodeList.reset(megaApi[0]->search(filterResults.get(), MegaApi::ORDER_DEFAULT_DESC));
    ASSERT_EQ(nodeList->size(), 3) << *nodeList;
    ASSERT_EQ(nodeList->get(0)->getHandle(), handleTxtFile);
    ASSERT_EQ(nodeList->get(1)->getHandle(), handleOrgFile);
    ASSERT_EQ(nodeList->get(2)->getHandle(), handleDocumentFile);

    filterResults->byCategory(MegaApi::FILE_TYPE_MISC);
    nodeList.reset(megaApi[0]->search(filterResults.get()));
    ASSERT_EQ(nodeList->size(), 1) << *nodeList;
    ASSERT_EQ(nodeList->get(0)->getHandle(), handleJsonFile);

    filterResults->byCategory(MegaApi::FILE_TYPE_SPREADSHEET);
    nodeList.reset(megaApi[0]->search(filterResults.get()));
    ASSERT_EQ(nodeList->size(), 1) << *nodeList;
    ASSERT_EQ(nodeList->get(0)->getHandle(), handleSpreadsheetFile);

    filterResults->byCategory(MegaApi::FILE_TYPE_ALL_DOCS); // any of {DOCUMENT, PDF, PRESENTATION, SPREADSHEET}
    nodeList.reset(megaApi[0]->search(filterResults.get(), MegaApi::ORDER_DEFAULT_ASC)); // order Alphabetical asc
    ASSERT_EQ(nodeList->size(), 5) << *nodeList;
    ASSERT_EQ(nodeList->get(0)->getHandle(), handleDocumentFile);
    ASSERT_EQ(nodeList->get(1)->getHandle(), handleSpreadsheetFile);
    ASSERT_EQ(nodeList->get(2)->getHandle(), handleOrgFile);
    ASSERT_EQ(nodeList->get(3)->getHandle(), handlePdfFile);
    ASSERT_EQ(nodeList->get(4)->getHandle(), handleTxtFile);

    filterResults->byCategory(MegaApi::FILE_TYPE_OTHERS); // none of {PHOTO, VIDEO, AUDIO, MISC, PROGRAM, DOCUMENT, PDF, PRESENTATION, SPREADSHEET}
    nodeList.reset(megaApi[0]->search(filterResults.get(), MegaApi::ORDER_DEFAULT_ASC)); // order Alphabetical asc
    ASSERT_EQ(nodeList->size(), 2) << *nodeList;
    ASSERT_EQ(nodeList->get(0)->getHandle(), handleWithoutExtensionFile);
    ASSERT_EQ(nodeList->get(1)->getHandle(), handleUnkownExtensionFile);

    deleteFile(PUBLICFILE);
}

/**
 * @brief TEST_F SdkTestMegaVpnCredentials
 *
 * Tests the MEGA VPN functionality.
 * This test is valid for both FREE and PRO testing accounts.
 * If the testing account is FREE, the request results are adjusted to the API error expected in those cases.
 *
 * 0) DELETE existing credentials. Generally, none should be present.
 * 1) GET the MEGA VPN regions.
 * 2) Choose one of the regions above to PUT a new VPN credential. It should return:
 *     - The SlotID where the credential has been created.
 *     - The User Public Key.
 *     - The credential string to be used for VPN connection.
 * 3-a) Check the MEGA VPN credentials. They should be valid.
 * 3-b) Check nonexistent MEGA VPN credentials. They should be invalid.
 * 4) GET the MEGA VPN credentials. Check the related fields for the returned slotID:
 *      - IPv4 and IPv6
 *      - DeviceID
 *      - ClusterID
 *      - Cluster Public Key
 * 5) DELETE the MEGA VPN credentials associated with the slotID used above.
 * 6) DELETE the MEGA VPN credentials from an unoccupied slot.
 * 7) DELETE the MEGA VPN credentials from an invalid slot.
 */
TEST_F(SdkTest, SdkTestMegaVpnCredentials)
{
    LOG_info << "___TEST SdkTestMegaVpnCredentials";
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    ASSERT_EQ(API_ENOENT, doDelVpnCredential(0, 149))  << "Wrong error when deleting VPN credentials from unused slotID 149";
    ASSERT_EQ(API_ENOENT, doDelVpnCredential(0, 200))  << "Wrong error when deleting VPN credentials from unused slotID 200";
    ASSERT_EQ(API_ENOENT, doDelVpnCredential(0, 3000)) << "Wrong error when deleting VPN credentials from unused slotID 3000";

    int result;

    // 0) Delete any existing credentials (most times there won't be any)
    {
        std::unique_ptr<MegaVpnCredentials> megaVpnCredentials;
        result = doGetVpnCredentials(0, megaVpnCredentials);

        if (result == API_OK)
        {
            ASSERT_NE(megaVpnCredentials, nullptr);

            // Get SlotIDs
            {
                std::unique_ptr<MegaIntegerList> slotIDsList;
                slotIDsList.reset(megaVpnCredentials->getSlotIDs());
                ASSERT_NE(slotIDsList, nullptr);
                ASSERT_NE(slotIDsList->size(), 0);

                for (int i = 0; i < slotIDsList->size(); ++i)
                {
                    int slotID = static_cast<int>(slotIDsList->get(i));
                    result = doDelVpnCredential(0, slotID);
                    ASSERT_EQ(API_OK, result) << "deleting the VPN credentials from the slotID " << slotID << " failed";
                }
            }

            // Check credentials again
            result = doGetVpnCredentials(0, megaVpnCredentials);
            ASSERT_EQ(API_ENOENT, result) << "there should not be any credentials left after deleting them";
            ASSERT_EQ(megaVpnCredentials, nullptr) << "credentials have been deleted, but MegaVpnCredentials object is not NULL";
        }
    }

    // 1) Get VPN regions to choose one of them
    {
        std::unique_ptr<MegaStringList> vpnRegions;
        std::unique_ptr<MegaVpnRegionList> vpnRegionsDetailed;
        result = doGetVpnRegions(0, vpnRegions, vpnRegionsDetailed);
        ASSERT_EQ(API_OK, result) << "getting the VPN regions failed";
        ASSERT_TRUE(vpnRegions) << "list of VPN regions is NULL";
        ASSERT_TRUE(vpnRegions->size()) << "list of VPN regions is empty";

        const char* vpnRegion = vpnRegions->get(0); // Select the first vpn region
        ASSERT_TRUE(vpnRegion) << "VPN region is NULL";
        ASSERT_TRUE(*vpnRegion) << "VPN region is EMPTY";

        auto testDetailedRegionList = [](const std::unique_ptr<MegaVpnRegionList>& regionList)
        {
            const MegaVpnRegion* region = regionList->get(0);
            ASSERT_THAT(region, testing::NotNull());
            ASSERT_STRNE(region->getName(), "");
            std::unique_ptr<MegaVpnClusterMap> clusters{region->getClusters()};
            ASSERT_THAT(clusters, testing::NotNull());
            ASSERT_GT(clusters->size(), 0);
            std::unique_ptr<MegaIntegerList> clusterIDs{clusters->getKeys()};
            ASSERT_THAT(clusterIDs, testing::NotNull());
            ASSERT_GT(clusterIDs->size(), 0);
            std::unique_ptr<MegaVpnCluster> firstCluster{clusters->get(clusterIDs->get(0))};
            ASSERT_THAT(firstCluster, testing::NotNull());
            ASSERT_THAT(firstCluster->getHost(), testing::NotNull());
            std::unique_ptr<MegaStringList> clusterDns{firstCluster->getDns()};
            ASSERT_THAT(clusterDns, testing::NotNull());
            ASSERT_GT(clusterDns->size(), 0);
            std::unique_ptr<MegaStringList> adBlockingDns{firstCluster->getAdBlockingDns()};
            ASSERT_THAT(adBlockingDns, testing::NotNull());
            ASSERT_GT(adBlockingDns->size(), 0);
            ASSERT_STRNE(region->getCountryCode(), "");
            ASSERT_STRNE(region->getCountryName(), "");
        };
        ASSERT_THAT(vpnRegionsDetailed, testing::NotNull());
        ASSERT_EQ(static_cast<int>(vpnRegionsDetailed->size()), vpnRegions->size());
        const MegaVpnRegion* region = vpnRegionsDetailed->get(0);
        ASSERT_THAT(region, testing::NotNull());
        ASSERT_STREQ(region->getName(), vpnRegion);
        ASSERT_NO_FATAL_FAILURE(testDetailedRegionList(vpnRegionsDetailed));

        // Get the PRO level for the testing account
        ASSERT_EQ(API_OK, synchronousGetSpecificAccountDetails(0, true, true, true)) << "Cannot get account details";
        bool isProAccount = (mApi[0].accountDetails->getProLevel() != MegaAccountDetails::ACCOUNT_TYPE_FREE);

        // 2) Put VPN credential on the chosen region
        int slotID = -1;
        std::string userPubKey;
        std::string newCredential;
        result = doPutVpnCredential(0, slotID, userPubKey, newCredential, vpnRegion);
        if (isProAccount)
        {
            ASSERT_EQ(API_OK, result) << "adding a new VPN credential failed";
            ASSERT_TRUE(slotID > 0) << "slotID should be greater than 0";
            size_t expectedPubKeyB64Size = ((ECDH::PUBLIC_KEY_LENGTH * 4) + 2) / 3; // URL-safe B64 length (no trailing '=')
            ASSERT_EQ(userPubKey.size(), expectedPubKeyB64Size) << "User Public Key does not have the expected size";
            ASSERT_FALSE(newCredential.empty()) << "VPN Credential data is EMPTY";

            // 3-a) Check the VPN credentials
            result = doCheckVpnCredential(0, userPubKey.c_str());
            ASSERT_EQ(API_OK, result) << "checking the VPN credentials failed";
        }
        else
        {
            ASSERT_EQ(API_EACCESS, result) << "adding a new VPN credential on a free account return wrong error";
            ASSERT_EQ(slotID, 0);
            ASSERT_EQ(API_EARGS, doDelVpnCredential(0, slotID)) << "deleting non-existing VPN credentials from slotID " << slotID << " returned wrong error";
        }

        // 3-b) Check nonexistent VPN credentials
        {
            string nonexistentUserPK = "obI7rWzm3qVQL5zOxHzv2XFHsP1kOOTR1mE7NluVjDM";
            result = doCheckVpnCredential(0, nonexistentUserPK.c_str());
            ASSERT_EQ(API_EACCESS, result) << "checking the VPN credentials with a nonexistent User Public Key should have returned API_EACCESS";
        }

        // 4) Get VPN credentials and search for the credential associated with the returned SlotID
        {
            std::unique_ptr<MegaVpnCredentials> megaVpnCredentials;
            result = doGetVpnCredentials(0, megaVpnCredentials);

            if (isProAccount)
            {
                ASSERT_EQ(API_OK, result) << "getting the VPN credentials failed";
                ASSERT_TRUE(megaVpnCredentials != nullptr) << "MegaVpnCredentials is NULL";

                // Get SlotIDs - it should not be empty
                {
                    std::unique_ptr<MegaIntegerList> slotIDsList;
                    slotIDsList.reset(megaVpnCredentials->getSlotIDs());
                    ASSERT_TRUE(slotIDsList) << "MegaIntegerList of slotIDs is NULL";
                    ASSERT_EQ(slotIDsList->size(), 1) << "slotIDs list should have 1 credential";
                }

                // Get IPv4
                const char* ipv4 = megaVpnCredentials->getIPv4(slotID);
                ASSERT_TRUE(ipv4) << "IPv4 value not found for SlotID: " << slotID;
                ASSERT_TRUE(*ipv4) << "IPv4 value is empty for SlotID: " << slotID;

                // Get IPv6
                const char* ipv6 = megaVpnCredentials->getIPv6(slotID);
                ASSERT_TRUE(ipv6) << "IPv6 value not found for SlotID: " << slotID;
                ASSERT_TRUE(*ipv6) << "IPv6 value is empty for SlotID: " << slotID;

                // Get DeviceID (it must be a valid pointer, but it can be empty if there's no associated deviceID)
                const char* deviceID = megaVpnCredentials->getDeviceID(slotID);
                ASSERT_TRUE(deviceID) << "deviceID not found for SlotID: " << slotID;

                // Get ClusterID
                int clusterID = megaVpnCredentials->getClusterID(slotID);
                ASSERT_TRUE(clusterID >= 0) << "clusterID should be a positive value. SlotID: " << slotID;

                // Get Cluster Public Key
                const char* clusterPublicKey = megaVpnCredentials->getClusterPublicKey(clusterID);
                ASSERT_TRUE(clusterPublicKey) << "Cluster Public Key not found for ClusterID: " << clusterID;
                ASSERT_TRUE(*clusterPublicKey) << "Cluster Public Key is empty for ClusterID: " << clusterID;

                // Check VPN regions, they should not be empty
                std::unique_ptr<MegaStringList> vpnRegionsFromCredentials;
                vpnRegionsFromCredentials.reset(megaVpnCredentials->getVpnRegions());
                ASSERT_TRUE(vpnRegionsFromCredentials) << "list of VPN regions is NULL";
                ASSERT_TRUE(vpnRegionsFromCredentials->size()) << "list of VPN regions is empty";
                std::unique_ptr<MegaVpnRegionList> vpnRegionsDetailed{
                    megaVpnCredentials->getVpnRegionsDetailed()};
                ASSERT_THAT(vpnRegionsDetailed, testing::NotNull())
                    << "list of detailed VPN regions was null";
                ASSERT_EQ(static_cast<int>(vpnRegionsDetailed->size()),
                          vpnRegionsFromCredentials->size());
                const MegaVpnRegion* regionDetailed = vpnRegionsDetailed->get(0);
                ASSERT_THAT(regionDetailed, testing::NotNull());
                ASSERT_STREQ(regionDetailed->getName(), vpnRegionsFromCredentials->get(0));
                ASSERT_NO_FATAL_FAILURE(testDetailedRegionList(vpnRegionsDetailed));
            }
            else
            {
                ASSERT_EQ(API_ENOENT, result) << "getting the VPN credentials for a free account returned wrong error";
                ASSERT_FALSE(megaVpnCredentials) << "MegaVpnCredentials is NOT NULL for a free account";
                ASSERT_EQ(slotID, 0);
                ASSERT_EQ(API_EARGS, doDelVpnCredential(0, slotID)) << "deleting non-existing VPN credentials from slotID " << slotID << " returned wrong error";
            }
        }

        if (isProAccount)
        {
            // 5) Delete VPN credentials from an occupied slot
            result = doDelVpnCredential(0, slotID);
            ASSERT_EQ(API_OK, result) << "deleting the VPN credentials from the slotID " << slotID << " failed";

            // Check again the VPN credentials, they should be invalid now
            result = doCheckVpnCredential(0, userPubKey.c_str());
            ASSERT_EQ(API_EACCESS, result) << "VPN credentials are still valid after being deleted";

            // Use the same slotID (it should be empty now) for the next test
        }
        else
        {
            slotID = 1; // Test the 1st slotID for a FREE account (must be empty)
        }

        // 6) Delete VPN credentials from an unoccupied slot. Expecting ENOENT: SlotID is empty
        result = doDelVpnCredential(0, slotID);
        ASSERT_EQ(API_ENOENT, result) << "deleting the VPN credentials from unused slotID " << slotID << " returned wrong error";

        // 7) Delete VPN credentials from an invalid slot. Expecting EARGS: SlotID is not valid
        slotID = -1;
        result = doDelVpnCredential(0, slotID);
        ASSERT_EQ(API_EARGS, result) << "deleting the VPN credential from the invalid slotID " << slotID << " didn't return the expected error value";
    }
}

#ifdef ENABLE_SYNC

/**
 * @brief TEST_F SdkTestMoveToSyncDebris
 *   - add syncs with folder and file
 *   - wait to check if file has been upload to cloud
 *   - move file to sync debris folder
 *   - wait to check file has been moved to sync debris cloud folder
 *   - remove sync
 */
TEST_F(SdkTest, SdkTestMoveToSyncDebris)
{
    LOG_info << "___TEST SdkTestMoveToSyncDebris___";
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    fs::path base = "SdkTestMoveToSyncDebris";
    std::string syncFolder = "sync1";
    fs::path basePath = base / syncFolder;
    const auto localPath = fs::current_path() / basePath;

    ASSERT_NO_FATAL_FAILURE(cleanUp(this->megaApi[0].get(), base));

    // Create local directories and a files.
    fs::create_directories(localPath);
    std::string fileName = "fileTest1";
    const auto filePath = localPath / fs::path(fileName);
    ASSERT_TRUE(createFile(filePath.u8string(), false));

    LOG_verbose << "Creating the remote folders to be synced to.";
    std::unique_ptr<MegaNode> remoteRootNode(megaApi[0]->getRootNode());
    ASSERT_NE(remoteRootNode.get(), nullptr);
    auto nh = createFolder(0, syncFolder.c_str(), remoteRootNode.get());
    ASSERT_NE(nh, UNDEF) << "Error creating remote folders";
    std::unique_ptr<MegaNode> remoteBaseNode1(megaApi[0]->getNodeByHandle(nh));
    ASSERT_NE(remoteBaseNode1.get(), nullptr);

    LOG_verbose << "Add syncs";
    bool check = false;
    mApi[0].mOnNodesUpdateCompletion = createOnNodesUpdateLambda(INVALID_HANDLE, MegaNode::CHANGE_TYPE_NEW, check);
    const auto& lp = localPath.u8string();
    ASSERT_EQ(API_OK, synchronousSyncFolder(0, nullptr, MegaSync::TYPE_TWOWAY, lp.c_str(), nullptr, remoteBaseNode1->getHandle(), nullptr)) << "API Error adding a new sync";
    ASSERT_EQ(MegaSync::NO_SYNC_ERROR, mApi[0].lastSyncError);
    std::unique_ptr<MegaSync> sync = waitForSyncState(megaApi[0].get(), remoteBaseNode1.get(), MegaSync::RUNSTATE_RUNNING, MegaSync::NO_SYNC_ERROR);
    ASSERT_TRUE(sync && sync->getRunState() == MegaSync::RUNSTATE_RUNNING);
    ASSERT_EQ(MegaSync::NO_SYNC_ERROR, sync->getError());
    waitForResponse(&check);
    resetOnNodeUpdateCompletionCBs();

    std::unique_ptr<MegaNode> file(megaApi[0]->getChildNode(remoteBaseNode1.get(), fileName.c_str()));
    ASSERT_NE(file, nullptr);

    handle backupId = sync->getBackupId();

    // Move file to local sync debris folder
    ASSERT_EQ(API_OK, syncMoveToDebris(0, filePath.u8string().c_str(), backupId));

    check = false;
    mApi[0].mOnNodesUpdateCompletion = createOnNodesUpdateLambda(file->getHandle(), MegaNode::CHANGE_TYPE_PARENT, check);
    waitForResponse(&check);
    resetOnNodeUpdateCompletionCBs();

    LOG_verbose << "SyncRemoveRemoteNode :  Remove Syncs that fail";
    {
        ASSERT_EQ(API_OK, synchronousRemoveSync(0, backupId)); // already removed.
    }

    ASSERT_NO_FATAL_FAILURE(cleanUp(this->megaApi[0].get(), base));
}

#endif // ENABLE_SYNC

/**
 * @brief SdkTesResumeSessionInFolderLinkDeleted
 *
 * Resume session in a folder link deleted and expect to be logged out.
 */
TEST_F(SdkTest, SdkTesResumeSessionInFolderLinkDeleted)
{
    // Configure folder owner test instance
    unsigned int numberOfTestInstances{1};
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(numberOfTestInstances));

    // Create folder
    const unsigned int folderOwnerApiIndex{0};
    std::unique_ptr<MegaNode> rootNode{megaApi[folderOwnerApiIndex]->getRootNode()};
    ASSERT_THAT(rootNode, ::testing::NotNull());

    const std::string folderName{"Test"};
    auto folderNodeHandle{createFolder(folderOwnerApiIndex, folderName.c_str(), rootNode.get())};
    ASSERT_NE(folderNodeHandle, UNDEF);

    std::unique_ptr<MegaNode> folderNode{megaApi[folderOwnerApiIndex]->getNodeByHandle(folderNodeHandle)};
    ASSERT_THAT(folderNode, ::testing::NotNull());

    // Create folder link
    const auto folderLink{createPublicLink(folderOwnerApiIndex, folderNode.get(), 0, maxTimeout, false)};

    // Configure folder visitor test instance
    numberOfTestInstances = 2;
    mApi.resize(numberOfTestInstances);
    megaApi.resize(numberOfTestInstances);

    unsigned int folderVisitorApiIndex{1};
    const bool checkCredentials{false};
    configureTestInstance(folderVisitorApiIndex, "", "", checkCredentials);

    // Login into folder link
    auto requestTracker{asyncRequestLoginToFolder(folderVisitorApiIndex, folderLink.c_str())};
    ASSERT_EQ(requestTracker->waitForResult(), API_OK) << "Failed to login to folder " << folderLink;
    ASSERT_NO_FATAL_FAILURE(fetchnodes(folderVisitorApiIndex));

    // Get session
    unique_ptr<char[]> session{megaApi[folderVisitorApiIndex]->dumpSession()};

    // Local logout
    locallogout(folderVisitorApiIndex);

    // Remove folder link
    removePublicLink(folderOwnerApiIndex, folderNode.get());

    // Login into folder link
    const auto requestFlagType{MegaRequest::TYPE_LOGOUT};
    auto& requestFlag{mApi[folderVisitorApiIndex].requestFlags[requestFlagType]};
    requestFlag = false;

    ASSERT_EQ(synchronousFastLogin(folderVisitorApiIndex, session.get(), this), API_OK);
    ASSERT_NO_FATAL_FAILURE(fetchnodes(folderVisitorApiIndex));

    const unsigned int timeoutInSeconds{60};
    ASSERT_TRUE(waitForResponse(&requestFlag, timeoutInSeconds))
        << "Logout did not happen after " << timeoutInSeconds  << " seconds";
}

class SdkTestAvatar : public SdkTest
{
protected:
    unsigned int mApiIndex{0};
    std::unique_ptr<MegaUser> mUser;
    fs::path mDstAvatarPath{sdk_test::getTestDataDir()/AVATARDST};
    const std::string PATH_SEPARATOR{LocalPath::localPathSeparator_utf8};

public:
    void SetUp() override
    {
        // Configure test instance
        unsigned int numberOfTestInstances{1};
        ASSERT_NO_FATAL_FAILURE(getAccountsForTest(numberOfTestInstances));

        // Get user
        mUser.reset(megaApi[mApiIndex]->getMyUser());
        ASSERT_THAT(mUser, ::testing::NotNull());

        // Set avatar
        ASSERT_TRUE(getFileFromArtifactory("test-data/" + AVATARSRC, AVATARSRC));
        ASSERT_EQ(API_OK, synchronousSetAvatar(mApiIndex, AVATARSRC.c_str()));
    }

    void TearDown() override
    {
        // Remove avatar
        ASSERT_EQ(API_OK, synchronousSetAvatar(mApiIndex, nullptr));

        // Check the avatar was removed
        mApi[mApiIndex].requestFlags[MegaRequest::TYPE_GET_ATTR_USER] = false;
        ASSERT_EQ(API_ENOENT, synchronousGetUserAvatar(mApiIndex, mUser.get(), mDstAvatarPath.string().c_str()));
    }
};

/**
 * @brief SdkTestGetAvatarIntoAFile
 *
 * Get avatar into a file.
 */
TEST_F(SdkTestAvatar, SdkTestGetAvatarIntoAFile)
{
    // Get avatar
    mApi[mApiIndex].requestFlags[MegaRequest::TYPE_GET_ATTR_USER] = false;
    ASSERT_EQ(API_OK, synchronousGetUserAvatar(mApiIndex, mUser.get(), mDstAvatarPath.string().c_str()));

    // Check avatar in local filesystem
    ASSERT_TRUE(fs::exists(mDstAvatarPath));

    // Remove avatar from local filesystem
    ASSERT_TRUE(fs::remove(mDstAvatarPath));
}

/**
 * @brief SdkTestGetAvatarIntoADirectoryEndingWithSlash
 *
 * Get avatar into a directory ending with slash.
 */
TEST_F(SdkTestAvatar, SdkTestGetAvatarIntoADirectoryEndingWithSlash)
{
    // Get avatar
    std::string dstAvatarPath{sdk_test::getTestDataDir().string()};
    dstAvatarPath.append(PATH_SEPARATOR);
    ASSERT_THAT(dstAvatarPath, ::testing::EndsWith(PATH_SEPARATOR));
    mApi[mApiIndex].requestFlags[MegaRequest::TYPE_GET_ATTR_USER] = false;
    ASSERT_EQ(API_OK, synchronousGetUserAvatar(mApiIndex, mUser.get(), dstAvatarPath.c_str()));

    // Check avatar in local filesystem
    dstAvatarPath.append(mUser->getEmail());
    dstAvatarPath.append("0.jpg");
    ASSERT_TRUE(fs::exists(dstAvatarPath));

    // Remove avatar from local filesystem
    ASSERT_TRUE(fs::remove(dstAvatarPath));
}

/**
 * @brief SdkTestGetAvatarIntoADirectoryNotEndingWithSlash
 *
 * Get avatar into a directory not ending with slash.
 */
TEST_F(SdkTestAvatar, SdkTestGetAvatarIntoADirectoryNotEndingWithSlash)
{
    // Get avatar
    std::string dstAvatarPath{sdk_test::getTestDataDir().string()};
    ASSERT_THAT(dstAvatarPath, ::testing::Not(::testing::EndsWith(PATH_SEPARATOR)));
    mApi[mApiIndex].requestFlags[MegaRequest::TYPE_GET_ATTR_USER] = false;
    ASSERT_EQ(API_EWRITE, synchronousGetUserAvatar(mApiIndex, mUser.get(), dstAvatarPath.c_str()));
}

/**
 * @brief SdkTestGetAvatarIntoAnEmptyPath
 *
 * Get avatar into an empty path.
 */
TEST_F(SdkTestAvatar, SdkTestGetAvatarIntoAnEmptyPath)
{
    // Get avatar
    mApi[mApiIndex].requestFlags[MegaRequest::TYPE_GET_ATTR_USER] = false;
    ASSERT_EQ(API_EARGS, synchronousGetUserAvatar(mApiIndex, mUser.get(), ""));
}

/**
 * @brief SdkTestGetAvatarIntoANullPath
 *
 * Get avatar into a null path.
 */
TEST_F(SdkTestAvatar, SdkTestGetAvatarIntoANullPath)
{
    // Get avatar
    mApi[mApiIndex].requestFlags[MegaRequest::TYPE_GET_ATTR_USER] = false;
    ASSERT_EQ(API_EARGS, synchronousGetUserAvatar(mApiIndex, mUser.get(), nullptr));
}

/**
 * @brief Set and get Welcome dialog visibility
 */
TEST_F(SdkTest, SetGetVisibleWelcomeDialog)
{
    const unsigned int numberOfTestInstances{1};
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(numberOfTestInstances));

    const unsigned int apiIndex{0};

    std::unique_ptr<RequestTracker> requestTrackerGetOriginalVisibleWelcomeDialog{
        asyncRequestGetVisibleWelcomeDialog(apiIndex)};
    ASSERT_THAT(requestTrackerGetOriginalVisibleWelcomeDialog->waitForResult(),
                ::testing::AnyOf(::testing::Eq(API_OK), ::testing::Eq(API_ENOENT)));
    const auto originalVisibleWelcomeDialog{
        requestTrackerGetOriginalVisibleWelcomeDialog->getFlag()};

    const auto newVisibleWelcomeDialog{!originalVisibleWelcomeDialog};
    ASSERT_EQ(API_OK, synchronousSetVisibleWelcomeDialog(apiIndex, newVisibleWelcomeDialog));

    std::unique_ptr<RequestTracker> requestTrackerGetNewVisibleWelcomeDialog{
        asyncRequestGetVisibleWelcomeDialog(apiIndex)};
    ASSERT_EQ(API_OK, requestTrackerGetNewVisibleWelcomeDialog->waitForResult());
    ASSERT_EQ(newVisibleWelcomeDialog, requestTrackerGetNewVisibleWelcomeDialog->getFlag());

    ASSERT_EQ(API_OK, synchronousSetVisibleWelcomeDialog(apiIndex, originalVisibleWelcomeDialog));

    std::unique_ptr<RequestTracker> requestTrackerGetRestoredVisibleWelcomeDialog{
        asyncRequestGetVisibleWelcomeDialog(apiIndex)};
    ASSERT_EQ(API_OK, requestTrackerGetRestoredVisibleWelcomeDialog->waitForResult());
    ASSERT_EQ(originalVisibleWelcomeDialog,
              requestTrackerGetRestoredVisibleWelcomeDialog->getFlag());
}

/**
 * @brief Create node tree with empty parent node
 */
TEST_F(SdkTest, CreateNodeTreeWithEmptyParentNode)
{
    const unsigned int numberOfTestInstances{1};
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(numberOfTestInstances));

    const unsigned int apiIndex{0};

    // Create node tree
    std::unique_ptr<MegaNodeTree> nodeTree{
        MegaNodeTree::createInstance(nullptr, nullptr, nullptr, nullptr)};

    ASSERT_EQ(API_EARGS, synchronousCreateNodeTree(apiIndex, nullptr, nodeTree.get()));
}

/**
 * @brief Create node tree with empty node tree
 */
TEST_F(SdkTest, CreateNodeTreeWithEmptyNodeTree)
{
    const unsigned int numberOfTestInstances{1};
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(numberOfTestInstances));

    const unsigned int apiIndex{0};

    std::unique_ptr<MegaNode> parentNode{megaApi[apiIndex]->getRootNode()};
    ASSERT_THAT(parentNode, ::testing::NotNull());

    ASSERT_EQ(API_EARGS, synchronousCreateNodeTree(apiIndex, parentNode.get(), nullptr));
}

/**
 * @brief Create node tree with malformed node tree
 */
TEST_F(SdkTest, CreateNodeTreeWithMalformedNodeTree)
{
    const unsigned int numberOfTestInstances{1};
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(numberOfTestInstances));

    const unsigned int apiIndex{0};

    std::unique_ptr<MegaNode> parentNode{megaApi[apiIndex]->getRootNode()};
    ASSERT_THAT(parentNode, ::testing::NotNull());

    // Create node tree with both child-tree and upload-data
    std::unique_ptr<MegaNodeTree> nodeTreeChild{MegaNodeTree::createInstance(nullptr, nullptr, nullptr, nullptr)};

    std::unique_ptr<const MegaCompleteUploadData> completeUploadData{
        MegaCompleteUploadData::createInstance(nullptr, nullptr, nullptr)};

    std::unique_ptr<MegaNodeTree> nodeTree{
        MegaNodeTree::createInstance(nodeTreeChild.get(), nullptr, nullptr, completeUploadData.get())};

    ASSERT_EQ(API_EARGS, synchronousCreateNodeTree(apiIndex, parentNode.get(), nodeTree.get()));
}

/**
 * @brief Create node tree with source-handle and child-tree
 */
TEST_F(SdkTest, CreateNodeTreeWithSourceHandleAndChildTree)
{
    const unsigned int numberOfTestInstances{ 1 };
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(numberOfTestInstances));

    const unsigned int apiIndex{ 0 };

    std::unique_ptr<MegaNode> parentNode{ megaApi[apiIndex]->getRootNode() };
    ASSERT_THAT(parentNode, ::testing::NotNull());

    // Create node tree with source-handle and child-tree
    MegaHandle sourceHandle = parentNode->getHandle(); // not important, any dummy (but valid) value will do
    std::unique_ptr<MegaNodeTree> childTree{ MegaNodeTree::createInstance(nullptr, nullptr, nullptr, nullptr) };

    std::unique_ptr<MegaNodeTree> treeWithSourceAndChild{
        MegaNodeTree::createInstance(childTree.get(), nullptr, nullptr, nullptr, sourceHandle) };

    ASSERT_EQ(API_EARGS, synchronousCreateNodeTree(apiIndex, parentNode.get(), treeWithSourceAndChild.get()));
}

/**
 * @brief Create node tree with source-handle and upload-data
 */
TEST_F(SdkTest, CreateNodeTreeWithSourceHandleAndUploadData)
{
    const unsigned int numberOfTestInstances{ 1 };
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(numberOfTestInstances));

    const unsigned int apiIndex{ 0 };

    std::unique_ptr<MegaNode> parentNode{ megaApi[apiIndex]->getRootNode() };
    ASSERT_THAT(parentNode, ::testing::NotNull());

    // Create node tree with both source-handle and upload-data
    MegaHandle sourceHandle = parentNode->getHandle(); // not important, any dummy (but valid) value will do
    std::unique_ptr<const MegaCompleteUploadData> uploadData{
        MegaCompleteUploadData::createInstance(nullptr, nullptr, nullptr) };

    std::unique_ptr<MegaNodeTree> treeWithSourceAndUploadData{
        MegaNodeTree::createInstance(nullptr, nullptr, nullptr, uploadData.get(), sourceHandle) };

    ASSERT_EQ(API_EARGS, synchronousCreateNodeTree(apiIndex, parentNode.get(), treeWithSourceAndUploadData.get()));
}

/**
 * @brief Create node tree with one directory without name
 */
TEST_F(SdkTest, CreateNodeTreeWithOneDirectoryWithoutName)
{
    const unsigned int numberOfTestInstances{1};
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(numberOfTestInstances));

    const unsigned int apiIndex{0};

    std::unique_ptr<MegaNode> parentNode{megaApi[apiIndex]->getRootNode()};
    ASSERT_THAT(parentNode, ::testing::NotNull());

    // Create node tree
    std::unique_ptr<MegaNodeTree> nodeTree{
        MegaNodeTree::createInstance(nullptr, nullptr, nullptr, nullptr)};

    ASSERT_EQ(API_EARGS, synchronousCreateNodeTree(apiIndex, parentNode.get(), nodeTree.get()));
}

/**
 * @brief Create node tree with directory as source
 *
 * Attempt to copy directory/
 * to              directory_copy/
 */
TEST_F(SdkTest, CreateNodeTreeWithDirectoryAsSource)
{
    const unsigned int numberOfTestInstances{ 1 };
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(numberOfTestInstances));

    const unsigned int apiIndex{ 0 };

    std::unique_ptr<MegaNode> rootNode{ megaApi[apiIndex]->getRootNode() };
    ASSERT_THAT(rootNode, ::testing::NotNull());

    // Create directory
    const std::string directoryName{ "directory" };
    MegaHandle directoryHandle = createFolder(apiIndex, directoryName.c_str(), rootNode.get());
    ASSERT_NE(directoryHandle, INVALID_HANDLE);

    std::unique_ptr<MegaNode> directoryNode{ megaApi[apiIndex]->getNodeByHandle(directoryHandle) };
    ASSERT_THAT(directoryNode, ::testing::NotNull());

    // attempt to copy a source directory
    string directoryNameCopy = directoryName + "_copy";
    std::unique_ptr<MegaNodeTree> directoryTreeCopy{
        MegaNodeTree::createInstance(nullptr, directoryNameCopy.c_str(), nullptr, nullptr, directoryHandle) };
    ASSERT_EQ(API_EARGS, synchronousCreateNodeTree(apiIndex, rootNode.get(), directoryTreeCopy.get()));
}

/**
 * @brief Create node tree with one directory and no S4 attribute
 *
 * directory/
 */
TEST_F(SdkTest, CreateNodeTreeWithOneDirectoryAndNoS4Attribute)
{
    const unsigned int numberOfTestInstances{1};
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(numberOfTestInstances));

    const unsigned int apiIndex{0};

    std::unique_ptr<MegaNode> parentNode{megaApi[apiIndex]->getRootNode()};
    ASSERT_THAT(parentNode, ::testing::NotNull());

    // Create node tree
    const std::string directoryName{"directory"};
    std::unique_ptr<MegaNodeTree> nodeTree{
        MegaNodeTree::createInstance(nullptr, directoryName.c_str(), nullptr, nullptr)};

    RequestTracker requestTracker(megaApi[apiIndex].get());
    megaApi[apiIndex]->createNodeTree(parentNode.get(), nodeTree.get(), &requestTracker);
    nodeTree.reset();
    ASSERT_THAT(requestTracker.waitForResult(),
                ::testing::AnyOf(::testing::Eq(API_OK), ::testing::Eq(API_ENOENT)));
    const MegaNodeTree* resultNodeTree = requestTracker.request->getMegaNodeTree();
    ASSERT_THAT(resultNodeTree, ::testing::NotNull());

    // Check result
    const auto directoryNodeHandle{resultNodeTree->getNodeHandle()};
    std::unique_ptr<MegaNode> directoryNode{
        megaApi[apiIndex]->getNodeByHandle(directoryNodeHandle)};
    ASSERT_THAT(directoryNode, ::testing::NotNull());
    ASSERT_STREQ(directoryName.c_str(), directoryNode->getName());
    ASSERT_STREQ("", directoryNode->getS4());
}

/**
 * @brief Create node tree with one directory and S4 attribute
 *
 * directory/
 */
TEST_F(SdkTest, CreateNodeTreeWithOneDirectoryAndS4Attribute)
{
    const unsigned int numberOfTestInstances{1};
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(numberOfTestInstances));

    const unsigned int apiIndex{0};

    std::unique_ptr<MegaNode> parentNode{megaApi[apiIndex]->getRootNode()};
    ASSERT_THAT(parentNode, ::testing::NotNull());

    // Create node tree
    const std::string directoryName{"directory"};
    const std::string s4AttributeValue{"value"};
    std::unique_ptr<MegaNodeTree> nodeTree{MegaNodeTree::createInstance(nullptr,
                                                                        directoryName.c_str(),
                                                                        s4AttributeValue.c_str(),
                                                                        nullptr)};

    RequestTracker requestTracker(megaApi[apiIndex].get());
    megaApi[apiIndex]->createNodeTree(parentNode.get(), nodeTree.get(), &requestTracker);
    nodeTree.reset();
    ASSERT_THAT(requestTracker.waitForResult(),
                ::testing::AnyOf(::testing::Eq(API_OK), ::testing::Eq(API_ENOENT)));
    const MegaNodeTree* resultNodeTree = requestTracker.request->getMegaNodeTree();
    ASSERT_THAT(resultNodeTree, ::testing::NotNull());

    // Check result
    const auto directoryNodeHandle{resultNodeTree->getNodeHandle()};
    std::unique_ptr<MegaNode> directoryNode{
        megaApi[apiIndex]->getNodeByHandle(directoryNodeHandle)};
    ASSERT_THAT(directoryNode, ::testing::NotNull());
    ASSERT_STREQ(directoryName.c_str(), directoryNode->getName());
    ASSERT_STREQ(s4AttributeValue.c_str(), directoryNode->getS4());
}

template <typename T>
std::time_t timePointToTimeT(T timePoint)
{
    using namespace std::chrono;
    // In C++17, time_point used system_clock on POSIX and a custom TrivialClock in VS, which had different
    // epoch start. The latter had no way to convert a timestamp to time_t (like system_clock::to_time_t()).
    // This was improved in C++20, but we're not there yet.
    // With no portable way of converting time_point to time_t, let's try this workaround:
    auto portableTimePoint = time_point_cast<system_clock::duration>(timePoint - T::clock::now() + system_clock::now());
    return system_clock::to_time_t(portableTimePoint);
}

/**
 * @brief Create node tree with one file
 *
 * logo.png
 */
TEST_F(SdkTest, CreateNodeTreeWithOneFile)
{
    const unsigned int numberOfTestInstances{1};
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(numberOfTestInstances));

    const unsigned int apiIndex{0};

    // File upload
    ASSERT_TRUE(getFileFromArtifactory("test-data/" + IMAGEFILE, IMAGEFILE));
    const auto fileSize{getFilesize(IMAGEFILE)};

    std::string fingerprint;
    std::string string64UploadToken;
    std::string string64FileKey;
    ASSERT_NO_FATAL_FAILURE(synchronousMediaUploadIncomplete(apiIndex,
                                                             fileSize,
                                                             IMAGEFILE.c_str(),
                                                             IMAGEFILE_C.c_str(),
                                                             fingerprint,
                                                             string64UploadToken,
                                                             string64FileKey));

    // Create node tree
    std::unique_ptr<MegaNode> parentNode{megaApi[apiIndex]->getRootNode()};
    ASSERT_THAT(parentNode, ::testing::NotNull());

    std::unique_ptr<const MegaCompleteUploadData> completeUploadData{
        MegaCompleteUploadData::createInstance(fingerprint.c_str(),
                                               string64UploadToken.c_str(),
                                               string64FileKey.c_str())};

    std::unique_ptr<MegaNodeTree> nodeTree{
        MegaNodeTree::createInstance(nullptr, IMAGEFILE.c_str(), nullptr, completeUploadData.get())};

    RequestTracker requestTracker(megaApi[apiIndex].get());
    megaApi[apiIndex]->createNodeTree(parentNode.get(), nodeTree.get(), &requestTracker);
    nodeTree.reset();
    ASSERT_THAT(requestTracker.waitForResult(),
                ::testing::AnyOf(::testing::Eq(API_OK), ::testing::Eq(API_ENOENT)));
    const MegaNodeTree* resultNodeTree = requestTracker.request->getMegaNodeTree();
    ASSERT_THAT(resultNodeTree, ::testing::NotNull());

    // Check result
    const auto fileNodeHandle{resultNodeTree->getNodeHandle()};
    std::unique_ptr<MegaNode> fileNode{megaApi[apiIndex]->getNodeByHandle(fileNodeHandle)};
    ASSERT_THAT(fileNode, ::testing::NotNull());
    ASSERT_STREQ(IMAGEFILE.c_str(), fileNode->getName());
    ASSERT_EQ(fileSize, fileNode->getSize());

    // Check "mega" fingerprint
    ASSERT_STREQ(fileNode->getFingerprint(), fingerprint.c_str());

    // Check that mtime was kept
    auto modtime = std::filesystem::last_write_time(IMAGEFILE_C);
    auto modtime_t = timePointToTimeT(modtime);
    ASSERT_EQ(modtime_t, fileNode->getModificationTime());
}

/**
 * @brief Create node tree to copy existing source file
 */
TEST_F(SdkTest, CreateNodeTreeToCopyExistingSource)
{
    const unsigned int numberOfTestInstances{ 1 };
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(numberOfTestInstances));

    const unsigned int apiIndex{ 0 };

    // File upload
    std::unique_ptr<MegaNode> rootnode{ megaApi[0]->getRootNode() };
    ASSERT_TRUE(createFile(UPFILE, false)) << "Couldn't create " << UPFILE;

    MegaHandle uploadedNode = UNDEF;
    ASSERT_EQ(MegaError::API_OK, doStartUpload(0, &uploadedNode, UPFILE.c_str(),
        rootnode.get(),
        nullptr /*fileName*/,
        ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
        nullptr /*appData*/,
        false   /*isSourceTemporary*/,
        false   /*startFirst*/,
        nullptr /*cancelToken*/)) << "Cannot upload " << UPFILE;

    std::unique_ptr<MegaNode> newNode(megaApi[0]->getNodeByHandle(uploadedNode));
    ASSERT_THAT(newNode, ::testing::NotNull());

    // Create a copy of the already existing file
    string fileCopy = UPFILE + "_copy";
    std::unique_ptr<MegaNodeTree> nodeTree{
        MegaNodeTree::createInstance(nullptr, fileCopy.c_str(), nullptr, nullptr, newNode->getHandle()) };

    RequestTracker requestTracker(megaApi[apiIndex].get());
    megaApi[apiIndex]->createNodeTree(rootnode.get(), nodeTree.get(), &requestTracker);
    rootnode.reset();
    ASSERT_THAT(requestTracker.waitForResult(),
                ::testing::AnyOf(::testing::Eq(API_OK), ::testing::Eq(API_ENOENT)));
    const MegaNodeTree* resultNodeTree = requestTracker.request->getMegaNodeTree();
    ASSERT_THAT(resultNodeTree, ::testing::NotNull());

    std::unique_ptr<MegaNode> newNodeCopy{ megaApi[apiIndex]->getNodeByHandle(resultNodeTree->getNodeHandle()) };
    ASSERT_THAT(newNodeCopy, ::testing::NotNull());
    ASSERT_STREQ(newNodeCopy->getName(), fileCopy.c_str());
    ASSERT_EQ(newNodeCopy->getSize(), newNode->getSize());
}

/**
 * @brief Create node tree with multiple levels of directories
 *
 * directory_0/directory_1/directory_2/
 */
TEST_F(SdkTest, CreateNodeTreeWithMultipleLevelsOfDirectories)
{
    const unsigned int numberOfTestInstances{1};
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(numberOfTestInstances));

    const unsigned int apiIndex{0};

    std::unique_ptr<MegaNode> parentNode{megaApi[apiIndex]->getRootNode()};
    ASSERT_THAT(parentNode, ::testing::NotNull());

    // Create node tree
    const std::string directoryNameLevel2{"directory_2"};
    std::unique_ptr<MegaNodeTree> nodeTreeLevel2{
        MegaNodeTree::createInstance(nullptr, directoryNameLevel2.c_str(), nullptr, nullptr)};

    const std::string directoryNameLevel1{"directory_1"};
    std::unique_ptr<MegaNodeTree> nodeTreeLevel1{
        MegaNodeTree::createInstance(nodeTreeLevel2.get(),
                                     directoryNameLevel1.c_str(),
                                     nullptr,
                                     nullptr)};

    const std::string directoryNameLevel0{"directory_0"};
    std::unique_ptr<MegaNodeTree> nodeTreeLevel0{
        MegaNodeTree::createInstance(nodeTreeLevel1.get(),
                                     directoryNameLevel0.c_str(),
                                     nullptr,
                                     nullptr)};

    RequestTracker requestTracker(megaApi[apiIndex].get());
    megaApi[apiIndex]->createNodeTree(parentNode.get(), nodeTreeLevel0.get(), &requestTracker);
    nodeTreeLevel0.reset();
    ASSERT_THAT(requestTracker.waitForResult(),
                ::testing::AnyOf(::testing::Eq(API_OK), ::testing::Eq(API_ENOENT)));
    const MegaNodeTree* resultNodeTree = requestTracker.request->getMegaNodeTree();
    ASSERT_THAT(resultNodeTree, ::testing::NotNull());

    // Check result
    const auto directoryNodeHandleLevel0{resultNodeTree->getNodeHandle()};
    std::unique_ptr<MegaNode> directoryNodeLevel0{
        megaApi[apiIndex]->getNodeByHandle(directoryNodeHandleLevel0)};
    ASSERT_THAT(directoryNodeLevel0, ::testing::NotNull());
    ASSERT_STREQ(directoryNameLevel0.c_str(), directoryNodeLevel0->getName());

    const auto directoryNodeHandleLevel1{resultNodeTree->getNodeTreeChild()->getNodeHandle()};
    std::unique_ptr<MegaNode> directoryNodeLevel1{
        megaApi[apiIndex]->getNodeByHandle(directoryNodeHandleLevel1)};
    ASSERT_THAT(directoryNodeLevel1, ::testing::NotNull());
    ASSERT_STREQ(directoryNameLevel1.c_str(), directoryNodeLevel1->getName());
    ASSERT_EQ(directoryNodeLevel1->getParentHandle(), directoryNodeLevel0->getHandle());

    const auto directoryNodeHandleLevel2{resultNodeTree->getNodeTreeChild()->getNodeTreeChild()->getNodeHandle()};
    std::unique_ptr<MegaNode> directoryNodeLevel2{
        megaApi[apiIndex]->getNodeByHandle(directoryNodeHandleLevel2)};
    ASSERT_THAT(directoryNodeLevel2, ::testing::NotNull());
    ASSERT_STREQ(directoryNameLevel2.c_str(), directoryNodeLevel2->getName());
    ASSERT_EQ(directoryNodeLevel2->getParentHandle(), directoryNodeLevel1->getHandle());
}

/**
 * @brief Create node tree with multiple levels of directories and one file at the end
 *
 * directory_0/directory_1/directory_2/logo.png
 */
TEST_F(SdkTest, CreateNodeTreeWithMultipleLevelsOfDirectoriesAndOneFileAtTheEnd)
{
    const unsigned int numberOfTestInstances{1};
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(numberOfTestInstances));

    const unsigned int apiIndex{0};

    std::unique_ptr<MegaNode> parentNode{megaApi[apiIndex]->getRootNode()};
    ASSERT_THAT(parentNode, ::testing::NotNull());

    // File upload
    ASSERT_TRUE(getFileFromArtifactory("test-data/" + IMAGEFILE, IMAGEFILE));
    const auto fileSize{getFilesize(IMAGEFILE)};

    std::string fingerprint;
    std::string string64UploadToken;
    std::string string64FileKey;
    ASSERT_NO_FATAL_FAILURE(synchronousMediaUploadIncomplete(apiIndex,
                                                             fileSize,
                                                             IMAGEFILE.c_str(),
                                                             IMAGEFILE_C.c_str(),
                                                             fingerprint,
                                                             string64UploadToken,
                                                             string64FileKey));

    // Create node tree
    std::unique_ptr<const MegaCompleteUploadData> completeUploadData{
        MegaCompleteUploadData::createInstance(fingerprint.c_str(),
                                               string64UploadToken.c_str(),
                                               string64FileKey.c_str())};

    std::unique_ptr<MegaNodeTree> nodeTreeLevel3{
        MegaNodeTree::createInstance(nullptr, IMAGEFILE.c_str(), nullptr, completeUploadData.get())};

    const std::string directoryNameLevel2{"directory_2"};
    std::unique_ptr<MegaNodeTree> nodeTreeLevel2{
        MegaNodeTree::createInstance(nodeTreeLevel3.get(),
                                     directoryNameLevel2.c_str(),
                                     nullptr,
                                     nullptr)};

    const std::string directoryNameLevel1{"directory_1"};
    std::unique_ptr<MegaNodeTree> nodeTreeLevel1{
        MegaNodeTree::createInstance(nodeTreeLevel2.get(),
                                     directoryNameLevel1.c_str(),
                                     nullptr,
                                     nullptr)};

    const std::string directoryNameLevel0{"directory_0"};
    std::unique_ptr<MegaNodeTree> nodeTreeLevel0{
        MegaNodeTree::createInstance(nodeTreeLevel1.get(),
                                     directoryNameLevel0.c_str(),
                                     nullptr,
                                     nullptr)};

    RequestTracker requestTracker(megaApi[apiIndex].get());
    megaApi[apiIndex]->createNodeTree(parentNode.get(),
                                      nodeTreeLevel0.get(),
                                      "192.168.0.0:0000", // dummy IP just to test sending "cip"
                                      &requestTracker);
    nodeTreeLevel0.reset();
    ASSERT_THAT(requestTracker.waitForResult(),
                ::testing::AnyOf(::testing::Eq(API_OK), ::testing::Eq(API_ENOENT)));
    const MegaNodeTree* resultNodeTree = requestTracker.request->getMegaNodeTree();
    ASSERT_THAT(resultNodeTree, ::testing::NotNull());

    // Check result
    const auto directoryNodeHandleLevel0{resultNodeTree->getNodeHandle()};
    std::unique_ptr<MegaNode> directoryNodeLevel0{
        megaApi[apiIndex]->getNodeByHandle(directoryNodeHandleLevel0)};
    ASSERT_THAT(directoryNodeLevel0, ::testing::NotNull());
    ASSERT_STREQ(directoryNameLevel0.c_str(), directoryNodeLevel0->getName());

    const auto directoryNodeHandleLevel1{resultNodeTree->getNodeTreeChild()->getNodeHandle()};
    std::unique_ptr<MegaNode> directoryNodeLevel1{
        megaApi[apiIndex]->getNodeByHandle(directoryNodeHandleLevel1)};
    ASSERT_THAT(directoryNodeLevel1, ::testing::NotNull());
    ASSERT_STREQ(directoryNameLevel1.c_str(), directoryNodeLevel1->getName());

    const auto directoryNodeHandleLevel2{resultNodeTree->getNodeTreeChild()->getNodeTreeChild()->getNodeHandle()};
    std::unique_ptr<MegaNode> directoryNodeLevel2{
        megaApi[apiIndex]->getNodeByHandle(directoryNodeHandleLevel2)};
    ASSERT_THAT(directoryNodeLevel2, ::testing::NotNull());
    ASSERT_STREQ(directoryNameLevel2.c_str(), directoryNodeLevel2->getName());

    const auto fileNodeHandle{resultNodeTree->getNodeTreeChild()->getNodeTreeChild()->getNodeTreeChild()->getNodeHandle()};
    std::unique_ptr<MegaNode> fileNode{megaApi[apiIndex]->getNodeByHandle(fileNodeHandle)};
    ASSERT_THAT(fileNode, ::testing::NotNull());
    ASSERT_STREQ(IMAGEFILE.c_str(), fileNode->getName());
    ASSERT_EQ(fileSize, fileNode->getSize());

    // Check that fileHandle was populated when file node was created
    Base64Str<MegaClient::NODEHANDLE> b64FileHandle{fileNode->getHandle()};
    MegaStringMap* fileHandles = requestTracker.request->getMegaStringMap();
    ASSERT_THAT(fileHandles, ::testing::NotNull());
    ASSERT_EQ(fileHandles->size(), 1);
    ASSERT_THAT(fileHandles->get(b64FileHandle), ::testing::NotNull());

    // Check that fileHandle was populated when file download url was fetched
    RequestTracker tracker{megaApi[apiIndex].get()};
    megaApi[apiIndex]->getDownloadUrl(fileNode.get(), true, &tracker);
    ASSERT_EQ(API_OK, tracker.waitForResult());
    MegaStringMap* fileHandle = tracker.request->getMegaStringMap();
    ASSERT_THAT(fileHandle, ::testing::NotNull());
    ASSERT_EQ(fileHandle->size(), 1);
    ASSERT_THAT(fileHandle->get(b64FileHandle), ::testing::NotNull());
    ASSERT_STREQ(fileHandle->get(b64FileHandle), fileHandles->get(b64FileHandle));
}

/**
 * @brief Create node tree version using identical upload data.
 *
 * The handle of the existing file should be returned, and no new version should be added.
 */
TEST_F(SdkTest, CreateNodeTreeVersionUsingIdenticalUploadData)
{
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    const unsigned apiIndex = 0;

    // Prepare the file to upload
    createFile(PUBLICFILE.c_str(), false);
    const int64_t fileSize = getFilesize(PUBLICFILE);
    string encryptedFile = PUBLICFILE + "_encr";
    std::string fingerprint;
    std::string string64UploadToken;
    std::string string64FileKey;

    // Upload file, incomplete
    ASSERT_NO_FATAL_FAILURE(synchronousMediaUploadIncomplete(apiIndex,
                                                             fileSize,
                                                             PUBLICFILE.c_str(),
                                                             encryptedFile.c_str(),
                                                             fingerprint,
                                                             string64UploadToken,
                                                             string64FileKey));

    // Prepare destination
    std::unique_ptr<MegaNode> parentNode{ megaApi[apiIndex]->getRootNode() };
    ASSERT_THAT(parentNode, ::testing::NotNull());

    // Create node tree from uploaded data
    std::unique_ptr<const MegaCompleteUploadData> uploadData{
        MegaCompleteUploadData::createInstance(fingerprint.c_str(),
                                               string64UploadToken.c_str(),
                                               string64FileKey.c_str()) };
    std::unique_ptr<MegaNodeTree> fileTreeFromData {
        MegaNodeTree::createInstance(nullptr, PUBLICFILE.c_str(), nullptr, uploadData.get()) };

    RequestTracker requestTrackerFirstTree(megaApi[apiIndex].get());
    megaApi[apiIndex]->createNodeTree(parentNode.get(),
                                      fileTreeFromData.get(),
                                      &requestTrackerFirstTree);
    ASSERT_THAT(requestTrackerFirstTree.waitForResult(),
                ::testing::AnyOf(::testing::Eq(API_OK), ::testing::Eq(API_ENOENT)));
    const MegaNodeTree* resultNodeTree1 = requestTrackerFirstTree.request->getMegaNodeTree();
    ASSERT_THAT(resultNodeTree1, ::testing::NotNull());
    ASSERT_NE(resultNodeTree1->getNodeHandle(), INVALID_HANDLE);

    // Ensure there's only 1 version
    std::unique_ptr<MegaNode> fileNode{ megaApi[apiIndex]->getNodeByHandle(resultNodeTree1->getNodeHandle()) };
    ASSERT_THAT(fileNode, ::testing::NotNull());
    std::unique_ptr<MegaNodeList> allVersions{ megaApi[apiIndex]->getVersions(fileNode.get()) };
    ASSERT_THAT(allVersions, ::testing::NotNull());
    ASSERT_EQ(allVersions->size(), 1);

    // Attempt to create another node tree from the same data
    std::unique_ptr<const MegaCompleteUploadData> uploadData2{
        MegaCompleteUploadData::createInstance(fingerprint.c_str(),
                                               string64UploadToken.c_str(),
                                               string64FileKey.c_str()) };
    std::unique_ptr<MegaNodeTree> fileTreeFromData2 {
        MegaNodeTree::createInstance(nullptr, PUBLICFILE.c_str(), nullptr, uploadData2.get()) };

    RequestTracker requestTrackerSecondTree(megaApi[apiIndex].get());
    megaApi[apiIndex]->createNodeTree(parentNode.get(),
                                      fileTreeFromData2.get(),
                                      &requestTrackerSecondTree);
    ASSERT_THAT(requestTrackerSecondTree.waitForResult(),
                ::testing::AnyOf(::testing::Eq(API_OK), ::testing::Eq(API_ENOENT)));
    const MegaNodeTree* resultNodeTree2 = requestTrackerSecondTree.request->getMegaNodeTree();
    ASSERT_THAT(resultNodeTree2, ::testing::NotNull());

    // Confirm there's still only 1 version
    std::unique_ptr<MegaNode> fileNode2{ megaApi[apiIndex]->getNodeByHandle(resultNodeTree2->getNodeHandle()) };
    ASSERT_THAT(fileNode2, ::testing::NotNull());
    std::unique_ptr<MegaNodeList> allVersions2{ megaApi[apiIndex]->getVersions(fileNode2.get()) };
    ASSERT_THAT(allVersions2, ::testing::NotNull());
    ASSERT_EQ(allVersions2->size(), 1);
    ASSERT_EQ(resultNodeTree2->getNodeHandle(), resultNodeTree1->getNodeHandle());
}

/**
 * @brief Create node tree version using identical source file.
 *
 * The handle of the existing file should be returned, and no new version should be added.
 */
TEST_F(SdkTest, CreateNodeTreeVersionUsingIdenticalSourceFile)
{
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    const unsigned apiIndex = 0;

    // File upload
    std::unique_ptr<MegaNode> rootNode{ megaApi[apiIndex]->getRootNode() };
    ASSERT_TRUE(createFile(UPFILE, false)) << "Couldn't create " << UPFILE;

    MegaHandle upHandle = INVALID_HANDLE;
    ASSERT_EQ(MegaError::API_OK, doStartUpload(0, &upHandle, UPFILE.c_str(),
        rootNode.get(),
        nullptr /*fileName*/,
        ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
        nullptr /*appData*/,
        false   /*isSourceTemporary*/,
        false   /*startFirst*/,
        nullptr /*cancelToken*/)) << "Cannot upload " << UPFILE;
    ASSERT_NE(upHandle, INVALID_HANDLE);

    // Ensure there's only 1 version
    std::unique_ptr<MegaNode> upNode(megaApi[0]->getNodeByHandle(upHandle));
    ASSERT_THAT(upNode, ::testing::NotNull());
    std::unique_ptr<MegaNodeList> allVersions{ megaApi[apiIndex]->getVersions(upNode.get()) };
    ASSERT_THAT(allVersions, ::testing::NotNull());
    ASSERT_EQ(allVersions->size(), 1);

    // Make a copy of the file
    string fileCopy = UPFILE + "_copy";
    RequestTracker nodeCopyTracker(megaApi[apiIndex].get());
    megaApi[apiIndex]->copyNode(upNode.get(), rootNode.get(), fileCopy.c_str(), &nodeCopyTracker);
    ASSERT_EQ(API_OK, nodeCopyTracker.waitForResult());
    ASSERT_NE(nodeCopyTracker.getNodeHandle(), INVALID_HANDLE);
    std::unique_ptr<MegaNode> upNodeCopy(megaApi[0]->getNodeByHandle(nodeCopyTracker.getNodeHandle()));

    // Create a new version of the initial file using its copy as source
    std::unique_ptr<MegaNodeTree> nodeTree{
        MegaNodeTree::createInstance(nullptr, UPFILE.c_str(), nullptr, nullptr, upNodeCopy->getHandle()) };

    RequestTracker requestTracker(megaApi[apiIndex].get());
    megaApi[apiIndex]->createNodeTree(rootNode.get(), nodeTree.get(), &requestTracker);
    nodeTree.reset();
    ASSERT_THAT(requestTracker.waitForResult(),
                ::testing::AnyOf(::testing::Eq(API_OK), ::testing::Eq(API_ENOENT)));
    const MegaNodeTree* resultNodeTree = requestTracker.request->getMegaNodeTree();
    ASSERT_THAT(resultNodeTree, ::testing::NotNull());

    // Confirm there's still only 1 version
    std::unique_ptr<MegaNode> upNodeVersion{ megaApi[apiIndex]->getNodeByHandle(resultNodeTree->getNodeHandle()) };
    ASSERT_THAT(upNodeVersion, ::testing::NotNull());
    std::unique_ptr<MegaNodeList> allVersions2{ megaApi[apiIndex]->getVersions(upNodeVersion.get()) };
    ASSERT_THAT(allVersions2, ::testing::NotNull());
    ASSERT_EQ(allVersions2->size(), 1);
    ASSERT_EQ(upNodeVersion->getHandle(), upNode->getHandle());
}

/**
 * @brief Create node tree version using different upload data.
 *
 * A new handle should be returned, a new version should be added, and the handle of
 * the existing file should correspond to the previous version.
 */
TEST_F(SdkTest, CreateNodeTreeVersionUsingDifferentUploadData)
{
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    const unsigned apiIndex = 0;

    // Prepare the file to upload
    createFile(PUBLICFILE.c_str(), false);
    const int64_t fileSize = getFilesize(PUBLICFILE);
    string encryptedFile = PUBLICFILE + "_encr";
    std::string fingerprint;
    std::string string64UploadToken;
    std::string string64FileKey;

    // Upload file, incomplete
    ASSERT_NO_FATAL_FAILURE(synchronousMediaUploadIncomplete(apiIndex,
                                                             fileSize,
                                                             PUBLICFILE.c_str(),
                                                             encryptedFile.c_str(),
                                                             fingerprint,
                                                             string64UploadToken,
                                                             string64FileKey));

    // Prepare destination
    std::unique_ptr<MegaNode> parentNode{ megaApi[apiIndex]->getRootNode() };
    ASSERT_THAT(parentNode, ::testing::NotNull());

    // Create node tree from uploaded data
    std::unique_ptr<const MegaCompleteUploadData> uploadData{
        MegaCompleteUploadData::createInstance(fingerprint.c_str(),
                                               string64UploadToken.c_str(),
                                               string64FileKey.c_str()) };
    std::unique_ptr<MegaNodeTree> fileTreeFromData {
        MegaNodeTree::createInstance(nullptr, PUBLICFILE.c_str(), nullptr, uploadData.get()) };

    RequestTracker requestTrackerFirstTree(megaApi[apiIndex].get());
    megaApi[apiIndex]->createNodeTree(parentNode.get(),
                                      fileTreeFromData.get(),
                                      &requestTrackerFirstTree);
    ASSERT_THAT(requestTrackerFirstTree.waitForResult(),
                ::testing::AnyOf(::testing::Eq(API_OK), ::testing::Eq(API_ENOENT)));
    const MegaNodeTree* resultNodeTree1 = requestTrackerFirstTree.request->getMegaNodeTree();
    ASSERT_THAT(resultNodeTree1, ::testing::NotNull());
    ASSERT_NE(resultNodeTree1->getNodeHandle(), INVALID_HANDLE);

    // Ensure there's only 1 version
    std::unique_ptr<MegaNode> fileNode{ megaApi[apiIndex]->getNodeByHandle(resultNodeTree1->getNodeHandle()) };
    ASSERT_THAT(fileNode, ::testing::NotNull());
    std::unique_ptr<MegaNodeList> allVersions{ megaApi[apiIndex]->getVersions(fileNode.get()) };
    ASSERT_THAT(allVersions, ::testing::NotNull());
    ASSERT_EQ(allVersions->size(), 1);

    // Upload file again, incomplete
    ASSERT_NO_FATAL_FAILURE(synchronousMediaUploadIncomplete(apiIndex,
                                                             fileSize,
                                                             PUBLICFILE.c_str(),
                                                             encryptedFile.c_str(),
                                                             fingerprint,
                                                             string64UploadToken,
                                                             string64FileKey));

    // Create node tree from the new data
    std::unique_ptr<const MegaCompleteUploadData> uploadData2{
        MegaCompleteUploadData::createInstance(fingerprint.c_str(),
                                               string64UploadToken.c_str(),
                                               string64FileKey.c_str()) };
    std::unique_ptr<MegaNodeTree> fileTreeFromData2 {
        MegaNodeTree::createInstance(nullptr, PUBLICFILE.c_str(), nullptr, uploadData2.get()) };

    RequestTracker requestTrackerSecondTree(megaApi[apiIndex].get());
    megaApi[apiIndex]->createNodeTree(parentNode.get(),
                                      fileTreeFromData2.get(),
                                      &requestTrackerSecondTree);
    ASSERT_THAT(requestTrackerSecondTree.waitForResult(),
                ::testing::AnyOf(::testing::Eq(API_OK), ::testing::Eq(API_ENOENT)));
    const MegaNodeTree* resultNodeTree2 = requestTrackerSecondTree.request->getMegaNodeTree();
    ASSERT_THAT(resultNodeTree2, ::testing::NotNull());
    ASSERT_NE(resultNodeTree2->getNodeHandle(), INVALID_HANDLE);

    // Confirm there are 2 versions now
    std::unique_ptr<MegaNode> fileNode2{ megaApi[apiIndex]->getNodeByHandle(resultNodeTree2->getNodeHandle()) };
    ASSERT_THAT(fileNode2, ::testing::NotNull());
    std::unique_ptr<MegaNodeList> allVersions2{ megaApi[apiIndex]->getVersions(fileNode2.get()) };
    ASSERT_THAT(allVersions2, ::testing::NotNull());
    ASSERT_EQ(allVersions2->size(), 2);
    ASSERT_EQ(allVersions2->get(1)->getHandle(), resultNodeTree1->getNodeHandle());
    ASSERT_EQ(allVersions2->get(0)->getHandle(), resultNodeTree2->getNodeHandle());
    ASSERT_NE(resultNodeTree2->getNodeHandle(), resultNodeTree1->getNodeHandle());
}

/**
 * @brief Create node tree version using different source file.
 *
 * A new handle should be returned, a new version should be added, and the handle of
 * the existing file should correspond to the previous version.
 */
TEST_F(SdkTest, CreateNodeTreeVersionUsingDifferentSourceFile)
{
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    const unsigned apiIndex = 0;

    // File upload
    std::unique_ptr<MegaNode> rootNode{ megaApi[apiIndex]->getRootNode() };
    ASSERT_TRUE(createFile(UPFILE, false, "UPFILE")) << "Couldn't create " << UPFILE;

    MegaHandle upHandle = INVALID_HANDLE;
    ASSERT_EQ(MegaError::API_OK, doStartUpload(0, &upHandle, UPFILE.c_str(),
        rootNode.get(),
        nullptr /*fileName*/,
        ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
        nullptr /*appData*/,
        false   /*isSourceTemporary*/,
        false   /*startFirst*/,
        nullptr /*cancelToken*/)) << "Cannot upload " << UPFILE;
    ASSERT_NE(upHandle, INVALID_HANDLE);

    // Ensure there's only 1 version
    std::unique_ptr<MegaNode> upNode(megaApi[0]->getNodeByHandle(upHandle));
    ASSERT_THAT(upNode, ::testing::NotNull());
    std::unique_ptr<MegaNodeList> allVersions{ megaApi[apiIndex]->getVersions(upNode.get()) };
    ASSERT_THAT(allVersions, ::testing::NotNull());
    ASSERT_EQ(allVersions->size(), 1);

    // File 2 upload
    ASSERT_TRUE(createFile(PUBLICFILE, false, "PUBLICFILE")) << "Couldn't create " << PUBLICFILE;

    MegaHandle pubHandle = INVALID_HANDLE;
    ASSERT_EQ(MegaError::API_OK, doStartUpload(0, &pubHandle, PUBLICFILE.c_str(),
        rootNode.get(),
        nullptr /*fileName*/,
        ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
        nullptr /*appData*/,
        false   /*isSourceTemporary*/,
        false   /*startFirst*/,
        nullptr /*cancelToken*/)) << "Cannot upload " << PUBLICFILE;
    ASSERT_NE(pubHandle, INVALID_HANDLE);

    // Create a new version of the initial file using a different file as source
    std::unique_ptr<MegaNodeTree> nodeTree{
        MegaNodeTree::createInstance(nullptr, UPFILE.c_str(), nullptr, nullptr, pubHandle) };

    RequestTracker requestTracker(megaApi[apiIndex].get());
    megaApi[apiIndex]->createNodeTree(rootNode.get(), nodeTree.get(), &requestTracker);
    ASSERT_THAT(requestTracker.waitForResult(),
                ::testing::AnyOf(::testing::Eq(API_OK), ::testing::Eq(API_ENOENT)));
    const MegaNodeTree* resultNodeTree = requestTracker.request->getMegaNodeTree();
    ASSERT_THAT(resultNodeTree, ::testing::NotNull());

    // Confirm there are 2 versions now
    std::unique_ptr<MegaNode> upNodeVersion{ megaApi[apiIndex]->getNodeByHandle(resultNodeTree->getNodeHandle()) };
    ASSERT_THAT(upNodeVersion, ::testing::NotNull());
    std::unique_ptr<MegaNodeList> allVersions2{ megaApi[apiIndex]->getVersions(upNodeVersion.get()) };
    ASSERT_THAT(allVersions2, ::testing::NotNull());
    ASSERT_EQ(allVersions2->size(), 2);
    ASSERT_EQ(allVersions2->get(1)->getHandle(), upHandle);
    ASSERT_EQ(allVersions2->get(0)->getHandle(), upNodeVersion->getHandle());
    ASSERT_NE(upNodeVersion->getHandle(), upHandle);
}

#ifdef ENABLE_SYNC
/**
 * ___RemoveInshareElementToSynDebris___
 * Steps:
 *  - Two users userA and userB
 *  - UserA creates a tree
 *  - UserA shares this tree with userB
 *  - UserB creates a sync with the inshare
 *  - UserB removes part of the tree
 * Result:
 *  - Removed sub-tree is added a useB SyncDebris folder
 *  - Sub-tree is removed from inshare
 */
TEST_F(SdkTest, RemoveInshareElementToSynDebris)
{
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(2));

    // --- Create some nodes to share ---
    //  |--Shared-folder
    //    |--Shared-subfolder
    //      |--file1.txt
    //      |--file2.txt

    std::unique_ptr<MegaNode> rootnode{ megaApi[0]->getRootNode() };
    static constexpr char foldername1[] = "Shared-folder";
    MegaHandle hfolder1 = createFolder(0, foldername1, rootnode.get());
    ASSERT_NE(hfolder1, UNDEF) << "Cannot create " << foldername1;
    std::unique_ptr<MegaNode> n1{ megaApi[0]->getNodeByHandle(hfolder1) };
    ASSERT_NE(n1, nullptr);

    static constexpr char foldername2[] = "Shared-subfolder";
    MegaHandle hfolder2 = createFolder(0, foldername2, n1.get());
    ASSERT_NE(hfolder2, UNDEF) << "Cannot create " << foldername1;
    std::unique_ptr<MegaNode> n2{ megaApi[0]->getNodeByHandle(hfolder2) };
    ASSERT_NE(n2, nullptr);

    createFile(PUBLICFILE.c_str(), false);
    MegaHandle hfile1 = 0;
    ASSERT_EQ(MegaError::API_OK, doStartUpload(0, &hfile1, PUBLICFILE.c_str(), n2.get(),
                                               nullptr /*fileName*/,
                                               ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
                                               nullptr /*appData*/,
                                               false   /*isSourceTemporary*/,
                                               false   /*startFirst*/,
                                               nullptr /*cancelToken*/)) << "Cannot upload a test file";

    createFile(UPFILE.c_str(), false);
    MegaHandle hfile2 = 0;
    ASSERT_EQ(MegaError::API_OK, doStartUpload(0, &hfile2, UPFILE.c_str(), n2.get(),
                                               nullptr /*fileName*/,
                                               ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
                                               nullptr /*appData*/,
                                               false   /*isSourceTemporary*/,
                                               false   /*startFirst*/,
                                               nullptr /*cancelToken*/)) << "Cannot upload a second test file";


    // --- Create a new contact to share to ---

    string message = "Hi contact. Let's share some stuff";

    mApi[1].contactRequestUpdated = false;
    ASSERT_NO_FATAL_FAILURE(inviteContact(0, mApi[1].email, message, MegaContactRequest::INVITE_ACTION_ADD));
    ASSERT_TRUE(waitForResponse(&mApi[1].contactRequestUpdated))   // at the target side (auxiliar account)
        << "Contact request creation not received after " << maxTimeout << " seconds";

    ASSERT_NO_FATAL_FAILURE(getContactRequest(1, false));

    mApi[0].contactRequestUpdated = mApi[1].contactRequestUpdated = false;
    ASSERT_NO_FATAL_FAILURE(replyContact(mApi[1].cr.get(), MegaContactRequest::REPLY_ACTION_ACCEPT));
    ASSERT_TRUE(waitForResponse(&mApi[1].contactRequestUpdated))   // at the target side (auxiliar account)
        << "Contact request creation not received after " << maxTimeout << " seconds";
    ASSERT_TRUE(waitForResponse(&mApi[0].contactRequestUpdated))   // at the source side (main account)
        << "Contact request creation not received after " << maxTimeout << " seconds";

    mApi[1].cr.reset();

    // --- Verify credentials in both accounts ---

    if (gManualVerification)
    {
        if (!areCredentialsVerified(0, mApi[1].email)) {ASSERT_NO_FATAL_FAILURE(verifyCredentials(0, mApi[1].email));}
        if (!areCredentialsVerified(1, mApi[0].email)) {ASSERT_NO_FATAL_FAILURE(verifyCredentials(1, mApi[0].email));}
    }

    // --- Share a folder with User2 ---
    MegaHandle nodeHandle = n1->getHandle();
    bool check1, check2;
    mApi[0].mOnNodesUpdateCompletion = createOnNodesUpdateLambda(nodeHandle, MegaNode::CHANGE_TYPE_OUTSHARE, check1);
    mApi[1].mOnNodesUpdateCompletion = createOnNodesUpdateLambda(nodeHandle, MegaNode::CHANGE_TYPE_INSHARE, check2);

    ASSERT_NO_FATAL_FAILURE(shareFolder(n1.get(), mApi[1].email.c_str(), MegaShare::ACCESS_FULL));
    ASSERT_TRUE(waitForResponse(&check1))   // at the target side (main account)
        << "Node update not received after " << maxTimeout << " seconds";
    ASSERT_TRUE(waitForResponse(&check2))   // at the target side (auxiliar account)
        << "Node update not received after " << maxTimeout << " seconds";
    // important to reset
    resetOnNodeUpdateCompletionCBs();
    ASSERT_EQ(check1, true);
    ASSERT_EQ(check2, true);

    fs::path localBasePath = makeNewTestRoot();
    fs::path localFolderPath = localBasePath / "SyncFolder";
    fs::create_directories(localFolderPath);
    MegaHandle newSyncRootNodeHandle = UNDEF;

    int err = synchronousSyncFolder(1, &newSyncRootNodeHandle, MegaSync::TYPE_TWOWAY, localFolderPath.u8string().c_str(), nullptr, hfolder1, nullptr);
    ASSERT_TRUE(err == API_OK) << "Backup folder failed (error: " << err << ")";
    std::unique_ptr<MegaNode> syncFolder(megaApi[1]->getNodeByHandle(hfolder1));
    std::unique_ptr<MegaSync> sync = waitForSyncState(megaApi[1].get(), syncFolder.get(), MegaSync::RUNSTATE_RUNNING, MegaSync::NO_SYNC_ERROR);
    ASSERT_TRUE(sync && sync->getRunState() == MegaSync::RUNSTATE_RUNNING);

    fs::path folder2Path = localFolderPath / foldername2;
    fs::path filePath1 = folder2Path / PUBLICFILE;
    fs::path filePath2 = folder2Path / UPFILE;
    // Wait until both files have been created at local path
    ASSERT_TRUE(waitForEvent([filePath1, filePath2]()
    {
        ifstream f1(filePath1.u8string().c_str());
        ifstream f2(filePath2.u8string().c_str());
        if (f1.good() && f2.good())
        {
            return true;
        }

        return false;

    })) << "Files haven't been download at local path";

    // Wait one of both files have been deleted from inshare. Copy operation has to be executed before deleting
    mApi[0].mOnNodesUpdateCompletion = createOnNodesUpdateLambda(hfile1, MegaNode::CHANGE_TYPE_REMOVED, check1);
    mApi[1].mOnNodesUpdateCompletion = createOnNodesUpdateLambda(hfile1, MegaNode::CHANGE_TYPE_REMOVED, check2);
    deleteFolder(folder2Path.u8string());
    ASSERT_TRUE(waitForResponse(&check2))   // at the target side (auxiliar account)
        << "Node hasn't been removed after " << maxTimeout << " seconds";
    ASSERT_TRUE(waitForResponse(&check1))   // at the target side (main account)
        << "Node hasn't been removed after " << maxTimeout << " seconds";
    resetOnNodeUpdateCompletionCBs();

    // Check file exits at SyncDebris folder
    std::unique_ptr<MegaNode> rubbishbin(megaApi[1]->getRubbishNode());
    std::unique_ptr<MegaSearchFilter> filter(MegaSearchFilter::createInstance());
    filter->byLocationHandle(rubbishbin->getHandle());
    filter->byName(PUBLICFILE.c_str());
    std::unique_ptr<MegaNodeList> nodes(megaApi[1]->search(filter.get()));
    ASSERT_EQ(nodes->size(), 1) << "Invalid number of nodes at rubbisbin (debris folder)";
    std::unique_ptr<char[]> path(megaApi[1]->getNodePath(nodes->get(0)));
    std::string stringPath(path.get());
    ASSERT_NE(stringPath.find("SyncDebris"), std::string::npos);

    ASSERT_EQ(API_OK, synchronousRemoveSync(1, sync->getBackupId()));
}
#endif

/**
 * @brief Set and get Terms of Service visibility
 */
TEST_F(SdkTest, SetGetVisibleTermsOfService)
{
    const unsigned int numberOfTestInstances{1};
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(numberOfTestInstances));

    const unsigned int apiIndex{0};
    const bool defaultTermsOfService = true;

    RequestTracker requestTrackerFirstGet(megaApi[apiIndex].get());
    megaApi[apiIndex]->getVisibleTermsOfService(&requestTrackerFirstGet);
    ASSERT_THAT(requestTrackerFirstGet.waitForResult(),
                ::testing::AnyOf(::testing::Eq(API_OK), ::testing::Eq(API_ENOENT)));
    ASSERT_EQ(defaultTermsOfService, requestTrackerFirstGet.getFlag());

    const bool modifiedTermsOfService = !defaultTermsOfService;

    RequestTracker requestTrackerFirstSet(megaApi[apiIndex].get());
    megaApi[apiIndex]->setVisibleTermsOfService(modifiedTermsOfService, &requestTrackerFirstSet);
    ASSERT_EQ(API_OK, requestTrackerFirstSet.waitForResult());

    RequestTracker requestTrackerSecondGet(megaApi[apiIndex].get());
    megaApi[apiIndex]->getVisibleTermsOfService(&requestTrackerSecondGet);
    ASSERT_EQ(API_OK, requestTrackerSecondGet.waitForResult());
    ASSERT_EQ(modifiedTermsOfService, requestTrackerSecondGet.getFlag());

    RequestTracker requestTrackerSecondSet(megaApi[apiIndex].get());
    megaApi[apiIndex]->setVisibleTermsOfService(defaultTermsOfService, &requestTrackerSecondSet);
    ASSERT_EQ(API_OK, requestTrackerSecondSet.waitForResult());

    RequestTracker requestTrackerThirdGet(megaApi[apiIndex].get());
    megaApi[apiIndex]->getVisibleTermsOfService(&requestTrackerThirdGet);
    ASSERT_EQ(API_OK, requestTrackerThirdGet.waitForResult());
    ASSERT_EQ(defaultTermsOfService, requestTrackerThirdGet.getFlag());
}

#ifdef ENABLE_CHAT

/**
 * @brief Give and remove access to download a file from a chat
 */
TEST_F(SdkTest, GiveRemoveChatAccess)
{
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(2));
    const unsigned int host = 0;
    const unsigned int guest = 1;
    mApi[host].chats.clear();
    mApi[guest].chats.clear();

    // Send and accept a new contact request
    string message = "Hi contact. This is a testing message";
    inviteTestAccount(host, guest, message);

    // Create chat between new contacts
    MegaHandle chatId = createChatWithChecks(0, {1}, true);
    ASSERT_NE(chatId, INVALID_HANDLE) << "Something went wrong when creating the group chat room";

    // Update test file

    ASSERT_TRUE(createFile(PUBLICFILE.c_str(), false)) << "Couldn't create " << PUBLICFILE;
    std::unique_ptr<MegaNode> rootnode{megaApi[host]->getRootNode()};
    ASSERT_NE(rootnode.get(), nullptr);
    MegaHandle fileHandle = UNDEF;
    ASSERT_EQ(MegaError::API_OK, doStartUpload(host, &fileHandle, PUBLICFILE.c_str(),
                                               rootnode.get(),
                                               nullptr /*fileName*/,
                                               ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
                                               nullptr /*appData*/,
                                               false   /*isSourceTemporary*/,
                                               false   /*startFirst*/,
                                               nullptr /*cancelToken*/)) << "Cannot upload a test file";
    std::unique_ptr<MegaNode> fileNode(megaApi[host]->getNodeByHandle(fileHandle));

    // Grant access to guest

    ASSERT_FALSE(megaApi[host]->hasAccessToAttachment(chatId, fileHandle, megaApi[guest]->getMyUser()->getHandle()));
    RequestTracker requestTrackerGrantAccess(megaApi[host].get());
    megaApi[host]->grantAccessInChat(chatId, fileNode.get(), megaApi[guest]->getMyUser()->getHandle(), &requestTrackerGrantAccess);
    ASSERT_EQ(API_OK, requestTrackerGrantAccess.waitForResult());
    ASSERT_TRUE(megaApi[host]->hasAccessToAttachment(chatId, fileHandle, megaApi[guest]->getMyUser()->getHandle()));

    // Remove access to guest

    RequestTracker requestTrackerRemoveAccess(megaApi[host].get());
    megaApi[host]->removeAccessInChat(chatId, fileNode.get(), megaApi[guest]->getMyUser()->getHandle(), &requestTrackerRemoveAccess);
    ASSERT_EQ(API_OK, requestTrackerRemoveAccess.waitForResult());
    ASSERT_FALSE(megaApi[host]->hasAccessToAttachment(chatId, fileHandle, megaApi[guest]->getMyUser()->getHandle()));
}

#endif

TEST_F(SdkTest, GetFileFromArtifactorySuccessfully)
{
    const std::string relativeUrl{"test-data/gfx-processing-crash/default_irradiance.dds"};
    const fs::path output{"default_irradiance.dds"};
    ASSERT_TRUE(getFileFromArtifactory(relativeUrl, output));
    ASSERT_TRUE(fs::exists(output));
    fs::remove(output);
}

TEST_F(SdkTest, GenerateRandomCharsPassword)
{
    LOG_debug << "### Test characters-based random passwords generation";
    bool useUpper = false;
    bool useDigits = false;
    bool useSymbols = false;
    const unsigned int minLength = 8, maxLength = 64;
    const unsigned int length = 10;

    LOG_debug << "# Test out-of-bounds password generation request";
    std::unique_ptr<char[]> pwd{
        MegaApi::generateRandomCharsPassword(useUpper, useDigits, useSymbols, minLength - 1)};
    ASSERT_FALSE(pwd);
    pwd.reset(MegaApi::generateRandomCharsPassword(useUpper, useDigits, useSymbols, maxLength + 1));
    ASSERT_FALSE(pwd);

    const auto validatePassword =
        [&useUpper, &useDigits, &useSymbols]
        (const std::string& pwd) -> bool
    {
        bool lowerFound = false;
        bool upperFound = false;
        bool digitFound = false;
        bool symbolFound = false;
        const std::set<char> validSymbols {'!','@','#','$','%','^','&','*','(',')'};

        for (auto c : pwd)
        {
            if (!upperFound && std::isupper(c))
            {
                if (!useUpper) return false;
                upperFound = true;
            }

            if (!digitFound && std::isdigit(c))
            {
                if (!useDigits) return false;
                digitFound = true;
            }

            if (!symbolFound && validSymbols.count(c))
            {
                if (!useSymbols) return false;
                symbolFound = true;
            }

            if (!lowerFound && std::islower(c)) lowerFound = true;
        }

        return lowerFound && (useUpper == upperFound) &&
               (useDigits == digitFound) && (useSymbols == symbolFound);
    };

    LOG_debug << "\t# Test only lower case characters";
    useUpper = useDigits = useSymbols = false;
    pwd.reset(MegaApi::generateRandomCharsPassword(useUpper, useDigits, useSymbols, length));
    ASSERT_TRUE(pwd);
    ASSERT_TRUE(std::strlen(pwd.get()) == length);
    ASSERT_TRUE(validatePassword(pwd.get())) << "Invalid generated password " << pwd.get();

    LOG_debug << "\t# Test lower and upper case characters only";
    useDigits = useSymbols = false;
    useUpper = true;
    pwd.reset(MegaApi::generateRandomCharsPassword(useUpper, useDigits, useSymbols, length));
    ASSERT_TRUE(pwd);
    ASSERT_TRUE(std::strlen(pwd.get()) == length);
    ASSERT_TRUE(validatePassword(pwd.get())) << "Invalid generated password " << pwd.get();

    LOG_debug << "\t# Test lower and digits only";
    useUpper = useSymbols = false;
    useDigits = true;
    pwd.reset(MegaApi::generateRandomCharsPassword(useUpper, useDigits, useSymbols, length));
    ASSERT_TRUE(pwd);
    ASSERT_TRUE(std::strlen(pwd.get()) == length);
    ASSERT_TRUE(validatePassword(pwd.get())) << "Invalid generated password " << pwd.get();

    LOG_debug << "\t# Test lower and symbols only";
    useUpper = useDigits = false;
    useSymbols = true;
    pwd.reset(MegaApi::generateRandomCharsPassword(useUpper, useDigits, useSymbols, length));
    ASSERT_TRUE(pwd);
    ASSERT_TRUE(std::strlen(pwd.get()) == length);
    ASSERT_TRUE(validatePassword(pwd.get())) << "Invalid generated password " << pwd.get();

    LOG_debug << "\t# Test lower, upper, and digits";
    useSymbols = false;
    useUpper = useDigits = true;
    pwd.reset(MegaApi::generateRandomCharsPassword(useUpper, useDigits, useSymbols, length));
    ASSERT_TRUE(pwd);
    ASSERT_TRUE(std::strlen(pwd.get()) == length);
    ASSERT_TRUE(validatePassword(pwd.get())) << "Invalid generated password " << pwd.get();

    LOG_debug << "\t# Test lower, upper, and symbols";
    useDigits = false;
    useUpper = useSymbols = true;
    pwd.reset(MegaApi::generateRandomCharsPassword(useUpper, useDigits, useSymbols, length));
    ASSERT_TRUE(pwd);
    ASSERT_TRUE(std::strlen(pwd.get()) == length);
    ASSERT_TRUE(validatePassword(pwd.get())) << "Invalid generated password " << pwd.get();

    LOG_debug << "\t# Test lower, digits, and symbols";
    useUpper = false;
    useDigits = useSymbols = true;
    pwd.reset(MegaApi::generateRandomCharsPassword(useUpper, useDigits, useSymbols, length));
    ASSERT_TRUE(pwd);
    ASSERT_TRUE(std::strlen(pwd.get()) == length);
    ASSERT_TRUE(validatePassword(pwd.get())) << "Invalid generated password " << pwd.get();

    LOG_debug << "\t# Test lower, upper, digits, and symbols";
    useUpper = useDigits = useSymbols = true;
    pwd.reset(MegaApi::generateRandomCharsPassword(useUpper, useDigits, useSymbols, length));
    ASSERT_TRUE(pwd);
    ASSERT_TRUE(std::strlen(pwd.get()) == length);
    ASSERT_TRUE(validatePassword(pwd.get())) << "Invalid generated password " << pwd.get();
}

/**
 * @brief Enable test-notifications by setting their IDs in "^!tnotif".
 * Get enabled-notifications (from cmd("ug")."notifs").
 * Get the complete notifications (using cmd("gnotif")).
 * Set and get the last-read-notification ("^!lnotif").
 * Set and get the last-actioned-banner ("^!lbannr").
 */
TEST_F(SdkTest, DynamicMessageNotifs)
{
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    // Sending a null list of test-notifications should fail
    RequestTracker nullNotifsTracker(megaApi[0].get());
    megaApi[0]->enableTestNotifications(nullptr, &nullNotifsTracker);
    ASSERT_EQ(nullNotifsTracker.waitForResult(), API_EARGS);

    // Clear any test-notifications that may be leftovers from previous tests
    unique_ptr<MegaIntegerList> ids{ MegaIntegerList::createInstance() };
    RequestTracker clearNotifsTracker(megaApi[0].get());
    megaApi[0]->enableTestNotifications(ids.get(), &clearNotifsTracker); // clear "^!tnotif"
    ASSERT_EQ(clearNotifsTracker.waitForResult(), API_OK);

    // Clear last-read-notification
    RequestTracker clearLastReadNotifTracker(megaApi[0].get());
    megaApi[0]->setLastReadNotification(0, &clearLastReadNotifTracker); // clear "^!lnotif"
    ASSERT_EQ(clearLastReadNotifTracker.waitForResult(), API_OK);

    // Clear last-actioned-banner
    RequestTracker clearLastActionedBannerTracker(megaApi[0].get());
    megaApi[0]->setLastActionedBanner(0, &clearLastActionedBannerTracker); // clear "^!lbannr"
    ASSERT_EQ(clearLastActionedBannerTracker.waitForResult(), API_OK);

    // Get last-read-notification (not previously set)
    RequestTracker getLastReadNotifTracker(megaApi[0].get());
    megaApi[0]->getLastReadNotification(&getLastReadNotifTracker); // get "^!lnotif"
    ASSERT_EQ(getLastReadNotifTracker.waitForResult(), API_OK);
    ASSERT_EQ(static_cast<uint32_t>(getLastReadNotifTracker.request->getNumber()), 0u);

    // Fetch user data ("ug" command), and cache IDs of enabled-notifications (ug.notifs).
    RequestTracker userDataTracker(megaApi[0].get());
    megaApi[0]->getUserData(&userDataTracker);
    ASSERT_EQ(userDataTracker.waitForResult(), API_OK);

    // Get IDs of enabled-notifications, which are composed of
    // - IDs of test-notifications: IDs of notifications that should already exist, enabled as
    //   requested by the user;
    // - IDs of always-enabled notifications: IDs of notifications that already exist and are
    //   always enabled regardless of not being requested by the user.
    unique_ptr<MegaIntegerList> defaultNotifs{ megaApi[0]->getEnabledNotifications() }; // get IDs cached fom ug.notifs
    ASSERT_THAT(defaultNotifs, ::testing::NotNull());
    ASSERT_GE(defaultNotifs->size(), 0);

    // Get the complete notifications
    RequestTracker gnotifTracker(megaApi[0].get());
    megaApi[0]->getNotifications(&gnotifTracker); // send "gnotif" and process its response
    ASSERT_THAT(gnotifTracker.waitForResult(),
        ::testing::AnyOf(::testing::Eq(API_OK), ::testing::Eq(API_ENOENT)));
    const auto* notificationList = gnotifTracker.request->getMegaNotifications();
    ASSERT_THAT(notificationList, ::testing::NotNull());
    ASSERT_EQ(notificationList->size(), 0u);

    // Enable some test-notifications.
    // IDs 1,2,3,4,5 have been reserved to be "^!tnotif" only notifications.
    // Notifications with IDs 1~4 existed at the time of writing this test
    // Notification with ID 2 has icon
    // Notification with ID 9 has render modes ('m')
    ids->add(1);                                   // add notification with ID 1
    ids->add(2);                                   // add notification with ID 2
    ids->add(9); // add notification with ID 9
    ids->add(numeric_limits<uint32_t>::max() - 1); // dummy

    RequestTracker notifsTracker(megaApi[0].get());
    megaApi[0]->enableTestNotifications(ids.get(), &notifsTracker); // set "^!tnotif"
    ASSERT_EQ(notifsTracker.waitForResult(), API_OK);

    // Fetch user data ("ug" command), and cache IDs of enabled-notifications (ug.notifs).
    RequestTracker userDataTracker2(megaApi[0].get());
    megaApi[0]->getUserData(&userDataTracker2); // get ug.notifs again
    ASSERT_EQ(userDataTracker2.waitForResult(), API_OK);

    // Get IDs of enabled-notifications
    unique_ptr<MegaIntegerList> enabledNotifs{ megaApi[0]->getEnabledNotifications() };
    ASSERT_THAT(enabledNotifs, ::testing::NotNull());
    ASSERT_EQ(
        enabledNotifs->size(),
        3); // only IDs of existing notifications will be there, dummy IDs will not be included
    ASSERT_EQ(enabledNotifs->get(0), 1);
    ASSERT_EQ(enabledNotifs->get(1), 2);
    ASSERT_EQ(enabledNotifs->get(2), 9);

    // Get the complete notifications (corresponding only to existing IDs)
    RequestTracker gnotifTracker2(megaApi[0].get());
    megaApi[0]->getNotifications(&gnotifTracker2); // send "gnotif" and process its response
    ASSERT_EQ(gnotifTracker2.waitForResult(), API_OK);
    const auto* notificationList2 = gnotifTracker2.request->getMegaNotifications();
    ASSERT_THAT(notificationList2, ::testing::NotNull());
    ASSERT_EQ(notificationList2->size(), 3u);

    // validate complete notifications
    ASSERT_NO_FATAL_FAILURE(validateNotification(notificationList2->get(0), 1, HasIcon::NO));
    ASSERT_NO_FATAL_FAILURE(validateNotification(notificationList2->get(1), 2, HasIcon::YES));
    ASSERT_NO_FATAL_FAILURE(validateNotification(notificationList2->get(2), 9, HasIcon::NO));
    unique_ptr<MegaStringList> renderModes{notificationList2->get(2)->getRenderModes()};
    ASSERT_THAT(renderModes, ::testing::NotNull());
    ASSERT_GT(renderModes->size(), 0);
    for (int i = 0; i < renderModes->size(); ++i)
    {
        unique_ptr<MegaStringMap> fields{
            notificationList2->get(2)->getRenderModeFields(renderModes->get(i))};
        ASSERT_THAT(fields, ::testing::NotNull());
        if (string{"btp"} == renderModes->get(i))
        {
            ASSERT_EQ(fields->size(), 2);
            ASSERT_THAT(fields->get("href"), ::testing::NotNull());
            ASSERT_THAT(fields->get("img"), ::testing::NotNull());
        }
        else if (string{"brp"} == renderModes->get(i))
        {
            ASSERT_EQ(fields->size(), 2);
            ASSERT_THAT(fields->get("href"), ::testing::NotNull());
            ASSERT_THAT(fields->get("img"), ::testing::NotNull());
        }
        else if (string{"bti"} == renderModes->get(i))
        {
            ASSERT_EQ(fields->size(), 1);
            ASSERT_THAT(fields->get("src"), ::testing::NotNull());
        }
        else if (string{"bri"} == renderModes->get(i))
        {
            ASSERT_EQ(fields->size(), 1);
            ASSERT_THAT(fields->get("src"), ::testing::NotNull());
        }
    }

    // Set last-read-notification
    const uint32_t lastReadNotifId = numeric_limits<uint32_t>::max() - 2; // dummy value
    RequestTracker setLastReadNotifTracker(megaApi[0].get());
    megaApi[0]->setLastReadNotification(lastReadNotifId, &setLastReadNotifTracker); // set "^!lnotif"
    ASSERT_EQ(setLastReadNotifTracker.waitForResult(), API_OK);

    // Get last-read-notification
    RequestTracker getLastReadNotifTracker2(megaApi[0].get());
    megaApi[0]->getLastReadNotification(&getLastReadNotifTracker2); // get "^!lnotif"
    ASSERT_EQ(getLastReadNotifTracker2.waitForResult(), API_OK);
    ASSERT_EQ(static_cast<uint32_t>(getLastReadNotifTracker2.request->getNumber()), lastReadNotifId);

    // Clear a previusly set last-read-notification
    RequestTracker clearLastReadNotifTracker2(megaApi[0].get());
    megaApi[0]->setLastReadNotification(0, &clearLastReadNotifTracker2); // clear "^!lnotif"
    ASSERT_EQ(clearLastReadNotifTracker2.waitForResult(), API_OK);

    // Set last-actioned-banner
    const uint32_t lastActionedBannerId = numeric_limits<uint32_t>::max() - 3; // dummy value
    RequestTracker setLastActionedBannerTracker(megaApi[0].get());
    megaApi[0]->setLastActionedBanner(lastActionedBannerId, &setLastActionedBannerTracker); // set "^!lbannr"
    ASSERT_EQ(setLastActionedBannerTracker.waitForResult(), API_OK);

    // Get last-actioned-banner
    RequestTracker getLastActionedBannerTracker2(megaApi[0].get());
    megaApi[0]->getLastActionedBanner(&getLastActionedBannerTracker2); // get "^!lbannr"
    ASSERT_EQ(getLastActionedBannerTracker2.waitForResult(), API_OK);
    ASSERT_EQ(static_cast<uint32_t>(getLastActionedBannerTracker2.request->getNumber()), lastActionedBannerId);

    // Clear a previously set last-actioned-banner
    RequestTracker clearLastActionedBannerTracker2(megaApi[0].get());
    megaApi[0]->setLastActionedBanner(0, &clearLastActionedBannerTracker2); // clear "^!lbannr"
    ASSERT_EQ(clearLastActionedBannerTracker2.waitForResult(), API_OK);

    // Clear test-notifications
    ids.reset(MegaIntegerList::createInstance());
    RequestTracker clearNotifsTracker2(megaApi[0].get());
    megaApi[0]->enableTestNotifications(ids.get(), &clearNotifsTracker2); // clear "^!tnotif"
    ASSERT_EQ(clearNotifsTracker2.waitForResult(), API_OK);

    // Fetch user data ("ug" command), and cache IDs of enabled-notifications (ug.notifs).
    RequestTracker userDataTracker3(megaApi[0].get());
    megaApi[0]->getUserData(&userDataTracker3);
    ASSERT_EQ(userDataTracker3.waitForResult(), API_OK);

    // Get IDs of enabled-notifications
    defaultNotifs.reset(megaApi[0]->getEnabledNotifications()); // get cached value of ug.notifs
    ASSERT_THAT(defaultNotifs, ::testing::NotNull());
    ASSERT_EQ(defaultNotifs->size(), 0);
}

/**
 * @brief SdkNodeDescription
 * Steps:
 *  - Create file and upload
 *  - Set description
 *  - Locallogout
 *  - Resume
 *  - Check description
 *  - Update description
 *  - Remove description
 *  - Update the contents of the file to create a new version
 *  - Check the description is retained across versions
 *
 */
TEST_F(SdkTest, SdkNodeDescription)
{
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));
    LOG_info << "___TEST SdkNodeDescription___";

    unique_ptr<MegaNode> rootnodeA(megaApi[0]->getRootNode());
    ASSERT_TRUE(rootnodeA);

    string filename = "test.txt";
    sdk_test::LocalTempFile testTempFile(filename, 0);
    MegaHandle mh = 0;
    ASSERT_EQ(MegaError::API_OK,
              doStartUpload(0,
                            &mh,
                            filename.data(),
                            rootnodeA.get(),
                            nullptr /*fileName*/,
                            ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
                            nullptr /*appData*/,
                            false /*isSourceTemporary*/,
                            false /*startFirst*/,
                            nullptr /*cancelToken*/))
        << "Cannot upload a test file";

    auto changeNodeDescription = [this](MegaHandle nodeHandle, const char* description)
    {
        bool check = false;
        mApi[0].mOnNodesUpdateCompletion =
            createOnNodesUpdateLambda(nodeHandle, MegaNode::CHANGE_TYPE_DESCRIPTION, check);
        RequestTracker trackerSetDescription(megaApi[0].get());
        std::unique_ptr<MegaNode> testNode(megaApi[0]->getNodeByHandle(nodeHandle));
        megaApi[0]->setNodeDescription(testNode.get(), description, &trackerSetDescription);
        ASSERT_EQ(trackerSetDescription.waitForResult(), API_OK);
        ASSERT_TRUE(waitForResponse(&check))
            << "Node hasn't updated description after " << maxTimeout << " seconds";
        resetOnNodeUpdateCompletionCBs();

        std::unique_ptr<MegaNode> node(megaApi[0]->getNodeByHandle(nodeHandle));
        ASSERT_TRUE(node);
        const char* nodeDescription = node->getDescription();
        if (description == nullptr || nodeDescription == nullptr)
        {
            ASSERT_EQ(description, nodeDescription);
            return;
        }

        ASSERT_STREQ(description, node->getDescription());
    };

    // Set description
    std::string description{"This is a test description to search in its content"};
    changeNodeDescription(mh, description.c_str());

    std::string descriptionFilter{"search"};
    std::unique_ptr<MegaSearchFilter> filter(MegaSearchFilter::createInstance());
    filter->byDescription(descriptionFilter.c_str());
    std::unique_ptr<MegaNodeList> nodeList(megaApi[0]->search(filter.get()));
    ASSERT_EQ(nodeList->size(), 1) << *nodeList;

    std::unique_ptr<MegaSearchFilter> filterChildren(MegaSearchFilter::createInstance());
    filterChildren->byDescription(descriptionFilter.c_str());
    filterChildren->byLocationHandle(rootnodeA->getHandle());
    nodeList.reset(megaApi[0]->getChildren(filterChildren.get()));
    ASSERT_EQ(nodeList->size(), 1) << *nodeList;

    std::string descriptionFilterNoFind{"searchin"};
    filter->byDescription(descriptionFilterNoFind.c_str());
    nodeList.reset(megaApi[0]->search(filter.get()));
    ASSERT_EQ(nodeList->size(), 0) << *nodeList;

    filterChildren->byDescription(descriptionFilterNoFind.c_str());
    nodeList.reset(megaApi[0]->getChildren(filterChildren.get()));
    ASSERT_EQ(nodeList->size(), 0) << *nodeList;

    std::string descriptionWithoutCapitalLetter("this");
    filter->byDescription(descriptionWithoutCapitalLetter.c_str());
    nodeList.reset(megaApi[0]->search(filter.get()));
    ASSERT_EQ(nodeList->size(), 1) << *nodeList;

    std::string descriptionWithAccent("thiŚ");
    filter->byDescription(descriptionWithAccent.c_str());
    nodeList.reset(megaApi[0]->search(filter.get()));
    ASSERT_EQ(nodeList->size(), 1) << *nodeList;

    auto& target = mApi[0];
    std::unique_ptr<char[]> session(dumpSession());
    locallogout(0);
    resumeSession(session.get());
    target.resetlastEvent();
    fetchnodes(0);

    // make sure that client is up to date (upon logout, recent changes might not be committed to DB)
    ASSERT_TRUE(WaitFor(
        [&target]()
        {
            return target.lastEventsContain(MegaEvent::EVENT_NODES_CURRENT);
        },
        10000))
        << "Timeout expired to receive actionpackets";

    std::unique_ptr<MegaNode> node(megaApi[0]->getNodeByHandle(mh));
    ASSERT_TRUE(node);
    ASSERT_EQ(description, node->getDescription());

    // Update description
    changeNodeDescription(mh, "Description modified");

    changeNodeDescription(mh, "Description with line break\n Other line\n and other more");

    // Remove description
    changeNodeDescription(mh, nullptr);

    const std::string lastDescription{
        "This is a description with *stars* to test if it's found correctly"};
    changeNodeDescription(mh, lastDescription.c_str());

    MegaHandle nodeCopiedHandle = UNDEF;
    ASSERT_EQ(API_OK, doCopyNode(0, &nodeCopiedHandle, node.get(), rootnodeA.get(), "test2.txt")) << "Cannot create a copy of a node";

    changeNodeDescription(nodeCopiedHandle, "This is a description without stars to test if it's found correctly");

    filter->byDescription("stars");
    nodeList.reset(megaApi[0]->search(filter.get()));
    ASSERT_EQ(nodeList->size(), 2) << *nodeList;

    filter->byDescription("*star");
    nodeList.reset(megaApi[0]->search(filter.get()));
    ASSERT_EQ(nodeList->size(), 1) << *nodeList;

    LOG_debug
        << "[SdkTest::SdkNodeDescription] Changing the contents of test.txt to force a new version";
    const std::string newVersionFileName = "test_new_version.txt";
    sdk_test::LocalTempFile fNewVersion(newVersionFileName, 1);
    MegaHandle mhNew = INVALID_HANDLE;
    ASSERT_EQ(MegaError::API_OK,
              doStartUpload(0,
                            &mhNew,
                            newVersionFileName.c_str(),
                            rootnodeA.get(),
                            filename.c_str(),
                            ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
                            nullptr /*appData*/,
                            false /*isSourceTemporary*/,
                            false /*startFirst*/,
                            nullptr /*cancelToken*/))
        << "Cannot update the test file";
    ASSERT_NE(mhNew, INVALID_HANDLE);

    std::unique_ptr<MegaNode> oldNode(megaApi[0]->getNodeByHandle(mh));
    ASSERT_NE(oldNode, nullptr);

    std::unique_ptr<MegaNode> newNode(megaApi[0]->getNodeByHandle(mhNew));
    ASSERT_NE(newNode, nullptr);

    // Check there are two versions for the file
    unique_ptr<MegaNodeList> allVersions(megaApi[0]->getVersions(newNode.get()));
    ASSERT_EQ(allVersions->size(), 2);

    EXPECT_STREQ(oldNode->getDescription(), newNode->getDescription())
        << "Description is not maintained after file update";
    EXPECT_STREQ(oldNode->getDescription(), lastDescription.c_str())
        << "The description of the old version has changed";
}

/**
 * @brief Test returned value by MegaApi::getNumNodesAtCacheLRU
 * Steps:
 *  - Check intial number of nodes at LRU cache
 *  - Set cache LRU limit 500
 *  - Add a new file and create 100 copies
 *  - Check number of nodes at cache LRU
 *  - Reduce size at cache LRU
 *  - Check number of nodes at cache LRU
 *  - Increase cache LRU size (60)
 *  - Check number of nodes at cache LRU
 *  - Copy same node 20 times more
 *  - Check number of nodes at cache LRU
 */
TEST_F(SdkTest, SdkCacheLRU)
{
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));
    LOG_info << "___TEST SdkCacheLRU___";

    unique_ptr<MegaNode> rootnodeA(megaApi[0]->getRootNode());
    ASSERT_TRUE(rootnodeA);

    uint64_t initialNumberNodes = megaApi[0]->getNumNodesAtCacheLRU();

    string filename = "test.txt";
    createFile(filename, false);
    MegaHandle mh = 0;
    ASSERT_EQ(MegaError::API_OK,
              doStartUpload(0,
                            &mh,
                            filename.data(),
                            rootnodeA.get(),
                            nullptr /*fileName*/,
                            ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
                            nullptr /*appData*/,
                            false /*isSourceTemporary*/,
                            false /*startFirst*/,
                            nullptr /*cancelToken*/))
        << "Cannot upload a test file";

    std::unique_ptr<MegaNode> node(megaApi[0]->getNodeByHandle(mh));

    megaApi[0]->setLRUCacheSize(500);

    unsigned numberOfCopies = 100;
    for (unsigned i = 0; i < numberOfCopies; ++i)
    {
        MegaHandle newNodeHandle;
        std::string newName{filename + std::to_string(i)};
        ASSERT_EQ(API_OK, doCopyNode(0, &newNodeHandle, node.get(), rootnodeA.get(), newName.c_str()));
    }

    uint64_t numNodeCacheLRU = megaApi[0]->getNumNodesAtCacheLRU();
    ASSERT_EQ(numNodeCacheLRU,
              numberOfCopies + 1 + initialNumberNodes); // 101 -> initial node + 100 copies

    uint64_t cacheLRUSize = 50;
    megaApi[0]->setLRUCacheSize(cacheLRUSize);

    numNodeCacheLRU = megaApi[0]->getNumNodesAtCacheLRU();
    ASSERT_EQ(numNodeCacheLRU, cacheLRUSize);

    uint64_t cacheLRUNewSize = 60;
    megaApi[0]->setLRUCacheSize(cacheLRUNewSize);

    numNodeCacheLRU = megaApi[0]->getNumNodesAtCacheLRU();
    ASSERT_EQ(numNodeCacheLRU, cacheLRUSize);

    for (unsigned i = 0; i < 20; ++i)
    {
        MegaHandle newNodeHandle;
        std::string newName{filename + std::to_string(numberOfCopies + i)};
        ASSERT_EQ(API_OK, doCopyNode(0, &newNodeHandle, node.get(), rootnodeA.get(), newName.c_str()));
    }

    numNodeCacheLRU = megaApi[0]->getNumNodesAtCacheLRU();
    ASSERT_EQ(numNodeCacheLRU, cacheLRUNewSize);
}

/**
 * @brief SdkTestVPN
 *
 * Test that MEGA VPN app receives Action Packets.
 */
TEST_F(SdkTest, SdkTestVPN)
{
    LOG_info << "___TEST SdkTestVPN";

    // Login first client
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    // get First Name
    string origName = "testingName"; // default value in case it is not set
    {
        RequestTracker getNameTracker(megaApi[0].get());
        megaApi[0]->getUserAttribute(MegaApi::USER_ATTR_FIRSTNAME, &getNameTracker);
        ErrorCodes res = getNameTracker.waitForResult();
        if (res != API_ENOENT)
        {
            ASSERT_EQ(res, API_OK) << "Failed to get First Name";
            ASSERT_THAT(getNameTracker.request->getText(), ::testing::NotNull());
            origName = getNameTracker.request->getText();
        }
    }

    // Prepare VPN client with the same account
    mApi.resize(2);
    megaApi.resize(2);
    const auto [email, pass] = getEnvVarAccounts().getVarValues(0);
    ASSERT_NO_FATAL_FAILURE(configureTestInstance(1, email, pass, true, MegaApi::CLIENT_TYPE_VPN));

    // Login VPN client
    {
        RequestTracker loginTracker(megaApi[1].get());
        megaApi[1]->login(mApi[1].email.c_str(), mApi[1].pwd.c_str(), &loginTracker);
        ASSERT_EQ(loginTracker.waitForResult(), API_OK) << "VPN client: failed to login";
        bool& fetchnodesDone = mApi[1].requestFlags[MegaRequest::TYPE_FETCH_NODES] = false;
        ASSERT_NO_FATAL_FAILURE(fetchnodes(1));
        ASSERT_TRUE(WaitFor([&fetchnodesDone]() { return fetchnodesDone; }, 60 * 1000))
            << "VPN client: fetchnodesDone not received";

        // test resume-session while at it
        unique_ptr<char[]> session(dumpSession(1));
        ASSERT_NO_FATAL_FAILURE(locallogout(1));
        ASSERT_NO_FATAL_FAILURE(resumeSession(session.get(), 1));
        ASSERT_NO_FATAL_FAILURE(fetchnodes(1));
    }

    // update First Name in default client
    string newName = origName + "_upd";
    bool& nameUpdated = mApi[1].userFirstNameUpdated = false;
    {
        RequestTracker setNameTracker(megaApi[0].get());
        megaApi[0]->setUserAttribute(MegaApi::USER_ATTR_FIRSTNAME, newName.c_str(), &setNameTracker);
        ASSERT_EQ(setNameTracker.waitForResult(), API_OK) << "Default client: failed to update First Name";

        // wait for VPN client to receive the name update
        ASSERT_TRUE(WaitFor([&nameUpdated]() { return nameUpdated; }, 60 * 1000))
            << "VPN client: AP about updated First Name not received";

        // get First Name from VPN client and confirm the update
        nameUpdated = false; // to be ignored this time; is set after getting UA
        RequestTracker getNameTracker(megaApi[1].get());
        megaApi[1]->getUserAttribute(MegaApi::USER_ATTR_FIRSTNAME, &getNameTracker);
        ASSERT_EQ(getNameTracker.waitForResult(), API_OK) << "VPN client: failed to get updated First Name";
        ASSERT_THAT(getNameTracker.request->getText(), ::testing::NotNull());
        ASSERT_EQ(newName, getNameTracker.request->getText());
        WaitFor([&nameUpdated]() { return nameUpdated; }, 5 * 1000); // to be ignored
    }

    // reset First Name to original value
    {
        // truncate remmnants of older test failures
        while (origName.length() > 4 && !origName.compare(origName.length() - 4, 4, "_upd"))
            origName.erase(origName.length() - 4);

        nameUpdated = false;
        RequestTracker setNameTracker(megaApi[0].get());
        megaApi[0]->setUserAttribute(MegaApi::USER_ATTR_FIRSTNAME, origName.c_str(), &setNameTracker);
        ASSERT_EQ(setNameTracker.waitForResult(), API_OK) << "Default client: failed to set original First Name";

        // wait for VPN client to receive the name update
        ASSERT_TRUE(WaitFor([&nameUpdated]() { return nameUpdated; }, 60 * 1000))
            << "VPN client: AP for reset First Name not received";

        // get First Name from VPN client and confirm the reset
        RequestTracker getNameTracker(megaApi[1].get());
        megaApi[1]->getUserAttribute(MegaApi::USER_ATTR_FIRSTNAME, &getNameTracker);
        ASSERT_EQ(getNameTracker.waitForResult(), API_OK) << "VPN client: failed to get reset First Name";
        ASSERT_THAT(getNameTracker.request->getText(), ::testing::NotNull());
        ASSERT_EQ(origName, getNameTracker.request->getText());
    }
}

/**
 * @brief Test checks deleting user attributes
 * Steps:
 *  - Set firstname attribute to make sure it exists
 *  - Delete firstname attribute
 *  - Get firstname attribute to check it does not exist anymore
 *  - Try to delete firstname attribute to get ENOENT response
 */
TEST_F(SdkTest, SdkDeleteUserAttribute)
{
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    string firstname = "testingName";
    ASSERT_EQ(API_OK,
              synchronousSetUserAttribute(0, MegaApi::USER_ATTR_FIRSTNAME, firstname.c_str()));

    RequestTracker deleteAttributeTracker(megaApi[0].get());
    megaApi[0]->deleteUserAttribute(MegaApi::USER_ATTR_FIRSTNAME, &deleteAttributeTracker);
    ASSERT_EQ(API_OK, deleteAttributeTracker.waitForResult());

    ASSERT_EQ(API_ENOENT, synchronousGetUserAttribute(0, MegaApi::USER_ATTR_FIRSTNAME));

    RequestTracker secondDeleteAttributeTracker(megaApi[0].get());
    megaApi[0]->deleteUserAttribute(MegaApi::USER_ATTR_FIRSTNAME, &secondDeleteAttributeTracker);
    ASSERT_EQ(API_ENOENT, secondDeleteAttributeTracker.waitForResult());

    ASSERT_EQ(API_OK,
              synchronousSetUserAttribute(0, MegaApi::USER_ATTR_FIRSTNAME, firstname.c_str()));
}

TEST_F(SdkTest, GetFeaturePlans)
{
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));
    LOG_info << "___TEST GetFeaturePlans___";

    int err = synchronousGetPricing(0);
    ASSERT_EQ(err, API_OK) << "synchronousGetPricing() failed: " << MegaError::getErrorString(err);

    for (int i = 0; i < mApi[0].mMegaPricing->getNumProducts(); ++i)
    {
        if (mApi[0].mMegaPricing->isFeaturePlan(i))
        {
            ASSERT_NE(mApi[0].mMegaPricing->getHandle(i), INVALID_HANDLE);
            ASSERT_EQ(mApi[0].mMegaPricing->getProLevel(i), 99999);
            ASSERT_NE(mApi[0].mMegaPricing->getMonths(i), 0);
            ASSERT_NE(mApi[0].mMegaPricing->getAmount(i), 0);
            ASSERT_STRNE(mApi[0].mMegaPricing->getDescription(i), "");

            std::unique_ptr<MegaStringIntegerMap> features{ mApi[0].mMegaPricing->getFeatures(i) };
            ASSERT_NE(features->size(), 0);

            ASSERT_STRNE(mApi[0].mMegaPricing->getIosID(i), "");
            ASSERT_STRNE(mApi[0].mMegaPricing->getAndroidID(i), "");
            ASSERT_NE(mApi[0].mMegaPricing->getAmountMonth(i), 0);
        }
    }
}

/**
 * @brief GetActivePlansAndFeatures
 */
TEST_F(SdkTest, GetActivePlansAndFeatures)
{
    LOG_info << "___TEST GetActivePlansAndFeaturess___";
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    RequestTracker accDetailsTracker(megaApi[0].get());
    megaApi[0]->getAccountDetails(&accDetailsTracker);
    ASSERT_EQ(accDetailsTracker.waitForResult(), API_OK) << "Failed to get account details";

    std::unique_ptr<MegaAccountDetails> accountDetails(
        accDetailsTracker.request->getMegaAccountDetails());
    ASSERT_TRUE(accountDetails) << "Missing account details";

    int proLevel = MegaAccountDetails::ACCOUNT_TYPE_FREE;
    set<string> featuresGranted;
    for (int i = 0; i < accountDetails->getNumPlans(); ++i)
    {
        std::unique_ptr<MegaAccountPlan> plan(accountDetails->getPlan(i));
        std::unique_ptr<MegaStringList> features(plan->getFeatures());
        for (int j = 0; j < features->size(); ++j)
        {
            // Acumulate granted features
            featuresGranted.emplace(features->get(j));
        }

        if (plan->isProPlan())
        {
            ASSERT_EQ(proLevel, MegaAccountDetails::ACCOUNT_TYPE_FREE)
                << "More than one PRO plan has been received";
            proLevel = plan->getAccountLevel();
            ASSERT_GT(proLevel, MegaAccountDetails::ACCOUNT_TYPE_FREE)
                << "PRO level is ACCOUNT_TYPE_FREE";
            ASSERT_NE(proLevel, MegaAccountDetails::ACCOUNT_TYPE_FEATURE)
                << "PRO plan is a feature plan";
            ASSERT_EQ(proLevel, accountDetails->getProLevel())
                << "PRO level of the plan does not match the PRO account level";
        }
        else // Feature plan
        {
            ASSERT_EQ(plan->getAccountLevel(), MegaAccountDetails::ACCOUNT_TYPE_FEATURE)
                << "Feature plan has not a feature account level";
            ASSERT_GT(features->size(), 0) << "Feature plan does not grant any feature";
        }
    }

    // Compare features contained in the plans with the features received for the account.
    ASSERT_EQ(featuresGranted.size(), accountDetails->getNumActiveFeatures())
        << "Features in active plans don't match the number of features of the account";
    m_time_t currTime = m_time();
    for (int i = 0; i < accountDetails->getNumActiveFeatures(); ++i)
    {
        std::unique_ptr<MegaAccountFeature> feature(accountDetails->getActiveFeature(i));
        ASSERT_GE(feature->getExpiry(), currTime) << "Received an expired feature";
        string featureId(std::unique_ptr<const char[]>(feature->getId()).get());
        ASSERT_NE(featuresGranted.find(featureId), featuresGranted.end())
            << "Feature " << featureId << " is not present in any plan";
    }
}

/**
 * @brief SdkTestSharesWhenMegaHosted
 *
 *  - Create a folder
 *  - Create a writable, mega-hosted link to the folder
 *  - Confirm that an encryption-key was used for the share-key (sent via "l"."sk")
 */
TEST_F(SdkTest, SdkTestSharesWhenMegaHosted)
{
    LOG_info << "___TEST SharesWhenMegaHosted___";
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    // Create some nodes to share
    //  |--Shared-folder

    std::unique_ptr<MegaNode> rootNode{megaApi[0]->getRootNode()};
    MegaHandle hFolder = createFolder(0, "Shared-folder", rootNode.get());
    ASSERT_NE(hFolder, UNDEF);
    std::unique_ptr<MegaNode> nFolder(megaApi[0]->getNodeByHandle(hFolder));
    ASSERT_THAT(nFolder, ::testing::NotNull());

    RequestTracker rt(megaApi[0].get());
    megaApi[0]->exportNode(nFolder.get(), 0, true /*writable*/, true /*megaHosted*/, &rt);
    ASSERT_EQ(rt.waitForResult(), API_OK);

    // Test that encryption-key was used for "sk" (share-key) sent via "l" command
    ASSERT_THAT(rt.request->getPassword(), ::testing::NotNull());
    string b64Key{rt.request->getPassword()};
    string binKey = Base64::atob(b64Key);
    ASSERT_FALSE(binKey.empty());
}

TEST_F(SdkTest, SdkTestRestoreNodeVersion)
{
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));
    LOG_info << "___TEST SdkTestRestoreNodeVersion___";

    const auto addTagToNode =
        [&api = megaApi[0]](const std::unique_ptr<MegaNode>& node, const std::string& tag)
    {
        RequestTracker trackerAddTag(api.get());
        api->addNodeTag(node.get(), tag.c_str(), &trackerAddTag);
        ASSERT_EQ(trackerAddTag.waitForResult(), API_OK);
    };

    const auto setNodeDescription =
        [&api = megaApi[0]](const std::unique_ptr<MegaNode>& node, const std::string& description)
    {
        RequestTracker trackerSetDescription(api.get());
        api->setNodeDescription(node.get(), description.c_str(), &trackerSetDescription);
        ASSERT_EQ(trackerSetDescription.waitForResult(), API_OK);
    };

    LOG_debug << "[SdkTest::SdkTestRestoreNodeVersion] Creating root node";
    std::unique_ptr<MegaNode> rootnodeA(megaApi[0]->getRootNode());
    ASSERT_TRUE(rootnodeA);

    LOG_debug << "[SdkTest::SdkTestRestoreNodeVersion] Uploading first version of the file";
    const std::string originalContent{"Original content"};
    MegaHandle originalHandle = 0;
    {
        const std::string filename = "test.txt";
        sdk_test::LocalTempFile testTempFile(filename, originalContent);
        ASSERT_EQ(MegaError::API_OK,
                  doStartUpload(0,
                                &originalHandle,
                                filename.data(),
                                rootnodeA.get(),
                                nullptr /*fileName*/,
                                ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
                                nullptr /*appData*/,
                                false /*isSourceTemporary*/,
                                false /*startFirst*/,
                                nullptr /*cancelToken*/))
            << "Cannot upload a test file";
    }
    LOG_debug << "[SdkTest::SdkTestRestoreNodeVersion] Setting metadata for first version";
    std::unique_ptr<MegaNode> originalNodeVersion(megaApi[0]->getNodeByHandle(originalHandle));
    ASSERT_TRUE(originalNodeVersion);
    ASSERT_NO_FATAL_FAILURE(addTagToNode(originalNodeVersion, "originaltag"));
    ASSERT_NO_FATAL_FAILURE(setNodeDescription(originalNodeVersion, "original description"));

    LOG_debug << "[SdkTest::SdkTestRestoreNodeVersion] Uploading second version of the file";
    MegaHandle currentHandle = 0;
    {
        const std::string filename = "test.txt";
        sdk_test::LocalTempFile testTempFile(filename, "Current content");
        ASSERT_EQ(MegaError::API_OK,
                  doStartUpload(0,
                                &currentHandle,
                                filename.data(),
                                rootnodeA.get(),
                                nullptr /*fileName*/,
                                ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
                                nullptr /*appData*/,
                                false /*isSourceTemporary*/,
                                false /*startFirst*/,
                                nullptr /*cancelToken*/))
            << "Cannot upload a test file";
    }
    LOG_debug << "[SdkTest::SdkTestRestoreNodeVersion] Setting metadata for second version";
    std::unique_ptr<MegaNode> currentNodeVersion(megaApi[0]->getNodeByHandle(currentHandle));
    ASSERT_TRUE(currentNodeVersion);
    ASSERT_NO_FATAL_FAILURE(addTagToNode(currentNodeVersion, "currenttag"));
    ASSERT_NO_FATAL_FAILURE(setNodeDescription(currentNodeVersion, "current description"));

    LOG_debug << "[SdkTest::SdkTestRestoreNodeVersion] Restoring first version";
    RequestTracker trackerRestoreVersion(megaApi[0].get());
    megaApi[0]->restoreVersion(originalNodeVersion.get(), &trackerRestoreVersion);
    ASSERT_EQ(trackerRestoreVersion.waitForResult(), API_OK);

    LOG_debug << "[SdkTest::SdkTestRestoreNodeVersion] Check description remains the same";
    std::unique_ptr<MegaNode> restoredNode(megaApi[0]->getNodeByPath("/test.txt"));
    ASSERT_TRUE(restoredNode);
    EXPECT_STREQ(restoredNode->getDescription(), "current description");

    LOG_debug << "[SdkTest::SdkTestRestoreNodeVersion] Check tags remain the same";
    std::unique_ptr<MegaStringList> tags(restoredNode->getTags());
    ASSERT_TRUE(tags);
    const auto tagsVec = stringListToVector(*tags);
    EXPECT_THAT(tagsVec, testing::UnorderedElementsAreArray({"originaltag", "currenttag"}));
    // Check the contents have changed to original

    LOG_debug << "[SdkTest::SdkTestRestoreNodeVersion] Check the contents has been restored";
    const std::string downloadFileName{DOTSLASH "downfile.txt"};
    TransferTracker downloadListener(megaApi[0].get());
    megaApi[0]->startDownload(
        restoredNode.get(),
        downloadFileName.c_str(),
        nullptr /*customName*/,
        nullptr /*appData*/,
        false /*startFirst*/,
        nullptr /*cancelToken*/,
        MegaTransfer::COLLISION_CHECK_FINGERPRINT /*collisionCheck*/,
        MegaTransfer::COLLISION_RESOLUTION_NEW_WITH_N /* collisionResolution */,
        false /* undelete */,
        &downloadListener);
    ASSERT_EQ(downloadListener.waitForResult(), API_OK);

    // Read file
    std::ifstream file{downloadFileName};
    ASSERT_TRUE(file);
    const std::string contents{std::istreambuf_iterator<char>(file),
                               std::istreambuf_iterator<char>()};
    // Check contents
    ASSERT_EQ(contents, originalContent);
}

/**
 * @brief TEST_F SdkRemoveTempFilesUponUploadTransfers
 *
 * Tests that file uploads transfers started with isSourceTemporary flag finally removes temporary
 * files in local filesystem
 *
 * # Test1 Upload file F1
 * # Test2 Upload file F1 again
 * # Test3 Upload file F2 with same fingerprint than F1 in cloud (Node copy)
 * # Test4 Upload file F1 (modified) with different fingerprint than F1 in cloud
 * # Test5 Upload file F3 and cancel transfer
 */
TEST_F(SdkTest, SdkRemoveTempFilesUponUploadTransfers)
{
    LOG_info << "___TEST SdkRemoveTempFilesUponUploadTransfers___";
    constexpr int accIdx{0};
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));
    std::shared_ptr<MegaNode> rootnode(megaApi[accIdx]->getRootNode());
    ASSERT_TRUE(!!rootnode) << "Cannot retrieve rootnode";

    auto uploadFile = [&](MegaNode* n, const fs::path& filePath, bool cancelTransfer = false)
    {
        TransferTracker uploadListener(megaApi[accIdx].get());
        megaApi[accIdx]->startUpload(filePath.u8string().c_str(),
                                     n,
                                     nullptr /*fileName*/,
                                     ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
                                     nullptr /*appData*/,
                                     true /*isSourceTemporary*/,
                                     false /*startFirst*/,
                                     nullptr /*cancelToken*/,
                                     &uploadListener);

        if (cancelTransfer)
        {
            ASSERT_EQ(API_OK, synchronousCancelTransfers(accIdx, MegaTransfer::TYPE_UPLOAD));
            ASSERT_EQ(API_EINCOMPLETE, uploadListener.waitForResult());
        }
        else
        {
            ASSERT_EQ(API_OK, uploadListener.waitForResult())
                << "Cannot upload local file: " << filePath;
            std::unique_ptr<MegaNode> n1(
                megaApi[accIdx]->getNodeByHandle(uploadListener.resultNodeHandle));
            ASSERT_TRUE(!!n1) << "Cannot get node in cloud drive file: "
                              << toHandle(uploadListener.resultNodeHandle);
        }

        ASSERT_TRUE(uploadListener.mTempFileRemoved)
            << "Temporary file couldn't be removed: " << filePath;
        ASSERT_TRUE(!fs::exists(filePath)) << "File still exists locally: " << filePath;
    };

    auto modifyFile = [](const fs::path& filePath, const std::string& text)
    {
        std::ofstream f(filePath, std::ios::app);
        f << text;
        f.close();
    };

    LOG_debug << "### Test1 (SdkRemoveTempFilesUponUploadTransfers) Upload file F1 ####";
    const fs::path f1Path = fs::current_path() / fs::u8path("file1.txt");
    ASSERT_TRUE(createFile(f1Path.u8string())) << "Couldn't create " << f1Path;
    ASSERT_NO_FATAL_FAILURE(uploadFile(rootnode.get(), f1Path));

    LOG_debug << "### Test2 (SdkRemoveTempFilesUponUploadTransfers) Upload file F1 again ####";
    ASSERT_TRUE(createFile(f1Path.u8string())) << "Couldn't create " << f1Path;
    const fs::path f2Path = fs::current_path() / fs::u8path("file2.txt");
    sdk_test::copyFileFromTestData(f1Path, f2Path);
    ASSERT_NO_FATAL_FAILURE(uploadFile(rootnode.get(), f1Path));

    LOG_debug << "### Test3 (SdkRemoveTempFilesUponUploadTransfers) Upload file F2 with same "
                 "fingerprint than F1 (Node copy) ####";
    ASSERT_NO_FATAL_FAILURE(uploadFile(rootnode.get(), f2Path));

    LOG_debug << "### Test4 (SdkRemoveTempFilesUponUploadTransfers) Upload file F1 (modified) with "
                 "different fingerprint than F1 in cloud ####";
    ASSERT_TRUE(createFile(f1Path.u8string())) << "Couldn't create " << f1Path;
    ASSERT_NO_FATAL_FAILURE(uploadFile(rootnode.get(), f1Path));
    modifyFile(f1Path, "Update");
    ASSERT_NO_FATAL_FAILURE(uploadFile(rootnode.get(), f1Path));

    LOG_debug << "### Test5 (SdkRemoveTempFilesUponUploadTransfers) Upload file F3 and cancel "
                 "transfer ####";
    const fs::path f3Path = fs::current_path() / fs::u8path("file3.txt");
    ASSERT_TRUE(createFile(f3Path.u8string(), true)) << "Couldn't create " << f3Path;
    ASSERT_NO_FATAL_FAILURE(uploadFile(rootnode.get(), f3Path, true));
}

#ifdef ENABLE_SYNC
/**
 * @brief SdkTestRemoveVersionsFromSync
 *
 *  - Create a sync with a file
 *  - Update the file content (sync engine add a new version)
 *  - Remove all versions from the account
 *  - Check file sync state is STATE_SYNCED
 */
TEST_F(SdkTest, SdkTestRemoveVersionsFromSync)
{
    LOG_info << "___TEST SdkTestRemoveVersionsFromSync";
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    std::string syncFolder{"sync1"};
    fs::path syncFolderPath = syncFolder;
    const auto localSyncFolderPath = fs::current_path() / syncFolderPath;

    // Create local directories and a files.
    fs::create_directories(localSyncFolderPath);

    LOG_verbose << "SdkTestRemoveVersionsFromSync :  Creating the remote folders to be synced to.";
    std::unique_ptr<MegaNode> rootNode(megaApi[0]->getRootNode());
    ASSERT_NE(rootNode.get(), nullptr);
    auto nh = createFolder(0, syncFolder.c_str(), rootNode.get());
    ASSERT_NE(nh, UNDEF) << "Error creating remote folders";
    std::unique_ptr<MegaNode> remoteBaseNode(megaApi[0]->getNodeByHandle(nh));
    ASSERT_NE(remoteBaseNode.get(), nullptr);

    LOG_verbose << "SdkTestRemoveVersionsFromSync :  Add syncs";
    auto lp = localSyncFolderPath.u8string();
    ASSERT_EQ(API_OK,
              synchronousSyncFolder(0,
                                    nullptr,
                                    MegaSync::TYPE_TWOWAY,
                                    lp.c_str(),
                                    nullptr,
                                    remoteBaseNode->getHandle(),
                                    nullptr))
        << "API Error adding a new sync";
    ASSERT_EQ(MegaSync::NO_SYNC_ERROR, mApi[0].lastSyncError);
    std::unique_ptr<MegaSync> sync = waitForSyncState(megaApi[0].get(),
                                                      remoteBaseNode.get(),
                                                      MegaSync::RUNSTATE_RUNNING,
                                                      MegaSync::NO_SYNC_ERROR);
    ASSERT_TRUE(sync && sync->getRunState() == MegaSync::RUNSTATE_RUNNING);
    ASSERT_EQ(MegaSync::NO_SYNC_ERROR, sync->getError());

    handle backupId{sync->getBackupId()};

    LOG_verbose << "SdkTestRemoveVersionsFromSync :  Create a file";
    bool check{false};
    mApi[0].mOnNodesUpdateCompletion =
        createOnNodesUpdateLambda(INVALID_HANDLE, MegaNode::CHANGE_TYPE_NEW, check);
    std::string name{"fileTest"};
    std::string fileName{(localSyncFolderPath / name).u8string()};
    ASSERT_TRUE(createFile(fileName, false));
    ASSERT_TRUE(waitForResponse(&check))
        << "Node update not received on client 0 after " << maxTimeout << " seconds";
    resetOnNodeUpdateCompletionCBs();

    auto checkSyncState = [this](const std::string& fileName)
    {
        static unsigned int waitSyncedState = 40;
        waitForEvent(
            [this, fileName]()
            {
                std::string path{fileName};

#ifdef _WIN32
                auto utf8ToUtf16 = [](const char* utf8data, std::string* utf16string)
                {
                    // Check if input is valid
                    if (utf8data == nullptr)
                    {
                        utf16string->clear();
                        return;
                    }

                    // Get the length required for the UTF-16 buffer
                    int utf16Length = MultiByteToWideChar(CP_UTF8, 0, utf8data, -1, nullptr, 0);

                    // If the length is 0, an error occurred
                    if (utf16Length == 0)
                    {
                        std::cerr << "Error converting UTF-8 to UTF-16." << std::endl;
                        utf16string->clear();
                        return;
                    }

                    // Create a buffer to hold the UTF-16 characters
                    std::wstring utf16buffer(utf16Length, 0);

                    // Perform the conversion from UTF-8 to UTF-16
                    MultiByteToWideChar(CP_UTF8, 0, utf8data, -1, &utf16buffer[0], utf16Length);

                    // Convert the UTF-16 wide string to a std::string by copying the raw bytes
                    utf16string->assign(reinterpret_cast<const char*>(utf16buffer.data()),
                                        utf16buffer.size() * sizeof(wchar_t));
                };

                std::string utf8String{std::move(path)};
                path.clear();
                utf8ToUtf16(utf8String.c_str(), &path);
#endif
                return MegaApi::STATE_SYNCED == megaApi[0]->syncPathState(&path);
            },
            waitSyncedState);
    };

    LOG_verbose << "SdkTestRemoveVersionsFromSync :  wait file is syncronized";
    checkSyncState(fileName);

    check = false;
    mApi[0].mOnNodesUpdateCompletion =
        createOnNodesUpdateLambda(INVALID_HANDLE, MegaNode::CHANGE_TYPE_NEW, check);
    // modify file
    {
        ofstream f{fileName};
        f << "update ";
        f.close();
    }
    ASSERT_TRUE(waitForResponse(&check))
        << "Node update not received on client 0 after " << maxTimeout << " seconds";
    resetOnNodeUpdateCompletionCBs();

    LOG_verbose << "SdkTestRemoveVersionsFromSync :  Wait file is syncronized after modification";
    checkSyncState(fileName);

    std::unique_ptr<MegaNodeList> children{megaApi[0]->getChildren(remoteBaseNode.get())};
    ASSERT_EQ(children->size(), 1);
    MegaNode* node{children->get(0)};
    ASSERT_TRUE(node);
    // Check number of versions
    ASSERT_EQ(megaApi[0]->getNumVersions(node), 2);

    LOG_verbose << "SdkTestRemoveVersionsFromSync :  Remove all versions";
    unique_ptr<RequestTracker> rt = std::make_unique<RequestTracker>(megaApi[0].get());
    megaApi[0]->removeVersions(rt.get());
    ASSERT_EQ(rt->waitForResult(), API_OK);
    ASSERT_EQ(megaApi[0]->getNumVersions(node), 1);

    // Check if file is at synced state. None state change should be generated
    checkSyncState(fileName);

    LOG_verbose << "SdkTestRemoveVersionsFromSync :  Remove syncs";
    rt = std::make_unique<RequestTracker>(megaApi[0].get());
    megaApi[0]->removeSync(backupId, rt.get());
    ASSERT_EQ(rt->waitForResult(), API_OK);

    ASSERT_NO_FATAL_FAILURE(cleanUp(this->megaApi[0].get(), syncFolderPath));
}
#endif

/**
 * @brief SdkTest.CreditCardCancelSubscriptions
 *
 *  - Limited test for canceling a subscription using multiple reasons
 *  - Create helper instances
 *  - Call relevant MegaApi interaface which should fail in a predefined manner
 */
TEST_F(SdkTest, CreditCardCancelSubscriptions)
{
    LOG_info << "___TEST SdkTest.CreditCardCancelSubscriptions___";
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    unique_ptr<MegaCancelSubscriptionReason> reason1{
        MegaCancelSubscriptionReason::create("Some reason", "22.n")};
    unique_ptr<MegaCancelSubscriptionReason> reason2{
        MegaCancelSubscriptionReason::create("Other reason", "99.MCMLXIV")};
    unique_ptr<MegaCancelSubscriptionReasonList> reasons{
        MegaCancelSubscriptionReasonList::create()};
    reasons->add(reason1.get());
    reasons->add(reason2.get());

    // Cancel dummy subscription
    {
        RequestTracker listener{megaApi[0].get()};
        megaApi[0]->creditCardCancelSubscriptions(
            reasons.get(),
            "Dummy subscription ID",
            MegaApi::CREDIT_CARD_CANCEL_SUBSCRIPTIONS_CAN_CONTACT_NO,
            &listener);
        ASSERT_EQ(listener.waitForResult(), API_EARGS);
    }

    // Cancel all subscriptions (no-op for free account)
    {
        RequestTracker listener{megaApi[0].get()};
        megaApi[0]->creditCardCancelSubscriptions(
            reasons.get(),
            "",
            MegaApi::CREDIT_CARD_CANCEL_SUBSCRIPTIONS_CAN_CONTACT_NO,
            &listener);
        ASSERT_EQ(listener.waitForResult(), API_OK);
    }

    // Cancel all subscriptions using null reason list (no-op for free account)
    {
        RequestTracker listener{megaApi[0].get()};
        megaApi[0]->creditCardCancelSubscriptions(
            static_cast<MegaCancelSubscriptionReasonList*>(
                nullptr), // passing 'nullptr' is ambiguous
            nullptr, // id
            MegaApi::CREDIT_CARD_CANCEL_SUBSCRIPTIONS_CAN_CONTACT_NO,
            &listener);
        ASSERT_EQ(listener.waitForResult(), API_OK);
    }

    // Cancel all subscriptions using null char pointer (no-op for free account)
    {
        RequestTracker listener{megaApi[0].get()};
        megaApi[0]->creditCardCancelSubscriptions(
            static_cast<char*>(nullptr), // passing 'nullptr' is ambiguous
            nullptr, // id
            MegaApi::CREDIT_CARD_CANCEL_SUBSCRIPTIONS_CAN_CONTACT_NO,
            &listener);
        ASSERT_EQ(listener.waitForResult(), API_OK);
    }
}

TEST_F(SdkTest, SdkTestSetAccountLevel)
{
    // Make sure we can transition between account levels.
    auto check = [](MegaApi& api, int months, int plan)
    {
        // Try and set the account level.
        auto result = setAccountLevel(api, plan, months, nullptr);

        EXPECT_EQ(result, API_OK) << "Couldn't set account level: " << result;

        // Make sure the account level actually changed.
        if (result == API_OK)
        {
            // Try and get the client's account level.
            auto level = getAccountLevel(api);

            // Couldn't get account level.
            if (result = ::result(level); result != API_OK)
            {
                EXPECT_EQ(result, API_OK) << "Couldn't retrieve account level: " << result;
                return result;
            }

            // Make sure the account level actually changed.
            EXPECT_EQ(value(level).months, months);
            EXPECT_EQ(value(level).plan, plan);
        }

        return result;
    }; // check

    // Get an account for us to play with.
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    // Convenience.
    constexpr auto FREE = MegaAccountDetails::ACCOUNT_TYPE_FREE;
    constexpr auto PRO = MegaAccountDetails::ACCOUNT_TYPE_PROI;

    // Convenience.
    auto& api = *megaApi[0];

    // Make sure any modifications we make are reversed.
    auto restorer = accountLevelRestorer(api);

    // Make sure we can change to a free plan.
    EXPECT_EQ(check(api, 0, FREE), API_OK);

    // Make sure we can change to a pro plan.
    EXPECT_EQ(check(api, 1, PRO), API_OK);
}

TEST_F(SdkTest, FailsWhenThumbnailIsTooLarge)
{
    // Convenience.
    using NodePtr = std::unique_ptr<MegaNode>;

    using fs::u8path;
    using ::mega::MegaApi;
    using sdk_test::LocalTempFile;

    // Clarity.
    constexpr auto KiB = 1024u;
    constexpr auto MiB = 1024u * KiB;

    // Make sure an account is ready for us to use.
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    // Convenience.
    auto& client = *megaApi[0];

    // Get our hands on the user's root node.
    NodePtr root(client.getRootNode());

    // Make sure we could retrieve the root node.
    ASSERT_TRUE(root.get()) << "Couldn't retrieve a reference to the user's root node";

    // Create a file for us to upload to the cloud.
    LocalTempFile content(u8path("content"), 16 * MiB);

    // Upload the file to the cloud so we have a node to play with.
    TransferTracker tracker(&client);

    client.startUpload("content",
                       root.get(),
                       nullptr,
                       MegaApi::INVALID_CUSTOM_MOD_TIME,
                       nullptr,
                       false,
                       false,
                       nullptr,
                       &tracker);

    // Wait for the upload to complete.
    ASSERT_EQ(tracker.waitForResult(), API_OK) << "Couldn't upload file to cloud";

    // Get our hands on our newly created node.
    NodePtr node(client.getNodeByHandle(tracker.resultNodeHandle));

    // Make sure our node exists in the cloud.
    ASSERT_TRUE(node.get()) << "Couldn't retrieve a reference to our newly uploaded file";

    // Try and add our file's content as a thumbnail.
    //
    // This should fail as thumbnails must be < 16MiB.
    ASSERT_EQ(setThumbnail(client, node.get(), "content"), API_EARGS);
}

Error SdkTest::acceptInvitation(MegaApi& client, const MegaContactRequest& invitation)
{
    // So we can wait for the invitation to be accepted.
    RequestTracker tracker(&client);

    // Tell the client to accept the invitation.
    client.replyContactRequest(&invitation, MegaContactRequest::REPLY_ACTION_ACCEPT, &tracker);

    // Couldn't accept the invitation.
    if (auto result = tracker.waitForResult(); result != API_OK)
    {
        return result;
    }

    // Convenience.
    const std::string sender = invitation.getSourceEmail();

    // Wait for the new contact to be added.
    auto added = WaitFor(
        [&]()
        {
            return hasContact(client, sender) != nullptr;
        },
        defaultTimeoutMs);

    // Let the caller know whether the invitation was accepted.
    return added ? API_OK : LOCAL_ETIMEOUT;
}

Error SdkTest::befriend(MegaApi& client0, MegaApi& client1)
{
    // Users are already friends.
    if (hasContact(client0, client1.getMyEmail()))
        return API_OK;

    // Send user1 an invitation.
    auto [invitation, invitationSent] = sendInvitationTo(client0, client1);

    // Couldn't send the invitation.
    if (invitationSent != API_OK)
        return invitationSent;

    // Accept user0's invitation.
    return acceptInvitation(client1, *invitation);
}

auto SdkTest::hasContact(MegaApi& client, const std::string& email) -> std::unique_ptr<MegaUser>
{
    // Convenience.
    constexpr auto VISIBLE = MegaUser::VISIBILITY_VISIBLE;

    // Check if email is a contact.
    auto contact = makeUniqueFrom(client.getContact(email.c_str()));

    // email's an active contact.
    if (contact && contact->getVisibility() == VISIBLE)
    {
        return contact;
    }

    // email's not an active contact.
    return nullptr;
}

auto SdkTest::hasReceivedInvitationFrom(MegaApi& client, const std::string& email)
    -> std::unique_ptr<MegaContactRequest>
{
    // True if an invitation is an incoming invitation from email.
    auto sentFrom = [&email](const auto& invitation)
    {
        return !invitation.isOutgoing() &&
               Utils::icasecmp(invitation.getSourceEmail(), email.c_str()) == 0;
    }; // sentFrom

    // Try and find an incoming invitation from email.
    return findInvitation(client, &MegaApi::getIncomingContactRequests, std::move(sentFrom));
}

auto SdkTest::hasSentInvitationTo(MegaApi& client, const std::string& email)
    -> std::unique_ptr<MegaContactRequest>
{
    // True if an invitation is an incoming invitation from email.
    auto sentTo = [&email](const auto& invitation)
    {
        return invitation.isOutgoing() &&
               Utils::icasecmp(invitation.getTargetEmail(), email.c_str()) == 0;
    }; // sentFrom

    // Try and find an outgoing invitation to email.
    return findInvitation(client, &MegaApi::getOutgoingContactRequests, std::move(sentTo));
}

Error SdkTest::removeContact(MegaApi& client, const std::string& email)
{
    // Do we even know this contact?
    auto contact = hasContact(client, email);

    // Don't know the contact.
    if (!contact || contact->getVisibility() == MegaUser::VISIBILITY_HIDDEN)
    {
        return API_ENOENT;
    }

    RequestTracker tracker(&client);

    // Try and remove the contact.
    client.removeContact(contact.get(), &tracker);

    // Let the caller know if the contact was removed.
    return tracker.waitForResult();
}

Error SdkTest::removeContact(MegaApi& client0, MegaApi& client1)
{
    // Try and break the contact relationship.
    auto result = removeContact(client0, client1.getMyEmail());

    // Couldn't break the contact relationship.
    if (result != API_OK)
    {
        return result;
    }

    // Wait for the contacts to be purged.
    auto purged = WaitFor(
        [&]()
        {
            return !hasContact(client0, client1.getMyEmail()) &&
                   !hasContact(client1, client0.getMyEmail());
        },
        defaultTimeoutMs);

    // Let the caller know if the relationship was broken.
    return purged ? API_OK : LOCAL_ETIMEOUT;
}

Error SdkTest::sendInvitationTo(MegaApi& client, const std::string& email)
{
    // So we can wait for our request to complete.
    RequestTracker tracker(&client);

    // Ask the client to send the user an invitation.
    client.inviteContact(email.c_str(), "", MegaContactRequest::INVITE_ACTION_ADD, &tracker);

    // Let caller know whether the invitation was sent.
    return tracker.waitForResult();
}

auto SdkTest::sendInvitationTo(MegaApi& client0, MegaApi& client1) -> SendInvitationToResult
{
    // Convenience.
    const std::string email0 = client0.getMyEmail();
    const std::string email1 = client1.getMyEmail();

    // Couldn't send an invitation to client1.
    if (auto result = sendInvitationTo(client0, email1); result != API_OK)
    {
        return std::make_pair(nullptr, result);
    }

    std::unique_ptr<MegaContactRequest> invitation;

    // Wait for both clients to recieve the invitation.
    WaitFor(
        [&]()
        {
            return hasSentInvitationTo(client0, email1) &&
                   (invitation = hasReceivedInvitationFrom(client1, email0));
        },
        defaultTimeoutMs);

    // Invitation was never received.
    if (!invitation)
    {
        return std::make_pair(nullptr, LOCAL_ETIMEOUT);
    }

    // Invitation was received.
    return std::make_pair(std::move(invitation), API_OK);
}

/**
 * @brief TEST_F TestPublicFolderLinkLogin
 *
 * Test setup:
 *  - Create a folder (Sharee user)
 *  - Create a public link for that folder (Sharee user)
 *
 * Test steps:
 *  - Login using the folder link (Guest user)
 *  - Expect fetch to be done with MODE_API
 *  - Do local logout (Guest user)
 *  - Login again using the folder link and tryToResumeFolderLinkFromCache as true(Guest user)
 *  - Expect fetch to be done with MODE_DB
 *  - Do local logout (Guest user)
 *  - Login again using the folder link and tryToResumeFolderLinkFromCache as false (Guest user)
 *  - Expect fetch to be done with MODE_API
 *
 */
TEST_F(SdkTestShares, TestPublicFolderLinkLogin)
{
    // Test setup
    ASSERT_NO_FATAL_FAILURE(createNodeTrees());

    const MegaHandle hfolder = getHandle("/sharedfolder");
    ASSERT_EQ(API_OK, synchronousGetSpecificAccountDetails(mSharerIndex, true, true, true))
        << "Cannot get account details";
    std::string nodeLink;
    ASSERT_NO_FATAL_FAILURE(createOnePublicLink(hfolder, nodeLink));

    // Test steps
    bool tryToResumeFolderLinkFromCache = false;
    auto loginFolderTracker = asyncRequestLoginToFolder(mGuestIndex,
                                                        nodeLink.c_str(),
                                                        nullptr,
                                                        tryToResumeFolderLinkFromCache);
    ASSERT_EQ(loginFolderTracker->waitForResult(), API_OK)
        << "Failed to login to folder " << nodeLink;
    ASSERT_NO_FATAL_FAILURE(fetchnodes(mGuestIndex));
    EXPECT_EQ(megaApi[mGuestIndex]->getClient()->fnstats.mode, FetchNodesStats::MODE_API);
    ASSERT_NO_FATAL_FAILURE(locallogout(mGuestIndex));

    tryToResumeFolderLinkFromCache = true;
    loginFolderTracker = asyncRequestLoginToFolder(mGuestIndex,
                                                   nodeLink.c_str(),
                                                   nullptr,
                                                   tryToResumeFolderLinkFromCache);
    ASSERT_EQ(loginFolderTracker->waitForResult(), API_OK)
        << "Failed to login to folder " << nodeLink;
    ASSERT_NO_FATAL_FAILURE(fetchnodes(mGuestIndex));
    EXPECT_EQ(megaApi[mGuestIndex]->getClient()->fnstats.mode, FetchNodesStats::MODE_DB);
    ASSERT_NO_FATAL_FAILURE(locallogout(mGuestIndex));

    tryToResumeFolderLinkFromCache = false;
    loginFolderTracker = asyncRequestLoginToFolder(mGuestIndex,
                                                   nodeLink.c_str(),
                                                   nullptr,
                                                   tryToResumeFolderLinkFromCache);
    ASSERT_EQ(loginFolderTracker->waitForResult(), API_OK)
        << "Failed to login to folder " << nodeLink;
    ASSERT_NO_FATAL_FAILURE(fetchnodes(mGuestIndex));
    EXPECT_EQ(megaApi[mGuestIndex]->getClient()->fnstats.mode, FetchNodesStats::MODE_API);

    // Cleanup
    ASSERT_NO_FATAL_FAILURE(logout(mGuestIndex, false, 20));
}

TEST_F(SdkTest, ExportNodeWithExpiryDate)
{
    // Get an account for us to play with.
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    // Convenience.
    auto& client = *megaApi[0];

    // Make sure any plan changed are reversed.
    auto restorer = accountLevelRestorer(client);

    // Make sure the backend thinks we have a free account.
    EXPECT_EQ(setAccountLevel(client, MegaAccountDetails::ACCOUNT_TYPE_FREE, 0, nullptr), API_OK);

    // Get our hands on this account's root node.
    auto root = makeUniqueFrom(client.getRootNode());
    ASSERT_NE(root, nullptr);

    // Create a directory for us to try and export.
    auto node = createDirectory(client, *root, "d");
    ASSERT_EQ(result(node), API_OK);

    // Get tomorrow's time stamp.
    auto tomorrow = ([]() {
        auto now = std::chrono::system_clock::now();
        auto tomorrow = now + std::chrono::hours(24);
        return std::chrono::system_clock::to_time_t(tomorrow);
    })();

    // Trying to export a node with an expiry date should fail.
    EXPECT_EQ(result(exportNode(client, *value(node), tomorrow)), API_EACCESS);

    // Unless, of course, the account is a *pro account* :)
    ASSERT_EQ(setAccountLevel(client, MegaAccountDetails::ACCOUNT_TYPE_PROI, 1, nullptr), API_OK);

    // Exporting a node with an expiry date should now succeed.
    auto link = exportNode(client, *value(node), tomorrow);
    ASSERT_EQ(result(link), API_OK);
}

TEST_F(SdkTest, HashCash)
{
    const auto [email, pass] = getEnvVarAccounts().getVarValues(0);
    ASSERT_FALSE(email.empty() || pass.empty());
    megaApi.resize(1);
    mApi.resize(1);
    configureTestInstance(0, email, pass, true, MegaApi::CLIENT_TYPE_DEFAULT);
    std::string ua = "HashcashDemo";
    megaApi[0]->getClient()->httpio->setuseragent(&ua);
    megaApi[0]->changeApiUrl("https://staging.api.mega.co.nz/");
    std::unique_ptr<RequestTracker> tracker;
    if (!gResumeSessions || gSessionIDs[0].empty() || gSessionIDs[0] == "invalid")
    {
        out() << "Starting new session of account #0: " << mApi[0].email;
        tracker = asyncRequestLogin(0, mApi[0].email.c_str(), mApi[0].pwd.c_str());
    }
    else
    {
        out() << "Resuming session of account #0";
        tracker = asyncRequestFastLogin(0, gSessionIDs[0].c_str());
    }
    auto loginResult = tracker->waitForResult();
    ASSERT_EQ(API_OK, loginResult)
        << " Login error  " << loginResult << " for account " << mApi[0].email;
    megaApi[0]->getClient()->httpio->setuseragent(&USER_AGENT); // stop hashcash, speed up cleanup
}

/**
 * @brief SdkTestRemovePublicLinkSet
 *
 *  - Login client 1 and client 2 with same account
 *  - Create a set
 *  - Generate a public link
 *  - Resume session with client 1 and check if Set is recover properly
 *  - Remove public link
 *  - Resume session with client 1 and check if Set is recover properly
 *  - Generate a public link again
 *  - Resume session with client 1 and check if Set is recover properly
 */
TEST_F(SdkTest, SdkTestRemovePublicLinkSet)
{
    LOG_info << "___TEST SdkTestRemovePublicLinkSet";
    static const unsigned long primaryClientIdx{0};
    static const unsigned long secondaryClientIdx{1};
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    // Client 2 is other client from user 1
    const auto [email, pass] = getEnvVarAccounts().getVarValues(0);
    ASSERT_FALSE(email.empty() || pass.empty());
    mApi.resize(2);
    megaApi.resize(2);
    configureTestInstance(secondaryClientIdx, email, pass); // index 1 = User B
    auto loginTracker = std::make_unique<RequestTracker>(megaApi[secondaryClientIdx].get());
    megaApi[secondaryClientIdx]->login(email.c_str(), pass.c_str(), loginTracker.get());
    ASSERT_EQ(API_OK, loginTracker->waitForResult()) << " Failed to login to account " << email;
    ASSERT_NO_FATAL_FAILURE(fetchnodes(secondaryClientIdx));

    const MrProper cleanUp(
        [this]()
        {
            // release secondary instance to avoid failure at tear down
            releaseMegaApi(secondaryClientIdx);
        });

    LOG_debug << "# Create set";
    const string name = "Set-test";

    mApi[secondaryClientIdx].setUpdated = false;
    RequestTracker trackerCreateSet{megaApi[primaryClientIdx].get()};
    megaApi[primaryClientIdx]->createSet(name.c_str(), MegaSet::SET_TYPE_ALBUM, &trackerCreateSet);
    ASSERT_EQ(trackerCreateSet.waitForResult(), API_OK);
    const MegaHandle sh = trackerCreateSet.request->getMegaSet()->id();
    ASSERT_TRUE(waitForResponse(&mApi[secondaryClientIdx].setUpdated));
    std::unique_ptr<MegaSet> setSecondAccount{megaApi[secondaryClientIdx]->getSet(sh)};
    ASSERT_TRUE(setSecondAccount);

    LOG_debug << "Set handle: " << Base64Str<MegaClient::USERHANDLE>(sh);

    auto exportSet = [this, sh]()
    {
        std::unique_ptr<MegaSet> set{megaApi[primaryClientIdx]->getSet(sh)};
        mApi[secondaryClientIdx].setUpdated = false;
        RequestTracker trackerExportSet{megaApi[primaryClientIdx].get()};
        megaApi[primaryClientIdx]->exportSet(set->id(), &trackerExportSet);
        ASSERT_EQ(trackerExportSet.waitForResult(), API_OK);
        ASSERT_TRUE(waitForResponse(&mApi[secondaryClientIdx].setUpdated));
        MegaSet* exportedSet = trackerExportSet.request->getMegaSet();
        ASSERT_TRUE(exportedSet->isExported());
        ASSERT_EQ(exportedSet->id(), set->id());
        ASSERT_TRUE(exportedSet->getLinkDeletionReason() == MegaSet::DELETION_LINK_NO_REMOVED);
        std::unique_ptr<MegaSet> setSecondAccount{megaApi[secondaryClientIdx]->getSet(sh)};
        ASSERT_TRUE(setSecondAccount);
        ASSERT_TRUE(setSecondAccount->getLinkDeletionReason() == MegaSet::DELETION_LINK_NO_REMOVED);
    };

    auto disableExportSet = [this, sh]()
    {
        std::unique_ptr<MegaSet> set{megaApi[primaryClientIdx]->getSet(sh)};
        mApi[secondaryClientIdx].setUpdated = false;
        RequestTracker trackerDisableSet{megaApi[primaryClientIdx].get()};
        megaApi[primaryClientIdx]->disableExportSet(sh, &trackerDisableSet);
        ASSERT_EQ(trackerDisableSet.waitForResult(), API_OK);
        ASSERT_TRUE(waitForResponse(&mApi[secondaryClientIdx].setUpdated));
        MegaSet* noExportedSet = megaApi[primaryClientIdx]->getSet(sh);
        ASSERT_FALSE(noExportedSet->isExported());
        ASSERT_EQ(noExportedSet->id(), set->id());
        std::unique_ptr<MegaSet> setSecondAccount{megaApi[secondaryClientIdx]->getSet(sh)};
        ASSERT_TRUE(setSecondAccount);
        ASSERT_FALSE(setSecondAccount->isExported());
    };

    auto checkDeletionReasonAfterResumeSession = [this, sh](bool exported)
    {
        PerApi& target = mApi[primaryClientIdx];
        target.resetlastEvent();
        std::unique_ptr<char[]> session(megaApi[primaryClientIdx]->dumpSession());
        ASSERT_NO_FATAL_FAILURE(locallogout());
        ASSERT_NO_FATAL_FAILURE(resumeSession(session.get()));
        ASSERT_NO_FATAL_FAILURE(fetchnodes(primaryClientIdx));
        // make sure that client is up to date (upon logout, recent changes might not be committed
        // to DB)
        ASSERT_TRUE(WaitFor(
            [&target]()
            {
                return target.lastEventsContain(MegaEvent::EVENT_NODES_CURRENT);
            },
            10000))
            << "Timeout expired to receive actionpackets";

        std::unique_ptr<MegaSet> setPrimaryAccount{megaApi[primaryClientIdx]->getSet(sh)};
        ASSERT_TRUE(setPrimaryAccount);
        ASSERT_EQ(setPrimaryAccount->isExported(), exported);
    };

    LOG_debug << "# Check if Set is exported (false)";
    ASSERT_FALSE(megaApi[primaryClientIdx]->isExportedSet(sh)) << "Set should not be public yet";

    LOG_debug << "# Enable Set export (creates public link)";
    ASSERT_NO_FATAL_FAILURE(exportSet());

    LOG_debug << "# Check state after resume session 1";
    checkDeletionReasonAfterResumeSession(true);

    LOG_debug << "# Disable public link";
    ASSERT_NO_FATAL_FAILURE(disableExportSet());

    LOG_debug << "# Check state after resume session 2";
    checkDeletionReasonAfterResumeSession(false);

    LOG_debug << "# Enable Set export again";
    ASSERT_NO_FATAL_FAILURE(exportSet());

    LOG_debug << "# Check state after resume session 3";
    checkDeletionReasonAfterResumeSession(true);
}

/**
 * @test SdkTestGetThumbnailUsingNodeAndHandle
 * @brief Verifies that thumbnails retrieved via MegaNode and MegaHandle are identical.
 *
 * Steps:
 * - Upload an image to the cloud.
 * - Retrieve its thumbnail using MegaNode.
 * - Retrieve the same thumbnail using MegaHandle.
 * - Compare the two thumbnail files byte by byte.
 */
TEST_F(SdkTest, SdkTestGetThumbnailUsingNodeAndHandle)
{
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));
    ASSERT_TRUE(getFileFromArtifactory("test-data/" + IMAGEFILE, IMAGEFILE));

    // Upload image file
    std::unique_ptr<MegaNode> rootnode(megaApi[0]->getRootNode());
    MegaHandle uploadResultHandle = UNDEF;
    ASSERT_EQ(MegaError::API_OK,
              doStartUpload(0,
                            &uploadResultHandle,
                            IMAGEFILE.c_str(),
                            rootnode.get(),
                            nullptr /*fileName*/,
                            ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
                            nullptr /*appData*/,
                            false /*isSourceTemporary*/,
                            false /*startFirst*/,
                            nullptr /*cancelToken*/))
        << "Uploaded file with wrong name (error: " << mApi[0].lastError << ")";

    // Get thumbnail using MegaNode
    std::unique_ptr<MegaNode> n1(megaApi[0]->getNodeByHandle(uploadResultHandle));
    std::string megaNodeThumbnailPath = THUMBNAIL;
    ASSERT_EQ(API_OK, doGetThumbnail(0, n1.get(), megaNodeThumbnailPath.c_str()));

    // Get thumbnail using the MegaHandle
    std::string megaHandleThumbnailPath =
        std::string(THUMBNAIL).insert(THUMBNAIL.rfind(".png"), "2");
    ASSERT_EQ(API_OK, doGetThumbnail(0, uploadResultHandle, megaHandleThumbnailPath.c_str()));

    // Check both images are equal
    std::ifstream file1(megaNodeThumbnailPath, std::ios::binary);
    std::ifstream file2(megaHandleThumbnailPath, std::ios::binary);

    ASSERT_TRUE(file1.is_open()) << "Failed to open " << megaNodeThumbnailPath;
    ASSERT_TRUE(file2.is_open()) << "Failed to open " << megaHandleThumbnailPath;

    std::vector<char> buffer1((std::istreambuf_iterator<char>(file1)), {});
    std::vector<char> buffer2((std::istreambuf_iterator<char>(file2)), {});

    ASSERT_EQ(buffer1.size(), buffer2.size()) << "Thumbnail sizes differ";
    ASSERT_EQ(buffer1, buffer2) << "Thumbnail contents differ";
}

/**
 * @brief SdkTest.SdkTestUploadNodeAttribute
 *
 * Tests if node attributes consistency on file uploading as follows
 *  Uploading same file again - Node attribute should be kept
 *  Uploading updated file content - Node attribute should be kept
 *  Uploading the same file with different name - Node attributes should not be copied from previous
 * node except fingerprint
 *
 */
TEST_F(SdkTest, SdkTestUploadNodeAttribute)
{
    // Get an account for us to play with.
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    // Convenience.
    auto& client = *megaApi[0];

    // Get our hands on this account's root node.
    auto root = makeUniqueFrom(client.getRootNode());
    ASSERT_NE(root, nullptr);

    // Create a directory for us to try and export.
    auto dirNode = createDirectory(client, *root, "UploadDirTest");
    ASSERT_EQ(result(dirNode), API_OK);

    MegaHandle fileHandle = 0;
    const auto fileName = "testFileAttr.txt";
    ASSERT_TRUE(createFile(fileName, false));

    ASSERT_EQ(MegaError::API_OK,
              doStartUpload(0,
                            &fileHandle,
                            fileName,
                            value(dirNode).get(),
                            nullptr /*fileName*/,
                            ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
                            nullptr /*appData*/,
                            false /*isSourceTemporary*/,
                            false /*startFirst*/,
                            nullptr /*cancelToken*/))
        << "Cannot upload " << fileName;

    ASSERT_NE(fileHandle, INVALID_HANDLE);

    auto fileNode = client.getNodeByPath("/UploadDirTest/testFileAttr.txt");
    ASSERT_EQ(API_OK, synchronousSetNodeFavourite(0, fileNode, true)) << "Error setting fav";
    ASSERT_EQ(API_OK, synchronousSetNodeLabel(0, fileNode, 4)) << "Error setting label";

    // Re-upload the same file with same content.
    ASSERT_EQ(MegaError::API_OK,
              doStartUpload(0,
                            &fileHandle,
                            fileName,
                            value(dirNode).get(),
                            nullptr /*fileName*/,
                            ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
                            nullptr /*appData*/,
                            false /*isSourceTemporary*/,
                            false /*startFirst*/,
                            nullptr /*cancelToken*/))
        << "Cannot upload " << fileName;
    ASSERT_NE(fileHandle, INVALID_HANDLE);
    fileNode = client.getNodeByPath("/UploadDirTest/testFileAttr.txt");
    ASSERT_EQ(fileNode->getLabel(), 4) << "Node label is not retained after re-uploading the file";
    ASSERT_EQ(fileNode->isFavourite(), true)
        << "Favourite flag is not retained after re-uploading the file";

    // Let's update the file and upload again.
    sdk_test::appendToFile(fs::path(fileName), 20000);
    ASSERT_EQ(MegaError::API_OK,
              doStartUpload(0,
                            &fileHandle,
                            fileName,
                            value(dirNode).get(),
                            nullptr /*fileName*/,
                            ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
                            nullptr /*appData*/,
                            false /*isSourceTemporary*/,
                            false /*startFirst*/,
                            nullptr /*cancelToken*/))
        << "Cannot upload " << fileName;
    ASSERT_NE(fileHandle, INVALID_HANDLE);
    fileNode = client.getNodeByPath("/UploadDirTest/testFileAttr.txt");
    ASSERT_EQ(fileNode->getLabel(), 4) << "Node label is not retained after updating the file";
    ASSERT_EQ(fileNode->isFavourite(), true)
        << "Favourite flag is not retained after updating the file";

    bool hasFingerprint = !!fileNode->getFingerprint();

    // Upload the same file with different name.
    ASSERT_EQ(MegaError::API_OK,
              doStartUpload(0,
                            &fileHandle,
                            fileName,
                            value(dirNode).get(),
                            "testFileAttr_1.txt" /*fileName*/,
                            ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
                            nullptr /*appData*/,
                            false /*isSourceTemporary*/,
                            false /*startFirst*/,
                            nullptr /*cancelToken*/))
        << "Cannot upload " << fileName;
    ASSERT_NE(fileHandle, INVALID_HANDLE);
    fileNode = client.getNodeByPath("/UploadDirTest/testFileAttr_1.txt");
    ASSERT_NE(fileNode->getLabel(), 4) << "Node label is copied for for renamed file upload";
    ASSERT_NE(fileNode->isFavourite(), true)
        << "Favourite flag is copied for for renamed file upload";
    if (hasFingerprint)
    {
        ASSERT_NE(fileNode->getFingerprint(), nullptr) << "Finger print has been copied";
    }
}

class SdkTestNodeGpsCoordinates: public SdkTest
{
protected:
    const unsigned int mApiIndex{0};

    MegaHandle mNodeHandle{INVALID_HANDLE};

    std::unique_ptr<MegaNode> mNode;

    struct GpsCoordinates
    {
        double latitude;
        double longitude;
    };

    const GpsCoordinates mGpsCoordinates{40.966095795138365, -5.662973159866294};

public:
    void SetUp() override
    {
        SdkTest::SetUp();

        // Configure test instance
        const unsigned int numberOfTestInstances{1};
        ASSERT_NO_FATAL_FAILURE(getAccountsForTest(numberOfTestInstances));

        // Upload file
        std::unique_ptr<MegaNode> rootNode{megaApi[mApiIndex]->getRootNode()};
        ASSERT_THAT(rootNode.get(), ::testing::NotNull());

        const std::string filename{"test.txt"};
        ASSERT_TRUE(createFile(filename, false, ""));

        ASSERT_EQ(doStartUpload(mApiIndex,
                                &mNodeHandle,
                                filename.c_str(),
                                rootNode.get(),
                                nullptr,
                                MegaApi::INVALID_CUSTOM_MOD_TIME,
                                nullptr,
                                false,
                                false,
                                nullptr),
                  MegaError::API_OK);
        ASSERT_NE(mNodeHandle, INVALID_HANDLE);

        // Get node
        mNode.reset(megaApi[mApiIndex]->getNodeByHandle(mNodeHandle));
        ASSERT_THAT(mNode.get(), ::testing::NotNull());
    }
};

TEST_F(SdkTestNodeGpsCoordinates, SetUnshareableNodeCoordinatesWithNullNode)
{
    std::unique_ptr<RequestTracker> requestTracker{
        asyncSetUnshareableNodeCoordinates(mApiIndex,
                                           nullptr,
                                           mGpsCoordinates.latitude,
                                           mGpsCoordinates.longitude)};
    ASSERT_EQ(requestTracker->waitForResult(), API_EARGS);
}

TEST_F(SdkTestNodeGpsCoordinates, SetUnshareableNodeCoordinatesWithNode)
{
    std::unique_ptr<RequestTracker> requestTracker{
        asyncSetUnshareableNodeCoordinates(mApiIndex,
                                           mNode.get(),
                                           mGpsCoordinates.latitude,
                                           mGpsCoordinates.longitude)};
    ASSERT_EQ(requestTracker->waitForResult(), API_OK);

    // Check if the user can read the GPS coordinates
    std::unique_ptr<MegaNode> node(megaApi[mApiIndex]->getNodeByHandle(mNode->getHandle()));
    ASSERT_TRUE(veryclose(node->getLatitude(), mGpsCoordinates.latitude));
    ASSERT_TRUE(veryclose(node->getLongitude(), mGpsCoordinates.longitude));
}

TEST_F(SdkTestNodeGpsCoordinates, SetUnshareableNodeCoordinatesWithNodeHandle)
{
    std::unique_ptr<RequestTracker> requestTracker{
        asyncSetUnshareableNodeCoordinates(mApiIndex,
                                           mNodeHandle,
                                           mGpsCoordinates.latitude,
                                           mGpsCoordinates.longitude)};
    ASSERT_EQ(requestTracker->waitForResult(), API_OK);

    // Check if the user can read the GPS coordinates
    std::unique_ptr<MegaNode> node(megaApi[mApiIndex]->getNodeByHandle(mNode->getHandle()));
    ASSERT_TRUE(veryclose(node->getLatitude(), mGpsCoordinates.latitude));
    ASSERT_TRUE(veryclose(node->getLongitude(), mGpsCoordinates.longitude));
}

TEST_F(SdkTest, EstablishContactRelationship)
{
    // Convenience.
    using testing::AnyOf;

    // We need at least two clients to work with.
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(2));

    // Convenience.
    auto& client0 = *megaApi[0];
    auto& client1 = *megaApi[1];

    // Make sure the clients aren't already friends.
    ASSERT_THAT(removeContact(client0, client1), AnyOf(API_ENOENT, API_OK));

    // Try and send a contact invitation.
    auto [invitation, invitationSent] = sendInvitationTo(client0, client1);

    // Make sure the invitation was received.
    ASSERT_EQ(invitationSent, API_OK);

    // Try and accept the invitation.
    ASSERT_EQ(acceptInvitation(client1, *invitation), API_OK);
}

TEST_F(SdkTest, EstablishContactRelationshipAutomatically)
{
    // Convenience.
    using testing::AnyOf;

    // We need at least two clients to work with.
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(2));

    // Convenience.
    auto& client0 = *megaApi[0];
    auto& client1 = *megaApi[1];

    // Make sure the clients aren't already friends.
    ASSERT_THAT(removeContact(client0, client1), AnyOf(API_ENOENT, API_OK));

    // Try and send an invitation from client0 to client1.
    auto [invitation0, invitation0Sent] = sendInvitationTo(client0, client1);

    // Make sure that invitation was sent.
    ASSERT_EQ(invitation0Sent, API_OK);

    // Try and send an invitation from client1 to client1.
    auto [invitation1, invitation1Sent] = sendInvitationTo(client1, client0);

    // Make sure that invitation fails: there's already an incoming PCR
    ASSERT_EQ(invitation1Sent, API_EEXIST);
}
