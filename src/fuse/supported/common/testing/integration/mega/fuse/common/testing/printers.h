#pragma once

#include "utility.h"

#include <mega/common/testing/printers.h>
#include <mega/fuse/common/inode_info_forward.h>
#include <mega/fuse/common/mount_event_type_forward.h>
#include <mega/fuse/common/mount_result_forward.h>
#include <mega/fuse/common/testing/utility.h>
#include <mega/types.h>

#include <iosfwd>

namespace mega
{
namespace fuse
{

std::ostream& operator<<(std::ostream& ostream, const InodeInfo& value);

void PrintTo(const MountEventType type, std::ostream* ostream);

void PrintTo(const MountResult result, std::ostream* ostream);

} // fuse
} // mega
