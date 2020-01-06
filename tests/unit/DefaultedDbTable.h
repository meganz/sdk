/**
 * (c) 2019 by Mega Limited, Wellsford, New Zealand
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

#include <mega/db.h>

#include "NotImplemented.h"

namespace mt {

class DefaultedDbTable: public mega::DbTable
{
public:
    using mega::DbTable::DbTable;
    void rewind() override
    {
        throw NotImplemented{__func__};
    }
    bool next(uint32_t*, std::string*) override
    {
        throw NotImplemented{__func__};
    }
    bool get(uint32_t, std::string*) override
    {
        throw NotImplemented{__func__};
    }
    bool put(uint32_t, char*, unsigned) override
    {
        throw NotImplemented{__func__};
    }
    bool del(uint32_t) override
    {
        throw NotImplemented{__func__};
    }
    void truncate() override
    {
        throw NotImplemented{__func__};
    }
    void begin() override
    {
        throw NotImplemented{__func__};
    }
    void commit() override
    {
        throw NotImplemented{__func__};
    }
    void abort() override
    {
        throw NotImplemented{__func__};
    }
    void remove() override
    {
        throw NotImplemented{__func__};
    }
};

} // mt
