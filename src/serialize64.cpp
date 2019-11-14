/**
 * @file serialize64.cpp
 * @brief 64-bit int serialization/unserialization
 *
 * (c) 2013-2014 by Mega Limited, Auckland, New Zealand
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

#include "mega/serialize64.h"

namespace mega {
int Serialize64::serialize(byte* bytes, uint64_t value)
{
    byte byteCount = 0;

    while (value)
    {
        bytes[++byteCount] = (byte)value;
        value >>= 8;
    }
    bytes[0] = byteCount;

    return byteCount + 1;
}

int Serialize64::unserialize(byte* bytes, int blen, uint64_t* value)
{
    byte byteCount = bytes[0];

    if ((byteCount > sizeof(*value)) || (byteCount >= blen))
    {
        return -1;
    }

    *value = 0;

    while (byteCount)
    {
        *value = (*value << 8) + bytes[(int)byteCount--];
    }

    return bytes[0] + 1;
}
} // namespace
