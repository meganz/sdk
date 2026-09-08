#pragma once

#include <mega/common/date_time_forward.h>
#include <mega/common/node_info_forward.h>

#include <iosfwd>

namespace mega
{
namespace common
{

std::ostream& operator<<(std::ostream& ostream, const DateTime& value);

std::ostream& operator<<(std::ostream& ostream, const NodeInfo& value);

} // fuse
} // mega
