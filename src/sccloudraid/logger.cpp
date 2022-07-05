#include "mega/sccloudraid/mega.h"

mega::SCCR::Logger logger("/usr/local/mega/logdsock");

void mega::SCCR::Logger::logf(const char* fmt, ...)
{
    char buf[16384];

    va_list va;
    va_start(va, fmt);
    vsnprintf(buf, sizeof buf, fmt, va);
    va_end(va);
    
    puts(buf);
}

void mega::SCCR::Logger::logline(const char* item, int len)
{
    if (len < 0) len = strlen(item);

    write(fileno(stdout), item, len);
    write(fileno(stdout), "\n", 1);

    if (s >= 0 && sendto(s, item, len, 0, (sockaddr*)&addr, sizeof addr) != len)
    {
        std::cout << "*** Error writing to log socket (" << errno << "), len=" << len << std::endl;
    }
}

mega::SCCR::Logger::Logger(const char* sunpath)
{
}
