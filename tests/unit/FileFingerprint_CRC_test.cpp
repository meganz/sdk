// === Unified CRC tests: production (IA/FA) + 32-bit-overflow emulation =======

#include "DefaultedFileAccess.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <mega/base64.h>
#include <mega/crypto/cryptopp.h>

#include <bitset>

#if !defined(_WIN32)
#include <arpa/inet.h> // htonl
#endif

namespace
{
using ::mega::byte;
using ::testing::ContainerEq;

using CRCLanes = std::array<std::uint32_t, 4>;

constexpr std::uint64_t operator"" _MiB(const unsigned long long n) noexcept
{
    return n * 1024ull * 1024ull;
}

struct Layout
{
    static constexpr unsigned kLanes{4};
    static constexpr unsigned kBlocks{32};
    static constexpr unsigned kWindowBytes{64}; // bytes per sampled window
    static constexpr unsigned kDenominator{kLanes * kBlocks - 1}; // 127
    static constexpr std::uint64_t kWindowU{kWindowBytes};
};

constexpr std::uint32_t kDeterministicSeed{0xA5A5A5A5u}; // stable non-trivial PRNG seed
constexpr std::int64_t kTestMtimeSecs{1'700'000'000};
constexpr std::size_t kCrcBytes{Layout::kLanes * 4};
constexpr std::uint32_t kEqMask40MiB{0b0111u};
constexpr std::uint32_t kEqMask52MiB{0b0011u};
constexpr std::uint32_t kEqMask88MiB{0b0001u};

// ---------- Minimal in-memory IA and FA (exercise production code) -----------

class MemIA final: public ::mega::InputStreamAccess
{
public:
    explicit MemIA(const std::vector<byte>& data):
        mData(data)
    {}

    m_off_t size() override
    {
        return static_cast<m_off_t>(mData.size());
    }

    bool read(byte* buffer, const unsigned n) override
    {
        if (!buffer)
        { // skip/seek forward by n
            if (mPos + n > mData.size())
                return false;
            mPos += n;
            return true;
        }
        if (mPos + n > mData.size())
            return false;
        std::memcpy(buffer, &mData[mPos], n);
        mPos += n;
        return true;
    }

private:
    const std::vector<byte>& mData;
    std::size_t mPos{0};
};

class MemFA final: public ::mt::DefaultedFileAccess
{
public:
    MemFA(const std::vector<byte>& data, const ::mega::m_time_t mt):
        mData(data)
    {
        mtime = mt;
        size = static_cast<m_off_t>(data.size());
    }

    bool openf(::mega::FSLogging) override
    {
        mIsOpen = true;
        return true;
    }

    void closef() override
    {
        mIsOpen = false;
    }

    bool frawread(void* buf,
                  unsigned long n,
                  m_off_t off,
                  bool /*nolock*/,
                  ::mega::FSLogging,
                  bool* /*retry*/ = nullptr) override
    {
        if (!mIsOpen || off < 0)
            return false;

        const auto nbytes = static_cast<std::size_t>(n);
        const auto offsz = static_cast<std::size_t>(off);

        if (offsz > mData.size() || nbytes > (mData.size() - offsz))
            return false;

        if (buf)
            std::memcpy(buf, mData.data() + offsz, nbytes);
        return true;
    }

private:
    bool mIsOpen{false};
    const std::vector<byte>& mData;
};

// --------- Utilities ---------------------------------------------------------

template<typename T, std::size_t N, typename Mask = std::uint32_t>
[[nodiscard]] inline Mask laneEqMaskBitset(const std::array<T, N>& a, const std::array<T, N>& b)
{
    static_assert(N <= std::numeric_limits<Mask>::digits,
                  "Mask too narrow for number of lanes; use a wider Mask.");
    std::bitset<N> bits;
    for (std::size_t i = 0; i < N; ++i)
        bits.set(i, a[i] == b[i]);
    return static_cast<Mask>(bits.to_ulong());
}

[[nodiscard]] inline std::uint32_t htonl_u32(const std::uint32_t x) noexcept
{
#if defined(_WIN32)
    return _byteswap_ulong(x);
#else
    return htonl(x);
#endif
}

// Extract the 22-char CRC b64 from size:mtime:CRC:valid
[[nodiscard]] std::string crcB64FromDbg(const std::string& dbg)
{
    const auto p1 = dbg.find(':');
    if (p1 == std::string::npos)
        return {};
    const auto p2 = dbg.find(':', p1 + 1);
    if (p2 == std::string::npos)
        return {};
    const auto p3 = dbg.find(':', p2 + 1);
    if (p3 == std::string::npos)
        return {};
    return dbg.substr(p2 + 1, p3 - (p2 + 1));
}

[[nodiscard]] CRCLanes b64ToLanesHost(const std::string& b64)
{
    CRCLanes out{};
    byte buf[kCrcBytes]{};
    const auto n = ::mega::Base64::atob(b64.c_str(), buf, static_cast<int>(kCrcBytes));
    if (n == static_cast<int>(kCrcBytes))
    {
        std::memcpy(out.data(), buf, kCrcBytes); // stored as host-endian words
    }
    return out;
}

[[nodiscard]] std::string lanesToB64(const CRCLanes& lanesHost)
{
    byte raw[kCrcBytes];
    std::memcpy(raw, lanesHost.data(), kCrcBytes);

    // base64 output capacity = 4 * ceil(N / 3)
    const auto cap = static_cast<std::size_t>(4 * ((kCrcBytes + 2) / 3));
    std::string out(cap, '\0');
    const auto outSize = ::mega::Base64::btoa(raw, static_cast<int>(kCrcBytes), out.data());
    if (outSize < 0)
        return {};
    out.resize(static_cast<std::size_t>(outSize));
    return out;
}

// Deterministic PRNG (xorshift32) for fully stable bytes across platforms
void fillDeterministic(std::vector<byte>& buf, const std::uint32_t seed = kDeterministicSeed)
{
    std::uint32_t x = seed;
    for (auto& b: buf)
    {
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        b = static_cast<byte>(x & 0xFF);
    }
}

// ---------- Buggy 32-bit overflow emulation (for comparison) -----------------

[[nodiscard]] inline std::uint64_t sparseOffset64(const std::uint64_t size,
                                                  const unsigned lane,
                                                  const unsigned j) noexcept
{
    const std::uint64_t idx = std::uint64_t(lane) * Layout::kBlocks + j;
    const std::uint64_t numer = (size - Layout::kWindowU) * idx; // 64-bit multiply
    const std::uint64_t off = Layout::kDenominator ? (numer / Layout::kDenominator) : 0;
    const std::uint64_t max = size - Layout::kWindowU;
    return off > max ? max : off;
}

// Emulates the 32-bit multiply (overflow) & 32-bit divide bug
[[nodiscard]] inline std::uint64_t sparseOffset32_bug(const std::uint64_t size,
                                                      const unsigned lane,
                                                      const unsigned j) noexcept
{
    const std::uint32_t sz32 = static_cast<std::uint32_t>(size);
    const std::uint32_t idx32 = static_cast<std::uint32_t>(lane * Layout::kBlocks + j);
    const std::uint32_t numer =
        static_cast<std::uint32_t>((sz32 - Layout::kWindowBytes) * idx32); // wraps
    const std::uint32_t off32 = Layout::kDenominator ? (numer / Layout::kDenominator) : 0;
    const std::uint64_t max = size - Layout::kWindowU;
    return off32 > max ? max : off32;
}

void computeCrcFromBytes(const std::vector<byte>& data,
                         const bool use64Fix,
                         CRCLanes& lanesHost_out)
{
    for (unsigned li = 0; li < Layout::kLanes; ++li)
    {
        ::mega::HashCRC32 crc;
        for (unsigned j = 0; j < Layout::kBlocks; ++j)
        {
            const auto off = use64Fix ?
                                 sparseOffset64(static_cast<std::uint64_t>(data.size()), li, j) :
                                 sparseOffset32_bug(static_cast<std::uint64_t>(data.size()), li, j);
            crc.add(&data[static_cast<std::size_t>(off)], Layout::kWindowBytes);
        }
        std::int32_t v{0};
        crc.get(reinterpret_cast<byte*>(&v));
        lanesHost_out[li] = htonl_u32(static_cast<std::uint32_t>(v)); // match cloud packing
    }
}

[[nodiscard]] inline CRCLanes computeCrcFromBytes(const std::vector<byte>& data,
                                                  const bool use64Fix)
{
    CRCLanes lanes{};
    computeCrcFromBytes(data, use64Fix, lanes);
    return lanes;
}

// ---------- Shared helper to compute + check one synthetic case --------------

struct SynthResult
{
    std::string goodB64;
    std::string bugB64;
};

void runOneSyntheticCase(const std::uint64_t sizeBytes,
                         const std::uint32_t seed,
                         const std::string_view label,
                         const std::uint32_t expectedEqMask,
                         SynthResult& out)
{
    // Create deterministic data
    std::vector<byte> data(static_cast<std::size_t>(sizeBytes));
    fillDeterministic(data, seed);

    // Production (IA)
    std::string goodB64_IA;
    {
        MemIA ia(data);
        ::mega::FileFingerprint fp;
        ASSERT_TRUE(fp.genfingerprint(&ia, /*cmtime*/ kTestMtimeSecs, /*ignoremtime*/ false));
        goodB64_IA = crcB64FromDbg(fp.fingerprintDebugString());
    }

    // Production (FA)
    std::string goodB64_FA;
    {
        MemFA fa(data, /*mtime*/ kTestMtimeSecs);
        ::mega::FileFingerprint fp;
        ASSERT_TRUE(fp.genfingerprint(&fa, /*ignoremtime*/ false));
        goodB64_FA = crcB64FromDbg(fp.fingerprintDebugString());
    }

    // Reference "good" emulation via helper (64-bit math)
    const auto goodCRClanes = computeCrcFromBytes(data, /*use64Fix=*/true);
    const auto goodB64_ref = lanesToB64(goodCRClanes);

    {
        const auto goodHostLanesFromB64 = b64ToLanesHost(goodB64_ref);
        ASSERT_THAT(goodCRClanes, ContainerEq(goodHostLanesFromB64));
    }

    EXPECT_EQ(goodB64_IA, goodB64_ref) << "IA/ref mismatch for " << label;
    EXPECT_EQ(goodB64_FA, goodB64_ref) << "FA/ref mismatch for " << label;
    EXPECT_EQ(goodB64_IA, goodB64_FA) << "IA/FA mismatch for " << label;

    // Buggy emulation
    const auto bugCRClanes = computeCrcFromBytes(data, /*use64Fix=*/false);
    const auto bugB64 = lanesToB64(bugCRClanes);

    {
        const auto badHostLanesFromB64 = b64ToLanesHost(bugB64);
        ASSERT_THAT(bugCRClanes, ContainerEq(badHostLanesFromB64));
    }

    EXPECT_NE(goodB64_ref, bugB64) << "Buggy CRC should differ for " << label;

    const auto eqMask = laneEqMaskBitset(goodCRClanes, bugCRClanes);
    EXPECT_EQ(eqMask, expectedEqMask) << "Unexpected lane pattern for " << label;

    out.goodB64 = goodB64_ref;
    out.bugB64 = bugB64;
}

} // namespace

TEST(FileFingerprint, CRC64Fix_Synth_40MiB_GoodVsBuggy)
{
    SynthResult r;
    ASSERT_NO_FATAL_FAILURE(
        runOneSyntheticCase(40_MiB, kDeterministicSeed, "40MiB", kEqMask40MiB, r));

    static constexpr const char* kGood{"6iqpUy7DdAKx5NIRg31i_g"};
    static constexpr const char* kBug{"6iqpUy7DdAKx5NIRGX1AAA"};

    EXPECT_EQ(r.goodB64, kGood);
    EXPECT_EQ(r.bugB64, kBug);
}

TEST(FileFingerprint, CRC64Fix_Synth_52MiB_GoodVsBuggy)
{
    SynthResult r;
    ASSERT_NO_FATAL_FAILURE(
        runOneSyntheticCase(52_MiB, kDeterministicSeed, "52MiB", kEqMask52MiB, r));

    static constexpr const char* kGood{"7SMVr_-v9_H7MDsN9yuVGA"};
    static constexpr const char* kBug{"7SMVr_-v9_Gk00B4SWd30g"};

    EXPECT_EQ(r.goodB64, kGood);
    EXPECT_EQ(r.bugB64, kBug);
}

TEST(FileFingerprint, CRC64Fix_Synth_88MiB_GoodVsBuggy)
{
    SynthResult r;
    ASSERT_NO_FATAL_FAILURE(
        runOneSyntheticCase(88_MiB, kDeterministicSeed, "88MiB", kEqMask88MiB, r));

    static constexpr const char* kGood{"3hhTVPVhwzudmjN1odbO6w"};
    static constexpr const char* kBug{"3hhTVIMatxXS_18ZkPyITg"};

    EXPECT_EQ(r.goodB64, kGood);
    EXPECT_EQ(r.bugB64, kBug);
}
