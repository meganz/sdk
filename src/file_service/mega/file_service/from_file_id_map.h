#pragma once

#include <mega/file_service/file_id_forward.h>

#include <map>

namespace mega
{
namespace file_service
{

template<typename T>
using FromFileIDMap = std::map<FileID, T>;

} // file_service
} // mega
