#include <mega/file_service/file_service_result.h>

#include <cassert>

namespace mega
{
namespace file_service
{

const char* toDescription(FileServiceResult result)
{
    static const char* descriptions[] = {
#define DEFINE_DESCRIPTION(name, description) description,
        DEFINE_FILE_SERVICE_RESULTS(DEFINE_DESCRIPTION)
#undef DEFINE_DESCRIPTION
    }; // descriptions

    if (result < sizeof(descriptions))
        return descriptions[result];

    assert(false && "Unhandled file service result enumerant");

    return "N/A";
}

const char* toString(FileServiceResult result)
{
    static const char* names[] = {
#define DEFINE_NAME(name, description) #name,
        DEFINE_FILE_SERVICE_RESULTS(DEFINE_NAME)
#undef DEFINE_NAME
    }; // names

    if (result < sizeof(names))
        return names[result];

    assert(false && "Unhandled file service result enumerant");

    return "N/A";
}

} // file_service
} // mega
