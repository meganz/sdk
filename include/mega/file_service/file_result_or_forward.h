#pragma once

#include <mega/common/expected_forward.h>
#include <mega/common/unexpected_forward.h>
#include <mega/file_service/file_result_forward.h>

namespace mega
{
namespace file_service
{

template<typename T>
using FileResultOr = common::Expected<FileResult, T>;

using common::unexpected;

} // file_service
} // mega
