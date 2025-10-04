#pragma once

#include <mega/common/logging.h>

#include <exception>

namespace mega
{
namespace common
{

template<typename T>
class InstanceLogger
{
    // The name of the class we are logging.
    const char* mClassName;

    // The class instance we are logging.
    const T& mInstance;

    // The logger we'll use to emit log messages.
    Logger& mLogger;

    // Used to detect if we're destructing due to an exception.
    int mUncaughtExceptions;

public:
    InstanceLogger(const char* className, const T& instance, Logger& logger):
        mClassName(className),
        mInstance(instance),
        mLogger(logger),
        mUncaughtExceptions(std::uncaught_exceptions())
    {
        LogDebugF(mLogger, "%s (%p) constructed", mClassName, &mInstance);
    }

    ~InstanceLogger()
    {
        const char* message = "";

        // We're being destructed due to exception unwinding.
        if (std::uncaught_exceptions() > mUncaughtExceptions)
            message = " due to uncaught exception";

        LogDebugF(mLogger, "%s (%p) destructed%s", mClassName, &mInstance, message);
    }
}; // InstanceLogger

} // common
} // mega
