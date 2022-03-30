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
    ASSERT_EQ(exp.id, act.id);
    ASSERT_EQ(exp.priv, act.priv);
    ASSERT_EQ(exp.shard, act.shard);
    ASSERT_EQ(*exp.userpriv, *act.userpriv);
    ASSERT_EQ(exp.group, act.group);
    ASSERT_EQ(exp.title, act.title);
    ASSERT_EQ(exp.ou, act.ou);
    ASSERT_EQ(exp.ts, act.ts);
    ASSERT_EQ(exp.attachedNodes, act.attachedNodes);
    ASSERT_EQ(exp.isFlagSet(0), act.isFlagSet(0));
    ASSERT_EQ(exp.publicchat, act.publicchat);
    ASSERT_EQ(exp.unifiedKey, act.unifiedKey);
}

}

TEST(TextChat, serialize_unserialize)
{
    mega::MegaApp app;
    auto client = mt::makeClient(app);

    mega::TextChat tc;
    tc.id = 1;
    tc.priv = mega::PRIV_STANDARD;
    tc.shard = 2;
    tc.userpriv = new mega::userpriv_vector;
    tc.userpriv->emplace_back(3, mega::PRIV_MODERATOR);
    tc.userpriv->emplace_back(4, mega::PRIV_RO);
    tc.group = true;
    tc.title = "foo";
    tc.ou = 5;
    tc.ts = 6;
    tc.attachedNodes[7].insert(8);
    tc.attachedNodes[7].insert(9);
    tc.attachedNodes[8].insert(10);
    tc.setFlag(true, 0);
    tc.publicchat = true;
    tc.unifiedKey = "bar";

    std::string d;
    ASSERT_TRUE(tc.serialize(&d));

    auto newTc = mega::TextChat::unserialize(client.get(), &d);
    checkTextChats(tc, *newTc);
}

TEST(TextChat, unserialize_32bit)
{
    mega::MegaApp app;
    auto client = mt::makeClient(app);

    mega::TextChat tc;
    tc.id = 1;
    tc.priv = mega::PRIV_STANDARD;
    tc.shard = 2;
    tc.userpriv = new mega::userpriv_vector;
    tc.userpriv->emplace_back(3, mega::PRIV_MODERATOR);
    tc.userpriv->emplace_back(4, mega::PRIV_RO);
    tc.group = true;
    tc.title = "foo";
    tc.ou = 5;
    tc.ts = 6;
    tc.attachedNodes[7].insert(8);
    tc.attachedNodes[7].insert(9);
    tc.attachedNodes[8].insert(10);
    tc.setFlag(true, 0);
    tc.publicchat = true;
    tc.unifiedKey = "bar";

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
