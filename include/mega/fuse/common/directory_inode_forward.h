#pragma once

#include <memory>

#include <mega/common/badge_forward.h>
#include <mega/fuse/common/ref_forward.h>

namespace mega
{
namespace fuse
{

class DirectoryInode;

using DirectoryInodeBadge = common::Badge<DirectoryInode>;
using DirectoryInodeRef = Ref<DirectoryInode>;

// Interface to Ref<T>.
void doRef(RefBadge badge, DirectoryInode& inode);

void doUnref(RefBadge badge, DirectoryInode& inode);

} // fuse
} // mega

