#pragma once

#include <mega/common/node_event_observer_forward.h>
#include <mega/common/node_event_queue_forward.h>

namespace mega
{
namespace common
{

class NodeEventObserver
{
protected:
    NodeEventObserver() = default;

    NodeEventObserver(const NodeEventObserver& other) = delete;

public:
    virtual ~NodeEventObserver() = default;

    NodeEventObserver& operator=(const NodeEventObserver& rhs) = default;

    // Called by the client when nodes have changed in the cloud.
    virtual void updated(NodeEventQueue& events) = 0;
}; // NodeEventObserver

} // common
} // mega

