#ifndef MEGA_NAME_ID_H
#define MEGA_NAME_ID_H

#include <cassert>
#include <cstdint>
#include <string_view>

namespace mega
{

// numeric representation of string (up to 8 chars)
using nameid = uint64_t;

constexpr nameid makeNameid(std::string_view name)
{
    // name must have at most size 8
    assert(name.size() <= 8);

    nameid id = 0;
    for (size_t n = 0; n < name.size(); ++n)
    {
        id = (id << 8) + static_cast<nameid>(name[n]);
    }
    return id;
}

namespace name_id
{
static constexpr nameid ipc = makeNameid("ipc");
static constexpr nameid c = makeNameid("c");
static constexpr nameid upci = makeNameid("upci");
static constexpr nameid upco = makeNameid("upco");
static constexpr nameid share = makeNameid("share");
static constexpr nameid dshare = makeNameid("dshare");
static constexpr nameid put = makeNameid("put");
static constexpr nameid d = makeNameid("d");
static constexpr nameid u = makeNameid("u");
static constexpr nameid psts = makeNameid("psts");
static constexpr nameid psts_v2 = makeNameid("psts_v2");
static constexpr nameid pses = makeNameid("pses");
static constexpr nameid ph = makeNameid("ph");
static constexpr nameid ass = makeNameid("ass");
#ifdef ENABLE_CHAT
static constexpr nameid mcsmp = makeNameid("mcsmp");
static constexpr nameid mcsmr = makeNameid("mcsmr");
#endif
} // namespace name_id

// convert 1...8 character ID to int64 integer (endian agnostic)
//
// @deprecated: Symbols below have been deprecated. Use the more generic functinality above to
// obtain nameid-s
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

} // namespace mega

#endif // MEGA_NAME_ID_H
