#pragma once

#include <map>
#include <memory>
#include <mutex>
#include <vector>

#include <mega/fuse/common/lock_forward.h>
#include <mega/fuse/common/ref_forward.h>

namespace mega
{
namespace fuse
{

class FileIOContext;

using FileIOContextLock = UniqueLock<const FileIOContext>;
using FileIOContextSharedLock = SharedLock<const FileIOContext>;

using FileIOContextPtr = std::unique_ptr<FileIOContext>;
using FileIOContextRef = Ref<FileIOContext>;

using FileIOContextRefVector = std::vector<FileIOContextRef>;

template<typename T>
using ToFileIOContextPtrMap = std::map<T, FileIOContextPtr>;

template<typename T>
using ToFileIOContextRawPtrMap = std::map<T, FileIOContext*>;

template<typename T>
using ToFileIOContextRawPtrMapIterator =
  typename ToFileIOContextRawPtrMap<T>::iterator;

// Interface to Ref<T>.
void doRef(RefBadge badge, FileIOContext& entry);

void doUnref(RefBadge badge, FileIOContext& entry);


} // fuse
} // mega

