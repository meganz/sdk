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

// ---------------------------------------------------------------------------------------------
// The completeness check (SDK-6795 fix): an upload must refuse to build a node key unless the
// chunk MACs cover the whole file. It must refuse every shape above and accept every complete map.
// ---------------------------------------------------------------------------------------------

namespace
{

m_off_t chunkStartOf(m_off_t fileSize, size_t index)
{
    m_off_t pos = 0;
    for (size_t i = 0; i < index; ++i)
    {
        pos = ChunkedHash::chunkceil(pos, fileSize);
    }
    return pos;
}

// Adds an entry for chunk `index` whose MAC is not confirmed yet (not finished).
void addUnconfirmedChunkMac(chunkmac_map& cm, SymmCipher& cipher, m_off_t fileSize, size_t index)
{
    byte buf[SymmCipher::BLOCKSIZE] = {};
    const m_off_t pos = chunkStartOf(fileSize, index);
    cm.ctr_encrypt(pos, &cipher, buf, sizeof(buf), pos, 0, false);
}

void expectRefused(const chunkmac_map& cm, m_off_t fileSize, const std::set<size_t>& missing)
{
    const auto coverage = cm.coverage(fileSize);
    EXPECT_FALSE(cm.coversWholeFile(fileSize))
        << "size " << fileSize << " missing " << describe(missing);
    EXPECT_EQ(coverage.chunks, chunkCountOf(fileSize));
    EXPECT_EQ(coverage.missing, missing.size()) << describe(missing);
    EXPECT_EQ(coverage.firstMissing, chunkStartOf(fileSize, *missing.begin())) << describe(missing);
}

// A copy of a complete map with the MACs of `missing` dropped, as the gap injector does.
chunkmac_map withDroppedMacs(const chunkmac_map& complete,
                             m_off_t fileSize,
                             const std::set<size_t>& missing)
{
    chunkmac_map cm = complete;
    for (const auto index: missing)
    {
        EXPECT_TRUE(cm.dropChunkMacForTesting(chunkStartOf(fileSize, index)))
            << "size " << fileSize << " chunk " << index;
    }
    return cm;
}

} // namespace

TEST(MetaMacIntegrity, CompletenessCheckRefusesEveryKnownShape)
{
    auto cipherPtr = integrityCipher();
    SymmCipher& cipher = *cipherPtr;
    for (const auto& shape: knownGapShapes())
    {
        chunkmac_map cm;
        addChunkMacs(cm, cipher, shape.fileSize, shape.missing);
        SCOPED_TRACE(shape.name);
        expectRefused(cm, shape.fileSize, shape.missing);
    }
}

// Every non-empty set of missing chunks for files of 1 to 13 chunks (57,298 shapes), dropped from a
// complete map as the gap injector does, and every single chunk left unconfirmed: the check refuses
// each one and reports the exact missing count and first missing chunk.
TEST(MetaMacIntegrity, CompletenessCheckRefusesEveryGapOfSmallFiles)
{
    auto cipherPtr = integrityCipher();
    SymmCipher& cipher = *cipherPtr;
    for (const m_off_t fileSize: smallFileSizes())
    {
        const size_t chunks = chunkCountOf(fileSize);
        chunkmac_map complete;
        addChunkMacs(complete, cipher, fileSize);
        ASSERT_TRUE(complete.coversWholeFile(fileSize)) << "size " << fileSize;
        for (unsigned mask = 1; mask < (1u << chunks); ++mask)
        {
            const auto missing = chunksOfMask(mask, chunks);
            expectRefused(withDroppedMacs(complete, fileSize, missing), fileSize, missing);
        }
        for (size_t index = 0; index < chunks; ++index)
        {
            chunkmac_map unconfirmed;
            addChunkMacs(unconfirmed, cipher, fileSize, {index});
            addUnconfirmedChunkMac(unconfirmed, cipher, fileSize, index);
            expectRefused(unconfirmed, fileSize, {index});
        }
    }
}

TEST(MetaMacIntegrity, CompletenessCheckRefusesRandomShapes)
{
    auto cipherPtr = integrityCipher();
    SymmCipher& cipher = *cipherPtr;
    for (const auto& shape: randomGapShapes())
    {
        chunkmac_map cm;
        addChunkMacs(cm, cipher, shape.fileSize, shape.missing);
        expectRefused(cm, shape.fileSize, shape.missing);
    }
}

// Structured families on large files: every single missing chunk, every adjacent pair, every
// missing tail ("first k sent"), and "first k minus one hole" (all of them on 68 chunks, a sample
// on 110/204/300). Then holes and unconfirmed entries near the end of maps whose prefix is
// condensed.
TEST(MetaMacIntegrity, CompletenessCheckRefusesStructuredFamiliesOnLargeFiles)
{
    auto cipherPtr = integrityCipher();
    SymmCipher& cipher = *cipherPtr;
    for (const m_off_t fileSize: {k64MiB, k110Chunks, k200MiB, k300Chunks})
    {
        const size_t chunks = chunkCountOf(fileSize);
        const size_t kStep = chunks <= 68 ? 1 : 7;
        const size_t holeStep = chunks <= 68 ? 1 : 5;
        chunkmac_map complete;
        addChunkMacs(complete, cipher, fileSize);
        std::vector<std::set<size_t>> family;
        for (size_t i = 0; i < chunks; ++i)
        {
            family.push_back({i});
            if (i + 1 < chunks)
            {
                family.push_back({i, i + 1});
            }
            family.push_back(chunkRange(i, chunks));
        }
        for (size_t k = 1; k < chunks; k += kStep)
        {
            for (size_t hole = 0; hole < k; hole += holeStep)
            {
                family.push_back(unite({hole}, chunkRange(k, chunks)));
            }
        }
        for (const auto& missing: family)
        {
            expectRefused(withDroppedMacs(complete, fileSize, missing), fileSize, missing);
        }
    }

    for (const m_off_t fileSize: {k110Chunks, k200MiB})
    {
        const size_t chunks = chunkCountOf(fileSize);
        for (size_t index = chunks - 20; index < chunks; ++index)
        {
            chunkmac_map holed;
            addChunkMacs(holed, cipher, fileSize, {index});
            condensePrefix(holed, cipher, fileSize);
            ASSERT_LT(holed.size(), chunks - 1) << "the prefix was not condensed";
            expectRefused(holed, fileSize, {index});

            chunkmac_map unconfirmed;
            addChunkMacs(unconfirmed, cipher, fileSize, {index});
            addUnconfirmedChunkMac(unconfirmed, cipher, fileSize, index);
            condensePrefix(unconfirmed, cipher, fileSize);
            expectRefused(unconfirmed, fileSize, {index});
        }
    }
}

// Testing the tests. Plausible but wrong completeness checks must each disagree with the truth on
// at least one case of the catalogue; coverage() and an independent correct check (contiguous
// progress reaching the end of the file) must agree with it on every case.
TEST(MetaMacIntegrity, ShapeCatalogueExposesWrongCompletenessChecks)
{
    auto cipherPtr = integrityCipher();
    SymmCipher& cipher = *cipherPtr;

    struct Case
    {
        m_off_t fileSize;
        chunkmac_map macs;
        bool complete;
        bool condensed;
    };

    std::vector<Case> cases;
    auto add = [&](m_off_t fileSize, const std::set<size_t>& missing, bool condensed)
    {
        chunkmac_map cm;
        addChunkMacs(cm, cipher, fileSize, missing);
        if (condensed)
        {
            condensePrefix(cm, cipher, fileSize);
        }
        cases.push_back({fileSize, std::move(cm), missing.empty(), condensed});
    };
    for (const m_off_t fileSize: {m_off_t{1},
                                  m_off_t{131072},
                                  m_off_t{131073},
                                  m_off_t{4718593},
                                  kFieldFile,
                                  k64MiB,
                                  k200MiB})
    {
        add(fileSize, {}, false);
    }
    add(k110Chunks, {}, true);
    add(k200MiB, {}, true);
    for (const auto& shape: knownGapShapes())
    {
        add(shape.fileSize, shape.missing, false);
    }
    for (unsigned mask = 1; mask < (1u << 13); ++mask)
    {
        add(kFieldFile, chunksOfMask(mask, 13), false);
    }
    for (const m_off_t fileSize: {k110Chunks, k200MiB})
    {
        const size_t last = chunkCountOf(fileSize) - 1;
        add(fileSize, {last - 4}, true);
        add(fileSize, {last - 9, last - 8}, true);
    }
    for (const size_t index: {size_t{0}, size_t{6}, size_t{12}})
    {
        chunkmac_map cm;
        addChunkMacs(cm, cipher, kFieldFile, {index});
        addUnconfirmedChunkMac(cm, cipher, kFieldFile, index);
        cases.push_back({kFieldFile, std::move(cm), false, false});
    }

    using Check = std::function<bool(chunkmac_map&, m_off_t)>;
    auto confirmedFrom = [](chunkmac_map& cm, m_off_t fileSize, size_t firstIndex)
    {
        for (size_t i = firstIndex; i < chunkCountOf(fileSize); ++i)
        {
            if (!cm.finishedAt(chunkStartOf(fileSize, i)))
            {
                return false;
            }
        }
        return true;
    };

    struct Candidate
    {
        const char* name;
        Check check;
        bool plainMapsOnly; // uses finishedAt(), which a condensed prefix does not support
    };

    const Candidate wrongChecks[] = {
        {"accept every key (before SDK-6795)",
         [](chunkmac_map&, m_off_t)
         {
             return true;
         },
         false},
        {"one entry per chunk",
         [](chunkmac_map& cm, m_off_t fileSize)
         {
             return cm.size() == chunkCountOf(fileSize);
         },
         false},
        {"last chunk confirmed",
         [&](chunkmac_map& cm, m_off_t fileSize)
         {
             return confirmedFrom(cm, fileSize, chunkCountOf(fileSize) - 1);
         },
         true},
        {"first and last chunks confirmed",
         [&](chunkmac_map& cm, m_off_t fileSize)
         {
             return cm.finishedAt(0) && confirmedFrom(cm, fileSize, chunkCountOf(fileSize) - 1);
         },
         true},
        {"no gap in the last 40 chunks (the download tolerance's window)",
         [&](chunkmac_map& cm, m_off_t fileSize)
         {
             const size_t chunks = chunkCountOf(fileSize);
             return confirmedFrom(cm, fileSize, chunks > 40 ? chunks - 40 : 0);
         },
         true},
    };
    for (const auto& candidate: wrongChecks)
    {
        size_t disagreements = 0;
        for (auto& c: cases)
        {
            if (candidate.plainMapsOnly && c.condensed)
            {
                continue;
            }
            chunkmac_map copy = c.macs;
            disagreements += candidate.check(copy, c.fileSize) != c.complete;
        }
        EXPECT_GT(disagreements, 0u) << "the catalogue does not expose: " << candidate.name;
    }

    size_t coverageDisagreements = 0;
    size_t contiguousDisagreements = 0;
    for (auto& c: cases)
    {
        coverageDisagreements += c.macs.coversWholeFile(c.fileSize) != c.complete;
        chunkmac_map copy = c.macs;
        contiguousDisagreements +=
            (copy.updateContiguousProgress(c.fileSize) == c.fileSize) != c.complete;
    }
    EXPECT_EQ(coverageDisagreements, 0u);
    EXPECT_EQ(contiguousDisagreements, 0u);
    EXPECT_GT(cases.size(), 8200u);
}

// A complete map passes at every chunk-lattice edge, a statecache round trip keeps it complete,
// and an entry that is not confirmed yet does not count.
TEST(MetaMacIntegrity, CompletenessCheckAcceptsCompleteMapsOnly)
{
    auto cipherPtr = integrityCipher();
    SymmCipher& cipher = *cipherPtr;

    chunkmac_map empty;
    EXPECT_TRUE(empty.coversWholeFile(0)) << "an empty file needs no MAC";
    EXPECT_EQ(empty.coverage(0).chunks, 0u);
    EXPECT_FALSE(empty.coversWholeFile(1));
    EXPECT_EQ(empty.coverage(5242880).missing, chunkCountOf(5242880));
    EXPECT_EQ(empty.coverage(5242880).firstMissing, 0);

    const m_off_t sizes[] =
        {1, 16, 131072, 131073, 393216, 393217, 4718592, 4718593, kFieldFile, k64MiB, k110Chunks};
    for (const m_off_t size: sizes)
    {
        chunkmac_map cm;
        addChunkMacs(cm, cipher, size);
        EXPECT_TRUE(cm.coversWholeFile(size)) << "size " << size;
        EXPECT_EQ(cm.coverage(size).chunks, chunkCountOf(size)) << "size " << size;
        EXPECT_EQ(cm.coverage(size).firstMissing, -1) << "size " << size;

        std::string blob;
        cm.serialize(blob);
        chunkmac_map restored;
        const char* ptr = blob.data();
        ASSERT_TRUE(restored.unserialize(ptr, blob.data() + blob.size())) << "size " << size;
        EXPECT_TRUE(restored.coversWholeFile(size)) << "size " << size;
    }

    chunkmac_map unconfirmed;
    addChunkMacs(unconfirmed, cipher, kFieldFile, {7});
    addUnconfirmedChunkMac(unconfirmed, cipher, kFieldFile, 7);
    expectRefused(unconfirmed, kFieldFile, {7});
}

// The condensed prefix counts as covered (it is gap-free by construction), it survives a
// statecache round trip, and holes after it are still refused.
TEST(MetaMacIntegrity, CompletenessCheckWithCondensedPrefix)
{
    auto cipherPtr = integrityCipher();
    SymmCipher& cipher = *cipherPtr;
    for (const m_off_t fileSize: {k110Chunks, k200MiB})
    {
        chunkmac_map complete;
        addChunkMacs(complete, cipher, fileSize);
        condensePrefix(complete, cipher, fileSize);
        ASSERT_LT(complete.size(), chunkCountOf(fileSize)) << "the prefix was not condensed";
        EXPECT_TRUE(complete.coversWholeFile(fileSize));
        EXPECT_EQ(complete.coverage(fileSize).chunks, chunkCountOf(fileSize));

        std::string blob;
        complete.serialize(blob);
        chunkmac_map restored;
        const char* ptr = blob.data();
        ASSERT_TRUE(restored.unserialize(ptr, blob.data() + blob.size()));
        EXPECT_TRUE(restored.coversWholeFile(fileSize));

        const size_t last = chunkCountOf(fileSize) - 1;
        for (const auto& missing: {std::set<size_t>{last - 4},
                                   std::set<size_t>{last - 9, last - 8},
                                   chunkRange(last - 13, last + 1)})
        {
            chunkmac_map holed;
            addChunkMacs(holed, cipher, fileSize, missing);
            condensePrefix(holed, cipher, fileSize);
            expectRefused(holed, fileSize, missing);
        }
    }
}

// ---------------------------------------------------------------------------------------------
// Events 800042/800043 report where the gaps are, as runs of chunk indexes.
// ---------------------------------------------------------------------------------------------

namespace
{

std::string runsOf(const std::set<size_t>& chunks)
{
    std::vector<std::pair<size_t, size_t>> runs;
    for (const auto c: chunks)
    {
        if (!runs.empty() && runs.back().first + runs.back().second == c)
        {
            ++runs.back().second;
        }
        else
        {
            runs.emplace_back(c, 1);
        }
    }
    return chunkmac_map::formatChunkRuns(runs, runs.size());
}

} // namespace

TEST(MetaMacIntegrity, UploadEventReportsTheMissingRuns)
{
    auto cipherPtr = integrityCipher();
    SymmCipher& cipher = *cipherPtr;
    EXPECT_EQ(runsOf({0, 12}), "0+1,12+1");
    EXPECT_EQ(runsOf(unite({3, 5}, chunkRange(10, 13))), "3+1,5+1,10+3");
    for (const auto& shape: knownGapShapes())
    {
        chunkmac_map cm;
        addChunkMacs(cm, cipher, shape.fileSize, shape.missing);
        const auto coverage = cm.coverage(shape.fileSize);
        EXPECT_EQ(chunkmac_map::formatChunkRuns(coverage.missingRuns, coverage.missingRuns.size()),
                  runsOf(shape.missing))
            << shape.name;
    }
    chunkmac_map complete;
    addChunkMacs(complete, cipher, kFieldFile);
    EXPECT_TRUE(complete.coverage(kFieldFile).missingRuns.empty());
}

TEST(MetaMacIntegrity, EventRunListIsCapped)
{
    std::vector<std::pair<size_t, size_t>> runs;
    EXPECT_EQ(chunkmac_map::formatChunkRuns(runs), "");
    for (size_t i = 0; i < 10; ++i)
    {
        runs.emplace_back(i * 3, 1);
    }
    EXPECT_EQ(chunkmac_map::formatChunkRuns(runs), "0+1,3+1,6+1,9+1,12+1,15+1,18+1,21+1,...(+2)");
    EXPECT_EQ(chunkmac_map::formatChunkRuns(runs, 2), "0+1,3+1,...(+8)");
}

// The download event names exactly the chunks macsmac_gaps() left out: the MAC with those gaps
// equals the full MAC of a map without them. Without a condensed prefix the tolerance's gap bounds
// are chunk indexes; after one they count the folded chunks plus one per entry.
TEST(MetaMacIntegrity, DownloadEventNamesTheSkippedChunks)
{
    auto cipherPtr = integrityCipher();
    SymmCipher& cipher = *cipherPtr;

    chunkmac_map small;
    addChunkMacs(small, cipher, kFieldFile);
    EXPECT_EQ(chunkmac_map::formatChunkRuns(small.gapChunkRuns(3, 5, 10, 13)), "3+2,10+3");
    EXPECT_EQ(small.macsmac_gaps(&cipher, 3, 5, 10, 13),
              metaMacBuiltLikeBeforeTheFix(cipher, kFieldFile, {3, 4, 10, 11, 12}));
    EXPECT_NE(small.macsmac_gaps(&cipher, 4, 6, 10, 13),
              metaMacBuiltLikeBeforeTheFix(cipher, kFieldFile, {3, 4, 10, 11, 12}));

    chunkmac_map big;
    addChunkMacs(big, cipher, k110Chunks);
    condensePrefix(big, cipher, k110Chunks);
    const size_t folded = 110 - (big.size() - 1); // chunks in the condensed prefix
    ASSERT_GT(folded, 1u) << "the prefix was not condensed";
    const size_t g1 = folded + 1 + 5; // the entries of chunks folded+5 and folded+6
    EXPECT_EQ(chunkmac_map::formatChunkRuns(big.gapChunkRuns(g1, g1 + 2, 0, 0)),
              std::to_string(folded + 5) + "+2");
    EXPECT_EQ(big.macsmac_gaps(&cipher, g1, g1 + 2, 0, 0),
              metaMacBuiltLikeBeforeTheFix(cipher, k110Chunks, {folded + 5, folded + 6}));
}

} // namespace mega
