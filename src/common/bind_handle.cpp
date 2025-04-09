#include <mega/common/bind_handle.h>
#include <mega/common/query.h>

namespace mega
{
namespace common
{

BindHandle::BindHandle(std::string nodeKey)
  : mNodeKey(std::move(nodeKey))
{
}

BindHandle::operator bool() const
{
    return !mNodeKey.empty();
}

bool BindHandle::operator==(const BindHandle& rhs) const
{
    return mNodeKey == rhs.mNodeKey;
}

bool BindHandle::operator<(const BindHandle& rhs) const
{
    return mNodeKey < rhs.mNodeKey;
}

bool BindHandle::operator!() const
{
    return mNodeKey.empty();
}

bool BindHandle::operator!=(const BindHandle& rhs) const
{
    return mNodeKey != rhs.mNodeKey;
}

const std::string& BindHandle::get() const
{
    return mNodeKey;
}

BindHandle SerializationTraits<BindHandle>::from(const Field& field)
{
    return BindHandle(field.get<std::string>());
}

void SerializationTraits<BindHandle>::to(Parameter& parameter,
                                         const BindHandle& value)
{
    parameter.set(value.get());
}

} // common
} // mega

