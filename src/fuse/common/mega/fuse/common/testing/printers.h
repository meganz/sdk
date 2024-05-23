#pragma once

#include "utility.h"
#include <ostream>

#include <mega/fuse/common/date_time_forward.h>
#include <mega/fuse/common/inode_info_forward.h>
#include <mega/fuse/common/mount_event_type_forward.h>
#include <mega/fuse/common/mount_result_forward.h>
#include <mega/fuse/common/node_info_forward.h>
#include <mega/fuse/common/testing/utility.h>

#include <mega/types.h>

namespace mega
{
namespace fuse
{

std::ostream& operator<<(std::ostream& ostream, const DateTime& value);

template<typename T>
auto operator<<(std::ostream& ostream, const T& value)
  -> typename testing::EnableIfInfoLike<T, std::ostream>::type&;

void PrintTo(const MountEventType type, std::ostream* ostream);

void PrintTo(const MountResult result, std::ostream* ostream);

} // fuse
} // mega

