#include "gtest/gtest.h"
#include "mega/gfx/worker/command_serializer.h"
#include "mega/gfx/worker/commands.h"
#include "mega/gfx/worker/comms.h"

#include <chrono>

using mega::GfxDimension;
using mega::gfx::CommandHello;
using mega::gfx::CommandHelloResponse;
using mega::gfx::CommandNewGfx;
using mega::gfx::CommandNewGfxResponse;
using mega::gfx::CommandSerializer;
using mega::gfx::CommandShutDown;
using mega::gfx::CommandShutDownResponse;
using mega::gfx::CommandSupportFormats;
using mega::gfx::CommandSupportFormatsResponse;
using mega::gfx::IReader;
using std::chrono::milliseconds;

using namespace std::chrono_literals;

namespace mega
{
namespace gfx
{
    bool operator==(const CommandNewGfx& lhs, const CommandNewGfx& rhs)
    {
        return lhs.Task.Path == rhs.Task.Path && lhs.Task.Dimensions == rhs.Task.Dimensions;
    }

    bool operator==(const CommandNewGfxResponse& lhs, const CommandNewGfxResponse& rhs)
    {
        return lhs.ErrorCode == rhs.ErrorCode && lhs.ErrorText == rhs.ErrorText && lhs.Images == rhs.Images;
    }

    bool operator==(const CommandShutDown& /*lhs*/, const CommandShutDown& /*rhs*/)
    {
        return true;
    }

    bool operator==(const CommandShutDownResponse& /*lhs*/, const CommandShutDownResponse& /*rhs*/)
    {
        return true;
    }

    bool operator==(const CommandHello& lhs, const CommandHello& rhs)
    {
        return lhs.Text == rhs.Text;
    }

    bool operator==(const CommandHelloResponse& lhs, const CommandHelloResponse& rhs)
    {
        return lhs.Text == rhs.Text;
    }

    bool operator==(const CommandSupportFormats& /*lhs*/, const CommandSupportFormats& /*rhs*/)
    {
        return true;
    }

    bool operator==(const CommandSupportFormatsResponse& lhs, const CommandSupportFormatsResponse& rhs)
    {
        return lhs.formats == rhs.formats && lhs.videoformats == rhs.videoformats;
    }
}
}

class StringReader : public IReader
{
public:
    StringReader(std::string&& value) : mValue(std::move(value)), mIndex{0} {}
private:
    bool doRead(void* out, size_t n, milliseconds timeout) override;

    std::string mValue;
    size_t      mIndex;
};

bool StringReader::doRead(void* out, size_t n, milliseconds /*timeout*/)
{
    if (mIndex > mValue.size()) return false;

    if (mIndex + n > mValue.size()) return false;

    std::memcpy(out, mValue.data() + mIndex, n);

    mIndex += n;

    return true;
}

TEST(GfxCommandSerializer, CommandNewGfxSerializeAndUnserializeSuccessfully)
{
    CommandNewGfx sourceCommand;
    sourceCommand.Task.Path = "c:\\path\\image.png";
    sourceCommand.Task.Dimensions = std::vector<GfxDimension>{ {250, 0} };

    auto data = CommandSerializer::serialize(&sourceCommand);
    ASSERT_NE(data, nullptr);

    StringReader reader(std::move(*data));
    auto command = CommandSerializer::unserialize(reader, 5000ms);
    ASSERT_NE(command, nullptr);
    auto targetCommand = dynamic_cast<CommandNewGfx*>(command.get());
    ASSERT_NE(targetCommand, nullptr);
    ASSERT_EQ(sourceCommand, *targetCommand);
}

TEST(GfxCommandSerializer, CommandNewGfxResponseSerializeAndUnserializeSuccessfully)
{
    CommandNewGfxResponse sourceCommand;
    sourceCommand.ErrorCode = 0;
    sourceCommand.ErrorText = "OK";
    sourceCommand.Images.push_back("imagedata");

    auto data = CommandSerializer::serialize(&sourceCommand);
    ASSERT_NE(data, nullptr);

    StringReader reader(std::move(*data));
    auto command = CommandSerializer::unserialize(reader, 5000ms);
    ASSERT_NE(command, nullptr);
    auto targetCommand = dynamic_cast<CommandNewGfxResponse*>(command.get());
    ASSERT_NE(targetCommand, nullptr);
    ASSERT_EQ(sourceCommand, *targetCommand);
}

TEST(GfxCommandSerializer, CommandShutdownSerializeAndUnserializeSuccessfully)
{
    CommandShutDown sourceCommand;

    auto data = CommandSerializer::serialize(&sourceCommand);
    ASSERT_NE(data, nullptr);

    StringReader reader(std::move(*data));
    auto command = CommandSerializer::unserialize(reader, 5000ms);
    ASSERT_NE(command, nullptr);
    auto targetCommand = dynamic_cast<CommandShutDown*>(command.get());
    ASSERT_NE(targetCommand, nullptr);
    ASSERT_EQ(sourceCommand, *targetCommand);
}

TEST(GfxCommandSerializer, CommandShutdownResponseSerializeAndUnserializeSuccessfully)
{
    CommandShutDownResponse sourceCommand;

    auto data = CommandSerializer::serialize(&sourceCommand);
    ASSERT_NE(data, nullptr);

    StringReader reader(std::move(*data));
    auto command = CommandSerializer::unserialize(reader, 5000ms);
    ASSERT_NE(command, nullptr);
    auto targetCommand = dynamic_cast<CommandShutDownResponse*>(command.get());
    ASSERT_NE(targetCommand, nullptr);
    ASSERT_EQ(sourceCommand, *targetCommand);
}

TEST(GfxCommandSerializer, CommandHelloSerializeAndUnserializeSuccessfully)
{
    CommandHello sourceCommand;

    auto data = CommandSerializer::serialize(&sourceCommand);
    ASSERT_NE(data, nullptr);

    StringReader reader(std::move(*data));
    auto command = CommandSerializer::unserialize(reader, 5000ms);
    ASSERT_NE(command, nullptr);
    auto targetCommand = dynamic_cast<CommandHello*>(command.get());
    ASSERT_NE(targetCommand, nullptr);
    ASSERT_EQ(sourceCommand, *targetCommand);
}

TEST(GfxCommandSerializer, CommandHelloResponseSerializeAndUnserializeSuccessfully)
{
    CommandHelloResponse sourceCommand;

    auto data = CommandSerializer::serialize(&sourceCommand);
    ASSERT_NE(data, nullptr);

    StringReader reader(std::move(*data));
    auto command = CommandSerializer::unserialize(reader, 5000ms);
    ASSERT_NE(command, nullptr);
    auto targetCommand = dynamic_cast<CommandHelloResponse*>(command.get());
    ASSERT_NE(targetCommand, nullptr);
    ASSERT_EQ(sourceCommand, *targetCommand);
}

TEST(GfxCommandSerializer, CommandSupportFormatsSerializeAndUnserializeSuccessfully)
{
    CommandSupportFormats sourceCommand;

    auto data = CommandSerializer::serialize(&sourceCommand);
    ASSERT_NE(data, nullptr);

    StringReader reader(std::move(*data));
    auto command = CommandSerializer::unserialize(reader, 5000ms);
    ASSERT_NE(command, nullptr);
    auto targetCommand = dynamic_cast<CommandSupportFormats*>(command.get());
    ASSERT_NE(targetCommand, nullptr);
    ASSERT_EQ(sourceCommand, *targetCommand);
}

TEST(GfxCommandSerializer, CommandSupportFormatsResponseSerializeAndUnserializeSuccessfully)
{
    CommandSupportFormatsResponse sourceCommand;

    auto data = CommandSerializer::serialize(&sourceCommand);
    ASSERT_NE(data, nullptr);

    StringReader reader(std::move(*data));
    auto command = CommandSerializer::unserialize(reader, 5000ms);
    ASSERT_NE(command, nullptr);
    auto targetCommand = dynamic_cast<CommandSupportFormatsResponse*>(command.get());
    ASSERT_NE(targetCommand, nullptr);
    ASSERT_EQ(sourceCommand, *targetCommand);
}
