#include "gtest/gtest.h"
#include "gfxworker/command_serializer.h"
#include "gfxworker/commands.h"
#include "gfxworker/comms.h"

using gfx::comms::CommandSerializer;
using gfx::comms::CommandNewGfx;
using gfx::comms::CommandNewGfxResponse;
using gfx::comms::CommandShutDown;
using gfx::comms::CommandShutDownResponse;
using gfx::comms::TimeoutMs;

namespace gfx
{
namespace comms
{
	bool operator==(const gfx::comms::CommandNewGfx& lhs, const gfx::comms::CommandNewGfx& rhs)
	{
		return lhs.Task.Path == rhs.Task.Path && lhs.Task.Sizes == rhs.Task.Sizes;
	}

	bool operator==(const gfx::comms::CommandNewGfxResponse& lhs, const gfx::comms::CommandNewGfxResponse& rhs)
	{
		return lhs.ErrorCode == rhs.ErrorCode && lhs.ErrorText == rhs.ErrorText && lhs.Images == rhs.Images;
	}

	bool operator==(const gfx::comms::CommandShutDown& /*lhs*/, const gfx::comms::CommandShutDown& /*rhs*/)
	{
		return true;
	}

	bool operator==(const gfx::comms::CommandShutDownResponse& /*lhs*/, const gfx::comms::CommandShutDownResponse& /*rhs*/)
	{
		return true;
	}
}
}

class StringReader : public gfx::comms::IReader
{
public:
	StringReader(std::string&& value) : mValue(std::move(value)), mIndex{0} {}
private:
	bool do_read(void* out, size_t n, TimeoutMs timeout) override;

	std::string mValue;
	size_t      mIndex;
};

bool StringReader::do_read(void* out, size_t n, TimeoutMs /*timeout*/)
{
	if (mIndex > mValue.size()) return false;

	if (mIndex + n > mValue.size()) return false;

	std::memcpy(out, mValue.data() + mIndex, n);

	mIndex += n;

	return true;
}

TEST(CommandSerializer, CommandNewGfxSerializeAndUnserializeSuccessfully)
{
	CommandNewGfx sourceCommand;
	sourceCommand.Task.Path = "c:\\path\\image.png";
	sourceCommand.Task.Sizes = std::vector<gfx::GfxSize>{ {250, 0} };

	auto data = CommandSerializer::serialize(&sourceCommand);
	ASSERT_NE(data, nullptr);

	StringReader reader(std::move(*data));
	auto command = CommandSerializer::unserialize(reader, TimeoutMs(5000));
	ASSERT_NE(command, nullptr);
	auto targetCommand = dynamic_cast<CommandNewGfx*>(command.get());
	ASSERT_NE(targetCommand, nullptr);
	ASSERT_EQ(sourceCommand, *targetCommand);
}

TEST(CommandSerializer, CommandNewGfxResponseSerializeAndUnserializeSuccessfully)
{
	CommandNewGfxResponse sourceCommand;
	sourceCommand.ErrorCode = 0;
	sourceCommand.ErrorText = "OK";
	sourceCommand.Images.push_back("imagedata");

	auto data = CommandSerializer::serialize(&sourceCommand);
	ASSERT_NE(data, nullptr);

	StringReader reader(std::move(*data));
	auto command = CommandSerializer::unserialize(reader, TimeoutMs(5000));
	ASSERT_NE(command, nullptr);
	auto targetCommand = dynamic_cast<CommandNewGfxResponse*>(command.get());
	ASSERT_NE(targetCommand, nullptr);
	ASSERT_EQ(sourceCommand, *targetCommand);
}

TEST(CommandSerializer, CommandShutdownSerializeAndUnserializeSuccessfully)
{
	CommandShutDown sourceCommand;

	auto data = CommandSerializer::serialize(&sourceCommand);
	ASSERT_NE(data, nullptr);

	StringReader reader(std::move(*data));
	auto command = CommandSerializer::unserialize(reader, TimeoutMs(5000));
	ASSERT_NE(command, nullptr);
	auto targetCommand = dynamic_cast<CommandShutDown*>(command.get());
	ASSERT_NE(targetCommand, nullptr);
	ASSERT_EQ(sourceCommand, *targetCommand);
}

TEST(CommandSerializer, CommandShutdownResponseSerializeAndUnserializeSuccessfully)
{
	CommandShutDownResponse sourceCommand;

	auto data = CommandSerializer::serialize(&sourceCommand);
	ASSERT_NE(data, nullptr);

	StringReader reader(std::move(*data));
	auto command = CommandSerializer::unserialize(reader, TimeoutMs(5000));
	ASSERT_NE(command, nullptr);
	auto targetCommand = dynamic_cast<CommandShutDownResponse*>(command.get());
	ASSERT_NE(targetCommand, nullptr);
	ASSERT_EQ(sourceCommand, *targetCommand);
}
