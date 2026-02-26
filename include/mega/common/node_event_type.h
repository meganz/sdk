#pragma once

#include <cstddef>

#include <mega/common/node_event_type_forward.h>

namespace mega
{
namespace common
{

#define DEFINE_NODE_EVENT_TYPES(expander) \
    expander(ADDED) \
    expander(MODIFIED) \
    expander(MOVED) \
    expander(PERMISSIONS) \
    expander(REMOVED)

enum NodeEventType : unsigned int
{
#define DEFINE_NODE_EVENT_TYPE_ENUMERANT(name) NODE_EVENT_ ## name,
    DEFINE_NODE_EVENT_TYPES(DEFINE_NODE_EVENT_TYPE_ENUMERANT)
#undef DEFINE_NODE_EVENT_TYPE_ENUMERANT
}; // NodeEventType

#define PLUS1(name) + 1

constexpr std::size_t NUM_NODE_EVENT_TYPES =
    DEFINE_NODE_EVENT_TYPES(PLUS1);

#undef PLUS1

const char* toString(NodeEventType type);

} // common
} // mega

