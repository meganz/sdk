/**
 * (c) 2026 by Mega Limited, Auckland, New Zealand
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

std::string makeMinimalValidInboundFrame()
{
    std::string payload;

    const std::uint32_t fileno = 7;
    const std::int64_t chunkpos = 123456789;
    const signed char event = 5;

    payload.append(reinterpret_cast<const char*>(&fileno), sizeof(fileno));
    payload.append(reinterpret_cast<const char*>(&chunkpos), sizeof(chunkpos));
    payload.push_back(static_cast<char>(event));

    return makeInboundFrame(payload);
}

} // namespace

#ifdef MEGA_USE_WSUPLOAD

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

#else

TEST(WsUpload, DisabledWithoutWsUpload)
{
    GTEST_SKIP() << "MEGA_USE_WSUPLOAD is disabled";
}

#endif
