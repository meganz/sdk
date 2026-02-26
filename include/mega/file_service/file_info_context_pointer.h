#include <mega/file_service/file_info_context_forward.h>

#include <memory>

namespace mega
{
namespace file_service
{

using FileInfoContextPtr = std::shared_ptr<FileInfoContext>;
using FileInfoContextWeakPtr = std::weak_ptr<FileInfoContext>;

} // file_service
} // mega
