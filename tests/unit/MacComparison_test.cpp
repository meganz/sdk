/**
 * @file tests/unit/MacComparison_test.cpp
 * @brief Unit tests for MAC comparison functions used in upload deduplication.
 */

#include "DefaultedDbTable.h"
#include "DefaultedFileAccess.h"
#include "megaapi_impl.h"
#include "utils.h"

#include <gtest/gtest.h>
#include <mega/crypto/cryptopp.h>
#include <mega/filesystem.h>
#include <mega/megaapp.h>
#include <mega/megaclient.h>
#include <mega/node.h>
#include <mega/utils.h>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <vector>

namespace
{

using namespace mega;

/**
 * @brief Mock FileAccess that simulates successful reads with configurable content.
 *
 * Can be configured to fail reads at a specific byte position to simulate I/O errors
 * or file truncation during MAC computation.
 */
class MockFileAccessForMac: public mt::DefaultedFileAccess
{
public:
    /**
     * @param content The file content to read from.
     * @param failAtBytePos If > 0, reads that would access data at or beyond this position will
     * fail.
     * @param failErrorCode The error code to set when read fails.
     */
    MockFileAccessForMac(const std::vector<byte>& content,
                         size_t failAtBytePos = 0,
                         int failErrorCode = 5):
        mContent(content),
        mFailAtBytePos(failAtBytePos),
        mFailErrorCode(failErrorCode)
    {
        size = static_cast<m_off_t>(content.size());
        type = FILENODE;
    }

    bool openf(FSLogging) override
    {
        mIsOpen = true;
        return true;
    }

    void closef() override
    {
        mIsOpen = false;
    }

    void fclose() override
    {
        mIsOpen = false;
    }

    bool sysread(void* buffer,
                 unsigned long length,
                 m_off_t offset,
                 bool* retry,
                 int* errorcode) override
    {
        return doRead(buffer, length, offset, retry, errorcode);
    }

    bool frawread(void* buffer,
                  unsigned long length,
                  m_off_t offset,
                  bool /*alreadyOpened*/,
                  FSLogging,
                  bool* retry = nullptr,
                  int* errorcode = nullptr) override
    {
        return doRead(buffer, length, offset, retry, errorcode);
    }

    // Allow test to modify failure position after creation
    void setFailAtBytePos(size_t pos, int errCode = 5)
    {
        mFailAtBytePos = pos;
        mFailErrorCode = errCode;
    }

    // Invoked on every successful read, with the offset being read. Lets a test count the
    // chunks consumed and cancel a CancelToken mid-computation.
    void setOnRead(std::function<void(m_off_t offset)> onRead)
    {
        mOnRead = std::move(onRead);
    }

    void setOnReadFailure(std::function<void()> onReadFailure)
    {
        mOnReadFailure = std::move(onReadFailure);
    }

private:
    bool doRead(void* buffer, unsigned long length, m_off_t offset, bool* retry, int* errorcode)
    {
        if (!errorcode)
            errorcode = &this->errorcode;

        if (retry)
            *retry = false;

        if (!mIsOpen || offset < 0)
            return false;

        const auto nbytes = static_cast<std::size_t>(length);
        const auto off = static_cast<std::size_t>(offset);

        // Simulate read failure at specific position
        if (mFailAtBytePos > 0 && (off + nbytes > mFailAtBytePos))
        {
            if (mOnReadFailure)
                mOnReadFailure();
            *errorcode = mFailErrorCode;
            return false;
        }

        // Normal out-of-bounds check
        if (off > mContent.size() || nbytes > (mContent.size() - off))
        {
            *errorcode = 38; // EOF-style error
            return false;
        }

        if (mOnRead)
            mOnRead(offset);

        if (buffer)
            std::memcpy(buffer, mContent.data() + off, nbytes);

        return true;
    }

    std::vector<byte> mContent;
    bool mIsOpen{false};
    size_t mFailAtBytePos{0};
    int mFailErrorCode{5};
    std::function<void(m_off_t offset)> mOnRead;
    std::function<void()> mOnReadFailure;
};

/**
 * @brief Helper to create a valid node key for testing.
 *
 * Creates a key with the specified IV and MAC values.
 */
std::string createTestNodeKey(int64_t iv, int64_t mac)
{
    std::string key(SymmCipher::KEYLENGTH + 2 * sizeof(int64_t), '\0');
    // Fill key portion with test data
    for (size_t i = 0; i < SymmCipher::KEYLENGTH; ++i)
    {
        key[i] = static_cast<char>(i ^ 0xAB);
    }
    // Set IV and MAC
    std::memcpy(&key[SymmCipher::KEYLENGTH], &iv, sizeof(iv));
    std::memcpy(&key[SymmCipher::KEYLENGTH + sizeof(iv)], &mac, sizeof(mac));
    return key;
}

std::string createTestNodeKeyWithSameCipherKey(const std::string& sourceKey,
                                               int64_t iv,
                                               int64_t mac)
{
    assert(sourceKey.size() == FILENODEKEYLENGTH);
    std::string key = createTestNodeKey(iv, mac);

    // For file nodes the effective AES key is the XOR of the two 16-byte halves. Preserve it
    // while replacing the IV/MAC half, otherwise changing the expected MAC also changes the MAC
    // calculation itself.
    for (size_t i = 0; i < SymmCipher::KEYLENGTH; ++i)
    {
        key[i] =
            sourceKey[i] ^ sourceKey[SymmCipher::KEYLENGTH + i] ^ key[SymmCipher::KEYLENGTH + i];
    }
    return key;
}

} // anonymous namespace

// ============================================================================
// Test Cases
// ============================================================================

TEST(MacComparison, SuccessfulMacMatch)
{
    // Create test content
    std::vector<byte> content(1024, 0x42); // 1KB of 0x42
    const int64_t iv = 0x1234567890ABCDEF;

    // Compute MAC and verify the result is valid
    MockFileAccessForMac fa(content);
    ASSERT_TRUE(fa.openf(FSLogging::logOnError));
    std::string dummyKey = createTestNodeKey(iv, 0); // MAC=0 to get computed MAC
    MacComparisonResult result =
        CompareLocalFileMetaMacWithNodeKey(&fa, dummyKey, FILENODE, "test.txt", CancelToken());

    // Verify MAC was computed successfully
    EXPECT_EQ(result.errorCode, 0);
    EXPECT_NE(result.localMac, INVALID_META_MAC);
    EXPECT_EQ(result.remoteMac, 0); // We passed 0 as remote MAC

    // Since remoteMac (0) != localMac (computed), areEqualMacs should be false
    EXPECT_FALSE(result.areEqualMacs);

    // Verify the MAC is deterministic by computing it again on a fresh mock
    // with the correct remote MAC
    MockFileAccessForMac fa2(content);
    ASSERT_TRUE(fa2.openf(FSLogging::logOnError));
    std::string correctKey = createTestNodeKeyWithSameCipherKey(dummyKey, iv, result.localMac);

    MacComparisonResult result2 =
        CompareLocalFileMetaMacWithNodeKey(&fa2, correctKey, FILENODE, "test.txt", CancelToken());

    // Now verify the comparison works when MACs match
    EXPECT_EQ(result2.errorCode, 0);
    EXPECT_EQ(result2.remoteMac, result.localMac);
    EXPECT_TRUE(result2.areEqualMacs);
}

TEST(MacComparison, MacMismatch_DifferentContent)
{
    // Create test content
    std::vector<byte> content(1024, 0x42);

    MockFileAccessForMac fa(content);
    ASSERT_TRUE(fa.openf(FSLogging::logOnError));

    // Create a key with a different (wrong) MAC value
    // Use static_cast to avoid implicit unsigned-to-signed narrowing on platforms where
    // the hex literal is unsigned long (> INT64_MAX)
    const int64_t wrongMac = static_cast<int64_t>(0xDEADBEEFCAFEBABEULL);
    std::string wrongKey = createTestNodeKey(0x1234567890ABCDEF, wrongMac);

    MacComparisonResult result =
        CompareLocalFileMetaMacWithNodeKey(&fa, wrongKey, FILENODE, "test.txt", CancelToken());

    // Should have computed MAC successfully but values don't match
    EXPECT_FALSE(result.areEqualMacs);
    EXPECT_EQ(result.errorCode, 0); // No error, just different content
    EXPECT_NE(result.localMac, INVALID_META_MAC);
    EXPECT_EQ(result.remoteMac, wrongMac);
    EXPECT_NE(result.localMac, result.remoteMac);
}

TEST(MacComparison, ReadError_MidFileFailure)
{
    // Create content large enough to require multiple reads (>128KB chunks)
    std::vector<byte> content(256 * 1024, 0x42); // 256KB

    // Fail after reading 100KB (mid-way through computation)
    MockFileAccessForMac fa(content, 100 * 1024 /*failAtBytePos*/, 5 /*EIO-style error*/);
    ASSERT_TRUE(fa.openf(FSLogging::logOnError));

    std::string nodeKey = createTestNodeKey(0x1234567890ABCDEF, 0x1111111111111111);

    MacComparisonResult result =
        CompareLocalFileMetaMacWithNodeKey(&fa, nodeKey, FILENODE, "test.txt", CancelToken());

    // Should report read error
    EXPECT_FALSE(result.areEqualMacs);
    EXPECT_NE(result.errorCode, 0); // Should have an error code
    EXPECT_EQ(result.localMac, INVALID_META_MAC);
    EXPECT_EQ(result.remoteMac, static_cast<int64_t>(0x1111111111111111));
}

TEST(MacComparison, ReadError_FileTruncated)
{
    // Create small content but claim larger size (simulates file truncation after fopen)
    std::vector<byte> content(100, 0x42);

    MockFileAccessForMac fa(content);
    fa.size = 1024; // Claim file is larger than actual content
    ASSERT_TRUE(fa.openf(FSLogging::logOnError));

    std::string nodeKey = createTestNodeKey(0x1234567890ABCDEF, 0x2222222222222222);

    MacComparisonResult result =
        CompareLocalFileMetaMacWithNodeKey(&fa, nodeKey, FILENODE, "test.txt", CancelToken());

    // Should report read error (trying to read beyond available data)
    EXPECT_FALSE(result.areEqualMacs);
    EXPECT_NE(result.errorCode, 0); // Should have an error code
    EXPECT_EQ(result.localMac, INVALID_META_MAC);
}

TEST(MacComparison, ErrorCodeIsCaptured)
{
    // Test that the specific error code from the FileAccess is captured
    std::vector<byte> content(256 * 1024, 0x42);

    const int expectedErrorCode = 42; // Custom error code
    MockFileAccessForMac fa(content, 50 * 1024, expectedErrorCode);
    ASSERT_TRUE(fa.openf(FSLogging::logOnError));

    std::string nodeKey = createTestNodeKey(0x1234567890ABCDEF, 0x3333333333333333);

    MacComparisonResult result =
        CompareLocalFileMetaMacWithNodeKey(&fa, nodeKey, FILENODE, "test.txt", CancelToken());

    EXPECT_FALSE(result.areEqualMacs);
    EXPECT_EQ(result.errorCode, expectedErrorCode);
}

TEST(MacComparison, EmptyFile)
{
    // Empty file should still compute a valid MAC
    std::vector<byte> content; // Empty
    const int64_t iv = 0x1234567890ABCDEF;

    // First, compute the reference MAC
    MockFileAccessForMac fa1(content);
    fa1.size = 0;
    ASSERT_TRUE(fa1.openf(FSLogging::logOnError));
    std::string dummyKey = createTestNodeKey(iv, 0);
    MacComparisonResult refResult =
        CompareLocalFileMetaMacWithNodeKey(&fa1, dummyKey, FILENODE, "empty.txt", CancelToken());
    ASSERT_EQ(refResult.errorCode, 0) << "Failed to compute reference MAC";
    int64_t expectedMac = refResult.localMac;

    // Now test with the correct MAC
    MockFileAccessForMac fa2(content);
    fa2.size = 0;
    ASSERT_TRUE(fa2.openf(FSLogging::logOnError));
    std::string correctKey = createTestNodeKey(iv, expectedMac);

    MacComparisonResult result =
        CompareLocalFileMetaMacWithNodeKey(&fa2, correctKey, FILENODE, "empty.txt", CancelToken());

    EXPECT_TRUE(result.areEqualMacs);
    EXPECT_EQ(result.errorCode, 0);
}

// ============================================================================
// Cancellation
// ============================================================================

TEST(MacComparison, CancelledBeforeComputation)
{
    std::vector<byte> content(256 * 1024, 0x42);

    MockFileAccessForMac fa(content);
    ASSERT_TRUE(fa.openf(FSLogging::logOnError));

    int reads = 0;
    fa.setOnRead(
        [&reads](m_off_t)
        {
            ++reads;
        });

    CancelToken cancelToken(true); // already cancelled when the comparison starts

    const int64_t remoteMac = 0x4444444444444444;
    std::string nodeKey = createTestNodeKey(0x1234567890ABCDEF, remoteMac);

    MacComparisonResult result =
        CompareLocalFileMetaMacWithNodeKey(&fa, nodeKey, FILENODE, "cancelled.txt", cancelToken);

    EXPECT_EQ(result.errorCode, API_EINCOMPLETE);
    EXPECT_FALSE(result.areEqualMacs); // must not be read as "the MACs differ"
    EXPECT_EQ(result.localMac, INVALID_META_MAC);
    EXPECT_EQ(result.remoteMac, remoteMac);
    EXPECT_EQ(reads, 0); // not a single byte read
}

TEST(MacComparison, CancelledBeforeEmptyFileComputation)
{
    MockFileAccessForMac fa({});
    ASSERT_TRUE(fa.openf(FSLogging::logOnError));

    CancelToken cancelToken(true);
    std::string nodeKey = createTestNodeKey(0x1234567890ABCDEF, 0x5555555555555555);

    MacComparisonResult result =
        CompareLocalFileMetaMacWithNodeKey(&fa, nodeKey, FILENODE, "empty.txt", cancelToken);

    EXPECT_EQ(result.errorCode, API_EINCOMPLETE);
    EXPECT_FALSE(result.areEqualMacs);
    EXPECT_EQ(result.localMac, INVALID_META_MAC);
}

TEST(MacComparison, CancelledMidComputation)
{
    // 1MB is read in growing chunks (128KB, 256KB, ...), so there is more than one loop
    // iteration available to be interrupted.
    std::vector<byte> content(1024 * 1024, 0x42);

    MockFileAccessForMac fa(content);
    ASSERT_TRUE(fa.openf(FSLogging::logOnError));

    CancelToken cancelToken(false);
    int reads = 0;
    fa.setOnRead(
        [&](m_off_t)
        {
            if (++reads == 1)
                cancelToken.cancel();
        });

    std::string nodeKey = createTestNodeKey(0x1234567890ABCDEF, 0x5555555555555555);

    MacComparisonResult result =
        CompareLocalFileMetaMacWithNodeKey(&fa, nodeKey, FILENODE, "big.txt", cancelToken);

    EXPECT_EQ(result.errorCode, API_EINCOMPLETE);
    EXPECT_FALSE(result.areEqualMacs);
    EXPECT_EQ(result.localMac, INVALID_META_MAC);
    // Stopped at the next chunk boundary rather than reading the remaining ~900KB.
    EXPECT_EQ(reads, 1);
}

TEST(MacComparison, CancelledWhileReadingOnlyChunk)
{
    std::vector<byte> content(128 * 1024, 0x42);

    MockFileAccessForMac fa(content);
    ASSERT_TRUE(fa.openf(FSLogging::logOnError));

    CancelToken cancelToken(false);
    int reads = 0;
    fa.setOnRead(
        [&](m_off_t)
        {
            ++reads;
            cancelToken.cancel();
        });

    std::string nodeKey = createTestNodeKey(0x1234567890ABCDEF, 0x5555555555555555);

    MacComparisonResult result =
        CompareLocalFileMetaMacWithNodeKey(&fa, nodeKey, FILENODE, "one-chunk.txt", cancelToken);

    EXPECT_EQ(result.errorCode, API_EINCOMPLETE);
    EXPECT_FALSE(result.areEqualMacs);
    EXPECT_EQ(result.localMac, INVALID_META_MAC);
    EXPECT_EQ(reads, 1);
}

TEST(MacComparison, UncancelledTokenComputesTheSameMac)
{
    std::vector<byte> content(256 * 1024, 0x42);
    const int64_t iv = 0x1234567890ABCDEF;
    std::string dummyKey = createTestNodeKey(iv, 0);

    MockFileAccessForMac fa1(content);
    ASSERT_TRUE(fa1.openf(FSLogging::logOnError));
    MacComparisonResult noToken =
        CompareLocalFileMetaMacWithNodeKey(&fa1, dummyKey, FILENODE, "t.txt", CancelToken());

    MockFileAccessForMac fa2(content);
    ASSERT_TRUE(fa2.openf(FSLogging::logOnError));
    CancelToken cancelToken(false);
    MacComparisonResult withToken =
        CompareLocalFileMetaMacWithNodeKey(&fa2, dummyKey, FILENODE, "t.txt", cancelToken);

    EXPECT_NE(noToken.errorCode, API_EINCOMPLETE);
    EXPECT_NE(withToken.errorCode, API_EINCOMPLETE);
    EXPECT_EQ(withToken.errorCode, 0);
    EXPECT_NE(withToken.localMac, INVALID_META_MAC);
    EXPECT_EQ(withToken.localMac, noToken.localMac);
}

TEST(MacComparison, ReadErrorIsNotReportedAsCancellation)
{
    std::vector<byte> content(256 * 1024, 0x42);

    const int expectedErrorCode = 42;
    MockFileAccessForMac fa(content, 50 * 1024, expectedErrorCode);
    ASSERT_TRUE(fa.openf(FSLogging::logOnError));

    CancelToken cancelToken(false); // live token, never cancelled
    std::string nodeKey = createTestNodeKey(0x1234567890ABCDEF, 0x3333333333333333);

    MacComparisonResult result =
        CompareLocalFileMetaMacWithNodeKey(&fa, nodeKey, FILENODE, "test.txt", cancelToken);

    EXPECT_NE(result.errorCode, API_EINCOMPLETE);
    EXPECT_FALSE(result.areEqualMacs);
    EXPECT_EQ(result.errorCode, expectedErrorCode);
}

TEST(MacComparison, ReadErrorWinsWhenReadAlsoCancelsToken)
{
    std::vector<byte> content(256 * 1024, 0x42);

    const int expectedErrorCode = 42;
    MockFileAccessForMac fa(content, 50 * 1024, expectedErrorCode);
    ASSERT_TRUE(fa.openf(FSLogging::logOnError));

    CancelToken cancelToken(false);
    fa.setOnReadFailure(
        [&cancelToken]()
        {
            cancelToken.cancel();
        });
    std::string nodeKey = createTestNodeKey(0x1234567890ABCDEF, 0x3333333333333333);

    MacComparisonResult result =
        CompareLocalFileMetaMacWithNodeKey(&fa, nodeKey, FILENODE, "test.txt", cancelToken);

    EXPECT_NE(result.errorCode, API_EINCOMPLETE);
    EXPECT_FALSE(result.areEqualMacs);
    EXPECT_EQ(result.errorCode, expectedErrorCode);
}

// ============================================================================
// Upload-dedup path: CompareLocalFileWithNodeMacAndFpExludingMtime
//
// This is the comparison the upload dedup in sendPendingTransfers performs, on the SDK thread
// while holding sdkMutex. A cancelled comparison must report NODE_COMP_CANCELLED rather than
// NODE_COMP_DIFFERS_MAC: the latter would mean "these are different files", causing a needless
// re-upload and a bogus event 800036.
// ============================================================================

namespace
{

class UploadDedupMacTest: public ::testing::Test
{
protected:
    void SetUp() override
    {
        // Pure in-memory node manager backed by a stub DB table.
        auto* table = new mt::DefaultedDbTable(mRng);
        client->sctable.reset(table);
        client->mNodeManager.setTable(table);

        // A real local file, since the function opens the path through client->fsaccess.
        // Contents and size are irrelevant beyond being non-empty: a pre-cancelled token stops
        // the read loop on its first iteration.
        {
            std::ofstream out(mFileName, std::ios::binary);
            const std::vector<char> content(1024, 'A');
            out.write(content.data(), static_cast<std::streamsize>(content.size()));
        }

        mPath = LocalPath::fromAbsolutePath(mFileName);

        // Give the node the local file's fingerprint so the comparison gets past the fingerprint
        // check and actually reaches the MAC computation.
        mNode = mt::makeNode(*client, FILENODE, NodeHandle().set6byte(1), nullptr);
        ASSERT_TRUE(mNode->keyApplied());

        auto fa = client->fsaccess->newfileaccess();
        ASSERT_TRUE(fa->fopen(mPath, OPEN_RDONLY, FSLogging::logOnError));
        ASSERT_TRUE(mLocalFp.genfingerprint(fa.get()));
        *static_cast<FileFingerprint*>(mNode.get()) = mLocalFp;
    }

    void TearDown() override
    {
        mNode.reset();
        client.reset();
        std::remove(mFileName);
    }

    const char* mFileName{"MacComparison_uploadDedup.tmp"};
    LocalPath mPath;
    FileFingerprint mLocalFp;
    std::shared_ptr<Node> mNode;

    PrnGen mRng;
    MegaApp app;
    std::shared_ptr<MegaClient> client = mt::makeClient(app);
};

} // anonymous namespace

TEST_F(UploadDedupMacTest, CancelledComparisonIsNotReportedAsMacMismatch)
{
    CancelToken cancelToken(true); // already cancelled
    ASSERT_FALSE(client->reqs.readyToSend());

    auto [compRes, localMac] = CompareLocalFileWithNodeMacAndFpExludingMtime(*client,
                                                                             mPath,
                                                                             mLocalFp,
                                                                             mNode.get(),
                                                                             cancelToken);

    EXPECT_EQ(compRes, NODE_COMP_CANCELLED);
    EXPECT_NE(compRes, NODE_COMP_DIFFERS_MAC); // would trigger a re-upload and event 800036
    EXPECT_EQ(localMac, INVALID_META_MAC);
    EXPECT_FALSE(client->reqs.readyToSend()) << "cancellation queued an event command";
}

// Discriminator for the test above: with a live token the same setup does reach the MAC
// comparison and reports a real mismatch, so NODE_COMP_CANCELLED above is genuinely the
// cancellation and not just "this setup never compares MACs".
// mtime is made to differ so the sameMtime branch (which reports event 800036) is not entered.
TEST_F(UploadDedupMacTest, UncancelledComparisonStillReachesTheMacComparison)
{
    mNode->mtime = mLocalFp.mtime + 1;

    CancelToken cancelToken(false); // live, never cancelled

    auto [compRes, localMac] = CompareLocalFileWithNodeMacAndFpExludingMtime(*client,
                                                                             mPath,
                                                                             mLocalFp,
                                                                             mNode.get(),
                                                                             cancelToken);

    // makeNode's key is filler bytes, so the computed MAC cannot match the node's.
    EXPECT_EQ(compRes, NODE_COMP_DIFFERS_MAC);
    EXPECT_NE(localMac, INVALID_META_MAC); // a MAC really was computed
}

TEST_F(UploadDedupMacTest, CancelledCollisionCheckReturnsNotYetForCoreAndPublicNodes)
{
    auto coreFa = client->fsaccess->newfileaccess();
    ASSERT_TRUE(coreFa->fopen(mPath, OPEN_RDONLY, FSLogging::logOnError));

    CancelToken coreCancelToken(true);
    EXPECT_EQ(CollisionChecker::check(
                  [fa = coreFa.get()]()
                  {
                      return fa;
                  },
                  mNode.get(),
                  CollisionChecker::Option::Metamac,
                  coreCancelToken),
              CollisionChecker::Result::NotYet);

    const std::string emptyAttrs;
    const std::string nodeKey = mNode->nodekey();
    MegaNodePrivate publicNode("public-file",
                               MegaNode::TYPE_FILE,
                               mLocalFp.size,
                               0,
                               mLocalFp.mtime,
                               2,
                               &nodeKey,
                               &emptyAttrs,
                               nullptr,
                               nullptr,
                               INVALID_HANDLE);

    auto publicFa = client->fsaccess->newfileaccess();
    ASSERT_TRUE(publicFa->fopen(mPath, OPEN_RDONLY, FSLogging::logOnError));

    CancelToken publicCancelToken(true);
    EXPECT_EQ(CollisionChecker::check(
                  [fa = publicFa.get()]()
                  {
                      return fa;
                  },
                  &publicNode,
                  CollisionChecker::Option::Metamac,
                  publicCancelToken),
              CollisionChecker::Result::NotYet);
}

TEST(MacComparison, CollisionCheckDoesNotMisclassifyReadErrorAsCancellation)
{
    std::vector<byte> content(256 * 1024, 0x42);
    MockFileAccessForMac fa(content, 50 * 1024, 42);
    ASSERT_TRUE(fa.openf(FSLogging::logOnError));

    CancelToken cancelToken(false);
    fa.setOnReadFailure(
        [&cancelToken]()
        {
            cancelToken.cancel();
        });

    const std::string nodeKey = createTestNodeKey(0x1234567890ABCDEF, 0x3333333333333333);
    const std::string emptyAttrs;
    MegaNodePrivate publicNode("public-file",
                               MegaNode::TYPE_FILE,
                               static_cast<int64_t>(content.size()),
                               0,
                               0,
                               3,
                               &nodeKey,
                               &emptyAttrs,
                               nullptr,
                               nullptr,
                               INVALID_HANDLE);

    EXPECT_EQ(CollisionChecker::check(
                  [&fa]()
                  {
                      return &fa;
                  },
                  &publicNode,
                  CollisionChecker::Option::Metamac,
                  cancelToken),
              CollisionChecker::Result::Download);
}

TEST(MacComparison, CollisionCheckMapsEqualAndNotEqualResults)
{
    std::vector<byte> content(128 * 1024, 0x42);
    const int64_t iv = 0x1234567890ABCDEF;
    const std::string differentKey = createTestNodeKey(iv, 0x3333333333333333);

    MockFileAccessForMac faForMac(content);
    ASSERT_TRUE(faForMac.openf(FSLogging::logOnError));
    const auto comparison = CompareLocalFileMetaMacWithNodeKey(&faForMac,
                                                               differentKey,
                                                               FILENODE,
                                                               "test.txt",
                                                               CancelToken());
    ASSERT_EQ(comparison.errorCode, 0);

    const std::string equalKey =
        createTestNodeKeyWithSameCipherKey(differentKey, iv, comparison.localMac);
    const std::string emptyAttrs;
    MegaNodePrivate equalNode("equal",
                              MegaNode::TYPE_FILE,
                              static_cast<int64_t>(content.size()),
                              0,
                              0,
                              4,
                              &equalKey,
                              &emptyAttrs,
                              nullptr,
                              nullptr,
                              INVALID_HANDLE);
    MegaNodePrivate differentNode("different",
                                  MegaNode::TYPE_FILE,
                                  static_cast<int64_t>(content.size()),
                                  0,
                                  0,
                                  5,
                                  &differentKey,
                                  &emptyAttrs,
                                  nullptr,
                                  nullptr,
                                  INVALID_HANDLE);

    ASSERT_NE(equalNode.getNodeKey(), nullptr);
    ASSERT_EQ(*equalNode.getNodeKey(), equalKey);

    MockFileAccessForMac directFa(content);
    ASSERT_TRUE(directFa.openf(FSLogging::logOnError));
    const auto directComparison = CompareLocalFileMetaMacWithNodeKey(&directFa,
                                                                     *equalNode.getNodeKey(),
                                                                     equalNode.getType(),
                                                                     "equal",
                                                                     CancelToken());
    ASSERT_EQ(directComparison.errorCode, 0);
    ASSERT_TRUE(directComparison.areEqualMacs);

    MockFileAccessForMac equalFa(content);
    ASSERT_TRUE(equalFa.openf(FSLogging::logOnError));
    EXPECT_EQ(CollisionChecker::check(
                  [&equalFa]()
                  {
                      return &equalFa;
                  },
                  &equalNode,
                  CollisionChecker::Option::Metamac,
                  CancelToken(false)),
              CollisionChecker::Result::Skip);

    MockFileAccessForMac differentFa(content);
    ASSERT_TRUE(differentFa.openf(FSLogging::logOnError));
    EXPECT_EQ(CollisionChecker::check(
                  [&differentFa]()
                  {
                      return &differentFa;
                  },
                  &differentNode,
                  CollisionChecker::Option::Metamac,
                  CancelToken(false)),
              CollisionChecker::Result::Download);
}

TEST(MacComparison, NodeComparisonResultToStrCoversCancelled)
{
    EXPECT_EQ(nodeComparisonResultToStr(NODE_COMP_CANCELLED), "NODE_COMP_CANCELLED");
}
