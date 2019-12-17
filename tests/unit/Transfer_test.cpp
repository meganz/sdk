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
#include <mega/transfer.h>

#include "DefaultedFileSystemAccess.h"
#include "utils.h"

namespace
{

class MockFileSystemAccess : public mt::DefaultedFileSystemAccess
{
};

void checkTransfers(const mega::Transfer& exp, const mega::Transfer& act)
{
    ASSERT_EQ(exp.type, act.type);
    ASSERT_EQ(exp.localfilename, act.localfilename);
    ASSERT_TRUE(std::equal(exp.filekey, exp.filekey + mega::FILENODEKEYLENGTH, act.filekey));
    ASSERT_EQ(exp.ctriv, act.ctriv);
    ASSERT_EQ(exp.metamac, act.metamac);
    ASSERT_TRUE(std::equal(exp.transferkey, exp.transferkey + mega::SymmCipher::KEYLENGTH, act.transferkey));
    ASSERT_EQ(exp.lastaccesstime, act.lastaccesstime);
    ASSERT_TRUE(std::equal(exp.ultoken, exp.ultoken + mega::NewNode::UPLOADTOKENLEN, act.ultoken));
    ASSERT_EQ(exp.tempurls, act.tempurls);
    ASSERT_EQ(exp.state, act.state);
    ASSERT_EQ(exp.priority, act.priority);
}

}

TEST(Transfer, serialize_unserialize)
{
    mega::MegaApp app;
    MockFileSystemAccess fsaccess;
    auto client = mt::makeClient(app, fsaccess);

    mega::Transfer tf{client.get(), mega::GET};
    tf.localfilename = "foo";
    std::fill(tf.filekey, tf.filekey + mega::FILENODEKEYLENGTH, 'X');
    tf.ctriv = 1;
    tf.metamac = 2;
    std::fill(tf.transferkey, tf.transferkey + mega::SymmCipher::KEYLENGTH, 'Y');
    tf.lastaccesstime = 3;
    tf.ultoken = new mega::byte[mega::NewNode::UPLOADTOKENLEN];
    std::fill(tf.ultoken, tf.ultoken + mega::NewNode::UPLOADTOKENLEN, 'Z');
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

    mega::transfer_map tfMap;
    auto newTf = std::unique_ptr<mega::Transfer>{mega::Transfer::unserialize(client.get(), &d, &tfMap)};
    checkTransfers(tf, *newTf);
}
