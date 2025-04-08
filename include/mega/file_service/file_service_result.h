#pragma once

#include <mega/file_service/file_service_result_forward.h>

namespace mega
{
namespace file_service
{

#define DEFINE_FILE_SERVICE_RESULTS(expander) \
    expander(ALREADY_INITIALIZED, "The File Service has already been initialized") \
    expander(SUCCESS, "The File Service completed the operation successfully") \
    expander(UNEXPECTED, "The File Service encountered an unexpected error") \
    expander(UNINITIALIZED, "The File Service has not been initialized") \
    expander(UNKNOWN_FILE, "The specified file isn't known by the File Service")

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
