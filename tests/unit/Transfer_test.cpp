/**
 * (c) 2019 by Mega Limited, Wellsford, New Zealand
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
 *
 * You should have received a copy of the license along with this
 * program.
 */

#include "mega/megaapp.h"
#include "mega/raid.h"
#include "mega/transfer.h"
#include "mega/utils.h"
#include "utils.h"

#include <gtest/gtest.h>

#include <cassert>
#include <limits>
#include <string>

namespace
{

void checkTransfers(const mega::Transfer& exp, const mega::Transfer& act)
{
    ASSERT_EQ(exp.type, act.type);
    ASSERT_EQ(exp.localfilename, act.localfilename);
    ASSERT_EQ(exp.filekey.bytes, act.filekey.bytes);
    ASSERT_EQ(exp.ctriv, act.ctriv);
    ASSERT_EQ(exp.metamac, act.metamac);
    ASSERT_TRUE(std::equal(exp.transferkey.data(),
                           exp.transferkey.data() + mega::SymmCipher::KEYLENGTH,
                           act.transferkey.data()));
    ASSERT_EQ(exp.lastaccesstime, act.lastaccesstime);
    ASSERT_EQ(exp.ultoken != nullptr, act.ultoken != nullptr); // Both NULLs OR both valid
    if (exp.ultoken && act.ultoken)
    {
        ASSERT_EQ(*exp.ultoken, *act.ultoken);
    }
    ASSERT_EQ(exp.tempurls, act.tempurls);
    ASSERT_EQ(exp.state, act.state);
    ASSERT_EQ(exp.priority, act.priority);
    ASSERT_EQ(exp.ws_fileno, act.ws_fileno);
    ASSERT_EQ(exp.ws_session_url, act.ws_session_url);
}

void setupTransfer(mega::Transfer& tf,
                   const std::string& localfilename,
                   char filekeyChar,
                   int64_t ctriv,
                   int64_t metamac,
                   char transferkeyChar,
                   int64_t lastaccesstime)
{
    tf.localfilename = ::mega::LocalPath::fromAbsolutePath(localfilename);
    std::fill(&tf.filekey.bytes[0], &tf.filekey.bytes[0] + sizeof(tf.filekey), filekeyChar);
    tf.ctriv = ctriv;
    tf.metamac = metamac;
    std::fill(tf.transferkey.data(),
              tf.transferkey.data() + mega::SymmCipher::KEYLENGTH,
              transferkeyChar);
    tf.lastaccesstime = lastaccesstime;
}
}

TEST(Transfer, serialize_unserialize_raid_urls_same_length)
{
    mega::MegaApp app;
    auto client = mt::makeClient(app);

    mega::Transfer tf{client.get(), mega::GET};
    setupTransfer(tf, "foo", 'X', 1, 2, 'Y', 3);
    tf.ultoken.reset(new mega::UploadToken);
    std::fill((::mega::byte*)tf.ultoken.get(),
              (::mega::byte*)tf.ultoken.get() + mega::UPLOADTOKENLEN,
              'Z');
    tf.tempurls = {
        "http://bar1.com",
        "http://bar2.com",
        "http://bar3.com",
        "http://bar4.com",
        "http://bar5.com",
        "http://bar6.com",
    };
    tf.state = mega::TRANSFERSTATE_PAUSED;
    tf.priority = 4;

    std::string d;
    ASSERT_TRUE(tf.serialize(&d));

    mega::transfer_multimap tfMap[2];
    auto newTf =
        std::unique_ptr<mega::Transfer>{mega::Transfer::unserialize(client.get(), &d, tfMap)};
    checkTransfers(tf, *newTf);
}

// Test that URLs with different lengths are correctly parsed (e.g., sandbox3 RAID)
TEST(Transfer, serialize_unserialize_raid_urls_different_lengths)
{
    mega::MegaApp app;
    auto client = mt::makeClient(app);

    mega::Transfer tf{client.get(), mega::GET};
    setupTransfer(tf, "test_file", 'A', 10, 20, 'B', 30);
    // Test with URLs of different lengths (simulating sandbox3 or different storage servers)
    tf.tempurls = {
        "http://gfs270n406.userstorage.mega.co.nz/dl/short",
        "http://gfs262n309.userstorage.mega.co.nz/dl/verylongtoken12345678901234567890",
        "http://gfs214n115.userstorage.mega.co.nz/dl/mediumtoken12345",
        "http://gfs204n127.userstorage.mega.co.nz/dl/"
        "extremelylongtokenabcdefghijklmnopqrstuvwxyz1234567890",
        "http://gfs208n116.userstorage.mega.co.nz/dl/normaltoken",
        "http://gfs206n167.userstorage.mega.co.nz/dl/anothermediumtoken67890",
    };
    ASSERT_EQ(tf.tempurls.size(), mega::RAIDPARTS);
    tf.state = mega::TRANSFERSTATE_NONE;
    tf.priority = 100;

    std::string d;
    ASSERT_TRUE(tf.serialize(&d));

    mega::transfer_multimap tfMap[2];
    auto newTf =
        std::unique_ptr<mega::Transfer>{mega::Transfer::unserialize(client.get(), &d, tfMap)};
    ASSERT_NE(newTf, nullptr);
    checkTransfers(tf, *newTf);
}

// Test single URL (non-RAID download)
TEST(Transfer, serialize_unserialize_single_url)
{
    mega::MegaApp app;
    auto client = mt::makeClient(app);

    mega::Transfer tf{client.get(), mega::GET};
    setupTransfer(tf, "single_file", 'C', 5, 10, 'D', 15);
    tf.tempurls = {
        "http://gfs123n456.userstorage.mega.co.nz/dl/"
        "verylongsingletokenabcdefghijklmnopqrstuvwxyz1234567890abcdefghijklmnopqrstuvwxyz"};
    ASSERT_EQ(tf.tempurls.size(), 1u);
    tf.state = mega::TRANSFERSTATE_NONE;
    tf.priority = 50;

    std::string d;
    ASSERT_TRUE(tf.serialize(&d));

    mega::transfer_multimap tfMap[2];
    auto newTf =
        std::unique_ptr<mega::Transfer>{mega::Transfer::unserialize(client.get(), &d, tfMap)};
    ASSERT_NE(newTf, nullptr);
    checkTransfers(tf, *newTf);
}

// Test empty URLs (transfer before URLs are fetched)
TEST(Transfer, serialize_unserialize_empty_urls)
{
    mega::MegaApp app;
    auto client = mt::makeClient(app);

    mega::Transfer tf{client.get(), mega::GET};
    setupTransfer(tf, "pending_file", 'E', 7, 14, 'F', 21);
    tf.tempurls = {}; // Empty URLs
    tf.state = mega::TRANSFERSTATE_NONE;
    tf.priority = 25;

    std::string d;
    ASSERT_TRUE(tf.serialize(&d));

    mega::transfer_multimap tfMap[2];
    auto newTf =
        std::unique_ptr<mega::Transfer>{mega::Transfer::unserialize(client.get(), &d, tfMap)};
    ASSERT_NE(newTf, nullptr);
    checkTransfers(tf, *newTf);
}

// Test with very long URLs (edge case for buffer handling)
TEST(Transfer, serialize_unserialize_very_long_urls)
{
    mega::MegaApp app;
    auto client = mt::makeClient(app);

    mega::Transfer tf{client.get(), mega::GET};
    setupTransfer(tf, "large_file", 'G', 8, 16, 'H', 24);
    std::string longToken(200, 'x');
    std::string mediumToken(150, 'y');
    std::string shortToken(100, 'z');
    tf.tempurls = {
        "http://gfs270n406.userstorage.mega.co.nz/dl/" + longToken,
        "http://gfs262n309.userstorage.mega.co.nz/dl/" + mediumToken,
        "http://gfs214n115.userstorage.mega.co.nz/dl/" + shortToken,
        "http://gfs204n127.userstorage.mega.co.nz/dl/" + longToken + "extra",
        "http://gfs208n116.userstorage.mega.co.nz/dl/" + mediumToken + "more",
        "http://gfs206n167.userstorage.mega.co.nz/dl/" + shortToken + "data",
    };
    ASSERT_EQ(tf.tempurls.size(), mega::RAIDPARTS);
    tf.state = mega::TRANSFERSTATE_PAUSED;
    tf.priority = 200;

    std::string d;
    ASSERT_TRUE(tf.serialize(&d));

    mega::transfer_multimap tfMap[2];
    auto newTf =
        std::unique_ptr<mega::Transfer>{mega::Transfer::unserialize(client.get(), &d, tfMap)};
    ASSERT_NE(newTf, nullptr);
    checkTransfers(tf, *newTf);
}

// Test PUT transfer (upload) with single URL
TEST(Transfer, serialize_unserialize_put_single_url)
{
    mega::MegaApp app;
    auto client = mt::makeClient(app);

    mega::Transfer tf{client.get(), mega::PUT};
    setupTransfer(tf, "upload_file", 'I', 9, 18, 'J', 27);
    tf.tempurls = {"http://gfs999n999.userstorage.mega.co.nz/ul/"
                   "uploadtoken1234567890abcdefghijklmnopqrstuvwxyz"};
    ASSERT_EQ(tf.tempurls.size(), 1u);
    tf.state = mega::TRANSFERSTATE_NONE;
    tf.priority = 75;

    std::string d;
    ASSERT_TRUE(tf.serialize(&d));

    mega::transfer_multimap tfMap[2];
    auto newTf =
        std::unique_ptr<mega::Transfer>{mega::Transfer::unserialize(client.get(), &d, tfMap)};
    ASSERT_NE(newTf, nullptr);
    checkTransfers(tf, *newTf);
}

// Test edge case: first URL is shortest, last URL is longest
TEST(Transfer, serialize_unserialize_extreme_length_variation)
{
    mega::MegaApp app;
    auto client = mt::makeClient(app);

    mega::Transfer tf{client.get(), mega::GET};
    setupTransfer(tf, "extreme_file", 'K', 11, 22, 'L', 33);
    tf.tempurls = {
        "http://a.co/x",
        "http://gfs262n309.userstorage.mega.co.nz/dl/medium12345",
        "http://gfs214n115.userstorage.mega.co.nz/dl/anothermedium67890",
        "http://gfs204n127.userstorage.mega.co.nz/dl/longertokenabcdefghijklmnopqrstuvwxyz",
        "http://gfs208n116.userstorage.mega.co.nz/dl/verylongtoken123456789012345678901234567890",
        "http://gfs206n167.userstorage.mega.co.nz/dl/"
        "extremelylongtokenabcdefghijklmnopqrstuvwxyz1234567890ABCDEFGHIJKLMNOPQRSTUVWXYZ",
    };
    ASSERT_EQ(tf.tempurls.size(), mega::RAIDPARTS);
    tf.state = mega::TRANSFERSTATE_NONE;
    tf.priority = 300;

    std::string d;
    ASSERT_TRUE(tf.serialize(&d));

    mega::transfer_multimap tfMap[2];
    auto newTf =
        std::unique_ptr<mega::Transfer>{mega::Transfer::unserialize(client.get(), &d, tfMap)};
    ASSERT_NE(newTf, nullptr);
    checkTransfers(tf, *newTf);
}

TEST(Transfer, serialize_unserialize_ws_resume_metadata_both_present)
{
    mega::MegaApp app;
    auto client = mt::makeClient(app);

    mega::Transfer tf{client.get(), mega::PUT};
    setupTransfer(tf, "ws_upload_file", 'M', 12, 24, 'N', 36);
    tf.tempurls = {"http://gfs999n999.userstorage.mega.co.nz/ul/resume-token"};
    tf.state = mega::TRANSFERSTATE_NONE;
    tf.priority = 301;
    tf.ws_fileno = 12345;
    tf.ws_session_url = "wss://gfs123n456.userstorage.mega.co.nz/ws-session-abc";

    std::string d;
    ASSERT_TRUE(tf.serialize(&d));

    mega::transfer_multimap tfMap[2];
    auto newTf =
        std::unique_ptr<mega::Transfer>{mega::Transfer::unserialize(client.get(), &d, tfMap)};
    ASSERT_NE(newTf, nullptr);
    checkTransfers(tf, *newTf);
}

TEST(Transfer, serialize_unserialize_ws_resume_metadata_only_fileno)
{
    mega::MegaApp app;
    auto client = mt::makeClient(app);

    mega::Transfer tf{client.get(), mega::PUT};
    setupTransfer(tf, "ws_upload_file_fileno_only", 'O', 13, 26, 'P', 39);
    tf.tempurls = {"http://gfs999n999.userstorage.mega.co.nz/ul/resume-token-fileno"};
    tf.state = mega::TRANSFERSTATE_NONE;
    tf.priority = 302;
    tf.ws_fileno = 77;
    tf.ws_session_url.clear();

    std::string d;
    ASSERT_TRUE(tf.serialize(&d));

    mega::transfer_multimap tfMap[2];
    auto newTf =
        std::unique_ptr<mega::Transfer>{mega::Transfer::unserialize(client.get(), &d, tfMap)};
    ASSERT_NE(newTf, nullptr);
    checkTransfers(tf, *newTf);
}

TEST(Transfer, serialize_unserialize_ws_resume_metadata_only_session_url)
{
    mega::MegaApp app;
    auto client = mt::makeClient(app);

    mega::Transfer tf{client.get(), mega::PUT};
    setupTransfer(tf, "ws_upload_file_url_only", 'Q', 14, 28, 'R', 42);
    tf.tempurls = {"http://gfs999n999.userstorage.mega.co.nz/ul/resume-token-url"};
    tf.state = mega::TRANSFERSTATE_NONE;
    tf.priority = 303;
    tf.ws_fileno = 0;
    tf.ws_session_url = "wss://gfs456n789.userstorage.mega.co.nz/ws-session-def";

    std::string d;
    ASSERT_TRUE(tf.serialize(&d));

    mega::transfer_multimap tfMap[2];
    auto newTf =
        std::unique_ptr<mega::Transfer>{mega::Transfer::unserialize(client.get(), &d, tfMap)};
    ASSERT_NE(newTf, nullptr);
    checkTransfers(tf, *newTf);
}

TEST(Transfer, serialize_unserialize_ws_resume_metadata_boundary_values)
{
    mega::MegaApp app;
    auto client = mt::makeClient(app);

    mega::Transfer tf{client.get(), mega::PUT};
    setupTransfer(tf, "ws_upload_file_boundary", 'S', 15, 30, 'T', 45);
    tf.tempurls = {"http://gfs999n999.userstorage.mega.co.nz/ul/resume-token-boundary"};
    tf.state = mega::TRANSFERSTATE_PAUSED;
    tf.priority = 304;
    tf.ws_fileno = std::numeric_limits<std::uint32_t>::max();
    tf.ws_session_url = "wss://gfs789n012.userstorage.mega.co.nz/ws-session-";
    tf.ws_session_url.append(512, 'x');
    tf.ultoken.reset(new mega::UploadToken);
    std::fill(tf.ultoken->begin(), tf.ultoken->end(), static_cast<mega::byte>('U'));

    std::string d;
    ASSERT_TRUE(tf.serialize(&d));

    mega::transfer_multimap tfMap[2];
    auto newTf =
        std::unique_ptr<mega::Transfer>{mega::Transfer::unserialize(client.get(), &d, tfMap)};
    ASSERT_NE(newTf, nullptr);
    checkTransfers(tf, *newTf);
}

TEST(Transfer, unserialize_legacy_v1_format_without_ws_fields)
{
    mega::MegaApp app;
    auto client = mt::makeClient(app);

    mega::Transfer tf{client.get(), mega::PUT};
    setupTransfer(tf, "legacy_ws_upload", 'V', 16, 32, 'W', 48);
    tf.tempurls = {"http://gfs999n999.userstorage.mega.co.nz/ul/legacy-token"};
    tf.state = mega::TRANSFERSTATE_PAUSED;
    tf.priority = 305;
    tf.ws_fileno = 0;
    tf.ws_session_url.clear();

    std::string d;
    ASSERT_TRUE(tf.serialize(&d));

    // Convert serialized payload from v2 to v1 at the version byte location.
    // Keep expansion flags[3..7] at zero and no WS payload fields to emulate legacy records.
    std::string suffix = d;
    mega::CacheableReader r(suffix);
    mega::direction_t direction{};
    std::string filepath;
    std::array<mega::byte, sizeof(tf.filekey)> filekeyBytes{};
    int64_t ctriv{};
    int64_t metamac{};
    std::array<mega::byte, mega::SymmCipher::KEYLENGTH> transferkey{};
    mega::chunkmac_map chunkmacs;
    mega::FileFingerprint fp;
    mega::FileFingerprint badfp;
    int64_t lastaccesstime{};
    int8_t hasUltoken{};
    std::array<mega::byte, mega::UPLOADTOKENLEN> ultoken{};
    std::string combinedUrls;
    int8_t state{};
    uint64_t priority{};

    ASSERT_TRUE(r.unserializedirection(direction));
    ASSERT_TRUE(r.unserializestring(filepath));
    ASSERT_TRUE(r.unserializebinary(filekeyBytes.data(), filekeyBytes.size()));
    ASSERT_TRUE(r.unserializei64(ctriv));
    ASSERT_TRUE(r.unserializei64(metamac));
    ASSERT_TRUE(r.unserializebinary(transferkey.data(), transferkey.size()));
    ASSERT_TRUE(r.unserializechunkmacs(chunkmacs));
    ASSERT_TRUE(r.unserializefingerprint(fp));
    ASSERT_TRUE(r.unserializefingerprint(badfp));
    ASSERT_TRUE(r.unserializei64(lastaccesstime));
    ASSERT_TRUE(r.unserializei8(hasUltoken));
    ASSERT_TRUE(hasUltoken == 0 || hasUltoken == 2);
    if (hasUltoken)
    {
        ASSERT_TRUE(r.unserializebinary(ultoken.data(), ultoken.size()));
    }
    ASSERT_TRUE(r.unserializestring(combinedUrls));
    ASSERT_TRUE(r.unserializei8(state));
    ASSERT_TRUE(r.unserializeu64(priority));
    r.eraseused(suffix);
    ASSERT_FALSE(suffix.empty());
    suffix[0] = 1; // legacy version 1

    std::string legacyData = d.substr(0, d.size() - suffix.size()) + suffix;

    mega::transfer_multimap tfMap[2];
    auto newTf = std::unique_ptr<mega::Transfer>{
        mega::Transfer::unserialize(client.get(), &legacyData, tfMap)};
    ASSERT_NE(newTf, nullptr);
    checkTransfers(tf, *newTf);
    ASSERT_EQ(newTf->ws_fileno, 0u);
    ASSERT_TRUE(newTf->ws_session_url.empty());
}

#ifdef MEGA_USE_WSUPLOAD

// Helper: serialize a Transfer with WS fields set, returning the serialized blob.
static std::string serializeTransferWithWsFields(mega::MegaClient* client,
                                                  const std::uint32_t fileno,
                                                  const std::string& sessionUrl)
{
    mega::Transfer tf{client, mega::PUT};
    setupTransfer(tf, "ws_corrupt_test", 'A', 1, 2, 'B', 3);
    tf.tempurls = {"http://gfs999n999.userstorage.mega.co.nz/ul/tok"};
    tf.state = mega::TRANSFERSTATE_NONE;
    tf.priority = 100;
    tf.ws_fileno = fileno;
    tf.ws_session_url = sessionUrl;

    std::string d;
    bool ok = tf.serialize(&d);
    assert(ok);
    (void)ok;
    return d;
}

TEST(Transfer, unserialize_truncated_at_ws_fileno)
{
    mega::MegaApp app;
    auto client = mt::makeClient(app);

    std::string d = serializeTransferWithWsFields(client.get(), 42, "wss://example.com/session");

    // Chop off the last few bytes so the ws_fileno field is incomplete.
    // The ws_session_url comes after ws_fileno, so removing enough from the end
    // will first corrupt the session_url, then eventually the fileno.
    // Remove everything after expansion flags + downloadFileHandle + discardedTempUrls fields
    // but before ws_fileno is fully read.
    // Strategy: progressively truncate from the end until unserialize fails.
    for (std::size_t cut = 1; cut < d.size() / 2; ++cut)
    {
        std::string truncated = d.substr(0, d.size() - cut);
        mega::transfer_multimap tfMap[2];
        auto result = std::unique_ptr<mega::Transfer>{
            mega::Transfer::unserialize(client.get(), &truncated, tfMap)};
        // Must either return nullptr (graceful failure) or a valid Transfer (partial parse).
        // Must never crash.
        (void)result;
    }
    // If we get here without crashing, truncation is handled gracefully.
    SUCCEED();
}

TEST(Transfer, unserialize_truncated_at_ws_session_url)
{
    mega::MegaApp app;
    auto client = mt::makeClient(app);

    // Use a long session URL so there's plenty of room to truncate mid-string.
    const std::string longUrl = "wss://gfs123.userstorage.mega.co.nz/ws-session-"
                                + std::string(200, 'x');
    std::string d = serializeTransferWithWsFields(client.get(), 99, longUrl);

    // Truncate within the last 250 bytes (where the URL payload lives).
    for (std::size_t cut = 1; cut < 250 && cut < d.size(); ++cut)
    {
        std::string truncated = d.substr(0, d.size() - cut);
        mega::transfer_multimap tfMap[2];
        auto result = std::unique_ptr<mega::Transfer>{
            mega::Transfer::unserialize(client.get(), &truncated, tfMap)};
        (void)result;
    }
    SUCCEED();
}

TEST(Transfer, unserialize_corrupted_expansion_flags_claiming_ws_fields)
{
    mega::MegaApp app;
    auto client = mt::makeClient(app);

    // Serialize a Transfer WITHOUT WS fields.
    mega::Transfer tf{client.get(), mega::PUT};
    setupTransfer(tf, "no_ws_fields", 'C', 5, 10, 'D', 15);
    tf.tempurls = {"http://gfs999n999.userstorage.mega.co.nz/ul/tok2"};
    tf.state = mega::TRANSFERSTATE_NONE;
    tf.priority = 200;
    tf.ws_fileno = 0;
    tf.ws_session_url.clear();

    std::string d;
    ASSERT_TRUE(tf.serialize(&d));

    // Serialized tail layout (no WS fields):
    //   ... priority(8) | version(1)=2 | expansion_flags(1) | discardedTempUrlsSize(1)
    // Expansion flags bits: [0]=downloadFileHandle [1]=discardedTempUrls [2]=localPath
    //                       [3]=ws_fileno [4]=ws_session_url
    // With no WS fields, bits 3 and 4 are 0. Set them to claim WS data is present.
    // unserialize should fail (return nullptr) because the data isn't there.
    ASSERT_GE(d.size(), 3u);
    const std::size_t flagsOffset = d.size() - 2; // second-to-last byte is expansion flags

    // Set bit 3 (ws_fileno present) — data is missing, should fail.
    {
        std::string corrupted = d;
        corrupted[flagsOffset] |= (1 << 3);
        mega::transfer_multimap tfMap[2];
        auto result = std::unique_ptr<mega::Transfer>{
            mega::Transfer::unserialize(client.get(), &corrupted, tfMap)};
        EXPECT_EQ(result, nullptr) << "Should fail when ws_fileno flag set but data missing";
    }

    // Set bit 4 (ws_session_url present) — data is missing, should fail.
    {
        std::string corrupted = d;
        corrupted[flagsOffset] |= (1 << 4);
        mega::transfer_multimap tfMap[2];
        auto result = std::unique_ptr<mega::Transfer>{
            mega::Transfer::unserialize(client.get(), &corrupted, tfMap)};
        EXPECT_EQ(result, nullptr) << "Should fail when ws_session_url flag set but data missing";
    }

    // Set both bits 3 and 4 — data is missing, should fail.
    {
        std::string corrupted = d;
        corrupted[flagsOffset] |= (1 << 3) | (1 << 4);
        mega::transfer_multimap tfMap[2];
        auto result = std::unique_ptr<mega::Transfer>{
            mega::Transfer::unserialize(client.get(), &corrupted, tfMap)};
        EXPECT_EQ(result, nullptr) << "Should fail when both WS flags set but data missing";
    }
}

#endif // MEGA_USE_WSUPLOAD
