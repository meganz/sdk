/**
 * (c) 2026 by MEGA Privacy Kft, Csomad, Hungary
 *
 * This file is part of the MEGA SDK - Client Access Engine.
 */

#include "mega/wsupload.h"

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

#else

TEST(WsUpload, DisabledWithoutWsUpload)
{
    GTEST_SKIP() << "MEGA_USE_WSUPLOAD is disabled";
}

#endif
