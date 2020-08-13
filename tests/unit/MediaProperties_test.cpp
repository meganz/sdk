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

#include <array>

#include <gtest/gtest.h>

#include <mega/mediafileattribute.h>

namespace
{

void checkMediaProperties(const mega::MediaProperties& exp, const mega::MediaProperties& act)
{
    ASSERT_EQ(exp.shortformat, act.shortformat);
    ASSERT_EQ(exp.width, act.width);
    ASSERT_EQ(exp.height, act.height);
    ASSERT_EQ(exp.fps, act.fps);
    ASSERT_EQ(exp.playtime, act.playtime);
    ASSERT_EQ(exp.containerid, act.containerid);
    ASSERT_EQ(exp.videocodecid, act.videocodecid);
    ASSERT_EQ(exp.audiocodecid, act.audiocodecid);
    ASSERT_EQ(exp.is_VFR, act.is_VFR);
    ASSERT_EQ(exp.no_audio, act.no_audio);
}

}

TEST(MediaProperties, serialize_unserialize)
{
    mega::MediaProperties mp;
    mp.shortformat = 10;
    mp.width = 11;
    mp.height = 12;
    mp.fps = 13;
    mp.playtime = 14;
    mp.containerid = 15;
    mp.videocodecid = 16;
    mp.audiocodecid = 17;
    mp.is_VFR = true;
    mp.no_audio = true;

    const auto d = mp.serialize();

    const mega::MediaProperties newMp{d};
    checkMediaProperties(mp, newMp);
}

TEST(MediaProperties, unserialize_32bit)
{
    mega::MediaProperties mp;
    mp.shortformat = 10;
    mp.width = 11;
    mp.height = 12;
    mp.fps = 13;
    mp.playtime = 14;
    mp.containerid = 15;
    mp.videocodecid = 16;
    mp.audiocodecid = 17;
    mp.is_VFR = true;
    mp.no_audio = true;

    // This is the result of serialization on 32bit Windows
    const std::array<char, 39> rawData = {
        0x0a, 0x0b, 0x00, 0x00, 0x00, 0x0c, 0x00, 0x00, 0x00, 0x0d, 0x00, 0x00,
        0x00, 0x0e, 0x00, 0x00, 0x00, 0x0f, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00,
        0x00, 0x11, 0x00, 0x00, 0x00, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00
    };
    const std::string d(rawData.data(), rawData.size());

    const mega::MediaProperties newMp{d};
    checkMediaProperties(mp, newMp);
}
