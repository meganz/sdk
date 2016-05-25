/**
 * @file db.cpp
 * @brief Database access interface
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

#include "mega/db.h"
#include "mega/utils.h"
#include "mega/logging.h"

namespace mega {
DbTable::DbTable()
{
    nextid = 0;
}

// add or update record from string
bool DbTable::put(uint32_t index, string* data)
{
    return put(index, (char*)data->data(), data->size());
}

// add or update record with padding and encryption
bool DbTable::put(uint32_t type, Cachable* record, SymmCipher* key)
{
    string data;

    if (!record->serialize(&data))
    {
        //Don't return false if there are errors in the serialization
        //to let the SDK continue and save the rest of records
        LOG_warn << "Serialization failed: " << type;
        return true;
    }

    PaddedCBC::encrypt(&data, key);

    if (!record->dbid)
    {
        record->dbid = (nextid += IDSPACING) | type;
    }

    return put(record->dbid, &data);
}

// get next record, decrypt and unpad
bool DbTable::next(uint32_t* type, string* data, SymmCipher* key)
{
    if (next(type, data))
    {
        if (!*type)
        {
            return true;
        }

        if (*type > nextid)
        {
            nextid = *type & - IDSPACING;
        }

        return PaddedCBC::decrypt(data, key);
    }

    return false;
}

DbAccess::DbAccess()
{
    currentDbVersion = LEGACY_DB_VERSION;
}

} // namespace
