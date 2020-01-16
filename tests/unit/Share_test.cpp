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

#include <gtest/gtest.h>

#include <mega/share.h>

void checkNewShares(const mega::NewShare& exp, const mega::NewShare& act)
{
    ASSERT_EQ(exp.h, act.h);
    ASSERT_EQ(exp.outgoing, act.outgoing);
    ASSERT_EQ(exp.peer, act.peer);
    ASSERT_EQ(exp.access, act.access);
    ASSERT_EQ(exp.ts, act.ts);
    ASSERT_TRUE(std::equal(exp.key, exp.key + mega::SymmCipher::BLOCKSIZE, act.key));
    ASSERT_EQ(exp.have_key, act.have_key);
    ASSERT_EQ(exp.have_auth, act.have_auth);
    ASSERT_EQ(exp.pending, act.pending);
}

TEST(Share, serialize_unserialize)
{
    mega::User user;
    user.userhandle = 42;
    mega::PendingContactRequest pcr{123};
    mega::Share share{&user, mega::RDONLY, 13, &pcr};
    std::string d;
    share.serialize(&d);

    mega::byte key[mega::SymmCipher::BLOCKSIZE];
    std::fill(key, key + mega::SymmCipher::BLOCKSIZE, 'X');
    auto data = d.c_str();
    auto newShare = std::unique_ptr<mega::NewShare>{mega::Share::unserialize(-1, 100, key, &data, d.data() + d.size())};

    const mega::NewShare expectedNewShare{100, -1, user.userhandle, mega::RDONLY, 13, key, NULL, 123};
    checkNewShares(expectedNewShare, *newShare);
}

TEST(Share, unserialize_32bit)
{
    // This is the result of serialization on 32bit Windows
    const std::array<char, 26> rawData = {
        0x2a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0d, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x7b, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00
    };
    const std::string d(rawData.data(), rawData.size());

    mega::byte key[mega::SymmCipher::BLOCKSIZE];
    std::fill(key, key + mega::SymmCipher::BLOCKSIZE, 'X');
    auto data = d.c_str();
    auto newShare = std::unique_ptr<mega::NewShare>{mega::Share::unserialize(-1, 100, key, &data, d.data() + d.size())};

    const mega::NewShare expectedNewShare{100, -1, 42, mega::RDONLY, 13, key, NULL, 123};
    checkNewShares(expectedNewShare, *newShare);
}
