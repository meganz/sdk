/**
 * @file uripath_test.cpp
 * @brief Unit tests for the PathURI class.
 */

#include "../stdfs.h"
#include "mega/logging.h"

#include <gtest/gtest.h>
#include <mega/file.h>

#include <regex>
#include <string>

using namespace std;
using namespace mega;

#ifdef WIN32
static const string rootName = "D";
static const string rootDrive = rootName + ':';
static const string winPathPrefix = "\\\\?\\";
static const string_type uriBase{L"content://com.android.externalstorage.documents"};
static const string_type uriLeaf1{L"folder1"};
static const string_type uriLeaf2{L"file.txt"};
#else
static const string_type uriBase{"content://com.android.externalstorage.documents"};
static const string_type uriLeaf1{"folder1"};
#endif
static const std::string auxUriBase{"content://com.android.externalstorage.documents"};
static const std::string auxUriLeaf1{"folder1"};
static const std::string auxUriLeaf2{"file.txt"};

static const std::string pathSep{LocalPath::localPathSeparator_utf8};
static const std::string uriPathSep{LocalPath::uriPathSeparator_utf8};

/**
 * @brief UnitTests implementation to handle URIs
 */
class MEGA_API TestPlatformURIHelper: public PlatformURIHelper
{
public:
    bool isURI(const string_type& path) override
    {
        std::string aux;
        LocalPath::local2path(&path, &aux, false);
        static const std::regex uriRegex(R"(^[a-zA-Z][a-zA-Z\d+\-.]*://.+$)");
        return std::regex_match(aux, uriRegex);
    }

    std::optional<string_type> getName(const string_type&) override
    {
        assert(false);
        return std::nullopt;
    }

    std::optional<string_type> getPath(const string_type&) override
    {
        assert(false);
        return std::nullopt;
    }

    std::optional<string_type> getURI(const string_type&, const std::vector<string_type>) override
    {
        assert(false);
        return std::nullopt;
    }

private:
    TestPlatformURIHelper()
    {
        URIHandler::setPlatformHelper(this);
    }

    ~TestPlatformURIHelper() override {}

    static TestPlatformURIHelper mPlatformHelper;
};

TestPlatformURIHelper TestPlatformURIHelper::mPlatformHelper;

TEST(UriPathTest, isURI)
{
    const std::map<std::string, bool> testURIs = {
        // URIs
        {"content://com.android.externalstorage.documents/document/primary%3ADownload%2Ffile.pdf",
         true},
        {"content://media/external/images/media/12345", true},
        {"content://com.android.providers.downloads.documents/document/5678", true},
        {"content://com.android.contacts/contacts/1", true},
        {"content://com.whatsapp.provider.media/item/12345", true},
        {"file:///storage/emulated/0/Download/example.txt", true},
        {"file:///sdcard/Pictures/photo.jpg", true},
        {"http://www.example.com/file.mp3", true},
        {"https://drive.google.com/uc?id=abc123", true},
        {"ftp://ftp.example.com/public/file.zip", true},
        // Non-URIs
        {"/storage/emulated/0/Download/example.txt", false},
        {"/sdcard/DCIM/Camera/photo.jpg", false},
        {"/mnt/sdcard/Music/song.mp3", false},
        {"/data/data/com.example.app/files/config.json", false},
        {"./relative/path/to/file.txt", false},
        {"storage/emulated/0/Music/audio.mp3", false},
        {"Downloads/file.txt", false},
        {"DCIM/Camera/video.mp4", false},
        {"data/user/0/com.example.app/cache/temp.tmp", false}};

    for (const auto& [s, r]: testURIs)
    {
        const auto isUri = LocalPath::isURIPath(s);
        EXPECT_EQ(isUri, r) << s << " - isURI(" << isUri << "). Expected(" << r << ")";
    }
}

TEST(UriPathTest, append)
{
    auto uriPath = LocalPath::fromURIPath(uriBase);
    uriPath.appendWithSeparator(LocalPath::fromRelativePath(auxUriLeaf1), true);
    uriPath.appendWithSeparator(LocalPath::fromRelativePath(auxUriLeaf2), true);
    const auto expected{auxUriBase + uriPathSep + auxUriLeaf1 + uriPathSep + auxUriLeaf2};
    EXPECT_EQ(uriPath.toPath(false), expected);
}

TEST(UriPathTest, appendRelativePathMultipleLevels)
{
    auto uriPath = LocalPath::fromURIPath(uriBase);
    auto aux = LocalPath::fromRelativePath(auxUriLeaf1 + pathSep + auxUriLeaf2);
    uriPath.appendWithSeparator(aux, true);
    const auto expected{auxUriBase + uriPathSep + auxUriLeaf1 + uriPathSep + auxUriLeaf2};
    EXPECT_EQ(uriPath.toPath(false), expected);
}

TEST(UriPathTest, getParentPath)
{
    auto uriPath = LocalPath::fromURIPath(uriBase + uriLeaf1);
    uriPath.appendWithSeparator(LocalPath::fromRelativePath(auxUriLeaf1), true);
    const auto expected{auxUriBase + auxUriLeaf1};
    EXPECT_EQ(uriPath.parentPath().toPath(false), expected);
}

TEST(UriPathTest, getLeafName)
{
    auto uriPath = LocalPath::fromURIPath(uriBase + uriLeaf1);
    uriPath.appendWithSeparator(LocalPath::fromRelativePath(auxUriLeaf2), true);
    EXPECT_EQ(uriPath.leafName().toPath(false), auxUriLeaf2);
    EXPECT_EQ(uriPath.leafOrParentName(), auxUriLeaf2);
}

TEST(UriPathTest, clear)
{
    auto uriPath = LocalPath::fromURIPath(uriBase);
    uriPath.appendWithSeparator(LocalPath::fromRelativePath(auxUriLeaf1), true);
    EXPECT_FALSE(uriPath.empty());
    uriPath.clear();
    EXPECT_TRUE(uriPath.empty());
}

TEST(UriPathTest, getExtension)
{
    auto uriPath = LocalPath::fromURIPath(uriBase);
    uriPath.appendWithSeparator(LocalPath::fromRelativePath(auxUriLeaf2), true);
    EXPECT_EQ(uriPath.extension(), ".txt");
}

TEST(UriPathTest, insertFilenameSuffix)
{
    auto uriPath = LocalPath::fromURIPath(uriBase);
    uriPath.appendWithSeparator(LocalPath::fromRelativePath(auxUriLeaf2), true);
    uriPath = uriPath.insertFilenameSuffix("(1)");
    const auto expected{auxUriBase + uriPathSep + "file(1).txt"};
    EXPECT_EQ(uriPath.toPath(false), expected);
}

TEST(UriPathTest, endsInSeparator)
{
    auto uriStr = uriBase;
    uriStr.pop_back();
    auto uriPath = LocalPath::fromURIPath(uriStr);
    EXPECT_TRUE(uriPath.endsInSeparator());
}

TEST(UriPathTest, trimNonDriveTrailingSeparator)
{
    // No trailing separator to remove is not an error: the call must be a silent no-op.
    auto bare = LocalPath::fromURIPath(uriBase);
    const auto bareBefore = bare.toPath(false);
    bare.trimNonDriveTrailingSeparator();
    EXPECT_EQ(bare.toPath(false), bareBefore);

    auto uriPath = LocalPath::fromURIPath(uriBase);
    uriPath.appendWithSeparator(LocalPath::fromRelativePath(auxUriLeaf1), true);
    uriPath.appendWithSeparator(LocalPath::fromRelativePath(auxUriLeaf2), true);
    const auto before = uriPath.toPath(false);
    uriPath.trimNonDriveTrailingSeparator();
    EXPECT_EQ(uriPath.toPath(false), before);

    // Consecutive separators make splitString emit an empty leaf, so the last one can be empty.
    auto emptyLeaf = LocalPath::fromURIPath(uriBase);
    emptyLeaf.appendWithSeparator(LocalPath::fromRelativePath(auxUriLeaf1 + pathSep + pathSep),
                                  true);
    const auto emptyLeafBefore = emptyLeaf.toPath(false);
    emptyLeaf.trimNonDriveTrailingSeparator();
    EXPECT_EQ(emptyLeaf.toPath(false), emptyLeafBefore);
}

/**
 *  Test that trimNonDriveTrailingSeparator() works as expected on the four SAF URI forms.
 *  content://<authority>/document/<documentId>
 *  content://<authority>/tree/<treeDocumentId>
 *  content://<authority>/tree/<treeDocumentId>/document/<documentId>
 *  content://<authority>/tree/<treeDocumentId>/document/<parentDocumentId>/children
 */
TEST(UriPathTest, trimNonDriveTrailingSeparatorOnSafUriForms)
{
    // The four shapes DocumentsContract builds, all addressing primary:Documents/node1k/abc.
    // The document id is a single path segment, so its ':' and '/' arrive percent-encoded.
    static const std::string authority{"content://com.android.externalstorage.documents"};
    static const std::string treeId{"primary%3ADocuments"};
    static const std::string docId{"primary%3ADocuments%2Fnode1k%2Fabc"};
    static const std::string parentId{"primary%3ADocuments%2Fnode1k"};

    const std::vector<std::string> safUris{
        authority + "/document/" + docId,
        authority + "/tree/" + treeId,
        authority + "/tree/" + treeId + "/document/" + docId,
        authority + "/tree/" + treeId + "/document/" + parentId + "/children",
    };

    for (const auto& uri: safUris)
    {
        string_type uriStr;
        LocalPath::path2local(&uri, &uriStr);

        // No leaves yet, so there is nothing that could be trimmed.
        auto bare = LocalPath::fromURIPath(uriStr);
        ASSERT_TRUE(bare.isURI()) << uri;
        bare.trimNonDriveTrailingSeparator();
        EXPECT_EQ(bare.toPath(false), uri) << uri;

        // A leaf that does not end in a separator must be left alone too.
        auto withLeaf = LocalPath::fromURIPath(uriStr);
        withLeaf.appendWithSeparator(LocalPath::fromRelativePath(auxUriLeaf2), true);
        const auto expected{uri + uriPathSep + auxUriLeaf2};
        ASSERT_EQ(withLeaf.toPath(false), expected) << uri;
        withLeaf.trimNonDriveTrailingSeparator();
        EXPECT_EQ(withLeaf.toPath(false), expected) << uri;
    }
}

/**
 * Premise guard for the WS-upload preflight path check (SDK-5360).
 *
 * MegaClient::prepareUploadForWs gates the preflight on LocalPath::isAbsolute(). A SAF
 * URI kept by the Android folder scan is a usable local file path, yet isAbsolute() is
 * false for it because URIs carry PathType::URI_PATH, not ABSOLUTE_PATH. This test pins
 * that asymmetry: it is the reason the check must be isAbsolute() || isURI(). It stays
 * GREEN before and after the fix — turning RED would mean the premise no longer holds.
 */
TEST(UriPathTest, UriLocalPathIsNotAbsoluteButIsUri)
{
    static const std::string uri{auxUriBase + "/document/primary%3ADocuments%2Ffile.txt"};
    ASSERT_TRUE(LocalPath::isURIPath(uri)) << uri;

    const auto uriPath = LocalPath::fromAbsolutePath(uri);
    EXPECT_FALSE(uriPath.empty()) << uri;
    EXPECT_FALSE(uriPath.isAbsolute()) << uri;
    EXPECT_TRUE(uriPath.isURI()) << uri;
    EXPECT_EQ(uriPath.toPath(false), uri);

    // Control: a conventional absolute path takes the other branch of the same factory.
#ifdef WIN32
    static const std::string absolute{rootDrive + "\\" + auxUriLeaf1 + "\\" + auxUriLeaf2};
#else
    static const std::string absolute{pathSep + auxUriLeaf1 + pathSep + auxUriLeaf2};
#endif
    const auto absolutePath = LocalPath::fromAbsolutePath(absolute);
    EXPECT_FALSE(absolutePath.empty()) << absolute;
    EXPECT_TRUE(absolutePath.isAbsolute()) << absolute;
    EXPECT_FALSE(absolutePath.isURI()) << absolute;
}

/**
 * Contract of the shared predicate the WS-upload preflight now uses (SDK-5360, Hunk 2).
 *
 * `isUsableLocalFilePath` is the one place that spells out "a path the filesystem layer
 * can open": non-empty AND (absolute OR URI). It must accept a SAF `content://` URI --
 * the case the old `!isAbsolute()` gate rejected -- and keep rejecting an empty path and
 * a relative one, because those are the inputs for which the preflight must fail the
 * transfer instead of deferring it forever.
 */
TEST(UriPathTest, WsPreflightPredicateAcceptsUriAndAbsolutePaths)
{
    static const std::string uri{auxUriBase + "/document/primary%3ADocuments%2Ffile.txt"};
    ASSERT_TRUE(LocalPath::isURIPath(uri)) << uri;

    const auto uriPath = LocalPath::fromAbsolutePath(uri);
    ASSERT_TRUE(uriPath.isURI()) << uri;
    ASSERT_FALSE(uriPath.isAbsolute()) << uri;
    EXPECT_TRUE(isUsableLocalFilePath(uriPath)) << uri;

#ifdef WIN32
    static const std::string absolute{rootDrive + "\\" + auxUriLeaf1 + "\\" + auxUriLeaf2};
#else
    static const std::string absolute{pathSep + auxUriLeaf1 + pathSep + auxUriLeaf2};
#endif
    const auto absolutePath = LocalPath::fromAbsolutePath(absolute);
    ASSERT_TRUE(absolutePath.isAbsolute()) << absolute;
    EXPECT_TRUE(isUsableLocalFilePath(absolutePath)) << absolute;

    EXPECT_FALSE(isUsableLocalFilePath(LocalPath{}));

    const auto relativePath = LocalPath::fromRelativePath("b.txt");
    ASSERT_FALSE(relativePath.empty());
    ASSERT_FALSE(relativePath.isAbsolute());
    ASSERT_FALSE(relativePath.isURI());
    EXPECT_FALSE(isUsableLocalFilePath(relativePath));
}
