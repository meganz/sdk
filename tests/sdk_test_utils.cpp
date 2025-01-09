#include "sdk_test_utils.h"

#include "mega/logging.h"

#include <fstream>
#include <string_view>
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

void createFile(const fs::path& filePath, const unsigned int fileSizeBytes)
{
    std::ofstream outFile(filePath, std::ios::binary);
    if (!outFile.is_open())
    {
        const auto msg = "Can't open the file: " + filePath.string();
        LOG_err << msg;
        throw std::runtime_error(msg);
    }
    std::vector<char> buffer(fileSizeBytes, 0);
    outFile.write(buffer.data(), static_cast<std::streamsize>(buffer.size()));
}

void createFile(const fs::path& filePath, const std::string_view contents)
{
    std::ofstream outFile(filePath, std::ios::binary);
    if (!outFile.is_open())
    {
        const auto msg = "Can't open the file: " + filePath.string();
        LOG_err << msg;
        throw std::runtime_error(msg);
    }
    outFile.write(contents.data(), static_cast<std::streamsize>(contents.size()));
}

LocalTempFile::LocalTempFile(const fs::path& _filePath, const unsigned int fileSizeBytes):
    mFilePath(_filePath)
{
    createFile(mFilePath, fileSizeBytes);
}

LocalTempFile::LocalTempFile(const fs::path& _filePath, const std::string_view contents):
    mFilePath(_filePath)
{
    createFile(mFilePath, contents);
}

LocalTempFile::~LocalTempFile()
{
    fs::remove(mFilePath);
}

FileNodeInfo::FileNodeInfo(const std::string& _name,
                           const std::optional<unsigned int>& _label,
                           const bool _fav,
                           const unsigned int _size,
                           const std::chrono::seconds _secondsSinceMod,
                           const bool _sensitive,
                           const std::string& _description,
                           const std::set<std::string>& _tags):
    NodeCommonInfo{_name, _label, _fav, _sensitive, _description, _tags},
    size(_size)
{
    using std::chrono::system_clock;
    static const int64_t refTime = system_clock::to_time_t(system_clock::now());
    if (auto nSecs = _secondsSinceMod.count(); nSecs != 0)
    {
        mtime = refTime - nSecs;
    }
}

LocalTempDir::LocalTempDir(const fs::path& _dirPath):
    mDirPath(_dirPath)
{
    if (fs::exists(mDirPath))
    {
        // We avoid creation in this case to avoid unintentional removals
        const auto msg = "Directory already exists: " + _dirPath.string();
        LOG_err << msg;
        throw std::runtime_error(msg);
    }
    fs::create_directories(mDirPath);
}

LocalTempDir::~LocalTempDir()
{
    try
    {
        if (fs::exists(mDirPath))
        {
            fs::remove_all(mDirPath);
        }
    }
    catch (const std::exception& e)
    {
        LOG_err << "Error removing directory: " << mDirPath.string() << ". Error: " << e.what();
    }
}

bool LocalTempDir::move(const fs::path& newLocation)
{
    if (std::filesystem::exists(newLocation))
    {
        LOG_err
            << "Moving " << mDirPath.string() << " to " << newLocation.string()
            << " will overwrite the target path. Romove it before proceeding with the operation.";
        return false;
    }
    try
    {
        std::filesystem::rename(mDirPath, newLocation);
    }
    catch (const std::exception& e)
    {
        LOG_err << "Error moving directory from " << mDirPath.string() << " to "
                << newLocation.string() << ". Error: " << e.what();
        return false;
    }
    mDirPath = newLocation;
    return true;
}

namespace
{

// Needed forward declaration for recursive call
void processDirChildName(const DirNodeInfo&, std::vector<std::string>&);

/**
 * @brief Put the name of the node and their children (if any) in the given vector
 */
void processNodeName(const NodeInfo& node, std::vector<std::string>& names)
{
    std::visit(
        [&names](const auto& arg)
        {
            names.push_back(arg.name);
            if constexpr (std::is_same_v<decltype(arg), const DirNodeInfo&>)
            {
                processDirChildName(arg, names);
            }
        },
        node);
}

/**
 * @brief Aux function to call inside the visitor. Puts the names of the children in names.
 */
void processDirChildName(const DirNodeInfo& dir, std::vector<std::string>& names)
{
    std::for_each(dir.childs.begin(),
                  dir.childs.end(),
                  [&names](const auto& child)
                  {
                      processNodeName(child, names);
                  });
}

}

std::vector<std::string> DirNodeInfo::getChildrenNames() const
{
    std::vector<std::string> result;
    result.reserve(childs.size());
    std::transform(std::begin(childs),
                   std::end(childs),
                   std::back_inserter(result),
                   [](const auto& child) -> std::string
                   {
                       return getNodeName(child);
                   });
    return result;
}

std::vector<std::string> getNodeNames(const NodeInfo& node)
{
    std::vector<std::string> result;
    processNodeName(node, result);
    return result;
}

std::string getNodeName(const NodeInfo& node)
{
    return std::visit(
        [](const auto& n) -> std::string
        {
            return n.name;
        },
        node);
}

std::vector<std::string>
    getLocalFirstChildrenNames_if(const std::filesystem::path& localPath,
                                  std::function<bool(const std::string&)> filter)
{
    if (!std::filesystem::is_directory(localPath))
        return {};
    std::vector<std::string> result;
    const auto pushName = [&result, &filter](const std::filesystem::path& path)
    {
        const auto name = path.filename().string();
        if (!filter || filter(name))
            result.emplace_back(std::move(name));
    };
    std::filesystem::directory_iterator children{localPath};
    std::for_each(begin(children), end(children), pushName);
    return result;
}
}
