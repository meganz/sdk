#pragma once

#include "megaapi.h"

namespace mega {
namespace gfx {

class Logger : public mega::MegaLogger {
public:
    void log(const char *time, int loglevel, const char *source, const char *message
#ifdef ENABLE_LOG_PERFORMANCE
          , const char **directMessages, size_t *directMessagesSizes, int numberMessages
#endif
    ) override;
};

}
}