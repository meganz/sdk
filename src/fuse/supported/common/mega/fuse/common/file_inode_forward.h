#pragma once

#include <mega/fuse/common/ref_forward.h>

#include <memory>
#include <vector>

namespace mega
{
namespace fuse
{

class FileInode;

using FileInodeRef = Ref<FileInode>;
using FileInodeRefVector = std::vector<FileInodeRef>;

// Interface to Ref<T>.
void doRef(RefBadge badge, FileInode& inode);

void doUnref(RefBadge badge, FileInode& inode);

} // fuse
} // mega
