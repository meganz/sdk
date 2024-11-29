#include <mega/fuse/common/bind_handle.h>

namespace mega
{
namespace fuse
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

} // fuse
} // mega

