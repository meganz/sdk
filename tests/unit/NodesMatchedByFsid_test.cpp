/**
 * @file NodesMatchedByFsid_test.cpp
 * @brief Tests for the methods used to search for nodes matched by fsid.
 */

#ifdef ENABLE_SYNC

#include <gtest/gtest.h>
#include <mega/syncinternals/syncinternals.h>

using namespace mega;

namespace
{

/**
 * @brief A default value for the user owner handle.
 */
constexpr handle COMMON_USER_OWNER = 1;
/**
 * @brief A default value for the isFsidReused flag.
 */
constexpr bool FSID_REUSED = false;
/**
 * @brief A default SourceNodeMatchByFSIDContext struct.
 */
constexpr SourceNodeMatchByFSIDContext BASIC_SOURCE_CONTEXT{FSID_REUSED,
                                                            ExclusionState::ES_INCLUDED};
/**
 * @brief A default mtime.
 */
constexpr m_time_t SIMPLE_MTIME = 1;
/**
 * @brief A default size.
 */
constexpr m_off_t SIMPLE_SIZE = 10;

/**
 * @brief Generates a light FileFingerprint (mtime and size).
 *
 * This light FileFingerprint is enough for comparison purposes,
 * the CRC needs real data to be calculated, and we are not
 * testing FileFingerprint fields here.
 *
 * @param mtime The modification time of the file.
 * @param size The size of the file.
 * @return A valid FileFingerprint with the mtime and size. CRC would be empty.
 */
FileFingerprint genLightFingerprint(const m_time_t mtime = SIMPLE_MTIME,
                                    const m_off_t size = SIMPLE_SIZE)
{
    FileFingerprint lightFp{};
    lightFp.mtime = mtime;
    lightFp.size = size;
    lightFp.isvalid = true;
    return lightFp;
}

/**
 * @brief Generates a NodeMatchByFSIDAttributes structure.
 *
 * @param nodeType The type of the node (FILENODE or FOLDERNODE).
 * @param filesystemFingerprint The fingerprint of the filesystem. It needs an int for the
 * fingerprint and a string for the UUID.
 * @param userHandle The handle of the user owner.
 * @param fileFingerprint The fingerprint of the file.
 * @return A NodeMatchByFSIDAttributes struct with the assigned fields.
 */
NodeMatchByFSIDAttributes
    genMatchAttributes(const nodetype_t nodeType = FILENODE,
                       const fsfp_t& filesystemFingerprint = {1, "UUID"},
                       const handle userHandle = COMMON_USER_OWNER,
                       const FileFingerprint& fileFingerprint = genLightFingerprint())
{
    return NodeMatchByFSIDAttributes{nodeType, filesystemFingerprint, userHandle, fileFingerprint};
}

} // namespace

/**
 * @brief Tests a match: both nodes are equivalent.
 */
TEST(NodesMatchedByFSIDTest, NodesAreEquivalent)
{
    const auto sourceNodeAttributes{genMatchAttributes()};
    const auto targetNodeAttributes{sourceNodeAttributes};

    ASSERT_EQ(areNodesMatchedByFsidEquivalent(sourceNodeAttributes,
                                              targetNodeAttributes,
                                              BASIC_SOURCE_CONTEXT),
              NodeMatchByFSIDResult::Matched);
}

/**
 * @brief Tests mismatch due to FSID reused by the source node.
 */
TEST(NodesMatchedByFSIDTest, SourceNodeFsidReused)
{
    const auto sourceNodeAttributes{genMatchAttributes()};
    const auto targetNodeAttributes{sourceNodeAttributes};

    constexpr bool fsidIsReused = true;
    constexpr SourceNodeMatchByFSIDContext context{fsidIsReused, ExclusionState::ES_INCLUDED};

    ASSERT_EQ(areNodesMatchedByFsidEquivalent(sourceNodeAttributes, targetNodeAttributes, context),
              NodeMatchByFSIDResult::SourceFsidReused);
}

/**
 * @brief Test mismatch due to different filesystem fingerprints.
 */
TEST(NodesMatchedByFSIDTest, DifferentFilesystemsFingerprints)
{
    const fsfp_t fsfp1{1, "UUID"};
    const fsfp_t fsfp2{2, "UUID2"};
    const auto sourceNodeAttributes{genMatchAttributes(FILENODE, fsfp1)};
    const auto targetNodeAttributes{genMatchAttributes(FILENODE, fsfp2)};

    ASSERT_EQ(areNodesMatchedByFsidEquivalent(sourceNodeAttributes,
                                              targetNodeAttributes,
                                              BASIC_SOURCE_CONTEXT),
              NodeMatchByFSIDResult::DifferentFilesystems);
}

/**
 * @brief Tests mismatch due to different node types.
 */
TEST(NodesMatchedByFSIDTest, DifferentNodeTypes)
{
    const auto sourceNodeAttributes{genMatchAttributes(FILENODE)};
    const auto targetNodeAttributes{genMatchAttributes(FOLDERNODE)};

    ASSERT_EQ(areNodesMatchedByFsidEquivalent(sourceNodeAttributes,
                                              targetNodeAttributes,
                                              BASIC_SOURCE_CONTEXT),
              NodeMatchByFSIDResult::DifferentTypes);
}

/**
 * @brief Tests mismatch due to different owners.
 */
TEST(NodesMatchedByFSIDTest, DifferentOwners)
{
    constexpr handle sourceOwner = 1;
    constexpr handle targetOwner = 2;

    const fsfp_t fsfp1{1, "UUID"};
    const auto sourceNodeAttributes{genMatchAttributes(FILENODE, fsfp1, sourceOwner)};
    const auto targetNodeAttributes{genMatchAttributes(FILENODE, fsfp1, targetOwner)};

    ASSERT_EQ(areNodesMatchedByFsidEquivalent(sourceNodeAttributes,
                                              targetNodeAttributes,
                                              BASIC_SOURCE_CONTEXT),
              NodeMatchByFSIDResult::DifferentOwners);
}

/**
 * @brief Tests mismatch due to exclusion unknown.
 */
TEST(NodesMatchedByFSIDTest, SourceNodeExclusionStateIsUnknown)
{
    const auto sourceNodeAttributes{genMatchAttributes()};
    const auto targetNodeAttributes{sourceNodeAttributes};

    constexpr SourceNodeMatchByFSIDContext context{false, ExclusionState::ES_UNKNOWN};

    ASSERT_EQ(areNodesMatchedByFsidEquivalent(sourceNodeAttributes, targetNodeAttributes, context),
              NodeMatchByFSIDResult::SourceExclusionUnknown);
}

/**
 * @brief Tests mismatch due to node exclusion.
 */
TEST(NodesMatchedByFSIDTest, SourceNodeIsExcluded)
{
    const auto sourceNodeAttributes{genMatchAttributes()};
    const auto targetNodeAttributes{sourceNodeAttributes};

    constexpr SourceNodeMatchByFSIDContext context{false, ExclusionState::ES_EXCLUDED};

    ASSERT_EQ(areNodesMatchedByFsidEquivalent(sourceNodeAttributes, targetNodeAttributes, context),
              NodeMatchByFSIDResult::SourceIsExcluded);
}

/**
 * @brief Test mismatch due to different fingerprint due to mtime.
 */
TEST(NodesMatchedByFSIDTest, DifferentFingerprintDueToMtime)
{
    const auto sourceFp = genLightFingerprint();
    const auto targetFp = genLightFingerprint(SIMPLE_MTIME + 30, SIMPLE_SIZE);

    const fsfp_t fsfp1{1, "UUID"};
    const auto sourceNodeAttributes{
        genMatchAttributes(FILENODE, fsfp1, COMMON_USER_OWNER, sourceFp)};
    const auto targetNodeAttributes{
        genMatchAttributes(FILENODE, fsfp1, COMMON_USER_OWNER, targetFp)};

    ASSERT_EQ(areNodesMatchedByFsidEquivalent(sourceNodeAttributes,
                                              targetNodeAttributes,
                                              BASIC_SOURCE_CONTEXT),
              NodeMatchByFSIDResult::DifferentFingerprint);
}

/**
 * @brief Test mismatch due to different fingerprint due to size.
 */
TEST(NodesMatchedByFSIDTest, DifferentFingerprintDueToSize)
{
    const auto sourceFp = genLightFingerprint();
    const auto targetFp = genLightFingerprint(SIMPLE_MTIME, SIMPLE_SIZE + 1);

    const fsfp_t fsfp1{1, "UUID"};
    const auto sourceNodeAttributes{
        genMatchAttributes(FILENODE, fsfp1, COMMON_USER_OWNER, sourceFp)};
    const auto targetNodeAttributes{
        genMatchAttributes(FILENODE, fsfp1, COMMON_USER_OWNER, targetFp)};

    ASSERT_EQ(areNodesMatchedByFsidEquivalent(sourceNodeAttributes,
                                              targetNodeAttributes,
                                              BASIC_SOURCE_CONTEXT),
              NodeMatchByFSIDResult::DifferentFingerprint);
}

#endif // ENABLE_SYNC