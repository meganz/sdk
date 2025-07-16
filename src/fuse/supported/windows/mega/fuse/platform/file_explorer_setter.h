#pragma once
#include <memory>
#include <string>

namespace mega
{
namespace fuse
{
namespace platform
{

// Sets running file explorer's view
class FEViewSetter
{
    class Executor;

    std::unique_ptr<Executor> mExecutor;

public:
    FEViewSetter();

    ~FEViewSetter();

    void notify(const std::wstring& prefix);
};

} // platform
} // fuse
} // mega
