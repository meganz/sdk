#pragma once

#include "SdkTest_test.h"

namespace mega
{

// Convenience types.
using MegaNodePtr = std::unique_ptr<MegaNode>;
using MegaSearchFilterPtr = std::unique_ptr<MegaSearchFilter>;

class SdkTestNodeTagsCommon: public SdkTest
{
protected:
    auto SetUp() -> void override;

public:
    // Convenience traits.
    template<typename T>
    using IsConvertibleToString = std::is_convertible<T, std::string>;

    template<typename T, typename... Ts>
    using AreConvertibleToStrings =
        std::conjunction<IsConvertibleToString<T>, IsConvertibleToString<Ts>...>;

    // Convenience types.
    using AllTagsResult = std::variant<Error, std::vector<std::string>>;
    using CopyNodeResult = std::variant<Error, MegaNodePtr>;
    using CreateDirectoryResult = std::variant<Error, MegaNodePtr>;
    using SearchResult = std::variant<Error, std::vector<MegaNodePtr>>;
    using UploadFileResult = std::variant<Error, MegaNodePtr>;

    auto addTag(MegaApi& client, const MegaNode& node, const std::string& tag) -> Error;

    template<typename T>
    auto addTags(MegaApi& client, const MegaNode& node, T&& tag)
        -> std::enable_if_t<IsConvertibleToString<T>::value, Error>
    {
        return addTag(client, node, tag);
    }

    template<typename T, typename... Ts>
    auto addTags(MegaApi& client, const MegaNode& node, T&& tag, Ts&&... tags)
        -> std::enable_if_t<AreConvertibleToStrings<T, Ts...>::value, Error>
    {
        if (auto result = addTag(client, node, tag); result != API_OK)
            return result;

        return addTags(client, node, tags...);
    }

    auto allTags(const MegaApi& client) -> AllTagsResult;

    auto copyNode(MegaApi& client,
                  const MegaNode& source,
                  const MegaNode& target,
                  const std::string& name) -> CopyNodeResult;

    auto createDirectory(MegaApi& client, const MegaNode& parent, const std::string& name)
        -> CreateDirectoryResult;

    auto createFile(MegaApi& client, const MegaNode& parent, const std::string& name)
        -> UploadFileResult;

    auto fileVersioning(MegaApi& client, bool enabled) -> Error;

    auto getTags(MegaApi& client, const std::string& path) -> AllTagsResult;

    auto moveNode(MegaApi& client, const MegaNode& source, const MegaNode& target) -> Error;

    auto nodeByHandle(const MegaApi& client, MegaHandle handle) -> MegaNodePtr;

    auto nodeByPath(const MegaApi& client, const std::string& path, const MegaNode* root = nullptr)
        -> MegaNodePtr;

    auto openShareDialog(MegaApi& client, const MegaNode& node) -> Error;

    auto removeTag(MegaApi& client, const MegaNode& node, const std::string& tag) -> Error;

    auto renameTag(MegaApi& client,
                   const MegaNode& node,
                   const std::string& oldTag,
                   const std::string& newTag) -> Error;

    auto rootNode(const MegaApi& client) const -> MegaNodePtr;

    auto search(const MegaApi& client, const MegaSearchFilter& filter) -> SearchResult;

    auto share(MegaApi& client0, const MegaNode& node, const MegaApi& client1, int permissions)
        -> Error;

    auto tagsBelow(const MegaApi& client,
                   const MegaNode& node,
                   const std::string& pattern = std::string()) -> AllTagsResult;

    auto uploadFile(MegaApi& client, const MegaNode& parent, const fs::path& path)
        -> UploadFileResult;

    MegaApi* client0 = nullptr;
    MegaApi* client1 = nullptr;
}; // SdkTestNodeTagsCommon

class SdkTestNodeTagsBasic: public SdkTestNodeTagsCommon
{
    auto SetUp() -> void override;
}; // SdkTestNodeTagsBasic

class SdkTestNodeTagsSearch: public SdkTestNodeTagsCommon
{
    auto SetUp() -> void override;
}; // SdkTestNodeTagsSearch

} // mega
