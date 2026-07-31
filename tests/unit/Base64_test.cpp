/**
 * @file Base64_test.cpp
 * @brief Unit tests for base64 encoding helpers
 *
 * (c) 2026 by Mega Limited, Auckland, New Zealand
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
#include <mega/base64.h>
#include <mega/crypto/cryptopp.h>

#include <cstring>
#include <string>
#include <vector>

using namespace mega;

namespace
{
// Number of characters btoa writes for a binlen-byte input, excluding the trailing NUL.
// Four output characters per three input bytes, with a partial final group of two characters
// for one leftover byte and three characters for two leftover bytes.
size_t exactEncodedLength(const size_t binlen)
{
    const size_t groups = binlen / 3;
    const size_t rem = binlen % 3;
    return groups * 4 + (rem == 0 ? 0 : (rem == 1 ? 2 : 3));
}

// Deterministic pseudo-random bytes so failures are reproducible.
std::vector<byte> makeBytes(const size_t n)
{
    std::vector<byte> out(n);
    unsigned state = 0x1234567u;
    for (size_t i = 0; i < n; i++)
    {
        state = state * 1103515245u + 12345u;
        out[i] = static_cast<byte>(state >> 16);
    }
    return out;
}
}

// btoaSize must never be smaller than what btoa actually writes, including the NUL terminator, so
// it is safe to use for sizing a destination buffer before calling btoa.
TEST(Base64, btoaSizeIsUpperBoundIncludingNul)
{
    for (size_t binlen = 0; binlen <= 4096; binlen++)
    {
        const size_t needed = exactEncodedLength(binlen) + 1; // + NUL
        EXPECT_GE(Base64::btoaSize(binlen), needed)
            << "btoaSize underestimates the buffer for binlen=" << binlen;
    }
}

// Encoding into a destination sized by btoaSize (resize(btoaSize(n)) then resize(btoa(...))) must
// write exactly the expected number of characters and always stay within the destination, checked
// across small sizes, the three input-length residue classes and the largest key-sized inputs
// (ASAN catches any overrun).
TEST(Base64, btoaIntoBufferSizedByBtoaSize)
{
    const std::vector<size_t> sizes{
        0, // empty input
        1, // one leftover byte, encodes to two characters
        2, // two leftover bytes, encode to three characters
        3, // one full group, encodes to four characters
        4, // one full group plus one leftover byte
        16, // an AES block multiple
        17, // one byte past an AES block multiple
        255, // one byte below a single byte length boundary
        256, // a single byte length boundary
        1539, // encodes to exactly 2052 characters
        1540, // first input whose encoded output exceeds 2052 characters
        2048, // a block aligned input near the largest key size
        AsymmCipher::MAXKEYLENGTH *
            2, // the largest length storebinary can return into a key buffer
    };

    for (const size_t n: sizes)
    {
        const std::vector<byte> bin = makeBytes(n);

        std::string encoded;
        encoded.resize(Base64::btoaSize(n));
        const size_t written = Base64::btoa(bin.data(), n, encoded.data());
        encoded.resize(written);

        EXPECT_EQ(written, exactEncodedLength(n)) << "unexpected encoded length for n=" << n;
        EXPECT_LT(written, Base64::btoaSize(n))
            << "btoa write plus NUL did not fit the btoaSize buffer for n=" << n;
    }
}

// Encoding into a btoaSize-sized buffer and decoding back returns the original bytes, confirming
// btoaSize does not truncate the output.
TEST(Base64, btoaSizeReencodeRoundTrips)
{
    for (const size_t n: {size_t{16}, size_t{256}, size_t{2048}})
    {
        const std::vector<byte> bin = makeBytes(n);

        std::string encoded;
        encoded.resize(Base64::btoaSize(n));
        encoded.resize(Base64::btoa(bin.data(), n, encoded.data()));

        std::string decoded;
        Base64::atob(encoded, decoded);

        ASSERT_EQ(decoded.size(), n) << "round trip changed the length for n=" << n;
        EXPECT_EQ(0, memcmp(decoded.data(), bin.data(), n))
            << "round trip changed the content for n=" << n;
    }
}
