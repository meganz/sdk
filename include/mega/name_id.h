#ifndef MEGA_NAME_ID_H
#define MEGA_NAME_ID_H

#include <cstdint>

namespace mega
{

// numeric representation of string (up to 8 chars)
using nameid = uint64_t;

// convert 1...8 character ID to int64 integer (endian agnostic)
#define MAKENAMEID1(a) (nameid)(a)
#define MAKENAMEID2(a, b) (nameid)(((a) << 8) + (b))
#define MAKENAMEID3(a, b, c) (nameid)(((a) << 16) + ((b) << 8) + (c))
#define MAKENAMEID4(a, b, c, d) (nameid)(((a) << 24) + ((b) << 16) + ((c) << 8) + (d))
#define MAKENAMEID5(a, b, c, d, e) \
(nameid)((((nameid)a) << 32) + ((b) << 24) + ((c) << 16) + ((d) << 8) + (e))
#define MAKENAMEID6(a, b, c, d, e, f) \
(nameid)((((nameid)a) << 40) + (((nameid)b) << 32) + ((c) << 24) + ((d) << 16) + ((e) << 8) + (f))
#define MAKENAMEID7(a, b, c, d, e, f, g) \
(nameid)((((nameid)a) << 48) + (((nameid)b) << 40) + (((nameid)c) << 32) + ((d) << 24) + \
         ((e) << 16) + ((f) << 8) + (g))
#define MAKENAMEID8(a, b, c, d, e, f, g, h) \
(nameid)((((nameid)a) << 56) + (((nameid)b) << 48) + (((nameid)c) << 40) + (((nameid)d) << 32) + \
         ((e) << 24) + ((f) << 16) + ((g) << 8) + (h))

namespace name_id
{
static constexpr nameid ipc = MAKENAMEID3('i', 'p', 'c');
static constexpr nameid c = 'c';
static constexpr nameid upci = MAKENAMEID4('u', 'p', 'c', 'i');
static constexpr nameid upco = MAKENAMEID4('u', 'p', 'c', 'o');
static constexpr nameid share = MAKENAMEID5('s', 'h', 'a', 'r', 'e');
static constexpr nameid dshare = MAKENAMEID6('d', 's', 'h', 'a', 'r', 'e');
static constexpr nameid put = MAKENAMEID3('p', 'u', 't');
} // namespace name_id

} // namespace mega

#endif // MEGA_NAME_ID_H
