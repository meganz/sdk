#pragma once

#include <mega/common/error_or_forward.h>
#include <mega/types.h>

#include <functional>
#include <optional>
#include <string>

namespace mega
{
namespace common
{

using BoundCallback =
  std::function<void(ErrorOr<NodeHandle>)>;

struct BindContext
{
    // Who should we call when the file's been bound?
    BoundCallback mCallback;

    // What should the file's modification time be?
    std::optional<std::int64_t> mModified;

    // What should the file be named?
    std::optional<std::string> mName;

    // Where should the file be uploaded?
    std::optional<NodeHandle> mParentHandle;

    // What was the handle of the file's previous version?
    NodeHandle mPreviousHandle;
}; // BindContext

using BindCallback = std::function<void(BindContext)>;

using UploadResult = BindCallback;

using UploadCallback =
  std::function<void(ErrorOr<UploadResult>)>;

} // common
} // mega

