#include <memory>
#include <string>
#include <gtest/gtest.h>
#include "mega/types.h"
#include "mega/utils.h"
#include "mega/utils_upload.h"
#include "mega/node.h"
#include "mega/filefingerprint.h"
#include "mega/filesystem.h"
#include "mega/localpath.h"
#include "mega/waiter.h"
#include "mega/megaapp.h"
#include "mega/megaclient.h"

namespace {

bool g_compare_result = false; // true: MAC match, false: MAC not match

mega::MegaApp app;

std::shared_ptr<mega::MegaClient> makeDummyClient(mega::MegaApp& app, mega::DbAccess* dbAccess)
{
    struct HttpIo : mega::HttpIO
    {
        void addevents(mega::Waiter*, int) override {}
        void post(struct mega::HttpReq*, const char* = NULL, unsigned = 0) override {}
        void cancel(mega::HttpReq*) override {}
        m_off_t postpos(void*) override { return 0; }
        bool doio(void) override { return true; }
        void setuseragent(std::string*) override {}
    };

    struct MockWaiter: mega::Waiter
    {
        int wait() override { return 0; }
        void notify() override {}
    };

    auto httpio = new HttpIo;
    auto waiter = std::make_shared<MockWaiter>();

    auto deleter = [httpio](mega::MegaClient* client)
    {
        delete client;
        delete httpio;
    };

    return std::shared_ptr<mega::MegaClient>(new mega::MegaClient{
        &app, waiter, httpio, dbAccess, nullptr, "TEST", "unit_test", 0}, deleter);
}

std::shared_ptr<mega::MegaClient> g_testClient = makeDummyClient(app, nullptr);

} // namespace

namespace mega {

bool mockCompareLocalFileMetaMacWithNode(mega::FileAccess* fa, mega::Node* node)
{
    return g_compare_result;
}

} // namespace mega

class MockNode : public mega::Node {
public:
    MockNode(mega::nodetype_t mtype, bool valid = true)
        : mega::Node(*g_testClient, mega::NodeHandle(),mega::NodeHandle(), mtype, 0, 0, "", 0)
    {
        this->isvalid = valid;
    }
};

class MockFileAccess : public mega::FileAccess
{
public:
    MockFileAccess(bool macMatch) : FileAccess(nullptr) {}

    bool fopen(const mega::LocalPath&, bool, bool, mega::FSLogging,
               mega::DirAccess* = nullptr,
               bool = false,
               bool = false,
               mega::LocalPath* = nullptr) override {
        return true;
    }

    void updatelocalname(const mega::LocalPath&, bool) override {}

    void fclose() override {}

    bool fwrite(const byte*, unsigned, m_off_t) override {
        return true;
    }

    bool fstat(mega::m_time_t& modified, m_off_t& size) override {
        modified = 0;
        size = 0;
        return true;
    }

    bool ftruncate(m_off_t = 0) override {
        return true;
    }

    bool sysread(byte*, unsigned, m_off_t) override {
        return true;
    }

    bool sysstat(mega::m_time_t*, m_off_t*, mega::FSLogging) override {
        return true;
    }

    bool sysopen(bool, mega::FSLogging) override {
        return true;
    }

    void sysclose() override {}

    mega::AsyncIOContext* newasynccontext() override {
        return nullptr;
    }
};

class UploadJudgementTest : public ::testing::Test 
{
protected:
    std::string testFileName = "test_upload.txt";

    void SetUp() override {
        g_compare_result = false;
    }

    std::unique_ptr<MockFileAccess> createFileAccess(bool macMatch) {
        return std::make_unique<MockFileAccess>(macMatch);
    }

    std::shared_ptr<MockNode> createFileNode(bool valid = true) {
        auto node = std::make_shared<MockNode>(mega::FILENODE, valid);
        node->isvalid = valid;
        return node;
    }

    std::shared_ptr<MockNode> createFolderNode() {
        return std::make_shared<MockNode>(mega::FOLDERNODE);
    }
};

// Test1: No remote node → upload required
TEST_F(UploadJudgementTest, NoPreviousNode) {
    mega::FileFingerprint fp;
    fp.isvalid = true;

    auto result = mega::shouldProceedWithUpload(
        nullptr,
        fp,
        createFileAccess(true),
        false,
        testFileName,
        mega::mockCompareLocalFileMetaMacWithNode
    );

    EXPECT_TRUE(result.needUpload);
    EXPECT_EQ(result.sourceNode, nullptr);
}

// Test2: Remote node is folder → upload not allowed
TEST_F(UploadJudgementTest, PreviousNodeIsFolder) {
    mega::FileFingerprint fp;
    fp.isvalid = true;

    auto result = mega::shouldProceedWithUpload(
        createFolderNode(),
        fp,
        createFileAccess(true),
        false,
        testFileName,
        mega::mockCompareLocalFileMetaMacWithNode
    );

    EXPECT_FALSE(result.needUpload);
    EXPECT_EQ(result.sourceNode, nullptr);
}

// Test3: Local fingerprint invalid → upload required
TEST_F(UploadJudgementTest, LocalFingerprintInvalid) {
    mega::FileFingerprint invalidFp;
    invalidFp.isvalid = false;

    auto result = mega::shouldProceedWithUpload(
        createFileNode(),
        invalidFp,
        createFileAccess(true),
        false,
        testFileName,
        mega::mockCompareLocalFileMetaMacWithNode
    );

    EXPECT_TRUE(result.needUpload);
    EXPECT_EQ(result.sourceNode, nullptr);
}

// Test4: Remote node invalid → upload required
TEST_F(UploadJudgementTest, PreviousNodeInvalid) {
    mega::FileFingerprint fp;
    fp.isvalid = true;

    auto result = mega::shouldProceedWithUpload(
        createFileNode(false),
        fp,
        createFileAccess(true),
        false,
        testFileName,
        mega::mockCompareLocalFileMetaMacWithNode
    );

    EXPECT_TRUE(result.needUpload);
    EXPECT_EQ(result.sourceNode, nullptr);
}

// Test5: Fingerprints mismatch → upload required
TEST_F(UploadJudgementTest, FingerprintsMismatch) {
    mega::FileFingerprint localFp;
    localFp.isvalid = true;
    localFp.crc[0] = 0x01;

    auto remoteNode = createFileNode();
    remoteNode->crc[0] = 0x02;

    auto result = mega::shouldProceedWithUpload(
        remoteNode,
        localFp,
        createFileAccess(true),
        false,
        testFileName,
        mega::mockCompareLocalFileMetaMacWithNode
    );

    EXPECT_TRUE(result.needUpload);
    EXPECT_EQ(result.sourceNode, nullptr);
}

// Test6: Fingerprint+MAC match, not allow duplicate → remote copy (no upload)
TEST_F(UploadJudgementTest, MatchFingerprintAndMac_NoDuplicates) {
    mega::FileFingerprint fp;
    fp.isvalid = true;
    fp.crc[0] = 0x01;

    auto remoteNode = createFileNode();
    *static_cast<mega::FileFingerprint*>(remoteNode.get()) = fp;
    //auto nodeAsFp = static_cast<mega::FileFingerprint*>(remoteNode.get());
    //*nodeAsFp = fp;
    //bool fingerprintsMatch = (fp == *nodeAsFp);

    g_compare_result = true;
    auto result = mega::shouldProceedWithUpload(
        remoteNode,
        fp,
        createFileAccess(true),
        false,
        testFileName,
        mega::mockCompareLocalFileMetaMacWithNode
    );

    EXPECT_FALSE(result.needUpload);
    EXPECT_EQ(result.sourceNode, remoteNode);
}

// Test7: Fingerprint+MAC match, allow duplicate → upload required
TEST_F(UploadJudgementTest, MatchFingerprintAndMac_AllowDuplicates) {
    mega::FileFingerprint fp;
    fp.isvalid = true;
    fp.crc[0] = 0x01;

    auto remoteNode = createFileNode();
    *static_cast<mega::FileFingerprint*>(remoteNode.get()) = fp;

    g_compare_result = true;
    auto result = mega::shouldProceedWithUpload(
        remoteNode,
        fp,
        createFileAccess(true),
        true,
        testFileName,
        mega::mockCompareLocalFileMetaMacWithNode
    );

    EXPECT_TRUE(result.needUpload);
    EXPECT_EQ(result.sourceNode, nullptr);
}

// Test8: Fingerprint match, MAC mismatch → upload required
TEST_F(UploadJudgementTest, MatchFingerprint_NoMacMatch) {
    mega::FileFingerprint fp;
    fp.isvalid = true;
    fp.crc[0] = 0x01;

    auto remoteNode = createFileNode();
    *static_cast<mega::FileFingerprint*>(remoteNode.get()) = fp;

    g_compare_result = false;
    auto result = mega::shouldProceedWithUpload(
        remoteNode,
        fp,
        createFileAccess(false),
        false,
        testFileName,
        mega::mockCompareLocalFileMetaMacWithNode
    );

    EXPECT_TRUE(result.needUpload);
    EXPECT_EQ(result.sourceNode, nullptr);
}