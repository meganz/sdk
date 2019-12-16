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

#include <mega/pendingcontactrequest.h>

namespace
{

void checkPcrs(const mega::PendingContactRequest& exp, const mega::PendingContactRequest& act)
{
    ASSERT_EQ(exp.id, act.id);
    ASSERT_EQ(exp.originatoremail, act.originatoremail);
    ASSERT_EQ(exp.targetemail, act.targetemail);
    ASSERT_EQ(exp.ts, act.ts);
    ASSERT_EQ(exp.uts, act.uts);
    ASSERT_EQ(exp.msg, act.msg);
    ASSERT_EQ(exp.isoutgoing, act.isoutgoing);
}

}

TEST(PendingContactRequest, serialize_unserialize)
{
    mega::PendingContactRequest pcr{1, "blah", "foo", 2, 3, "hello", true};

    std::string d;
    ASSERT_TRUE(pcr.serialize(&d));

    auto newPcr = std::unique_ptr<mega::PendingContactRequest>{mega::PendingContactRequest::unserialize(&d)};
    checkPcrs(pcr, *newPcr);
}
