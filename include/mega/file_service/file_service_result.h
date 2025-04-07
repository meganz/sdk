#pragma once

#include <mega/file_service/file_service_result_forward.h>

namespace mega
{
namespace file_service
{

#define DEFINE_FILE_SERVICE_RESULTS(expander) \
    expander(UNEXPECTED, "The File Service encountered an unexpected error")

enum FileServiceResult : unsigned int
{
#define DEFINE_ENUMERANT(name, description) FILE_SERVICE_##name,
    DEFINE_FILE_SERVICE_RESULTS(DEFINE_ENUMERANT)
#undef DEFINE_ENUMERANT
}; // FileServiceResult

auto toDescription(FileServiceResult result) -> const char*;

auto toString(FileServiceResult result) -> const char*;

} // file_service
} // mega
