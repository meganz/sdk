#include <gtest/gtest.h>
#include "mega/filefingerprint.h"

using namespace mega;

namespace upload_verification {

bool shouldAttemptDeduplication(bool allowDuplicateVersions, 
                               bool localFpValid, 
                               bool nodeValid)
{
    if (allowDuplicateVersions || !localFpValid || !nodeValid) {
        return false;
    }
    return true;
}

bool fingerprintsMatch(const FileFingerprint& localFp, const FileFingerprint& nodeFp)
{
    return localFp == nodeFp;
}

bool isValidNodeKey(const std::string& nodekey)
{
    return nodekey.size() >= FILENODEKEYLENGTH;
}

bool canDeduplicateFile(const FileFingerprint& localFp, 
                       const FileFingerprint& nodeFp,
                       const std::string& nodekey,
                       bool allowDuplicateVersions,
                       bool nodeValid)
{
    if (!shouldAttemptDeduplication(allowDuplicateVersions, localFp.isvalid, nodeValid)) {
        return false;
    }
    
    if (!fingerprintsMatch(localFp, nodeFp)) {
        return false;
    }
    
    if (!isValidNodeKey(nodekey)) {
        return false;
    }
    
    return true;
}

} // namespace upload_verification

class FileUploadVerification_test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        validFingerprint.size = 1024;
        validFingerprint.mtime = 1234567890;
        validFingerprint.isvalid = true;
        for (size_t i = 0; i < validFingerprint.crc.size(); ++i) {
            validFingerprint.crc[i] = static_cast<uint32_t>(i + 1);
        }
        
        invalidFingerprint.size = 0;
        invalidFingerprint.mtime = 0;
        invalidFingerprint.isvalid = false;
        
        differentFingerprint = validFingerprint;
        differentFingerprint.size = 2048;
        
        validNodeKey = std::string(FILENODEKEYLENGTH, 'k');
        shortNodeKey = "short";
    }

    FileFingerprint validFingerprint;
    FileFingerprint invalidFingerprint;
    FileFingerprint differentFingerprint;
    std::string validNodeKey;
    std::string shortNodeKey;
};

TEST_F(FileUploadVerification_test, ShouldAttemptDeduplication_AllowDuplicateVersions_ReturnsFalse)
{
    EXPECT_FALSE(upload_verification::shouldAttemptDeduplication(true, true, true));
}

TEST_F(FileUploadVerification_test, ShouldAttemptDeduplication_InvalidLocalFingerprint_ReturnsFalse)
{
    EXPECT_FALSE(upload_verification::shouldAttemptDeduplication(false, false, true));
}

TEST_F(FileUploadVerification_test, ShouldAttemptDeduplication_InvalidNode_ReturnsFalse)
{
    EXPECT_FALSE(upload_verification::shouldAttemptDeduplication(false, true, false));
}

TEST_F(FileUploadVerification_test, ShouldAttemptDeduplication_AllValid_ReturnsTrue)
{
    EXPECT_TRUE(upload_verification::shouldAttemptDeduplication(false, true, true));
}

TEST_F(FileUploadVerification_test, FingerprintsMatch_IdenticalFingerprints_ReturnsTrue)
{
    FileFingerprint fp1 = validFingerprint;
    FileFingerprint fp2 = validFingerprint;

    EXPECT_TRUE(upload_verification::fingerprintsMatch(fp1, fp2));
}

TEST_F(FileUploadVerification_test, FingerprintsMatch_DifferentFingerprints_ReturnsFalse)
{
    EXPECT_FALSE(upload_verification::fingerprintsMatch(validFingerprint, differentFingerprint));
}

TEST_F(FileUploadVerification_test, FingerprintsMatch_ValidVsInvalid_ReturnsFalse)
{
    EXPECT_FALSE(upload_verification::fingerprintsMatch(validFingerprint, invalidFingerprint));
}

TEST_F(FileUploadVerification_test, IsValidNodeKey_ValidKey_ReturnsTrue)
{
    EXPECT_TRUE(upload_verification::isValidNodeKey(validNodeKey));
}

TEST_F(FileUploadVerification_test, IsValidNodeKey_ShortKey_ReturnsFalse)
{
    EXPECT_FALSE(upload_verification::isValidNodeKey(shortNodeKey));
}

TEST_F(FileUploadVerification_test, IsValidNodeKey_EmptyKey_ReturnsFalse)
{
    EXPECT_FALSE(upload_verification::isValidNodeKey(""));
}

TEST_F(FileUploadVerification_test, CanDeduplicateFile_AllConditionsMet_ReturnsTrue)
{
    EXPECT_TRUE(upload_verification::canDeduplicateFile(
        validFingerprint, validFingerprint, validNodeKey, false, true));
}

TEST_F(FileUploadVerification_test, CanDeduplicateFile_AllowDuplicateVersions_ReturnsFalse)
{
    EXPECT_FALSE(upload_verification::canDeduplicateFile(
        validFingerprint, validFingerprint, validNodeKey, true, true));
}

TEST_F(FileUploadVerification_test, CanDeduplicateFile_InvalidLocalFingerprint_ReturnsFalse)
{
    EXPECT_FALSE(upload_verification::canDeduplicateFile(
        invalidFingerprint, validFingerprint, validNodeKey, false, true));
}

TEST_F(FileUploadVerification_test, CanDeduplicateFile_InvalidNode_ReturnsFalse)
{
    EXPECT_FALSE(upload_verification::canDeduplicateFile(
        validFingerprint, validFingerprint, validNodeKey, false, false));
}

TEST_F(FileUploadVerification_test, CanDeduplicateFile_DifferentFingerprints_ReturnsFalse)
{
    EXPECT_FALSE(upload_verification::canDeduplicateFile(
        validFingerprint, differentFingerprint, validNodeKey, false, true));
}

TEST_F(FileUploadVerification_test, CanDeduplicateFile_InvalidNodeKey_ReturnsFalse)
{
    EXPECT_FALSE(upload_verification::canDeduplicateFile(
        validFingerprint, validFingerprint, shortNodeKey, false, true));
}

TEST_F(FileUploadVerification_test, FileFingerprint_EqualityOperator_WorksCorrectly)
{
    FileFingerprint fp1 = validFingerprint;
    FileFingerprint fp2 = validFingerprint;
    EXPECT_TRUE(fp1 == fp2);
    
    fp2.size = fp1.size + 1;
    EXPECT_FALSE(fp1 == fp2);
    
    fp2 = fp1;
    fp2.mtime = fp1.mtime + 3;
    EXPECT_FALSE(fp1 == fp2);
    
    fp2 = fp1;
    fp2.isvalid = !fp1.isvalid;
    EXPECT_FALSE(fp1 == fp2);
    
    fp2 = fp1;
    fp2.crc[0] = fp1.crc[0] + 1;
    EXPECT_FALSE(fp1 == fp2);
}

TEST_F(FileUploadVerification_test, DeduplicationWorkflow_TypicalScenarios)
{
    EXPECT_TRUE(upload_verification::canDeduplicateFile(
        validFingerprint, validFingerprint, validNodeKey, false, true));
    
    FileFingerprint differentSize = validFingerprint;
    differentSize.size = validFingerprint.size + 100;
    EXPECT_FALSE(upload_verification::canDeduplicateFile(
        validFingerprint, differentSize, validNodeKey, false, true));
    
    FileFingerprint differentMtime = validFingerprint;
    differentMtime.mtime = validFingerprint.mtime + 3600;
    EXPECT_FALSE(upload_verification::canDeduplicateFile(
        validFingerprint, differentMtime, validNodeKey, false, true));
    
    EXPECT_FALSE(upload_verification::canDeduplicateFile(
        validFingerprint, validFingerprint, validNodeKey, true, true));
}
