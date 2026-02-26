#pragma once

#include <memory>

namespace mega
{
namespace common
{

class Task;
class TaskContext;
class TaskQueue;

using TaskContextPtr = std::shared_ptr<TaskContext>;

} // common
} // mega

