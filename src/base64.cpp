/**
 * @file base64.cpp
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

#include "mega/base64.h"
#include "mega/utils.h"

namespace mega {
// modified base64 conversion (no trailing '=' and '-_' instead of '+/')
unsigned char Base64::to64(byte c)
{
    c &= 63;

    if (c < 26)
    {
        return static_cast<unsigned char>(c + 'A');
    }

    if (c < 52)
    {
        return static_cast<unsigned char>(c - 26 + 'a');
    }

    if (c < 62)
    {
        return static_cast<unsigned char>(c - 52 + '0');
    }

    if (c == 62)
    {
        return '-';
    }

    return '_';
}

unsigned char Base64::from64(byte c)
{
    if ((c >= 'A') && (c <= 'Z'))
    {
        return static_cast<unsigned char>(c - 'A');
    }

    if ((c >= 'a') && (c <= 'z'))
    {
        return static_cast<unsigned char>(c - 'a' + 26);
    }

    if ((c >= '0') && (c <= '9'))
    {
        return static_cast<unsigned char>(c - '0' + 52);
    }

    if (c == '-' || c == '+')
    {
        return static_cast<unsigned char>(62);
    }

    if (c == '_' || c == '/')
    {
        return static_cast<unsigned char>(63);
    }

    return static_cast<unsigned char>(255);
}


int Base64::atob(const string &in, string &out)
{
    out.resize(in.size() * 3 / 4 + 3);
    out.resize(static_cast<size_t>(Base64::atob(in.data(), (byte*)out.data(), (int)out.size())));

    return (int)out.size();
}

std::string Base64::atob(const std::string &in)
{
    string out;
    out.resize(in.size() * 3 / 4 + 3);
    out.resize(static_cast<size_t>(Base64::atob(in.data(), (byte*)out.data(), (int)out.size())));

    return out;
}

int Base64::atob(const char* a, byte* b, int blen)
{
    byte c[4]={};
    int i;
    int p = 0;

    for (;;)
    {
        for (i = 0; i < 4; i++)
        {
            if ((c[i] = from64(static_cast<byte>(*a++))) == 255)
            {
                break;
            }
        }

        if ((p >= blen) || !i)
        {
            return p;
        }

        b[p++] = static_cast<byte>((c[0] << 2) | ((c[1] & 0x30) >> 4));

        if ((p >= blen) || (i < 3))
        {
            return p;
        }

        b[p++] = static_cast<byte>((c[1] << 4) | ((c[2] & 0x3c) >> 2));

        if ((p >= blen) || (i < 4))
        {
            return p;
        }

        b[p++] = static_cast<byte>((c[2] << 6) | c[3]);
    }
}

void Base64::itoa(int64_t val, string *result)
{
    byte c;
    int64_t rest;
    if (!result || val < 0)
    {
        return;
    }

    if (!val)
    {
        *result = "A";
        return;
    }

    result->clear();
    while (val)
    {
        rest = val % 64;
        val /= 64;
        c = to64(byte(rest));
        result->insert(result->begin(), (char) c);
    }
}

int64_t Base64::atoi(string *val)
{
    if (!val)
    {
        return -1;
    }

    size_t len = val->size();
    if (len == 0)
    {
        return -1;
    }

    size_t pos = 0;
    int64_t res = 0;
    int valid = 0;
    while (pos < len)
    {
        byte b = from64(static_cast<byte>(val->at(pos)));
        if (b == 255)
        {
            pos++;
            continue;
        }

        valid++;
        res *= 64;
        res += b;
        pos++;
    }

    if (!valid || res < 0)
    {
        return -1;
    }

    return res;
}

int Base64::btoa(const string &in, string &out)
{
    out.resize(in.size() * 4 / 3 + 4);
    out.resize(static_cast<size_t>(
        Base64::btoa((const byte*)in.data(), (int)in.size(), (char*)out.data())));

    return (int)out.size();
}

std::string Base64::btoa(const string &in)
{
    string out;
    out.resize(in.size() * 4 / 3 + 4);
    out.resize(static_cast<size_t>(
        Base64::btoa((const byte*)in.data(), (int)in.size(), (char*)out.data())));

    return out;
}

int Base64::btoa(const byte* b, int blen, char* a)
{
    int p = 0;

    for (;;)
    {
        if (blen <= 0)
        {
            break;
        }

        a[p++] = static_cast<char>(to64(static_cast<byte>(*b >> 2)));
        a[p++] =
            static_cast<char>(to64(static_cast<byte>((*b << 4) | (((blen > 1) ? b[1] : 0) >> 4))));

        if (blen < 2)
        {
            break;
        }

        a[p++] =
            static_cast<char>(to64(static_cast<byte>(b[1] << 2 | (((blen > 2) ? b[2] : 0) >> 6))));

        if (blen < 3)
        {
            break;
        }

        a[p++] = static_cast<char>(to64(b[2]));

        blen -= 3;
        b += 3;
    }

    a[p] = 0;

    return p;
}

void Base64::toStandard(string& b64str)
{
    std::replace(b64str.begin(), b64str.end(), '-', '+');
    std::replace(b64str.begin(), b64str.end(), '_', '/');

    // Calculate how many padding '=' chars are needed
    size_t padChars = 4 - (b64str.size() % 4);
    if (padChars < 4)
    {
        b64str.append(padChars, '=');
    }

    assert(b64str.size() % 4 == 0);
}

byte Base32::to32(byte c)
{
    c &= 31;

    if (c < 26)
    {
        return static_cast<byte>(c + 'a');
    }

    return static_cast<byte>(c - 26 + '2');
}

byte Base32::from32(byte c)
{
    if ((c >= 'a') && (c <= 'z'))
    {
        return static_cast<byte>(c - 'a');
    }

    if ((c >= '2') && (c <= '9'))
    {
        return static_cast<byte>(c - '2' + 26);
    }

    return static_cast<byte>(255);
}

int Base32::btoa(const byte *b, int blen, char *a)
{
    int p = 0;

    for (;;)
    {
        if (blen <= 0)
        {
            break;
        }

        a[p++] = static_cast<char>(to32(static_cast<byte>(*b >> 3)));
        a[p++] =
            static_cast<char>(to32(static_cast<byte>((*b << 2) | (((blen > 1) ? b[1] : 0) >> 6))));

        if (blen < 2)
        {
            break;
        }

        a[p++] = static_cast<char>(to32(static_cast<byte>(b[1] >> 1)));
        a[p++] =
            static_cast<char>(to32(static_cast<byte>(b[1] << 4 | (((blen > 2) ? b[2] : 0) >> 4))));

        if (blen < 3)
        {
            break;
        }

        a[p++] = static_cast<char>(
            to32(static_cast<byte>((b[2] << 1) | (((blen > 3) ? b[3] : 0) >> 7))));

        if (blen < 4)
        {
            break;
        }

        a[p++] = static_cast<char>(to32(static_cast<byte>(b[3] >> 2)));
        a[p++] =
            static_cast<char>(to32(static_cast<byte>(b[3] << 3 | (((blen > 4) ? b[4] : 0) >> 5))));

        if (blen < 5)
        {
            break;
        }

        a[p++] = static_cast<char>(to32(b[4]));

        blen -= 5;
        b += 5;
    }

    a[p] = 0;

    return p;
}

int Base32::atob(const char *a, byte *b, int blen)
{
    byte c[8]={};
    int i;
    int p = 0;

    for (;;)
    {
        for (i = 0; i < 8; i++)
        {
            if ((c[i] = from32(static_cast<byte>(*a++))) == 255)
            {
                break;
            }
        }

        if ((p >= blen) || !i)
        {
            return p;
        }

        b[p++] = static_cast<byte>((c[0] << 3) | ((c[1] & 0x1C) >> 2));

        if ((p >= blen) || (i < 4))
        {
            return p;
        }

        b[p++] = static_cast<byte>((c[1] << 6) | (c[2] << 1) | ((c[3] & 0x10) >> 4));

        if ((p >= blen) || (i < 5))
        {
            return p;
        }

        b[p++] = static_cast<byte>((c[3] << 4) | ((c[4] & 0x1E) >> 1));

        if ((p >= blen) || (i < 7))
        {
            return p;
        }

        b[p++] = static_cast<byte>((c[4] << 7) | (c[5] << 2) | ((c[6] & 0x18) >> 3));

        if ((p >= blen) || (i < 8))
        {
            return p;
        }

        b[p++] = static_cast<byte>((c[6] << 5) | c[7]);
    }
}

bool URLCodec::issafe(char c)
{
    if (ishexdigit(c)
            || (c >= 'a' && c <= 'z')
            || (c >= 'A' && c <= 'Z')
            || c == '-' || c == '.'
            || c == '_' || c == '~')
    {
        return true;
    }
    return false;
}

bool URLCodec::ishexdigit(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

void URLCodec::escape(string *plain, string *escaped)
{
    if (!escaped || !plain)
    {
        return;
    }

    escaped->clear();
    for (const char& c: *plain)
    {
        if (issafe(c))
        {
            escaped->push_back(c);
        }
        else
        {
            char buf[4];
            snprintf(buf, sizeof(buf), "%%%02x", (unsigned char)c); // cast it to avoid being sign-extended
            escaped->append(buf);
        }
    }
}

void URLCodec::unescape(string *escaped, string *plain)
{
    if (!(escaped && plain))
    {
        return;
    }

    plain->clear();
    plain->reserve(escaped->size());

    const char* m = escaped->c_str();
    const char* n = m + escaped->size();

    while (m < n)
    {
        if (*m == '%' && n - m > 2)
        {
            if (ishexdigit(m[1]) && ishexdigit(m[2]))
            {
                auto c = hexval(m[1]) << 4u | hexval(m[2]);

                plain->push_back(static_cast<char>(c));

                m += 3;
                continue;
            }
        }

        plain->push_back(*m++);
    }
}

} // namespace
