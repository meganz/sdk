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
#include "gfxworker/tasks.h"
#include "gfxworker/commands.h"

namespace gfx {
namespace comms {

class IReader
{
public:
    virtual ~IReader() = default;

    /**
        * @brief always try best to read n BYTEs to the buffer unless error or EOF
        * @param out the output buffer. It has at least n BYTEs space.
        * @param n the n BYTEs to read
        * @return true: successfully read n BYTEs, false: there is either an error or EOF
        */
    bool read(void* out, size_t n, DWORD milliseconds) { return do_read(out, n, milliseconds); };

private:
    virtual bool do_read(void* out, size_t n, DWORD milliseconds) = 0;
};

class IWriter
{
public:
    virtual ~IWriter() = default;

    bool write(void* in, size_t n, DWORD milliseconds) { return do_write(in, n, milliseconds); };

private:
    virtual bool do_write(void* in, size_t n, DWORD milliseconds) = 0;
};

class IEndpoint : public IReader, public IWriter
{
public:
    virtual ~IEndpoint() = default;
};

using FinishCallback = std::function<void(bool)>;

class IGfxCommunicationsClient
{
public:
    virtual ~IGfxCommunicationsClient() = default;

    virtual std::unique_ptr<gfx::comms::IEndpoint> connect() = 0;
};

} //namespace comms
} //namespace gfx
