/**
 * @file ScopedUploadSpeedLimit.h
 * @brief RAII wrapper around MegaApi::setMaxUploadSpeed.
 *
 * Sets a temporary upload-speed cap on construction and restores unlimited
 * (-1) on destruction. Used by WS tests that need predictable mid-transfer
 * timing windows (e.g. file-deletion / file-modification / overquota tests
 * that depend on the transfer being mid-flight when a side action fires).
 */

#pragma once

#include "megaapi.h"

namespace mega::test::wsupload
{

class ScopedUploadSpeedLimit
{
public:
    ScopedUploadSpeedLimit(::mega::MegaApi& api, int bytesPerSecond) :
        mApi(&api)
    {
        mApi->setMaxUploadSpeed(bytesPerSecond);
    }

    ~ScopedUploadSpeedLimit()
    {
        if (mApi)
        {
            mApi->setMaxUploadSpeed(-1);
        }
    }

    ScopedUploadSpeedLimit(const ScopedUploadSpeedLimit&) = delete;
    ScopedUploadSpeedLimit& operator=(const ScopedUploadSpeedLimit&) = delete;

    ScopedUploadSpeedLimit(ScopedUploadSpeedLimit&& other) noexcept :
        mApi(other.mApi)
    {
        other.mApi = nullptr;
    }

    ScopedUploadSpeedLimit& operator=(ScopedUploadSpeedLimit&&) = delete;

private:
    ::mega::MegaApi* mApi;
};

} // namespace mega::test::wsupload
