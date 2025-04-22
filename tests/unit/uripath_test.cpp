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
static const string rootName;
static const string rootDrive;
static const string_type uriBase{"content://com.android.externalstorage.documents/"};
static const string_type uriLeaf1{"folder1"};
static const string_type uriLeaf2{"file.txt"};
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

    virtual std::optional<string_type> getParentURI(const string_type&) override
    {
        assert(false);
        return std::nullopt;
    }

    virtual std::optional<string_type> getPath(const string_type&) override
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
