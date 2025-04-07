#include <mega/file_service/file_id.h>
#include <mega/types.h>
#include <mega/utils.h>

#include <cinttypes>
#include <limits>

namespace mega
{
namespace file_service
{

static constexpr auto kSynthetic = UINT64_C(1) << 63u;

static constexpr auto kUndefined = std::numeric_limits<std::uint64_t>::max();

FileID::FileID(std::uint64_t id):
    mID(id)
{}

FileID::FileID():
    FileID(kUndefined)
{}

FileID::operator bool() const
{
    return mID != kUndefined;
}

auto FileID::operator==(const FileID& rhs) const -> bool
{
    return mID == rhs.mID;
}

auto FileID::operator<(const FileID& rhs) const -> bool
{
    return mID < rhs.mID;
}

auto FileID::operator!=(const FileID& rhs) const -> bool
{
    return !(*this == rhs);
}

auto FileID::operator!() const -> bool
{
    return !operator bool();
}

auto FileID::from(NodeHandle handle) -> FileID
{
    if (!handle.isUndef())
        return FileID(handle.as8byte());

    return FileID();
}

auto FileID::from(std::uint64_t u64) -> FileID
{
    assert(!synthetic(u64));

    return FileID(kSynthetic | u64);
}

auto FileID::toHandle() const -> NodeHandle
{
    assert(!synthetic(mID));

    if (*this)
        return NodeHandle().set6byte(mID);

    return NodeHandle();
}

auto FileID::toU64() const -> std::uint64_t
{
    return mID;
}

auto synthetic(FileID id) -> bool
{
    return synthetic(id.toU64());
}

auto synthetic(std::uint64_t u64) -> bool
{
    return u64 != kUndefined && u64 >= kSynthetic;
}

auto toString(FileID id) -> std::string
{
    return toHandle(id.toU64());
}

} // file_service
} // mega
