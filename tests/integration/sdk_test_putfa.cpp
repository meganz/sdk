/**
 * @file sdk_test_putfa.cpp
 * @brief Integration tests for the file attribute upload channel (MegaClient::activefa)
 */

#include "api_request_hook.h"
#include "mega/testhooks.h"
#include "SdkTest_test.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <set>
#include <vector>

#ifdef MEGASDK_DEBUG_TEST_HOOKS_ENABLED

using namespace mega;

namespace
{

/**
 * @brief Turns the first file attribute POST it sees into one whose connection died silently:
 * left in flight with no data ever moving and no curl completion ever arriving, which is what a
 * stuck putfa looks like (MEGAcmd issue 1135). Its lastdata is aged by `age` so that the
 * stale-data timeout (MegaClient::PUTFATIMEOUT) fires without a real-time wait of that length.
 *
 * A retry after the timeout shows up as the very same request (same id) being posted again, as
 * MegaClient::exec disconnects it and moves it back to queuedfa; that second post goes through.
 */
class StuckFileAttributeUpload
{
public:
    explicit StuckFileAttributeUpload(dstime age):
        mAge{age},
        mHook{[this](HttpReq* req)
              {
                  return onPost(req);
              }}
    {}

    bool seen() const
    {
        std::lock_guard<std::mutex> g{mMutex};
        return mStuckId.has_value();
    }

    bool retried() const
    {
        std::lock_guard<std::mutex> g{mMutex};
        return mRetried;
    }

private:
    bool onPost(HttpReq* req)
    {
        // only file attribute uploads; everything else (API, transfer chunks) is left alone
        if (!dynamic_cast<HttpReqFA*>(req))
        {
            return false;
        }

        std::lock_guard<std::mutex> g{mMutex};
        if (!mStuckId)
        {
            mStuckId = req->getId();

            // post() has already prepared the request: stand in for httpio->post() and leave it
            // in flight for ever, with its last data movement `age` deciseconds in the past
            req->status = REQ_INFLIGHT;
            req->lastdata = Waiter::ds - mAge;
            LOG_info << "SIMULATING STUCK FILE ATTRIBUTE UPLOAD (Req#" << *mStuckId << ")";
            return true;
        }

        if (req->getId() == *mStuckId)
        {
            LOG_info << "STUCK FILE ATTRIBUTE UPLOAD RETRIED (Req#" << *mStuckId << ")";
            mRetried = true;
        }
        return false;
    }

    const dstime mAge;
    mutable std::mutex mMutex;
    std::optional<uint32_t> mStuckId;
    bool mRetried = false;
    sdk_test::ScopedHttpReqPostHook mHook;
};

/**
 * @brief Cancels an upload once its data is complete while its file attribute uploads (thumbnail
 * and preview, posted under the upload handle) are stuck as in StuckFileAttributeUpload, at a
 * moment when they are already stale for MegaClient::PUTFATIMEOUT: the pass of MegaClient::exec
 * that processes the cancellation is one whose watchdog would fire too.
 *
 * Only the REQ_INFLIGHT handling of activefa may process the cancellation for this to happen. Once
 * MegaClient::checkfacompletion(uploadCompleted = true) has run, the transfer has no slot and is
 * out of multi_transfers, but dispatchTransfers() would still cancel it (orphaning the stuck
 * requests) if the token were cancelled before dispatchTransfers() runs in the same pass. Hence
 * three SDK-thread hooks:
 *  - onHttpReqPost sticks every file attribute POST while armed;
 *  - onProgressCompletedUpdate notes the data completion; a request already stuck is aged and the
 *    upload cancelled right away (exec next looks at the transfer in the activefa loop, later in
 *    this pass or in the next one);
 *  - interceptCSRequest, first thing in every pass, cancels the upload for a request that got
 *    stuck after the data completion: its post was processed after the activefa loop but before
 *    dispatchTransfers(), so the cancellation had to wait for the next pass.
 * A stale stuck request also has MegaClient::preparewait() schedule the next pass at once.
 *
 * A stuck request that is posted again after the cancellation is a retry: it was requeued by the
 * watchdog instead of being discarded.
 */
class UploadCancelledWithStuckFileAttributes
{
public:
    UploadCancelledWithStuckFileAttributes(MegaCancelToken* token, m_off_t dataSize):
        mToken{token},
        mDataSize{dataSize},
        mHook{[this](HttpReq* req)
              {
                  return onPost(req);
              }}
    {
        std::lock_guard<std::mutex> g{globalMegaTestHooks.mMutex};

        mPrevOnProgressCompletedUpdate = globalMegaTestHooks.onProgressCompletedUpdate;
        globalMegaTestHooks.onProgressCompletedUpdate =
            [this, prev = mPrevOnProgressCompletedUpdate](const m_off_t p)
        {
            if (prev)
                prev(p);
            onProgressCompleted(p);
        };

        mPrevInterceptCSRequest = globalMegaTestHooks.interceptCSRequest;
        globalMegaTestHooks.interceptCSRequest =
            [this, prev = mPrevInterceptCSRequest](HttpReq* pendingcs)
        {
            if (prev)
                prev(pendingcs);
            onExecPass();
        };
    }

    ~UploadCancelledWithStuckFileAttributes()
    {
        std::lock_guard<std::mutex> g{globalMegaTestHooks.mMutex};
        globalMegaTestHooks.interceptCSRequest = std::move(mPrevInterceptCSRequest);
        globalMegaTestHooks.onProgressCompletedUpdate = std::move(mPrevOnProgressCompletedUpdate);
    }

    // The installed hooks hold a pointer to this object.
    UploadCancelledWithStuckFileAttributes(const UploadCancelledWithStuckFileAttributes&) = delete;
    UploadCancelledWithStuckFileAttributes&
        operator=(const UploadCancelledWithStuckFileAttributes&) = delete;

    // Lets subsequent file attribute POSTs through; retries of the stuck ones are still detected.
    void disarm()
    {
        std::lock_guard<std::mutex> g{mMutex};
        mArmed = false;
    }

    size_t stuckCount() const
    {
        std::lock_guard<std::mutex> g{mMutex};
        return mStuckIds.size();
    }

    bool cancelled() const
    {
        std::lock_guard<std::mutex> g{mMutex};
        return mCancelled;
    }

    bool retried() const
    {
        std::lock_guard<std::mutex> g{mMutex};
        return mRetried;
    }

private:
    bool onPost(HttpReq* req)
    {
        auto* fa = dynamic_cast<HttpReqFA*>(req);
        if (!fa)
        {
            return false;
        }

        std::lock_guard<std::mutex> g{mMutex};
        if (mStuckIds.count(req->getId()))
        {
            LOG_info << "STUCK FILE ATTRIBUTE UPLOAD RETRIED (Req#" << req->getId() << ")";
            mRetried = true;
            return false;
        }

        if (!mArmed)
        {
            return false;
        }

        // as in StuckFileAttributeUpload: in flight for ever
        mStuckIds.insert(req->getId());
        req->status = REQ_INFLIGHT;
        if (mDataCompleted)
        {
            // stale straight away: the cancellation follows first thing in the next pass
            req->lastdata = Waiter::ds - MegaClient::PUTFATIMEOUT;
        }
        else
        {
            // fresh: the watchdog must not fire before the cancellation, which ages it
            req->lastdata = Waiter::ds;
            mStuckFresh.push_back(req);
        }
        LOG_info << "SIMULATING STUCK FILE ATTRIBUTE UPLOAD (Req#" << req->getId() << ") - "
                 << fa->th;
        return true;
    }

    void onProgressCompleted(const m_off_t p)
    {
        std::lock_guard<std::mutex> g{mMutex};
        if (mDataCompleted || p < mDataSize)
        {
            return;
        }

        mDataCompleted = true;
        LOG_info << "UPLOAD DATA COMPLETE (" << p << " bytes)";
        if (!mStuckIds.empty())
        {
            cancelUpload();
        }
    }

    void onExecPass()
    {
        std::lock_guard<std::mutex> g{mMutex};
        if (mDataCompleted && !mCancelled && !mStuckIds.empty())
        {
            cancelUpload();
        }
    }

    // mMutex held, SDK thread
    void cancelUpload()
    {
        for (HttpReq* req: mStuckFresh)
        {
            req->lastdata = Waiter::ds - MegaClient::PUTFATIMEOUT;
        }
        mStuckFresh.clear();
        mCancelled = true;
        LOG_info << "CANCELLING THE UPLOAD WITH " << mStuckIds.size()
                 << " STUCK (NOW STALE) FILE ATTRIBUTE UPLOAD(S)";
        mToken->cancel();
    }

    MegaCancelToken* const mToken;
    const m_off_t mDataSize;
    mutable std::mutex mMutex;
    bool mArmed = true;
    bool mDataCompleted = false;
    bool mCancelled = false;
    bool mRetried = false;
    std::set<uint32_t> mStuckIds;
    std::vector<HttpReq*> mStuckFresh; // stuck before the data completion, aged on cancellation
    std::function<void(const m_off_t)> mPrevOnProgressCompletedUpdate;
    std::function<void(HttpReq*)> mPrevInterceptCSRequest;
    sdk_test::ScopedHttpReqPostHook mHook; // last: uninstalled before the members it uses
};

} // namespace

/**
 * @brief SdkTest.StuckFileAttributeUploadTimesOutAndIsRetried
 *
 * A file attribute upload whose connection dies silently must not hold its activefa slot for
 * ever: once no data has moved for MegaClient::PUTFATIMEOUT it must be disconnected, requeued and
 * retried, so that the attribute still gets attached. Before that, it must be left alone.
 *
 * The stuck request is aged so that it hits the timeout 15s after being posted: no retry may be
 * observed during the first half of that window, the retry must follow, and setThumbnail must
 * then succeed.
 */
TEST_F(SdkTest, StuckFileAttributeUploadTimesOutAndIsRetried)
{
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    auto& client = *megaApi[0];

    std::unique_ptr<MegaNode> root(client.getRootNode());
    ASSERT_TRUE(root) << "Couldn't retrieve the root node";

    // a node to attach the attribute to (not a media file: no attributes are generated on upload)
    const std::string fileName = "putfa_stuck_file.sdktest";
    sdk_test::LocalTempFile file(u8path_compat(fileName), 1024u);

    MegaHandle nodeHandle = UNDEF;
    ASSERT_EQ(API_OK,
              doStartUpload(0,
                            &nodeHandle,
                            fileName.c_str(),
                            root.get(),
                            nullptr /*fileName*/,
                            ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
                            nullptr /*appData*/,
                            false /*isSourceTemporary*/,
                            false /*startFirst*/,
                            nullptr /*cancelToken*/))
        << "Couldn't upload the file to attach the attribute to";

    std::unique_ptr<MegaNode> node(client.getNodeByHandle(nodeHandle));
    ASSERT_TRUE(node) << "Couldn't retrieve the uploaded node";

    // the bytes to upload as the attribute
    const std::string thumbName = "putfa_stuck_thumb.sdktest";
    sdk_test::LocalTempFile thumb(u8path_compat(thumbName), 4096u);

    // the stuck request reaches the stale-data timeout this long after being posted
    constexpr dstime remaining = 150; // 15s
    ASSERT_LT(remaining, MegaClient::PUTFATIMEOUT);
    StuckFileAttributeUpload stuck(MegaClient::PUTFATIMEOUT - remaining);

    RequestTracker tracker(&client);
    client.setThumbnail(node.get(), thumbName.c_str(), &tracker);

    ASSERT_TRUE(WaitFor(
        [&]()
        {
            return stuck.seen();
        },
        defaultTimeoutMs))
        << "no file attribute upload was observed - the test is vacuous";

    // not stale enough yet: the request must be left in flight
    ASSERT_FALSE(WaitFor(
        [&]()
        {
            return stuck.retried();
        },
        static_cast<unsigned>(remaining / 2 * 100)))
        << "the file attribute upload was retried before it was stale for PUTFATIMEOUT";

    // stale for PUTFATIMEOUT: the request must be disconnected, requeued and retried
    ASSERT_TRUE(WaitFor(
        [&]()
        {
            return stuck.retried();
        },
        defaultTimeoutMs))
        << "the stuck file attribute upload was not retried after PUTFATIMEOUT";

    // ... and the retry must complete the operation
    ASSERT_EQ(API_OK, tracker.waitForResult())
        << "setThumbnail did not succeed after the stuck upload was retried";
}

/**
 * @brief SdkTest.StuckFileAttributeUploadOfCancelledUploadIsDiscarded
 *
 * The stale-data watchdog must leave alone a file attribute upload that the same pass of
 * MegaClient::exec has just discarded. That is the case for the attributes of an upload in
 * progress (the thumbnail and preview of an image, posted under the upload handle rather than a
 * node handle) that is cancelled while they are stuck: the REQ_INFLIGHT handling of activefa
 * processes the cancellation first, deleting the transfer and erasing the attribute request that
 * found it cancelled (the other ones, orphaned by then, are erased in turn), and must then stop
 * rather than fall through to the watchdog, which would requeue an erased request.
 *
 * The cancellation is arranged so that the stuck requests are already stale when that handling
 * processes it, and so that it is that handling, and not dispatchTransfers(), which does (see
 * UploadCancelledWithStuckFileAttributes). The upload must finish as cancelled, none of the
 * discarded requests may ever be posted again, and the channel must remain usable: a subsequent
 * upload of the same image gets its thumbnail and preview attached.
 */
TEST_F(SdkTest, StuckFileAttributeUploadOfCancelledUploadIsDiscarded)
{
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    auto& client = *megaApi[0];

    std::unique_ptr<MegaNode> root(client.getRootNode());
    ASSERT_TRUE(root) << "Couldn't retrieve the root node";

    // an image: its thumbnail and preview are generated on upload and posted under the upload
    // handle, as file attributes the upload waits for before creating the node
    ASSERT_TRUE(getFileFromArtifactory("test-data/" + IMAGEFILE, IMAGEFILE))
        << "Couldn't get " << IMAGEFILE;
    const auto imageSize = getFilesize(IMAGEFILE);
    ASSERT_GT(imageSize, 0) << "Couldn't read the size of " << IMAGEFILE;

    std::unique_ptr<MegaCancelToken> cancelToken{MegaCancelToken::createInstance()};
    ASSERT_TRUE(cancelToken);

    UploadCancelledWithStuckFileAttributes stuck(cancelToken.get(), imageSize);

    MegaHandle nodeHandle = UNDEF;
    ASSERT_EQ(API_EINCOMPLETE,
              doStartUpload(0,
                            &nodeHandle,
                            IMAGEFILE.c_str(),
                            root.get(),
                            nullptr /*fileName*/,
                            ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
                            nullptr /*appData*/,
                            false /*isSourceTemporary*/,
                            false /*startFirst*/,
                            cancelToken.get()))
        << "the upload cancelled while its file attribute uploads were stuck did not finish as "
           "cancelled";
    EXPECT_EQ(nodeHandle, UNDEF) << "the cancelled upload produced a node";

    // the test is vacuous unless the cancellation was issued as intended
    ASSERT_TRUE(stuck.cancelled())
        << "the upload was not cancelled once its data was complete - it failed on its own";
    ASSERT_GE(stuck.stuckCount(), size_t{1}) << "no file attribute upload was left stuck";

    // the discarded requests must not come back: a requeued one would be posted again right after
    // the file attribute backoff
    stuck.disarm();
    ASSERT_FALSE(WaitFor(
        [&]()
        {
            return stuck.retried();
        },
        3000u))
        << "a discarded file attribute upload was retried";

    // ... and the channel must be intact: a fresh upload of the same image gets its attributes
    MegaHandle healthyHandle = UNDEF;
    ASSERT_EQ(API_OK,
              doStartUpload(0,
                            &healthyHandle,
                            IMAGEFILE.c_str(),
                            root.get(),
                            nullptr /*fileName*/,
                            ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME,
                            nullptr /*appData*/,
                            false /*isSourceTemporary*/,
                            false /*startFirst*/,
                            nullptr /*cancelToken*/))
        << "the upload following the cancelled one did not succeed";

    std::unique_ptr<MegaNode> node(client.getNodeByHandle(healthyHandle));
    ASSERT_TRUE(node) << "Couldn't retrieve the node of the upload following the cancelled one";
    EXPECT_TRUE(node->hasThumbnail())
        << "the upload following the cancelled one did not get its thumbnail attached";
    EXPECT_TRUE(node->hasPreview())
        << "the upload following the cancelled one did not get its preview attached";
    EXPECT_FALSE(stuck.retried()) << "a discarded file attribute upload was retried";
}

#endif // MEGASDK_DEBUG_TEST_HOOKS_ENABLED
