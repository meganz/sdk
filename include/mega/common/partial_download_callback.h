#pragma once

#include <mega/common/deciseconds.h>
#include <mega/common/partial_download_callback_forward.h>

#include <cstdint>
#include <variant>

namespace mega
{

class Error;

namespace common
{

class PartialDownloadCallback
{
protected:
    PartialDownloadCallback() = default;

public:
    // Indicates the download should be aborted.
    struct Abort
    {}; // Abort

    // Indicates that the download should continue.
    struct Continue
    {}; // Continue

    // Indicates the download should be retried.
    struct Retry
    {
        Retry(deciseconds when):
            mWhen(when)
        {}

        // When the download should be retried.
        const deciseconds mWhen;
    }; // Retry

    virtual ~PartialDownloadCallback() = default;

    // Called when the download has completed.
    virtual void completed(Error result) = 0;

    // Called repeatedly as data is downloaded from the cloud.
    virtual auto data(const void* buffer, std::uint64_t offset, std::uint64_t length)
        -> std::variant<Abort, Continue> = 0;

    // Called when the download has failed.
    virtual auto failed(Error result, int retries) -> std::variant<Abort, Retry> = 0;
}; // PartialDownloadCallback

} // common
} // mega
