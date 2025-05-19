#include <mega/base64.h>
#include <mega/common/query.h>
#include <mega/file_service/file_id.h>
#include <mega/types.h>

#include <cinttypes>
#include <limits>

namespace mega
{
namespace common
{

using namespace file_service;

FileID SerializationTraits<FileID>::from(const Field& field)
{
    auto value = field.get<std::uint64_t>();

    if (synthetic(value))
        return FileID::from(value);

    return FileID::from(NodeHandle().set6byte(value));
}

void SerializationTraits<FileID>::to(Parameter& parameter, FileID id)
{
    parameter.set(id.toU64());
}

} // common

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

bool FileID::operator==(const FileID& rhs) const
{
    return mID == rhs.mID;
}

bool FileID::operator<(const FileID& rhs) const
{
    return mID < rhs.mID;
}

bool FileID::operator!=(const FileID& rhs) const
{
    return !(*this == rhs);
}

bool FileID::operator!() const
{
    return !operator bool();
}

FileID FileID::from(NodeHandle handle)
{
    if (!handle.isUndef())
        return FileID(handle.as8byte());

    return FileID();
}

FileID FileID::from(std::uint64_t u64)
{
    assert(!synthetic(u64));

    return FileID(kSynthetic | u64);
}

NodeHandle FileID::toHandle() const
{
    assert(!synthetic(mID));

    if (*this)
        return NodeHandle().set6byte(mID);

    return NodeHandle();
}

std::string FileID::toString() const
{
    std::string string(16, '\x0');

    auto length = Base64::btoa(reinterpret_cast<const byte*>(&mID), sizeof(mID), string.data());

    string.resize(static_cast<std::size_t>(length));

    return string;
}

std::uint64_t FileID::toU64() const
{
    return mID;
}

bool synthetic(FileID id)
{
    return synthetic(id.toU64());
}

bool synthetic(std::uint64_t u64)
{
    return u64 != kUndefined && u64 >= kSynthetic;
}

std::string toString(FileID id)
{
    return id.toString();
}

} // file_service
} // mega
