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

public:
    InstanceLogger(const char* className, const T& instance, Logger& logger):
        mClassName(className),
        mInstance(instance),
        mLogger(logger)
    {
        LogDebugF(mLogger, "%s (%p) constructed", mClassName, &mInstance);
    }

    ~InstanceLogger()
    {
        LogDebugF(mLogger, "%s (%p) destructed", mClassName, &mInstance);
    }
}; // InstanceLogger

} // common
} // mega
