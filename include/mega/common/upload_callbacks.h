#pragma once

#include <functional>

#include <mega/common/bind_handle_forward.h>
#include <mega/common/error_or_forward.h>

#include <mega/types.h>

namespace mega
{
namespace common
{

using BoundCallback =
  std::function<void(ErrorOr<NodeHandle>)>;

using BindCallback =
  std::function<void(BoundCallback, NodeHandle)>;

using UploadResult =
  std::pair<BindCallback, BindHandle>;

using UploadCallback =
  std::function<void(ErrorOr<UploadResult>)>;

} // common
} // mega

