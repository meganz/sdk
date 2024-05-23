#pragma once

#include <cstddef>

#include <mega/fuse/common/node_event_forward.h>
#include <mega/fuse/common/node_event_queue_forward.h>

namespace mega
{
namespace fuse
{

class NodeEventQueue
{
protected:
    NodeEventQueue() = default;

    ~NodeEventQueue() = default;

public:
    // Is the queue empty?
    virtual bool empty() const = 0;

    // Retrieve a reference to the first event in the queue.
    virtual const NodeEvent& front() const = 0;

    // Pop the first event from the queue.
    virtual void pop_front() = 0;

    // How many events are in the queue?
    virtual std::size_t size() const = 0;
}; // NodeEventQueue

} // fuse
} // mega

