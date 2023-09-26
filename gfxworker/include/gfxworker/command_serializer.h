#pragma once

#include <memory>
#include <string>
#include "gfxworker/commands.h"

namespace gfx
{
namespace comms
{

class IReader;
class IWriter;

class ProtocolWriter
{
public:
    ProtocolWriter(IWriter* writer) : mWriter(writer) {}

    bool writeCommand(ICommand* command, DWORD milliseconds) const;
private:
    IWriter* mWriter;
};

class ProtocolReader
{
public:
    ProtocolReader(IReader* reader) : mReader(reader) {}

    std::unique_ptr<ICommand> readCommand(DWORD milliseconds) const;

private:

    IReader* mReader;
};

struct CommandSerializer
{
    static std::unique_ptr<std::string> serialize(ICommand* command);

    static std::unique_ptr<ICommand> unserialize(IReader& reader, DWORD milliseconds);

private:

    static bool unserializeHelper(IReader& reader, uint32_t& data, DWORD milliseconds);

    static bool unserializeHelper(IReader& reader, std::string& data, DWORD milliseconds);

    static std::unique_ptr<ICommand> unserializeHelper(CommandType type, const std::string& data);

    static bool serializeHelper(ICommand* command, std::string& data);
};

}
}

