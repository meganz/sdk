#include "sdk_test_utils.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <mega.h>
#include <memory>
#include <optional>
#include <regex>

namespace mega
{

// Convenience.
using namespace std::filesystem;
using namespace std::string_literals;
using namespace std;
using namespace testing;

// Check that processor correctly generates thumbnail for sourcePath.
static bool checkThumbnailFile(GfxProviderFreeImage& provider,
                               const path& destinationPath,
                               const path& sourcePath);

// Create a function that checks whether we can correctly thumbnail a file.
static std::function<void(const path&)> checkThumbnailFileFunction(const path& destinationPath);

// Check whether two files contain the same data.
static bool compareFiles(const path& path0, const path& path1);

// Execute a callback for each file a specified directory.
template<typename Callback>
auto forEachFile(Callback callback, const path& directoryPath)
    -> enable_if_t<is_invocable_v<Callback, const path&>>;

// Read the file named by path into memory.
static optional<vector<uint8_t>> readFile(const path& path);

// Replace all occurances of pattern in input with replacement.
static string replaceAll(const string& input, const string& pattern, const string& replacement);

// Returns a path to our test data.
static fs::path thumbnailDataDirectory();

// Use provider to save a thumbnail of sourcePath to destinationPath.
static bool thumbnailFile(GfxProviderFreeImage& provider,
                          const path& destinationPath,
                          const path& sourcePath);

// Convert a path to an input file into a path to an expected result file.
static path toExpectedPath(const path& inputPath);

// Convert a standard path into a local path.
static LocalPath toLocalPath(const path& path);

// Write content to destinationPath.
static bool writeFile(const path& destinationPath, const string& content);

TEST(ThumbnailTest, rotated_jpeg)
{
    using sdk_test::LocalTempDir;

    LocalTempDir destination("jpeg");

    forEachFile(checkThumbnailFileFunction(destination.path()),
                thumbnailDataDirectory() / "jpeg" / "input");
}

TEST(ThumbnailTest, rotated_mp4)
{
    using sdk_test::LocalTempDir;

    LocalTempDir destination("mp4");

    forEachFile(checkThumbnailFileFunction(destination.path()),
                thumbnailDataDirectory() / "mp4" / "input");
}

bool checkThumbnailFile(GfxProviderFreeImage& provider,
                        const path& destinationPath,
                        const path& sourcePath)
{
    // Convenience.
    static const auto JPEG = u8path("jpeg");

    // Compute destination file name.
    auto destinationFile = destinationPath / sourcePath.filename();
    destinationFile.replace_extension(JPEG);

    // Try and generate a thumbnail for sourcePath.
    auto result = thumbnailFile(provider, destinationFile, sourcePath);

    // Couldn't generate a thumbnail for sourcePath.
    EXPECT_TRUE(result) << "Couldn't generate thumbnail for " << sourcePath;

    if (!result)
    {
        return false;
    }

    // Compute expected file name.
    auto expectedFile = toExpectedPath(sourcePath).replace_extension(JPEG);

    // Does the generated thumbnail match our expected reuslt?
    result = compareFiles(destinationFile, expectedFile);

    // Make sure the thumbnails match.
    EXPECT_TRUE(result) << "The thumbnail generated for " << sourcePath << " (at "
                        << destinationFile << ") doesn't match " << expectedFile;

    // Let the caller know if the thumbnail wsa generated correctly.
    return result;
}

std::function<void(const path&)> checkThumbnailFileFunction(const path& destinationPath)
{
    // So we can quietly handle filesystem errors.
    error_code result;

    // Make sure destination path exists.
    create_directories(destinationPath, result);

    // Couldn't create necessary directories.
    EXPECT_FALSE(result) << "Couldn't create destination directory " << destinationPath;

    // Return a dummy if we can't create our destination directories.
    if (result)
    {
        return [](const path&)
        {
            return false;
        };
    }

    // Make a provider so we can generate thumbnails.
    auto provider = make_shared<GfxProviderFreeImage>();

    // Return our check function.
    return [destinationPath, provider](const path& sourcePath)
    {
        return checkThumbnailFile(*provider, destinationPath, sourcePath);
    };
}

bool compareFiles(const path& path0, const path& path1)
{
    // Read path0 and path1 into memory.
    auto data0 = readFile(path0);
    auto data1 = readFile(path1);

    // Couldn't read one or both files into memory.
    if (!data0 || !data1)
    {
        return false;
    }

    // Make sure both files are the same.
    return data0.value() == data1.value();
}

template<typename Callback>
auto forEachFile(Callback callback, const path& directoryPath)
    -> enable_if_t<is_invocable_v<Callback, const path&>>
{
    // So we can handle filesystem failures without throwing.
    error_code result;

    // So we can iterate over our directory's content.
    directory_iterator i(directoryPath, result);

    // Couldn't open directory for iteration.
    ASSERT_FALSE(result) << "Couldn't open " << directoryPath << " for iteration";

    // Iterate over the directory.
    for (auto j = directory_iterator(); i != j; ++i)
    {
        // Calling callback on each path in turn.
        invoke(callback, i->path());
    }
}

optional<vector<uint8_t>> readFile(const path& path)
{
    // So we can handle filesystem errors quietly.
    error_code result;

    // Determine how large the file named by path is.
    auto size = static_cast<streamsize>(file_size(path, result));

    // Make sure we could determine the file's size.
    EXPECT_FALSE(result) << "Couldn't determine file size of " << path;

    // Couldn't determine the file's size.
    if (result)
    {
        return nullopt;
    }

    // Allocate a buffer to store the file's content.
    vector<uint8_t> buffer(static_cast<size_t>(size));

    // Open the file for reading.
    std::ifstream istream(path.u8string(), ios::binary);

    // Make sure we could open the file for reading.
    EXPECT_TRUE(istream) << "Couldn't open " << path << " for reading";

    // Couldn't open the file for reading.
    if (!istream)
    {
        return nullopt;
    }

    // Try and read the file into memory.
    istream.read(reinterpret_cast<char*>(buffer.data()), size);

    // Make sure we could read the entire fileinto memory.
    EXPECT_EQ(istream.gcount(), size) << "Couldn't read " << path << " into memory";

    // Couldn't read the file into memory.
    if (istream.gcount() != size)
    {
        return nullopt;
    }

    // Return buffer to caller.
    return buffer;
}

string replaceAll(const string& input, const string& pattern, const string& replacement)
{
    // Replace pattern with replacement in input.
    return regex_replace(input, regex(pattern), replacement);
}

fs::path thumbnailDataDirectory()
{
    // Convenience.
    return sdk_test::getTestDataDir() / "unit-test-data" / "thumbnails";
}

bool thumbnailFile(GfxProviderFreeImage& provider,
                   const path& destinationPath,
                   const path& sourcePath)
{
    // Convenience.
    static auto dimension = GfxProc::DIMENSIONS[GfxProc::THUMBNAIL];
    static auto dimensions = vector<GfxDimension>(1, dimension);

    // Try and generate a thumbnail image.
    auto thumbnails = provider.generateImages(toLocalPath(sourcePath), dimensions);

    // Make sure some thumbnails were generated.
    EXPECT_FALSE(thumbnails.empty());

    // Couldn't generate any thumbnails.
    if (thumbnails.empty())
    {
        return false;
    }

    // Let the caller know if we could write the thumbnail to destination path.
    return writeFile(destinationPath, thumbnails.front());
}

path toExpectedPath(const path& inputPath)
{
    // Replace "input" in inputPath with "expected."
    return u8path(replaceAll(inputPath.u8string(), "input", "expected"));
}

LocalPath toLocalPath(const path& path)
{
    return LocalPath::fromPlatformEncodedAbsolute(path.u8string());
}

bool writeFile(const path& destinationPath, const string& content)
{
    // Try and open destinaton path for writing.
    ofstream ostream(destinationPath, ios::binary | ios::trunc);

    // Make sure the file's open for writing.
    EXPECT_TRUE(ostream) << "Couldn't open " << destinationPath << " for writing";

    // Couldn't open the file for writing.
    if (!ostream)
    {
        return false;
    }

    // Try and write content to the file.
    ostream.write(content.data(), static_cast<streamsize>(content.size()));

    EXPECT_TRUE(ostream.good()) << "Couldn't write data to " << destinationPath;

    // Let the caller know if the content was written to the file.
    return ostream.good();
}

} // mega
