#pragma once

#include <mega/file_service/file_request.h>
#include <mega/file_service/file_request_tags.h>
#include <mega/file_service/type_traits.h>

#include <type_traits>

namespace mega
{
namespace file_service
{
namespace detail
{

template<typename T>
using DetectTypeType = typename T::Type;

template<typename T, typename U>
using HasTypeOf = std::is_same<DetectedT<DetectTypeType, T>, U>;

template<typename T>
using IsFileFlushRequest = std::is_base_of<FileFlushRequest, T>;

template<typename T>
constexpr auto IsFileFlushRequestV = IsFileFlushRequest<T>::value;

template<typename T>
using IsFileReadRequest = HasTypeOf<T, FileReadRequestTag>;

template<typename T>
constexpr auto IsFileReadRequestV = IsFileReadRequest<T>::value;

template<typename T>
using IsFileRequest = std::is_constructible<FileRequest, T>;

template<typename T>
constexpr auto IsFileRequestV = IsFileRequest<T>::value;

template<typename T>
using IsFileWriteRequest = HasTypeOf<T, FileWriteRequestTag>;

template<typename T>
constexpr auto IsFileWriteRequestV = IsFileWriteRequest<T>::value;

} // detail

using detail::IsFileFlushRequest;
using detail::IsFileFlushRequestV;
using detail::IsFileReadRequest;
using detail::IsFileReadRequestV;
using detail::IsFileRequest;
using detail::IsFileRequestV;
using detail::IsFileWriteRequest;
using detail::IsFileWriteRequestV;

} // file_service
} // mega
