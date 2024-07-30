/**
 * (c) 2013 by Mega Limited, Auckland, New Zealand
 *
 * This file is part of the MEGA SDK - Client Access Engine.
 *
 * Applications using the MEGA API must present a valid application key
 * and comply with the the rules set forth in the Terms of Service.
 *
 * The MEGA SDK is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * @copyright Simplified (2-clause) BSD License.
 *
 * You should have received a copy of the license along with this
 * program.
 */

#pragma once

#include "mega/gfx/worker/tasks.h"

#include <memory>

namespace mega {
namespace gfx {

enum class CommandType
{
    BEGIN                       = 1,
    NEW_GFX                     = 1,
    NEW_GFX_RESPONSE            = 2,
    ABORT                       = 3,
    SHUTDOWN                    = 4,
    SHUTDOWN_RESPONSE           = 5,
    HELLO                       = 6,
    HELLO_RESPONSE              = 7,
    SUPPORT_FORMATS             = 8,
    SUPPORT_FORMATS_RESPONSE    = 9,
    END                         = 10  // 1 more than the last valid one
};

class ICommand
{
public:
    virtual ~ICommand() = default;

    virtual CommandType type() const = 0;

    virtual std::string typeStr() const = 0;

    virtual std::string serialize() const = 0;

    virtual bool unserialize(const std::string& data) = 0;

    static std::unique_ptr<ICommand> factory(CommandType type);
};

struct CommandShutDown : public ICommand
{
    CommandType type() const override { return CommandType::SHUTDOWN; }

    std::string typeStr() const override { return "SHUTDOWN"; };

    std::string serialize() const override;

    bool unserialize(const std::string& data) override;
};

struct CommandShutDownResponse : public ICommand
{
    CommandType type() const override { return CommandType::SHUTDOWN_RESPONSE; }

    std::string typeStr() const override { return "SHUTDOWN_RESPONSE"; };

    std::string serialize() const override;

    bool unserialize(const std::string& data) override;
};

struct CommandNewGfx : public ICommand
{
    GfxTask Task;

    CommandType type() const override { return CommandType::NEW_GFX; }

    std::string typeStr() const override { return "NEW_GFX"; };

    std::string serialize() const override;

    bool unserialize(const std::string& data) override;
};

struct CommandNewGfxResponse : public ICommand
{
    uint32_t    ErrorCode;
    std::string ErrorText;
    std::vector<std::string> Images;

    CommandType type() const override { return CommandType::NEW_GFX_RESPONSE; }

    std::string typeStr() const override { return "NEW_GFX_RESPONSE"; };

    std::string serialize() const override;

    bool unserialize(const std::string& data) override;
};

struct CommandHello : public ICommand
{
    std::string Text;

    CommandType type() const override { return CommandType::HELLO; }

    std::string typeStr() const override { return "HELLO"; };

    std::string serialize() const override;

    bool unserialize(const std::string& data) override;
};

struct CommandHelloResponse : public ICommand
{
    std::string Text;

    CommandType type() const override { return CommandType::HELLO_RESPONSE; }

    std::string typeStr() const override { return "HELLO_RESPONSE"; };

    std::string serialize() const override;

    bool unserialize(const std::string& data) override;
};

struct CommandSupportFormats : public ICommand
{
    CommandType type() const override { return CommandType::SUPPORT_FORMATS; }

    std::string typeStr() const override { return "SUPPORT_FORMATS"; };

    std::string serialize() const override;

    bool unserialize(const std::string& data) override;
};

struct CommandSupportFormatsResponse : public ICommand
{
    std::string formats;
    std::string videoformats;

    CommandType type() const override { return CommandType::SUPPORT_FORMATS_RESPONSE; }

    std::string typeStr() const override { return "SUPPORT_FORMATS_RESPONSE"; };

    std::string serialize() const override;

    bool unserialize(const std::string& data) override;
};

} //namespace gfx
} //namespace mega
