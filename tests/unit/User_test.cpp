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

#include "DefaultedFileSystemAccess.h"
#include "mega.h"
#include "utils.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <mega/megaapp.h>
#include <mega/megaclient.h>
#include <mega/user.h>
#include <mega/user_attribute.h>

namespace
{

//class MockFileSystemAccess : public mt::DefaultedFileSystemAccess
//{
//};

void checkUsers(const mega::User& exp, const mega::User& act)
{
    ASSERT_EQ(exp.userhandle, act.userhandle);
    ASSERT_EQ(exp.email, act.email);
    ASSERT_EQ(exp.show, act.show);
    ASSERT_EQ(exp.ctime, act.ctime);
    std::string expKey;
    exp.pubk.serializekey(&expKey, mega::AsymmCipher::PUBKEY);
    ASSERT_FALSE(expKey.empty());
    std::string actKey;
    act.pubk.serializekey(&actKey, mega::AsymmCipher::PUBKEY);
    ASSERT_FALSE(actKey.empty());
    ASSERT_EQ(expKey, actKey);

    const mega::UserAttribute* attribute1 = exp.getAttribute(mega::ATTR_FIRSTNAME);
    const mega::UserAttribute* attribute2 = act.getAttribute(mega::ATTR_FIRSTNAME);
    ASSERT_THAT(attribute1, testing::NotNull());
    ASSERT_THAT(attribute2, testing::NotNull());
    ASSERT_EQ(attribute1->value(), attribute2->value());
    ASSERT_EQ(attribute1->version(), attribute2->version());

    attribute1 = exp.getAttribute(mega::ATTR_LASTNAME);
    attribute2 = act.getAttribute(mega::ATTR_LASTNAME);
    ASSERT_THAT(attribute1, testing::NotNull());
    ASSERT_THAT(attribute2, testing::NotNull());
    ASSERT_EQ(attribute1->value(), attribute2->value());
}

}

TEST(User, serialize_unserialize)
{
    mega::MegaApp app;
    auto client = mt::makeClient(app);

    const std::string email = "foo@bar.com";
    mega::User user{email.c_str()};
    user.userhandle = 13;
    user.ctime = 14;
    user.show = mega::VISIBLE;
    std::string firstname1 = "f";
    std::string firstname2 = "f2";
    std::string lastname = "oo";
    user.setAttribute(mega::ATTR_FIRSTNAME, firstname1, firstname2);
    user.setAttribute(mega::ATTR_LASTNAME, lastname, {});
    std::string key(128, 1);
    user.pubk.setkey(mega::AsymmCipher::PUBKEY, reinterpret_cast<const mega::byte*>(key.c_str()), static_cast<int>(key.size()));
    ASSERT_TRUE(user.pubk.isvalid(mega::AsymmCipher::PUBKEY));

    std::string d;
    ASSERT_TRUE(user.serialize(&d));

    auto newUser = mega::User::unserialize(client.get(), &d);
    checkUsers(user, *newUser);
}

TEST(User, unserialize_32bit)
{
    mega::MegaApp app;
    auto client = mt::makeClient(app);
    const std::string email = "foo@bar.com";
    mega::User user{email.c_str()};
    user.userhandle = 13;
    user.ctime = 14;
    user.show = mega::VISIBLE;
    std::string firstname1 = "f";
    std::string firstname2 = "f2";
    std::string lastname = "oo";
    user.setAttribute(mega::ATTR_FIRSTNAME, firstname1, firstname2);
    user.setAttribute(mega::ATTR_LASTNAME, lastname, {});
    std::string key(128, 1);
    user.pubk.setkey(mega::AsymmCipher::PUBKEY, reinterpret_cast<const mega::byte*>(key.c_str()), static_cast<int>(key.size()));
    ASSERT_TRUE(user.pubk.isvalid(mega::AsymmCipher::PUBKEY));

    // This is the result of serialization on 32bit Windows
    const std::array<char, 133> rawData = {
        0x0d, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0e, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x0b, 0x66, 0x6f, 0x6f,
        0x40, 0x62, 0x61, 0x72, 0x2e, 0x63, 0x6f, 0x6d, 0x31, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x02, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x66,
        0x02, 0x00, 0x66, 0x32, 0x02, 0x00, 0x00, 0x00, 0x02, 0x00, 0x6f, 0x6f,
        0x01, 0x00, 0x4e, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
        0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
        0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
        0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
        0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
        0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
        0x01
    };
    std::string d(rawData.data(), rawData.size());

    auto newUser = mega::User::unserialize(client.get(), &d);
    checkUsers(user, *newUser);
}
