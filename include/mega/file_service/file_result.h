#pragma once

#include <mega/file_service/file_result_forward.h>

namespace mega
{
namespace file_service
{

#define DEFINE_FILE_RESULTS(expander) expander(CANCELLED, "The file operation has been cancelled")

enum FileResult : unsigned int
{
#define DEFINE_ENUMERANT(name, description) FILE_##name,
    DEFINE_FILE_RESULTS(DEFINE_ENUMERANT)
#undef DEFINE_ENUMERANT
}; // FileResult

const char* toDescription(FileResult result);

const char* toString(FileResult result);

} // file_service
} // mega
