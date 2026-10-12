#pragma once

#include <mega/file_service/application_data_forward.h>

#include <atomic>
#include <cstdint>

namespace mega
{
namespace file_service
{

// Represents a particular application using the File Service.
class ApplicationID
{
    // The concrete type of each ID.
    using IDType = std::uint8_t;

    // The ID of a specific application.
    IDType mID{0};

    // Used to generate a unique ID for each application.
    static inline std::atomic<IDType> mNextID{1};

    // Construct a new application ID.
    explicit ApplicationID(IDType id):
        mID(id)
    {}

public:
    ApplicationID() = default;

    ApplicationID(const ApplicationID& other) = default;

    ApplicationID& operator=(const ApplicationID& rhs) = default;

    // Does rhs represent the same application as this instance?
    bool operator==(const ApplicationID& rhs) const
    {
        return mID == rhs.mID;
    }

    // Does rhs represent a different application than this instance?
    bool operator!=(const ApplicationID& rhs) const
    {
        return !(*this == rhs);
    }

    // Generate a unique ID associated with the specified tag.
    template<typename ApplicationTag>
    static ApplicationID generate()
    {
        static IDType id = mNextID.fetch_add(1);

        return ApplicationID{id};
    }
}; // ApplicationID

struct ApplicationData
{
    bool operator==(const ApplicationData& rhs) const
    {
        return mData == rhs.mData && mID == rhs.mID;
    }

    bool operator!=(const ApplicationData& rhs) const
    {
        return !(*this == rhs);
    }

    std::uint64_t mData;
    ApplicationID mID;
}; // ApplicationData

} // file_service
} // mega
