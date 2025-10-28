#pragma once

#include <mega/common/partial_download_forward.h>

#include <cstdint>

namespace mega
{
namespace common
{

class PartialDownload
{
protected:
    PartialDownload() = default;

public:
    virtual ~PartialDownload() = default;

    // Begin the partial download.
    virtual void begin() = 0;

    // Cancel the partial download.
    virtual bool cancel() = 0;

    // Is this download cancellable?
    virtual bool cancellable() const = 0;

    // Has the download been cancelled?
    virtual bool cancelled() const = 0;

    // Has this download completed?
    virtual bool completed() const = 0;
}; // PartialDownload

} // common
} // mega
