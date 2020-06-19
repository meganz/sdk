/**
 * @file mega/json.h
 * @brief Linear non-strict JSON scanner
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

#ifndef MEGA_JSON_H
#define MEGA_JSON_H 1

#include "types.h"

namespace mega {

// linear non-strict JSON scanner
struct MEGA_API JSON
{
    const char* pos = nullptr;

    bool isnumeric();

    void begin(const char*);

    m_off_t getint();
    double getfloat();
    const char* getvalue();

    nameid getnameid();
    nameid getnameid(const char*) const;
    string getname();
    string getnameWithoutAdvance() const;

    bool is(const char*);

    int storebinary(byte*, int);
    bool storebinary(string*);

    // MegaClient::NODEHANDLE
    bool ishandle(int = 6);
    handle gethandle(int = 6);

    bool enterarray();
    bool leavearray();

    bool enterobject();
    bool leaveobject();

    bool storeobject(string* = NULL);

    static void unescape(string*);

    /**
     * @brief Extract a string value for a name in a JSON string
     * @param json JSON string to check
     * @param name Attribute name.
     * @param value Atribute value.
     * @return False if the JSON string doesn't contains the string attribute
     */
    static bool extractstringvalue(const string & json, const string & name, string* value);

    // convenience functions, which avoid warnings and casts
    inline int      getint32()  { return int(getint()); }
    inline unsigned getuint32() { return unsigned(getint()); }
    inline bool     getbool()   { return bool(getint()); }
};

} // namespace

#endif
