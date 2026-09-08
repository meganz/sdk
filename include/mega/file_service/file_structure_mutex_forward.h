#pragma once

#include <mega/common/async_mutex_forward.h>
#include <mega/common/concurrent_execution_policy_forward.h>
#include <mega/common/scoped_async_lock_forward.h>

namespace mega
{
namespace file_service
{

using FileStructureMutex = common::AsyncMutex<common::ConcurrentExecutionPolicy>;
using FileStructureSharedLock = common::ScopedAsyncSharedLock<FileStructureMutex>;
using FileStructureUniqueLock = common::ScopedAsyncUniqueLock<FileStructureMutex>;

} // file_service
} // mega
