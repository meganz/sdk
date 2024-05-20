#include "mega/logging.h"
#include "mega/gfx/worker/command_serializer.h"
#include "mega/utils.h"


namespace
{
    template<typename T>
    bool isInOpenEndRange(const T& value, const T& low, const T& high)
    {
        return value >= low && value < high;
    }

}

namespace mega
{
namespace gfx
{
enum class CommandProtocolVersion
{
    V_1 = 1,
    UNSUPPORTED
};

bool ProtocolWriter::writeCommand(ICommand* command, TimeoutMs timeout) const
{
    auto dataToWrite = CommandSerializer::serialize(command);
    if (!dataToWrite)
    {
        return false;
    }

    if (!mWriter->write(dataToWrite->data(), dataToWrite->length(), timeout))
    {
        return false;
    }

    return true;
}

std::unique_ptr<ICommand> ProtocolReader::readCommand(TimeoutMs timeout) const
{
    return CommandSerializer::unserialize(*mReader, timeout);
}

std::unique_ptr<std::string> CommandSerializer::serialize(ICommand* command)
{
    std::string dataToReturn;
    CacheableWriter writer(dataToReturn);

    // protocol version unit32_t
    writer.serializeu32(static_cast<uint32_t>(CommandProtocolVersion::V_1));

    // command type
    writer.serializeu32(static_cast<uint32_t>(command->type()));

    // length and command
    std::string commandData = command->serialize();

    writer.serializestring_u32(commandData);

    return std::make_unique<std::string>(std::move(dataToReturn));
}

bool CommandSerializer::unserializeUInt32(IReader& reader, uint32_t& data, TimeoutMs timeout)
{
    return reader.read(&data, sizeof(uint32_t), timeout);
}

bool CommandSerializer::unserializeString(IReader& reader, std::string& data, TimeoutMs timeout)
{
    uint32_t len;
    if (!unserializeUInt32(reader, len, timeout))
    {
        return false;
    }
    if (len == 0) // pipe couldn't read 0 byte, terminate early
    {
        return true;
    }
    data.resize(len);
    return reader.read(const_cast<char *>(data.data()), len, timeout);
}

std::unique_ptr<ICommand> CommandSerializer::unserialize(IReader& reader, TimeoutMs timeout)
{
    // proto version unit32_t
    uint32_t protoVer;
    if (!reader.read(&protoVer, sizeof(protoVer), timeout))
    {
        return nullptr;
    }
    if (protoVer != static_cast<uint32_t>(CommandProtocolVersion::V_1))
    {
        return nullptr;
    }

    // command type
    uint32_t type;
    if (!reader.read(&type, sizeof(type), timeout))
    {
        return nullptr;
    }

    if (!isInOpenEndRange(type, static_cast<uint32_t>(CommandType::BEGIN), static_cast<uint32_t>(CommandType::END)))
    {
        return nullptr;
    }

    // command data
    std::string data;
    if (!unserializeString(reader, data, timeout))
    {
        return nullptr;
    }

    return unserializeCommand(static_cast<CommandType>(type), data);

}

std::unique_ptr<ICommand> CommandSerializer::unserializeCommand(CommandType type, const std::string& data)
{
    auto command = ICommand::factory(type);

    if (!command) return nullptr;

    if (!command->unserialize(data))
    {
        LOG_err << "CommandSerializer::unserializeCommand unable to unseriaize";
        return nullptr;
    }

    return command;
}

}
}
