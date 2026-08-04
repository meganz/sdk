#include "mega/db.h"
#include "mega/file.h"
#include "mega/megaapp.h"
#include "mega/megaclient.h"
#include "mega/testhooks.h"
#include "mega/transfer.h"
#include "sdk_test_utils.h"
#include "utils.h"

#include <gtest/gtest.h>

#include <filesystem>

using namespace mega;
using namespace sdk_test;

namespace
{
class MegaClientTest: public ::testing::Test
{
protected:
    void SetUp() override
    {
        app = std::make_shared<MegaApp>();
        client = mt::makeClient(*app);
    }

    void TearDown() override
    {
        client.reset();
        app.reset();
    }

    std::shared_ptr<MegaApp> app;
    std::shared_ptr<MegaClient> client;
    handle testHandle = 0x1234;

#ifdef MEGASDK_DEBUG_TEST_HOOKS_ENABLED
    HttpReq* setPendingScResponse(const std::string& payload)
    {
        HttpReq* req = new HttpReq;
        req->in = payload;
        req->contentlength = (m_off_t)payload.size();

        globalMegaTestHooks.interceptSCRequest = [payload, req](std::unique_ptr<HttpReq>& pendingsc)
        {
            pendingsc.reset(req);

            globalMegaTestHooks.interceptSCRequest = nullptr;
        };

        return req;
    }

    void setTimedOutPendingScRequest(bool responseStarted)
    {
        HttpReq* req = setPendingScResponse(R"({"a":[{}]})");
        client->chooseScParsingMode();

        req->status = REQ_INFLIGHT;
        req->mResponseStarted = responseStarted;
        req->lastdata = Waiter::ds -
                        (responseStarted ? HttpIO::SCREQUESTTIMEOUT : HttpIO::HEARTBEATTIMEOUT) - 1;
    }
#endif
};

TEST_F(MegaClientTest, isValidLocalSyncRoot_OK)
{
    const fs::path dirPath = fs::current_path() / "megaclient_test_valid_local_sync_root";
    LocalTempDir tempDir(dirPath);
    LocalPath localPath = LocalPath::fromAbsolutePath(path_u8string(dirPath));
    const auto [err, sErr, sWarn] = client->isValidLocalSyncRoot(localPath, testHandle);
    EXPECT_EQ(err, API_OK);
    EXPECT_EQ(sErr, NO_SYNC_ERROR);
    EXPECT_EQ(sWarn, NO_SYNC_WARNING);
}

TEST_F(MegaClientTest, isValidLocalSyncRoot_NotAbsolutePath)
{
    const fs::path relPath = fs::path("relative") / "path" / "to" / "dir";
    LocalPath localPath = LocalPath::fromRelativePath(path_u8string(relPath));
    const auto [err, sErr, sWarn] = client->isValidLocalSyncRoot(localPath, testHandle);
    EXPECT_EQ(err, API_EARGS);
    EXPECT_EQ(sErr, NO_SYNC_ERROR);
    EXPECT_EQ(sWarn, NO_SYNC_WARNING);
}

TEST_F(MegaClientTest, isValidLocalSyncRoot_NonExistentPath)
{
    const fs::path dirPath = fs::current_path() / "megaclient_test_non_existent_path";
    LocalPath localPath = LocalPath::fromAbsolutePath(path_u8string(dirPath));
    const auto [err, sErr, sWarn] = client->isValidLocalSyncRoot(localPath, testHandle);
    EXPECT_EQ(err, API_ENOENT);
    EXPECT_EQ(sErr, LOCAL_PATH_UNAVAILABLE);
    EXPECT_EQ(sWarn, NO_SYNC_WARNING);
}

TEST_F(MegaClientTest, isValidLocalSyncRoot_NotAFolder)
{
    const fs::path filePath = fs::current_path() / "megaclient_test_not_a_folder.txt";
    LocalTempFile tempFile(filePath, "Temporary file content");
    LocalPath localPath = LocalPath::fromAbsolutePath(path_u8string(filePath));
    const auto [err, sErr, sWarn] = client->isValidLocalSyncRoot(localPath, testHandle);
    EXPECT_EQ(err, API_EACCESS);
    EXPECT_EQ(sErr, INVALID_LOCAL_TYPE);
    EXPECT_EQ(sWarn, NO_SYNC_WARNING);
}

TEST_F(MegaClientTest, setMaxConnectionsAndPersistRejectsZero)
{
    EXPECT_EQ(client->setmaxconnectionsandpersist(GET, uint8_t{0}), API_EARGS);
    EXPECT_EQ(client->setmaxconnectionsandpersist(uint8_t{0}), API_EARGS);
}

TEST(FormatReqstatOpcode, PrintableAndNonPrintable)
{
    // The opcode (first letter of the API command) is surfaced verbatim rather
    // than mapped to a description, since the command set is a moving target
    // (SDK-6296). Printable bytes are shown as a quoted character.
    EXPECT_EQ(MegaClient::formatReqstatOpcode('p'), "'p'");
    EXPECT_EQ(MegaClient::formatReqstatOpcode('d'), "'d'");
    EXPECT_EQ(MegaClient::formatReqstatOpcode('m'), "'m'");

    // Non-printable bytes fall back to hex.
    EXPECT_EQ(MegaClient::formatReqstatOpcode('\x01'), "0x01");
    EXPECT_EQ(MegaClient::formatReqstatOpcode('\xff'), "0xff");
}

#ifdef MEGASDK_DEBUG_TEST_HOOKS_ENABLED
TEST_F(MegaClientTest, chooseScParsingMode_EnableAndDisableStreaming)
{
    // Default: disabled
    ASSERT_FALSE(client->isStreamingEnabled());

    // Enable streaming
    HttpReq* pendingScHolder = setPendingScResponse(R"({"apm":0,"a":[{}]})");
    client->chooseScParsingMode();

    EXPECT_TRUE(client->isStreamingEnabled());
    EXPECT_TRUE(pendingScHolder->mChunked);

    // Disable streaming
    pendingScHolder = setPendingScResponse(R"({"apm":1,"a":[{}]})");
    client->chooseScParsingMode();

    EXPECT_FALSE(client->isStreamingEnabled());
    EXPECT_FALSE(pendingScHolder->mChunked);
}

TEST_F(MegaClientTest, chooseScParsingMode_EnableStreamingWhenNoApm)
{
    // Default: disabled
    ASSERT_FALSE(client->isStreamingEnabled());

    // Enable streaming
    HttpReq* pendingScHolder = setPendingScResponse(R"({"a":[{}]})");
    client->chooseScParsingMode();

    EXPECT_TRUE(client->isStreamingEnabled());
    EXPECT_TRUE(pendingScHolder->mChunked);
}

TEST_F(MegaClientTest, chooseScParsingMode_DoesNothingForNonStartPayload)
{
    // Default: disabled
    ASSERT_FALSE(client->isStreamingEnabled());

    // Enable streaming
    HttpReq* pendingScHolder = setPendingScResponse(R"(["a":[{}]])");
    client->chooseScParsingMode();

    // No change
    EXPECT_FALSE(client->isStreamingEnabled());
    EXPECT_FALSE(pendingScHolder->mChunked);
}

TEST_F(MegaClientTest, chooseScParsingMode_DoesNothingForShortPayload)
{
    // Default: disabled
    ASSERT_FALSE(client->isStreamingEnabled());

    // Enable streaming
    HttpReq* pendingScHolder = setPendingScResponse(R"({"apm":)");
    client->chooseScParsingMode();

    // No change
    EXPECT_FALSE(client->isStreamingEnabled());
    EXPECT_FALSE(pendingScHolder->mChunked);
}

TEST_F(MegaClientTest, handleScTimeoutInFlightState_PreResponseTimeoutIsRetryable)
{
    setTimedOutPendingScRequest(false);
    EXPECT_TRUE(client->handleScTimeoutInFlightState());

    setTimedOutPendingScRequest(false);
    EXPECT_TRUE(client->handleScTimeoutInFlightState())
        << "pre-response heartbeat timeouts must not leave the SC timeout latch set";
}

TEST_F(MegaClientTest, handleScTimeoutInFlightState_PostResponseTimeoutLatches)
{
    setTimedOutPendingScRequest(true);
    EXPECT_TRUE(client->handleScTimeoutInFlightState());

    setTimedOutPendingScRequest(true);
    EXPECT_FALSE(client->handleScTimeoutInFlightState())
        << "post-response timeouts must set the SC timeout latch to break an endless "
           "request loop (until the next REQ_SUCCESS clears it)";
}
#endif

// Reproduction: UAF in TransferList::nexttransfers when a transfer callback re-enters a
// transfers-list getter. file_removed() stands in for the app re-entering getTransfers():
// it tombstones the deque front and calls size() -> applyErase() frees the block the
// nexttransfers iterator points into. Run under ASan (-DENABLE_ASAN=ON);
class ReentrantCompactApp: public MegaApp
{
public:
    MegaClient* client = nullptr;
    bool done = false;

    void file_removed(File*, const Error&) override
    {
        if (done || !client)
        {
            return;
        }
        done = true;

        auto& dq = client->transferlist.transfers[GET];

        // Tombstone the front of the deque. This is the exact same call the SDK makes in
        // production: TransferList::removetransfer() -> transfers[type].erase(it)
        // (transfer.cpp), reached via Transfer::removeAndDeleteSelf() when a cancelled transfer
        // is cleaned up inside this very nexttransfers() loop. erase() only sets the erased flag
        // + bumps nErased; it does not modify the deque structure, so the iterator used here
        // stays valid until the size() below flushes it.
        std::size_t n = 0;
        for (auto i = dq.begin(/*canHandleErasedElements*/ true);
             i != dq.end(/*canHandleErasedElements*/ true) && n < 1000;
             ++i, ++n)
        {
            dq.erase(i);
        }

        // Trigger the compaction the way getTransfers()/getNumPendingDownloads() do.
        // applyErase() pop_front-frees the now-empty front blocks, invalidating the iterator
        // nexttransfers is currently holding.
        (void)dq.size();
    }
};

TEST(TransferListReentrancy, nexttransfers_UAF_when_getter_compacts_deque_in_callback)
{
    auto app = std::make_shared<ReentrantCompactApp>();
    auto client = mt::makeClient(*app);
    app->client = client.get();

    std::vector<Transfer*> transfers;
    std::vector<File*> files;

    // Enough transfers to span several libc++ deque blocks, so the pop_front compaction
    // actually frees the block the iterator points into (a single-block deque would only read
    // shifted-but-valid memory and might not fault under ASan).
    constexpr int kCount = 1024;
    for (int i = 0; i < kCount; ++i)
    {
        Transfer* t = new Transfer(client.get(), GET);
        transfers.push_back(t);
        client->transferlist.transfers[GET].push_back(t);

        // Every transfer keeps one non-cancelled file so it is never emptied, and therefore
        // never removeAndDeleteSelf()'d during the loop (which would delete it out from under
        // the teardown below). This isolates the iterator bug from the transfer-deletion path.
        File* keep = new File();
        keep->transfer = t;
        keep->file_it = t->files.insert(t->files.end(), keep);
        files.push_back(keep);
    }

    // The first transfer additionally carries a cancelled file, so removeCancelledTransferFiles()
    // removes it and fires file_removed() -> our stand-in for the app's re-entrant getTransfers().
    // Its surviving 'keep' file means t0 itself is not emptied.
    Transfer* t0 = transfers.front();
    File* cancelled = new File();
    cancelled->transfer = t0;
    cancelled->cancelToken = CancelToken(true); // cancelled
    cancelled->file_it = t0->files.insert(t0->files.begin(), cancelled); // processed first
    files.push_back(cancelled);

    std::function<bool(Transfer*)> cont = [](Transfer*)
    {
        return false;
    };
    std::function<bool(direction_t)> dirCont = [](direction_t)
    {
        return true;
    };
    TransferDbCommitter committer(client->tctable);

    // The nexttransfers iterates a copy of the pointers, so the compaction is
    // harmless and this returns normally.
    client->transferlist.nexttransfers(cont, dirCont, committer);

    // Clear the deque first so ~Transfer's removetransfer() is a no-op (transfers_it is already
    // end() from the ctor, slot is null, finished is false -> ~Transfer is otherwise safe).
    client->transferlist.transfers[GET].clear();
    for (Transfer* t: transfers)
    {
        delete t;
    }
    for (File* f: files)
    {
        delete f;
    }
}

} // namespace
