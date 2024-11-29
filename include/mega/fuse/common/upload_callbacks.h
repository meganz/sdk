#pragma once

#include <functional>

#include <mega/fuse/common/bind_handle_forward.h>
#include <mega/fuse/common/error_or_forward.h>

#include <mega/types.h>

namespace mega
{
namespace fuse
{

using BoundCallback =
  std::function<void(ErrorOr<NodeHandle>)>;

using BindCallback =
  std::function<void(BoundCallback, NodeHandle)>;

using UploadResult =
  std::pair<BindCallback, BindHandle>;

using UploadCallback =
  std::function<void(ErrorOr<UploadResult>)>;

} // fuse
} // mega

