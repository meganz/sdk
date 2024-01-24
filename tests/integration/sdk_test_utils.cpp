#include "sdk_test_utils.h"


namespace sdk_test
{

static fs::path executableDir;

fs::path getTestDataDir()
{
    return executableDir;
}

void setTestDataDir(const fs::path& dataDir)
{
    executableDir = dataDir;
}

void copyFileFromTestData(fs::path filename, fs::path destination)
{
    fs::path source = getTestDataDir() / filename;
    if (fs::is_directory(destination))
    {
        destination = destination / filename;
    }
    if (fs::equivalent(source, destination))
    {
        return;
    }
    if (fs::exists(destination))
    {
        fs::remove(destination);
    }
    fs::copy_file(source, destination);
}

}
