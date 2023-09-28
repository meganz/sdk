#pragma once

#include "megaapi.h"

namespace mega {
namespace gfx {


class Logger : public mega::MegaLogger {
public:
    void log(const char *time, int loglevel, const char *source, const char *message
#ifdef ENABLE_LOG_PERFORMANCE
          , const char **directMessages = nullptr, size_t *directMessagesSizes = nullptr, unsigned numberMessages = 0
#endif
    ) override;


};

}
}