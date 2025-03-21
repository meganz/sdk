/**
 * @file attrmap.cpp
 * @brief Class for manipulating node attributes
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

#include "mega/attrmap.h"

#include <mega/json.h>

namespace mega {
// approximate raw storage size of serialized AttrMap, not taking JSON escaping
// or name length into account
unsigned AttrMap::storagesize(int perrecord) const
{
    assert(perrecord >= 0);
    unsigned t = 0;

    for (attr_map::const_iterator it = map.begin(); it != map.end(); it++)
    {
        t += static_cast<unsigned>(static_cast<unsigned>(perrecord) + it->second.size());
    }

    return t;
}

bool AttrMap::getBool(const char* name) const {
    nameid id = string2nameid(name);
    attr_map::const_iterator iter = map.find(id);
    bool value = iter != map.end() && iter->second == "1";
    return value;
}

std::optional<std::string> AttrMap::getString(std::string_view name) const
{
    // Try and locate the specified attribute.
    auto i = map.find(string2nameid(name.data()));

    // Couldn't find the attribute.
    if (i == map.end())
        return std::nullopt;

    // Return attribute value to caller.
    return std::optional<std::string>(std::in_place, i->second);
}

int AttrMap::nameid2string(nameid id, char* buf)
{
    char* ptr = buf;

    for (int i = 64; (i -= 8) >= 0;)
    {
        *ptr = static_cast<char>((id >> i) & 0xff);
        if (*ptr)
        {
            ptr++;
        }
    }

    return static_cast<int>(ptr - buf);
}

string AttrMap::nameid2string(nameid id)
{
    string s;
    s.resize(10);
    s.resize(static_cast<size_t>(nameid2string(id, const_cast<char*>(s.data()))));
    return s;
}

nameid AttrMap::string2nameid(const char* n)
{
    if (!n)
    {
        return 0;
    }

    return makeNameid(n);
}

// generate binary serialize of attr_map name-value pairs
void AttrMap::serialize(string* d) const
{
    char buf[8];
    unsigned char l;
    unsigned short ll;

    for (attr_map::const_iterator it = map.begin(); it != map.end(); it++)
    {
        l = (unsigned char)nameid2string(it->first, buf);
        if (l)
        {
            d->append((char*)&l, sizeof l);
            d->append(buf, l);
            ll = (unsigned short)it->second.size();
            d->append((char*)&ll, sizeof ll);
            d->append(it->second.data(), ll);
        }
    }

    d->append("", 1);
}

// read binary serialize, return final offset
const char* AttrMap::unserialize(const char* ptr , const char *end)
{
    unsigned char l;
    unsigned short ll;
    nameid id;

    while ((ptr < end) && (l = static_cast<unsigned char>(*ptr++)) != 0)
    {
        id = 0;

        if (ptr + l + sizeof ll > end)
        {
            return NULL;
        }

        while (l--)
        {
            id = (id << 8) + (unsigned char)*ptr++;
        }

        ll = static_cast<unsigned short>(MemAccess::get<short>(ptr));
        ptr += sizeof ll;

        if (ptr + ll > end)
        {
            return NULL;
        }

        map[id].assign(ptr, ll);
        ptr += ll;
    }

    return ptr;
}

bool AttrMap::hasUpdate(nameid attrId, const attr_map& updates) const
{
    auto curIt = map.find(attrId);
    auto updIt = updates.find(attrId);
    return updIt != updates.end() && // is present in updates AND
           ((curIt == map.end() && !updIt->second.empty()) || // is not present here and has non-empty value in updates OR
            (curIt != map.end() && curIt->second != updIt->second)); // is present here but has different value in updates
}

bool AttrMap::hasDifferentValue(nameid attrId, const attr_map& otherAttrs) const
{
    auto curIt = map.find(attrId);
    auto otherIt = otherAttrs.find(attrId);
    return (curIt != map.end() && otherIt == otherAttrs.end()) || // present only here OR
           (curIt == map.end() && otherIt != otherAttrs.end()) || // present only in other attrs OR
           (curIt != map.end() && otherIt != otherAttrs.end() && curIt->second != otherIt->second); // have different values
}

void AttrMap::applyUpdates(const attr_map& updates)
{
    for (auto& u : updates)
    {
        if (u.second.empty()) map.erase(u.first);
        else map[u.first] = u.second;
    }
}

// generate JSON object containing attr_map
void AttrMap::getjson(string* s) const
{
    nameid id;
    char buf[8];
    const char* ptr;
    const char* pptr;

    // reserve estimated size of final string
    s->erase();
    s->reserve(storagesize(20));

    for (attr_map::const_iterator it = map.begin(); it != map.end(); it++)
    {
        s->append(s->size() ? ",\"" : "\"");

        id = it->first;
        if (id)
        {
            // no JSON escaping here, as no escape characters are allowed in
            // attribute names
            s->append(buf, static_cast<size_t>(nameid2string(id, buf)));
            s->append("\":\"");

            // JSON-escape value
            pptr = it->second.c_str();
            ptr = it->second.c_str();

            for (int i = static_cast<int>(it->second.size()); i-- >= 0; ptr++)
            {
                if ((i < 0) || ((*(const signed char*)ptr >= 0) && (*ptr < ' ')) || (*ptr == '"') || (*ptr == '\\'))
                {
                    if (ptr > pptr)
                    {
                        s->append(pptr, static_cast<size_t>(ptr - pptr));
                    }

                    if (i >= 0)
                    {
                        s->append("\\");

                        switch (*ptr)
                        {
                            case '"':
                                s->append("\"");
                                break;

                            case '\\':
                                s->append("\\");
                                break;

                            case '\n':
                                s->append("n");
                                break;

                            case '\r':
                                s->append("r");
                                break;

                            case '\b':
                                s->append("b");
                                break;

                            case '\f':
                                s->append("f");
                                break;

                            case '\t':
                                s->append("t");
                                break;

                            default:
                                s->append("u00");
                                snprintf(buf, sizeof(buf), "%02x", (unsigned char)*ptr);
                                s->append(buf);
                        }

                        pptr = ptr + 1;
                    }
                }
            }

            s->append("\"");
        }
    }
}

void AttrMap::fromjson(const char* buf)
{
    if (!buf) return;

    JSON json;
    json.begin(buf);
    nameid name;
    string* t;

    while ((name = json.getnameid()) != EOO && json.storeobject((t = &map[name])))
    {
        JSON::unescape(t);
    }
}

} // namespace
