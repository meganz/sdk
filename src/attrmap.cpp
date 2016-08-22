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

namespace mega {
// approximate raw storage size of serialized AttrMap, not taking JSON escaping
// or name length into account
unsigned AttrMap::storagesize(int perrecord) const
{
    unsigned t = 0;

    for (attr_map::const_iterator it = map.begin(); it != map.end(); it++)
    {
        t += perrecord + it->second.size();
    }

    return t;
}

int AttrMap::nameid2string(nameid id, char* buf)
{
    char* ptr = buf;

    for (int i = 64; (i -= 8) >= 0;)
    {
        if ((*ptr = ((id >> i) & 0xff)))
        {
            ptr++;
        }
    }

    return ptr - buf;
}

nameid AttrMap::string2nameid(const char *a)
{
    int len;
    if (!a)
    {
        return 0;
    }

    len = strlen(a);
    if (len > 8)
    {
        return 0;
    }

    switch (len)
    {
        case 1:
            return *a;
        case 2:
            return MAKENAMEID2(a[0], a[1]);
        case 3:
            return MAKENAMEID3(a[0], a[1], a[2]);
        case 4:
            return MAKENAMEID4(a[0], a[1], a[2], a[3]);
        case 5:
            return MAKENAMEID5(a[0], a[1], a[2], a[3], a[4]);
        case 6:
            return MAKENAMEID6(a[0], a[1], a[2], a[3], a[4], a[5]);
        case 7:
            return MAKENAMEID7(a[0], a[1], a[2], a[3], a[4], a[5], a[6]);
        case 8:
            return MAKENAMEID8(a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7]);
        default:
            break;
    }
    return 0;
}

// generate binary serialize of attr_map name-value pairs
void AttrMap::serialize(string* d) const
{
    char buf[8];
    unsigned char l;
    unsigned short ll;

    for (attr_map::const_iterator it = map.begin(); it != map.end(); it++)
    {
        if ((l = (unsigned char)nameid2string(it->first, buf)))
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

    while ((ptr < end) && (l = *ptr++))
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

        ll = MemAccess::get<short>(ptr);
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

        if ((id = it->first))
        {
            // no JSON escaping here, as no escape characters are allowed in
            // attribute names
            s->append(buf, nameid2string(id, buf));
            s->append("\":\"");

            // JSON-escape value
            pptr = it->second.c_str();
            ptr = it->second.c_str();

            for (int i = it->second.size(); i-- >= 0; ptr++)
            {
                if ((i < 0) || ((*ptr >= 0) && (*ptr < ' ')) || (*ptr == '"') || (*ptr == '\\'))
                {
                    if (ptr > pptr)
                    {
                        s->append(pptr, ptr - pptr);
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
                                sprintf(buf, "%02x", (unsigned char)*ptr);
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
} // namespace
