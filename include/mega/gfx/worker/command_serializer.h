#pragma once

#include "mega/gfx/worker/commands.h"
#include "mega/gfx/worker/comms.h"

#include <memory>
#include <string>

namespace mega
{
namespace gfx
{

class IReader;
class IWriter;

class ProtocolWriter
{
public:
    ProtocolWriter(IWriter* writer) : mWriter(writer) {}

    bool writeCommand(ICommand* command, TimeoutMs timeout) const;
private:
    IWriter* mWriter;
};

class ProtocolReader
{
public:
    ProtocolReader(IReader* reader) : mReader(reader) {}

    std::unique_ptr<ICommand> readCommand(TimeoutMs timeout) const;

private:

    IReader* mReader;
};

class CommandSerializer
{
public:
    static std::unique_ptr<std::string> serialize(ICommand* command);

    static std::unique_ptr<ICommand> unserialize(IReader& reader, TimeoutMs timeout);

private:

    static bool unserializeUInt32(IReader& reader, uint32_t& data, TimeoutMs timeout);

    static bool unserializeString(IReader& reader, std::string& data, TimeoutMs timeout);

    static std::unique_ptr<ICommand> unserializeCommand(CommandType type, const std::string& data);
};

}
}

