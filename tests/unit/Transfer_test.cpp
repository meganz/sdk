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
#include "utils.h"

#include <gtest/gtest.h>

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
    // Create very long URLs (simulating real-world scenarios)
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
    // Extreme variation: shortest first, longest last - tests parsing logic thoroughly
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
