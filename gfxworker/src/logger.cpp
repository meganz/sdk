#include "gfxworker/logger.h"

#include <iostream>

namespace mega {
namespace gfx {

void Logger::log(const char *time, int loglevel, const char *source, const char *message
#ifdef ENABLE_LOG_PERFORMANCE
          , const char **directMessages, size_t *directMessagesSizes , int numberMessages
#endif
)
{
    std::cout << std::string(time) << " " << loglevel << " " << std::string(source) << ":" << std::string(message) << std::endl;
}

}
}