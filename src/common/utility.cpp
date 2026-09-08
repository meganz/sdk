#include <mega/common/utility.h>

#include <cassert>
#include <exception>

namespace mega
{
namespace common
{

std::chrono::minutes defaultTimeout()
{
    return std::chrono::minutes(5);
}

std::string exceptionMessage(const char* defaultMessage)
{
    // Sanity: defaultMessage should never be null.
    assert(defaultMessage != nullptr);

    // Just in case it is null.
    if (defaultMessage == nullptr)
        defaultMessage = "unknown";

    try
    {
        // Get a reference to the current exception.
        auto exception = std::current_exception();

        // We haven't been called with an active exception.
        if (exception == nullptr)
            return "no active exception";

        // Rethrow the exception so we can get its dynamic type.
        std::rethrow_exception(exception);
    }
    catch (std::exception& exception)
    {
        // Exception is a subclass of std::exception.
        return exception.what();
    }
    catch (...)
    {
        // Exception is of an unknown type.
        return defaultMessage;
    }
}

std::string exceptionMessage()
{
    return exceptionMessage("unknown");
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

std::optional<std::string> fromCharPointer(const char* maybeString)
{
    if (maybeString)
        return std::string(maybeString);

    return std::nullopt;
}

std::optional<std::string> fromStringPointer(const std::string* maybeString)
{
    if (maybeString && !maybeString->empty())
        return *maybeString;

    return std::nullopt;
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

const char* toCharPointer(const std::optional<std::string>& maybeString)
{
    if (maybeString)
        return maybeString->data();

    return nullptr;
}

const std::string* toStringPointer(const std::optional<std::string>& maybeString)
{
    if (maybeString)
        return &*maybeString;

    return nullptr;
}

} // common
} // mega

