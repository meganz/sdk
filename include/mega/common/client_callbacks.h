#pragma once

#include <functional>
#include <utility>

#include <mega/common/error_or_forward.h>
#include <mega/common/node_info_forward.h>
#include <mega/common/upload_forward.h>

#include <mega/types.h>

namespace mega
{
namespace common
{

using DownloadCallback =
  std::function<void(Error)>;

using MakeDirectoryCallback =
  std::function<void(ErrorOr<NodeInfo>)>;

using MoveCallback =
  std::function<void(Error)>;

using RemoveCallback =
  std::function<void(Error)>;

using RenameCallback =
  std::function<void(Error)>;

using ReplaceCallback =
  std::function<void(Error)>;

using StorageInfoCallback =
  std::function<void(ErrorOr<StorageInfo>)>;

using TouchCallback =
  std::function<void(Error)>;

} // common
} // mega

