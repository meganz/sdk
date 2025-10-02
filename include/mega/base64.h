/**
 * @file mega/base64.h
 * @brief modified base64 encoding/decoding
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

#ifndef MEGA_BASE64_H
#define MEGA_BASE64_H 1

#include "types.h"

namespace mega {
// modified base64 encoding/decoding (unpadded, -_ instead of +/)
class MEGA_API Base64
{
    static byte to64(byte);
    static byte from64(byte);

public:
    static int btoa(const string&, string&);
    static string btoa(const string &in);   // use Base64Str<size> instead when `size` is known at compile time (more efficient)
    static int btoa(const byte*, int, char*);   // deprecated
    static int atob(const string&, string&);
    static string atob(const string&);
    static int atob(const char*, byte*, int);   // deprecated

    static void itoa(int64_t, string *);
    static int64_t atoi(string *);

    // modify a base64 string to standard conversion:
    // 1. Trailing(s) '=' if needed to have a "correct" length (ex: from 32 to 44)
    // 2. '+/' instead of '-_'
    static void toStandard(string& b64str);
};

template <unsigned BINARYSIZE>
struct Base64Str
{
    // provides a way to build the C string on the stack efficiently, using minimal space
    enum { STRLEN = (BINARYSIZE * 4 + 2) / 3};
    char chars[STRLEN + 1]; // sizeof(chars) can be larger due to alignment etc

    Base64Str(const void* b):
        Base64Str(b, BINARYSIZE)
    {}

    Base64Str(const void* b, int size)
    {
        [[maybe_unused]] int n = Base64::btoa(reinterpret_cast<const byte*>(b), size, chars);
        assert(static_cast<size_t>(n + 1) <= sizeof(chars));
    }

    Base64Str(const handle& h):
        Base64Str(&h)
    {}
    operator const char* () const
    {
        return chars;
    }
    const byte* bytes() const
    {
        return reinterpret_cast<const byte*>(chars);
    }
    unsigned int size() const
    {
        return STRLEN;
    }
};

// lowercase base32 encoding
class MEGA_API Base32
{
    static byte to32(byte);
    static byte from32(byte);

public:
    static int btoa(const byte*, int, char*);
    static int atob(const char*, byte*, int);
};

class MEGA_API URLCodec
{
    static bool ishexdigit(char c);

public:
    static bool issafe(char c);
    static void escape(string* plain, string* escaped);
    static void unescape(string* escaped, string* plain);
};

} // namespace

#endif
