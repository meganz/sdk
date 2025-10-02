#pragma once

#include <mega/common/expected_forward.h>
#include <mega/common/unexpected_forward.h>
#include <mega/file_service/file_service_result_forward.h>

namespace mega
{
namespace file_service
{

template<typename T>
using FileServiceResultOr = common::Expected<FileServiceResult, T>;

using common::unexpected;

} // file_service
} // mega
