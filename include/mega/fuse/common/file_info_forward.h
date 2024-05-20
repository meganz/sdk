#pragma once

#include <map>
#include <memory>

#include <mega/fuse/common/ref_forward.h>

namespace mega
{
namespace fuse
{

class FileInfo;

using FileInfoPtr = std::unique_ptr<FileInfo>;
using FileInfoRef = Ref<FileInfo>;

template<typename T>
using ToFileInfoPtrMap = std::map<T, FileInfoPtr>;

// Interface to Ref<T>.
void doRef(RefBadge badge, FileInfo& info);

void doUnref(RefBadge badge, FileInfo& info);

} // fuse
} // mega

