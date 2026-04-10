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

#include "sdk_test_utils.h"

#include <gtest/gtest.h>
#include <mega/filesystem.h>
#include <mega/mediafileattribute.h>

#include <array>
#include <megafs.h>

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
    ASSERT_EQ(exp.containerName, act.containerName);
    ASSERT_EQ(exp.videocodecNames, act.videocodecNames);
    ASSERT_EQ(exp.audiocodecNames, act.audiocodecNames);
    ASSERT_EQ(exp.containerFormat, act.containerFormat);
    ASSERT_EQ(exp.videocodecFormat, act.videocodecFormat);
    ASSERT_EQ(exp.audiocodecFormat, act.audiocodecFormat);
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
    mp.containerName = "MPEG-4";
    mp.videocodecNames = "AVC";
    mp.audiocodecNames = "AAC";
    mp.containerFormat = "mp42";
    mp.videocodecFormat = "avc1";
    mp.audiocodecFormat = "mp4a";

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

TEST(MediaProperties, deserialize_accepts_a_full_payload)
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
    mp.containerName = "MPEG-4";
    mp.videocodecNames = "AVC";
    mp.audiocodecNames = "AAC";
    mp.containerFormat = "mp42";
    mp.videocodecFormat = "avc1";
    mp.audiocodecFormat = "mp4a";

    mega::MediaProperties out;
    ASSERT_TRUE(mega::MediaProperties::deserialize(mp.serialize(), out));
    checkMediaProperties(mp, out);
}

TEST(MediaProperties, deserialize_rejects_a_truncated_payload)
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
    const auto full = mp.serialize();

    // Every proper prefix must be rejected, and must leave the output default-constructed
    // rather than partly filled - the fields are read in order, so a short buffer would
    // otherwise leave later ones holding whatever was on the stack.
    const mega::MediaProperties fresh;
    for (size_t n = 0; n < full.size(); ++n)
    {
        mega::MediaProperties out;
        out.shortformat = 99; // poison, to prove it is reset
        out.width = 12345;
        EXPECT_FALSE(mega::MediaProperties::deserialize(full.substr(0, n), out))
            << "accepted a payload truncated to " << n << " of " << full.size() << " bytes";
        checkMediaProperties(fresh, out);
    }
}

TEST(MediaProperties, deserialize_rejects_garbage)
{
    mega::MediaProperties out;
    EXPECT_FALSE(mega::MediaProperties::deserialize("", out));
    EXPECT_FALSE(mega::MediaProperties::deserialize(std::string(3, '\xff'), out));
    checkMediaProperties(mega::MediaProperties{}, out);
}

#ifdef USE_MEDIAINFO

// A file mediainfo cannot make sense of is not an extraction failure. The properties are left at
// their defaults so the caller can turn them into the "not identified" marker; reporting failure
// instead leaves the file with no media attribute at all, and then every later download of it
// extracts again, because the marker is what tells the client to stop trying.
TEST(MediaProperties, unidentifiable_file_extracts_successfully)
{
    // Larger than the 10 MB extractMediaPropertyFileAttributes allows itself to read, and random,
    // so the parser reaches that limit having identified nothing.
    static constexpr size_t SIZE_BEYOND_EXTRACTION_LIMIT = 11 * 1024 * 1024;
    const sdk_test::LocalTempFile file{std::filesystem::path{"unidentifiable_media.mp4"},
                                       SIZE_BEYOND_EXTRACTION_LIMIT};

    mega::FSACCESS_CLASS fsAccess;
    mega::MediaProperties mp;
    ASSERT_TRUE(mp.extractMediaPropertyFileAttributes(
        mega::LocalPath::fromAbsolutePath(std::filesystem::absolute(file.getPath()).string()),
        &fsAccess));

    checkMediaProperties(mega::MediaProperties{}, mp);

    // The caller is what turns those defaults into the marker.
    mega::MediaFileInfo mediaInfo;
    uint32_t fakey[4] = {1, 2, 3, 4};
    EXPECT_FALSE(mp.convertMediaPropertyFileAttributes(fakey, mediaInfo).empty());
    EXPECT_EQ(mp.shortformat, mega::MediaProperties::NOT_IDENTIFIED_FORMAT);
}

// Only a local file error is a real failure, and then nothing may be attached.
TEST(MediaProperties, unreadable_file_fails_extraction)
{
    mega::FSACCESS_CLASS fsAccess;
    mega::MediaProperties mp;
    EXPECT_FALSE(mp.extractMediaPropertyFileAttributes(
        mega::LocalPath::fromAbsolutePath(
            std::filesystem::absolute("no_such_media_file.mp4").string()),
        &fsAccess));

    checkMediaProperties(mega::MediaProperties{}, mp);
}

#endif // USE_MEDIAINFO
