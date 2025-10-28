#pragma once

#include <mega/file_service/avl_tree.h>
#include <mega/file_service/file_range_forward.h>
#include <mega/file_service/file_range_traits.h>
#include <mega/file_service/file_range_tree_node.h>
#include <mega/file_service/file_range_tree_traits.h>

#include <functional>

namespace mega
{
namespace file_service
{
namespace detail
{

template<typename KeyFunction, typename ValueType>
using IsValidKeyFunction = std::is_invocable_r<const FileRange&, KeyFunction, const ValueType&>;

template<typename KeyFunction, typename ValueType>
constexpr auto IsValidKeyFunctionV = IsValidKeyFunction<KeyFunction, ValueType>::value;

template<typename ValueType>
struct IsValidValueType: IsFileRange<ValueType>
{}; // IsValidValueType<ValueType>

template<typename FirstType, typename SecondType>
struct IsValidValueType<std::pair<FirstType, SecondType>>: IsFileRange<FirstType>
{}; // IsValidValueType<std::pair<FirstType, SecondType>>

template<typename ValueType>
constexpr auto IsValidValueTypeV = IsValidValueType<ValueType>::value;

template<typename KeyFunctionType, typename ValueType>
struct IndexByRangeBegin
{
    // Sanity.
    static_assert(IsValidValueTypeV<ValueType>);
    static_assert(IsValidKeyFunctionV<KeyFunctionType, ValueType>);

    struct KeyFunction
    {
        auto& operator()(const ValueType& value) const
        {
            return KeyFunctionType()(value).mBegin;
        }
    }; // KeyFunction

    // Convenenience.
    using NodeType = FileRangeTreeNode<ValueType>;

    static constexpr auto mLinkPointer = &NodeType::mByRangeBegin;
    static constexpr auto mValuePointer = &NodeType::mValue;
}; // IndexByRangeBegin<KeyFunction, ValueType>

template<typename KeyFunctionType, typename ValueType>
struct IndexByRangeEnd
{
    // Sanity.
    static_assert(IsValidValueTypeV<ValueType>);
    static_assert(IsValidKeyFunctionV<KeyFunctionType, ValueType>);

    struct KeyFunction
    {
        auto& operator()(const ValueType& value) const
        {
            return KeyFunctionType()(value).mEnd;
        }
    }; // KeyFunction

    // Convenenience.
    using NodeType = FileRangeTreeNode<ValueType>;

    static constexpr auto mLinkPointer = &NodeType::mByRangeEnd;
    static constexpr auto mValuePointer = &NodeType::mValue;
}; // IndexByRangeEnd<KeyFunction, ValueType>

} // detail
} // file_service
} // mega
