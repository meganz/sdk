#pragma once

#include <string>

#include <mega/common/bind_handle_forward.h>
#include <mega/common/node_event_forward.h>
#include <mega/common/node_event_type_forward.h>
#include <mega/common/node_info_forward.h>

#include <mega/types.h>

namespace mega
{
namespace common
{

class NodeEvent
{
protected:
    NodeEvent() = default;

    ~NodeEvent() = default;

public:
    // What is this node's bind handle?
    virtual BindHandle bindHandle() const = 0;

    // Is this node a directory?
    virtual bool isDirectory() const = 0;

    // What is this node's handle?
    virtual NodeHandle handle() const = 0;

    // Retrieve this node's description.
    virtual NodeInfo info() const = 0;

    // What is this node's name?
    virtual const std::string& name() const = 0;

    // Who is this node's parent?
    virtual NodeHandle parentHandle() const = 0;

    // What kind of event is this?
    virtual NodeEventType type() const = 0;
}; // NodeEvent

} // common
} // mega

