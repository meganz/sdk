#ifndef INCLUDE_TESTS_SDK_TEST_UTILS_H_
#define INCLUDE_TESTS_SDK_TEST_UTILS_H_

#include "stdfs.h"

#include <optional>
#include <set>
#include <variant>
#include <vector>

namespace sdk_test
{

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
 * @class LocalTempFile
 * @brief Helper class to apply RAII when creating a file locally
 */
class LocalTempFile
{
public:
    LocalTempFile(const fs::path& _filePath, const unsigned int fileSizeBytes);
    LocalTempFile(const fs::path& _filePath, const std::string_view contents);
    ~LocalTempFile();

    // Delete copy constructors -> Don't allow many objects to remove the same file
    LocalTempFile(const LocalTempFile&) = delete;
    LocalTempFile& operator=(const LocalTempFile&) = delete;

    // Allow move operations
    LocalTempFile(LocalTempFile&&) noexcept = default;
    LocalTempFile& operator=(LocalTempFile&&) noexcept = default;

private:
    fs::path mFilePath;
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
};
}

#endif // INCLUDE_TESTS_SDK_TEST_UTILS_H_
