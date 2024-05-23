#pragma once

#include <cstddef>

#include <mega/fuse/common/node_event_type_forward.h>

namespace mega
{
namespace fuse
{

#define DEFINE_NODE_EVENT_TYPES(expander) \
    expander(NODE_EVENT_ADDED) \
    expander(NODE_EVENT_MODIFIED) \
    expander(NODE_EVENT_MOVED) \
    expander(NODE_EVENT_PERMISSIONS) \
    expander(NODE_EVENT_REMOVED)

enum NodeEventType : unsigned int
{
#define DEFINE_NODE_EVENT_TYPE_ENUMERANT(name) name,
    DEFINE_NODE_EVENT_TYPES(DEFINE_NODE_EVENT_TYPE_ENUMERANT)
#undef DEFINE_NODE_EVENT_TYPE_ENUMERANT
}; // NodeEventType

#define PLUS1(name) + 1

constexpr std::size_t NUM_NODE_EVENT_TYPES =
    DEFINE_NODE_EVENT_TYPES(PLUS1);

#undef PLUS1

const char* toString(NodeEventType type);

} // fuse
} // mega

