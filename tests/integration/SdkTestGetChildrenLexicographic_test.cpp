#include "megaapi.h"
#include "SdkTestNodesSetUp.h"

#include <gmock/gmock.h>

#include <array>
#include <string_view>
#include <vector>

using namespace sdk_test;
using namespace testing;
using namespace std::string_view_literals;

class SdkTestGetChildrenLexicographic: public SdkTestNodesSetUp
{
    const std::vector<NodeInfo>& getElements() const override
    {
        static const std::vector<NodeInfo> ELEMENTS{
            FileNodeInfo("a"),
            DirNodeInfo("a"),
            FileNodeInfo("a1"),
            FileNodeInfo("a2"),
            FileNodeInfo("a11"),

            FileNodeInfo("A1"),
            FileNodeInfo("A2"),

            FileNodeInfo("B"),
            FileNodeInfo("b"),
            DirNodeInfo("b"), // Same name, different types

            FileNodeInfo("ab"),
            FileNodeInfo("aB"),
        };
        return ELEMENTS;
    }

    const std::string& getRootTestDir() const override
    {
        static const std::string dirName{"SDK_TEST_GETCHILDRENLEXICOGRAPHIC"};
        return dirName;
    }

public:
    MegaHandle getRootDirHandle() const
    {
        return getRootTestDirectory()->getHandle();
    }

    const auto& expectedInOrder()
    {
        static constexpr std::array expected{
            "A1"sv,
            "A2"sv,
            "B"sv,
            "a"sv,
            "a/"sv,
            "a1"sv,
            "a11"sv,
            "a2"sv,
            "aB"sv,
            "ab"sv,
            "b"sv,
            "b/"sv,
        };
        return expected;
    }
};

static std::vector<std::string> toNamesVectorWithSlashForDirs(MegaNodeList* nodes)
{
    std::vector<std::string> result;
    result.reserve(static_cast<size_t>(nodes->size()));
    const auto getNameWithSlash = [](auto* node) -> std::string
    {
        if (node->getType() == MegaNode::TYPE_FOLDER)
            return node->getName() + "/"s;
        return node->getName();
    };
    for (int i = 0; i < nodes->size(); ++i)
    {
        result.emplace_back(getNameWithSlash(nodes->get(i)));
    }
    return result;
}

TEST_F(SdkTestGetChildrenLexicographic, CommonCases)
{
    const auto expected = expectedInOrder();

    {
        // Validate order
        const auto searchResults = megaApi[0]->listChildNodesLexicographically(getRootDirHandle());
        EXPECT_THAT(toNamesVectorWithSlashForDirs(searchResults),
                    ElementsAreArray(expectedInOrder()));
    }

    {
        // Validate limit
        const auto searchResults =
            megaApi[0]->listChildNodesLexicographically(getRootDirHandle(), nullptr, 5);
        EXPECT_THAT(toNamesVectorWithSlashForDirs(searchResults),
                    ElementsAreArray(expected.begin(), expected.begin() + 5));
    }

    {
        // Validate offset alone
        MegaSearchLexicographicalOffset offset{"a"}; // With no type should start at a1

        auto searchResults =
            megaApi[0]->listChildNodesLexicographically(getRootDirHandle(), nullptr, 0, offset);
        EXPECT_THAT(
            toNamesVectorWithSlashForDirs(searchResults),
            ElementsAreArray(std::find(begin(expected), end(expected), "a1"sv), expected.end()));

        offset.mLastType = MegaNode::TYPE_FILE; // Now a/ should be contained

        searchResults =
            megaApi[0]->listChildNodesLexicographically(getRootDirHandle(), nullptr, 0, offset);
        EXPECT_THAT(
            toNamesVectorWithSlashForDirs(searchResults),
            ElementsAreArray(std::find(begin(expected), end(expected), "a/"sv), expected.end()));
    }

    {
        // Validate a real case: 1. Take first n elements 2. Resume from last result
        static constexpr int N_FIRST{4}; // so the end is between "a" and "a/"

        // First query
        auto searchResults =
            megaApi[0]->listChildNodesLexicographically(getRootDirHandle(), nullptr, N_FIRST);
        EXPECT_THAT(toNamesVectorWithSlashForDirs(searchResults),
                    ElementsAreArray(expected.begin(), expected.begin() + N_FIRST));

        // Rest
        auto* lastNode = searchResults->get(searchResults->size() - 1);
        MegaSearchLexicographicalOffset offset{lastNode->getName(),
                                               lastNode->getType(),
                                               lastNode->getHandle()};
        searchResults =
            megaApi[0]->listChildNodesLexicographically(getRootDirHandle(), nullptr, 0, offset);
        EXPECT_THAT(toNamesVectorWithSlashForDirs(searchResults),
                    ElementsAreArray(expected.begin() + N_FIRST, expected.end()));
    }
}
