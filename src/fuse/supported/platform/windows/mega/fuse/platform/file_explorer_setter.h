#pragma once
#include <mega/fuse/platform/shell.h>

#include <atomic>
#include <functional>
#include <memory>

namespace mega
{
namespace fuse
{
namespace platform
{

// Sets running file explorer's view
class FileExplorerSetter
{
    class Executor;

    std::unique_ptr<Executor> mExecutor;

public:
    FileExplorerSetter();

    ~FileExplorerSetter();

    void notify(std::function<shell::Prefixes()> getPrefixes);
};

} // platform
} // fuse
} // mega
