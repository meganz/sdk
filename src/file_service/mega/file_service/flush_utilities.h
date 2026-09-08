#pragma once

#include <mega/file_service/file_service_options_forward.h>

#include <chrono>
#include <cstdint>

namespace mega
{
namespace file_service
{

auto computeFlushDelay(const ServiceOptions& options, std::uint64_t size) -> std::chrono::seconds;

} // file_service
} // mega
