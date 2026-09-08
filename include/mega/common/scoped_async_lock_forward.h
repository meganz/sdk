#pragma once

namespace mega
{
namespace common
{
namespace detail
{

template<template<typename> typename Derived, typename Mutex>
class ScopedAsyncLock;

template<typename Mutex>
class ScopedAsyncSharedLock;

template<typename Mutex>
class ScopedAsyncUniqueLock;

} // detail

using detail::ScopedAsyncSharedLock;
using detail::ScopedAsyncUniqueLock;

} // common
} // mega
