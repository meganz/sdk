#include <cassert>

#include <mega/common/utility.h>

namespace mega
{
namespace common
{

std::chrono::minutes defaultTimeout()
{
    return std::chrono::minutes(5);
}

std::string format(const char* format, ...)
{
    assert(format);

    std::va_list arguments;

    va_start(arguments, format);

    auto result = formatv(arguments, format);

    va_end(arguments);

    return result;
}

std::string formatv(std::va_list arguments, const char* format)
{
    assert(format);

    std::va_list temp;

    va_copy(temp, arguments);

    auto required = std::vsnprintf(nullptr,
                                   0,
                                   format,
                                   temp);

    va_end(temp);

    std::string buffer;

    buffer.resize(static_cast<std::size_t>(required) + 1);

    va_copy(temp, arguments);

    std::vsnprintf(&buffer[0],
                   buffer.size(),
                   format,
                   temp);

    buffer.pop_back();

    va_end(temp);

    return buffer;
}

std::int64_t now()
{
    // Convenience.
    using std::chrono::system_clock;

    // Get our hands on the current time.
    auto now = system_clock::now();

    // Return the current time to our caller as a time_t value.
    return system_clock::to_time_t(now);
}

} // common
} // mega

