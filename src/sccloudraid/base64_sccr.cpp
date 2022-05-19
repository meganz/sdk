#include "mega/sccloudraid/mega.h"

// modified base64 conversion (no trailing '=' and '-_' instead of '+/')
unsigned char Base64::to64(byte c)
{
    c &= 63;

    if (c < 26) return c+'A';

    if (c < 52) return c-26+'a';

    if (c < 62) return c-52+'0';

    return "-_"[c-62];
}

unsigned char Base64::from64(byte c)
{
    if (c >= 'A' && c <= 'Z') return c-'A';

    if (c >= 'a' && c <= 'z') return c-'a'+26;

    if (c >= '0' && c <= '9') return c-'0'+52;

    if (c == '-') return 62;

    if (c == '_') return 63;

    return 255;
}

int Base64::btoa(const byte* b, int blen, char* a)
{
    int p = 0;

    while (blen > 0)
    {
        a[p++] = to64(*b >> 2);
        a[p++] = to64((*b << 4) | (((blen > 1) ? b[1] : 0) >> 4));

        if (blen < 2) break;

        a[p++] = to64(b[1] << 2 | (((blen > 2) ? b[2] : 0) >> 6));

        if (blen < 3) break;

        a[p++] = to64(b[2]);

        blen -= 3;
        b += 3;
    }

    a[p] = 0;

    return p;
}

int Base64::atob(const char* a, byte* b, int blen)
{
    byte c[4];
    int i;
    int p = 0;

    c[3] = 0;

    for (;;)
    {
        if (p >= blen) return p;

        for (i = 0; i < 4; i++)
        {
            if ((c[i] = from64(*a++)) == 255)
            {
                while (i < 4) c[i++] = 0;
                if (blen-p > 3) blen = p+3;
            }
        }

        b[p++] = (c[0] << 2) | ((c[1] & 0x30) >> 4);

        if (p >= blen) return p;

        b[p++] = (c[1] << 4) | ((c[2] & 0x3c) >> 2);

        if (p >= blen) return p;

        b[p++] = (c[2] << 6) | c[3];
    }
}
