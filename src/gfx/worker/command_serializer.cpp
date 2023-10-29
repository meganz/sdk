#include "mega/gfx.h"
#include "mega/logging.h"
#include "mega/gfx/worker/commands.h"
#include "mega/gfx/worker/tasks.h"
#include "mega/gfx/worker/comms.h"
#include "mega/gfx/worker/command_serializer.h"
#include "mega/utils.h"
#include <stdint.h>

namespace
{
    using mega::GfxDimension;
    using mega::gfx::GfxTaskProcessStatus;
    using mega::gfx::CommandNewGfx;
    using mega::gfx::CommandNewGfxResponse;
    using mega::gfx::CommandShutDown;
    using mega::gfx::CommandShutDownResponse;
    using mega::gfx::CommandHello;
    using mega::gfx::CommandHelloResponse;
    using mega::gfx::CommandSupportFormats;
    using mega::gfx::CommandSupportFormatsResponse;
    using mega::gfx::CommandType;
    using mega::CacheableWriter;
    using mega::CacheableReader;

    class GfxSerializationHelper
    {
        static constexpr size_t MAX_VECT_SIZE = 100;
        static constexpr size_t MAX_STRING_SIZE = 5 * 1024 * 1024;
    public:

        static void serialize(CacheableWriter& writer, const GfxDimension& source)
        {
            writer.serializeu32(source.w());
            writer.serializeu32(source.h());
        }

        static void serialize(CacheableWriter& writer, const std::string& source)
        {
            writer.serializestring_u32(source);
        }

        template<typename T>
        static void serialize(CacheableWriter& writer, const std::vector<T>& target)
        {

            auto vecSize = target.size();
            assert(vecSize < std::numeric_limits<uint32_t>::max());
            writer.serializeu32(static_cast<uint32_t>(vecSize));
            for (const auto& entry : target)
            {
                GfxSerializationHelper::serialize(writer, entry);
            }
        }

        static bool unserialize(CacheableReader& reader, GfxDimension& target)
        {
            uint32_t w = 0;
            if (!reader.unserializeu32(w))
            {
                return false;
            }

            uint32_t h = 0;
            if (!reader.unserializeu32(h))
            {
                return false;
            }

            target.setW(w);
            target.setH(h);
            return true;
        }

        static bool unserialize(CacheableReader& reader, std::string& target)
        {
            return reader.unserializestring_u32(target);
        }

        template<typename T>
        static bool unserialize(CacheableReader& reader, std::vector<T>& target, const size_t maxVectSize = MAX_VECT_SIZE)
        {
            uint32_t vecSize = 0;
            if (!reader.unserializeu32(vecSize))
            {
                return false;
            }

            if (vecSize > maxVectSize)
            {
                return false;
            }

            target.resize(vecSize);
            for (auto& entry : target)
            {
                if (!GfxSerializationHelper::unserialize(reader, entry))
                {
                    return false;
                }
            }

            return true;
        }
    };

    struct CommandNewGfxSerializer
    {
        static std::string serialize(const CommandNewGfx& command);
        static bool unserialize(const std::string& data, CommandNewGfx& command);
    };

    struct CommandNewGfxResponseSerializer
    {
        static std::string serialize(const CommandNewGfxResponse& command);
        static bool unserialize(const std::string& data, CommandNewGfxResponse& command);
    };

    struct CommandShutDownSerializer
    {
        static std::string serialize(const CommandShutDown& command);
        static bool unserialize(const std::string& data, CommandShutDown& command);
    };

    struct CommandShutDownResponseSerializer
    {
        static std::string serialize(const CommandShutDownResponse& command);
        static bool unserialize(const std::string& data, CommandShutDownResponse& command);
    };

    struct CommandHelloSerializer
    {
        static std::string serialize(const CommandHello& command);
        static bool unserialize(const std::string& data, CommandHello& command);
    };

    struct CommandHelloResponseSerializer
    {
        static std::string serialize(const CommandHelloResponse& command);
        static bool unserialize(const std::string& data, CommandHelloResponse& command);
    };

    struct CommandSupportFormatsSerializer
    {
        static std::string serialize(const CommandSupportFormats& command);
        static bool unserialize(const std::string& data, CommandSupportFormats& command);
    };

    struct CommandSupportFormatsResponseSerializer
    {
        static std::string serialize(const CommandSupportFormatsResponse& command);
        static bool unserialize(const std::string& data, CommandSupportFormatsResponse& command);
    };

    std::string CommandNewGfxSerializer::serialize(const CommandNewGfx& command)
    {
        std::string toret;
        CacheableWriter writer(toret);
        writer.serializestring_u32(command.Task.Path);
        GfxSerializationHelper::serialize(writer, command.Task.Dimensions);
        return toret;
    }

    bool CommandNewGfxSerializer::unserialize(const std::string& data, CommandNewGfx& command)
    {
        CacheableReader reader(data);

        // path
        if (!reader.unserializestring_u32(command.Task.Path))
        {
            return false;
        }

        // dimensions
        if (!GfxSerializationHelper::unserialize(reader, command.Task.Dimensions))
        {
            return false;
        }

        // empty dimensions considered an invalid task
        if (command.Task.Dimensions.size() == 0)
        {
            return false;
        }

        return true;
    }

    std::string CommandNewGfxResponseSerializer::serialize(const CommandNewGfxResponse& command)
    {
        std::string toret;
        CacheableWriter writer(toret);
        writer.serializeu32(command.ErrorCode);
        writer.serializestring_u32(command.ErrorText);
        GfxSerializationHelper::serialize(writer, command.Images);
        return toret;
    }

    bool CommandNewGfxResponseSerializer::unserialize(const std::string& data, CommandNewGfxResponse& command)
    {
        CacheableReader reader(data);

        // ErrorCode
        if (!reader.unserializeu32(command.ErrorCode))
        {
            return false;
        }

        // ErrorText
        if (!reader.unserializestring_u32(command.ErrorText))
        {
            return false;
        }

        // images
        if (!GfxSerializationHelper::unserialize(reader, command.Images))
        {
            return false;
        }

        return true;
    }

    std::string CommandShutDownSerializer::serialize(const CommandShutDown& /*command*/)
    {
        return "";
    }

    bool CommandShutDownSerializer::unserialize(const std::string& /*data*/, CommandShutDown& /*command*/)
    {
        return true;
    }

    std::string CommandShutDownResponseSerializer::serialize(const CommandShutDownResponse& /*command*/)
    {
        return "";
    }

    bool CommandShutDownResponseSerializer::unserialize(const std::string& /*data*/, CommandShutDownResponse& /*command*/)
    {
        return true;
    }

    std::string CommandHelloSerializer::serialize(const CommandHello& command)
    {
        std::string toret;
        CacheableWriter writer(toret);
        writer.serializestring_u32(command.Text);
        return toret;
    }

    bool CommandHelloSerializer::unserialize(const std::string& data, CommandHello& command)
    {
        CacheableReader reader(data);

        // Text
        if (!reader.unserializestring_u32(command.Text))
        {
            return false;
        }

        return true;
    }

    std::string CommandHelloResponseSerializer::serialize(const CommandHelloResponse& command)
    {
        std::string toret;
        CacheableWriter writer(toret);
        writer.serializestring_u32(command.Text);
        return toret;
    }

    bool CommandHelloResponseSerializer::unserialize(const std::string& data, CommandHelloResponse& command)
    {
        CacheableReader reader(data);

        // Text
        if (!reader.unserializestring_u32(command.Text))
        {
            return false;
        }

        return true;
    }

    std::string CommandSupportFormatsSerializer::serialize(const CommandSupportFormats& /*command*/)
    {
        return "";
    }

    bool CommandSupportFormatsSerializer::unserialize(const std::string& /*data*/, CommandSupportFormats& /*command*/)
    {
        return true;
    }

    std::string CommandSupportFormatsResponseSerializer::serialize(const CommandSupportFormatsResponse& command)
    {
        std::string toret;
        CacheableWriter writer(toret);
        writer.serializestring_u32(command.formats);
        writer.serializestring_u32(command.videoformats);
        return toret;
    }

    bool CommandSupportFormatsResponseSerializer::unserialize(const std::string& data, CommandSupportFormatsResponse& command)
    {
        CacheableReader reader(data);

        // formats
        if (!reader.unserializestring_u32(command.formats))
        {
            return false;
        }

        // videoformats
        if (!reader.unserializestring_u32(command.videoformats))
        {
            return false;
        }

        return true;
    }

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
    std::string commandData;
    if (!CommandSerializer::serializeHelper(command, commandData))
    {
        return nullptr;
    }
    writer.serializestring_u32(commandData);

    return ::mega::make_unique<std::string>(std::move(dataToReturn));
}

bool CommandSerializer::serializeHelper(ICommand* command, std::string& data)
{
    switch (command->type())
    {
    case CommandType::NEW_GFX:
        if (auto c = dynamic_cast<CommandNewGfx*>(command))
        {
            data = CommandNewGfxSerializer::serialize(*c);
            return true;
        }
    case CommandType::NEW_GFX_RESPONSE:
        if (auto c = dynamic_cast<CommandNewGfxResponse*>(command))
        {
            data = CommandNewGfxResponseSerializer::serialize(*c);
            return true;
        }
    case CommandType::SHUTDOWN:
        if (auto c = dynamic_cast<CommandShutDown*>(command))
        {
            data = CommandShutDownSerializer::serialize(*c);
            return true;
        }
    case CommandType::SHUTDOWN_RESPONSE:
        if (auto c = dynamic_cast<CommandShutDownResponse*>(command))
        {
            data = CommandShutDownResponseSerializer::serialize(*c);
            return true;
        }
    case CommandType::HELLO:
        if (auto c = dynamic_cast<CommandHello*>(command))
        {
            data = CommandHelloSerializer::serialize(*c);
            return true;
        }
    case CommandType::HELLO_RESPONSE:
        if (auto c = dynamic_cast<CommandHelloResponse*>(command))
        {
            data = CommandHelloResponseSerializer::serialize(*c);
            return true;
        }
    case CommandType::SUPPORT_FORMATS:
        if (auto c = dynamic_cast<CommandSupportFormats*>(command))
        {
            data = CommandSupportFormatsSerializer::serialize(*c);
            return true;
        }
    case CommandType::SUPPORT_FORMATS_RESPONSE:
        if (auto c = dynamic_cast<CommandSupportFormatsResponse*>(command))
        {
            data = CommandSupportFormatsResponseSerializer::serialize(*c);
            return true;
        }
    default:
        break;
    }
    return false;
}

bool CommandSerializer::unserializeHelper(IReader& reader, uint32_t& data, TimeoutMs timeout)
{
    return reader.read(&data, sizeof(uint32_t), timeout);
}

bool CommandSerializer::unserializeHelper(IReader& reader, std::string& data, TimeoutMs timeout)
{
    uint32_t len;
    if (!unserializeHelper(reader, len, timeout))
    {
        return false;
    }
    if (len == 0) // pipe couldn't read 0 byte, terminate early
    {
        return true;
    }
    data.resize(len);
    return reader.read(data.data(), len, timeout);
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
    if (!unserializeHelper(reader, data, timeout))
    {
        return nullptr;
    }

    return unserializeHelper(static_cast<CommandType>(type), data);

}

std::unique_ptr<ICommand> CommandSerializer::unserializeHelper(CommandType type, const std::string& data)
{
    switch (type)
    {
    case CommandType::NEW_GFX:
    {
        CommandNewGfx command;
        if (CommandNewGfxSerializer::unserialize(data, command))
        {
            return mega::make_unique<CommandNewGfx>(std::move(command));
        }
    }
    case CommandType::NEW_GFX_RESPONSE:
    {
        CommandNewGfxResponse command;
        if (CommandNewGfxResponseSerializer::unserialize(data, command))
        {
            return mega::make_unique<CommandNewGfxResponse>(std::move(command));
        }
    }
    case CommandType::SHUTDOWN:
    {
        CommandShutDown command;
        if (CommandShutDownSerializer::unserialize(data, command))
        {
            return mega::make_unique<CommandShutDown>(std::move(command));
        }
    }
    case CommandType::SHUTDOWN_RESPONSE:
    {
        CommandShutDownResponse command;
        if (CommandShutDownResponseSerializer::unserialize(data, command))
        {
            return mega::make_unique<CommandShutDownResponse>(std::move(command));
        }
    }
    case CommandType::HELLO:
    {
        CommandHello command;
        if (CommandHelloSerializer::unserialize(data, command))
        {
            return mega::make_unique<CommandHello>(std::move(command));
        }
    }
    case CommandType::HELLO_RESPONSE:
    {
        CommandHelloResponse command;
        if (CommandHelloResponseSerializer::unserialize(data, command))
        {
            return mega::make_unique<CommandHelloResponse>(std::move(command));
        }
    }
    case CommandType::SUPPORT_FORMATS:
    {
        CommandSupportFormats command;
        if (CommandSupportFormatsSerializer::unserialize(data, command))
        {
            return mega::make_unique<CommandSupportFormats>(std::move(command));
        }
    }
    case CommandType::SUPPORT_FORMATS_RESPONSE:
    {
        CommandSupportFormatsResponse command;
        if (CommandSupportFormatsResponseSerializer::unserialize(data, command))
        {
            return mega::make_unique<CommandSupportFormatsResponse>(std::move(command));
        }
    }
    default:
        break;
    }

    return nullptr;
}

}
}
