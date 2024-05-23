#pragma once

#include <condition_variable>
#include <cstddef>
#include <mutex>

#include <mega/fuse/common/activity_monitor_forward.h>

namespace mega
{
namespace fuse
{

// Represents some action that is being performed.
class Activity
{
    friend class ActivityMonitor;

    // Informs the monitor that a new activity has begun.
    Activity(ActivityMonitor& monitor);

    // Who monitors our activity?
    ActivityMonitor* mMonitor;

public:
    Activity();

    // Informs a monitor that a new activity has begun.
    Activity(const Activity& other);

    Activity(Activity&& other);

    // Informs a monitor that an activity has been completed.
    ~Activity();

    Activity& operator=(const Activity& rhs);

    Activity& operator=(Activity&& rhs);

    void swap(Activity& other);
}; // Activity

// Lets an entity wait until all activities have completed.
class ActivityMonitor
{
    friend class Activity;

    // Signalled when all activity has completed.
    std::condition_variable mCompleted;

    // Serializes access to mProcessing.
    mutable std::mutex mLock;

    // How many activities are in progress?
    std::size_t mProcessing;

public:
    ActivityMonitor();

    ~ActivityMonitor();

    // Are any activities in progress?
    bool active() const;

    // Begin a new activity.
    Activity begin();

    // Wait until all activities have completed.
    void waitUntilIdle();
}; // ActivityMonitor

} // fuse
} // mega
