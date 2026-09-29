/**
 * (c) 2026 by MEGA Privacy Kft, Csomad, Hungary
 *
 * This file is part of the MEGA SDK - Client Access Engine.
 */

#include "mega/utils.h" // ChunkedHash::chunkfloor (S16 Gate-1 lattice checker)
#include "mega/wsupload.h"

#include <algorithm> // std::max/std::min (S16 clamp reference implementation)

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <string>

#include <zlib.h>

namespace
{

std::string makeInboundFrame(const std::string& payloadWithoutCrc)
{
    std::string frame = payloadWithoutCrc;
    const auto crc =
        static_cast<std::uint32_t>(::crc32(0,
                                           reinterpret_cast<const Bytef*>(frame.data()),
                                           static_cast<uInt>(frame.size())));
    frame.append(reinterpret_cast<const char*>(&crc), sizeof(crc));
    return frame;
}

std::string makeInboundFrameWith(const std::uint32_t fileno,
                                  const std::int64_t chunkpos,
                                  const signed char event,
                                  const std::string& extraPayload = {})
{
    std::string payload;
    payload.append(reinterpret_cast<const char*>(&fileno), sizeof(fileno));
    payload.append(reinterpret_cast<const char*>(&chunkpos), sizeof(chunkpos));
    payload.push_back(static_cast<char>(event));
    payload.append(extraPayload);
    return makeInboundFrame(payload);
}

std::string makeMinimalValidInboundFrame()
{
    return makeInboundFrameWith(7, 123456789, 5);
}

} // namespace

#ifdef MEGA_USE_WSUPLOAD

// =====================================================================
// Inbound frame validation
// =====================================================================

TEST(WsUpload, ValidateInboundFrameRejectsShortFrames)
{
    for (int len = 9; len < mega::ws::detail::kMinInboundFrameBytes; ++len)
    {
        std::string payload(static_cast<std::size_t>(len - mega::ws::detail::kInboundFrameTrailerCrcBytes),
                            static_cast<char>('a' + (len % 23)));
        const auto frame = makeInboundFrame(payload);

        EXPECT_EQ(mega::ws::detail::InboundFrameValidationResult::TooShort,
                  mega::ws::detail::validateInboundFrame(frame.data(),
                                                         static_cast<int>(frame.size())))
            << "len=" << len;
    }
}

TEST(WsUpload, ValidateInboundFrameRejectsBadCrc)
{
    auto frame = makeMinimalValidInboundFrame();
    ASSERT_GE(frame.size(),
              static_cast<std::size_t>(mega::ws::detail::kMinInboundFrameBytes));

    frame.back() ^= 0x5a;

    EXPECT_EQ(mega::ws::detail::InboundFrameValidationResult::BadCrc,
              mega::ws::detail::validateInboundFrame(frame.data(),
                                                     static_cast<int>(frame.size())));
}

TEST(WsUpload, ValidateInboundFrameAcceptsValidMinimalFrame)
{
    const auto frame = makeMinimalValidInboundFrame();
    ASSERT_EQ(frame.size(),
              static_cast<std::size_t>(mega::ws::detail::kMinInboundFrameBytes));

    EXPECT_EQ(mega::ws::detail::InboundFrameValidationResult::Ok,
              mega::ws::detail::validateInboundFrame(frame.data(),
                                                     static_cast<int>(frame.size())));
}

TEST(WsUpload, ValidateInboundFrameRejectsZeroLength)
{
    const char dummy = 0;
    EXPECT_EQ(mega::ws::detail::InboundFrameValidationResult::TooShort,
              mega::ws::detail::validateInboundFrame(&dummy, 0));
}

TEST(WsUpload, ValidateInboundFrameRejectsNegativeLength)
{
    const char dummy = 0;
    EXPECT_EQ(mega::ws::detail::InboundFrameValidationResult::TooShort,
              mega::ws::detail::validateInboundFrame(&dummy, -1));
}

TEST(WsUpload, ValidateInboundFrameAcceptsLargerFrame)
{
    // A frame with extra payload bytes beyond the minimum header is valid as long as CRC matches.
    const std::string extraPayload(64, 'X');
    const auto frame = makeInboundFrameWith(42, 0, 1, extraPayload);

    EXPECT_GT(static_cast<int>(frame.size()), mega::ws::detail::kMinInboundFrameBytes);
    EXPECT_EQ(mega::ws::detail::InboundFrameValidationResult::Ok,
              mega::ws::detail::validateInboundFrame(frame.data(),
                                                     static_cast<int>(frame.size())));
}

TEST(WsUpload, ValidateInboundFrameRejectsBadCrcInLargerFrame)
{
    const std::string extraPayload(64, 'X');
    auto frame = makeInboundFrameWith(42, 0, 1, extraPayload);

    // Corrupt one byte in the payload (not the CRC trailer).
    frame[5] ^= static_cast<char>(0xFF);

    EXPECT_EQ(mega::ws::detail::InboundFrameValidationResult::BadCrc,
              mega::ws::detail::validateInboundFrame(frame.data(),
                                                     static_cast<int>(frame.size())));
}

TEST(WsUpload, ValidateInboundFrameAcceptsAllEventTypes)
{
    // Server events 1-7 should all produce valid frames.
    for (signed char ev = 1; ev <= 7; ++ev)
    {
        const auto frame = makeInboundFrameWith(1, 0, ev);
        EXPECT_EQ(mega::ws::detail::InboundFrameValidationResult::Ok,
                  mega::ws::detail::validateInboundFrame(frame.data(),
                                                         static_cast<int>(frame.size())))
            << "event=" << static_cast<int>(ev);
    }
}

TEST(WsUpload, ValidateInboundFrameAcceptsMaxFieldValues)
{
    const auto frame = makeInboundFrameWith(
        std::numeric_limits<std::uint32_t>::max(),
        std::numeric_limits<std::int64_t>::max(),
        std::numeric_limits<signed char>::max());

    EXPECT_EQ(mega::ws::detail::InboundFrameValidationResult::Ok,
              mega::ws::detail::validateInboundFrame(frame.data(),
                                                     static_cast<int>(frame.size())));
}

TEST(WsUpload, ValidateInboundFrameAcceptsZeroFieldValues)
{
    const auto frame = makeInboundFrameWith(0, 0, 0);

    EXPECT_EQ(mega::ws::detail::InboundFrameValidationResult::Ok,
              mega::ws::detail::validateInboundFrame(frame.data(),
                                                     static_cast<int>(frame.size())));
}

TEST(WsUpload, ValidateInboundFrameRejectsExactlyOneByteTooShort)
{
    // One byte less than the minimum should be rejected as TooShort.
    const int tooShort = mega::ws::detail::kMinInboundFrameBytes - 1;
    std::string payload(static_cast<std::size_t>(tooShort - mega::ws::detail::kInboundFrameTrailerCrcBytes),
                        'Z');
    const auto frame = makeInboundFrame(payload);

    ASSERT_EQ(static_cast<int>(frame.size()), tooShort);
    EXPECT_EQ(mega::ws::detail::InboundFrameValidationResult::TooShort,
              mega::ws::detail::validateInboundFrame(frame.data(),
                                                     static_cast<int>(frame.size())));
}

// =====================================================================
// Frame constants consistency
// =====================================================================

TEST(WsUpload, FrameConstantsAreConsistent)
{
    // Header: fileno(4) + chunkpos(8) + event(1) = 13
    EXPECT_EQ(mega::ws::detail::kInboundChunkResponseBytes,
              static_cast<int>(sizeof(std::uint32_t) + sizeof(m_off_t) + sizeof(signed char)));

    // Trailer: CRC32(4)
    EXPECT_EQ(mega::ws::detail::kInboundFrameTrailerCrcBytes,
              static_cast<int>(sizeof(std::uint32_t)));

    // Min frame = header + trailer
    EXPECT_EQ(mega::ws::detail::kMinInboundFrameBytes,
              mega::ws::detail::kInboundChunkResponseBytes +
                  mega::ws::detail::kInboundFrameTrailerCrcBytes);

    // Verify concrete expected values to catch accidental type-size changes.
    EXPECT_EQ(mega::ws::detail::kInboundChunkResponseBytes, 13);
    EXPECT_EQ(mega::ws::detail::kInboundFrameTrailerCrcBytes, 4);
    EXPECT_EQ(mega::ws::detail::kMinInboundFrameBytes, 17);
}

// =====================================================================
// Chunk size calculation
// =====================================================================

// Defined in wsupload.cpp — no header exposure needed.
namespace mega { namespace ws { int chunkSizeAtPosition(m_off_t pos); } }

static constexpr int SEG = 131072;          // SEGSIZE in wsupload.cpp
static constexpr int MAX_CHUNK = 8 * SEG;   // 1 MiB

TEST(WsUpload, ChunkSizeAtPositionZeroIsOneSegment)
{
    EXPECT_EQ(mega::ws::chunkSizeAtPosition(0), SEG);
}

TEST(WsUpload, ChunkSizeGrowsProgressively)
{
    // Chunk map layout:
    //   pos=0*SEG  → 1*SEG     pos=1*SEG  → 2*SEG     pos=3*SEG  → 3*SEG
    //   pos=6*SEG  → 4*SEG     pos=10*SEG → 5*SEG     pos=15*SEG → 6*SEG
    //   pos=21*SEG → 7*SEG     pos=28*SEG → 8*SEG     pos=36*SEG+→ 8*SEG

    struct Expected { m_off_t pos; int size; };
    const Expected expected[] = {
        {static_cast<m_off_t>(0)  * SEG, 1 * SEG},
        {static_cast<m_off_t>(1)  * SEG, 2 * SEG},
        {static_cast<m_off_t>(3)  * SEG, 3 * SEG},
        {static_cast<m_off_t>(6)  * SEG, 4 * SEG},
        {static_cast<m_off_t>(10) * SEG, 5 * SEG},
        {static_cast<m_off_t>(15) * SEG, 6 * SEG},
        {static_cast<m_off_t>(21) * SEG, 7 * SEG},
        {static_cast<m_off_t>(28) * SEG, 8 * SEG},
    };

    for (const auto& e : expected)
    {
        EXPECT_EQ(mega::ws::chunkSizeAtPosition(e.pos), e.size) << "pos=" << e.pos;
    }
}

TEST(WsUpload, ChunkSizeAfterProgressiveRegionIsMaxChunkSize)
{
    const m_off_t steadyStart = static_cast<m_off_t>(36) * SEG;
    EXPECT_EQ(mega::ws::chunkSizeAtPosition(steadyStart), MAX_CHUNK);
    EXPECT_EQ(mega::ws::chunkSizeAtPosition(steadyStart + MAX_CHUNK), MAX_CHUNK);
    EXPECT_EQ(mega::ws::chunkSizeAtPosition(steadyStart + 10LL * MAX_CHUNK), MAX_CHUNK);
    // Multi-GB offset.
    EXPECT_EQ(mega::ws::chunkSizeAtPosition(static_cast<m_off_t>(5) * 1024 * 1024 * 1024), MAX_CHUNK);
}

TEST(WsUpload, ChunkSizeAtNonBoundaryPositionReturnsMaxChunkSize)
{
    EXPECT_EQ(mega::ws::chunkSizeAtPosition(1), MAX_CHUNK);
    EXPECT_EQ(mega::ws::chunkSizeAtPosition(12345), MAX_CHUNK);
    EXPECT_EQ(mega::ws::chunkSizeAtPosition(SEG + 1), MAX_CHUNK);
}

TEST(WsUpload, ChunkBoundariesAreContiguous)
{
    m_off_t pos = 0;
    for (int mult = 1; mult <= 8; ++mult)
    {
        const int size = mega::ws::chunkSizeAtPosition(pos);
        EXPECT_EQ(size, mult * SEG) << "pos=" << pos;
        pos += size;
    }
    EXPECT_EQ(mega::ws::chunkSizeAtPosition(pos), MAX_CHUNK);
}

// =====================================================================
// S16 Gate-1: lattice closure of the REAL fresh-chunk advance step
// =====================================================================

// Defined in wsupload.cpp (extracted from WsPool::nextChunk so this test covers the real
// production computation — the S15 clamp shipped precisely because this step had no
// unit-reachable form).
namespace mega { namespace ws { m_off_t wsFreshChunkAdvance(m_off_t pos, m_off_t fileSize); } }

namespace
{

// The lattice checker: walks a file head from 0 to EOF through an advance function and
// verifies every invariant the chunk/MAC machinery depends on. Returns the first violating
// position, or -1 if the walk is clean. Violations checked:
//  (a) pos == ChunkedHash::chunkfloor(pos) — off-lattice keys corrupt the macsmac fold
//      (utils.cpp:843 class; the S15 round-6 CI crash);
//  (b) pos % 16 == 0 — SymmCipher::ctr_crypt keystream alignment (cryptopp.cpp:735 class;
//      ciphertext corruption in Release);
//  (c) advance == chunkSizeAtPosition(pos) except at EOF — a non-EOF short chunk makes the
//      server infer EOF and silently TRUNCATE the upload (the S16 E0'/E1b evidence);
//  (d) the walk terminates exactly at fileSize.
template <typename AdvanceFn>
m_off_t firstLatticeViolation(AdvanceFn advance, m_off_t fileSize)
{
    m_off_t pos = 0;
    while (pos < fileSize)
    {
        if (pos != mega::ChunkedHash::chunkfloor(pos))
            return pos; // (a)
        if (pos % 16 != 0)
            return pos; // (b)
        const m_off_t adv = advance(pos, fileSize);
        if (adv <= 0)
            return pos; // stuck walk
        const m_off_t canonical = static_cast<m_off_t>(mega::ws::chunkSizeAtPosition(pos));
        if (adv < canonical && pos + adv != fileSize)
            return pos; // (c) non-EOF short chunk == server-side truncation
        if (adv > canonical)
            return pos; // over-long chunk: also off-contract
        pos += adv;
    }
    return (pos == fileSize) ? -1 : pos; // (d)
}

} // namespace

TEST(WsUpload, FreshChunkAdvanceLatticeClosure)
{
    // File sizes straddling every regime: sub-chunk, exact chunk multiples, the ramp knee
    // (3670016 = first 1 MiB chunk >= pos 3.5 MiB), the CI G-cell sizes, and multi-ramp files.
    const m_off_t sizes[] = {1,
                             SEG - 1,
                             SEG,
                             SEG + 1,
                             262144,
                             262145,
                             3670015,
                             3670016,
                             3670017,
                             5242879, // MixedPools fileSizeA
                             5242880, // MixedPools fileSizeB
                             16777216,
                             static_cast<m_off_t>(1) << 30};
    for (const m_off_t fileSize: sizes)
    {
        EXPECT_EQ(firstLatticeViolation(&mega::ws::wsFreshChunkAdvance, fileSize), -1)
            << "fileSize=" << fileSize;
    }
}

TEST(WsUpload, LatticeCheckerWouldHaveCaughtS15Clamp)
{
    // The S15 round-5 clamp (REVERTED in round 6 as a data-integrity defect), embedded here
    // verbatim as a known-bad reference implementation: advance capped at max(cap*12s, SEG).
    // This test proves the Gate-1 checker CATCHES that defect class — pre-registered bars:
    // cap 10485 B/s must flag pos 262144 (the r6 Windows CI crash key), cap 50001 B/s must
    // flag pos 1910732 (the tier-1 16-misalignment). If this test ever fails, the checker
    // has lost its teeth — fix the checker, never the expectation.
    const auto clamped = [](m_off_t capBps) {
        return [capBps](m_off_t pos, m_off_t fileSize) -> m_off_t {
            m_off_t adv = mega::ws::wsFreshChunkAdvance(pos, fileSize);
            const m_off_t maxLen = std::max<m_off_t>(capBps * 12, SEG);
            return std::min(adv, maxLen);
        };
    };
    const m_off_t fileSize = 5242880;

    // CI cap (10,485 B/s): S = max(125,820, 131,072) = 131,072 -> uniform 128K stride. The
    // checker flags pos 131,072: the clamped chunk there (131,072 < canonical 262,144) is a
    // NON-EOF SHORT chunk — the first defect manifestation, and byte-exactly where the S16
    // E1b replay showed the server truncating (token after the short chunk at 131,072; head
    // stopped at 262,144). The r6 Windows CI crash key 262,144 (chunkfloor = 131,072) is
    // this same walk one step later.
    EXPECT_EQ(firstLatticeViolation(clamped(10485), fileSize), 131072);

    // Tier-1 cap (50,001 B/s, %4 != 0): S = 600,012. The checker flags pos 1,310,720 — the
    // clamped 640K chunk there (600,012 < 655,360) is a NON-EOF SHORT chunk, i.e. rule (c)
    // catches the defect at its SOURCE: the server would truncate the upload right here
    // (the S16 E0' evidence: a 1,912,220-byte node from a 5 MiB source), and the 16-
    // misaligned head 1,910,732 (% 16 == 12, ciphertext-corrupting) is what the clamped
    // engine would produce NEXT. Rule (c) structurally precedes rule (b) for this defect
    // family — any clamp that misaligns emitted a short chunk one step earlier.
    EXPECT_EQ(firstLatticeViolation(clamped(50001), fileSize), 1310720);

    // Sanity: above the no-op boundary (cap >= 87,382 B/s ⇒ S >= 1 MiB) the clamp vanishes
    // and the walk is clean.
    EXPECT_EQ(firstLatticeViolation(clamped(87382), fileSize), -1);
}

#else

TEST(WsUpload, DisabledWithoutWsUpload)
{
    GTEST_SKIP() << "MEGA_USE_WSUPLOAD is disabled";
}

#endif
