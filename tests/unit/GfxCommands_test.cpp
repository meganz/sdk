#include "gtest/gtest.h"
#include "mega/gfx/worker/command_serializer.h"
#include "mega/gfx/worker/commands.h"
#include "mega/gfx/worker/comms.h"
#include "mega/utils.h"

#include <chrono>

using mega::CacheableReader;
using mega::CacheableWriter;
using mega::GfxDimension;
using mega::MediaProperties;
using mega::gfx::CommandHello;
using mega::gfx::CommandHelloResponse;
using mega::gfx::CommandMediaInfo;
using mega::gfx::CommandMediaInfoResponse;
using mega::gfx::CommandNewGfx;
using mega::gfx::CommandNewGfxResponse;
using mega::gfx::CommandSerializer;
using mega::gfx::CommandShutDown;
using mega::gfx::CommandShutDownResponse;
using mega::gfx::CommandSupportFormats;
using mega::gfx::CommandSupportFormatsResponse;
using mega::gfx::CommandType;
using mega::gfx::GfxSerializationHelper;
using mega::gfx::GfxTask;
using mega::gfx::ICommand;
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

    bool operator==(const CommandMediaInfo& lhs, const CommandMediaInfo& rhs)
    {
        return lhs.Path == rhs.Path;
    }

    bool operator==(const CommandMediaInfoResponse& lhs, const CommandMediaInfoResponse& rhs)
    {
        return lhs.ErrorCode == rhs.ErrorCode &&
               lhs.MediaInfo.shortformat == rhs.MediaInfo.shortformat &&
               lhs.MediaInfo.width == rhs.MediaInfo.width &&
               lhs.MediaInfo.height == rhs.MediaInfo.height &&
               lhs.MediaInfo.fps == rhs.MediaInfo.fps &&
               lhs.MediaInfo.playtime == rhs.MediaInfo.playtime &&
               lhs.MediaInfo.containerid == rhs.MediaInfo.containerid &&
               lhs.MediaInfo.videocodecid == rhs.MediaInfo.videocodecid &&
               lhs.MediaInfo.audiocodecid == rhs.MediaInfo.audiocodecid &&
               lhs.MediaInfo.is_VFR == rhs.MediaInfo.is_VFR &&
               lhs.MediaInfo.no_audio == rhs.MediaInfo.no_audio &&
               lhs.MediaInfo.containerName == rhs.MediaInfo.containerName &&
               lhs.MediaInfo.videocodecNames == rhs.MediaInfo.videocodecNames &&
               lhs.MediaInfo.audiocodecNames == rhs.MediaInfo.audiocodecNames &&
               lhs.MediaInfo.containerFormat == rhs.MediaInfo.containerFormat &&
               lhs.MediaInfo.videocodecFormat == rhs.MediaInfo.videocodecFormat &&
               lhs.MediaInfo.audiocodecFormat == rhs.MediaInfo.audiocodecFormat;
    }
}
}

struct CommandNewGfx0: public ICommand
{
    GfxTask Task;

    CommandType type() const override
    {
        return CommandType::NEW_GFX;
    }

    std::string typeStr() const override
    {
        return "NEW_GFX";
    };

    std::string serialize() const override;

    bool unserialize(const std::string& data) override;
};

std::string CommandNewGfx0::serialize() const
{
    std::string message;
    CacheableWriter writer(message);

    writer.serializestring_u32(Task.Path);
    GfxSerializationHelper::serialize(writer, Task.Dimensions);

    return message;
}

bool CommandNewGfx0::unserialize(const std::string& data)
{
    CacheableReader reader(data);

    // Couldn't deserialize image path.
    if (!reader.unserializestring_u32(Task.Path))
        return false;

    // Couldn't deserialize image dimensions.
    if (!GfxSerializationHelper::unserialize(reader, Task.Dimensions))
        return false;

    // No dimensions specified.
    if (Task.Dimensions.empty())
        return false;

    // All done.
    return true;
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
    // Convenience.
    auto deserialize = [](auto&& blob) -> std::unique_ptr<CommandNewGfx>
    {
        StringReader reader(std::forward<decltype(blob)>(blob));

        // Try and deserialize the command from blob.
        auto command = CommandSerializer::unserialize(reader, 5s);

        // Couldn't deserialize the command.
        if (!command)
            return nullptr;

        auto* pointer = dynamic_cast<CommandNewGfx*>(command.get());

        // Couldn't create an instance of the right command.
        if (!pointer)
            return nullptr;

        command.release();

        // Return command instance to caller.
        return std::unique_ptr<CommandNewGfx>(pointer);
    }; // deserialize

    // Make sure we can deserialize old NewGfx commands.
    {
        CommandNewGfx0 command0;

        command0.Task.Dimensions = std::vector<GfxDimension>{{250, 0}};
        command0.Task.Path = "image.png";

        auto blob = CommandSerializer::serialize(&command0);
        ASSERT_NE(blob, nullptr);

        auto command1 = deserialize(std::move(*blob));
        ASSERT_NE(command1, nullptr);

        ASSERT_EQ(command1->Task.Dimensions, command0.Task.Dimensions);
        ASSERT_EQ(command1->Task.Name, "image.png");
        ASSERT_EQ(command1->Task.Path, command0.Task.Path);
    }

    CommandNewGfx command0;

    command0.Task.Dimensions = std::vector<GfxDimension>{{32, 32}, {64, 64}};
    command0.Task.Name = "frob.png";
    command0.Task.Path = "image.png";

    auto blob = CommandSerializer::serialize(&command0);
    ASSERT_NE(blob, nullptr);

    auto command1 = deserialize(std::move(*blob));
    ASSERT_NE(command1, nullptr);

    ASSERT_EQ(command1->Task.Dimensions, command0.Task.Dimensions);
    ASSERT_EQ(command1->Task.Name, command0.Task.Name);
    ASSERT_EQ(command1->Task.Path, command0.Task.Path);
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

TEST(GfxCommandSerializer, CommandMediaInfoSerializeAndUnserializeSuccessfully)
{
    CommandMediaInfo sourceCommand;
    sourceCommand.Path = "/some/where/clip.mp4";

    auto data = CommandSerializer::serialize(&sourceCommand);
    ASSERT_NE(data, nullptr);

    StringReader reader(std::move(*data));
    auto command = CommandSerializer::unserialize(reader, 5000ms);
    ASSERT_NE(command, nullptr);
    auto targetCommand = dynamic_cast<CommandMediaInfo*>(command.get());
    ASSERT_NE(targetCommand, nullptr);
    ASSERT_EQ(sourceCommand, *targetCommand);
}

TEST(GfxCommandSerializer, CommandMediaInfoResponseSerializeAndUnserializeSuccessfully)
{
    CommandMediaInfoResponse sourceCommand;
    sourceCommand.MediaInfo.shortformat = 10;
    sourceCommand.MediaInfo.width = 1920;
    sourceCommand.MediaInfo.height = 1080;
    sourceCommand.MediaInfo.fps = 30;
    sourceCommand.MediaInfo.playtime = 120;
    sourceCommand.MediaInfo.containerid = 15;
    sourceCommand.MediaInfo.videocodecid = 16;
    sourceCommand.MediaInfo.audiocodecid = 17;
    sourceCommand.MediaInfo.is_VFR = true;
    sourceCommand.MediaInfo.no_audio = false;
    sourceCommand.MediaInfo.containerName = "MPEG-4";
    sourceCommand.MediaInfo.videocodecNames = "AVC";
    sourceCommand.MediaInfo.audiocodecNames = "AAC";
    sourceCommand.MediaInfo.containerFormat = "mp42";
    sourceCommand.MediaInfo.videocodecFormat = "avc1";
    sourceCommand.MediaInfo.audiocodecFormat = "mp4a";

    auto data = CommandSerializer::serialize(&sourceCommand);
    ASSERT_NE(data, nullptr);

    StringReader reader(std::move(*data));
    auto command = CommandSerializer::unserialize(reader, 5000ms);
    ASSERT_NE(command, nullptr);
    auto targetCommand = dynamic_cast<CommandMediaInfoResponse*>(command.get());
    ASSERT_NE(targetCommand, nullptr);
    ASSERT_EQ(sourceCommand, *targetCommand);
}

// A worker that could not extract sends ErrorCode set and no properties. The response must
// round-trip, so the client can tell extraction failure from a malformed payload.
TEST(GfxCommandSerializer, CommandMediaInfoResponseCarriesTheExtractionFailure)
{
    CommandMediaInfoResponse sourceCommand;
    sourceCommand.ErrorCode = 1;

    auto data = CommandSerializer::serialize(&sourceCommand);
    ASSERT_NE(data, nullptr);

    StringReader reader(std::move(*data));
    auto command = CommandSerializer::unserialize(reader, 5000ms);
    ASSERT_NE(command, nullptr);
    auto targetCommand = dynamic_cast<CommandMediaInfoResponse*>(command.get());
    ASSERT_NE(targetCommand, nullptr);
    EXPECT_EQ(1u, targetCommand->ErrorCode);
    EXPECT_EQ(0u, targetCommand->MediaInfo.width) << "no properties should be reported";
}

TEST(GfxCommandSerializer, CommandMediaInfoResponseRejectsATruncatedPayload)
{
    MediaProperties properties;
    properties.shortformat = 10;
    properties.width = 1920;
    properties.height = 1080;
    properties.containerName = "MPEG-4";
    properties.videocodecNames = "AVC";
    const std::string serializedProperties = properties.serialize();

    // Chop the last byte off the properties and re-wrap them at their new length, so the outer
    // length-prefixed read succeeds and MediaProperties::deserialize is what has to reject the
    // payload. Truncating the whole command instead would fail in that outer read first, and
    // would pass whether or not the properties are checked at all.
    std::string payload;
    CacheableWriter writer(payload);
    writer.serializeu32(0); // ErrorCode 0, so the properties are read rather than skipped
    writer.serializestring_u32(serializedProperties.substr(0, serializedProperties.size() - 1));

    CommandMediaInfoResponse response;
    ASSERT_FALSE(response.unserialize(payload));
}
