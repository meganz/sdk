#include "sdk_test_node_tags.h"

#include "mega/scoped_helpers.h"
#include "sdk_test_utils.h"

#include <gmock/gmock-matchers.h>

namespace mega
{

template<typename T>
struct ExpectedValueType;

template<typename T>
struct ExpectedValueType<std::variant<Error, T>>
{
    using type = T;
}; // ExpectedValueType<std::variant<Error, T>>

template<typename T>
struct IsExpected: std::false_type
{}; // IsExpected<T>

template<typename T>
struct IsExpected<std::variant<Error, T>>: std::true_type
{}; // IsExpected<std::variant<Error, T>>

template<typename T>
static constexpr auto IsExpectedV = IsExpected<T>::value;

template<typename T>
using RemoveCVRef = std::remove_cv<std::remove_reference_t<T>>;

template<typename T>
using RemoveCVRefT = typename RemoveCVRef<T>::type;

static bool contains(const MegaStringList& list, const std::string& value);

static std::vector<std::string> nodeNames(const std::vector<MegaNodePtr>& nodes);

template<typename T>
Error result(const std::variant<Error, T>& expected)
{
    if (auto* result = std::get_if<0>(&expected))
        return *result;

    return API_OK;
}

static std::vector<MegaNodePtr> toVector(const MegaNodeList& list);
static std::vector<std::string> toVector(const MegaStringList& list);

template<typename T, typename = std::enable_if_t<IsExpectedV<RemoveCVRefT<T>>>>
decltype(auto) value(T&& expected)
{
    assert(result(expected) == API_OK);

    return std::get<1>(std::forward<T>(expected));
}

static constexpr auto DefaultTimeoutMs = 30 * 1000;
static constexpr auto ErrorTag = std::in_place_type<Error>;
static constexpr auto NodeTag = std::in_place_type<std::unique_ptr<MegaNode>>;
static constexpr auto StringVectorTag = std::in_place_type<std::vector<std::string>>;

TEST_F(SdkTestNodeTagsBasic, add_tag_fails_when_read_only)
{
    auto root = rootNode(*client1);
    ASSERT_NE(root, nullptr);

    // Make sure client1 has something to share with client0.
    auto d = createDirectory(*client1, *root, "d");
    ASSERT_EQ(result(d), API_OK);

    // Make sure client0 and client1 are friends.
    EXPECT_EQ(befriend(*client0, *client1), API_OK);

    // client1 should share d with client0.
    EXPECT_EQ(share(*client1, *value(d), *client0, MegaShare::ACCESS_READ), API_OK);

    // client0 shouldn't be able to add a new tag to client0's directory.
    EXPECT_EQ(addTag(*client0, *value(d), "d"), API_EACCESS);
}

TEST_F(SdkTestNodeTagsBasic, add_tag_fails_when_tag_contains_separator)
{
    auto file = nodeByPath(*client0, "/d0/f0");
    ASSERT_NE(file, nullptr);

    EXPECT_EQ(addTag(*client0, *file, "f0,f1"), API_EARGS);
}

TEST_F(SdkTestNodeTagsBasic, add_tag_fails_when_tag_exists)
{
    auto file = nodeByPath(*client0, "/d0/f0");
    ASSERT_NE(file, nullptr);

    EXPECT_EQ(addTag(*client0, *file, "f0"), API_OK);
    EXPECT_EQ(addTag(*client0, *file, "F0"), API_EEXIST);
}

TEST_F(SdkTestNodeTagsBasic, add_tag_succeeds_when_tag_contains_wildcard)
{
    auto file = nodeByPath(*client0, "/d0/f0");
    ASSERT_NE(file, nullptr);

    EXPECT_EQ(addTag(*client0, *file, "f*0?"), API_OK);
}

TEST_F(SdkTestNodeTagsBasic, add_tag_succeeds)
{
    auto directory = nodeByPath(*client0, "/d0");
    ASSERT_NE(directory, nullptr);

    EXPECT_EQ(addTag(*client0, *directory, "cafe"), API_OK);
    EXPECT_EQ(addTag(*client0, *directory, "café"), API_OK);
}

TEST_F(SdkTestNodeTagsBasic, existing_tags_copied_to_new_file_version)
{
    auto file = nodeByPath(*client0, "/d0/f0");
    ASSERT_NE(file, nullptr);

    EXPECT_EQ(addTag(*client0, *file, "f0"), API_OK);
    EXPECT_EQ(addTag(*client0, *file, "f1"), API_OK);

    auto fileTags = getTags(*client0, "/d0/f0");
    ASSERT_EQ(result(fileTags), API_OK);

    auto directory = nodeByPath(*client0, "/d0");
    ASSERT_NE(directory, nullptr);

    auto newFile = createFile(*client0, *directory, "f0");
    ASSERT_EQ(result(newFile), API_OK);

    auto newFileTags = getTags(*client0, "/d0/f0");
    ASSERT_EQ(result(newFileTags), API_OK);

    EXPECT_EQ(value(fileTags), value(newFileTags));
}

TEST_F(SdkTestNodeTagsBasic, remove_tag_fails_when_tag_doesnt_exist)
{
    auto directory = nodeByPath(*client0, "/d0");
    ASSERT_NE(directory, nullptr);

    EXPECT_EQ(removeTag(*client0, *directory, "d0"), API_ENOENT);
}

TEST_F(SdkTestNodeTagsBasic, remove_tag_succeeds)
{
    auto file = nodeByPath(*client0, "/d0/f0");
    ASSERT_NE(file, nullptr);

    EXPECT_EQ(addTag(*client0, *file, "f0"), API_OK);
    EXPECT_EQ(removeTag(*client0, *file, "F0"), API_OK);
}

TEST_F(SdkTestNodeTagsBasic, rename_tag_fails_when_new_tag_exists)
{
    auto file = nodeByPath(*client0, "/d0/f0");
    ASSERT_NE(file, nullptr);

    EXPECT_EQ(addTag(*client0, *file, "café"), API_OK);
    EXPECT_EQ(addTag(*client0, *file, "tupée"), API_OK);
    EXPECT_EQ(renameTag(*client0, *file, "CAFÉ", "TUPÉE"), API_EEXIST);
}

TEST_F(SdkTestNodeTagsBasic, rename_tag_fails_when_tag_doesnt_exist)
{
    auto directory = nodeByPath(*client0, "/d0");
    ASSERT_NE(directory, nullptr);

    EXPECT_EQ(renameTag(*client0, *directory, "bogus", "insane"), API_ENOENT);
}

TEST_F(SdkTestNodeTagsBasic, rename_tag_succeeds)
{
    auto directory = nodeByPath(*client0, "/d0");
    ASSERT_NE(directory, nullptr);

    EXPECT_EQ(addTag(*client0, *directory, "d0"), API_OK);
    EXPECT_EQ(renameTag(*client0, *directory, "D0", "d1"), API_OK);
}

TEST_F(SdkTestNodeTagsSearch, all_tags_succeeds)
{
    using testing::UnorderedElementsAre;

    auto root = rootNode(*client1);
    ASSERT_NE(root, nullptr);

    // Make sure client1 contains at least one tag.
    auto q = createDirectory(*client1, *root, "q");
    ASSERT_EQ(result(q), API_OK);
    EXPECT_EQ(addTag(*client1, *value(q), "q"), API_OK);

    // Make sure client0 and client1 are friends.
    EXPECT_EQ(befriend(*client0, *client1), API_OK);

    // Share qf with client0.
    EXPECT_EQ(share(*client1, *value(q), *client0, MegaShare::ACCESS_FULL), API_OK);

    // Move x/y/z into the rubbish bin.
    auto rubbish = makeUniqueFrom(client0->getRubbishNode());
    ASSERT_NE(rubbish, nullptr);

    auto z = nodeByPath(*client0, "/x/y/z");
    ASSERT_NE(z, nullptr);

    EXPECT_EQ(moveNode(*client0, *z, *rubbish), API_OK);

    // Get all tags visible in client0.
    auto tags = allTags(*client0);
    ASSERT_EQ(result(tags), API_OK);

    // Should contain all tags except those from client1.
    EXPECT_THAT(
        value(tags),
        UnorderedElementsAre("xf0", "xf1", "xf2", "yf0", "yf1", "yf2", "zf0", "zf1", "zf2"));
}

TEST_F(SdkTestNodeTagsSearch, find_nodes_by_directory_succeeds)
{
    using testing::UnorderedElementsAre;

    auto y = nodeByPath(*client0, "/x/y");
    ASSERT_NE(y, nullptr);

    auto filter = makeUniqueFrom(MegaSearchFilter::createInstance());
    ASSERT_NE(filter, nullptr);

    filter->byLocationHandle(y->getHandle());

    auto nodes = search(*client0, *filter);
    ASSERT_EQ(result(nodes), API_OK);

    EXPECT_THAT(nodeNames(value(nodes)), UnorderedElementsAre("yf", "z", "zf"));
}

TEST_F(SdkTestNodeTagsSearch, find_nodes_by_wildcard_succeeds)
{
    using testing::UnorderedElementsAre;

    auto filter = makeUniqueFrom(MegaSearchFilter::createInstance());
    ASSERT_NE(filter, nullptr);

    filter->byTag("f0");

    auto nodes = search(*client0, *filter);
    ASSERT_EQ(result(nodes), API_OK);

    EXPECT_THAT(nodeNames(value(nodes)), UnorderedElementsAre("xf", "yf", "zf"));
}

TEST_F(SdkTestNodeTagsSearch, find_node_by_tag_succeeds_when_no_matches)
{
    auto filter = makeUniqueFrom(MegaSearchFilter::createInstance());
    ASSERT_NE(filter, nullptr);

    filter->byTag("bogus");

    auto nodes = search(*client0, *filter);
    ASSERT_EQ(result(nodes), API_OK);
    ASSERT_TRUE(value(nodes).empty());
}

TEST_F(SdkTestNodeTagsSearch, find_node_by_tag_succeeds_when_wildcard)
{
    auto filter = makeUniqueFrom(MegaSearchFilter::createInstance());
    ASSERT_NE(filter, nullptr);

    filter->byTag("zf*");

    auto nodes = search(*client0, *filter);
    ASSERT_EQ(result(nodes), API_OK);
    ASSERT_TRUE(value(nodes).empty());
}

TEST_F(SdkTestNodeTagsSearch, find_node_by_tag_succeeds)
{
    auto filter = makeUniqueFrom(MegaSearchFilter::createInstance());
    ASSERT_NE(filter, nullptr);

    // Find a node with a given name by some specified tag.
    auto find = [&](const char* tag, const char* name)
    {
        filter->byTag(tag);

        auto nodes = search(*client0, *filter);
        ASSERT_EQ(result(nodes), API_OK);
        ASSERT_EQ(value(nodes).size(), 1u);
        ASSERT_STREQ(value(nodes).front()->getName(), name);
    }; // find

    // Find xf based on its first tag, xf0.
    EXPECT_NO_FATAL_FAILURE(find("xf0", "xf"));

    // Find yf based on its second tag, yf1.
    EXPECT_NO_FATAL_FAILURE(find("YF1", "yf"));

    // Find zf based on its third and final tag, zf2.
    EXPECT_NO_FATAL_FAILURE(find("zf2", "zf"));
}

TEST_F(SdkTestNodeTagsSearch, tags_below_naturally_sorted_succeeds)
{
    using testing::ElementsAre;

    auto x = nodeByPath(*client0, "/x");
    ASSERT_NE(x, nullptr);

    // Get our hands on the files under /x.
    auto xf = nodeByPath(*client0, "xf", x.get());
    ASSERT_NE(xf, nullptr);
    auto yf = nodeByPath(*client0, "y/yf", x.get());
    ASSERT_NE(yf, nullptr);
    auto zf = nodeByPath(*client0, "y/z/zf", x.get());
    ASSERT_NE(zf, nullptr);

    // Add some recognizable tags.
    EXPECT_EQ(addTags(*client0, *zf, "nf000", "nf123", "nf0123", "nf00123"), API_OK);
    EXPECT_EQ(addTags(*client0, *yf, "nf00", "nf234", "nf0234", "nf00234"), API_OK);
    EXPECT_EQ(addTags(*client0, *xf, "nf0", "nf345", "nf0345", "nf00345"), API_OK);

    // Retrieve all tags under /x starting with nf.
    auto tags = tagsBelow(*client0, *x, "nf");
    ASSERT_EQ(result(tags), API_OK);
    EXPECT_THAT(value(tags),
                ElementsAre("nf0",
                            "nf00",
                            "nf000",
                            "nf00123",
                            "nf0123",
                            "nf123",
                            "nf00234",
                            "nf0234",
                            "nf234",
                            "nf00345",
                            "nf0345",
                            "nf345"));
}

TEST_F(SdkTestNodeTagsSearch, tags_below_succeeds)
{
    using testing::ElementsAre;

    auto x = nodeByPath(*client0, "/x");
    ASSERT_NE(x, nullptr);

    auto y = nodeByPath(*client0, "y", x.get());
    ASSERT_NE(y, nullptr);

    auto z = nodeByPath(*client0, "z", y.get());
    ASSERT_NE(z, nullptr);

    // All tags below z.
    auto tags = tagsBelow(*client0, *z);
    ASSERT_EQ(result(tags), API_OK);
    EXPECT_THAT(value(tags), ElementsAre("zf0", "zf1", "zf2"));

    // All tags below y.
    tags = tagsBelow(*client0, *y);
    ASSERT_EQ(result(tags), API_OK);
    EXPECT_THAT(value(tags), ElementsAre("yf0", "yf1", "yf2", "zf0", "zf1", "zf2"));

    // Add a new version of yf without the yf1 tag.
    auto yf = createFile(*client0, *y, "yf");
    ASSERT_EQ(result(yf), API_OK);
    ASSERT_EQ(removeTag(*client0, *value(yf), "yf1"), API_OK);

    // All tags below x starting with y.
    tags = tagsBelow(*client0, *x, "y*");
    ASSERT_EQ(result(tags), API_OK);
    EXPECT_THAT(value(tags), ElementsAre("yf0", "yf2"));
}

auto SdkTestNodeTagsCommon::SetUp() -> void
{
    SdkTest::SetUp();

    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(2));

    client0 = megaApi[0].get();
    client1 = megaApi[1].get();

    ASSERT_EQ(fileVersioning(*client0, true), API_OK);
    ASSERT_EQ(fileVersioning(*client1, true), API_OK);

    // Makes sharing a lot more convenient.
    client1->setManualVerificationFlag(false);
}

auto SdkTestNodeTagsCommon::addTag(MegaApi& client, const MegaNode& node, const std::string& tag)
    -> Error
{
    RequestTracker tracker(&client);

    client.addNodeTag(const_cast<MegaNode*>(&node), tag.c_str(), &tracker);

    if (auto result = tracker.waitForResult(); result != API_OK)
        return result;

    auto added = WaitFor(
        [&client, handle = node.getHandle(), &tag, this]()
        {
            auto node = nodeByHandle(client, handle);
            if (!node)
                return false;

            auto tags = makeUniqueFrom(node->getTags());
            if (!tags)
                return false;

            return contains(*tags, tag);
        },
        DefaultTimeoutMs);

    return added ? API_OK : LOCAL_ETIMEOUT;
}

auto SdkTestNodeTagsCommon::allTags(const MegaApi& client) -> AllTagsResult
{
    auto self = const_cast<MegaApi*>(&client);
    auto tags = makeUniqueFrom(self->getAllNodeTags());

    if (!tags)
        return AllTagsResult(ErrorTag, API_EINTERNAL);

    return AllTagsResult(StringVectorTag, toVector(*tags));
}

auto SdkTestNodeTagsCommon::copyNode(MegaApi& client,
                                     const MegaNode& source,
                                     const MegaNode& target,
                                     const std::string& name) -> CopyNodeResult
{
    RequestTracker tracker(&client);

    client.copyNode(const_cast<MegaNode*>(&source),
                    const_cast<MegaNode*>(&target),
                    name.c_str(),
                    &tracker);

    if (auto result = tracker.waitForResult(); result != API_OK)
        return CopyNodeResult(ErrorTag, result);

    MegaNodePtr node;

    WaitFor(
        [&]()
        {
            return (node = nodeByPath(client, name, &target)) != nullptr;
        },
        DefaultTimeoutMs);

    if (!node)
        return CopyNodeResult(ErrorTag, LOCAL_ETIMEOUT);

    return CopyNodeResult(NodeTag, std::move(node));
}

auto SdkTestNodeTagsCommon::createDirectory(MegaApi& client,
                                            const MegaNode& parent,
                                            const std::string& name) -> CreateDirectoryResult
{
    RequestTracker tracker(&client);

    client.createFolder(name.c_str(), const_cast<MegaNode*>(&parent), &tracker);

    if (auto result = tracker.waitForResult(); result != API_OK)
    {
        return CreateDirectoryResult(ErrorTag, result);
    }

    MegaNode* directory = nullptr;
    MegaHandle directoryHandle = tracker.request->getNodeHandle();

    WaitFor(
        [&]()
        {
            return (directory = client.getNodeByHandle(directoryHandle)) != nullptr;
        },
        DefaultTimeoutMs);

    if (!directory)
    {
        return CreateDirectoryResult(ErrorTag, LOCAL_ETIMEOUT);
    }

    return CreateDirectoryResult(NodeTag, makeUniqueFrom(directory));
}

auto SdkTestNodeTagsCommon::createFile(MegaApi& client,
                                       const MegaNode& parent,
                                       const std::string& name) -> UploadFileResult
{
    using sdk_test::LocalTempFile;

    auto filePath = fs::u8path(name);
    LocalTempFile file(filePath, 0);

    return uploadFile(client, parent, filePath);
}

auto SdkTestNodeTagsCommon::fileVersioning(MegaApi& client, bool enabled) -> Error
{
    RequestTracker tracker(&client);

    client.setFileVersionsOption(!enabled, &tracker);

    if (auto result = tracker.waitForResult(); result != API_OK)
        return result;

    std::string text = tracker.request->getText();
    unsigned int value;

    auto result = std::from_chars(text.data(), &text[text.size()], value);

    if (result.ec != std::errc{} || enabled != !value)
        return API_EINTERNAL;

    return API_OK;
}

auto SdkTestNodeTagsCommon::getTags(MegaApi& client, const std::string& path) -> AllTagsResult
{
    auto node = nodeByPath(client, path);
    if (!node)
        return AllTagsResult(ErrorTag, API_ENOENT);

    auto tags = makeUniqueFrom(node->getTags());
    if (!tags)
        return AllTagsResult(ErrorTag, API_EINTERNAL);

    return AllTagsResult(StringVectorTag, toVector(*tags));
}

auto SdkTestNodeTagsCommon::moveNode(MegaApi& client,
                                     const MegaNode& source,
                                     const MegaNode& target) -> Error
{
    RequestTracker tracker(&client);

    client.moveNode(const_cast<MegaNode*>(&source), const_cast<MegaNode*>(&target), &tracker);

    if (auto result = tracker.waitForResult(); result != API_OK)
        return result;

    auto sourceHandle = source.getHandle();
    auto targetHandle = target.getHandle();

    auto moved = WaitFor(
        [&]()
        {
            auto node = nodeByHandle(client, sourceHandle);
            return node != nullptr && node->getParentHandle() == targetHandle;
        },
        DefaultTimeoutMs);

    return moved ? API_OK : LOCAL_ETIMEOUT;
}

auto SdkTestNodeTagsCommon::nodeByHandle(const MegaApi& client, MegaHandle handle) -> MegaNodePtr
{
    return makeUniqueFrom(const_cast<MegaApi&>(client).getNodeByHandle(handle));
}

auto SdkTestNodeTagsCommon::nodeByPath(const MegaApi& client,
                                       const std::string& path,
                                       const MegaNode* root) -> MegaNodePtr
{
    auto self = const_cast<MegaApi*>(&client);
    auto node = self->getNodeByPath(path.c_str(), const_cast<MegaNode*>(root));

    return makeUniqueFrom(node);
}

auto SdkTestNodeTagsCommon::openShareDialog(MegaApi& client, const MegaNode& node) -> Error
{
    RequestTracker tracker(&client);

    client.openShareDialog(const_cast<MegaNode*>(&node), &tracker);

    return tracker.waitForResult();
}

auto SdkTestNodeTagsCommon::removeTag(MegaApi& client, const MegaNode& node, const std::string& tag)
    -> Error
{
    RequestTracker tracker(&client);

    client.removeNodeTag(const_cast<MegaNode*>(&node), tag.c_str(), &tracker);

    if (auto result = tracker.waitForResult(); result != API_OK)
        return result;

    auto removed = WaitFor(
        [&client, handle = node.getHandle(), &tag, this]()
        {
            auto node = nodeByHandle(client, handle);
            if (!node)
                return false;

            auto tags = makeUniqueFrom(node->getTags());
            if (!tags)
                return false;

            return !contains(*tags, tag);
        },
        DefaultTimeoutMs);

    return removed ? API_OK : LOCAL_ETIMEOUT;
}

auto SdkTestNodeTagsCommon::renameTag(MegaApi& client,
                                      const MegaNode& node,
                                      const std::string& oldTag,
                                      const std::string& newTag) -> Error
{
    RequestTracker tracker(&client);

    client.updateNodeTag(const_cast<MegaNode*>(&node), newTag.c_str(), oldTag.c_str(), &tracker);

    if (auto result = tracker.waitForResult(); result != API_OK)
        return result;

    auto handle = node.getHandle();

    auto renamed = WaitFor(
        [&client, handle, &newTag, &oldTag, this]()
        {
            auto node = nodeByHandle(client, handle);
            if (!node)
                return false;

            auto tags = makeUniqueFrom(node->getTags());
            if (!tags)
                return false;

            return contains(*tags, newTag) && !contains(*tags, oldTag);
        },
        DefaultTimeoutMs);

    return renamed ? API_OK : LOCAL_ETIMEOUT;
}

auto SdkTestNodeTagsCommon::rootNode(const MegaApi& client) const -> MegaNodePtr
{
    return makeUniqueFrom(const_cast<MegaApi&>(client).getRootNode());
}

auto SdkTestNodeTagsCommon::search(const MegaApi& client, const MegaSearchFilter& filter)
    -> SearchResult
{
    auto nodes = makeUniqueFrom(const_cast<MegaApi&>(client).search(&filter));

    if (!nodes)
        return SearchResult(std::in_place_index<0>, API_EINTERNAL);

    return SearchResult(std::in_place_index<1>, toVector(*nodes));
}

auto SdkTestNodeTagsCommon::share(MegaApi& client0,
                                  const MegaNode& node,
                                  const MegaApi& client1,
                                  int permissions) -> Error
{
    RequestTracker tracker(&client0);

    client0.share(const_cast<MegaNode*>(&node),
                  const_cast<MegaApi&>(client1).getMyEmail(),
                  permissions,
                  &tracker);

    auto result = tracker.waitForResult();

    if (result == API_EKEY)
    {
        if ((result = openShareDialog(client0, node)) != API_OK)
            return result;

        return share(client0, node, client1, permissions);
    }

    if (result != API_OK)
        return result;

    auto shared = WaitFor(
        [&client1, handle = node.getHandle(), this]()
        {
            return nodeByHandle(client1, handle) != nullptr;
        },
        DefaultTimeoutMs);

    return shared ? API_OK : LOCAL_ETIMEOUT;
}

auto SdkTestNodeTagsCommon::tagsBelow(const MegaApi& client,
                                      const MegaNode& node,
                                      const std::string& pattern) -> AllTagsResult
{
    auto pattern_ = pattern.empty() ? nullptr : pattern.c_str();
    auto self = const_cast<MegaApi*>(&client);
    auto tags = makeUniqueFrom(self->getAllNodeTagsBelow(&node, pattern_));

    if (!tags)
        return AllTagsResult(ErrorTag, API_EINTERNAL);

    return AllTagsResult(StringVectorTag, toVector(*tags));
}

auto SdkTestNodeTagsCommon::uploadFile(MegaApi& client,
                                       const MegaNode& parent,
                                       const fs::path& path) -> UploadFileResult
{
    TransferTracker tracker(&client);

    client.startUpload(path.u8string().c_str(),
                       const_cast<MegaNode*>(&parent),
                       path.filename().u8string().c_str(),
                       MegaApi::INVALID_CUSTOM_MOD_TIME,
                       nullptr,
                       false,
                       false,
                       nullptr,
                       &tracker);

    if (auto result = tracker.waitForResult(); result != API_OK)
    {
        return UploadFileResult(ErrorTag, result);
    }

    MegaNodePtr file = nullptr;
    MegaHandle fileHandle = tracker.resultNodeHandle;

    WaitFor(
        [&]()
        {
            return (file = nodeByHandle(client, fileHandle)) != nullptr;
        },
        DefaultTimeoutMs);

    if (!file)
    {
        return UploadFileResult(ErrorTag, LOCAL_ETIMEOUT);
    }

    return UploadFileResult(NodeTag, std::move(file));
}

auto SdkTestNodeTagsBasic::SetUp() -> void
{
    SdkTestNodeTagsCommon::SetUp();

    auto prepare = [&](MegaApi& client)
    {
        auto root = rootNode(client);
        ASSERT_NE(root, nullptr);

        auto directory = createDirectory(client, *root, "d0");
        ASSERT_EQ(result(directory), API_OK);

        auto file = createFile(client, *value(directory), "f0");
        ASSERT_EQ(result(file), API_OK);
    }; // prepare

    ASSERT_NO_FATAL_FAILURE(prepare(*client0));
}

auto SdkTestNodeTagsSearch::SetUp() -> void
{
    SdkTestNodeTagsCommon::SetUp();

    auto prepare = [&](MegaApi& client)
    {
        auto root = rootNode(client);
        ASSERT_NE(root, nullptr);

        auto x = createDirectory(client, *root, "x");
        ASSERT_EQ(result(x), API_OK);

        auto xf = createFile(client, *value(x), "xf");
        ASSERT_EQ(result(xf), API_OK);

        auto y = createDirectory(client, *value(x), "y");
        ASSERT_EQ(result(y), API_OK);

        auto yf = copyNode(client, *value(xf), *value(y), "yf");
        ASSERT_EQ(result(yf), API_OK);

        auto z = createDirectory(client, *value(y), "z");
        ASSERT_EQ(result(z), API_OK);

        auto zf = copyNode(client, *value(xf), *value(z), "zf");
        ASSERT_EQ(result(zf), API_OK);

        ASSERT_EQ(addTags(client, *value(xf), "xf0", "xf1", "xf2"), API_OK);
        ASSERT_EQ(addTags(client, *value(yf), "yf0", "yf1", "yf2"), API_OK);
        ASSERT_EQ(addTags(client, *value(zf), "zf0", "zf1", "zf2"), API_OK);
    }; // prepare

    // Set up test state.
    ASSERT_NO_FATAL_FAILURE(prepare(*client0));
}

bool contains(const MegaStringList& list, const std::string& value)
{
    for (auto i = 0, j = list.size(); i < j; ++i)
    {
        if (value == list.get(i))
            return true;
    }

    return false;
}

std::vector<std::string> nodeNames(const std::vector<MegaNodePtr>& nodes)
{
    auto name = [](const MegaNodePtr& node) -> std::string
    {
        return node->getName();
    }; // name

    std::vector<std::string> names;

    std::transform(std::begin(nodes), std::end(nodes), std::back_inserter(names), name);

    return names;
}

std::vector<MegaNodePtr> toVector(const MegaNodeList& list)
{
    std::vector<MegaNodePtr> result;

    result.reserve(static_cast<std::size_t>(list.size()));

    for (auto i = 0, j = list.size(); i < j; ++i)
    {
        result.emplace_back(makeUniqueFrom(list.get(i)->copy()));
    }

    return result;
}

std::vector<std::string> toVector(const MegaStringList& list)
{
    std::vector<std::string> result;

    result.reserve(static_cast<std::size_t>(list.size()));

    for (auto i = 0, j = list.size(); i < j; ++i)
    {
        result.push_back(list.get(i));
    }

    return result;
}

} // mega
