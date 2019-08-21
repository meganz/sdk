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
#include <memory>
#include <numeric>

#include <gtest/gtest.h>

#include <mega/filefingerprint.h>

#include "DefaultedFileAccess.h"

namespace {

template<size_t N>
std::array<int32_t, N> toArr(const int32_t (&values)[N])
{
    std::array<int32_t, N> array;
    for (size_t i = 0; i < N; ++i)
    {
        array[i] = values[i];
    }
    return array;
}

class MockFileAccess : public mt::DefaultedFileAccess
{
public:
    MockFileAccess(const mega::m_time_t mtime, std::vector<mega::byte> content, const bool readFails = false)
    : mContent{std::move(content)}
    , mReadFails{readFails}
    {
        this->size = mContent.size();
        this->mtime = mtime;
    }

    MEGA_DISABLE_COPY_MOVE(MockFileAccess)

    bool frawread(mega::byte* buffer, const unsigned size, const m_off_t offset) override
    {
        if (mReadFails)
        {
            return false;
        }
        assert(static_cast<unsigned>(offset) + size <= mContent.size());
        std::copy(mContent.begin() + static_cast<unsigned>(offset), mContent.begin() + static_cast<unsigned>(offset) + size, buffer);
        return true;
    }

    bool getReadFails() const
    {
        return mReadFails;
    }

private:
    const std::vector<mega::byte> mContent;
    const bool mReadFails = false;
};

class MockInputStreamAccess : public mega::InputStreamAccess
{
public:
    MockInputStreamAccess(const mega::m_time_t mtime, std::vector<mega::byte> content, const bool readFails = false)
    : mFa{mtime, std::move(content), readFails}
    {}

    mega::m_time_t getMTime() const
    {
        return mFa.mtime;
    }

    void setSize(const m_off_t size)
    {
        mFa.size = size;
    }

    m_off_t size() override
    {
        return mFa.size;
    }

    bool read(mega::byte* buffer, const unsigned size) override
    {
        if (mFa.getReadFails())
        {
            return false;
        }
        if (!buffer)
        {
            return true;
        }
        return mFa.frawread(buffer, size, 0);
    }

private:
    MockFileAccess mFa;
};

} // anonymous

TEST(FileFingerprint, FileFingerprintCmp_compareNotSmaller)
{
    mega::FileFingerprint ffp;
    ffp.size = 1;
    ffp.mtime = 2;
    std::iota(ffp.crc, ffp.crc +  sizeof(ffp.crc) / sizeof(*ffp.crc), 3);
    ffp.isvalid = true;

    mega::FileFingerprint copiedFfp;
    copiedFfp = ffp;

    ASSERT_FALSE(mega::FileFingerprintCmp{}(&ffp, &copiedFfp));
}

TEST(FileFingerprint, FileFingerprintCmp_compareSmallerBecauseOfSize)
{
    mega::FileFingerprint ffp;
    ffp.size = 1;

    mega::FileFingerprint ffp2;
    ffp2.size = 2;

    ASSERT_TRUE(mega::FileFingerprintCmp{}(&ffp, &ffp2));
}

TEST(FileFingerprint, FileFingerprintCmp_compareNotSmallerBecauseOfSize)
{
    mega::FileFingerprint ffp;
    ffp.size = 2;

    mega::FileFingerprint ffp2;
    ffp2.size = 1;

    ASSERT_FALSE(mega::FileFingerprintCmp{}(&ffp, &ffp2));
}

TEST(FileFingerprint, FileFingerprintCmp_compareSmallerBecauseOfMTime)
{
    mega::FileFingerprint ffp;
    ffp.mtime = 1;

    mega::FileFingerprint ffp2;
    ffp2.mtime = 2;

    ASSERT_TRUE(mega::FileFingerprintCmp{}(&ffp, &ffp2));
}

TEST(FileFingerprint, FileFingerprintCmp_compareNotSmallerBecauseOfMTime)
{
    mega::FileFingerprint ffp;
    ffp.mtime = 2;

    mega::FileFingerprint ffp2;
    ffp2.mtime = 1;

    ASSERT_FALSE(mega::FileFingerprintCmp{}(&ffp, &ffp2));
}

TEST(FileFingerprint, FileFingerprintCmp_compareSmallerBecauseOfCrc)
{
    mega::FileFingerprint ffp;
    ffp.crc[0] = 1;

    mega::FileFingerprint ffp2;
    ffp2.crc[0] = 2;

    ASSERT_TRUE(mega::FileFingerprintCmp{}(&ffp, &ffp2));
}

TEST(FileFingerprint, defaultConstructor)
{
    const mega::FileFingerprint ffp;
    ASSERT_EQ(-1, ffp.size);
    ASSERT_EQ(0, ffp.mtime);
    const std::array<int32_t, 4> expected = {0, 0, 0, 0};
    ASSERT_EQ(expected, toArr(ffp.crc));
    ASSERT_EQ(false, ffp.isvalid);
}

TEST(FileFingerprint, copyAssignment)
{
    mega::FileFingerprint ffp;
    ffp.size = 1;
    ffp.mtime = 2;
    std::iota(ffp.crc, ffp.crc +  sizeof(ffp.crc) / sizeof(*ffp.crc), 3);
    ffp.isvalid = true;

    mega::FileFingerprint copiedFfp;
    copiedFfp = ffp;

    ASSERT_EQ(copiedFfp.size, ffp.size);
    ASSERT_EQ(copiedFfp.mtime, ffp.mtime);
    ASSERT_EQ(toArr(copiedFfp.crc), toArr(ffp.crc));
    ASSERT_EQ(copiedFfp.isvalid, ffp.isvalid);
}

TEST(FileFingerprint, comparisonOperator_compareEqual)
{
    mega::FileFingerprint ffp;
    ffp.size = 1;
    ffp.mtime = 2;
    std::iota(ffp.crc, ffp.crc +  sizeof(ffp.crc) / sizeof(*ffp.crc), 3);
    ffp.isvalid = true;

    mega::FileFingerprint copiedFfp;
    copiedFfp = ffp;

    ASSERT_TRUE(ffp == copiedFfp);
}

TEST(FileFingerprint, comparisonOperator_compareNotEqualBecauseOfSize)
{
    mega::FileFingerprint ffp;
    ffp.isvalid = true;
    ffp.size = 1;

    mega::FileFingerprint ffp2;
    ffp2.isvalid = true;

    ASSERT_FALSE(ffp == ffp2);
}

#ifndef __ANDROID__
#ifndef WINDOWS_PHONE
TEST(FileFingerprint, comparisonOperator_compareNotEqualBecauseOfMTime)
{
    mega::FileFingerprint ffp;
    ffp.isvalid = true;
    ffp.mtime = 3; // difference must be at least 3

    mega::FileFingerprint ffp2;
    ffp2.isvalid = true;

    ASSERT_FALSE(ffp == ffp2);
}
#endif
#endif

TEST(FileFingerprint, comparisonOperator_compareNotEqualBecauseOfValid)
{
    mega::FileFingerprint ffp;
    ffp.isvalid = false;

    mega::FileFingerprint ffp2;
    ffp2.isvalid = true;

    ASSERT_TRUE(ffp == ffp2);
}

TEST(FileFingerprint, comparisonOperator_compareNotEqualBecauseOfCrc)
{
    mega::FileFingerprint ffp;
    ffp.isvalid = true;
    ffp.crc[0] = 1;

    mega::FileFingerprint ffp2;
    ffp2.isvalid = true;

    ASSERT_FALSE(ffp == ffp2);
}

TEST(FileFingerprint, serialize_unserialize)
{
    mega::FileFingerprint ffp;
    ffp.size = 1;
    ffp.mtime = 2;
    std::iota(ffp.crc, ffp.crc +  sizeof(ffp.crc) / sizeof(*ffp.crc), 3);
    ffp.isvalid = true;

    std::string data;
    ASSERT_TRUE(ffp.serialize(&data));
    auto ffp2 = std::unique_ptr<mega::FileFingerprint>{mega::FileFingerprint::unserialize(&data)};

    ASSERT_EQ(ffp2->size, ffp.size);
    ASSERT_EQ(ffp2->mtime, ffp.mtime);
    ASSERT_EQ(toArr(ffp2->crc), toArr(ffp.crc));
    ASSERT_EQ(ffp2->isvalid, ffp.isvalid);
}

TEST(FileFingerprint, unserialize_butStringTooShort)
{
    std::string data = "blah";
    ASSERT_EQ(nullptr, mega::FileFingerprint::unserialize(&data));
}

TEST(FileFingerprint, serializefingerprint_unserializefingerprint)
{
    mega::FileFingerprint ffp;
    ffp.size = 1;
    ffp.mtime = 2;
    std::iota(ffp.crc, ffp.crc +  sizeof(ffp.crc) / sizeof(*ffp.crc), 3);
    ffp.isvalid = true;

    std::string data;
    ffp.serializefingerprint(&data);
    mega::FileFingerprint ffp2;
    ASSERT_TRUE(ffp2.unserializefingerprint(&data));

    ASSERT_EQ(ffp2.size, -1); // it is not clear why `size` is dealed with
    ASSERT_EQ(ffp2.mtime, ffp.mtime);
    ASSERT_EQ(toArr(ffp2.crc), toArr(ffp.crc));
    ASSERT_EQ(ffp2.isvalid, ffp.isvalid);
}

TEST(FileFingerprint, genfingerprint_FileAccess_forTinyFile)
{
    mega::FileFingerprint ffp;
    MockFileAccess fa{1, {3, 4, 5, 6}};
    ASSERT_TRUE(ffp.genfingerprint(&fa));
    ASSERT_EQ(4, ffp.size);
    ASSERT_EQ(1, ffp.mtime);
    const std::array<int32_t, 4> expected = {100992003, 0, 0, 0};
    ASSERT_EQ(expected, toArr(ffp.crc));
    ASSERT_EQ(true, ffp.isvalid);
}

TEST(FileFingerprint, genfingerprint_FileAccess_forTinyFile_butReadFails)
{
    mega::FileFingerprint ffp;
    MockFileAccess fa{1, {3, 4, 5, 6}, true};
    ASSERT_TRUE(ffp.genfingerprint(&fa));
    ASSERT_EQ(-1, ffp.size);
    ASSERT_EQ(1, ffp.mtime);
    const std::array<int32_t, 4> expected = {0, 0, 0, 0};
    ASSERT_EQ(expected, toArr(ffp.crc));
    ASSERT_EQ(false, ffp.isvalid);
}

TEST(FileFingerprint, genfingerprint_FileAccess_forSmallFile)
{
    mega::FileFingerprint ffp;
    std::vector<mega::byte> content(100);
    std::iota(content.begin(), content.end(), mega::byte{0});
    MockFileAccess fa{1, std::move(content)};
    ASSERT_TRUE(ffp.genfingerprint(&fa));
    ASSERT_EQ(100, ffp.size);
    ASSERT_EQ(1, ffp.mtime);
    const std::array<int32_t, 4> expected = {215253208, 661795201, 937191950, 562141813};
    ASSERT_EQ(expected, toArr(ffp.crc));
    ASSERT_EQ(true, ffp.isvalid);
}

TEST(FileFingerprint, genfingerprint_FileAccess_forSmallFile_butReadFails)
{
    mega::FileFingerprint ffp;
    std::vector<mega::byte> content(100);
    std::iota(content.begin(), content.end(), mega::byte{0});
    MockFileAccess fa{1, std::move(content), true};
    ASSERT_TRUE(ffp.genfingerprint(&fa));
    ASSERT_EQ(-1, ffp.size);
    ASSERT_EQ(1, ffp.mtime);
    const std::array<int32_t, 4> expected = {0, 0, 0, 0};
    ASSERT_EQ(expected, toArr(ffp.crc));
    ASSERT_EQ(false, ffp.isvalid);
}

TEST(FileFingerprint, genfingerprint_FileAccess_forLargeFile)
{
    mega::FileFingerprint ffp;
    std::vector<mega::byte> content(20000);
    std::iota(content.begin(), content.end(), mega::byte{0});
    MockFileAccess fa{1, std::move(content)};
    ASSERT_TRUE(ffp.genfingerprint(&fa));
    ASSERT_EQ(20000, ffp.size);
    ASSERT_EQ(1, ffp.mtime);
    const std::array<int32_t, 4> expected = {-1424885571, 1204627086, 1194313128, -177560448};
    ASSERT_EQ(expected, toArr(ffp.crc));
    ASSERT_EQ(true, ffp.isvalid);
}

TEST(FileFingerprint, genfingerprint_FileAccess_forLargeFile_butReadFails)
{
    mega::FileFingerprint ffp;
    std::vector<mega::byte> content(20000);
    std::iota(content.begin(), content.end(), mega::byte{0});
    MockFileAccess fa{1, std::move(content), true};
    ASSERT_TRUE(ffp.genfingerprint(&fa));
    ASSERT_EQ(-1, ffp.size);
    ASSERT_EQ(1, ffp.mtime);
    const std::array<int32_t, 4> expected = {0, 0, 0, 0};
    ASSERT_EQ(expected, toArr(ffp.crc));
    ASSERT_EQ(false, ffp.isvalid);
}

TEST(FileFingerprint, genfingerprint_InputStreamAccess_forTinyFile)
{
    mega::FileFingerprint ffp;
    MockInputStreamAccess is{1, {3, 4, 5, 6}};
    ASSERT_TRUE(ffp.genfingerprint(&is, is.getMTime()));
    ASSERT_EQ(4, ffp.size);
    ASSERT_EQ(1, ffp.mtime);
    const std::array<int32_t, 4> expected = {100992003, 0, 0, 0};
    ASSERT_EQ(expected, toArr(ffp.crc));
    ASSERT_EQ(true, ffp.isvalid);
}

TEST(FileFingerprint, genfingerprint_InputStreamAccess_forTinyFile_butReadFails)
{
    mega::FileFingerprint ffp;
    MockInputStreamAccess is{1, {3, 4, 5, 6}, true};
    ASSERT_TRUE(ffp.genfingerprint(&is, is.getMTime()));
    ASSERT_EQ(-1, ffp.size);
    ASSERT_EQ(1, ffp.mtime);
    const std::array<int32_t, 4> expected = {0, 0, 0, 0};
    ASSERT_EQ(expected, toArr(ffp.crc));
    ASSERT_EQ(false, ffp.isvalid);
}

TEST(FileFingerprint, genfingerprint_InputStreamAccess_forTinyFile_butSizeNegative)
{
    mega::FileFingerprint ffp;
    MockInputStreamAccess is{1, {3, 4, 5, 6}};
    is.setSize(-1);
    ASSERT_TRUE(ffp.genfingerprint(&is, is.getMTime()));
    ASSERT_EQ(-1, ffp.size);
    ASSERT_EQ(1, ffp.mtime);
    const std::array<int32_t, 4> expected = {0, 0, 0, 0};
    ASSERT_EQ(expected, toArr(ffp.crc));
    ASSERT_EQ(false, ffp.isvalid);
}

TEST(FileFingerprint, genfingerprint_InputStreamAccess_forSmallFile)
{
    mega::FileFingerprint ffp;
    std::vector<mega::byte> content(100);
    std::iota(content.begin(), content.end(), mega::byte{0});
    MockInputStreamAccess is{1, std::move(content)};
    ASSERT_TRUE(ffp.genfingerprint(&is, is.getMTime()));
    ASSERT_EQ(100, ffp.size);
    ASSERT_EQ(1, ffp.mtime);
    const std::array<int32_t, 4> expected = {215253208, 661795201, 937191950, 562141813};
    ASSERT_EQ(expected, toArr(ffp.crc));
    ASSERT_EQ(true, ffp.isvalid);
}

TEST(FileFingerprint, genfingerprint_InputStreamAccess_forSmallFile_butReadFails)
{
    mega::FileFingerprint ffp;
    std::vector<mega::byte> content(100);
    std::iota(content.begin(), content.end(), mega::byte{0});
    MockInputStreamAccess is{1, std::move(content), true};
    ASSERT_TRUE(ffp.genfingerprint(&is, is.getMTime()));
    ASSERT_EQ(-1, ffp.size);
    ASSERT_EQ(1, ffp.mtime);
    const std::array<int32_t, 4> expected = {0, 0, 0, 0};
    ASSERT_EQ(expected, toArr(ffp.crc));
    ASSERT_EQ(false, ffp.isvalid);
}

TEST(FileFingerprint, genfingerprint_InputStreamAccess_forLargeFile)
{
    mega::FileFingerprint ffp;
    std::vector<mega::byte> content(20000);
    std::iota(content.begin(), content.end(), mega::byte{0});
    MockInputStreamAccess is{1, std::move(content)};
    ASSERT_TRUE(ffp.genfingerprint(&is, is.getMTime()));
    ASSERT_EQ(20000, ffp.size);
    ASSERT_EQ(1, ffp.mtime);
    const std::array<int32_t, 4> expected = {-1236811658, -1236811658, -1236811658, -1236811658};
    ASSERT_EQ(expected, toArr(ffp.crc));
    ASSERT_EQ(true, ffp.isvalid);
}

TEST(FileFingerprint, genfingerprint_InputStreamAccess_forLargeFile_butReadFails)
{
    mega::FileFingerprint ffp;
    std::vector<mega::byte> content(20000);
    std::iota(content.begin(), content.end(), mega::byte{0});
    MockInputStreamAccess is{1, std::move(content), true};
    ASSERT_TRUE(ffp.genfingerprint(&is, is.getMTime()));
    ASSERT_EQ(-1, ffp.size);
    ASSERT_EQ(1, ffp.mtime);
    const std::array<int32_t, 4> expected = {0, 0, 0, 0};
    ASSERT_EQ(expected, toArr(ffp.crc));
    ASSERT_EQ(false, ffp.isvalid);
}

TEST(FileFingerprint, getHash)
{
    mega::FileFingerprint ffp;
    ffp.size = 1;
    ffp.mtime = 2;
    std::iota(ffp.crc, ffp.crc +  sizeof(ffp.crc) / sizeof(*ffp.crc), 3);
    ffp.isvalid = true;
    const auto hash = ffp.getHash();
    ASSERT_EQ(sizeof(hash) == 4 ? 2056764164u : 3005401618104503162u, hash);
}
