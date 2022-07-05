#ifndef MEGA_SCCR_LOGGER_H
#define MEGA_SCCR_LOGGER_H 1

namespace mega::SCCR {

class Logger
{
    int s;
    struct sockaddr_un addr = { AF_UNIX, "" };

public:
    void logf(const char*, ...);
    void logline(const char*, int = -1);

    Logger(const char*);
};

#define LOGF logger.logf
#define LOG logger.logline

} // namespace

extern mega::SCCR::Logger logger;

#endif
