/**
 * @file tests/SdkTestGetRecentActions_test.cpp
 * @brief Integration tests for getRecentActionsAsync and getRecentActionById.
 */

#include "mega/utils.h"
#include "megautils.h"
#include "sdk_test_utils.h"
#include "SdkTest_test.h"

#include <algorithm>
#include <string>
#include <vector>

using namespace std;

namespace
{
// Build a recent-action id matching the server/client token format:
// dayStart|windowStart|windowEnd|user|parent|media|update|excludeSensitives
std::string buildRecentActionId(const std::string& dayStart,
                                const std::string& windowStart,
                                const std::string& windowEnd,
                                const std::string& userHandle,
                                const std::string& parentHandle,
                                const std::string& media,
                                const std::string& update,
                                const std::string& excludeSensitives)
{
    return dayStart + "|" + windowStart + "|" + windowEnd + "|" + userHandle + "|" + parentHandle +
           "|" + media + "|" + update + "|" + excludeSensitives;
}

std::string replaceToken(const std::string& id, size_t tokenIndex, const std::string& value)
{
    auto tokens = splitString<std::vector<std::string>>(id, '|');
    if (tokenIndex >= tokens.size())
    {
        return id;
    }

    tokens[tokenIndex] = value;
    std::string out;
    for (size_t i = 0; i < tokens.size(); ++i)
    {
        if (i)
        {
            out.push_back('|');
        }
        out.append(tokens[i]);
    }
    return out;
}

// Find the bucket that contains a given filename.
// Returns nullptr if not found.
const MegaRecentActionBucket* getBucketContainingFilename(const MegaRecentActionBucketList& buckets,
                                                          const std::string& filename)
{
    const auto bucketsAsVec = bucketsToVector(buckets);
    for (size_t i = 0; i < bucketsAsVec.size(); ++i)
    {
        const auto& bucketFiles = bucketsAsVec[i];
        if (std::find(bucketFiles.begin(), bucketFiles.end(), filename) != bucketFiles.end())
        {
            return buckets.get(static_cast<int>(i));
        }
    }
    return nullptr;
}

// Return the id of the bucket that contains a given filename.
// Returns an empty string if not found.
std::string getRecentActionIdContainingFilename(const MegaRecentActionBucketList& buckets,
                                                const std::string& filename)
{
    const MegaRecentActionBucket* bucket = getBucketContainingFilename(buckets, filename);
    return (bucket && bucket->getId()) ? bucket->getId() : std::string{};
}

// Fetch recent action buckets. Records a failure and returns nullptr on error.
std::unique_ptr<MegaRecentActionBucketList> fetchRecentBuckets(MegaApi* api,
                                                               unsigned maxdays,
                                                               unsigned maxcount,
                                                               bool excludeSensitives = true)
{
    RequestTracker tracker(api);
    api->getRecentActionsAsync(maxdays, maxcount, excludeSensitives, &tracker);
    if (tracker.waitForResult() != API_OK || !tracker.request ||
        !tracker.request->getRecentActions())
    {
        ADD_FAILURE() << "getRecentActionsAsync failed";
        return nullptr;
    }
    return std::unique_ptr<MegaRecentActionBucketList>{tracker.request->getRecentActions()->copy()};
}

// Call getRecentActionById and assert API_OK with exactly one bucket returned.
// Records a failure and returns nullptr on any mismatch.
std::unique_ptr<MegaRecentActionBucketList> queryByIdAssertOk(MegaApi* api, const std::string& id)
{
    RequestTracker tracker(api);
    api->getRecentActionById(id.c_str(), &tracker);
    const int rc = tracker.waitForResult();
    if (rc != API_OK)
    {
        ADD_FAILURE() << "getRecentActionById(" << id << ") returned " << rc << ", expected API_OK";
        return nullptr;
    }
    if (!tracker.request || !tracker.request->getRecentActions() ||
        tracker.request->getRecentActions()->size() != 1)
    {
        ADD_FAILURE() << "getRecentActionById did not return exactly 1 bucket";
        return nullptr;
    }
    return std::unique_ptr<MegaRecentActionBucketList>{tracker.request->getRecentActions()->copy()};
}

}

/**
 * Integration test for getRecentActionsAsync and clearRecentActionHistory.
 *
 * Scenario setup:
 *   - Two sessions for the same account are opened (megaApi[0] and megaApi[1]).
 *   - Four distinct files are uploaded in sequence with 1-second gaps to
 *     guarantee deterministic bucket ordering:
 *       file1.txt (empty)  →  file1.txt.bkp1  →  file1.txt.bkp2
 *       →  file1.txt (updated)  →  file2.txt  →  file2.txt (updated)
 *   - file1.txt is marked sensitive after its second upload.
 *
 * Sections covered:
 *   1. Input validation  – days=0 and maxnodes=0 must return API_EARGS.
 *   2. Full listing      – all buckets returned without sensitive exclusion;
 *                          verified from both sessions of the same account.
 *   3. maxnodes cap      – only the most-recent bucket is returned when the
 *                          node limit is reached before the second bucket.
 *   4. Sensitive filter  – file1.txt (sensitive) is absent when
 *                          excludeSensitives=true; rest of the bucket shape
 *                          is unchanged. Verified via both the parameterised
 *                          and the simple (default-exclusion) overloads.
 *   5. maxnodes + filter – combined: only the first filtered bucket returned.
 *   6. clearRecentActionHistory happy path – actions older than 'now' are
 *                          hidden; the clear timestamp is persisted as a user
 *                          attribute and propagated to the second session via
 *                          an SC action packet followed by an automatic fetch.
 *   7. Post-clear listing – a new upload made after the clear appears in
 *                          results while the earlier uploads remain hidden.
 *   8. Invalid clear timestamps – values 0 and -1 are rejected (API_EARGS)
 *                          and must not overwrite the stored timestamp.
 *   9. Re-login session  – a freshly logged-in session (megaApi[2]) picks up
 *                          the persisted clear timestamp via getuserdata and
 *                          returns the correct post-clear result set.
 */
TEST_F(SdkTest, SdkRecentActionsTest)
{
    LOG_info << "___TEST SdkGetRecentActionsTest___";
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    // Open a second session with the same account credentials so we can later
    // verify that clearRecentActionHistory propagates across sessions.
    loginSameAccountsForTest(0);

    const auto updloadFile =
        [this, rootnode = std::unique_ptr<MegaNode>(megaApi[0]->getRootNode())](
            const std::string& fname,
            const std::string_view contents)
    {
        sdk_test::LocalTempFile f(fname, contents);
        auto node = sdk_test::uploadFile(megaApi[0].get(), fname, rootnode.get());
        ASSERT_TRUE(node) << "Cannot upload " << fname;
    };

    // --- Section 1: Build the file history used throughout the test ---
    // The expected bucket layout after all uploads (newest bucket first):
    //   bucket 0: {file2.txt, file1.txt}           (updates, share a time window)
    //   bucket 1: {file1.txt.bkp2, file1.txt.bkp1} (original uploads)
    // file1.txt is marked sensitive, which exercises the exclusion filter.
    const std::string filename1 = "file1.txt";
    const std::string filename1bkp1 = filename1 + ".bkp1";
    const std::string filename1bkp2 = filename1 + ".bkp2";
    const std::string filename2 = "file2.txt";
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

    // Re-uploading filename1 with different content creates a new version and
    // moves it into a newer time window together with the subsequent file2.txt.
    LOG_debug << "# SdkRecentsTest: updating file " << filename1;
    updloadFile(filename1, "update");
    WaitMillisec(1000);

    // Mark file1.txt as sensitive so it can be hidden by the exclusion filter.
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

    // Ensure the second session (megaApi[1]) is fully synced before querying.
    synchronousCatchup(1, maxTimeout);

    const auto getRecentActionBuckets = [this](unsigned int index,
                                               unsigned days,
                                               unsigned maxnodes,
                                               bool optExcludeSensitives,
                                               ::mega::ErrorCodes expectedCode,
                                               vector<string_vector>& expectedVec,
                                               bool simple = false)
    {
        RequestTracker tracker(megaApi[index].get());
        simple ?
            megaApi[index]->getRecentActionsAsync(days, maxnodes, &tracker) :
            megaApi[index]->getRecentActionsAsync(days, maxnodes, optExcludeSensitives, &tracker);

        ASSERT_EQ(tracker.waitForResult(), expectedCode);
        if (expectedCode != API_OK)
        {
            return;
        }
        std::unique_ptr<MegaRecentActionBucketList> buckets{
            tracker.request->getRecentActions()->copy()};

        ASSERT_TRUE(buckets != nullptr);
        EXPECT_TRUE(bucketsToVector(*buckets) == expectedVec);
    };

    // --- Section 2: Input validation ---
    // days=0 is not a valid lookback window; both overloads must reject it.
    vector<string_vector> expectedEmpty = {};
    LOG_debug << "# SdkRecentsTest: Get all recent actions with invalid days=0";
    getRecentActionBuckets(0, 0, 10, false, API_EARGS, expectedEmpty);
    getRecentActionBuckets(0, 0, 10, false, API_EARGS, expectedEmpty, true);

    // maxnodes=0 would return no nodes, which is meaningless; both overloads must reject it.
    LOG_debug << "# SdkRecentsTest: Get all recent actions with invalid maxnodes=0";
    getRecentActionBuckets(0, 1, 0, false, API_EARGS, expectedEmpty);
    getRecentActionBuckets(0, 1, 0, false, API_EARGS, expectedEmpty, true);

    // --- Section 3: Full listing without sensitive exclusion ---
    // Both sessions of the same account must return the identical bucket layout.
    LOG_debug << "# SdkRecentsTest: Get all recent actions (no exclusion)";
    vector<string_vector> expectedInclude = {{filename2, filename1},
                                             {filename1bkp2, filename1bkp1}};
    getRecentActionBuckets(0, 1, 10, false, API_OK, expectedInclude);
    getRecentActionBuckets(1, 1, 10, false, API_OK, expectedInclude);

    // --- Section 4: maxnodes cap (no exclusion) ---
    // With maxnodes=2 the result is truncated: only the most-recent two nodes
    // (both in bucket 0) are returned, so bucket 1 disappears entirely.
    LOG_debug << "# SdkRecentsTest: Get 2 recent actions (no exclusion)";
    vector<string_vector> expectedIncludeMax2 = {{filename2, filename1}};
    getRecentActionBuckets(0, 1, 2, false, API_OK, expectedIncludeMax2);

    // --- Section 5: Sensitive exclusion ---
    // file1.txt is sensitive, so the first bucket loses it and becomes a
    // single-node bucket {file2.txt}.  The second bucket is unaffected.
    // The simple overload uses excludeSensitives=true by default.
    LOG_debug << "# SdkRecentsTest: Get recent actions excluding sensitive nodes";
    vector<string_vector> expectedExclude = {{filename2}, {filename1bkp2, filename1bkp1}};
    getRecentActionBuckets(0, 1, 10, true, API_OK, expectedExclude);
    getRecentActionBuckets(0, 1, 10, true, API_OK, expectedExclude, true);

    // --- Section 6: maxnodes cap + sensitive exclusion ---
    // maxnodes=1 with exclusion: only {file2.txt} survives; the second bucket
    // is dropped because the node quota is exhausted after the first node.
    LOG_debug << "# SdkRecentsTest: Get 1 recent actions excluding sensitive nodes";
    vector<string_vector> expectedExcludeMax1 = {{filename2}};
    getRecentActionBuckets(0, 1, 1, true, API_OK, expectedExcludeMax1);

    // --- Section 7: clearRecentActionHistory happy path ---
    // Sleep so the recorded 'now' timestamp is strictly greater than the
    // most-recent action timestamp, guaranteeing all prior actions are hidden.
    WaitMillisec(1000);

    const auto setClearRecentsUpTo =
        [this](MegaTimeStamp timestamp, ::mega::ErrorCodes expectedCode)
    {
        // set user attributes to ensure SDK is working properly before clearing recents
        RequestTracker trackerSetAttr(megaApi[0].get());
        megaApi[0]->clearRecentActionHistory(timestamp, &trackerSetAttr);
        ASSERT_EQ(trackerSetAttr.waitForResult(), expectedCode);
        EXPECT_EQ(trackerSetAttr.request->getNumber(), timestamp);
    };

    const auto verifyClearRecentsUpTo = [this](unsigned index, m_time_t timestamp)
    {
        // get user attributes to ensure SDK is working properly after clearing recents
        RequestTracker trackerGetAttr(megaApi[index].get());
        megaApi[index]->getUserAttribute(MegaApi::USER_ATTR_RECENT_CLEAR_TIMESTAMP,
                                         &trackerGetAttr);
        ASSERT_EQ(trackerGetAttr.waitForResult(), API_OK);
        EXPECT_EQ(trackerGetAttr.request->getNumber(), timestamp);
    };

    m_time_t now = m_time();
    // Reset update counters so we can count exactly how many attribute-change
    // notifications each session receives after the clear.
    int& recentClearTimeUpdatedCount0 = mApi[0].recentClearTimeUpdatedCount = 0;
    int& recentClearTimeUpdatedCount1 = mApi[1].recentClearTimeUpdatedCount = 0;

    LOG_debug << "# SdkRecentsTest: Clear recent actions up to now";
    setClearRecentsUpTo(now, API_OK);

    // After clearing, the primary session must see an empty bucket list and the
    // stored attribute must equal the timestamp we just set.
    LOG_debug << "# SdkRecentsTest: Get all recent actions after clear";
    getRecentActionBuckets(0, 1, 10, false, API_OK, expectedEmpty);
    verifyClearRecentsUpTo(0, now);

    // The secondary session should receive exactly two notifications:
    //   1. An SC action packet that propagates the attribute change in real-time.
    //   2. An automatic re-fetch triggered by the SDK once the new value is seen.
    // wait for 2 update, 1 is for SC action packet, 1 is for automatically fetching
    ASSERT_TRUE(WaitFor(
        [&recentClearTimeUpdatedCount1]()
        {
            return recentClearTimeUpdatedCount1 == 2;
        },
        10 * 1000));
    LOG_debug
        << "# SdkRecentsTest: the second account fetched the attribute automatically after clear";
    // The secondary session must now also return an empty bucket list and have
    // the same clear timestamp persisted locally.
    getRecentActionBuckets(1, 1, 10, false, API_OK, expectedEmpty);
    verifyClearRecentsUpTo(1, now);
    // The primary session generates only the SC action-packet notification (no
    // self-triggered re-fetch), so its counter stays at 1.
    // only 1 SC action packet
    EXPECT_EQ(recentClearTimeUpdatedCount0, 1);
    EXPECT_EQ(recentClearTimeUpdatedCount1, 2);

    // --- Section 8: Post-clear listing ---
    // A new upload after the clear must appear in results even though all
    // earlier uploads are hidden by the clear timestamp.
    updloadFile(filename1, "update after clear");

    LOG_debug << "# SdkRecentsTest: Get all recent actions after clear and one new action";
    vector<string_vector> expectedAfterClear = {{filename1}};
    getRecentActionBuckets(0, 1, 10, false, API_OK, expectedAfterClear);

    // --- Section 9: Invalid clear timestamp validation ---
    // Zero and negative timestamps are not valid Unix epoch values; both must
    // be rejected without modifying the stored clear timestamp.
    LOG_debug << "# SdkRecentsTest: Clear recent actions up to invalid value 0";
    setClearRecentsUpTo(0, API_EARGS);
    verifyClearRecentsUpTo(0, now);

    LOG_debug << "# SdkRecentsTest: Clear recent actions up to invalid value -1";
    setClearRecentsUpTo(-1, API_EARGS);
    verifyClearRecentsUpTo(0, now);

    // --- Section 10: Re-login session picks up persisted state ---
    // A freshly logged-in session must obtain the clear timestamp via the
    // initial getuserdata call and therefore return the same post-clear result.
    LOG_debug << "# SdkRecentsTest: Get all recent actions after login and getuserdata";

    loginSameAccountsForTest(0);
    getRecentActionBuckets(2, 1, 10, false, API_OK, expectedAfterClear);
    verifyClearRecentsUpTo(2, now);
}

/**
 * Happy-path validation for getRecentActionById.
 *
 * Uploads one file, retrieves the generated bucket id, verifies that the id
 * encodes the correct metadata, then queries getRecentActionById and confirms
 * the returned bucket matches the original.
 */
TEST_F(SdkTest, SdkRecentActionByIdTest)
{
    LOG_info << "___TEST SdkRecentActionByIdTest___";
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    std::unique_ptr<MegaNode> rootNode(megaApi[0]->getRootNode());
    ASSERT_TRUE(rootNode);

    const std::string filename = "recent_action_by_id.txt";
    {
        sdk_test::LocalTempFile f(filename, "recent action by id");
        ASSERT_TRUE(sdk_test::uploadFile(megaApi[0].get(), filename, rootNode.get()));
    }
    WaitMillisec(1000);

    // Locate the bucket that contains our file.
    auto buckets = fetchRecentBuckets(megaApi[0].get(), 1, 10);
    ASSERT_TRUE(buckets);
    const MegaRecentActionBucket* bucket = getBucketContainingFilename(*buckets, filename);
    ASSERT_NE(bucket, nullptr) << filename << " not found in recent buckets";
    ASSERT_NE(bucket->getId(), nullptr);
    const std::string id{bucket->getId()};

    // Verify the id encodes the expected bucket metadata (owner / parent / flags).
    const auto idTokens = splitString<std::vector<std::string>>(id, '|');
    ASSERT_EQ(idTokens.size(), 8u);
    EXPECT_EQ(idTokens[3],
              std::string(Base64Str<MegaClient::USERHANDLE>(megaApi[0]->getMyUserHandleBinary())));
    EXPECT_EQ(idTokens[4],
              std::string(Base64Str<MegaClient::NODEHANDLE>(bucket->getParentHandle())));
    EXPECT_EQ(idTokens[5], bucket->isMedia() ? "1" : "0");
    EXPECT_EQ(idTokens[6], bucket->isUpdate() ? "1" : "0");
    EXPECT_EQ(idTokens[7], "1"); // excludeSensitives=true by default

    // Round-trip: getRecentActionById must return the exact same bucket.
    auto result = queryByIdAssertOk(megaApi[0].get(), id);
    ASSERT_TRUE(result);
    EXPECT_STREQ(result->get(0)->getId(), id.c_str());
    const auto originalVec = bucketsToVector(*buckets);
    EXPECT_NE(std::find(originalVec.begin(), originalVec.end(), bucketsToVector(*result)[0]),
              originalVec.end());
}

/**
 * Negative/edge-input validation for getRecentActionById.
 *
 * Scenario:
 * - Send malformed ids (bad token count, invalid day/window, invalid handles,
 *   invalid boolean flags) and verify parser-level rejection.
 * - Send well-formed ids that do not match existing data and verify lookup miss.
 *
 * Expected:
 * - Malformed inputs -> API_EARGS
 * - Well-formed but non-existing inputs -> API_ENOENT
 */
TEST_F(SdkTest, SdkRecentActionByIdAbnormalTest)
{
    LOG_info << "___TEST SdkRecentActionByIdAbnormalTest___";
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    std::unique_ptr<MegaNode> rootNode(megaApi[0]->getRootNode());
    ASSERT_TRUE(rootNode);
    const std::string userHandleB64{
        Base64Str<MegaClient::USERHANDLE>(megaApi[0]->getMyUserHandleBinary())};
    const std::string parentHandleB64{Base64Str<MegaClient::NODEHANDLE>(rootNode->getHandle())};
    const std::string otherUserHandleB64{
        Base64Str<MegaClient::USERHANDLE>(static_cast<MegaHandle>(1))};

    const auto expectResult = [this](const char* id, int expectedCode)
    {
        RequestTracker tracker(megaApi[0].get());
        megaApi[0]->getRecentActionById(id, &tracker);
        ASSERT_EQ(tracker.waitForResult(), expectedCode)
            << "Unexpected result for id: " << (id ? id : "<null>");
    };

    const auto expectEargs = [&expectResult](const char* id)
    {
        expectResult(id, API_EARGS);
    };

    const auto expectEnoent = [&expectResult](const char* id)
    {
        expectResult(id, API_ENOENT);
    };

    // Format/parser validation (EARGS)
    expectEargs(nullptr); // null id
    expectEargs(""); // empty id
    expectEargs("bad|id"); // too few tokens
    expectEargs("1|0|6||UNDEF|1|0|0"); // empty user handle token
    expectEargs("1|0|6||||||"); // multiple empty tokens

    expectEargs(buildRecentActionId("abc", "0", "6", userHandleB64, parentHandleB64, "1", "0", "0")
                    .c_str()); // non-numeric dayStart
    expectEargs(buildRecentActionId("0", "0", "6", userHandleB64, parentHandleB64, "1", "0", "0")
                    .c_str()); // dayStart must be > 0
    expectEargs(buildRecentActionId("-1", "0", "6", userHandleB64, parentHandleB64, "1", "0", "0")
                    .c_str()); // negative dayStart
    expectEargs(
        buildRecentActionId("12345678901", "0", "6", userHandleB64, parentHandleB64, "1", "0", "0")
            .c_str()); // dayStart length overflow

    expectEargs(buildRecentActionId("1", "100", "6", userHandleB64, parentHandleB64, "1", "0", "0")
                    .c_str()); // windowStart length > 2
    expectEargs(buildRecentActionId("1", "", "6", userHandleB64, parentHandleB64, "1", "0", "0")
                    .c_str()); // empty windowStart
    expectEargs(buildRecentActionId("1", "-1", "6", userHandleB64, parentHandleB64, "1", "0", "0")
                    .c_str()); // negative windowStart

    expectEargs(buildRecentActionId("1", "0", "100", userHandleB64, parentHandleB64, "1", "0", "0")
                    .c_str()); // windowEnd length > 2
    expectEargs(buildRecentActionId("1", "0", "", userHandleB64, parentHandleB64, "1", "0", "0")
                    .c_str()); // empty windowEnd
    expectEargs(buildRecentActionId("1", "0", "-1", userHandleB64, parentHandleB64, "1", "0", "0")
                    .c_str()); // negative windowEnd
    expectEargs(buildRecentActionId("1", "3", "6", userHandleB64, parentHandleB64, "1", "0", "0")
                    .c_str()); // invalid windowStart
    expectEargs(buildRecentActionId("1", "0", "3", userHandleB64, parentHandleB64, "1", "0", "0")
                    .c_str()); // invalid windowEnd
    expectEargs(buildRecentActionId("1", "0", "25", userHandleB64, parentHandleB64, "1", "0", "0")
                    .c_str()); // windowEnd beyond 24h
    expectEargs(buildRecentActionId("1", "6", "0", userHandleB64, parentHandleB64, "1", "0", "0")
                    .c_str()); // reversed time window
    expectEargs(buildRecentActionId("1", "6", "6", userHandleB64, parentHandleB64, "1", "0", "0")
                    .c_str()); // empty time window (start == end)
    expectEargs(buildRecentActionId("1", "6", "18", userHandleB64, parentHandleB64, "1", "0", "0")
                    .c_str()); // valid time window but not aligned to 6h boundaries (e.g. 0-6,
                               // 6-12, 12-18, 18-24)

    expectEargs(buildRecentActionId("1", "0", "6", "", parentHandleB64, "1", "0", "0")
                    .c_str()); // empty user handle
    expectEargs(buildRecentActionId("1", "0", "6", "UNDEF", parentHandleB64, "1", "0", "0")
                    .c_str()); // user handle cannot be UNDEF
    expectEargs(buildRecentActionId("1", "0", "6", userHandleB64, "", "1", "0", "0")
                    .c_str()); // empty parent handle
    expectEargs(buildRecentActionId("1", "0", "6", userHandleB64, "UNDEF", "1", "0", "0")
                    .c_str()); // parent handle cannot be UNDEF
    expectEargs(buildRecentActionId("1", "0", "6", "@@@@", parentHandleB64, "1", "0", "0")
                    .c_str()); // invalid base64 user handle
    expectEargs(buildRecentActionId("1", "0", "6", userHandleB64, "@@@@", "1", "0", "0")
                    .c_str()); // invalid base64 parent handle
    expectEargs(buildRecentActionId("1", "0", "6", "A", parentHandleB64, "1", "0", "0")
                    .c_str()); // invalid user handle length
    expectEargs(buildRecentActionId("1", "0", "6", userHandleB64, "A", "1", "0", "0")
                    .c_str()); // invalid parent handle length

    expectEargs(buildRecentActionId("1", "0", "6", userHandleB64, parentHandleB64, "yes", "0", "0")
                    .c_str()); // media must be 0/1
    expectEargs(buildRecentActionId("1", "0", "6", userHandleB64, parentHandleB64, "2", "0", "0")
                    .c_str()); // media out of range
    expectEargs(buildRecentActionId("1", "0", "6", userHandleB64, parentHandleB64, "-1", "0", "0")
                    .c_str()); // media negative
    expectEargs(
        buildRecentActionId("1", "0", "6", userHandleB64, parentHandleB64, "1", "maybe", "0")
            .c_str()); // update must be 0/1
    expectEargs(buildRecentActionId("1", "0", "6", userHandleB64, parentHandleB64, "1", "-1", "0")
                    .c_str()); // update negative
    expectEargs(
        buildRecentActionId("1", "0", "6", userHandleB64, parentHandleB64, "1", "0", "maybe")
            .c_str()); // excludeSensitives must be 0/1
    expectEargs(buildRecentActionId("1", "0", "6", userHandleB64, parentHandleB64, "1", "0", "-1")
                    .c_str()); // excludeSensitives negative
    expectEargs(buildRecentActionId("1", "0", "6", userHandleB64, parentHandleB64, "1", "0", "")
                    .c_str()); // empty excludeSensitives
    expectEargs((buildRecentActionId("1", "0", "6", userHandleB64, parentHandleB64, "1", "0", "0") +
                 "|extra")
                    .c_str());

    // Well-formed ids that do not match any existing bucket (ENOENT)
    expectEnoent(buildRecentActionId("1", "0", "6", userHandleB64, parentHandleB64, "1", "0", "0")
                     .c_str()); // valid shape, no matching bucket
    expectEnoent(
        buildRecentActionId("1", "0", "6", otherUserHandleB64, parentHandleB64, "1", "0", "0")
            .c_str()); // non-existing/other owner
}

/**
 * Filter-branch coverage for getRecentActionById.
 *
 * Mutates one id token at a time to force each server-side filter branch and
 * confirms that any mismatch yields API_ENOENT while the original id resolves.
 * Covers: owner, parent, time window, media flag, update flag, and excludeSensitives.
 */
TEST_F(SdkTest, SdkRecentActionByIdFilterMatchTest)
{
    LOG_info << "___TEST SdkRecentActionByIdFilterMatchTest___";
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    std::unique_ptr<MegaNode> rootNode(megaApi[0]->getRootNode());
    ASSERT_TRUE(rootNode);

    // Upload to a sub-folder so the parent handle is distinct from the root.
    const auto folderHandle = createFolder(0, "recent_action_parent", rootNode.get());
    ASSERT_NE(folderHandle, UNDEF);
    std::unique_ptr<MegaNode> parentFolder(megaApi[0]->getNodeByHandle(folderHandle));
    ASSERT_TRUE(parentFolder);

    {
        const std::string name = "recent_action_filter.txt";
        sdk_test::LocalTempFile f(name, "content");
        ASSERT_TRUE(sdk_test::uploadFile(megaApi[0].get(), name, parentFolder.get()));

        WaitMillisec(1000);
        auto buckets = fetchRecentBuckets(megaApi[0].get(), 1, 10);
        ASSERT_TRUE(buckets);
        const std::string id = getRecentActionIdContainingFilename(*buckets, name);
        ASSERT_FALSE(id.empty()) << name << " not found in recent buckets";

        const auto idTokens = splitString<std::vector<std::string>>(id, '|');
        ASSERT_EQ(idTokens.size(), 8u);

        const auto expectCode = [this](const std::string& queryId, int expectedCode)
        {
            RequestTracker tracker(megaApi[0].get());
            megaApi[0]->getRecentActionById(queryId.c_str(), &tracker);
            EXPECT_EQ(tracker.waitForResult(), expectedCode) << "id=" << queryId;
        };

        expectCode(id, API_OK); // baseline

        // token[3]: owner mismatch
        std::string mismachOwner{Base64Str<MegaClient::USERHANDLE>(static_cast<MegaHandle>(1))};
        expectCode(replaceToken(id, 3, mismachOwner), API_ENOENT);

        // token[4]: parent mismatch (switch to root, which differs from the upload folder)
        const std::string rootHandleB64{Base64Str<MegaClient::NODEHANDLE>(rootNode->getHandle())};
        ASSERT_NE(rootHandleB64, idTokens[4]);
        expectCode(replaceToken(id, 4, rootHandleB64), API_ENOENT);

        // token[1]/[2]: time window mismatch (shift to a disjoint 6-hour window)
        const int windowStart = std::stoi(idTokens[1]);
        const int disjointStart = (windowStart + 6) % 24;
        expectCode(replaceToken(replaceToken(id, 1, std::to_string(disjointStart)),
                                2,
                                std::to_string(disjointStart + 6)),
                   API_ENOENT);

        // token[5]: media flag mismatch
        expectCode(replaceToken(id, 5, idTokens[5] == "1" ? "0" : "1"), API_ENOENT);

        // token[6]: update flag mismatch
        expectCode(replaceToken(id, 6, idTokens[6] == "1" ? "0" : "1"), API_ENOENT);
    }

    // token[7]: excludeSensitives mismatch — requires a real sensitive node.
    {
        const std::string sensitiveName = "recent_action_sensitive.txt";
        sdk_test::LocalTempFile sf(sensitiveName, "sensitive");
        ASSERT_TRUE(sdk_test::uploadFile(megaApi[0].get(), sensitiveName, rootNode.get()));
        std::unique_ptr<MegaNode> sensitiveNode(
            megaApi[0]->getNodeByPath(("/" + sensitiveName).c_str()));
        ASSERT_TRUE(sensitiveNode);
        ASSERT_EQ(synchronousSetNodeSensitive(0, sensitiveNode.get(), true), API_OK);
        WaitMillisec(1000);

        // Fetch with excludeSensitives=false so the sensitive file appears.
        auto allBuckets = fetchRecentBuckets(megaApi[0].get(), 1, 10, /*excludeSensitives=*/false);
        ASSERT_TRUE(allBuckets);
        const std::string sensitiveId =
            getRecentActionIdContainingFilename(*allBuckets, sensitiveName);
        ASSERT_FALSE(sensitiveId.empty()) << sensitiveName << " not found in recent buckets";

        RequestTracker trackerOk(megaApi[0].get());
        megaApi[0]->getRecentActionById(sensitiveId.c_str(), &trackerOk);
        EXPECT_EQ(trackerOk.waitForResult(), API_OK); // id with excludeSensitives=0 resolves

        RequestTracker trackerMismatch(megaApi[0].get());
        megaApi[0]->getRecentActionById(replaceToken(sensitiveId, 7, "1").c_str(),
                                        &trackerMismatch);
        EXPECT_EQ(trackerMismatch.waitForResult(), API_ENOENT); // flipped to 1 → no match
    }
}

/**
 * Cross-session and cross-account isolation validation.
 *
 * - Same account, two sessions (megaApi[0] and megaApi[2]): both resolve the id
 *   and return identical bucket payloads.
 * - Different account (megaApi[1]): cannot resolve the id (API_ENOENT).
 * - After session catchup: id remains resolvable for the second session.
 */
TEST_F(SdkTest, SdkRecentActionByIdCrossAccountConsistencyTest)
{
    LOG_info << "___TEST SdkRecentActionByIdCrossAccountConsistencyTest___";
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(2));

    // megaApi[2] = second session logged in with the same credentials as megaApi[0].
    loginSameAccountsForTest(0);

    std::unique_ptr<MegaNode> rootNode(megaApi[0]->getRootNode());
    ASSERT_TRUE(rootNode);

    const std::string filename = "recent_action_cross_account.txt";
    {
        sdk_test::LocalTempFile f(filename, "cross account");
        ASSERT_TRUE(sdk_test::uploadFile(megaApi[0].get(), filename, rootNode.get()));
    }
    WaitMillisec(1000);

    auto buckets = fetchRecentBuckets(megaApi[0].get(), 1, 10);
    ASSERT_TRUE(buckets);
    const std::string id = getRecentActionIdContainingFilename(*buckets, filename);
    ASSERT_FALSE(id.empty()) << filename << " not found in recent buckets";

    // Same account, session 0: resolve id.
    auto session0Result = queryByIdAssertOk(megaApi[0].get(), id);
    ASSERT_TRUE(session0Result);

    // Same account, session 1: resolve id and verify payload matches session 0.
    auto session1Result = queryByIdAssertOk(megaApi[2].get(), id);
    ASSERT_TRUE(session1Result);
    EXPECT_EQ(bucketsToVector(*session0Result), bucketsToVector(*session1Result));

    // Different account must not resolve a foreign id.
    {
        RequestTracker tracker(megaApi[1].get());
        megaApi[1]->getRecentActionById(id.c_str(), &tracker);
        EXPECT_EQ(tracker.waitForResult(), API_ENOENT);
    }

    // After catchup, same-account session remains consistent.
    synchronousCatchup(2, maxTimeout);
    {
        RequestTracker tracker(megaApi[2].get());
        megaApi[2]->getRecentActionById(id.c_str(), &tracker);
        EXPECT_EQ(tracker.waitForResult(), API_OK);
    }
}

/**
 * Positive path for non-default bucket types.
 *
 * 1) media=1  – uploading a .mp4 file and a .jpg file produces a media bucket.
 * 2) update=1 – uploading the same filename twice produces a versioned bucket.
 * 3) Multi-file bucket – two files in the same folder/window share one bucket id.
 */
TEST_F(SdkTest, SdkRecentActionByIdBucketVariantsTest)
{
    LOG_info << "___TEST SdkRecentActionByIdBucketVariantsTest___";
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    std::unique_ptr<MegaNode> rootNode(megaApi[0]->getRootNode());
    ASSERT_TRUE(rootNode);

    const auto upload =
        [this](const std::string& fname, std::string_view contents, MegaNode* parent)
    {
        sdk_test::LocalTempFile f(fname, contents);
        ASSERT_TRUE(sdk_test::uploadFile(megaApi[0].get(), fname, parent))
            << "Cannot upload " << fname;
    };

    // Helper: fetch buckets, find the one containing `fname`, assert the given token value.
    const auto verifyTokenAndQuery = [this](const std::string& fname,
                                            size_t tokenIndex,
                                            const std::string& expectedTokenValue) -> std::string
    {
        auto buckets = fetchRecentBuckets(megaApi[0].get(), 1, 10);
        if (!buckets)
        {
            return {};
        }
        const std::string id = getRecentActionIdContainingFilename(*buckets, fname);
        if (id.empty())
        {
            ADD_FAILURE() << fname << " not found in recent buckets";
            return {};
        }
        const auto tokens = splitString<std::vector<std::string>>(id, '|');
        EXPECT_EQ(tokens.size(), 8u);
        if (tokens.size() > tokenIndex)
        {
            EXPECT_EQ(tokens[tokenIndex], expectedTokenValue)
                << "token[" << tokenIndex << "] mismatch";
        }
        return id;
    };

    // --- media=1: .mp4 and .jpg file produces a media bucket ---
    {
        const std::string mp4File = "recent_action_media.mp4";
        upload(mp4File, "fake video content", rootNode.get());
        WaitMillisec(1000);

        const std::string photoFile = "recent_action_photo.jpg";
        upload(photoFile, "fake photo content", rootNode.get());
        WaitMillisec(1000);

        const std::string mp4Id = verifyTokenAndQuery(mp4File, 5, "1"); // token[5] = media
        ASSERT_FALSE(mp4Id.empty());

        const std::string photoId = verifyTokenAndQuery(photoFile, 5, "1"); // token[5] = media
        ASSERT_FALSE(photoId.empty());

        ASSERT_TRUE(mp4Id == photoId) << "Both files should share the same media bucket";

        auto result = queryByIdAssertOk(megaApi[0].get(), mp4Id);
        ASSERT_TRUE(result);
        const std::vector<std::string> expectedFiles{photoFile, mp4File};
        EXPECT_EQ(bucketsToVector(*result)[0], expectedFiles);
    }

    // --- update=1: same filename uploaded twice produces a versioned node ---
    {
        const std::string name = "recent_action_versioned.txt";
        upload(name, "version 1", rootNode.get());
        WaitMillisec(500);
        upload(name, "version 2", rootNode.get());
        WaitMillisec(1000);

        const std::string id = verifyTokenAndQuery(name, 6, "1"); // token[6] = update
        ASSERT_FALSE(id.empty());

        auto result = queryByIdAssertOk(megaApi[0].get(), id);
        ASSERT_TRUE(result);
        const std::vector<std::string> expectedFiles{name};
        EXPECT_EQ(bucketsToVector(*result)[0], expectedFiles);
    }

    // --- multi-file: two files uploaded to the same folder share one bucket ---
    {
        const auto folderHandle = createFolder(0, "recent_action_multi", rootNode.get());
        ASSERT_NE(folderHandle, UNDEF);
        std::unique_ptr<MegaNode> folder(megaApi[0]->getNodeByHandle(folderHandle));
        ASSERT_TRUE(folder);

        const std::string nameA = "recent_action_multi_a.txt";
        const std::string nameB = "recent_action_multi_b.txt";
        upload(nameA, "content a", folder.get());
        upload(nameB, "content b", folder.get());
        WaitMillisec(1000);

        auto buckets = fetchRecentBuckets(megaApi[0].get(), 1, 10);
        ASSERT_TRUE(buckets);
        const std::string idA = getRecentActionIdContainingFilename(*buckets, nameA);
        const std::string idB = getRecentActionIdContainingFilename(*buckets, nameB);
        ASSERT_FALSE(idA.empty()) << nameA << " not found";
        EXPECT_EQ(idA, idB) << "Both files should share the same bucket";

        auto result = queryByIdAssertOk(megaApi[0].get(), idA);
        ASSERT_TRUE(result);
        const auto nodeNames = bucketsToVector(*result)[0];
        EXPECT_EQ(nodeNames.size(), 2u);
        EXPECT_NE(std::find(nodeNames.begin(), nodeNames.end(), nameA), nodeNames.end());
        EXPECT_NE(std::find(nodeNames.begin(), nodeNames.end(), nameB), nodeNames.end());
    }
}