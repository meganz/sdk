#include <cassert>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <stdexcept>

#include <mega/fuse/common/error_or.h>
#include <mega/fuse/common/node_info.h>
#include <mega/fuse/common/testing/client.h>
#include <mega/fuse/common/testing/cloud_path.h>
#include <mega/fuse/common/testing/model.h>
#include <mega/fuse/common/testing/path.h>
#include <mega/fuse/common/testing/utility.h>
#include <mega/fuse/platform/date_time.h>

#include <mega/logging.h>

namespace mega
{
namespace fuse
{
namespace testing
{

static Model::DirectoryNodePtr generate(const std::string& prefix,
                                        std::size_t height,
                                        std::size_t numDirectories,
                                        std::size_t numFiles);

static bool mismatch(const std::string& path, const std::string& type);

static const std::string MISMATCH_EXIST_LEFT  = "E<";
static const std::string MISMATCH_EXIST_RIGHT = ">E";
static const std::string MISMATCH_CONTENT     = "CN";
static const std::string MISMATCH_MODIFIED    = "MT";
static const std::string MISMATCH_SIZE        = "SZ";
static const std::string MISMATCH_TYPE        = "TY";

Model::Node::Node(std::string name)
  : mModified()
  , mName(std::move(name))
{
}

void Model::Node::swap(Node& other)
{
    using std::swap;

    swap(mModified, other.mModified);
    swap(mName, other.mName);
}

auto Model::Node::directory() const -> const DirectoryNode*
{
    return const_cast<Node*>(this)->directory();
}

auto Model::Node::directory() -> DirectoryNode*
{
    return nullptr;
}

auto Model::Node::file() const -> const FileNode*
{
    return const_cast<Node*>(this)->file();
}

auto Model::Node::file() -> FileNode*
{
    return nullptr;
}

Model::DirectoryNode::DirectoryNode(std::string name)
  : Node(std::move(name))
  , mChildren()
{
}

Model::DirectoryNode::DirectoryNode(const DirectoryNode& other)
  : Node(other)
  , mChildren()
{
    // Copy other's children.
    for (auto& c : other.mChildren)
        add(c.second->copy());
}

Model::DirectoryNode::DirectoryNode(DirectoryNode&& other)
  : Node(std::move(other))
  , mChildren(std::move(other.mChildren))
{
}

auto Model::DirectoryNode::operator=(DirectoryNode&& rhs) -> DirectoryNode&
{
    DirectoryNode temp(std::move(rhs));

    swap(temp);

    return *this;
}

auto Model::DirectoryNode::add(NodePtr child) -> Node*
{
    // Sanity.
    assert(child);
    assert(!child->mName.empty());

    // Safety.
    auto name = child->mName;

    // Try and add the child to our map.
    auto result = mChildren.emplace(std::move(name), std::move(child));

    // Child's been added.
    if (result.second)
        return result.first->second.get();

    // Child already exists with this name.
    return nullptr;
}

auto Model::DirectoryNode::children() const -> const NodeMap&
{
    return mChildren;
}

auto Model::DirectoryNode::copy() -> NodePtr
{
    return std::make_unique<DirectoryNode>(*this);
}

auto Model::DirectoryNode::directory() -> DirectoryNode*
{
    return this;
}

auto Model::DirectoryNode::from(const Client& client, NodeInfo info) -> NodePtr
{
    // Instantiate a new directory.
    auto directory = std::make_unique<DirectoryNode>(std::move(info.mName));

    // Determine the names of this node's children.
    auto childNames = client.childNames(CloudPath(info.mHandle));

    // Populate directory from the cloud.
    for (const auto& name : childNames)
    {
        // Retrieve information about this child.
        auto info_ = client.get(info.mHandle, name);

        // Child doesn't exist.
        if (!info_)
            continue;

        if (info_->mIsDirectory)
            directory->add(DirectoryNode::from(client, std::move(*info_)));
        else
            directory->add(FileNode::from(client, std::move(*info_)));
    }

    // Return directory to caller.
    return directory;
}

auto Model::DirectoryNode::from(const fs::path& path) -> NodePtr
{
    // What's the name of this directory?
    auto name = path.filename().u8string();

    // Instantiate a new directory.
    auto directory = std::make_unique<DirectoryNode>(std::move(name));

    auto i = fs::directory_iterator(path);
    auto j = fs::directory_iterator();

    // Populate directory from disk.
    for ( ; i != j; ++i)
    {
        switch (i->status().type())
        {
        case fs::file_type::directory:
            directory->add(DirectoryNode::from(i->path()));
            break;
        case fs::file_type::regular:
            directory->add(FileNode::from(i->path()));
        default:
            break;
        }
    }

    return directory;
}

auto Model::DirectoryNode::get(const std::string& name) const -> const Node*
{
    return const_cast<DirectoryNode*>(this)->get(name);
}

auto Model::DirectoryNode::get(const std::string& name) -> Node*
{
    // Try and locate the specified child.
    auto i = mChildren.find(name);

    // Child exists.
    if (i != mChildren.end())
        return i->second.get();

    // Child doesn't exist.
    return nullptr;
}

bool Model::DirectoryNode::match(const std::string& path, const Node& rhs) const
{
    // Is rhs a directory?
    auto rhsDirectory = rhs.directory();

    // Rhs isn't a directory.
    if (!rhsDirectory)
        return mismatch(path, MISMATCH_TYPE);

    std::set<std::string> childNames;

    // Retrieve names of children.
    for (auto& child : mChildren)
        childNames.emplace(child.first);

    for (auto& child : rhsDirectory->mChildren)
        childNames.emplace(child.first);

    // Assume our children are matched.
    auto matched = true;

    // Try and match children.
    for (auto& child : mChildren)
    {
        // Compute child path.
        auto childPath = path + child.first + "/";

        // Mark child as having been visited.
        childNames.erase(child.first);

        // Check if child exists on the right.
        auto rhsChild = rhsDirectory->get(child.first);

        // Child only exists on the left.
        if (!rhsChild)
        {
            // Emit mismatch message.
            matched &= mismatch(childPath, MISMATCH_EXIST_LEFT);

            // Try and match next child.
            continue;
        }

        // Try and match child.
        matched &= child.second->match(childPath, *rhsChild);
    }

    // All children have been processed.
    if (childNames.empty())
        return matched;

    // Emit a mismatch message for children that exist only on the right.
    for (auto& name : childNames)
        mismatch(path + name + "/", MISMATCH_EXIST_RIGHT);

    return false;
}

void Model::DirectoryNode::populate(fs::path path) const
{
    // Add our name to the path.
    path /= fs::u8path(mName);

    // Make sure we exist on disk.
    fs::create_directories(path);

    // Populate the new directory with our children.
    for (auto& c : mChildren)
        c.second->populate(path);
}

auto Model::DirectoryNode::remove(const std::string& name) -> NodePtr
{
    // Try and locate the specified child.
    auto i = mChildren.find(name);

    // Child doesn't exist.
    if (i == mChildren.end())
        return nullptr;

    // Extract the child.
    auto child = std::move(i->second);

    // Remove the child from our map.
    mChildren.erase(i);

    // Return child to caller.
    return child;
}

void Model::DirectoryNode::swap(DirectoryNode& other)
{
    using std::swap;

    Node::swap(other);

    swap(mChildren, other.mChildren);
}

Model::FileNode::FileNode(std::string name)
  : Node(std::move(name))
  , mContent()
  , mSize(0)
{
}

auto Model::FileNode::copy() -> NodePtr
{
    return std::make_unique<FileNode>(*this);
}

auto Model::FileNode::file() -> FileNode*
{
    return this;
}

auto Model::FileNode::from(const Client& client, NodeInfo info) -> NodePtr
{
    // Instantiate file.
    auto file = std::make_unique<FileNode>(std::move(info.mName));

    // Convenience.
    using std::chrono::system_clock;

    // Latch modification time.
    file->mModified = system_clock::from_time_t(info.mModified);

    // Latch size.
    file->mSize = static_cast<std::uintmax_t>(info.mSize);

    // Return file to caller.
    return file;
}

auto Model::FileNode::from(const fs::path& path) -> NodePtr
{
    // What is our name?
    auto name = path.filename().u8string();

    // Instantiate file.
    auto file = std::make_unique<FileNode>(std::move(name));

    // Latch modification time.
    file->mModified = lastWriteTime(path);

    // Determine the file's size.
    file->mSize = fs::file_size(path);

    // File's empty.
    if (!file->mSize)
        return file;

    // Try and open file for reading.
    std::ifstream istream(path.u8string(), std::ios::binary);

    // Expand buffer.
    file->mContent.resize(file->mSize);

    // Convenience.
    auto size_ = static_cast<std::streamsize>(file->mSize);

    // Try and read content from disk.
    istream.read(&file->mContent[0], size_);

    // File's been read from disk.
    if (istream.gcount() == size_)
        return file;

    // Couldn't read the file from disk.
    std::ostringstream ostream;

    ostream << "Couldn't read \""
            << path
            << "\" from disk";

    throw std::runtime_error(ostream.str());
}


bool Model::FileNode::match(const std::string& path, const Node& rhs) const
{
    // Is rhs a file?
    auto rhsFile = rhs.file();

    // Rhs isn't a file.
    if (!rhsFile)
        return mismatch(path, MISMATCH_TYPE);

    // Modification time is different.
    if (mModified != rhsFile->mModified)
        return mismatch(path, MISMATCH_MODIFIED);

    // Size is different.
    if (mSize != rhsFile->mSize)
        return mismatch(path, MISMATCH_SIZE);

    // Only compare content if available on both sides.
    if (mContent.empty() || rhsFile->mContent.empty())
        return true;

    // Content is different.
    if (mContent != rhsFile->mContent)
        return mismatch(path, MISMATCH_CONTENT);

    // File's matched.
    return true;
}

void Model::FileNode::populate(fs::path path) const
{
    // Compute our path.
    path /= fs::u8path(mName);

    // Convenience.
    constexpr auto flags =
      std::ios::binary | std::ios::trunc;

    constexpr auto mask =
      std::ios::badbit | std::ios::failbit;

    std::ofstream ostream;

    // Make sure we throw if we can't create the file.
    ostream.exceptions(mask);

    // Try and open the file.
    ostream.open(path.u8string(), flags);

    // Try and write our content to disk.
    ostream.write(mContent.data(),
                  static_cast<std::streamsize>(mContent.size()));

    // Make sure our content has been written.
    ostream.flush();

    // Close the file.
    ostream.close();

    // Set the file's modification time.
    lastWriteTime(path, mModified);
}

Model::Model()
  : mRoot("")
{
}

Model::Model(const Model& other)
  : mRoot(other.mRoot)
{
}

Model::Model(Model&& other)
  : mRoot(std::move(other.mRoot))
{
}

Model& Model::operator=(const Model& rhs)
{
    Model temp(rhs);

    swap(temp);

    return *this;
}

Model& Model::operator=(Model&& rhs)
{
    Model temp(std::move(rhs));

    swap(temp);

    return *this;
}

auto Model::add(NodePtr child, const std::string& parentPath) -> Node*
{
    // Sanity.
    assert(child);
    assert(!child->mName.empty());

    // Try and locate the specified parent.
    auto parentNode = get(parentPath);

    // Parent doesn't exist.
    if (!parentNode)
        return nullptr;

    auto parentDirectory = parentNode->directory();

    // Parent's not a directory.
    if (!parentDirectory)
        return nullptr;

    // Try and add the child to the directory.
    return parentDirectory->add(std::move(child));
}

auto Model::directory(const std::string& name) -> DirectoryNodePtr
{
    auto directory = std::make_unique<DirectoryNode>(name);

    directory->mModified = std::chrono::system_clock::now();

    return directory;
}

auto Model::file(const std::string& name,
                 const std::string& content) -> FileNodePtr
{
    // Convenience.
    using Clock = std::chrono::system_clock;

    auto file = std::make_unique<FileNode>(name);

    file->mContent = content;
    file->mSize = content.size();

    // Make sure time is consistent with SDK.
    file->mModified = Clock::from_time_t(Clock::to_time_t(Clock::now()));

    return file;
}

auto Model::file(const std::string& name) -> FileNodePtr
{
    return file(name, name);
}

Model Model::from(const Client& client, CloudPath path)
{
    // Retrieve the directory's info.
    auto info = client.get(std::move(path));

    // Directory doesn't exist.
    if (!info)
        return Model();

    // Directory isn't a directory.
    if (!info->mIsDirectory)
        throw std::runtime_error("Path doesn't specify a directory");

    Model model;

    // Populate the model.
    model.mRoot.add(DirectoryNode::from(client, std::move(*info)));

    return model;
}

Model Model::from(const Path& path)
{
    // Directory doesn't exist.
    if (!fs::exists(path))
        return Model();

    // Directory isn't a directory.
    if (!fs::is_directory(path))
        throw std::runtime_error("Path doesn't specify a directory");

    Model model;

    // Populate the model.
    model.mRoot.add(DirectoryNode::from(path));

    return model;
}

Model Model::generate(const std::string& prefix,
                      std::size_t height,
                      std::size_t numDirectories,
                      std::size_t numFiles)
{
    // Sanity.
    assert(!prefix.empty());

    Model model;

    // Model contains no content.
    if (!height)
        return model;

    // Generate root node.
    auto root = testing::generate(prefix,
                                  height - 1,
                                  numDirectories,
                                  numFiles);

    // Add root node.
    model.mRoot.add(std::move(root));

    // Return model.
    return model;
}

auto Model::get(const std::string& path) const -> const Node*
{
    return const_cast<Model*>(this)->get(path);
}

auto Model::get(const std::string& path) -> Node*
{
    // Start traversal from the root node.
    auto* parent = &mRoot;

    // Skip leading separators.
    auto m = path.find_first_not_of('/');

    // Iterate over path fragments.
    while (m < path.size())
    {
        // Find end of current path fragment.
        auto n = path.find_first_of('/', m + 1);

        // Compute fragment.
        auto fragment = path.substr(m, n - m);

        // Try and locate child.
        auto* child = parent->get(fragment);

        // Child doesn't exist or we've processed the last fragment.
        if (!child || n == path.npos)
            return child;

        // Move to start of next fragment.
        m = path.find_first_not_of('/', n + 1);

        // Ignore trailing separators.
        if (m == path.npos)
            return child;

        // Child should be a directory.
        parent = child->directory();

        // Child isn't a directory.
        if (!parent)
            return nullptr;
    }

    return parent;
}

bool Model::match(const Model& rhs) const
{
    return mRoot.match("/", rhs.mRoot);
}

void Model::populate(const Path& path) const
{
    mRoot.populate(path);
}

auto Model::remove(const std::string& path) -> NodePtr
{
    // Where does the child's name end?
    auto n = path.find_last_not_of('/');

    // Path is nothing but separators.
    if (n == path.npos)
        return nullptr;

    // Where does the child's name begin?
    auto m = path.find_last_of('/', n - 1);

    // Entire path is the name of a child.
    if (m == path.npos)
        return mRoot.remove(path.substr(0, n + 1));

    // Try and locate the child's parent.
    auto* parent = get(path.substr(0, m));

    // Parent doesn't exist.
    if (!parent)
        return nullptr;

    auto* parentDirectory = parent->directory();

    // Parent isn't a directory.
    if (!parentDirectory)
        return nullptr;

    // Try and remove the child.
    return parentDirectory->remove(path.substr(m + 1, n - m));
}

void Model::swap(Model& other)
{
    using std::swap;

    swap(mRoot, other.mRoot);
}

void swap(Model& lhs, Model& rhs)
{
    lhs.swap(rhs);
}

Model::DirectoryNodePtr generate(const std::string& prefix,
                                 std::size_t height,
                                 std::size_t numDirectories,
                                 std::size_t numFiles)
{
    // Create a directory.
    auto directory = Model::directory(prefix);

    // Directory contains no children.
    if (!height)
        return directory;

    // Create subdirectories.
    for (auto d = 0u; d < numDirectories; ++d)
    {
        // Compute subdirectory name.
        auto name = prefix + "d" + std::to_string(d);

        // Generate subdirectory.
        auto subdirectory = generate(name,
                                     height - 1,
                                     numDirectories,
                                     numFiles);

        // Add subdirectory.
        directory->add(std::move(subdirectory));
    }

    // Create files.
    for (auto f = 0u; f < numFiles; ++f)
    {
        // Compute file name.
        auto name = prefix + "f" + std::to_string(f);

        // Add file.
        directory->add(Model::file(name));
    }

    // Return directory.
    return directory;
}

bool mismatch(const std::string& path, const std::string& type)
{
    LOG_debug << "Mismatch "
              << type
              << ": "
              << path;

    return false;
}

} // testing
} // fuse
} // mega

