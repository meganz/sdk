#include "env_var_accounts.h"
#include "integration_test_utils.h"
#include "mega/testhooks.h"
#include "sdk_test_utils.h"
#include "SdkTest_test.h"

#ifdef MEGASDK_DEBUG_TEST_HOOKS_ENABLED

#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>

namespace
{
class PitagCommandObserver
{
public:
    PitagCommandObserver():
        mPreviousHook(globalMegaTestHooks.onHttpReqPost)
    {
        globalMegaTestHooks.onHttpReqPost = [this](HttpReq* req)
        {
            handleRequest(req);
            return false;
        };
    }

    ~PitagCommandObserver()
    {
        globalMegaTestHooks.onHttpReqPost = mPreviousHook;
    }

    bool waitForValue(const std::string& expected, std::chrono::milliseconds timeout)
    {
        std::unique_lock<std::mutex> lock(mMutex);
        if (!mCv.wait_for(lock,
                          timeout,
                          [&]
                          {
                              return mCaptured;
                          }))
        {
            return false;
        }
        return mLastValue == expected;
    }

    std::string capturedValue() const
    {
        std::lock_guard<std::mutex> lock(mMutex);
        return mLastValue;
    }

private:
    void handleRequest(HttpReq* req)
    {
        if (!req || !req->out)
        {
            return;
        }

        const std::string& payload = *req->out;
        const std::string commandToken = "\"a\":\"p\"";
        const std::string pitagToken = "\"p\":\"";

        const auto commandPos = payload.find(commandToken);
        if (commandPos == std::string::npos)
        {
            return;
        }

        auto pitagPos = payload.find(pitagToken, commandPos);
        if (pitagPos == std::string::npos)
        {
            return;
        }
        pitagPos += pitagToken.size();

        const auto endPos = payload.find('"', pitagPos);
        if (endPos == std::string::npos)
        {
            return;
        }

        {
            std::lock_guard<std::mutex> lock(mMutex);
            if (mCaptured)
            {
                return;
            }
            mCaptured = true;
            mLastValue = payload.substr(pitagPos, endPos - pitagPos);
        }
        mCv.notify_all();
    }

    std::function<bool(HttpReq*)> mPreviousHook;
    mutable std::mutex mMutex;
    std::condition_variable mCv;
    bool mCaptured{false};
    std::string mLastValue;
};
} // anonymous namespace

TEST_F(SdkTest, PitagCapturedForRegularUpload)
{
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    const auto localFilePath = fs::current_path() / (getFilePrefix() + "pitag_regular.bin");
    const std::string remoteName = path_u8string(localFilePath.filename());
    const std::string localPathUtf8 = path_u8string(localFilePath);
    const sdk_test::LocalTempFile localFile(localFilePath, "pitag-regular-upload");

    std::unique_ptr<MegaNode> rootNode{megaApi[0]->getRootNode()};
    ASSERT_TRUE(rootNode) << "Unable to get root node";

    PitagCommandObserver observer;
    TransferTracker tracker(megaApi[0].get());
    megaApi[0]->startUpload(localPathUtf8.c_str(),
                            rootNode.get(),
                            remoteName.c_str(),
                            MegaApi::INVALID_CUSTOM_MOD_TIME,
                            nullptr,
                            false,
                            false,
                            nullptr,
                            &tracker);

    ASSERT_EQ(API_OK, tracker.waitForResult());

    const auto waitTimeout =
        std::chrono::duration_cast<std::chrono::milliseconds>(sdk_test::MAX_TIMEOUT);
    ASSERT_TRUE(observer.waitForValue("U.fD.", waitTimeout))
        << "Unexpected pitag payload captured: " << observer.capturedValue();
}

TEST_F(SdkTest, PitagCapturedForIncomingShareUpload)
{
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(2));

    std::unique_ptr<MegaNode> ownerRoot{megaApi[0]->getRootNode()};
    ASSERT_TRUE(ownerRoot) << "Unable to get root node for owner account";

    inviteTestAccount(0, 1, "Hi!!");

    const std::string folderName = getFilePrefix() + "incomingShare";
    RequestTracker folderTracker(megaApi[0].get());
    megaApi[0]->createFolder(folderName.c_str(), ownerRoot.get(), &folderTracker);
    ASSERT_EQ(API_OK, folderTracker.waitForResult()) << "Failed to create folder for sharing";

    std::unique_ptr<MegaNode> folderNode{
        megaApi[0]->getNodeByHandle(folderTracker.request->getNodeHandle())};
    ASSERT_TRUE(folderNode) << "Unable to obtain shared folder node";

    ASSERT_NO_FATAL_FAILURE(
        shareFolder(folderNode.get(), mApi[1].email.c_str(), MegaShare::ACCESS_FULL));

    auto inShareAvailable = [this]()
    {
        std::unique_ptr<MegaShareList> shares{megaApi[1]->getInSharesList()};
        return shares && shares->size() > 0;
    };
    ASSERT_TRUE(WaitFor(inShareAvailable, defaultTimeoutMs))
        << "Incoming share not received by sharee";

    std::unique_ptr<MegaShareList> inShares{megaApi[1]->getInSharesList()};
    ASSERT_TRUE(inShares && inShares->size() > 0);
    const MegaHandle sharedHandle = inShares->get(0)->getNodeHandle();
    std::unique_ptr<MegaNode> incomingNode{megaApi[1]->getNodeByHandle(sharedHandle)};
    ASSERT_TRUE(incomingNode) << "Sharee cannot access incoming share node";

    const auto localFilePath = fs::current_path() / (getFilePrefix() + "pitag_inshare.bin");
    const std::string localPathUtf8 = path_u8string(localFilePath);
    const sdk_test::LocalTempFile localFile(localFilePath, "pitag-inshare-upload");

    PitagCommandObserver observer;
    TransferTracker tracker(megaApi[1].get());
    megaApi[1]->startUpload(localPathUtf8.c_str(),
                            incomingNode.get(),
                            nullptr,
                            MegaApi::INVALID_CUSTOM_MOD_TIME,
                            nullptr,
                            false,
                            false,
                            nullptr,
                            &tracker);
    ASSERT_EQ(API_OK, tracker.waitForResult());

    constexpr auto timeout = 3s; // short timeout, it has to be available
    const auto waitTimeout = std::chrono::duration_cast<std::chrono::milliseconds>(timeout);
    ASSERT_TRUE(observer.waitForValue("U.fi.", waitTimeout))
        << "Unexpected pitag payload captured: " << observer.capturedValue();
}

#endif // MEGASDK_DEBUG_TEST_HOOKS_ENABLED
