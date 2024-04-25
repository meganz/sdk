#pragma once

#include <algorithm>
#include <string>

#include <mega/fuse/common/path_adapter_forward.h>
#include <mega/fuse/common/type_traits.h>

#include <mega/filesystem.h>

namespace mega
{

template<typename Traits>
struct IsPath<fuse::detail::PathAdapter<Traits>>
  : std::true_type
{
}; // IsPath<fuse::detail::PathAdapter<Traits>>

namespace fuse
{
namespace detail
{

template<typename TraitsType>
class PathAdapter
{
    using SizeType   = typename TraitsType::SizeType;
    using StringType = typename TraitsType::StringType;
    using ValueType  = typename TraitsType::ValueType;

    const ValueType* mPath = nullptr;
    SizeType mLength = 0;

public:
    PathAdapter() = default;

    explicit PathAdapter(const StringType& path)
      : mPath(path.data())
      , mLength(path.size())
    {
    }

    PathAdapter(const PathAdapter& other) = default;

    PathAdapter& operator=(const PathAdapter& rhs) = default;

    // Clear the path.
    void clear()
    {
        operator=(PathAdapter());
    }

    // Query whether the path is empty.
    bool empty() const
    {
        return !mPath || !mLength;
    }

    // Locate the next path separator.
    bool findNextSeparator(SizeType& index) const
    {
        auto* current = mPath + index;
        auto* end = mPath + mLength;

        // Index is at, or beyond, the end of this path.
        if (current >= end)
            return false;

        // Search for the next separator.
        auto* next = std::find(current, end, TraitsType::separator());

        // Couldn't find another separator.
        if (next == end)
            return false;

        // Compute index of separator.
        index = static_cast<SizeType>(next - mPath);

        // Let the caller know we've found another separator.
        return true;
    }

    // Query whether the path has any further components.
    bool hasNextPathComponent(SizeType index) const
    {
        // Not strictly correct but good enough.
        return index < mLength;
    }

    // Retrieve the next path component.
    bool nextPathComponent(SizeType& index,
                           PathAdapter& component) const
    {
        auto* current = mPath + index;
        auto* end = mPath + mLength;

        // Index is at, or beyond, the end of this path.
        if (current >= end)
            return component.clear(), false;

        // For convenience.
        auto separator = [](const ValueType& character) {
            return character == TraitsType::separator();
        }; // separator

        // Skip any leading separators.
        current = std::find_if_not(current, end, separator);

        // Path is effectively empty.
        if (current == end)
            return component.clear(), false;

        // Find the end of this component.
        end = std::find(current, end, TraitsType::separator());

        // Populate component.
        component.mPath = current;
        component.mLength = static_cast<SizeType>(end - current);

        // Compute index of next separator.
        index = static_cast<SizeType>(end - mPath);

        // Let the caller know we've extracted a component.
        return true;
    }

    // Translate component into a cloud-friendly form.
    const std::string toName(const FileSystemAccess& fsAccess) const
    {
        // Return transcoded path to caller.
        return TraitsType::toUTF8(mPath, mLength);
    }
}; // PathAdapter<T>

} // detail
} // fuse
} // mega

