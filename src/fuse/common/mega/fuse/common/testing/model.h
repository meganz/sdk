#pragma once

#include <chrono>
#include <map>
#include <memory>
#include <string>

#include <mega/common/node_info_forward.h>
#include <mega/fuse/common/testing/client_forward.h>
#include <mega/fuse/common/testing/cloud_path_forward.h>
#include <mega/fuse/common/testing/model_forward.h>
#include <mega/fuse/common/testing/path_forward.h>
#include <mega/fuse/platform/date_time.h>

#include <tests/stdfs.h>

namespace mega
{
namespace fuse
{
namespace testing
{

class Model
{
public:
    class DirectoryNode;
    class FileNode;
    class Node;

    // Convenience.
    using DirectoryNodePtr = std::unique_ptr<DirectoryNode>;
    using FileNodePtr = std::unique_ptr<FileNode>;
    using NodePtr = std::unique_ptr<Node>;
    using NodeMap = std::map<std::string, NodePtr>;

    // Describes an entity in the model.
    class Node
    {
    protected:
        Node(std::string name);

        // Swap this node's attributes with anothers.
        void swap(Node& other);

    public:
        virtual ~Node() = default;

        // Create a copy of this node.
        virtual auto copy() -> NodePtr = 0;

        // Does this node represent a directory?
        auto directory() const -> const DirectoryNode*;

        virtual auto directory() -> DirectoryNode*;

        // Does this node represent a file?
        auto file() const -> const FileNode*;

        virtual auto file() -> FileNode*;

        // Check if this node matches another.
        virtual bool match(const std::string& path, const Node& rhs) const = 0;

        // Populate path with the contents of this node.
        virtual void populate(fs::path path) const = 0;

        // When was this node last modified?
        DateTime mModified;

        // The name of the node.
        std::string mName;
    }; // Node

    // Describes a directory in the model.
    class DirectoryNode
      : public Node
    {
        // Who are this directory's children?
        NodeMap mChildren;

    public:
        DirectoryNode(std::string name);

        DirectoryNode(const DirectoryNode& other);

        DirectoryNode(DirectoryNode&& other);

        auto operator=(DirectoryNode&& rhs) -> DirectoryNode&;

        // Add a child to this directory.
        auto add(NodePtr child) -> Node*;

        // Who are this directory's children?
        auto children() const -> const NodeMap&;

        // Create a copy of this directory.
        auto copy() -> NodePtr override;

        // Return a reference to this directory.
        auto directory() -> DirectoryNode* override;

        // Create a directory based on the content of the cloud.
        static auto from(const Client& client, common::NodeInfo info) -> NodePtr;

        // Create a directory based on the content of path.
        static auto from(const fs::path& path) -> NodePtr;

        // Locate a child in this directory.
        auto get(const std::string& name) const -> const Node*;

        auto get(const std::string& name) -> Node*;

        // Check if this node matches another.
        bool match(const std::string& path, const Node& rhs) const override;

        // Populate path with the contents of this directory.
        void populate(fs::path path) const override;

        // Remove a child from this directory.
        auto remove(const std::string& name) -> NodePtr;

        // Swap this directory's attributes with another.
        void swap(DirectoryNode& other);
    }; // DirectoryNode

    // Describes a file in the model.
    class FileNode
      : public Node
    {
    public:
        FileNode(std::string name);

        // Create a copy of this file.
        auto copy() -> NodePtr override;

        // Return a reference to this file.
        auto file() -> FileNode* override;

        // Create a file based on the content of the cloud.
        static auto from(const Client& client, common::NodeInfo info) -> NodePtr;

        // Create a file based on the content of path.
        static auto from(const fs::path& path) -> NodePtr;

        // Check if this node matches another.
        bool match(const std::string& path, const Node& rhs) const override;

        // Populate path with the contents of this file.
        void populate(fs::path path) const override;

        // What is this file's content?
        std::string mContent;
        
        // How large is this file?
        std::uint64_t mSize;
    }; // FileNode

    Model();

    Model(const Model& other);

    Model(Model&& other);

    Model& operator=(const Model& rhs);

    Model& operator=(Model&& rhs);

    // Add a node to the model.
    auto add(NodePtr child, const std::string& parentPath) -> Node*;

    // Create a new directory node.
    static auto directory(const std::string& name) -> DirectoryNodePtr;

    // Create a new file node.
    static auto file(const std::string& name,
                     const std::string& content) -> FileNodePtr;

    static auto file(const std::string& name) -> FileNodePtr;

    // Build a model based on the contents of the cloud.
    static Model from(const Client& client, CloudPath path);

    // Build a model based on the contents of path.
    static Model from(const Path& path);

    // Generate a model.
    static Model generate(const std::string& prefix,
                          std::size_t height,
                          std::size_t numDirectories,
                          std::size_t numFiles);

    // Locate a node in the model.
    auto get(const std::string& path) const -> const Node*;

    auto get(const std::string& path) -> Node*;

    // Check if this model matches another.
    bool match(const Model& rhs) const;

    // Populate path with the contents of this model.
    void populate(const Path& path) const;

    // Remove a node from the model.
    auto remove(const std::string& path) -> NodePtr;

    // Swap this model's content with another.
    void swap(Model& other);

private:
    // The root directory of this model.
    DirectoryNode mRoot;
}; // Model

// Swap one model with another.
void swap(Model& lhs, Model& rhs);

} // testing
} // fuse
} // mega

