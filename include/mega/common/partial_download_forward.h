#pragma once

#include <memory>

namespace mega
{
namespace common
{

class PartialDownload;

using PartialDownloadPtr = std::shared_ptr<PartialDownload>;
using PartialDownloadWeakPtr = std::weak_ptr<PartialDownload>;

} // common
} // mega
