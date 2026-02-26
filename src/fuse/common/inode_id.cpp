#include <cassert>
#include <iomanip>
#include <limits>
#include <sstream>

#include <mega/common/query.h>
#include <mega/fuse/common/inode_id.h>
#include <mega/fuse/common/mount_inode_id.h>

#include <mega/base64.h>
#include <mega/utils.h>

namespace mega
{

constexpr auto Synthetic = std::uint64_t(1) << 63;

namespace common
{

using fuse::InodeID;

InodeID SerializationTraits<InodeID>::from(const Field& field)
{
    auto value = field.get<std::uint64_t>();

    if (value < Synthetic)
        return InodeID(NodeHandle().set6byte(value));

    return InodeID(value);
}

void SerializationTraits<InodeID>::to(Parameter& parameter,
                                      const InodeID& value)
{
    parameter.set(value.get());
}

} // common

namespace fuse
{

constexpr auto Undefined = std::numeric_limits<std::uint64_t>::max();

InodeID::InodeID()
  : mValue(Undefined)
{
}

InodeID::InodeID(MountInodeID id)
  : mValue(id.get())
{
}

InodeID::InodeID(NodeHandle handle)
  : mValue(handle.as8byte())
{
}

InodeID::InodeID(std::uint64_t value)
  : mValue(value | Synthetic)
{
    assert(value != Undefined);
}

InodeID::operator NodeHandle() const
{
    if (mValue == Undefined)
        return NodeHandle();

    NodeHandle handle;

    // Sanity.
    assert(!(mValue & Synthetic));

    handle.set6byte(mValue);

    return handle;
}

InodeID::operator bool() const
{
    return mValue != Undefined;
}

bool InodeID::operator==(const InodeID& rhs) const
{
    return mValue == rhs.mValue;
}

bool InodeID::operator==(const NodeHandle& rhs) const
{
    return rhs.as8byte() == mValue;
}

bool InodeID::operator<(const InodeID& rhs) const
{
    return mValue < rhs.mValue;
}

bool InodeID::operator!=(const InodeID& rhs) const
{
    return mValue != rhs.mValue;
}

bool InodeID::operator!=(const NodeHandle& rhs) const
{
    assert(!rhs.isUndef());

    return rhs.as8byte() != mValue;
}

bool InodeID::operator!() const
{
    return mValue == Undefined;
}

InodeID InodeID::fromFileName(const std::string& filename)
{
    // Name's not longer enough to contain a valid Inode ID.
    if (filename.size() < 16)
        return InodeID();

    // Convenience.
    SplitFragment name;
    SplitFragment extension;

    // Split the name into two chunks: name and extension.
    std::tie(name, extension) = split(filename, '.');

    // Filename's nothing but a giant extension.
    if (!name.second)
        return InodeID();

    InodeID id;
    bool result;

    // Try and decode the inode's ID.
    std::tie(id.mValue, result) =
      fromHex<std::uint64_t>(name.first, name.second);

    // Successfully decoded the inode's ID.
    if (result)
        return id;

    // Couldn't decode the inode's ID.
    return InodeID();
}

std::uint64_t InodeID::get() const
{
    return mValue;
}

bool InodeID::synthetic() const
{
    return (mValue & Synthetic) > 0;
}

std::string toFileName(InodeID id)
{
    std::ostringstream ostream;

    ostream << std::hex
            << std::setfill('0')
            << std::setw(16)
            << id.get();

    return ostream.str();
}

std::string toString(InodeID id)
{
    return toHandle(id.get());
}

} // fuse
} // mega

