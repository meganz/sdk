/**
 * @file MetaMacIntegrity_test.cpp
 * @brief Meta-MAC integrity core (SDK-6795), unit level.
 *
 * A node key's meta-MAC must fold the MAC of every chunk of the file. Before SDK-6795 an upload
 * could build the key from an incomplete set (chunkmac_map::macsmac folds whatever entries exist),
 * and the download-side tolerance hid many of those keys. These tests feed that broken builder
 * every gap shape we know of (the S4 field file, our retry/resume/corrupt-token repros, S4's
 * storage-red and HTTP cases, file-size edge cases, condensed prefixes) plus exhaustive and random
 * shapes, and check that a strict comparison always rejects the result.
 *
 * Chunk MACs are computed over a short buffer per chunk (distinct per chunk index): the meta-MAC
 * algebra does not depend on the chunk length, only on the chunk lattice (the keys) and the MACs.
 */

#include <gtest/gtest.h>
#include <mega/utils.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <random>
#include <set>
#include <string>
#include <vector>

namespace mega
{

namespace
{

std::unique_ptr<SymmCipher> integrityCipher()
{
    byte key[SymmCipher::KEYLENGTH];
    for (unsigned i = 0; i < sizeof(key); ++i)
    {
        key[i] = static_cast<byte>(i * 7 + 3);
    }
    return std::make_unique<SymmCipher>(key);
}

size_t chunkCountOf(m_off_t fileSize)
{
    size_t count = 0;
    for (m_off_t pos = 0; pos < fileSize; pos = ChunkedHash::chunkceil(pos, fileSize))
    {
        ++count;
    }
    return count;
}

// The upload's chunk MACs for a file: one finished MAC per chunk, each over its own content, so a
// chunk's MAC does not depend on the other chunks. `seed` changes the content of every chunk.
void addChunkMacs(chunkmac_map& cm,
                  SymmCipher& cipher,
                  m_off_t fileSize,
                  const std::set<size_t>& skip = {},
                  unsigned seed = 0)
{
    size_t index = 0;
    for (m_off_t pos = 0; pos < fileSize; pos = ChunkedHash::chunkceil(pos, fileSize), ++index)
    {
        byte buf[SymmCipher::BLOCKSIZE];
        for (size_t i = 0; i < sizeof(buf); ++i)
        {
            buf[i] = static_cast<byte>(index * 31 + i + seed);
        }
        if (!skip.count(index))
        {
            cm.ctr_encrypt(pos, &cipher, buf, sizeof(buf), pos, 0, true);
        }
    }
}

// Folds the gap-free prefix the way transfers do once a map holds more than 101 entries.
void condensePrefix(chunkmac_map& cm, SymmCipher& cipher, m_off_t fileSize)
{
    cm.updateContiguousProgress(fileSize);
    cm.updateMacsmacProgress(&cipher);
}

int64_t trueMetaMac(SymmCipher& cipher, m_off_t fileSize, unsigned seed = 0)
{
    chunkmac_map cm;
    addChunkMacs(cm, cipher, fileSize, {}, seed);
    return cm.macsmac(&cipher);
}

// The BROKEN key builder: what an upload did before SDK-6795 when the token arrived with chunk
// MACs missing. It folds whatever MACs exist, so its meta-MAC is wrong for any gap.
int64_t metaMacBuiltLikeBeforeTheFix(SymmCipher& cipher,
                                     m_off_t fileSize,
                                     const std::set<size_t>& missing,
                                     bool condensed = false)
{
    chunkmac_map cm;
    addChunkMacs(cm, cipher, fileSize, missing);
    if (condensed)
    {
        condensePrefix(cm, cipher, fileSize);
    }
    return cm.macsmac(&cipher);
}

std::set<size_t> chunkRange(size_t first, size_t end)
{
    std::set<size_t> result;
    for (size_t i = first; i < end; ++i)
    {
        result.insert(i);
    }
    return result;
}

std::set<size_t> unite(std::set<size_t> a, const std::set<size_t>& b)
{
    a.insert(b.begin(), b.end());
    return a;
}

std::string describe(const std::set<size_t>& chunks)
{
    std::string out;
    for (const auto c: chunks)
    {
        out += (out.empty() ? "" : ",") + std::to_string(c);
    }
    return "{" + out + "}";
}

constexpr m_off_t kFieldFile = 9930566; // 13 chunks: the S4 field file
constexpr m_off_t k64MiB = 64 * 1048576; // 68 chunks
constexpr m_off_t k200MiB = 200 * 1048576; // 204 chunks
constexpr m_off_t k110Chunks = 4718592 + 102 * 1048576;
constexpr m_off_t k300Chunks = 4718592 + 292 * 1048576;

struct GapShape
{
    const char* name;
    m_off_t fileSize;
    std::set<size_t> missing;
};

// Every gap shape we have seen or that a known mechanism can leave.
std::vector<GapShape> knownGapShapes()
{
    return {
        {"S4 field file: chunks 0 and 12", kFieldFile, {0, 12}},
        {"first chunk", kFieldFile, {0}},
        {"middle chunk", kFieldFile, {6}},
        {"last chunk", kFieldFile, {12}},
        {"resume with pending acks: missing tail", kFieldFile, chunkRange(9, 13)},
        {"retry: first 8 chunks only", kFieldFile, chunkRange(8, 13)},
        {"retry: one hole and missing tail", kFieldFile, unite({4}, chunkRange(9, 13))},
        {"retry: two holes and missing tail", kFieldFile, unite({3, 5}, chunkRange(10, 13))},
        {"retry: adjacent holes and missing tail", kFieldFile, unite({3, 4}, chunkRange(10, 13))},
        {"64 MiB retry: first 9 minus 4 and 5", k64MiB, unite({4, 5}, chunkRange(9, 68))},
        {"200 MiB retry: first 13 chunks", k200MiB, chunkRange(13, 204)},
        {"200 MiB retry: first 23 chunks", k200MiB, chunkRange(23, 204)},
        {"storage red: one mid-file hole", k200MiB, {100}},
        {"storage red: two adjacent mid-file holes", k200MiB, {50, 51}},
        {"HTTP: in-flight requests near the end", k300Chunks, chunkRange(280, 288)},
        {"one-chunk file", 100000, {0}},
        {"two-chunk file: second chunk", 131073, {1}},
        {"one-byte last chunk", 4718593, {8}},
    };
}

// Random retry-like shapes on large files: a missing tail plus up to three holes below it.
std::vector<GapShape> randomGapShapes()
{
    std::vector<GapShape> shapes;
    std::mt19937 rng(6795);
    for (const m_off_t fileSize: {k64MiB, k200MiB})
    {
        const size_t chunks = chunkCountOf(fileSize);
        for (int i = 0; i < 200; ++i)
        {
            const size_t tailStart = std::uniform_int_distribution<size_t>(1, chunks)(rng);
            auto missing = chunkRange(tailStart, chunks);
            const int holes = std::uniform_int_distribution<int>(0, 3)(rng);
            std::uniform_int_distribution<size_t> below(0, tailStart - 1);
            for (int h = 0; h < holes || missing.empty(); ++h)
            {
                missing.insert(below(rng));
            }
            shapes.push_back({"random", fileSize, std::move(missing)});
        }
    }
    return shapes;
}

// The position right after the first n chunks (chunk sizes grow from 128 KiB to 1 MiB).
m_off_t chunkEnd(size_t n)
{
    m_off_t pos = 0;
    for (size_t i = 0; i < n; ++i)
    {
        pos = ChunkedHash::chunkceil(pos);
    }
    return pos;
}

// Every chunk count from 1 to 13, each ending in the three ways a file can end on the chunk
// lattice: a full last chunk, a 1-byte last chunk and a half-full one. Then the S4 field file
// (13 chunks).
std::vector<m_off_t> smallFileSizes()
{
    std::vector<m_off_t> sizes;
    for (size_t n = 1; n <= 13; ++n)
    {
        const m_off_t lastStart = chunkEnd(n - 1);
        const m_off_t lastEnd = chunkEnd(n);
        sizes.push_back(lastEnd);
        sizes.push_back(lastStart + 1);
        sizes.push_back(lastStart + (lastEnd - lastStart) / 2);
    }
    sizes.push_back(kFieldFile);
    return sizes;
}

std::set<size_t> chunksOfMask(unsigned mask, size_t chunks)
{
    std::set<size_t> result;
    for (size_t i = 0; i < chunks; ++i)
    {
        if (mask & (1u << i))
        {
            result.insert(i);
        }
    }
    return result;
}

} // namespace

// The fold is deterministic, and it depends on the content: the strict check has a real oracle.
TEST(MetaMacIntegrity, CompleteMacsGiveOneMetaMacPerContent)
{
    auto cipherPtr = integrityCipher();
    SymmCipher& cipher = *cipherPtr;
    EXPECT_EQ(trueMetaMac(cipher, kFieldFile), trueMetaMac(cipher, kFieldFile));
    EXPECT_NE(trueMetaMac(cipher, kFieldFile), trueMetaMac(cipher, kFieldFile, 1));
    EXPECT_EQ(chunkCountOf(kFieldFile), 13u);
    EXPECT_EQ(chunkCountOf(k64MiB), 68u);
    EXPECT_EQ(chunkCountOf(k200MiB), 204u);
    EXPECT_EQ(chunkCountOf(k110Chunks), 110u);
    EXPECT_EQ(chunkCountOf(k300Chunks), 300u);
    EXPECT_EQ(chunkCountOf(131073), 2u);
    EXPECT_EQ(chunkCountOf(4718593), 9u);
}

TEST(MetaMacIntegrity, KeyBuiltLikeBeforeTheFixIsWrongForEveryKnownShape)
{
    auto cipherPtr = integrityCipher();
    SymmCipher& cipher = *cipherPtr;
    for (const auto& shape: knownGapShapes())
    {
        ASSERT_LT(*shape.missing.rbegin(), chunkCountOf(shape.fileSize)) << shape.name;
        EXPECT_NE(metaMacBuiltLikeBeforeTheFix(cipher, shape.fileSize, shape.missing),
                  trueMetaMac(cipher, shape.fileSize))
            << shape.name << ": a gap key passed the strict check";
    }
}

// Every non-empty set of missing chunks for files of 1 to 13 chunks, at each of the three ways the
// last chunk can end, and for the S4 field file: 3 x 16,369 + 8,191 = 57,298 shapes, the S4 field
// file's chunks 0 and 12 among them.
TEST(MetaMacIntegrity, EveryGapOfSmallFilesIsCaught)
{
    auto cipherPtr = integrityCipher();
    SymmCipher& cipher = *cipherPtr;
    const auto sizes = smallFileSizes();
    ASSERT_EQ(sizes.size(), 40u);
    size_t shapes = 0;
    for (size_t i = 0; i < sizes.size(); ++i)
    {
        const m_off_t fileSize = sizes[i];
        const size_t chunks = chunkCountOf(fileSize);
        ASSERT_EQ(chunks, i < 39 ? i / 3 + 1 : 13) << "size " << fileSize;
        const auto expected = trueMetaMac(cipher, fileSize);
        for (unsigned mask = 1; mask < (1u << chunks); ++mask)
        {
            const auto missing = chunksOfMask(mask, chunks);
            EXPECT_NE(metaMacBuiltLikeBeforeTheFix(cipher, fileSize, missing), expected)
                << "size " << fileSize << " missing " << describe(missing);
            ++shapes;
        }
    }
    EXPECT_EQ(shapes, 57298u);
}

TEST(MetaMacIntegrity, RandomGapShapesAreCaught)
{
    auto cipherPtr = integrityCipher();
    SymmCipher& cipher = *cipherPtr;
    const auto expected64 = trueMetaMac(cipher, k64MiB);
    const auto expected200 = trueMetaMac(cipher, k200MiB);
    for (const auto& shape: randomGapShapes())
    {
        EXPECT_NE(metaMacBuiltLikeBeforeTheFix(cipher, shape.fileSize, shape.missing),
                  shape.fileSize == k64MiB ? expected64 : expected200)
            << "size " << shape.fileSize << " missing " << describe(shape.missing);
    }
}

// Maps of more than 101 entries fold their gap-free prefix; folding must not change the meta-MAC
// of a complete map, and holes after the prefix must still make a wrong one.
TEST(MetaMacIntegrity, HolesAfterACondensedPrefixAreCaught)
{
    auto cipherPtr = integrityCipher();
    SymmCipher& cipher = *cipherPtr;
    for (const m_off_t fileSize: {k110Chunks, k200MiB})
    {
        chunkmac_map complete;
        addChunkMacs(complete, cipher, fileSize);
        const auto expected = complete.macsmac(&cipher);
        condensePrefix(complete, cipher, fileSize);
        ASSERT_LT(complete.size(), chunkCountOf(fileSize)) << "the prefix was not condensed";
        EXPECT_EQ(complete.macsmac(&cipher), expected) << "condensing changed the meta-MAC";

        const size_t last = chunkCountOf(fileSize) - 1;
        for (const auto& missing: {std::set<size_t>{last - 4},
                                   std::set<size_t>{last - 9, last - 8},
                                   chunkRange(last - 13, last + 1)})
        {
            EXPECT_NE(metaMacBuiltLikeBeforeTheFix(cipher, fileSize, missing, true), expected)
                << "size " << fileSize << " missing " << describe(missing);
        }
    }
}

// Why our tests must be strict: the apps' download tolerance (checkMetaMacWithMissingLateEntries,
// via macsmac_gaps) reproduces these gap keys and accepts them, although they are wrong.
TEST(MetaMacIntegrity, AppToleranceAcceptsGapKeysTheStrictCheckRejects)
{
    auto cipherPtr = integrityCipher();
    SymmCipher& cipher = *cipherPtr;
    chunkmac_map downloaded;
    addChunkMacs(downloaded, cipher, kFieldFile);
    const auto expected = downloaded.macsmac(&cipher);

    struct Case
    {
        std::set<size_t> missing;
        size_t g1, g2, g3, g4; // the tolerance's gap bounds (map positions, no condensed prefix)
    };

    const Case cases[] = {
        {{12}, 12, 13, 13, 13},
        {chunkRange(9, 13), 9, 13, 13, 13},
        {{0, 12}, 0, 1, 12, 13}, // the S4 field file
        {unite({4}, chunkRange(9, 13)), 4, 5, 9, 13},
    };
    for (const auto& c: cases)
    {
        const auto gapKey = metaMacBuiltLikeBeforeTheFix(cipher, kFieldFile, c.missing);
        EXPECT_EQ(downloaded.macsmac_gaps(&cipher, c.g1, c.g2, c.g3, c.g4), gapKey)
            << describe(c.missing) << ": the tolerance would not match this gap key";
        EXPECT_NE(gapKey, expected) << describe(c.missing);
    }
}

} // namespace mega
