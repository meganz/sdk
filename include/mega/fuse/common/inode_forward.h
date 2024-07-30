#pragma once

#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <vector>

#include <mega/fuse/common/ref_forward.h>

namespace mega
{
namespace fuse
{

class Inode;

using InodeLock = std::unique_lock<const Inode>;
using InodeLockPtr = std::shared_ptr<InodeLock>;
using InodePtr = std::unique_ptr<Inode>;
using InodeRawPtr = Inode*;
using InodeRef = Ref<Inode>;
using InodeRefSet = std::set<InodeRef>;
using InodeRefVector = std::vector<InodeRef>;

template<typename T>
using ToInodePtrMap = std::map<T, InodePtr>;

template<typename T>
using FromInodeRefMap = std::map<InodeRef, T>;

template<typename T>
using ToInodeRawPtrMap = std::map<T, InodeRawPtr>;

// Interface to Ref<T>.
void doRef(RefBadge badge, Inode& inode);

void doUnref(RefBadge badge, Inode& inode);

} // fuse
} // mega

