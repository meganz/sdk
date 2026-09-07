#pragma once

#include <mega/file_service/file_read_result.h>
#include <mega/file_service/file_result_or_forward.h>
#include <mega/file_service/file_stream_result_forward.h>
#include <mega/file_service/file_truncate_result_forward.h>
#include <mega/file_service/file_write_result.h>

#include <cstdint>
#include <functional>

namespace mega
{
namespace file_service
{

using FileAppendCallback = std::function<void(FileResultOr<FileWriteResult>)>;
using FileFetchBarrierCallback = std::function<void()>;
using FileFetchCallback = std::function<void(FileResult)>;
using FileFlushCallback = std::function<void(FileResult)>;
using FilePurgeCallback = std::function<void(FileResult)>;
using FileReadCallback = std::function<void(FileResultOr<FileReadResult>)>;
using FileReclaimCallback = std::function<void(FileResultOr<std::uint64_t>)>;
using FileRemoveCallback = std::function<void(FileResult)>;
using FileStreamContinueCallback = std::function<void(std::uint64_t)>;
using FileStreamDataCallback = std::function<void(FileResultOr<FileStreamResult>)>;
using FileStreamFDCallback = std::function<void(FileResultOr<FileStreamFDResult>)>;
using FileTouchCallback = std::function<void(FileResult)>;
using FileTruncateCallback = std::function<void(FileResultOr<FileTruncateResult>)>;
using FileWriteCallback = std::function<void(FileResultOr<FileWriteResult>)>;

} // file_service
} // mega
