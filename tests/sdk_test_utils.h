#ifndef INCLUDE_TESTS_SDK_TEST_UTILS_H_
#define INCLUDE_TESTS_SDK_TEST_UTILS_H_

#include "stdfs.h"

#include <fstream>
#include <functional>
#include <optional>
#include <set>
#include <thread>
#include <type_traits>
#include <variant>
#include <vector>

namespace sdk_test
{

/**
 * @brief Opens a file with the given mode, and writes either "size" zeros or the given "contents".
 *
 * @tparam T A type that indicates the data: either std::size_t or std::string_view.
 * @param filePath Path to open
 * @param openMode e.g. std::ios::binary or std::ios::binary | std::ios::app
 * @param data The data to write. If T = std::size_t, we write that many zero bytes.
 *             If T = std::string_view or std::string, we write those bytes.
 */
template<typename T>
void writeFileContent(const fs::path& filePath, const std::ios::openmode openMode, const T& data)
{
    std::ofstream outFile(filePath, openMode);
    if (!outFile.is_open())
    {
        const auto msg = "Cannot open file: " + filePath.string();
        throw std::runtime_error(msg);
    }

    if constexpr (std::is_convertible_v<T, std::size_t>)
    {
        // data is a byte count -> write that many zeros
        const std::vector<char> buffer(data, 0);
        outFile.write(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    }
    else if constexpr (std::is_same_v<T, std::string_view> || std::is_same_v<T, std::string>)
    {
        // data is a string -> write these contents
        outFile.write(data.data(), static_cast<std::streamsize>(data.size()));
    }
    else
    {
        static_assert(!sizeof(T*), "writeFileContent used with invalid type");
    }
}

/**
 * @brief Returns the path to the folder containing resources for the tests.
 *
 * IMPORTANT: `setTestDataDir` must be called before.
 */
fs::path getTestDataDir();

/**
 * @brief Sets the path to the folder where the test resources are located. Usually called from the main function.
 * It should be set before calling getTestDataDir or copyFileFromTestData
 *
 * Example:
 *    setTestDataDir(fs::absolute(fs::path(argv[0]).parent_path()));
 *
 */
void setTestDataDir(const fs::path& dataDir);

/**
 * @brief Copies file from the resources data directory to the given destination (current working directory by default).
 * IMPORTANT: `setTestDataDir` must be called before.
 */
void copyFileFromTestData(fs::path filename, fs::path destination = ".");

/**
 * @brief Creates a file of a given size. It throws if the file cannot be opened
 */
void createFile(const fs::path& filePath, const size_t fileSizeBytes);

/**
 * @brief Creates a file with the given contents. It throws if the file cannot be opened
 */
void createFile(const fs::path& filePath, const std::string_view contents);

/**
 * @brief Appends data to a file with the given contents. It throws if the file cannot be opened
 */
void appendToFile(const fs::path& filePath, const size_t bytesToAppend);

/**
 * @brief Appends data to a file with the given contents. It throws if the file cannot be opened
 */
void appendToFile(const fs::path& filePath, const std::string_view contentsToAppend);

/**
 * @class LocalTempFile
 * @brief Helper class to apply RAII when creating a file locally
 */
class LocalTempFile
{
public:
    LocalTempFile(const fs::path& _filePath, const size_t fileSizeBytes);
    LocalTempFile(const fs::path& _filePath, const std::string_view contents);
    ~LocalTempFile();

    // Delete copy constructors -> Don't allow many objects to remove the same file
    LocalTempFile(const LocalTempFile&) = delete;
    LocalTempFile& operator=(const LocalTempFile&) = delete;

    // Allow move operations
    LocalTempFile(LocalTempFile&&) noexcept = default;
    LocalTempFile& operator=(LocalTempFile&&) noexcept = default;

    const fs::path& getPath() const
    {
        return mFilePath;
    }

    /*
     * @brief Appends bytesToAppend of data to the file.
     * @see appendToFile()
     */
    void appendData(const size_t bytesToAppend) const;
    /*
     * @brief Appends contents to the file.
     * @see appendToFile()
     */
    void appendData(const std::string_view contentsToAppend) const;

private:
    fs::path mFilePath;
};

/**
 * @class LocalTempDir
 * @brief Helper class to apply RAII when creating a directory locally
 */
class LocalTempDir
{
public:
    LocalTempDir(const fs::path& _dirPath);
    ~LocalTempDir();

    // Delete copy constructors -> Don't allow many objects to remove the same dir
    LocalTempDir(const LocalTempDir&) = delete;
    LocalTempDir& operator=(const LocalTempDir&) = delete;

    // Allow move operations
    LocalTempDir(LocalTempDir&&) noexcept = default;
    LocalTempDir& operator=(LocalTempDir&&) noexcept = default;

    const fs::path& getPath() const
    {
        return mDirPath;
    }

    /**
     * @brief Move the current temp dir to the given location
     *
     * @return true if the operation succeeded, false there was an error or if there is already a
     * file/directory in the given newLocation.
     */
    bool move(const fs::path& newLocation);

private:
    fs::path mDirPath;
};

/**
 * @class NodeCommonInfo
 * @brief Common information shared both by files and directories
 *
 * This struct and the inherited ones implement the builder pattern so it is easy to create custom
 * nodes with the required fields.
 */
template<typename Derived>
struct NodeCommonInfo
{
    std::string name;
    std::optional<unsigned int> label = std::nullopt; // e.g. MegaNode::NODE_LBL_PURPLE
    bool fav = false;
    bool sensitive = false;
    std::string description;
    std::set<std::string> tags;

    Derived& setName(const std::string& _name)
    {
        name = _name;
        return static_cast<Derived&>(*this);
    }

    Derived& setLabel(const std::optional<unsigned int>& _label)
    {
        label = _label;
        return static_cast<Derived&>(*this);
    }

    Derived& setFav(const bool _fav)
    {
        fav = _fav;
        return static_cast<Derived&>(*this);
    }

    Derived& setSensitive(const bool _sensitive)
    {
        sensitive = _sensitive;
        return static_cast<Derived&>(*this);
    }

    Derived& setDescription(const std::string& _description)
    {
        description = _description;
        return static_cast<Derived&>(*this);
    }

    Derived& setTags(const std::set<std::string>& _tags)
    {
        tags = _tags;
        return static_cast<Derived&>(*this);
    }

    Derived& addTag(const std::string& _tag)
    {
        tags.insert(_tag);
        return static_cast<Derived&>(*this);
    }
};

/**
 * @class FileNodeInfo
 * @brief Struct holding all the relevant information a file node can have
 */
struct FileNodeInfo: public NodeCommonInfo<FileNodeInfo>
{
    // Same value as in megaapi.h
    static constexpr int64_t INVALID_CUSTOM_MOD_TIME = -1;

    unsigned int size = 0;
    int64_t mtime = INVALID_CUSTOM_MOD_TIME;

    FileNodeInfo(const std::string& _name,
                 const std::optional<unsigned int>& _label = std::nullopt,
                 const bool _fav = false,
                 const unsigned int _size = 0,
                 const std::chrono::seconds _secondsSinceMod = {},
                 const bool _sensitive = false,
                 const std::string& _description = {},
                 const std::set<std::string>& _tags = {});

    FileNodeInfo& setSize(const unsigned int _size)
    {
        size = _size;
        return *this;
    }

    FileNodeInfo& setMtime(const std::chrono::seconds _secondsSinceMod)
    {
        mtime = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now() -
                                                     _secondsSinceMod);
        return *this;
    }

    FileNodeInfo& setMtime(const std::chrono::system_clock::time_point _timePoint)
    {
        mtime = std::chrono::system_clock::to_time_t(_timePoint);
        return *this;
    }
};

struct DirNodeInfo;

using NodeInfo = std::variant<FileNodeInfo, DirNodeInfo>;

/**
 * @class DirNodeInfo
 * @brief Struct holding all the relevant information a directory node can have
 */
struct DirNodeInfo: public NodeCommonInfo<DirNodeInfo>
{
    std::vector<NodeInfo> childs{};

    DirNodeInfo(const std::string& _name,
                const std::vector<NodeInfo>& _childs = {},
                const std::optional<unsigned int>& _label = std::nullopt,
                const bool _fav = false,
                const bool _sensitive = false,
                const std::string& _description = {},
                const std::set<std::string>& _tags = {}):
        NodeCommonInfo{_name, _label, _fav, _sensitive, _description, _tags},
        childs(_childs)
    {}

    DirNodeInfo& addChild(const NodeInfo& child)
    {
        childs.emplace_back(child);
        return *this;
    }

    /**
     * @brief Returns a vector with the names of the firs successors of the directory
     */
    std::vector<std::string> getChildrenNames() const;
};

/**
 * @brief Returns the names in the tree specified by node
 *
 * @note The tree is iterated using a depth-first approach
 */
std::vector<std::string> getNodeNames(const NodeInfo& node);

/**
 * @brief Get the name of the give node
 */
std::string getNodeName(const NodeInfo& node);

/**
 * @brief Waits for a condition to become true or until a timeout occurs.
 *
 * This function repeatedly checks a predicate at intervals (controlled by sleepDuration)
 * and stops when the predicate returns true or when the specified timeout has been reached.
 *
 * @tparam Duration The type of the timeout duration (e.g., std::chrono::milliseconds,
 * std::chrono::seconds).
 * @tparam SleepDuration The type of the sleep interval between predicate checks (default is
 * std::chrono::milliseconds).
 * @param predicate The condition to be evaluated. It should be a callable returning a bool.
 * @param timeout The maximum duration to wait for the predicate to return true.
 * @param sleepDuration The interval between each check of the predicate. Defaults to 100
 * milliseconds.
 *
 * @return true if the predicate returns true within the timeout period, otherwise false.
 *
 * @note The predicate will be evaluated at least once, and then at intervals of `sleepDuration`.
 *
 * @example
 * using namespace std::chrono_literals
 * bool conditionMet = waitFor(
 *     [] { return some_condition(); },
 *     5s,
 *     200ms
 * );
 */
template<typename Duration, typename SleepDuration = std::chrono::milliseconds>
bool waitFor(const std::function<bool()>& predicate,
             Duration timeout,
             SleepDuration sleepDuration = std::chrono::milliseconds(100))
{
    auto startTime = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - startTime < timeout)
    {
        if (predicate())
            return true;
        std::this_thread::sleep_for(sleepDuration);
    }
    return false;
}

/**
 * @brief Get the names of the files/directories that are contained within the given path.
 *
 * Note: if the path does not point to a directory, an empty vector is returned
 *
 * @param localPath The path to evaluate their children
 * @param filter Required named-based condition to be included in the results.
 * @return A vector with the names of the children
 */
std::vector<std::string>
    getLocalFirstChildrenNames_if(const std::filesystem::path& localPath,
                                  std::function<bool(const std::string&)> filter = nullptr);
}

#endif // INCLUDE_TESTS_SDK_TEST_UTILS_H_
