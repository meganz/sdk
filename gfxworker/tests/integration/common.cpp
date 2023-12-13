#include "common.h"

#include <filesystem>

namespace mega_test
{

namespace fs = std::filesystem;

std::string ExecutableDir::mDir;

void ExecutableDir::init(const std::string& executable)
{
    auto executableDir = fs::absolute(fs::path(executable).parent_path());
    ExecutableDir::mDir = executableDir.string();
}

std::string ExecutableDir::get()
{
    return ExecutableDir::mDir;
}

}
