#pragma once

#include <mutex>

namespace mega
{
namespace fuse
{
namespace detail
{

class Database;

using DatabaseLock = std::unique_lock<const Database>;

} // detail

using detail::Database;
using detail::DatabaseLock;

} // fuse
} // mega

