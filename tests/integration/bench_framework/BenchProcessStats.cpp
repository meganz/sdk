#include "headers/BenchProcessStats.h"

#if defined(__unix__) || defined(__APPLE__)
#include <sys/resource.h>
#include <sys/time.h>
#endif

namespace mega::bench
{

BenchProcessStats sampleProcessStats()
{
    BenchProcessStats out;
#if defined(__unix__) || defined(__APPLE__)
    struct rusage ru
    {};
    if (getrusage(RUSAGE_SELF, &ru) == 0)
    {
        out.rssMaxKb = static_cast<std::int64_t>(ru.ru_maxrss);
#if defined(__APPLE__)
        // macOS reports ru_maxrss in bytes (not kB as on Linux); normalise to kB.
        out.rssMaxKb /= 1024;
#endif
        out.userCpuMs =
            static_cast<std::int64_t>(ru.ru_utime.tv_sec) * 1000 +
            static_cast<std::int64_t>(ru.ru_utime.tv_usec) / 1000;
        out.sysCpuMs =
            static_cast<std::int64_t>(ru.ru_stime.tv_sec) * 1000 +
            static_cast<std::int64_t>(ru.ru_stime.tv_usec) / 1000;
        out.sampled = true;
    }
#endif
    return out;
}

BenchProcessStats deltaProcessStats(const BenchProcessStats& before,
                                    const BenchProcessStats& after)
{
    BenchProcessStats out;
    if (!before.sampled || !after.sampled)
        return out;
    out.rssMaxKb = after.rssMaxKb - before.rssMaxKb;
    out.userCpuMs = after.userCpuMs - before.userCpuMs;
    out.sysCpuMs = after.sysCpuMs - before.sysCpuMs;
    out.sampled = true;
    return out;
}

} // namespace mega::bench
