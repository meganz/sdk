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

#include <memory>
#include <openssl/ssl.h>
#include "mega/gfx/worker/tasks.h"

namespace mega {
namespace gfx {

enum class CommandType
{
    BEGIN               = 1,
    NEW_GFX             = 1,
    NEW_GFX_RESPONSE    = 2,
    ABORT               = 3,
    SHUTDOWN            = 4,
    SHUTDOWN_RESPONSE   = 5,
    HELLO               = 6,
    HELLO_RESPONSE      = 7,
    END                 = 8  // 1 more than the last valid one
};

class ICommand
{
public:
    virtual ~ICommand() = default;

    virtual CommandType type() const = 0;
};

struct CommandShutDown : public ICommand
{
    CommandType type() const override { return CommandType::SHUTDOWN; }
};

struct CommandShutDownResponse : public ICommand
{
    CommandType type() const override { return CommandType::SHUTDOWN_RESPONSE; }
};

struct CommandNewGfx : public ICommand
{
    GfxTask Task;
    CommandType type() const override { return CommandType::NEW_GFX; }
};

struct CommandNewGfxResponse : public ICommand
{
    uint32_t    ErrorCode;
    std::string ErrorText;
    std::vector<std::string> Images;
    CommandType type() const override { return CommandType::NEW_GFX_RESPONSE; }
};

struct CommandHello : public ICommand
{
    std::string Text;
    CommandType type() const override { return CommandType::HELLO; }
};

struct CommandHelloResponse : public ICommand
{
    std::string Text;
    CommandType type() const override { return CommandType::HELLO_RESPONSE; }
};

} //namespace gfx
} //namespace mega
