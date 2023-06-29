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

#include <mega/megaclient.h>
#include <mega/megaapp.h>
#include <mega/types.h>

#include "DefaultedFileSystemAccess.h"
#include "utils.h"

#ifdef ENABLE_CHAT

namespace
{

class MockFileSystemAccess : public mt::DefaultedFileSystemAccess
{
};

void checkTextChats(const mega::TextChat& exp, const mega::TextChat& act)
{
    ASSERT_EQ(exp.getChatId(), act.getChatId());
    ASSERT_EQ(exp.getOwnPrivileges(), act.getOwnPrivileges());
    ASSERT_EQ(exp.getShard(), act.getShard());
    ASSERT_EQ(*exp.getUserPrivileges(), *act.getUserPrivileges());
    ASSERT_EQ(exp.getGroup(), act.getGroup());
    ASSERT_EQ(exp.getTitle(), act.getTitle());
    ASSERT_EQ(exp.getOwnUser(), act.getOwnUser());
    ASSERT_EQ(exp.getTs(), act.getTs());
    ASSERT_EQ(exp.getAttachments(), act.getAttachments());
    ASSERT_EQ(exp.isFlagSet(0), act.isFlagSet(0));
    ASSERT_EQ(exp.publicChat(), act.publicChat());
    ASSERT_EQ(exp.getUnifiedKey(), act.getUnifiedKey());
}

}

TEST(TextChat, serialize_unserialize)
{
    mega::MegaApp app;
    auto client = mt::makeClient(app);

    mega::TextChat tc(true);
    tc.setChatId(1);
    tc.setOwnPrivileges(mega::PRIV_STANDARD);
    tc.setShard(2);
    tc.addUserPrivileges(3, mega::PRIV_MODERATOR);
    tc.addUserPrivileges(4, mega::PRIV_RO);
    tc.setGroup(true);
    tc.setTitle("foo");
    tc.setOwnUser(5);
    tc.setTs(6);
    tc.addUserForAttachment(7, 8);
    tc.addUserForAttachment(7, 9);
    tc.addUserForAttachment(8, 10);
    tc.setFlag(true, 0);
    tc.setUnifiedKey("bar");

    std::string d;
    ASSERT_TRUE(tc.serialize(&d));

    auto newTc = mega::TextChat::unserialize(client.get(), &d);
    checkTextChats(tc, *newTc);
}

TEST(TextChat, unserialize_32bit)
{
    mega::MegaApp app;
    auto client = mt::makeClient(app);

    mega::TextChat tc(true);
    tc.setChatId(1);
    tc.setOwnPrivileges(mega::PRIV_STANDARD);
    tc.setShard(2);
    tc.addUserPrivileges(3, mega::PRIV_MODERATOR);
    tc.addUserPrivileges(4, mega::PRIV_RO);
    tc.setGroup(true);
    tc.setTitle("foo");
    tc.setOwnUser(5);
    tc.setTs(6);
    tc.addUserForAttachment(7, 8);
    tc.addUserForAttachment(7, 9);
    tc.addUserForAttachment(8, 10);
    tc.setFlag(true, 0);
    tc.setUnifiedKey("bar");

    // This is the result of serialization on 32bit Windows
    const std::array<char, 125> rawData = {
        0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
        0x02, 0x00, 0x00, 0x00, 0x02, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x03, 0x00, 0x66, 0x6f, 0x6f,
        0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x02, 0x00, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x02, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x09, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x01, 0x00, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x03, 0x00, 0x62, 0x61, 0x72
    };
    std::string d(rawData.data(), rawData.size());

    auto newTc = mega::TextChat::unserialize(client.get(), &d);
    checkTextChats(tc, *newTc);
}
#endif
