#include "sdk_test_utils.h"

#include "mega/logging.h"

#include <fstream>
#include <vector>

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
    if (fs::exists(destination))
    {
        if (fs::equivalent(source, destination))
        {
            return;
        }
        fs::remove(destination);
    }
    fs::copy_file(source, destination);
}

LocalTempFile::LocalTempFile(const fs::path& _filePath, const unsigned int fileSizeBytes):
    mFilePath(_filePath)
{
    std::ofstream outFile(mFilePath, std::ios::binary);
    if (!outFile.is_open())
    {
        const auto msg = "Can't open the file: " + _filePath.string();
        LOG_err << msg;
        throw std::runtime_error(msg);
    }
    std::vector<char> buffer(fileSizeBytes, 0);
    outFile.write(buffer.data(), static_cast<std::streamsize>(buffer.size()));
}

LocalTempFile::~LocalTempFile()
{
    fs::remove(mFilePath);
}
}
