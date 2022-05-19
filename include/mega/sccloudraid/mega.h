#ifndef MEGA_SCCR_MEGA_H
#define MEGA_SCCR_MEGA_H 1


#include "system.h"

#include "raidstub.h"
#include "aes.h"
#include "chunkedhash.h"
#include "raidproxy.h"
#include "pstats.h"
#include "base64.h"
#include "config.h"
#include "logger.h"

using namespace mega::SCCR;

extern mtime_t currtime;
extern Config config;

namespace mega::SCCR {

int main_sccr(int argc, char** argv);

class TcpServer
{
public:
    static void makenblock(int s, bool makeblock = false)
    {
        int flags, t;

        flags = fcntl(s, F_GETFL, 0);

        if (flags == -1)
        {
            syslog(LOG_INFO, "FCNTL_GETFL failed (%d)", errno);
            return;
        }

        if (makeblock) flags &= ~O_NONBLOCK;
        else flags |= O_NONBLOCK;

        t = fcntl(s, F_SETFL, flags);

        if (t == -1)
        {
            syslog(LOG_INFO, "FCNTL_SETFL failed (%d)", errno);
        }
    }
};

} // namespace

#endif
