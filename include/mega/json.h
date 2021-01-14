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
    JSON()
      : pos(nullptr)
    {
    }

    explicit JSON(const string& data)
      : pos(data.c_str())
    {
    }

    const char* pos;

    bool isnumeric();

    void begin(const char*);

    m_off_t getint();
    double getfloat();
    const char* getvalue();

    fsfp_t getfp();
    uint64_t getuint64();

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

    // Only advance the pointer if it's an error (0, -1, -2, -3, ...)
    bool isNumericError(error& e);
};

class MEGA_API JSONWriter
{
public:
    JSONWriter();

    MEGA_DEFAULT_COPY_MOVE(JSONWriter);

    void cmd(const char*);
    void notself(MegaClient*);

    void arg(const char*, const string&, int = 1);
    void arg(const char*, const char*, int = 1);
    void arg(const char*, handle, int);
    void arg(const char*, const byte*, int);
    void arg(const char*, m_off_t);
    void addcomma();
    void appendraw(const char*);
    void appendraw(const char*, int);
    void beginarray();
    void beginarray(const char*);
    void endarray();
    void beginobject();
    void beginobject(const char*);
    void endobject();
    void element(int);
    void element(handle, int = sizeof(handle));
    void element(const byte*, int);
    void element(const char*);

    void openobject();
    void closeobject();

    const byte* getbytes() const;
    const string& getstring() const;

    size_t size() const;

private:
    static const int MAXDEPTH = 8;

    int elements();

    string mJson;
    std::array<signed char, MAXDEPTH> mLevels;
    signed char mLevel;
}; // JSONWriter

} // namespace

#endif
