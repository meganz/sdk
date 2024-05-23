#pragma once

#include <functional>
#include <utility>

#include <mega/fuse/common/bind_handle_forward.h>
#include <mega/fuse/common/error_or_forward.h>
#include <mega/fuse/common/node_info_forward.h>
#include <mega/fuse/common/upload_forward.h>

#include <mega/types.h>

namespace mega
{
namespace fuse
{

using BoundCallback =
  std::function<void(ErrorOr<NodeHandle>)>;

using BindCallback =
  std::function<void(BoundCallback, NodeHandle)>;

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

using UploadResult =
  std::pair<BindCallback, BindHandle>;

using UploadCallback =
  std::function<void(ErrorOr<UploadResult>)>;

} // fuse
} // mega

