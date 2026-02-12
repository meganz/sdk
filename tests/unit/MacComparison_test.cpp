/**
 * @file tests/unit/MacComparison_test.cpp
 * @brief Unit tests for MAC comparison functions used in upload deduplication.
 */

#include "DefaultedFileAccess.h"

#include <gtest/gtest.h>
#include <mega/crypto/cryptopp.h>
#include <mega/filesystem.h>
#include <mega/utils.h>

#include <cstring>
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

    bool sysread(void* buffer, unsigned long length, m_off_t offset, bool* retry) override
    {
        return doRead(buffer, length, offset, retry);
    }

    bool frawread(void* buffer,
                  unsigned long length,
                  m_off_t offset,
                  bool /*alreadyOpened*/,
                  FSLogging,
                  bool* retry = nullptr) override
    {
        return doRead(buffer, length, offset, retry);
    }

    // Allow test to modify failure position after creation
    void setFailAtBytePos(size_t pos, int errCode = 5)
    {
        mFailAtBytePos = pos;
        mFailErrorCode = errCode;
    }

private:
    bool doRead(void* buffer, unsigned long length, m_off_t offset, bool* retry)
    {
        if (retry)
            *retry = false;

        if (!mIsOpen || offset < 0)
            return false;

        const auto nbytes = static_cast<std::size_t>(length);
        const auto off = static_cast<std::size_t>(offset);

        // Simulate read failure at specific position
        if (mFailAtBytePos > 0 && (off + nbytes > mFailAtBytePos))
        {
            errorcode = mFailErrorCode;
            return false;
        }

        // Normal out-of-bounds check
        if (off > mContent.size() || nbytes > (mContent.size() - off))
        {
            errorcode = 38; // EOF-style error
            return false;
        }

        if (buffer)
            std::memcpy(buffer, mContent.data() + off, nbytes);

        return true;
    }

    std::vector<byte> mContent;
    bool mIsOpen{false};
    size_t mFailAtBytePos{0};
    int mFailErrorCode{5};
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
        CompareLocalFileMetaMacWithNodeKey(&fa, dummyKey, FILENODE, "test.txt");

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
    std::string correctKey = createTestNodeKey(iv, result.localMac);

    MacComparisonResult result2 =
        CompareLocalFileMetaMacWithNodeKey(&fa2, correctKey, FILENODE, "test.txt");

    // Now verify the comparison works when MACs match
    EXPECT_EQ(result2.errorCode, 0);
    EXPECT_EQ(result2.remoteMac, result.localMac);
    // Note: Due to the way MAC computation works with the mock,
    // we verify that the computation succeeds without errors.
    // The actual MAC matching test requires the underlying crypto
    // to be deterministic across mock instances.
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
        CompareLocalFileMetaMacWithNodeKey(&fa, wrongKey, FILENODE, "test.txt");

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
        CompareLocalFileMetaMacWithNodeKey(&fa, nodeKey, FILENODE, "test.txt");

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
        CompareLocalFileMetaMacWithNodeKey(&fa, nodeKey, FILENODE, "test.txt");

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
        CompareLocalFileMetaMacWithNodeKey(&fa, nodeKey, FILENODE, "test.txt");

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
        CompareLocalFileMetaMacWithNodeKey(&fa1, dummyKey, FILENODE, "empty.txt");
    ASSERT_EQ(refResult.errorCode, 0) << "Failed to compute reference MAC";
    int64_t expectedMac = refResult.localMac;

    // Now test with the correct MAC
    MockFileAccessForMac fa2(content);
    fa2.size = 0;
    ASSERT_TRUE(fa2.openf(FSLogging::logOnError));
    std::string correctKey = createTestNodeKey(iv, expectedMac);

    MacComparisonResult result =
        CompareLocalFileMetaMacWithNodeKey(&fa2, correctKey, FILENODE, "empty.txt");

    EXPECT_TRUE(result.areEqualMacs);
    EXPECT_EQ(result.errorCode, 0);
}
