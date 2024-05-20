#pragma once

#include <memory>

namespace mega
{
namespace fuse
{

class Task;
class TaskContext;
class TaskQueue;

using TaskContextPtr = std::shared_ptr<TaskContext>;

} // fuse
} // mega

