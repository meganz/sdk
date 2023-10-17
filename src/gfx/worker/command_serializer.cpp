#include "mega/gfx/worker/commands.h"
#include "mega/logging.h"
#include "mega/gfx/worker/tasks.h"
#include "mega/gfx/worker/comms.h"
#include "mega/gfx/worker/command_serializer.h"
#include <stdint.h>

namespace
{
    using mega::gfx::GfxSize;
    using mega::gfx::GfxTaskProcessStatus;
    using mega::gfx::GfxSerializeVersion;
    using mega::gfx::CommandNewGfx;
    using mega::gfx::CommandNewGfxResponse;
    using mega::gfx::CommandShutDown;
    using mega::gfx::CommandShutDownResponse;
    using mega::gfx::CommandHello;
    using mega::gfx::CommandHelloResponse;
    using mega::gfx::CommandSupportFormats;
    using mega::gfx::CommandSupportFormatsResponse;
    using mega::gfx::CommandType;

    class GfxSerializationHelper
    {
        template<typename T>
        static void serialize_as_uint32_t(std::string& target, const T source)
        {
            uint32_t fixed = static_cast<uint32_t>(source);
            target.append(reinterpret_cast<const char*>(&fixed), sizeof(uint32_t));
        }

        template<typename T>
        static size_t unserialize_as_uint32_t(T& target, const char* source, const size_t len)
        {
            uint32_t fixed = 0;
            size_t consume = sizeof(uint32_t);
            if (consume > len)
            {
                return 0;
            }
            memcpy(reinterpret_cast<void*>(&fixed), source, consume);
            target = static_cast<T>(fixed);
            return consume;
        }

        static constexpr size_t MAX_VECT_SIZE = 100;
        static constexpr size_t MAX_STRING_SIZE = 5 * 1024 * 1024;
    public:

        template<typename T>
        static void serialize(std::string& target, const std::vector<T>& source)
        {

            auto vecSize = source.size();
            assert(vecSize < std::numeric_limits<uint32_t>::max());
            GfxSerializationHelper::serialize_as_uint32_t(target, vecSize);
            for (const auto& entry : source)
            {
                GfxSerializationHelper::serialize(target, entry);
            }
        }

        static void serialize(std::string& target, const std::string& source);

        static void serialize(std::string& target, const bool source);

        static void serialize(std::string& target, const uint64_t source);

        static void serialize(std::string& target, const uint32_t source);

        static void serialize(std::string& target, const GfxSize& source);

        static void serialize(std::string& target, const GfxTaskProcessStatus source);

        static void serialize(std::string& target, const GfxSerializeVersion source);

        static void serialize(std::string& target, const CommandType source);

        template<typename T>
        static size_t unserialize(std::vector<T>& target, const char* source, const size_t len, const size_t maxVectSize = MAX_VECT_SIZE)
        {
            size_t count = 0;
            typename std::vector<T>::size_type vecSize = 0;
            size_t consumed = GfxSerializationHelper::unserialize_as_uint32_t(vecSize, source, len);
            if (!consumed)
            {
                return 0;
            }
            count += consumed;
            if (vecSize > maxVectSize)
            {
                return 0;
            }
            target.resize(vecSize);
            for (auto& entry : target)
            {
                consumed = GfxSerializationHelper::unserialize(entry, source + count, len - count);
                if (!consumed)
                {
                    return 0;
                }
                count += consumed;
            }
            return count;
        }

        static size_t unserialize(std::string& target, const char* source, const size_t len, const size_t maxStringSize = MAX_STRING_SIZE);

        static size_t unserialize(bool& target, const char* source, const size_t len);

        static size_t unserialize(uint32_t& target, const char* source, const size_t len);

        static size_t unserialize(uint64_t& target, const char* source, const size_t len);

        static size_t unserialize(GfxSize& target, const char* source, const size_t len);

        static size_t unserialize(GfxTaskProcessStatus& target, const char* source, const size_t len);

        static size_t unserialize(GfxSerializeVersion& target, const char* source, const size_t len);
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

    void GfxSerializationHelper::serialize(std::string& target, const std::string& source)
    {
        auto strSize = source.size();
        assert(strSize < std::numeric_limits<uint32_t>::max());
        GfxSerializationHelper::serialize_as_uint32_t(target, strSize);
        target.append(reinterpret_cast<const char*>(source.data()), strSize);
    }

    void GfxSerializationHelper::serialize(std::string& target, const bool source)
    {
        uint8_t boolean = source ? 1 : 0;
        target.append(reinterpret_cast<const char*>(&boolean), sizeof(uint8_t));
    }

    void GfxSerializationHelper::serialize(std::string& target, const uint32_t source)
    {
        target.append(reinterpret_cast<const char*>(&source), sizeof(uint32_t));
    }

    void GfxSerializationHelper::serialize(std::string& target, const uint64_t source)
    {
        target.append(reinterpret_cast<const char*>(&source), sizeof(uint64_t));
    }

    void GfxSerializationHelper::serialize(std::string& target, const GfxSize& source)
    {
        GfxSerializationHelper::serialize_as_uint32_t(target, source.w());
        GfxSerializationHelper::serialize_as_uint32_t(target, source.h());
    }

    void GfxSerializationHelper::serialize(std::string& target, const GfxTaskProcessStatus source)
    {
        GfxSerializationHelper::serialize_as_uint32_t(target, source);
    }

    void GfxSerializationHelper::serialize(std::string& target, const GfxSerializeVersion source)
    {
        GfxSerializationHelper::serialize_as_uint32_t(target, source);
    }

    void GfxSerializationHelper::serialize(std::string& target, const CommandType source)
    {
        GfxSerializationHelper::serialize_as_uint32_t(target, source);
    }

    size_t GfxSerializationHelper::unserialize(std::string& target, const char* source, const size_t len, const size_t maxStringSize)
    {
        size_t count = 0;
        std::string::size_type strSize;
        size_t consumed = GfxSerializationHelper::unserialize_as_uint32_t(strSize, source, len);
        if (!consumed)
        {
            return 0;
        }
        count += consumed;
        if (strSize > maxStringSize)
        {
            return 0;
        }
        if (count + strSize > len)
        {
            return 0;
        }
        target = std::string(source + count, strSize);
        count += strSize;
        return count;
    }

    size_t GfxSerializationHelper::unserialize(bool& target, const char* source, const size_t len)
    {
        uint8_t boolean = 0;
        size_t consume = sizeof(uint8_t);
        if (consume > len)
        {
            return 0;
        }
        memcpy(reinterpret_cast<void*>(&boolean), source, consume);
        target = boolean > 0 ? true : false;
        return consume;
    }

    size_t GfxSerializationHelper::unserialize(uint32_t& target, const char* source, const size_t len)
    {
        size_t consume = sizeof(uint32_t);
        if (consume > len)
        {
            return 0;
        }
        memcpy(reinterpret_cast<void*>(&target), source, consume);
        return consume;
    }

    size_t GfxSerializationHelper::unserialize(uint64_t& target, const char* source, const size_t len)
    {
        size_t consume = sizeof(uint64_t);
        if (consume > len)
        {
            return 0;
        }
        memcpy(reinterpret_cast<void*>(&target), source, consume);
        return consume;
    }

    size_t GfxSerializationHelper::unserialize(GfxSize& target, const char* source, const size_t len)
    {
        size_t count = 0;
        int w = 0;
        size_t consumed = GfxSerializationHelper::unserialize_as_uint32_t(w, source, len);
        if (!consumed)
        {
            return 0;
        }
        count += consumed;
        int h = 0;
        consumed = GfxSerializationHelper::unserialize_as_uint32_t(h, source + count, len - count);
        if (!consumed)
        {
            return 0;
        }
        count += consumed;
        target.setW(w);
        target.setH(h);
        return count;
    }

    size_t GfxSerializationHelper::unserialize(GfxTaskProcessStatus& target, const char* source, const size_t len)
    {
        return GfxSerializationHelper::unserialize_as_uint32_t(target, source, len);
    }

    size_t GfxSerializationHelper::unserialize(GfxSerializeVersion& target, const char* source, const size_t len)
    {
        return GfxSerializationHelper::unserialize_as_uint32_t(target, source, len);
    }

    std::string CommandNewGfxSerializer::serialize(const CommandNewGfx& command)
    {
        std::string toret;
        GfxSerializationHelper::serialize(toret, command.Task.Path);
        GfxSerializationHelper::serialize(toret, command.Task.Sizes);
        return toret;
    }

    bool CommandNewGfxSerializer::unserialize(const std::string& data, CommandNewGfx& command)
    {
        size_t count = 0;
        size_t consumed = 0;
        auto source = data.data();
        auto len = data.size();

        // path
        std::string path;
        if (!(consumed = GfxSerializationHelper::unserialize(path, source, len)))
        {
            return false;
        }
        count += consumed;

        // sizes
        std::vector<GfxSize> sizes;
        if (!(consumed = GfxSerializationHelper::unserialize(sizes, source + count, len - count)))
        {
            return false;
        }
        count += consumed;

        // empty sizes considered an invalid task
        if (sizes.size() == 0)
        {
            return false;
        }

        command.Task.Path = std::move(path);
        command.Task.Sizes = std::move(sizes);

        return true;
    }

    std::string CommandNewGfxResponseSerializer::serialize(const CommandNewGfxResponse& command)
    {
        std::string toret;
        GfxSerializationHelper::serialize(toret, command.ErrorCode);
        GfxSerializationHelper::serialize(toret, command.ErrorText);
        GfxSerializationHelper::serialize(toret, command.Images);
        return toret;
    }

    bool CommandNewGfxResponseSerializer::unserialize(const std::string& data, CommandNewGfxResponse& command)
    {
        size_t count = 0;
        size_t consumed = 0;
        auto source = data.data();
        auto len = data.size();

        // ErrorCode
        if (!(consumed = GfxSerializationHelper::unserialize(command.ErrorCode, source, len)))
        {
            return false;
        }
        count += consumed;

        // ErrorText
        if (!(consumed = GfxSerializationHelper::unserialize(command.ErrorText, source + count, len - count)))
        {
            return false;
        }
        count += consumed;

        // images
        std::vector<std::string> images;
        if (!(consumed = GfxSerializationHelper::unserialize(command.Images, source + count, len - count)))
        {
            return false;
        }
        count += consumed;

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
        GfxSerializationHelper::serialize(toret, command.Text);
        return toret;
    }

    bool CommandHelloSerializer::unserialize(const std::string& data, CommandHello& command)
    {
        size_t consumed = 0;
        auto source = data.data();
        auto len = data.size();

        // Text
        std::string text;
        if (!(consumed = GfxSerializationHelper::unserialize(text, source, len)))
        {
            return false;
        }

        command.Text = std::move(text);

        return true;
    }

    std::string CommandHelloResponseSerializer::serialize(const CommandHelloResponse& command)
    {
        std::string toret;
        GfxSerializationHelper::serialize(toret, command.Text);
        return toret;
    }

    bool CommandHelloResponseSerializer::unserialize(const std::string& data, CommandHelloResponse& command)
    {
        size_t consumed = 0;
        auto source = data.data();
        auto len = data.size();

        // Text
        std::string text;
        if (!(consumed = GfxSerializationHelper::unserialize(text, source, len)))
        {
            return false;
        }

        command.Text = std::move(text);

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
        GfxSerializationHelper::serialize(toret, command.formats);
        GfxSerializationHelper::serialize(toret, command.videoformats);
        return toret;
    }

    bool CommandSupportFormatsResponseSerializer::unserialize(const std::string& data, CommandSupportFormatsResponse& command)
    {
        size_t count = 0;
        size_t consumed = 0;
        auto source = data.data();
        auto len = data.size();

        // formats
        if (!(consumed = GfxSerializationHelper::unserialize(command.formats, source, len)))
        {
            return false;
        }
        count += consumed;

        // videoformats
        if (!(consumed = GfxSerializationHelper::unserialize(command.videoformats, source + count, len - count)))
        {
            return false;
        }
        count += consumed;

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

    // proto version unit32_t
    GfxSerializationHelper::serialize(dataToReturn, GfxSerializeVersion::V_1);

    // command type
    GfxSerializationHelper::serialize(dataToReturn, command->type());

    // length and command
    std::string commandData;
    if (!CommandSerializer::serializeHelper(command, commandData))
    {
        return nullptr;
    }
    GfxSerializationHelper::serialize(dataToReturn, commandData);

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
