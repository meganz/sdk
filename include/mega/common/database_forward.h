#pragma once

#include <mutex>

namespace mega
{
namespace common
{

class Database;

using DatabaseLock = std::unique_lock<const Database>;

} // common
} // mega

