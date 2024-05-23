#include <mega/fuse/common/node_event_type.h>

namespace mega
{
namespace fuse
{


const char* toString(NodeEventType type)
{
    switch (type)
    {
#define DEFINE_NODE_EVENT_TYPE_CLAUSE(name) case name: return #name;
        DEFINE_NODE_EVENT_TYPES(DEFINE_NODE_EVENT_TYPE_CLAUSE);
#undef  DEFINE_NODE_EVENT_TYPE_CLAUSE
    }

    // Silence the compiler.
    return "N/A";
}

} // fuse
} // mega

