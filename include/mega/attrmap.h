/**
 * @file mega/attrmap.h
 * @brief Class for manipulating file attributes
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

#ifndef MEGA_ATTRMAP_H
#define MEGA_ATTRMAP_H 1

#include "name_id.h"
#include "utils.h"

#include <optional>
#include <string_view>

namespace mega {

// maps attribute names to attribute values
struct attr_map : map<nameid, string>
{
    attr_map() {}

    attr_map(nameid key, string value)
    {
        (*this)[key] = std::move(value);
    }

    attr_map(map<nameid, string>&& m)
    {
        m.swap(*this);
    }

    bool contains(nameid k) const
    {
        return this->find(k) != this->end();
    }
};

struct MEGA_API AttrMap
{
    attr_map map;

    bool getBool(const char* name) const;

    std::optional<std::string> getString(std::string_view name) const;

    // Same as getString but without copying the string
    const char* read(const char* const name) const;

    // compute rough storage size
    unsigned storagesize(int) const;

    // convert nameid to string
    static int nameid2string(nameid, char*);
    static string nameid2string(nameid);

    // convert string to nameid
    static nameid string2nameid(const char *);

    // export as JSON string
    void getjson(string*) const;

    // import from JSON string
    void fromjson(const char* buf);

    // export as raw binary serialize
    void serialize(string*) const;

    // import raw binary serialize
    const char* unserialize(const char*, const char*);

    // overwrite entries in map (or remove them if the value is empty)
    void applyUpdates(const attr_map& updates);

    // determine if the value of attrId will receive an update if applyUpdates() will be called for updates
    // (an attribute will be updated only if present among received updates;
    // even for removal, it should be present with an empty value)
    bool hasUpdate(nameid attrId, const attr_map& updates) const;

    // determine if attrId differs between the 2 maps
    bool hasDifferentValue(nameid attrId, const attr_map& other) const;
};
} // namespace

#endif
