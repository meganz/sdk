#pragma once

#include <mega/file_service/file_location_provider.h>
#include <mega/file_service/file_location_provider_id.h>
#include <mega/file_service/file_service.h>
#include <mega/file_service/file_service_result_or.h>

namespace mega
{
namespace file_service
{
namespace testing
{

class ScopedFileLocationProvider
{
    // What is our provider's ID?
    FileLocationProviderID mID;

    // What service did we add the provider to?
    FileService& mService;

public:
    ScopedFileLocationProvider(FileLocationProvider provider, FileService& service):
        mID(),
        mService(service)
    {
        // Try and add the provider to service.
        auto id = service.addLocationProvider(std::move(provider));

        // Sanity: Adding a location provider should never fail.
        assert(id);

        // Remember what the provider's ID is.
        mID = *id;
    }

    ~ScopedFileLocationProvider()
    {
        // Remove the provider from service.
        mService.removeLocationProvider(mID);
    }
}; // ScopedFileLocationProvider

} // testing
} // file_service
} // mega
