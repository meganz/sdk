#pragma once

#include <mega/file_service/file_append_request_forward.h>
#include <mega/file_service/file_fetch_request_forward.h>
#include <mega/file_service/file_flush_request_forward.h>
#include <mega/file_service/file_read_request_forward.h>
#include <mega/file_service/file_reclaim_request_forward.h>
#include <mega/file_service/file_touch_request_forward.h>
#include <mega/file_service/file_truncate_request_forward.h>
#include <mega/file_service/file_write_request_forward.h>

#include <variant>

namespace mega
{
namespace file_service
{

using FileRequest = std::variant<FileAppendRequest,
                                 FileFetchRequest,
                                 FileFlushRequest,
                                 FileReadRequest,
                                 FileReclaimRequest,
                                 FileTouchRequest,
                                 FileTruncateRequest,
                                 FileWriteRequest>;

} // file_service
} // mega
