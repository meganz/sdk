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
#include "mega/types.h"

#include <chrono>

namespace mega
{
namespace gfx
{

class IReader
{
public:
    virtual ~IReader() = default;

    /**
     * @brief Attempts to read exactly 'n' bytes into the provided buffer unless an error, EOF, or
     * timeout occurs.
     * @param out The output buffer, which must have space for at least 'n' bytes.
     * @param n The number of bytes to read.
     * @param timeout The timeout duration in milliseconds.
     *                    A positive value waits up to the specified time,
     *                    0 returns immediately without waiting, and a negative value waits
     * indefinitely.
     * @return true if 'n' bytes are successfully read, false if an error, EOF, or timeout occurs.
     */
    bool read(void* out, size_t n, std::chrono::milliseconds timeout)
    {
        return doRead(out, n, timeout);
    };

private:
    virtual bool doRead(void* out, size_t n, std::chrono::milliseconds timeout) = 0;
};

class IWriter
{
public:
    virtual ~IWriter() = default;

    bool write(const void* in, size_t n, std::chrono::milliseconds timeout)
    {
        return doWrite(in, n, timeout);
    };

private:
    virtual bool doWrite(const void* in, size_t n, std::chrono::milliseconds timeout) = 0;
};

//
// It represents a communication endpoint
//
class IEndpoint : public IReader, public IWriter
{
public:
    virtual ~IEndpoint() = default;
};

enum class CommError: uint8_t
{
    OK              = 0,
    ERR             = 1, // an generic error
    NOT_EXIST       = 2,
    TIMEOUT         = 3,
};

} // namespace gfx
} // namespace mega
