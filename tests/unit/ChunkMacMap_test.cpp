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

#include <mega/utils.h>

#include <cstring>
#include <string>
#include <vector>

namespace mega {

namespace {

// S16 Gate-1: populate a chunkmac_map exactly the way the WS upload path does — whole-chunk
// ctr_encrypt at canonical ChunkedHash boundaries (ctr_encrypt asserts chunkid == startpos;
// "encrypt is always done on whole chunks").
void populateCanonical(chunkmac_map& cm, SymmCipher& cipher, m_off_t fileSize, int64_t ctriv)
{
    std::vector<byte> buf;
    m_off_t pos = 0;
    while (pos < fileSize)
    {
        const m_off_t next = ChunkedHash::chunkceil(pos, fileSize);
        const unsigned len = static_cast<unsigned>(next - pos);
        buf.assign(len, static_cast<byte>(0x5a ^ (pos & 0xff)));
        cm.ctr_encrypt(pos, &cipher, buf.data(), len, pos, ctriv, true);
        pos = next;
    }
}

} // namespace

// S16 Gate-1 (binding validation contract): the meta-MAC must survive a statecache
// round-trip byte-exactly — serialize -> unserialize -> macsmac must equal the original
// macsmac for an on-lattice map (the invariant the S15 round-5 clamp silently broke by
// re-partitioning the fold).
TEST(ChunkMacMap, MacsmacSurvivesSerializeRoundTrip)
{
    byte key[SymmCipher::KEYLENGTH];
    for (unsigned i = 0; i < sizeof(key); ++i)
        key[i] = static_cast<byte>(i * 7 + 3);
    const int64_t ctriv = 0x0123456789abcdefLL;

    // Sizes straddling the ramp knee + the CI G-cell sizes.
    const m_off_t sizes[] = {131072, 262144, 3670016, 5242879, 5242880};
    for (const m_off_t fileSize: sizes)
    {
        SymmCipher cipherA(key);
        chunkmac_map original;
        populateCanonical(original, cipherA, fileSize, ctriv);

        std::string blob;
        original.serialize(blob);

        chunkmac_map restored;
        const char* ptr = blob.data();
        ASSERT_TRUE(restored.unserialize(ptr, blob.data() + blob.size()))
            << "fileSize=" << fileSize;

        SymmCipher cipherB(key);
        SymmCipher cipherC(key);
        const int64_t before = original.macsmac(&cipherB);
        const int64_t after = restored.macsmac(&cipherC);
        EXPECT_EQ(before, after) << "fileSize=" << fileSize;
    }
}

// S16: unserialize must REJECT (Debug assert) a lattice-poisoned statecache blob — an
// off-canonical chunk key yields a wrong meta-MAC (API_EKEY on every download), so it must
// detonate at resume, not at download. Release builds accept the blob silently (the assert
// compiles out), which is exactly why the WS path also guards at nextChunk.
#ifndef NDEBUG
TEST(ChunkMacMapDeathTest, UnserializeRejectsOffLatticeKey)
{
    // Serialize a legal 1-chunk map (pos 0), then PATCH the stored pos to 262144 — NOT a
    // lattice point (chunkfloor(262144) == 131072; the exact key from the S15 round-6
    // Windows CI crash). Blob layout: unsigned short count, then (m_off_t pos + raw
    // ChunkMAC) records.
    byte key[SymmCipher::KEYLENGTH];
    for (unsigned i = 0; i < sizeof(key); ++i)
        key[i] = static_cast<byte>(i + 1);
    SymmCipher cipher(key);

    chunkmac_map legal;
    std::vector<byte> buf(131072, 0x42);
    legal.ctr_encrypt(0, &cipher, buf.data(), static_cast<unsigned>(buf.size()), 0,
                      0x1122334455667788LL, true);

    std::string blob;
    legal.serialize(blob);
    ASSERT_GT(blob.size(), sizeof(unsigned short) + sizeof(m_off_t));

    const m_off_t badPos = 262144;
    std::memcpy(&blob[sizeof(unsigned short)], &badPos, sizeof(badPos));

    chunkmac_map cm;
    const char* ptr = blob.data();
    EXPECT_DEATH((void)cm.unserialize(ptr, blob.data() + blob.size()),
                 "off the canonical chunk lattice");
}
#endif

}


