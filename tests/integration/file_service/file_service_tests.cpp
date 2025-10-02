#include <gmock/gmock.h>
#include <mega/common/error_or.h>
#include <mega/common/node_info.h>
#include <mega/common/node_key_data.h>
#include <mega/common/testing/cloud_path.h>
#include <mega/common/testing/file.h>
#include <mega/common/testing/path.h>
#include <mega/common/testing/single_client_test.h>
#include <mega/common/testing/utility.h>
#include <mega/common/testing/watchdog.h>
#include <mega/common/utility.h>
#include <mega/file_service/file.h>
#include <mega/file_service/file_event.h>
#include <mega/file_service/file_event_vector.h>
#include <mega/file_service/file_flush_event.h>
#include <mega/file_service/file_id.h>
#include <mega/file_service/file_info.h>
#include <mega/file_service/file_location.h>
#include <mega/file_service/file_move_event.h>
#include <mega/file_service/file_range.h>
#include <mega/file_service/file_read_result.h>
#include <mega/file_service/file_remove_event.h>
#include <mega/file_service/file_result.h>
#include <mega/file_service/file_result_or.h>
#include <mega/file_service/file_service.h>
#include <mega/file_service/file_service_options.h>
#include <mega/file_service/file_service_result.h>
#include <mega/file_service/file_service_result_or.h>
#include <mega/file_service/file_touch_event.h>
#include <mega/file_service/file_truncate_event.h>
#include <mega/file_service/file_write_event.h>
#include <mega/file_service/file_write_result.h>
#include <mega/file_service/logging.h>
#include <mega/file_service/source.h>
#include <mega/file_service/testing/integration/client.h>
#include <mega/file_service/testing/integration/real_client.h>
#include <mega/file_service/testing/integration/scoped_file_event_observer.h>

#include <chrono>
#include <cinttypes>

namespace mega
{
namespace file_service
{

// Convenience.
std::ostream& operator<<(std::ostream& ostream, const FileLocation& location)
{
    return ostream << "{name: " << location.mName
                   << ", parent: " << toNodeHandle(location.mParentHandle) << "}";
}

// Teach gtest how to print file IDs.
void PrintTo(const FileID& id, std::ostream* ostream)
{
    *ostream << toString(id);
}

// Teach gtest how to print our file event instances.
void PrintTo(const FileFlushEvent& event, std::ostream* ostream)
{
    *ostream << "Flush {handle: " << toNodeHandle(event.mHandle) << ", id: " << toString(event.mID)
             << "}";
}

void PrintTo(const FileMoveEvent& event, std::ostream* ostream)
{
    *ostream << "Move {from: " << event.mFrom << ", to: " << event.mTo
             << ", id: " << toString(event.mID) << "}";
}

void PrintTo(const FileRemoveEvent& event, std::ostream* ostream)
{
    *ostream << "Remove {id: " << toString(event.mID) << ", replaced: " << event.mReplaced << "}";
}

void PrintTo(const FileTouchEvent& event, std::ostream* ostream)
{
    *ostream << "Touch {id: " << toString(event.mID) << ", modified: " << event.mModified << "}";
}

void PrintTo(const FileTruncateEvent& event, std::ostream* ostream)
{
    // Assume no part of the file has been "cut off."
    std::string range = "[]";

    // Actually did cut off some part of the file.
    if (event.mRange)
        range = toString(*event.mRange);

    *ostream << "Truncate {range: " << range << ", id: " << toString(event.mID)
             << ", size: " << event.mSize << "}";
}

void PrintTo(const FileWriteEvent& event, std::ostream* ostream)
{
    *ostream << "Write {range: " << event.mRange << ", id: " << toString(event.mID) << "}";
}

void PrintTo(const FileEvent& event, std::ostream* ostream)
{
    std::visit(
        [ostream](const auto& event)
        {
            PrintTo(event, ostream);
        },
        event);
}

namespace testing
{

// Convenience.
using common::Expected;
using common::makeSharedPromise;
using common::NodeKeyData;
using common::now;
using common::unexpected;
using common::testing::Path;
using common::testing::randomBytes;
using common::testing::randomName;
using common::testing::ScopedWatch;
using common::testing::SingleClientTest;
using common::testing::waitFor;
using common::testing::Watchdog;
using ::testing::AnyOf;
using ::testing::ElementsAre;
using testing::observe;
using ::testing::UnorderedElementsAreArray;

// Forward declaration so we can keep things ordered.
template<typename T>
struct FutureResult;

template<typename T>
using FutureResultT = typename FutureResult<T>::type;

template<typename Function, typename... Parameters>
struct IsAsynchronousFunctionCall;

template<typename Function, typename... Parameters>
constexpr auto IsAsynchronousFunctionCallV =
    IsAsynchronousFunctionCall<Function, Parameters...>::value;

template<typename T>
struct IsFuture;

template<typename T>
constexpr auto IsFutureV = IsFuture<T>::value;

// Determine the value returned by an asynchronous function call.
template<typename Function, typename... Parameters>
struct AsynchronousFunctionCallResult
{
    // Ensure that function(parameters, ...) is an asynchronous call.
    static_assert(IsAsynchronousFunctionCallV<Function, Parameters...>);

    // Convenience.
    using result = std::invoke_result_t<Function, Parameters...>;
    using type = FutureResultT<result>;
}; // AsynchronousFunctionCallResult<Function, Parameters...>

template<typename Function, typename... Parameters>
using AsynchronousFunctionCallResultT =
    typename AsynchronousFunctionCallResult<Function, Parameters...>::type;

// Determine the value returned by an std::future.
template<typename T>
struct FutureResult
{}; // FutureResult<T>

template<typename T>
struct FutureResult<std::future<T>>
{
    using type = T;
}; // FutureResult<std::future<T>>

template<typename T>
struct GenerateFailure;

template<typename E, typename T>
struct GenerateFailure<Expected<E, T>>
{
    static auto value()
    {
        return unexpected(GenerateFailure<E>::value());
    }
}; // GenerateFailure<Expected<E, T>>

template<>
struct GenerateFailure<FileResult>
{
    static auto value()
    {
        return FILE_FAILED;
    }
}; // GenerateFailure<FileResult>

template<>
struct GenerateFailure<FileServiceResult>
{
    static auto value()
    {
        return FILE_SERVICE_UNEXPECTED;
    }
}; // GenerateFailure<FileServiceResult>

template<typename T>
constexpr auto GenerateFailureV = GenerateFailure<T>::value();

// Check if function(parameters, ...) is an asynchronous function call.
template<typename Function, typename... Parameters>
struct IsAsynchronousFunctionCall:
    public std::conjunction<std::is_invocable<Function, Parameters...>,
                            IsFuture<std::invoke_result_t<Function, Parameters...>>>
{}; // IsAsynchronousFunctionCall<Function, Parameters...>

// Check whether T is an std::future.
template<typename T>
struct IsFuture: public std::false_type
{}; // IsFuture<T>

template<typename T>
struct IsFuture<std::future<T>>: public std::true_type
{}; // IsFuture<std::future<T>>

// Convenience.
constexpr auto timeout = std::future_status::timeout;

struct FileServiceTestTraits
{
    using AbstractClient = Client;
    using ConcreteClient = RealClient;

    static constexpr const char* mName = "file_service";
}; // FileServiceTestTraits

class FileServiceTests: public SingleClientTest<FileServiceTestTraits>
{
public:
    // Perform instance-specific setup.
    void SetUp() override;

    // Perform fixture-wide setup.
    static void SetUpTestSuite();

    // Perform instance-specific teardown.
    void TearDown() override;

    // The content of the file we want to read.
    static std::string mFileContent;

    // The handle of the file we want to read.
    static NodeHandle mFileHandle;

    // The name of the file we want to read.
    static std::string mFileName;

    // The handle of our test root directory.
    static NodeHandle mRootHandle;

    // Terminates the program if we encounter a deadlock.
    static Watchdog mWatchdog;
}; // FUSEPartialDownloadTests

// For clarity.
constexpr std::uint64_t operator""_KiB(unsigned long long value)
{
    return value * 1024;
}

constexpr std::uint64_t operator""_MiB(unsigned long long value)
{
    return value * 1024_KiB;
}

// Append content to the end of the specified file.
static auto append(const void* buffer, File file, std::uint64_t length) -> std::future<FileResult>;

// Compare content.
static bool compare(const std::string& computed,
                    const std::string& expected,
                    std::uint64_t offset,
                    std::uint64_t length);

// Execute an asynchronous request synchronously.
template<typename Function, typename... Parameters>
auto execute(Function&& function, Parameters&&... arguments)
    -> std::enable_if_t<IsAsynchronousFunctionCallV<Function, Parameters...>,
                        AsynchronousFunctionCallResultT<Function, Parameters...>>;

// Fetch all of a file's content from the cloud.
static auto fetch(File file) -> std::future<FileResult>;

// Wait until all fetches have been completed.
static auto fetchBarrier(File file) -> std::future<FileResult>;

// Flush a file's modified content to the cloud.
static auto flush(File file) -> std::future<FileResult>;

// Purge a file from the service.
static auto purge(File file) -> std::future<FileResult>;

// Read some content from the specified file.
static auto read(File file, std::uint64_t offset, std::uint64_t length)
    -> std::future<FileResultOr<std::string>>;

// As above but a partial read is allowed.
static auto readOnce(File file, std::uint64_t offset, std::uint64_t length)
    -> std::future<FileResultOr<std::string>>;

// Reclaim a file's storage.
static auto reclaim(File file) -> std::future<FileResultOr<std::uint64_t>>;

// Reclaim zero or more files managed by client.
static auto reclaimAll(ClientPtr& client) -> std::future<FileServiceResultOr<std::uint64_t>>;

// Remove a file.
static auto remove(File file) -> std::future<FileResult>;

// Update the specified file's modification time.
static auto touch(File file, std::int64_t modified) -> std::future<FileResult>;

// Truncate the specified file to a particular size.
static auto truncate(File file, std::uint64_t newSize) -> std::future<FileResult>;

// Write some content to the specified file.
static auto write(const void* buffer, File file, std::uint64_t offset, std::uint64_t length)
    -> std::future<FileResult>;

std::string FileServiceTests::mFileContent;

NodeHandle FileServiceTests::mFileHandle;

std::string FileServiceTests::mFileName;

NodeHandle FileServiceTests::mRootHandle;

Watchdog FileServiceTests::mWatchdog(logger());

static const FileServiceOptions DefaultOptions;

static const FileServiceOptions DisableReadahead = {
    DefaultOptions.mMaximumRangeRetries,
    0u,
    0u,
    DefaultOptions.mRangeRetryBackoff,
    DefaultOptions.mReclaimAgeThreshold,
    DefaultOptions.mReclaimBatchSize,
    DefaultOptions.mReclaimDelay,
    DefaultOptions.mReclaimPeriod,
    DefaultOptions.mReclaimSizeThreshold}; // DisableReadahead

static constexpr auto MaxTestRunTime = std::chrono::minutes(15);
static constexpr auto MaxTestSetupTime = std::chrono::minutes(15);

TEST_F(FileServiceTests, DISABLED_measure_average_linear_read_time)
{
    // How large should the test file be?
    constexpr auto fileSize = 16_MiB;

    // How many samples should we perform?
    constexpr auto numSamples = 10ul;

    // How large should each individual read be?
    constexpr auto readSize = 8_KiB;

    // Try and create a test file for us to read from.
    auto handle = mClient->upload(randomBytes(fileSize), randomName(), mRootHandle);

    // Make sure we could create our test file.
    ASSERT_EQ(handle.errorOr(API_OK), API_OK);

    // Average read time for the entire file.
    std::uint64_t averageFileReadTime = 0ul;

    // Average read time for each range.
    std::uint64_t averageRangeReadTime = 0ul;

    // Figure out how long it takes to linearly read all our file.
    for (auto i = 0ul; i < numSamples; ++i)
    {
        // Try and open our file for reading.
        auto file = mClient->fileOpen(*handle);

        // Make sure we could open our file.
        ASSERT_EQ(file.errorOr(FILE_SERVICE_SUCCESS), FILE_SERVICE_SUCCESS);

        // Linearly read the entire file.
        for (auto offset = 0ul; offset < fileSize; offset += readSize)
        {
            // Convenience.
            using std::chrono::duration_cast;
            using std::chrono::milliseconds;
            using std::chrono::steady_clock;

            // Track when this read began.
            auto began = steady_clock::now();

            // Try and read some data.
            auto data = execute(read, *file, offset, readSize);

            // Track when the read finished.
            auto elapsed = steady_clock::now() - began;

            // Make sure the read succeeded.
            ASSERT_EQ(data.errorOr(FILE_SUCCESS), FILE_SUCCESS);

            // Convenience.
            auto elapsedMs =
                static_cast<std::uint64_t>(duration_cast<milliseconds>(elapsed).count());

            // For curiosity.
            FSDebugF("Range read time: %s: %" PRIi64 " millisecond(s).",
                     toString(FileRange(offset, offset + readSize)).c_str(),
                     elapsedMs);

            // Make sure our measurements don't overflow.
            ASSERT_GE(UINT64_MAX - elapsedMs, averageFileReadTime);
            ASSERT_GE(UINT64_MAX - elapsedMs, averageRangeReadTime);

            // Update our measurements.
            averageFileReadTime += elapsedMs;
            averageRangeReadTime += elapsedMs;
        }
    }

    // Calculate the average time it took to read the entire file.
    averageFileReadTime /= numSamples;

    // Calculate the average time it took to read each range.
    averageRangeReadTime /= (fileSize / readSize) * numSamples;

    // Log our findings.
    FSDebugF("Average linear file read time: %" PRIu64 " millisecond(s)", averageFileReadTime);
    FSDebugF("Average linear range read time: %" PRIu64 " millisecond(s)", averageRangeReadTime);
}

TEST_F(FileServiceTests, add_external_succeeds)
{
    // Get a public link for our test directory.
    auto link = mClient->getPublicLink(mRootHandle);
    ASSERT_EQ(link.errorOr(API_OK), API_OK);

    // Create a client responsible for accessing our test directory.
    auto client = CreateClient("file_service_" + randomName());
    ASSERT_TRUE(client);

    // Log the client into our test directory.
    ASSERT_EQ(client->login(*link), API_OK);

    // Retrieve our file's key data.
    auto keyData = client->keyData(mFileHandle, true);
    ASSERT_EQ(keyData.errorOr(API_OK), API_OK);

    // Log out of the test directory.
    ASSERT_EQ(client->logout(false), API_OK);

    // Log client into a account distinct from mClient.
    ASSERT_EQ(client->login(1), API_OK);

    // Try and add the file to the client.
    auto id = client->fileService().add(mFileHandle, *keyData, mFileContent.size());
    ASSERT_EQ(id.errorOr(FILE_SERVICE_SUCCESS), FILE_SERVICE_SUCCESS);
}

TEST_F(FileServiceTests, add_public_succeeds)
{
    // Create a new client.
    auto client = CreateClient("file_service_" + randomName());
    ASSERT_TRUE(client);

    // Log the client in.
    ASSERT_EQ(client->login(1), API_OK);

    // Retrieve public link for our test file.
    auto link = mClient->getPublicLink(mFileHandle);
    ASSERT_EQ(link.errorOr(API_OK), API_OK);

    // We should be able to add mClient's test file to client.
    auto id = client->fileAdd(*link);
    ASSERT_EQ(id.errorOr(FILE_SERVICE_SUCCESS), FILE_SERVICE_SUCCESS);

    // We should get a useful error if we add the same file multiple times.
    id = client->fileAdd(*link);
    ASSERT_EQ(id.errorOr(FILE_SERVICE_SUCCESS), FILE_SERVICE_FILE_ALREADY_EXISTS);
}

TEST_F(FileServiceTests, append_succeeds)
{
    // Disable readahead.
    mClient->fileService().options(DisableReadahead);

    // Open file for writing.
    auto file = mClient->fileOpen(mFileHandle);

    // Make sure we could open the file.
    ASSERT_EQ(file.errorOr(FILE_SERVICE_SUCCESS), FILE_SERVICE_SUCCESS);

    // Get our hands on the file's attributes.
    auto info = file->info();

    // Convenience.
    FileRange range(info.size() - 64_KiB, info.size() - 32_KiB);

    // Read some data just before the end of the file.
    {
        // Convenience.
        auto offset = range.mBegin;
        auto length = range.mEnd - range.mBegin;

        // Perform the read.
        auto result = execute(read, *file, offset, length);

        // Make sure the read completed successfully.
        ASSERT_EQ(result.errorOr(FILE_SUCCESS), FILE_SUCCESS);
        ASSERT_EQ(result->size(), static_cast<std::size_t>(length));

        // Reads shouldn't dirty a file.
        ASSERT_FALSE(info.dirty());
    }

    // Events that we expect to receive.
    FileEventVector expected;

    // Store events emitted for our file.
    auto fileObserver = observe(*file);
    auto serviceObserver = observe(mClient->fileService());

    // Generate some data for us to append to the file.
    auto computed = randomBytes(32_KiB);

    // Latch the file's access time, modification time and size.
    auto modified = info.modified();
    auto size = info.size();

    // Try and append the data to the end of the file.
    ASSERT_EQ(execute(append, computed.data(), *file, computed.size()), FILE_SUCCESS);

    expected.emplace_back(FileWriteEvent{FileRange(size, size + computed.size()), info.id()});

    // The file should now have two ranges.
    ASSERT_THAT(file->ranges(), ElementsAre(range, FileRange(size, size + computed.size())));

    // Make sure the file's attributes have been updated.
    ASSERT_TRUE(info.dirty());
    ASSERT_GE(info.accessed(), modified);
    ASSERT_GE(info.modified(), modified);
    ASSERT_EQ(info.size(), size + computed.size());

    // Latch current modification time and size.
    modified = info.modified();
    size = info.size();

    // Append again to make sure contigous ranges are extended.
    ASSERT_EQ(execute(append, computed.data(), *file, computed.size()), FILE_SUCCESS);

    expected.emplace_back(FileWriteEvent{FileRange(size, size + computed.size()), info.id()});

    ASSERT_GE(info.modified(), modified);
    ASSERT_EQ(info.size(), size + computed.size());

    ASSERT_THAT(file->ranges(),
                ElementsAre(range, FileRange(size - computed.size(), size + computed.size())));

    // Make sure we received the events we expected.
    ASSERT_EQ(expected, fileObserver.events());
    ASSERT_EQ(expected, serviceObserver.events());
}

TEST_F(FileServiceTests, cloud_file_removed_when_parent_removed)
{
    // Create a tree we can mess with.
    auto d0 = mClient->makeDirectory(randomName(), mRootHandle);
    ASSERT_EQ(d0.errorOr(API_OK), API_OK);

    auto d1 = mClient->makeDirectory(randomName(), *d0);
    ASSERT_EQ(d1.errorOr(API_OK), API_OK);

    auto d0f = mClient->upload(randomBytes(512), randomName(), *d0);
    ASSERT_EQ(d0f.errorOr(API_OK), API_OK);

    auto d1f = mClient->upload(randomBytes(512), randomName(), *d1);
    ASSERT_EQ(d1f.errorOr(API_OK), API_OK);

    // What events do we expect to receive?
    struct
    {
        FileEventVector file0;
        FileEventVector file1;
        FileEventVector service;
    } expected;

    // Open d0f and d1f.
    auto file0 = mClient->fileOpen(*d0f);
    ASSERT_EQ(file0.errorOr(FILE_SERVICE_SUCCESS), FILE_SERVICE_SUCCESS);

    auto file1 = mClient->fileOpen(*d1f);
    ASSERT_EQ(file1.errorOr(FILE_SERVICE_SUCCESS), FILE_SERVICE_SUCCESS);

    // So we can receive file events.
    auto fileObserver0 = observe(*file0);
    auto fileObserver1 = observe(*file1);
    auto serviceObserver = observe(mClient->fileService());

    // Remove d0 and by proxy, d0f, d1 and d1f.
    ASSERT_EQ(mClient->remove(*d0), API_OK);

    expected.file0.emplace_back(FileRemoveEvent{file0->info().id(), false});
    expected.file1.emplace_back(FileRemoveEvent{file1->info().id(), false});
    expected.service.emplace_back(expected.file0.back());
    expected.service.emplace_back(expected.file1.back());

    // Make sure our files are marked as removed.
    EXPECT_TRUE(waitFor(
        [&]()
        {
            return file0->info().removed() && file1->info().removed();
        },
        mDefaultTimeout));

    EXPECT_TRUE(file0->info().removed());
    EXPECT_TRUE(file1->info().removed());

    // Make sure we received remove events.
    EXPECT_EQ(expected.file0, fileObserver0.events());
    EXPECT_EQ(expected.file1, fileObserver1.events());

    // UnorderedElementsAreArray(...) necessary as order is unpredictable.
    EXPECT_THAT(expected.service, UnorderedElementsAreArray(serviceObserver.events()));
}

TEST_F(FileServiceTests, cloud_file_removed_when_removed_in_cloud)
{
    // What events do we expect to receive?
    FileEventVector expected;

    // Create a test file in the cloud.
    auto handle = mClient->upload(randomBytes(512), randomName(), mRootHandle);
    ASSERT_EQ(handle.errorOr(API_OK), API_OK);

    // Open our test file.
    auto file = mClient->fileOpen(*handle);
    ASSERT_EQ(file.errorOr(FILE_SERVICE_SUCCESS), FILE_SERVICE_SUCCESS);

    // So we can receive file events.
    auto fileObserver = observe(*file);
    auto serviceObserver = observe(mClient->fileService());

    // Remove the file from the cloud.
    ASSERT_EQ(mClient->remove(*handle), API_OK);

    expected.emplace_back(FileRemoveEvent{file->info().id(), false});

    // Make sure our file's been marked as removed.
    EXPECT_TRUE(waitFor(
        [&]()
        {
            return file->info().removed();
        },
        mDefaultTimeout));

    EXPECT_TRUE(file->info().removed());

    // And that we received a remove event.
    EXPECT_EQ(expected, fileObserver.events());
    EXPECT_EQ(expected, serviceObserver.events());
}

TEST_F(FileServiceTests, cloud_file_removed_when_replaced_by_cloud_add)
{
    // So we have a stable name.
    auto name = randomName();

    // Create a test file in the cloud.
    auto handle = mClient->upload(randomBytes(512), name, mRootHandle);
    ASSERT_EQ(handle.errorOr(API_OK), API_OK);

    // Open our test file.
    auto file = mClient->fileOpen(*handle);
    ASSERT_EQ(file.errorOr(FILE_SERVICE_SUCCESS), FILE_SERVICE_SUCCESS);

    // So we can receive file events.
    auto fileObserver = observe(*file);
    auto serviceObserver = observe(mClient->fileService());

    // Add a directory with the same name and parent as our file.
    auto directory = mClient->makeDirectory(name, mRootHandle);
    ASSERT_EQ(directory.errorOr(API_OK), API_OK);

    // Make sure our file's been marked as removed.
    EXPECT_TRUE(waitFor(
        [&]()
        {
            return file->info().removed();
        },
        mDefaultTimeout));

    EXPECT_TRUE(file->info().removed());

    // And that we received a remove event.
    FileEventVector expected;

    expected.emplace_back(FileRemoveEvent{file->info().id(), true});

    EXPECT_EQ(expected, fileObserver.events());
    EXPECT_EQ(expected, serviceObserver.events());
}

TEST_F(FileServiceTests, cloud_file_removed_when_replaced_by_new_version)
{
    // So we have a stable name.
    auto name = randomName();

    // Create an initial version of name in the cloud.
    auto handle0 = mClient->upload(randomBytes(512), name, mRootHandle);
    ASSERT_EQ(handle0.errorOr(API_OK), API_OK);

    // Open our file.
    auto file = mClient->fileOpen(*handle0);
    ASSERT_EQ(file.errorOr(FILE_SERVICE_SUCCESS), FILE_SERVICE_SUCCESS);

    // So we can receive file events.
    auto fileObserver = observe(*file);
    auto serviceObserver = observe(mClient->fileService());

    // Upload a new version of our file.
    auto handle1 = mClient->upload(randomBytes(512), name, mRootHandle);
    ASSERT_EQ(handle1.errorOr(API_OK), API_OK);

    // Make sure the client sees our new file.
    EXPECT_TRUE(waitFor(
        [&]()
        {
            return mClient->get(*handle1);
        },
        mDefaultTimeout));

    auto info0 = mClient->get(*handle0);
    auto info1 = mClient->get(*handle1);

    // For a better log message in Jenkins.
    EXPECT_EQ(info0.errorOr(API_OK), API_OK);
    EXPECT_EQ(info1.errorOr(API_OK), API_OK);

    if (HasFailure())
        return;

    // Make sure version relationship has been updated correctly.
    EXPECT_EQ(info0->mParentHandle, info1->mHandle);

    // Make sure our file has been marked as removed.
    EXPECT_TRUE(file->info().removed());

    // And that we received a remove event.
    FileEventVector expected;

    expected.emplace_back(FileRemoveEvent{file->info().id(), true});

    EXPECT_EQ(expected, fileObserver.events());
    EXPECT_EQ(expected, serviceObserver.events());
}

TEST_F(FileServiceTests, create_fails_when_file_already_exists)
{
    // Generate a name for a local file.
    auto name = randomName();

    // Create a local file for us to collide with.
    auto file0 = mClient->fileCreate(mRootHandle, name);
    ASSERT_EQ(file0.errorOr(FILE_SERVICE_SUCCESS), FILE_SERVICE_SUCCESS);

    // You can't create a file if it already exists in the cloud.
    auto file1 = mClient->fileCreate(mRootHandle, mFileName);
    ASSERT_EQ(file1.errorOr(FILE_SERVICE_SUCCESS), FILE_SERVICE_FILE_ALREADY_EXISTS);

    // You can't create a file if it already exists locally.
    file1 = mClient->fileCreate(mRootHandle, name);
    ASSERT_EQ(file1.errorOr(FILE_SERVICE_SUCCESS), FILE_SERVICE_FILE_ALREADY_EXISTS);
}

TEST_F(FileServiceTests, create_fails_when_name_is_empty)
{
    // Files must have a valid name.
    auto file = mClient->fileCreate(mRootHandle, std::string());
    ASSERT_EQ(file.errorOr(FILE_SERVICE_SUCCESS), FILE_SERVICE_INVALID_NAME);
}

TEST_F(FileServiceTests, create_fails_when_parent_doesnt_exist)
{
    // Files must have a valid parent.
    auto file = mClient->fileCreate(NodeHandle(), randomName());
    ASSERT_EQ(file.errorOr(FILE_SERVICE_SUCCESS), FILE_SERVICE_PARENT_DOESNT_EXIST);
}

TEST_F(FileServiceTests, create_fails_when_parent_is_a_file)
{
    // A file's parent must be a directory.
    auto file = mClient->fileCreate(mFileHandle, randomName());
    ASSERT_EQ(file.errorOr(FILE_SERVICE_SUCCESS), FILE_SERVICE_PARENT_IS_A_FILE);
}

TEST_F(FileServiceTests, create_flush_succeeds)
{
    // Generate a name for our file.
    auto name = randomName();

    // Create a new file.
    auto file = mClient->fileCreate(mRootHandle, name);

    // Make sure the file was created.
    ASSERT_EQ(file.errorOr(FILE_SERVICE_SUCCESS), FILE_SERVICE_SUCCESS);

    // Generate some data for us to write to the file.
    auto expected = randomBytes(128_KiB);

    // Write data to the file.
    ASSERT_EQ(execute(write, expected.data(), *file, 0, 128_KiB), FILE_SUCCESS);

    // Try and flush the file to the cloud.
    auto handle = [file = std::move(file)]() mutable -> FileResultOr<NodeHandle>
    {
        // What events do we expect to receive?
        FileEventVector wanted;

        // So we can receive file events.
        auto fileObserver = observe(*file);
        auto serviceObserver = observe(mClient->fileService());

        // Convenience.
        auto info = file->info();

        // Latch the file's access and modification time.
        auto accessed = info.accessed();
        auto modified = info.modified();

        // Try and flush the file to the cloud.
        auto result = execute(flush, *file);

        // Couldn't flush the file to the cloud.
        if (result != FILE_SUCCESS)
            return unexpected(result);

        // Make sure the file's access time has been bumped.
        EXPECT_GE(info.accessed(), accessed);

        // And that the file's modification time is unchanged.
        EXPECT_EQ(info.modified(), modified);

        // Make sure we received the events we expected.
        wanted.emplace_back(FileFlushEvent{info.handle(), info.id()});

        EXPECT_EQ(fileObserver.events(), wanted);
        EXPECT_EQ(serviceObserver.events(), wanted);

        // One or more of our expectations failed.
        if (HasFailure())
            return unexpected(FILE_FAILED);

        // Return the file's handle.
        return file->info().handle();
    }();

    // Make sure we were able to flush the file to the cloud.
    ASSERT_EQ(handle.errorOr(FILE_SUCCESS), FILE_SUCCESS);

    // Check that the file exists in the cloud.
    auto node = mClient->get(mRootHandle, name);
    ASSERT_EQ(node.errorOr(API_OK), API_OK);
    ASSERT_EQ(node->mHandle, *handle);

    // Reopen the file.
    file = mClient->fileOpen(*handle);

    // Make sure the file was opened successfully.
    ASSERT_EQ(file.errorOr(FILE_SERVICE_SUCCESS), FILE_SERVICE_SUCCESS);

    // Make sure the data we uploaded was the data we wrote.
    auto computed = execute(read, *file, 0, 128_KiB);

    ASSERT_EQ(computed.errorOr(FILE_SUCCESS), FILE_SUCCESS);
    ASSERT_FALSE(computed->compare(expected));
}

TEST_F(FileServiceTests, create_succeeds)
{
    FileID id0;

    // Create a file and latch its ID.
    {
        // Try and create a file.
        auto file = mClient->fileCreate(mRootHandle, randomName());

        // Make sure the file was created.
        ASSERT_EQ(file.errorOr(FILE_SERVICE_SUCCESS), FILE_SERVICE_SUCCESS);

        // Make sure we can get information about the file.
        auto info0 = file->info();
        auto info1 = mClient->fileInfo(info0.id());

        ASSERT_EQ(info1.errorOr(FILE_SERVICE_SUCCESS), FILE_SERVICE_SUCCESS);
        ASSERT_EQ(info0, *info1);

        // Make sure the file isn't associated with any node.
        EXPECT_TRUE(info0.handle().isUndef());

        // And that the file's empty.
        EXPECT_EQ(info0.size(), 0u);

        // And that its access and modification time have been set.
        EXPECT_EQ(info0.accessed(), info0.modified());

        // Latch the file's ID.
        id0 = info0.id();

        // Remove the file from storage.
        ASSERT_EQ(execute(remove, *file), FILE_SUCCESS);
    }

    // Make sure the file's been purged from storage.
    auto info = mClient->fileInfo(id0);
    ASSERT_EQ(info.errorOr(FILE_SERVICE_SUCCESS), FILE_SERVICE_UNKNOWN_FILE);

    // Try and create a new file.
    auto file1 = mClient->fileCreate(mRootHandle, randomName());
    ASSERT_EQ(file1.errorOr(FILE_SERVICE_SUCCESS), FILE_SERVICE_SUCCESS);

    // Make sure our original file's ID was recycled.
    EXPECT_EQ(file1->info().id(), id0);

    // Create a new file.
    auto file2 = mClient->fileCreate(mRootHandle, randomName());
    ASSERT_EQ(file2.errorOr(FILE_SERVICE_SUCCESS), FILE_SERVICE_SUCCESS);

    // Make sure it has a newly generated ID.
    EXPECT_NE(file2->info().id(), id0);
}

TEST_F(FileServiceTests, create_write_succeeds)
{
    // Disable readahead.
    mClient->fileService().options(DisableReadahead);

    // Create a new file.
    auto file = mClient->fileCreate(mRootHandle, randomName());

    // Make sure the file was created.
    ASSERT_EQ(file.errorOr(FILE_SERVICE_SUCCESS), FILE_SERVICE_SUCCESS);

    // Events we expect to receive.
    FileEventVector expected;

    // So we can track what events were emitted for our file.
    auto fileObserver = observe(*file);
    auto serviceObserver = observe(mClient->fileService());

    // Generate some data for us to write to the file.
    auto data = randomBytes(64_KiB);

    // Try and write data to the file.
    ASSERT_EQ(execute(write, data.data(), *file, 128_KiB, 64_KiB), FILE_SUCCESS);

    expected.emplace_back(FileWriteEvent{FileRange(128_KiB, 192_KiB), file->info().id()});

    // Make sure the file's size is correct.
    ASSERT_EQ(file->info().size(), 192_KiB);

    // The file should have one range starting from the beginning of the file.
    ASSERT_THAT(file->ranges(), ElementsAre(FileRange(0, 192_KiB)));

    // All data before what we wrote should be zeroed.
    auto computed = execute(read, *file, 0, 128_KiB);

    // Make sure the read succeeded.
    ASSERT_EQ(computed.errorOr(FILE_SUCCESS), FILE_SUCCESS);

    // Make sure the data we read is nothing but zeroes.
    ASSERT_EQ(computed->find_first_not_of('\0'), std::string::npos);

    // We should be able to read back what we wrote.
    computed = execute(read, *file, 128_KiB, 64_KiB);

    ASSERT_EQ(computed.errorOr(FILE_SUCCESS), FILE_SUCCESS);
    ASSERT_EQ(data, *computed);

    // Write more data to the file.
    ASSERT_EQ(execute(write, data.data(), *file, 320_KiB, 64_KiB), FILE_SUCCESS);

    expected.emplace_back(FileWriteEvent{FileRange(320_KiB, 384_KiB), file->info().id()});

    // Make sure the file's size is correct.
    ASSERT_EQ(file->info().size(), 384_KiB);

    // We should still have a single range.
    ASSERT_THAT(file->ranges(), ElementsAre(FileRange(0, 384_KiB)));

    // We should be able to read back what we wrote.
    computed = execute(read, *file, 320_KiB, 64_KiB);

    ASSERT_EQ(computed.errorOr(FILE_SUCCESS), FILE_SUCCESS);
    ASSERT_EQ(data, *computed);

    // Make sure we received the events we were expecting.
    ASSERT_EQ(expected, fileObserver.events());
    ASSERT_EQ(expected, serviceObserver.events());
}

TEST_F(FileServiceTests, fetch_succeeds)
{
    // Disable readahead.
    mClient->fileService().options(DisableReadahead);

    // Open a file for reading.
    auto file = mClient->fileOpen(mFileHandle);

    // Make sure we could open the file.
    ASSERT_EQ(file.errorOr(FILE_SERVICE_SUCCESS), FILE_SERVICE_SUCCESS);

    // Read some ranges from the file.
    ASSERT_EQ(execute(read, *file, 256_KiB, 256_KiB).errorOr(FILE_SUCCESS), FILE_SUCCESS);
    ASSERT_EQ(execute(read, *file, 768_KiB, 128_KiB).errorOr(FILE_SUCCESS), FILE_SUCCESS);

    // Reads shouldn't dirty a file.
    ASSERT_FALSE(file->info().dirty());

    // Make sure two ranges are active.
    ASSERT_THAT(file->ranges(),
                ElementsAre(FileRange(256_KiB, 512_KiB), FileRange(768_KiB, 896_KiB)));

    // Try and fetch the rest of the file's content.
    ASSERT_EQ(execute(fetch, *file), FILE_SUCCESS);

    // Fetching shouldn't dirty a file.
    ASSERT_FALSE(file->info().dirty());

    // We should now have a single range.
    ASSERT_THAT(file->ranges(), ElementsAre(FileRange(0, 1_MiB)));
}

TEST_F(FileServiceTests, flush_cancel_on_client_logout_succeeds)
{
    // Create a client that we can safely logout.
    auto client = CreateClient("file_service_" + randomName());
    ASSERT_TRUE(client);

    // Log the client in.
    ASSERT_EQ(client->login(0), API_OK);

    // Upload content so we have a file we can safely mess with.
    auto handle = client->upload(randomBytes(512_KiB), randomName(), "/z");
    ASSERT_EQ(handle.errorOr(API_OK), API_OK);

    // Open the file so we can modify it.
    auto file = client->fileOpen(*handle);
    ASSERT_EQ(file.errorOr(FILE_SERVICE_SUCCESS), FILE_SERVICE_SUCCESS);

    // Truncate the file so it's considered modified.
    ASSERT_EQ(execute(truncate, *file, 256_KiB), FILE_SUCCESS);

    // Retrieve the file's remaining content.
    ASSERT_EQ(execute(fetch, *file), FILE_SUCCESS);

    // Make sure the upload doesn't complete before we logout the client.
    client->setUploadSpeed(4096);

    // Try and flush our local changes.
    auto waiter = flush(std::move(*file));

    // Log out the client.
    EXPECT_EQ(client->logout(true), API_OK);

    // Wait for the flush to complete.
    ASSERT_NE(waiter.wait_for(mDefaultTimeout), timeout);

    // Make sure the flush was cancelled.
    ASSERT_EQ(waiter.get(), FILE_CANCELLED);
}

TEST_F(FileServiceTests, flush_cancel_on_file_destruction_succeeds)
{
    // Upload content so we have a file we can safely mess with.
    auto handle = mClient->upload(randomBytes(512_KiB), randomName(), "/z");
    ASSERT_EQ(handle.errorOr(API_OK), API_OK);

    // Open the file so we can modify it.
    auto file = mClient->fileOpen(*handle);
    ASSERT_EQ(file.errorOr(FILE_SERVICE_SUCCESS), FILE_SERVICE_SUCCESS);

    // Initiate truncate and fetch requests.
    auto truncated = truncate(*file, 256_KiB);
    auto fetched = fetch(*file);

    // Make sure our truncate request completed succesfully.
    ASSERT_NE(truncated.wait_for(mDefaultTimeout), timeout);
    ASSERT_EQ(truncated.get(), FILE_SUCCESS);

    // Make sure our fetch request completed successfully.
    ASSERT_NE(fetched.wait_for(mDefaultTimeout), timeout);
    ASSERT_EQ(fetched.get(), FILE_SUCCESS);

    // Make sure the upload doesn't complete before we can the file.
    mClient->setUploadSpeed(4096);

    // Flush the file.
    auto waiter = [](File file)
    {
        // So we can wait for the request to complete.
        auto notifier = makeSharedPromise<FileResult>();

        // Try and flush our changes to the cloud.
        file.flush(
            [notifier](FileResult result)
            {
                return notifier->set_value(result);
            });

        // Return waiter to our caller.
        return notifier->get_future();
    }(std::move(*file));

    // Wait for the flush to complete.
    ASSERT_NE(waiter.wait_for(mDefaultTimeout), timeout);

    // Make sure the flush was cancelled.
    ASSERT_EQ(waiter.get(), FILE_CANCELLED);
}

TEST_F(FileServiceTests, flush_removed_file_fails)
{
    // Create a file we can safely remove.
    auto handle = mClient->upload(randomBytes(512_KiB), randomName(), mRootHandle);

    // Make sure we could create our file.
    ASSERT_EQ(handle.errorOr(API_OK), API_OK);

    // Open the file.
    auto file = mClient->fileOpen(*handle);

    // Make sure we could open the file.
    ASSERT_EQ(file.errorOr(FILE_SERVICE_SUCCESS), FILE_SERVICE_SUCCESS);

    // Retrieve the file's data.
    ASSERT_EQ(execute(fetch, *file), FILE_SUCCESS);

    // Remove the file from the cloud.
    ASSERT_EQ(mClient->remove(*handle), API_OK);

    // Touch the file so that it has been modified.
    ASSERT_EQ(execute(touch, *file, now() + 1), FILE_SUCCESS);

    // You shouldn't be able to flush a file that has been removed.
    ASSERT_EQ(execute(flush, *file), FILE_REMOVED);
}

TEST_F(FileServiceTests, flush_succeeds)
{
    // Generate content for us to mutate.
    auto initial = randomBytes(512_KiB);

    // Upload content so we have a file we can safely mess with.
    auto oldHandle = mClient->upload(initial, randomName(), "/z");
    ASSERT_EQ(oldHandle.errorOr(API_OK), API_OK);

    // Open the file we uploaded.
    auto oldFile = mClient->fileOpen(*oldHandle);
    ASSERT_EQ(oldFile.errorOr(FILE_SERVICE_SUCCESS), FILE_SERVICE_SUCCESS);

    // Generate some content for us to write to the file.
    auto content = randomBytes(128_KiB);

    // Write content to our file.
    ASSERT_EQ(execute(write, content.data(), *oldFile, 128_KiB, 128_KiB), FILE_SUCCESS);
    ASSERT_EQ(execute(write, content.data(), *oldFile, 384_KiB, 128_KiB), FILE_SUCCESS);

    // Make sure the file's been marked as dirty.
    ASSERT_TRUE(oldFile->info().dirty());

    // Keep track of the file's expected content.
    auto expected = initial;

    expected.replace(128_KiB, 128_KiB, content);
    expected.replace(384_KiB, 128_KiB, content);

    // Flush our modifications to the cloud.
    {
        // So we can receive file events.
        auto fileObserver = observe(*oldFile);
        auto serviceObserver = observe(mClient->fileService());

        // The file events we expect to receive.
        FileEventVector wanted;

        // Latch the file's ID.
        auto id = oldFile->info().id();

        // Flush our modifications to the cloud.
        ASSERT_EQ(execute(flush, *oldFile), FILE_SUCCESS);

        // Make sure the file's ID hasn't changed.
        ASSERT_EQ(oldFile->info().id(), id);

        // Make sure the file's no longer marked as dirty.
        ASSERT_FALSE(oldFile->info().dirty());

        // Make sure we received a flush event.
        wanted.emplace_back(FileFlushEvent{oldFile->info().handle(), id});

        EXPECT_EQ(fileObserver.events(), wanted);
        EXPECT_EQ(serviceObserver.events(), wanted);
    }

    // Latch the file's new handle.
    auto newHandle = oldFile->info().handle();

    // Make sure the file's handle has changed.
    ASSERT_NE(newHandle, *oldHandle);

    // Make sure our updated file's in the cloud.
    EXPECT_TRUE(waitFor(
        [&]()
        {
            return mClient->get(newHandle);
        },
        mDefaultTimeout));

    // Better log message in Jenkins.
    EXPECT_EQ(mClient->get(newHandle).errorOr(API_OK), API_OK);

    if (HasFailure())
        return;

    // Make sure the file hasn't been marked as removed.
    ASSERT_FALSE(oldFile->info().removed());

    // Make sure we can get information about the file using its new handle.
    {
        auto info = mClient->fileInfo(newHandle);
        ASSERT_EQ(info.errorOr(FILE_SERVICE_SUCCESS), FILE_SERVICE_SUCCESS);
        ASSERT_EQ(info->id(), oldFile->info().id());
    }

    // Remove the file from storage.
    ASSERT_EQ(execute(purge, std::move(*oldFile)), FILE_SUCCESS);

    // newHandle and oldHandle now represent distinct files.
    auto newFile = mClient->fileOpen(newHandle);
    ASSERT_EQ(newFile.errorOr(FILE_SERVICE_SUCCESS), FILE_SERVICE_SUCCESS);

    oldFile = mClient->fileOpen(*oldHandle);
    ASSERT_EQ(oldFile.errorOr(FILE_SERVICE_SUCCESS), FILE_SERVICE_SUCCESS);

    // Make sure newFile and oldFile are distinct.
    ASSERT_NE(newFile->info().id(), oldFile->info().id());

    // Make sure oldFile's content is unchanged.
    auto computed = execute(read, *oldFile, 0, 512_KiB);
    ASSERT_EQ(computed.errorOr(FILE_SUCCESS), FILE_SUCCESS);
    ASSERT_EQ(computed, initial);

    // Make sure newFile's content includes our changes.
    computed = execute(read, *newFile, 0, 512_KiB);
    ASSERT_EQ(computed.errorOr(FILE_SUCCESS), FILE_SUCCESS);
    ASSERT_EQ(computed, expected);

    // Touch the file so that it's considered modified.
    ASSERT_EQ(execute(touch, *newFile, newFile->info().modified() - 1), FILE_SUCCESS);

    // Disable versioning.
    //
    // The reason for this is that when we upload a new version of our file,
    // the original version will first be removed and we want to make sure
    // the node event logic properly handles this situation.
    mClient->useVersioning(false);

    // What events do we expect to receive?
    FileEventVector wanted;

    // So we can receive file events.
    auto fileObserver = observe(*newFile);
    auto serviceObserver = observe(mClient->fileService());

    // Flush our file to the cloud.
    ASSERT_EQ(execute(flush, *newFile), FILE_SUCCESS);

    // Make sure our file's handle has changed.
    oldHandle = newHandle;
    newHandle = newFile->info().handle();

    ASSERT_NE(oldHandle, newHandle);

    // Make sure we received a flush event.
    wanted.emplace_back(FileFlushEvent{newHandle, newFile->info().id()});

    EXPECT_EQ(fileObserver.events(), wanted);
    EXPECT_EQ(serviceObserver.events(), wanted);

    // Make sure our updated file is in the cloud.
    EXPECT_TRUE(waitFor(
        [&]()
        {
            return mClient->get(newHandle);
        },
        mDefaultTimeout));

    EXPECT_EQ(mClient->get(newHandle).errorOr(API_OK), API_OK);

    if (HasFailure())
        return;

    // Make sure our file hasn't been marked as removed.
    ASSERT_FALSE(newFile->info().removed());
}

TEST_F(FileServiceTests, foreign_files_are_read_only)
{
    // Create a foreign client.
    auto client = CreateClient("file_service_" + randomName());
    ASSERT_TRUE(client);

    // Log the client in.
    ASSERT_EQ(client->login(1), API_OK);

    // Get the public link for mClient's test file.
    auto link = mClient->getPublicLink(mFileHandle);
    ASSERT_EQ(link.errorOr(API_OK), API_OK);

    // Add mClient's test file to client.
    auto id = client->fileAdd(*link);
    ASSERT_EQ(id.errorOr(FILE_SERVICE_SUCCESS), FILE_SERVICE_SUCCESS);

    // Open the added file.
    auto file = client->fileOpen(*id);
    ASSERT_EQ(file.errorOr(FILE_SERVICE_SUCCESS), FILE_SERVICE_SUCCESS);

    // Generate some data for us to write to the file.
    auto data = randomBytes(512);

    // Convenience.
    auto info = [info = file->info()]()
    {
        return std::make_tuple(info.dirty(), info.modified(), info.size());
    }; // info

    // Latch the file's modification time and size.
    auto before = info();

    // You shouldn't be able to append data to a foreign file.
    EXPECT_EQ(execute(append, data.data(), *file, data.size()), FILE_READONLY);

    // Make sure the file's information hasn't changed.
    EXPECT_EQ(before, info());

    // You shouldn't be able to write data to a foreign file.
    EXPECT_EQ(execute(write, data.data(), *file, 0, data.size()), FILE_READONLY);

    // Make sure the file's information hasn't changed.
    EXPECT_EQ(before, info());

    // You shouldn't be able to touch a foreign file.
    EXPECT_EQ(execute(touch, *file, 0l), FILE_READONLY);

    // Make sure the file's information hasn't changed.
    EXPECT_EQ(before, info());

    // You shouldn't be able to truncate a foreign file.
    EXPECT_EQ(execute(truncate, *file, 0ul), FILE_READONLY);

    // Make sure the file's information hasn't changed.
    EXPECT_EQ(before, info());

    // You should be able to flush a file but it'll be a no-op.
    EXPECT_EQ(execute(flush, *file), FILE_SUCCESS);
}

TEST_F(FileServiceTests, inactive_file_moved)
{
    // For later reference.
    auto name0 = randomName();

    // Create a file in the cloud that we can move freely.
    auto handle = mClient->upload(randomBytes(512), name0, mRootHandle);
    ASSERT_EQ(handle.errorOr(API_OK), API_OK);

    // Open the file so that the service is aware of it.
    ASSERT_EQ(mClient->fileOpen(*handle).errorOr(FILE_SERVICE_SUCCESS), FILE_SERVICE_SUCCESS);

    // So we can receive file events.
    auto observer = observe(mClient->fileService());

    auto name1 = randomName();

    // Move the file in the cloud.
    ASSERT_EQ(mClient->move(name1, *handle, mRootHandle), API_OK);

    // Wait for the client to recognize the move.
    EXPECT_TRUE(waitFor(
        [&]()
        {
            return !mClient->get(mRootHandle, name0) && mClient->get(mRootHandle, name1);
        },
        mDefaultTimeout));

    EXPECT_EQ(mClient->get(mRootHandle, name0).errorOr(API_OK), API_FUSE_ENOTFOUND);
    EXPECT_EQ(mClient->get(mRootHandle, name1).errorOr(API_OK), API_OK);

    if (HasFailure())
        return;

    // Reopen the file.
    auto file = mClient->fileOpen(*handle);
    ASSERT_EQ(file.errorOr(FILE_SERVICE_SUCCESS), FILE_SERVICE_SUCCESS);

    // Make sure the file's location has been updated.
    auto location = file->info().location();
    ASSERT_TRUE(location);

    EXPECT_EQ(location->mName, name1);
    EXPECT_EQ(location->mParentHandle, mRootHandle);

    // And that we received a move event.
    FileEventVector expected;

    expected.emplace_back(FileMoveEvent{FileLocation{name0, mRootHandle},
                                        FileLocation{name1, mRootHandle},
                                        FileID::from(*handle)});

    EXPECT_EQ(expected, observer.events());
}

TEST_F(FileServiceTests, inactive_file_removed)
{
    // Create a file that we can remove.
    auto handle = mClient->upload(randomBytes(512), randomName(), mRootHandle);
    ASSERT_EQ(handle.errorOr(API_OK), API_OK);

    // Open the file so that the service is aware of it.
    ASSERT_EQ(mClient->fileOpen(*handle).errorOr(FILE_SERVICE_SUCCESS), FILE_SERVICE_SUCCESS);

    // So we can receive file events.
    auto observer = observe(mClient->fileService());

    // Remove the file from the cloud.
    ASSERT_EQ(mClient->remove(*handle), API_OK);

    // Wait for the client to realize the file's been removed.
    EXPECT_TRUE(waitFor(
        [&]()
        {
            return mClient->get(*handle).errorOr(API_OK) == API_ENOENT;
        },
        mDefaultTimeout));

    EXPECT_EQ(mClient->get(*handle).errorOr(API_OK), API_ENOENT);

    if (HasFailure())
        return;

    // Make sure we received a removed event.
    FileEventVector expected;

    expected.emplace_back(FileRemoveEvent{FileID::from(*handle), false});

    EXPECT_EQ(expected, observer.events());
}

TEST_F(FileServiceTests, inactive_file_replaced)
{
    // For later reference.
    auto name0 = randomName();
    auto name1 = randomName();

    // Create a file that we can move.
    auto handle = mClient->upload(randomBytes(512), name0, mRootHandle);
    ASSERT_EQ(handle.errorOr(API_OK), API_OK);

    FileID id;

    // Get the ID of a new local file.
    {
        // Create a new local file.
        auto file = mClient->fileCreate(mRootHandle, name1);
        ASSERT_EQ(file.errorOr(FILE_SERVICE_SUCCESS), FILE_SERVICE_SUCCESS);

        // Latch the new file's ID.
        id = file->info().id();
    }

    // So we can receive file events.
    auto observer = observe(mClient->fileService());

    // Move our cloud file such that it replaces our inactive local file.
    ASSERT_EQ(mClient->move(name1, *handle, mRootHandle), API_OK);

    // Wait for the client to recognize the move.
    EXPECT_TRUE(waitFor(
        [&]()
        {
            return !mClient->get(mRootHandle, name0) && mClient->get(mRootHandle, name1);
        },
        mDefaultTimeout));

    EXPECT_EQ(mClient->get(mRootHandle, name0).errorOr(API_OK), API_FUSE_ENOTFOUND);
    EXPECT_EQ(mClient->get(mRootHandle, name1).errorOr(API_OK), API_OK);

    // Make sure we received a remove event for our inactive file.
    FileEventVector expected;

    expected.emplace_back(FileRemoveEvent{id, true});

    EXPECT_EQ(expected, observer.events());
}

TEST_F(FileServiceTests, info_directory_fails)
{
    // Can't get info about a directory.
    EXPECT_EQ(mClient->fileInfo("/z").errorOr(FILE_SERVICE_SUCCESS), FILE_SERVICE_UNKNOWN_FILE);
}

TEST_F(FileServiceTests, info_unknown_fails)
{
    // Can't get info about a file the service isn't managing.
    EXPECT_EQ(mClient->fileInfo(mFileHandle).errorOr(FILE_SERVICE_SUCCESS),
              FILE_SERVICE_UNKNOWN_FILE);
}

TEST_F(FileServiceTests, info_succeeds)
{
    // Convenience.
    using std::chrono::seconds;

    // Open our test file.
    auto file = mClient->fileOpen(mFileHandle);

    // Make sure we could open the file.
    ASSERT_EQ(file.errorOr(FILE_SERVICE_SUCCESS), FILE_SERVICE_SUCCESS);

    // Latch the file's access time.
    auto accessed = file->info().accessed();

    // Move time forward.
    std::this_thread::sleep_for(seconds(1));

    // Get our hands on the file's information.
    auto info = mClient->fileInfo(mFileHandle);

    // Make sure we could get our hands on the file's information.
    ASSERT_EQ(info.errorOr(FILE_SERVICE_SUCCESS), FILE_SERVICE_SUCCESS);

    // Make sure the file's access time hasn't changed.
    ASSERT_EQ(info->accessed(), accessed);
}

TEST_F(FileServiceTests, local_file_removed_when_parent_removed)
{
    // Create a directory tree for us to play with.
    auto d0 = mClient->makeDirectory(randomName(), mRootHandle);
    ASSERT_EQ(d0.errorOr(API_OK), API_OK);

    auto d1 = mClient->makeDirectory(randomName(), *d0);
    ASSERT_EQ(d1.errorOr(API_OK), API_OK);

    // Make sure the directories are visible to our client.
    EXPECT_TRUE(waitFor(
        [&]()
        {
            return mClient->get(*d0) && mClient->get(*d1);
        },
        mDefaultTimeout));

    // For better log messages on Jenkins.
    EXPECT_EQ(mClient->get(*d0).errorOr(API_OK), API_OK);
    EXPECT_EQ(mClient->get(*d1).errorOr(API_OK), API_OK);

    if (HasFailure())
        return;

    // Create a couple test files.
    auto d0f = mClient->fileCreate(*d0, randomName());
    ASSERT_EQ(d0f.errorOr(FILE_SERVICE_SUCCESS), FILE_SERVICE_SUCCESS);

    auto d1f = mClient->fileCreate(*d1, randomName());
    ASSERT_EQ(d1f.errorOr(FILE_SERVICE_SUCCESS), FILE_SERVICE_SUCCESS);

    // So we can receive file events.
    auto fileObserver0 = observe(*d0f);
    auto fileObserver1 = observe(*d1f);
    auto serviceObserver = observe(mClient->fileService());

    // Remove d0 and by proxy, d0f, d1 and d1f.
    ASSERT_EQ(mClient->remove(*d0), API_OK);

    // Make sure the directories are no longer visible to our client.
    EXPECT_TRUE(waitFor(
        [&]()
        {
            return !mClient->get(*d0) && !mClient->get(*d1);
        },
        mDefaultTimeout));

    // Better log messages.
    EXPECT_EQ(mClient->get(*d0).errorOr(API_OK), API_ENOENT);
    EXPECT_EQ(mClient->get(*d1).errorOr(API_OK), API_ENOENT);

    if (HasFailure())
        return;

    // Make sure our files have been marked as removed.
    EXPECT_TRUE(d0f->info().removed());
    EXPECT_TRUE(d1f->info().removed());

    // And that we received remove events.
    FileEventVector expected;

    expected.emplace_back(FileRemoveEvent{d0f->info().id(), false});
    EXPECT_EQ(expected, fileObserver0.events());

    expected.emplace_back(FileRemoveEvent{d1f->info().id(), false});

    // UnorderedElementsAreArray(...) necessary as order is unpredictable.
    EXPECT_THAT(expected, UnorderedElementsAreArray(serviceObserver.events()));

    expected.erase(expected.begin());
    EXPECT_EQ(expected, fileObserver1.events());
}

TEST_F(FileServiceTests, local_file_removed_when_replaced_by_cloud_add)
{
    // Generate a name for our file.
    auto name = randomName();

    // Create a local file.
    auto file = mClient->fileCreate(mRootHandle, name);
    ASSERT_EQ(file.errorOr(FILE_SERVICE_SUCCESS), FILE_SERVICE_SUCCESS);

    // So we can receive file events.
    auto fileObserver = observe(*file);
    auto serviceObserver = observe(mClient->fileService());

    // Add a directory with the same name and parent as our file.
    auto directory = mClient->makeDirectory(name, mRootHandle);
    ASSERT_EQ(directory.errorOr(API_OK), API_OK);

    // Make sure our client's aware of the new directory.
    EXPECT_TRUE(waitFor(
        [&]()
        {
            return mClient->get(*directory);
        },
        mDefaultTimeout));

    // For better messages on Jenkins.
    EXPECT_EQ(mClient->get(*directory).errorOr(API_OK), API_OK);

    if (HasFailure())
        return;

    // Make sure our file has been marked as removed.
    ASSERT_TRUE(file->info().removed());

    // And that we received a remove event.
    FileEventVector expected;

    expected.emplace_back(FileRemoveEvent{file->info().id(), true});

    EXPECT_EQ(expected, fileObserver.events());
    EXPECT_EQ(expected, serviceObserver.events());
}

TEST_F(FileServiceTests, local_file_removed_when_replaced_by_cloud_move)
{
    // So we have a stable name.
    auto fileName0 = randomName();

    // Create a local test file.
    auto file0 = mClient->fileCreate(mRootHandle, fileName0);
    ASSERT_EQ(file0.errorOr(FILE_SERVICE_SUCCESS), FILE_SERVICE_SUCCESS);

    // As above.
    auto fileName1 = randomName();

    // Add a new file to the cloud.
    auto handle0 = mClient->upload(randomBytes(512), fileName1, mRootHandle);
    ASSERT_EQ(handle0.errorOr(API_OK), API_OK);

    // Open the file.
    auto file1 = mClient->fileOpen(*handle0);
    ASSERT_EQ(file1.errorOr(FILE_SERVICE_SUCCESS), FILE_SERVICE_SUCCESS);

    // So we can receive file events.
    auto fileObserver0 = observe(*file0);
    auto fileObserver1 = observe(*file1);
    auto serviceObserver = observe(mClient->fileService());

    // Move file1 so it replaces file0.
    ASSERT_EQ(mClient->move(fileName0, *handle0, mRootHandle), API_OK);

    // Make sure the client recognizes the move.
    EXPECT_TRUE(waitFor(
        [&]()
        {
            return !mClient->get(mRootHandle, fileName1) && mClient->get(mRootHandle, fileName0);
        },
        mDefaultTimeout));

    EXPECT_EQ(mClient->get(mRootHandle, fileName1).errorOr(API_OK), API_FUSE_ENOTFOUND);
    EXPECT_EQ(mClient->get(mRootHandle, fileName0).errorOr(API_OK), API_OK);

    if (HasFailure())
        return;

    // Make sure file0 has been marked as removed.
    EXPECT_TRUE(file0->info().removed());

    // And that we received a remove event.
    struct
    {
        FileEventVector file0;
        FileEventVector file1;
        FileEventVector service;
    } expected;

    expected.file0.emplace_back(FileRemoveEvent{file0->info().id(), true});

    expected.file1.emplace_back(FileMoveEvent{FileLocation{fileName1, mRootHandle},
                                              FileLocation{fileName0, mRootHandle},
                                              file1->info().id()});

    expected.service.emplace_back(expected.file0.back());
    expected.service.emplace_back(expected.file1.back());

    EXPECT_EQ(expected.file0, fileObserver0.events());
    EXPECT_EQ(expected.file1, fileObserver1.events());
    EXPECT_EQ(expected.service, serviceObserver.events());
}

TEST_F(FileServiceTests, location_updated_when_moved_in_cloud)
{
    // Generate a name for our file.
    auto name = randomName();

    // Upload a file that we can move.
    auto handle = mClient->upload(randomBytes(512), name, mRootHandle);

    // Make sure we could upload the file.
    ASSERT_EQ(handle.errorOr(API_OK), API_OK);

    // Try and open the file.
    auto file = mClient->fileOpen(*handle);
    ASSERT_EQ(file.errorOr(FILE_SERVICE_SUCCESS), FILE_SERVICE_SUCCESS);

    // So we can receive file events.
    auto fileObserver = observe(*file);
    auto serviceObserver = observe(mClient->fileService());

    // Make sure we could open the file.
    ASSERT_EQ(file.errorOr(FILE_SERVICE_SUCCESS), FILE_SERVICE_SUCCESS);

    // Make sure the file's location is correct.
    auto location = file->info().location();
    ASSERT_TRUE(location);

    EXPECT_EQ(location->mName, name);
    ASSERT_EQ(location->mParentHandle, mRootHandle);

    // Expected new location.
    FileLocation newLocation{randomName(), mRootHandle};

    // Sanity.
    ASSERT_NE(location, newLocation);

    // Move the file in the cloud.
    ASSERT_EQ(mClient->move(newLocation.mName, *handle, mRootHandle), API_OK);

    // Our file's location should change.
    EXPECT_TRUE(waitFor(
        [&]()
        {
            return file->info().location() == newLocation;
        },
        mDefaultTimeout));

    // Make sure the file's location has updated.
    EXPECT_EQ(file->info().location(), newLocation);

    // And that we received a move event.
    FileEventVector expected;

    expected.emplace_back(
        FileMoveEvent{std::move(*location), std::move(newLocation), file->info().id()});

    EXPECT_EQ(expected, fileObserver.events());
    EXPECT_EQ(expected, serviceObserver.events());
}

TEST_F(FileServiceTests, open_by_path_fails_when_file_is_a_directory)
{
    EXPECT_EQ(mClient->fileOpen("/", "z").errorOr(FILE_SERVICE_SUCCESS),
              FILE_SERVICE_FILE_IS_A_DIRECTORY);
}

TEST_F(FileServiceTests, open_by_path_fails_when_file_is_unknown)
{
    EXPECT_EQ(mClient->fileOpen("/z", "q").errorOr(FILE_SERVICE_SUCCESS),
              FILE_SERVICE_FILE_DOESNT_EXIST);
}

TEST_F(FileServiceTests, open_by_path_fails_when_name_is_empty)
{
    EXPECT_EQ(mClient->fileOpen("/z", "").errorOr(FILE_SERVICE_SUCCESS), FILE_SERVICE_INVALID_NAME);
}

TEST_F(FileServiceTests, open_by_path_fails_when_parent_is_a_file)
{
    EXPECT_EQ(mClient->fileOpen(mFileHandle, "x").errorOr(FILE_SERVICE_SUCCESS),
              FILE_SERVICE_PARENT_IS_A_FILE);
}

TEST_F(FileServiceTests, open_by_path_fails_when_parent_is_unknown)
{
    EXPECT_EQ(mClient->fileOpen("/bogus", "x").errorOr(FILE_SERVICE_SUCCESS),
              FILE_SERVICE_PARENT_DOESNT_EXIST);
}

TEST_F(FileServiceTests, open_by_path_succeeds)
{
    auto file = mClient->fileOpen("/z", mFileName);
    ASSERT_EQ(file.errorOr(FILE_SERVICE_SUCCESS), FILE_SERVICE_SUCCESS);
    EXPECT_EQ(file->info().handle(), mFileHandle);
}

TEST_F(FileServiceTests, open_directory_fails)
{
    // Can't open a directory.
    EXPECT_EQ(mClient->fileOpen("/z").errorOr(FILE_SERVICE_SUCCESS),
              FILE_SERVICE_FILE_IS_A_DIRECTORY);
}

TEST_F(FileServiceTests, open_file_succeeds)
{
    // We should be able to open a file.
    auto file = mClient->fileOpen(mFileHandle);
    EXPECT_EQ(file.errorOr(FILE_SERVICE_SUCCESS), FILE_SERVICE_SUCCESS);

    // We should be able to get information about that file.
    auto fileInfo = mClient->fileInfo(mFileHandle);
    EXPECT_EQ(fileInfo.errorOr(FILE_SERVICE_SUCCESS), FILE_SERVICE_SUCCESS);

    // Files are initially clean.
    ASSERT_FALSE(fileInfo->dirty());

    // Get our hands on the node's information.
    auto nodeInfo = mClient->get(mFileHandle);
    ASSERT_EQ(nodeInfo.errorOr(API_OK), API_OK);

    // Make sure the file's information matches the node's.
    EXPECT_EQ(fileInfo->id(), FileID::from(mFileHandle));
    EXPECT_EQ(fileInfo->modified(), nodeInfo->mModified);
    EXPECT_EQ(fileInfo->size(), static_cast<std::uint64_t>(nodeInfo->mSize));
}

TEST_F(FileServiceTests, open_unknown_fails)
{
    // Can't open a file that doesn't exist.
    EXPECT_EQ(mClient->fileOpen("/bogus").errorOr(FILE_SERVICE_SUCCESS),
              FILE_SERVICE_FILE_DOESNT_EXIST);
}

TEST_F(FileServiceTests, purge_foreign_file_succeeds)
{
    // Create a foreign client.
    auto client = CreateClient("file_service_" + randomName());
    ASSERT_TRUE(client);

    // Log in the client.
    ASSERT_EQ(client->login(1), API_OK);

    // Get a link to mClient's test file.
    auto link = mClient->getPublicLink(mFileHandle);
    ASSERT_EQ(link.errorOr(API_OK), API_OK);

    // Add mClient's test file to our foreign client.
    auto id = client->fileAdd(*link);
    ASSERT_EQ(id.errorOr(FILE_SERVICE_SUCCESS), FILE_SERVICE_SUCCESS);

    // Open the test file, fetch its content and purge it from the service.
    {
        // Open the test file.
        auto file = client->fileOpen(*id);
        ASSERT_EQ(file.errorOr(FILE_SERVICE_SUCCESS), FILE_SERVICE_SUCCESS);

        // Retrieve the file's data.
        ASSERT_EQ(execute(fetch, *file), FILE_SUCCESS);

        // Mark the file as removed.
        ASSERT_EQ(execute(purge, *file), FILE_SUCCESS);
    }

    // Make sure the file's been purged from the service.
    ASSERT_EQ(client->fileOpen(*id).errorOr(FILE_SERVICE_SUCCESS), FILE_SERVICE_FILE_DOESNT_EXIST);
}

TEST_F(FileServiceTests, read_cancel_on_client_logout_succeeds)
{
    // Create a client that we can safely logout.
    auto client = CreateClient("file_service_" + randomName());
    ASSERT_TRUE(client);

    // Log the client in.
    ASSERT_EQ(client->login(0), API_OK);

    // Disable readahead.
    client->fileService().options(DisableReadahead);

    // Open a file for reading.
    auto file = client->fileOpen(mFileHandle);
    ASSERT_EQ(file.errorOr(FILE_SERVICE_SUCCESS), FILE_SERVICE_SUCCESS);

    // Make sure the read doesn't complete before we logout the client.
    client->setDownloadSpeed(4096);

    // Kick off a read.
    auto waiter = read(std::move(*file), 512_KiB, 256_KiB);

    // Log out the client.
    EXPECT_EQ(client->logout(true), API_OK);

    // Wait for the read to complete.
    ASSERT_NE(waiter.wait_for(mDefaultTimeout), timeout);

    // Make sure the read was cancelled.
    EXPECT_EQ(waiter.get().errorOr(FILE_SUCCESS), FILE_CANCELLED);
}

TEST_F(FileServiceTests, read_cancel_on_file_destruction_succeeds)
{
    // Disable readahead.
    mClient->fileService().options(DisableReadahead);

    // Open a file for reading.
    auto file = mClient->fileOpen(mFileHandle);
    ASSERT_EQ(file.errorOr(FILE_SERVICE_SUCCESS), FILE_SERVICE_SUCCESS);

    // Make sure the read doesn't complete before the file's destroyed.
    mClient->setDownloadSpeed(4096);

    // Begin the read, taking care to drop our file reference.
    auto waiter = readOnce(std::move(*file), 768_KiB, 256_KiB);

    // Wait for the read to complete.
    ASSERT_NE(waiter.wait_for(mDefaultTimeout), timeout);

    // Make sure the read was cancelled.
    EXPECT_EQ(waiter.get().errorOr(FILE_SUCCESS), FILE_CANCELLED);
}

TEST_F(FileServiceTests, read_extension_succeeds)
{
    // No minimum read size, extend if another range is <= 32K distant.
    mClient->fileService().options(FileServiceOptions{DefaultOptions.mMaximumRangeRetries,
                                                      32_KiB,
                                                      0u,
                                                      DefaultOptions.mRangeRetryBackoff});

    // Open a file for reading.
    auto file = mClient->fileOpen(mFileHandle);

    // Make sure the file was opened successfully.
    ASSERT_EQ(file.errorOr(FILE_SERVICE_SUCCESS), FILE_SERVICE_SUCCESS);

    // Read two ranges, leaving a 128KiB hole between them.
    auto data = execute(read, *file, 0, 64_KiB);
    ASSERT_EQ(data.errorOr(FILE_SUCCESS), FILE_SUCCESS);

    data = execute(read, *file, 192_KiB, 64_KiB);
    ASSERT_EQ(data.errorOr(FILE_SUCCESS), FILE_SUCCESS);

    // Make sure we have the ranges we expect.
    ASSERT_THAT(file->ranges(), ElementsAre(FileRange(0, 64_KiB), FileRange(192_KiB, 256_KiB)));

    // Read another range, right in the hole we created before.
    data = execute(read, *file, 96_KiB, 64_KiB);
    ASSERT_EQ(data.errorOr(FILE_SUCCESS), FILE_SUCCESS);

    // Make sure our range was expanded to fill the hole.
    ASSERT_EQ(execute(fetchBarrier, *file), FILE_SUCCESS);
    ASSERT_THAT(file->ranges(), ElementsAre(FileRange(0, 256_KiB)));

    // Read another range, just beyond the extension threshold.
    data = execute(read, *file, 289_KiB, 64_KiB);
    ASSERT_EQ(data.errorOr(FILE_SUCCESS), FILE_SUCCESS);

    // Make sure the range wasn't extended.
    ASSERT_EQ(execute(fetchBarrier, *file), FILE_SUCCESS);
    ASSERT_THAT(file->ranges(), ElementsAre(FileRange(0, 256_KiB), FileRange(289_KiB, 353_KiB)));

    // Perform a read to make sure we extend to the left.
    data = execute(read, *file, 385_KiB, 64_KiB);
    ASSERT_EQ(data.errorOr(FILE_SUCCESS), FILE_SUCCESS);

    ASSERT_EQ(execute(fetchBarrier, *file), FILE_SUCCESS);
    ASSERT_THAT(file->ranges(), ElementsAre(FileRange(0, 256_KiB), FileRange(289_KiB, 449_KiB)));

    // Perform another read to create another hole.
    data = execute(read, *file, 640_KiB, 64_KiB);
    ASSERT_EQ(data.errorOr(FILE_SUCCESS), FILE_SUCCESS);

    // Perform a read to make sure we extend to the right.
    data = execute(read, *file, 576_KiB, 32_KiB);
    ASSERT_EQ(data.errorOr(FILE_SUCCESS), FILE_SUCCESS);

    ASSERT_EQ(execute(fetchBarrier, *file), FILE_SUCCESS);
    ASSERT_THAT(file->ranges(),
                ElementsAre(FileRange(0, 256_KiB),
                            FileRange(289_KiB, 449_KiB),
                            FileRange(576_KiB, 704_KiB)));

    // Fill remaining holes via extension.
    data = execute(read, *file, 272_KiB, 8_KiB);
    ASSERT_EQ(data.errorOr(FILE_SUCCESS), FILE_SUCCESS);

    data = execute(read, *file, 481_KiB, 63_KiB);
    ASSERT_EQ(data.errorOr(FILE_SUCCESS), FILE_SUCCESS);

    // We should now have a single range.
    ASSERT_EQ(execute(fetchBarrier, *file), FILE_SUCCESS);
    ASSERT_THAT(file->ranges(), ElementsAre(FileRange(0, 704_KiB)));
}

TEST_F(FileServiceTests, read_external_succeeds)
{
    // Get a public link for our test directory.
    auto link = mClient->getPublicLink(mRootHandle);
    ASSERT_EQ(link.errorOr(API_OK), API_OK);

    // Create a client responsible for accessing our test directory.
    auto client = CreateClient("file_service_" + randomName());
    ASSERT_TRUE(client);

    // Log the client into our test directory.
    ASSERT_EQ(client->login(*link), API_OK);

    // Retrieve our file's key data.
    auto keyData = client->keyData(mFileHandle, true);
    ASSERT_EQ(keyData.errorOr(API_OK), API_OK);

    // Log out of the test directory.
    ASSERT_EQ(client->logout(false), API_OK);

    // Log client into a account distinct from mClient.
    ASSERT_EQ(client->login(1), API_OK);

    // Try and add the file to the client.
    auto id = client->fileService().add(mFileHandle, *keyData, mFileContent.size());
    ASSERT_EQ(id.errorOr(FILE_SERVICE_SUCCESS), FILE_SERVICE_SUCCESS);

    // Try and open the file.
    auto file = client->fileOpen(*id);
    ASSERT_EQ(file.errorOr(FILE_SERVICE_SUCCESS), FILE_SERVICE_SUCCESS);

    // Try and read the file's content.
    auto computed = execute(read, *file, 0, mFileContent.size());
    ASSERT_EQ(computed.errorOr(FILE_SUCCESS), FILE_SUCCESS);

    // Make sure we read what we expected.
    ASSERT_TRUE(compare(*computed, mFileContent, 0, mFileContent.size()));
}

TEST_F(FileServiceTests, read_foreign_succeeds)
{
    // Create a new client.
    auto client = CreateClient("file_service_" + randomName());
    ASSERT_TRUE(client);

    // Log the client in.
    ASSERT_EQ(client->login(1), API_OK);

    // Get our test file's public link.
    auto link = mClient->getPublicLink(mFileHandle);
    ASSERT_EQ(link.errorOr(API_OK), API_OK);

    // Add mClient's test file to client.
    auto id = client->fileAdd(*link);
    ASSERT_EQ(id.errorOr(FILE_SERVICE_SUCCESS), FILE_SERVICE_SUCCESS);

    // Open the file.
    auto file = client->fileOpen(*id);
    ASSERT_EQ(file.errorOr(FILE_SERVICE_SUCCESS), FILE_SERVICE_SUCCESS);

    // Try and read the file's content.
    auto computed = execute(read, *file, 0, mFileContent.size());
    ASSERT_EQ(computed.errorOr(FILE_SUCCESS), FILE_SUCCESS);

    // Make sure we've read what we expected.
    ASSERT_TRUE(compare(*computed, mFileContent, 0, mFileContent.size()));
}

TEST_F(FileServiceTests, read_removed_file_succeeds)
{
    // Create a file for us to play with.
    auto handle = mClient->upload(randomBytes(512_KiB), randomName(), mRootHandle);

    // Make sure we could create our file.
    ASSERT_EQ(handle.errorOr(API_OK), API_OK);

    // Open the file for reading.
    auto file = mClient->fileOpen(*handle);

    // Make sure we could open the file.
    ASSERT_EQ(file.errorOr(FILE_SERVICE_SUCCESS), FILE_SERVICE_SUCCESS);

    // Disable readahead.
    mClient->fileService().options(DisableReadahead);

    // Read some data from the file.
    auto data0 = execute(read, *file, 0, 256_KiB);

    // Make sure the read succeeded.
    ASSERT_EQ(data0.errorOr(FILE_SUCCESS), FILE_SUCCESS);

    // Remove the file from the cloud.
    ASSERT_EQ(mClient->remove(*handle), API_OK);

    // Make sure we can read data we've already retrieved.
    auto data1 = execute(read, *file, 0, 256_KiB);

    ASSERT_EQ(data1.errorOr(FILE_SUCCESS), FILE_SUCCESS);
    ASSERT_FALSE(data0->compare(*data1));

    // Reading new data should fail.
    data1 = execute(read, *file, 256_KiB, 256_KiB);
    ASSERT_EQ(data1.errorOr(FILE_SUCCESS), FILE_REMOVED);
}

TEST_F(FileServiceTests, read_size_extension_succeeds)
{
    // Minimum read size is 64KiB, everything else are defaults.
    mClient->fileService().options(FileServiceOptions{DefaultOptions.mMaximumRangeRetries,
                                                      DefaultOptions.mMinimumRangeDistance,
                                                      64_KiB,
                                                      DefaultOptions.mRangeRetryBackoff});

    // Open a file for reading.
    auto file = mClient->fileOpen(mFileHandle);
    ASSERT_EQ(file.errorOr(FILE_SERVICE_SUCCESS), FILE_SERVICE_SUCCESS);

    // Read 4K from the file.
    auto data = execute(read, *file, 0, 4_KiB);
    ASSERT_EQ(data.errorOr(FILE_SUCCESS), FILE_SUCCESS);
    ASSERT_EQ(static_cast<std::uint64_t>(data->size()), 4_KiB);

    // Make sure the read's size was extended.
    ASSERT_THAT(file->ranges(), ElementsAre(FileRange(0, 64_KiB)));
}

TEST_F(FileServiceTests, read_succeeds)
{
    // Disable readahead.
    mClient->fileService().options(DisableReadahead);

    // Open a file for reading.
    auto file = mClient->fileOpen(mFileHandle);
    ASSERT_EQ(file.errorOr(FILE_SERVICE_SUCCESS), FILE_SERVICE_SUCCESS);

    // Latch the file's access time.
    auto accessed = file->info().accessed();

    // We should be able to read 64KiB from the beginning of the file.
    auto result = execute(read, *file, 0, 64_KiB);

    // Make sure the file's access time have been bumped.
    EXPECT_GE(file->info().accessed(), accessed);

    // Make sure the read completed successfully.
    ASSERT_EQ(result.errorOr(FILE_SUCCESS), FILE_SUCCESS);

    // And that it provided the data we expected.
    EXPECT_TRUE(compare(*result, mFileContent, 0, 64_KiB));

    // Make sure the range is considered to be in storage.
    ASSERT_THAT(file->ranges(), ElementsAre(FileRange(0, 64_KiB)));

    // Latch the file's access time.
    accessed = file->info().accessed();

    // Read another 64KiB.
    result = execute(read, *file, 64_KiB, 64_KiB);

    // Make sure the file's access time have been bumped.
    EXPECT_GE(file->info().accessed(), accessed);

    // Make sure the read completed successfully.
    ASSERT_EQ(result.errorOr(FILE_SUCCESS), FILE_SUCCESS);

    // And that it provided the data we expected.
    EXPECT_TRUE(compare(*result, mFileContent, 64_KiB, 64_KiB));

    // We should have one 128KiB range in storage.
    ASSERT_THAT(file->ranges(), ElementsAre(FileRange(0, 128_KiB)));

    // Latch the file's access time.
    accessed = file->info().accessed();

    // Kick off two reads in parallel.
    auto waiter0 = read(*file, 128_KiB, 64_KiB);
    auto waiter1 = read(*file, 192_KiB, 64_KiB);

    // Wait for our reads to complete.
    ASSERT_NE(waiter0.wait_for(mDefaultTimeout), timeout);
    ASSERT_NE(waiter1.wait_for(mDefaultTimeout), timeout);

    // Make sure the file's access time have been bumped.
    EXPECT_GE(file->info().accessed(), accessed);

    // Make sure both reads succeeded.
    auto result0 = waiter0.get();
    auto result1 = waiter1.get();

    EXPECT_EQ(result0.errorOr(FILE_SUCCESS), FILE_SUCCESS);
    EXPECT_EQ(result1.errorOr(FILE_SUCCESS), FILE_SUCCESS);

    ASSERT_FALSE(HasFailure());

    // Make sure both reads gave us what we expected.
    EXPECT_TRUE(compare(*result0, mFileContent, 128_KiB, 64_KiB));
    EXPECT_TRUE(compare(*result1, mFileContent, 192_KiB, 64_KiB));

    // We should have one 256KiB range in storage.
    ASSERT_THAT(file->ranges(), ElementsAre(FileRange(0, 256_KiB)));

    // Latch the file's access time.
    accessed = file->info().accessed();

    // Make sure zero length reads are handled correctly.
    result = execute(read, *file, 0, 0);
    ASSERT_EQ(result.errorOr(FILE_SUCCESS), FILE_SUCCESS);
    EXPECT_TRUE(result->empty());

    // Zero length reads shouldn't change a file's access time.
    EXPECT_EQ(file->info().accessed(), accessed);

    // Make sure reads are clamped.
    result = execute(read, *file, 768_KiB, 512_KiB);
    ASSERT_EQ(result.errorOr(FILE_SUCCESS), FILE_SUCCESS);
    EXPECT_TRUE(compare(*result, mFileContent, 768_KiB, 256_KiB));

    // Make sure the file's access time have been bumped.
    EXPECT_GE(file->info().accessed(), accessed);

    // Reads should never dirty a file.
    ASSERT_FALSE(file->info().dirty());
}

TEST_F(FileServiceTests, read_write_sequence)
{
    // Try and open our test file.
    auto file = mClient->fileOpen(mFileHandle);
    ASSERT_EQ(file.errorOr(FILE_SERVICE_SUCCESS), FILE_SERVICE_SUCCESS);

    // Generate some data for us to write to the file.
    auto expected = randomBytes(512_KiB);

    // Disable readahead.
    mClient->fileService().options(DisableReadahead);

    // Make sure our initial read doesn't complete too quickly.
    mClient->setDownloadSpeed(4096);

    // Initiate a requset to read all of the file's data.
    auto read0 = readOnce(*file, 0, 1_MiB);

    // Initiate a request to overwrite all of the file's data.
    auto write = testing::write(expected.data(), *file, 0, expected.size());

    // Initiate a request to read some of the file's new data.
    auto read1 = read(*file, 0, expected.size());

    // Allow our reads to complete quickly.
    mClient->setDownloadSpeed(0);

    // Wait for our requests to complete.
    EXPECT_NE(read0.wait_for(mDefaultTimeout), timeout);
    EXPECT_NE(write.wait_for(mDefaultTimeout), timeout);
    EXPECT_NE(read1.wait_for(mDefaultTimeout), timeout);

    // One or more requests timed out.
    if (HasFailure())
        return;

    // Make sure all of our requests succeeded.
    auto readResult0 = read0.get();
    auto readResult1 = read1.get();
    auto writeResult = write.get();

    EXPECT_EQ(readResult0.errorOr(FILE_SUCCESS), FILE_SUCCESS);
    EXPECT_EQ(readResult1.errorOr(FILE_SUCCESS), FILE_SUCCESS);
    EXPECT_EQ(writeResult, FILE_SUCCESS);

    // One or more requests failed.
    if (HasFailure())
        return;

    // The first read should return the file's original data.
    EXPECT_FALSE(mFileContent.compare(0, readResult0->size(), *readResult0));

    // The second read should return the file's updated data.
    EXPECT_EQ(expected.size(), readResult1->size());
    EXPECT_FALSE(expected.compare(*readResult1));
}

TEST_F(FileServiceTests, reclaim_all_succeeds)
{
    // Handles of our test files.
    std::vector<NodeHandle> handles;

    // Create some test files for us to play with.
    for (auto i = 0; i < 4; ++i)
    {
        // Generate some data for our file.
        auto data = randomBytes(1_MiB);

        // Generate a name for our file.
        auto name = randomName();

        // Try and upload our test file.
        auto handle = mClient->upload(data, name, mRootHandle);

        // Make sure the upload succeeded.
        ASSERT_EQ(handle.errorOr(API_OK), API_OK);

        // Remember the file's node handle.
        handles.emplace_back(*handle);
    }

    // Tracks each file that we've opened.
    std::vector<File> files;

    // We'll be modifying these options later.
    auto options = DisableReadahead;

    // Disable readahead.
    //
    // This is necessary to ensure we read only as much as specified.
    mClient->fileService().options(options);

    // Open, read and modify each file.
    for (auto handle: handles)
    {
        // Try and open the file.
        auto file = mClient->fileOpen(handle);

        // Make sure we could open the file.
        ASSERT_EQ(file.errorOr(FILE_SERVICE_SUCCESS), FILE_SERVICE_SUCCESS);

        // Read some data from the file.
        auto data = execute(read, *file, 0, 512_KiB);

        // Make sure the read succeeded.
        ASSERT_EQ(data.errorOr(FILE_SUCCESS), FILE_SUCCESS);

        // Modify the file.
        ASSERT_EQ(execute(write, data->data(), *file, 0, 32_KiB), FILE_SUCCESS);

        // Make sure the service doesn't purge the file.
        files.emplace_back(std::move(*file));
    }

    // Determine how much storage the service is using.
    auto usedBefore = mClient->fileService().storageUsed();
    ASSERT_EQ(usedBefore.errorOr(FILE_SERVICE_SUCCESS), FILE_SERVICE_SUCCESS);

    // Make sure we're using only as much as we read.
    ASSERT_EQ(*usedBefore, 512_KiB * files.size());

    // Try and reclaim some storage.
    //
    // This should have no effect as we've specified no quota.
    auto reclaimed = execute(reclaimAll, mClient);
    ASSERT_EQ(reclaimed.errorOr(FILE_SERVICE_SUCCESS), FILE_SERVICE_SUCCESS);
    ASSERT_EQ(reclaimed.valueOr(0ul), 0ul);

    // Make sure no storage was reclaimed.
    auto usedAfter = mClient->fileService().storageUsed();
    ASSERT_EQ(usedAfter.errorOr(FILE_SERVICE_SUCCESS), FILE_SERVICE_SUCCESS);
    ASSERT_EQ(*usedAfter, *usedBefore);

    // Let the service know it should store no more than 544K.
    options.mReclaimSizeThreshold = 544_KiB;

    // Reclaim files that haven't been accessed for three hours.
    options.mReclaimAgeThreshold = std::chrono::hours(3);

    mClient->fileService().options(options);

    // Try and reclaim storage.
    //
    // This should also have no effect as we accessed the files recently.
    reclaimed = execute(reclaimAll, mClient);
    ASSERT_EQ(reclaimed.errorOr(FILE_SERVICE_SUCCESS), FILE_SERVICE_SUCCESS);
    ASSERT_EQ(reclaimed.valueOr(0ul), 0ul);

    // Make sure no storage was reclaimed.
    usedAfter = mClient->fileService().storageUsed();

    ASSERT_EQ(usedAfter.errorOr(FILE_SERVICE_SUCCESS), FILE_SERVICE_SUCCESS);
    ASSERT_EQ(*usedBefore, *usedAfter);

    // Let the service know it can reclaim files regardless of access time.
    options.mReclaimAgeThreshold = std::chrono::hours(0);

    // Reclaim a single file at a time.
    options.mReclaimBatchSize = 1;

    mClient->fileService().options(options);

    // Try and reclaim storage.
    reclaimed = execute(reclaimAll, mClient);
    ASSERT_EQ(reclaimed.errorOr(FILE_SERVICE_SUCCESS), FILE_SERVICE_SUCCESS);

    // Make sure storage was reclaimed.
    usedAfter = mClient->fileService().storageUsed();

    ASSERT_EQ(usedAfter.errorOr(FILE_SERVICE_SUCCESS), FILE_SERVICE_SUCCESS);
    ASSERT_LT(*usedAfter, *usedBefore);
    ASSERT_EQ(*reclaimed, *usedBefore - *usedAfter);

    // For later comparison.
    usedBefore = usedAfter;

    // Try and reclaim storage again.
    //
    // This should have no effect as we're already below the quota.
    reclaimed = execute(reclaimAll, mClient);
    ASSERT_EQ(reclaimed.errorOr(FILE_SERVICE_SUCCESS), FILE_SERVICE_SUCCESS);
    ASSERT_EQ(reclaimed.valueOr(0ul), 0ul);

    // Make sure no more storage was reclaimed.
    usedAfter = mClient->fileService().storageUsed();

    ASSERT_EQ(usedAfter.errorOr(FILE_SERVICE_SUCCESS), FILE_SERVICE_SUCCESS);
    ASSERT_EQ(*usedAfter, *usedBefore);
}

TEST_F(FileServiceTests, reclaim_cancel_on_file_destruction_succeeds)
{
    // Open the test file.
    auto file = mClient->fileOpen(mFileHandle);
    ASSERT_EQ(file.errorOr(FILE_SERVICE_SUCCESS), FILE_SERVICE_SUCCESS);

    // Fetch the file's content.
    ASSERT_EQ(execute(fetch, *file), FILE_SUCCESS);

    // Reclaim the file's storage.
    auto waiter = reclaim(std::move(*file));

    // Wait for the reclamation to complete.
    ASSERT_NE(waiter.wait_for(mDefaultTimeout), timeout);

    // Reopen the file.
    file = mClient->fileOpen(mFileHandle);
    ASSERT_EQ(file.errorOr(FILE_SERVICE_SUCCESS), FILE_SERVICE_SUCCESS);

    // Fetch the file's content (if necessary.)
    ASSERT_EQ(execute(fetch, *file), FILE_SUCCESS);

    // Mark the file as modified (to force a flush.)
    ASSERT_EQ(execute(touch, *file, 0), FILE_SUCCESS);

    // Reclaim the file's storage.
    waiter = reclaim(std::move(*file));

    // Wait for reclamation to complete.
    ASSERT_NE(waiter.wait_for(mDefaultTimeout), timeout);
}

TEST_F(FileServiceTests, reclaim_concurrent_succeeds)
{
    // Disable readahead and reclamation.
    mClient->fileService().options(
        []()
        {
            auto options = DisableReadahead;
            options.mReclaimSizeThreshold = 0;
            return options;
        }());

    // Open our test file.
    auto file = mClient->fileOpen(mFileHandle);
    ASSERT_EQ(file.errorOr(FILE_SERVICE_SUCCESS), FILE_SERVICE_SUCCESS);

    // Make sure all of the file's content is present on disk.
    ASSERT_EQ(execute(fetch, *file), FILE_SUCCESS);

    // Make sure the file's actually taking space on disk.
    auto usedBefore = mClient->fileService().storageUsed();
    ASSERT_EQ(usedBefore.errorOr(FILE_SERVICE_SUCCESS), FILE_SERVICE_SUCCESS);
    ASSERT_EQ(*usedBefore, mFileContent.size());

    // Initiate several concurrent reclaim requests.
    using ReclaimResult = decltype(reclaim(std::declval<File>()));
    using ReclaimResultVector = std::vector<ReclaimResult>;

    ReclaimResultVector reclamations;

    for (auto i = 0; i < 8; ++i)
        reclamations.emplace_back(reclaim(*file));

    // Wait for all reclamations to complete.
    while (!reclamations.empty())
    {
        ASSERT_NE(reclamations.back().wait_for(mDefaultTimeout), timeout);
        ASSERT_EQ(reclamations.back().get().errorOr(FILE_SUCCESS), FILE_SUCCESS);
        reclamations.pop_back();
    }

    // Make sure disk space has actually been reclaimed.
    EXPECT_TRUE(waitFor(
        [&]()
        {
            return mClient->fileService().storageUsed().valueOr(UINT64_MAX) == 0;
        },
        mDefaultTimeout));

    auto usedAfter = mClient->fileService().storageUsed();
    ASSERT_EQ(usedAfter.errorOr(FILE_SERVICE_SUCCESS), FILE_SERVICE_SUCCESS);
    ASSERT_LT(*usedAfter, *usedBefore);
    ASSERT_EQ(*usedAfter, 0ul);
}

TEST_F(FileServiceTests, reclaim_foreign_file_succeeds)
{
    // Create a foreign client.
    auto client = CreateClient("file_service_" + randomName());
    ASSERT_TRUE(client);

    // Log in the client.
    ASSERT_EQ(client->login(1), API_OK);

    // Get a link to mClient's test file.
    auto link = mClient->getPublicLink(mFileHandle);
    ASSERT_EQ(link.errorOr(API_OK), API_OK);

    // Add mClient's test file to our foreign client.
    auto id = client->fileAdd(*link);
    ASSERT_EQ(id.errorOr(FILE_SERVICE_SUCCESS), FILE_SERVICE_SUCCESS);

    // Open the test file.
    auto file = client->fileOpen(*id);
    ASSERT_EQ(file.errorOr(FILE_SERVICE_SUCCESS), FILE_SERVICE_SUCCESS);

    // Retrieve the file's data.
    ASSERT_EQ(execute(fetch, *file), FILE_SUCCESS);

    // Figure out how much space our file is using on disk.
    auto allocated = file->info().allocatedSize();

    // Make sure the file's footprint is what we expect it is.
    ASSERT_EQ(allocated, mFileContent.size());

    // Reclaim the file's storage.
    auto reclaimed = execute(reclaim, *file);
    ASSERT_EQ(reclaimed.errorOr(FILE_SUCCESS), FILE_SUCCESS);
    ASSERT_EQ(*reclaimed, mFileContent.size());

    // Make sure the file's storage footprint has decreased.
    EXPECT_EQ(file->info().allocatedSize(), 0u);
}

TEST_F(FileServiceTests, reclaim_periodic_succeeds)
{
    // Convenience.
    using std::chrono::hours;
    using std::chrono::minutes;
    using std::chrono::seconds;

    // Keeps track of our test files.
    std::vector<File> files;

    // Disable readahead and reclamation.
    auto options = DisableReadahead;

    options.mReclaimSizeThreshold = 0;

    mClient->fileService().options(options);

    // Create a few files for us to test with.
    for (auto i = 0; i < 4; ++i)
    {
        // Generate some data for our test file.
        auto data = randomBytes(1_MiB);

        // Generate a name for our test file.
        auto name = randomName();

        // Try and upload our test file to the cloud.
        auto handle = mClient->upload(data, name, mRootHandle);

        // Make sure the upload succeeded.
        ASSERT_EQ(handle.errorOr(API_OK), API_OK);

        // Try and open our test file.
        auto file = mClient->fileOpen(*handle);

        // Make sure we could open our file.
        ASSERT_EQ(file.errorOr(FILE_SERVICE_SUCCESS), FILE_SERVICE_SUCCESS);

        // Read some data from the file.
        ASSERT_EQ(execute(read, *file, 0, 512_KiB).errorOr(FILE_SUCCESS), FILE_SUCCESS);

        // Make sure the service doesn't prematurely purge our file.
        files.emplace_back(std::move(*file));
    }

    // Make sure our storage footprint is as we expect.
    auto usedBefore = mClient->fileService().storageUsed();
    ASSERT_EQ(usedBefore.errorOr(FILE_SERVICE_SUCCESS), FILE_SERVICE_SUCCESS);
    ASSERT_EQ(*usedBefore, 512_KiB * files.size());

    // Enable storage reclamation.
    options.mReclaimAgeThreshold = hours(0);
    options.mReclaimPeriod = seconds(15);
    options.mReclaimSizeThreshold = 512_KiB;

    mClient->fileService().options(options);

    // Wait for storage to be reclaimed.
    EXPECT_TRUE(waitFor(
        [&]()
        {
            return mClient->fileService().storageUsed().valueOr(0) == 512_KiB;
        },
        minutes(5)));

    // So we get useful logs.
    auto usedAfter = mClient->fileService().storageUsed();
    ASSERT_EQ(usedAfter.errorOr(FILE_SERVICE_SUCCESS), FILE_SERVICE_SUCCESS);
    EXPECT_LT(*usedAfter, *usedBefore);
    EXPECT_EQ(*usedAfter, 512_KiB);
}

TEST_F(FileServiceTests, reclaim_single_succeeds)
{
    // Generate data for us to write to the cloud.
    auto expected = randomBytes(512_KiB);

    // Create a file that we can modify.
    auto handle = mClient->upload(expected, randomName(), mRootHandle);

    // Make sure we could create our test file.
    ASSERT_EQ(handle.errorOr(API_OK), API_OK);

    // Open the file for IO.
    auto file = mClient->fileOpen(*handle);

    // Make sure we could open our file.
    ASSERT_EQ(file.errorOr(FILE_SERVICE_SUCCESS), FILE_SERVICE_SUCCESS);

    // Read some data from the file.
    auto computed = execute(read, *file, 0, 64_KiB);
    ASSERT_EQ(computed.errorOr(FILE_SUCCESS), FILE_SUCCESS);

    computed = execute(read, *file, 128_KiB, 64_KiB);
    ASSERT_EQ(computed.errorOr(FILE_SUCCESS), FILE_SUCCESS);

    // Write some data to the file.
    ASSERT_EQ(execute(write, expected.data(), *file, 256_KiB, 64_KiB), FILE_SUCCESS);

    // Update our expected buffer.
    expected.replace(256_KiB, 64_KiB, expected, 0, 64_KiB);

    // Find out how much space the service is currently using.
    auto usedBefore = mClient->fileService().storageUsed();
    ASSERT_EQ(usedBefore.errorOr(FILE_SERVICE_SUCCESS), FILE_SERVICE_SUCCESS);

    // Try and reclaim the file's storage.
    {
        // For later comparison.
        auto allocatedBefore = file->info().allocatedSize();

        // Try and reclaim storage.
        auto reclaimed = execute(reclaim, *file);

        // Make sure reclamation succeeded.
        ASSERT_EQ(reclaimed.errorOr(FILE_SUCCESS), FILE_SUCCESS);

        // Convenience.
        auto allocatedAfter = file->info().allocatedSize();

        // Make sure we actually reclaimed space.
        ASSERT_LT(allocatedAfter, allocatedBefore);
        ASSERT_EQ(*reclaimed, allocatedBefore - allocatedAfter);

        // Make sure our file's attributes were updated correctly.
        ASSERT_EQ(file->info().reportedSize(), 0u);
        ASSERT_EQ(file->info().size(), expected.size());
    }

    // Make sure a new copy of our file has been uploaded to the cloud.
    auto info = mClient->get(file->info().handle());
    ASSERT_EQ(info.errorOr(API_OK), API_OK);
    ASSERT_NE(*handle, info->mHandle);

    // Make sure we actually reclaimed space.
    auto usedAfter = mClient->fileService().storageUsed();
    ASSERT_EQ(usedAfter.errorOr(FILE_SERVICE_SUCCESS), FILE_SERVICE_SUCCESS);
    ASSERT_LT(*usedAfter, *usedBefore);

    // Make sure we can read all of the file's data.
    computed = execute(read, *file, 0, 512_KiB);

    // Make sure the read completed.
    ASSERT_EQ(computed.errorOr(FILE_SUCCESS), FILE_SUCCESS);

    // And that the data we read was correct.
    ASSERT_TRUE(*computed == expected);
}

TEST_F(FileServiceTests, remove_local_succeeds)
{
    // Records the ID of the file created directly below.
    FileID id;

    // Records how much storage space our test file occupied.
    FileServiceResultOr<std::uint64_t> usedBefore;

    // Create and then remove a local file.
    {
        // Generate a name for our file.
        auto name = randomName();

        // Create a local file.
        auto file0 = mClient->fileCreate(mRootHandle, name);

        // Make sure we could create the file.
        ASSERT_EQ(file0.errorOr(FILE_SERVICE_SUCCESS), FILE_SERVICE_SUCCESS);

        // Latch the file's ID.
        id = file0->info().id();

        // Generate some data to write to our file.
        auto data = randomBytes(512_KiB);

        // Write some data to the file.
        ASSERT_EQ(execute(write, data.data(), *file0, 0, data.size()), FILE_SUCCESS);

        // Figure out how much space our file's using.
        usedBefore = mClient->fileService().storageUsed();

        // Make sure we could determine how much space our file was using.
        ASSERT_EQ(usedBefore.errorOr(FILE_SERVICE_SUCCESS), FILE_SERVICE_SUCCESS);

        // What events do we expect to receive?
        FileEventVector expected;

        // So we can receive file events.
        auto fileObserver = observe(*file0);
        auto serviceObserver = observe(mClient->fileService());

        // Remove the file.
        ASSERT_EQ(execute(remove, *file0), FILE_SUCCESS);

        expected.emplace_back(FileRemoveEvent{id, false});

        // Make sure the file's been marked as removed.
        ASSERT_TRUE(file0->info().removed());

        // Make sure we received a remove event.
        EXPECT_EQ(expected, fileObserver.events());
        EXPECT_EQ(expected, serviceObserver.events());

        // Make sure we can't get a new reference to a removed file.
        ASSERT_EQ(mClient->fileInfo(id).errorOr(FILE_SERVICE_SUCCESS), FILE_SERVICE_UNKNOWN_FILE);

        ASSERT_EQ(mClient->fileOpen(id).errorOr(FILE_SERVICE_SUCCESS),
                  FILE_SERVICE_FILE_DOESNT_EXIST);

        // Make sure we can create another file at the same location.
        auto file1 = mClient->fileCreate(mRootHandle, name);

        // Make sure we could create our file.
        ASSERT_EQ(file1.errorOr(FILE_SERVICE_SUCCESS), FILE_SERVICE_SUCCESS);

        // Mark that file for removal, too.
        ASSERT_EQ(execute(remove, *file1), FILE_SUCCESS);
    }

    // Make sure the file's been removed.
    auto file = mClient->fileOpen(id);
    ASSERT_EQ(file.errorOr(FILE_SERVICE_SUCCESS), FILE_SERVICE_FILE_DOESNT_EXIST);

    // Make sure storage space has been recovered.
    auto usedAfter = mClient->fileService().storageUsed();

    ASSERT_EQ(usedAfter.errorOr(FILE_SERVICE_SUCCESS), FILE_SERVICE_SUCCESS);
    ASSERT_LT(*usedAfter, *usedBefore);
}

TEST_F(FileServiceTests, remove_cloud_succeeds)
{
    // Generate a name for our file.
    auto name = randomName();

    // Upload a file to the cloud.
    auto handle = mClient->upload(randomBytes(512_KiB), name, mRootHandle);

    // Make sure our file was uploaded.
    ASSERT_EQ(handle.errorOr(API_OK), API_OK);

    // How much storage space our file used before it was removed.
    FileServiceResultOr<std::uint64_t> usedBefore;

    // Open the file, read some data and then remove it.
    {
        // Try and open our file.
        auto file0 = mClient->fileOpen(*handle);

        // Make sure we could open the file.
        ASSERT_EQ(file0.errorOr(FILE_SERVICE_SUCCESS), FILE_SERVICE_SUCCESS);

        // Latch the file's ID.
        auto id = file0->info().id();

        // Read all data from the file.
        ASSERT_EQ(execute(fetch, *file0), FILE_SUCCESS);

        // Figure out how much storage space our file's using.
        usedBefore = mClient->fileService().storageUsed();
        ASSERT_EQ(usedBefore.errorOr(FILE_SERVICE_SUCCESS), FILE_SERVICE_SUCCESS);

        // What events do we expect to receive?
        FileEventVector expected;

        // So we can receive file events.
        auto fileObserver = observe(*file0);
        auto serviceObserver = observe(mClient->fileService());

        // Remove the file.
        ASSERT_EQ(execute(remove, *file0), FILE_SUCCESS);

        expected.emplace_back(FileRemoveEvent{id, false});

        // Make sure the file's been removed.
        ASSERT_TRUE(waitFor(
            [&]()
            {
                return mClient->get(*handle).errorOr(API_OK) == API_ENOENT &&
                       file0->info().removed();
            },
            mDefaultTimeout));

        // Make sure we received a remove event.
        EXPECT_EQ(expected, fileObserver.events());
        EXPECT_EQ(expected, serviceObserver.events());

        EXPECT_EQ(mClient->get(*handle).errorOr(API_OK), API_ENOENT);
        EXPECT_TRUE(file0->info().removed());

        // Make sure we can't get a new reference to a removed file.
        ASSERT_EQ(mClient->fileInfo(id).errorOr(FILE_SERVICE_SUCCESS), FILE_SERVICE_UNKNOWN_FILE);

        ASSERT_EQ(mClient->fileOpen(id).errorOr(FILE_SERVICE_SUCCESS),
                  FILE_SERVICE_FILE_DOESNT_EXIST);

        // Make sure we can create a new file at the same location.
        auto file1 = mClient->fileCreate(mRootHandle, name);
        ASSERT_EQ(file1.errorOr(FILE_SERVICE_SUCCESS), FILE_SERVICE_SUCCESS);

        // Mark that file for removal, too.
        ASSERT_EQ(execute(remove, *file1), FILE_SUCCESS);
    }

    // Make sure the file's been removed.
    auto file = mClient->fileOpen(*handle);
    ASSERT_EQ(file.errorOr(FILE_SERVICE_SUCCESS), FILE_SERVICE_FILE_DOESNT_EXIST);

    // Make sure storage space has been recovered.
    auto usedAfter = mClient->fileService().storageUsed();

    ASSERT_EQ(usedAfter.errorOr(FILE_SERVICE_SUCCESS), FILE_SERVICE_SUCCESS);
    ASSERT_LT(*usedAfter, *usedBefore);
}

TEST_F(FileServiceTests, touch_succeeds)
{
    // Open a file for modification.
    auto file = mClient->fileOpen(mFileHandle);

    // Make sure the file was opened okay.
    ASSERT_EQ(file.errorOr(FILE_SERVICE_SUCCESS), FILE_SERVICE_SUCCESS);

    // Events we expect to receive.
    FileEventVector expected;

    // So we can keep track of our file's events.
    auto fileObserver = observe(*file);
    auto serviceObserver = observe(mClient->fileService());

    // Get our hands on the file's attributes.
    auto info = file->info();

    // Files should be clean initially.
    ASSERT_FALSE(info.dirty());

    // Latch the file's current access and modification time.
    auto accessed = info.accessed();
    auto modified = info.modified();

    // Try and update the file's modification time.
    ASSERT_EQ(execute(touch, *file, modified - 1), FILE_SUCCESS);

    expected.emplace_back(FileTouchEvent{file->info().id(), modified - 1});

    // Make sure the file's now considered dirty.
    EXPECT_TRUE(info.dirty());

    // Make sure the file's access time has been updated.
    EXPECT_GE(info.accessed(), accessed);
    EXPECT_GE(info.accessed(), info.modified());

    // Make sure the file's modification time was updated.
    EXPECT_EQ(info.modified(), modified - 1);

    // Make sure we received an event.
    ASSERT_EQ(expected, fileObserver.events());
    ASSERT_EQ(expected, serviceObserver.events());
}

TEST_F(FileServiceTests, truncate_with_ranges_succeeds)
{
    // Disable readahead.
    mClient->fileService().options(DisableReadahead);

    // Open the file for truncation.
    auto file = mClient->fileOpen(mFileHandle);

    // Make sure the file was opened.
    ASSERT_EQ(file.errorOr(FILE_SERVICE_SUCCESS), FILE_SERVICE_SUCCESS);

    // Reads a range from the file.
    auto read = [&](std::uint64_t offset, std::uint64_t length)
    {
        return execute(testing::read, *file, offset, length);
    }; // read

    // Download a range from the file.
    auto fetch = [&](std::uint64_t offset, std::uint64_t length)
    {
        return read(offset, length).errorOr(FILE_SUCCESS);
    }; // fetch

    // Truncate the file to a particular size.
    auto truncate = [&](std::uint64_t newSize)
    {
        // The events we expect to receive.
        FileEventVector expected;

        // So we can receive events.
        auto fileObserver = observe(*file);
        auto serviceObserver = observe(mClient->fileService());

        // Get our hands on the file's attributes.
        auto info = file->info();

        // Latch the file's current size.
        auto size = info.size();

        // Determine whether the file should become dirty.
        auto dirty = newSize != size;

        // Latch the file's current access time.
        auto accessed = info.accessed();

        // Latch the file's current modification time.
        auto modified = info.modified();

        // Initiate the truncate request.
        auto result = execute(testing::truncate, *file, newSize);

        // Truncate failed.
        if (result != FILE_SUCCESS)
            return result;

        // We should only receive events if the file's size changed.
        if (dirty)
        {
            // Assume the file's size is increasing.
            FileTruncateEvent event{std::nullopt, info.id(), newSize};

            // File's size is actually decreasing.
            if (newSize < size)
                event.mRange.emplace(newSize, size);

            expected.emplace_back(event);
        }

        // Make sure the file's attributes have been updated.
        EXPECT_EQ(info.dirty(), dirty);
        EXPECT_GE(info.accessed(), accessed);
        EXPECT_GE(info.modified(), modified);
        EXPECT_EQ(info.size(), newSize);

        // Make sure we received our expected events.
        EXPECT_EQ(expected, fileObserver.events());
        EXPECT_EQ(expected, serviceObserver.events());

        // One of the above expectations wasn't met.
        if (HasFailure())
            return FILE_FAILED;

        // Let the caller know the truncation was successful.
        return result;
    }; // truncate

    // Read a few ranges from the file.
    EXPECT_EQ(fetch(32_KiB, 32_KiB), FILE_SUCCESS);
    EXPECT_EQ(fetch(96_KiB, 32_KiB), FILE_SUCCESS);
    EXPECT_EQ(fetch(160_KiB, 32_KiB), FILE_SUCCESS);

    // Make sure we have the ranges we requested.
    ASSERT_THAT(file->ranges(),
                ElementsAre(FileRange(32_KiB, 64_KiB),
                            FileRange(96_KiB, 128_KiB),
                            FileRange(160_KiB, 192_KiB)));

    // Truncate the file to 256KiB.
    ASSERT_EQ(truncate(256_KiB), FILE_SUCCESS);

    // Existing ranges should be unchanged.
    ASSERT_THAT(file->ranges(),
                ElementsAre(FileRange(32_KiB, 64_KiB),
                            FileRange(96_KiB, 128_KiB),
                            FileRange(160_KiB, 192_KiB)));

    // Truncate the file to 160KiB.
    ASSERT_EQ(truncate(160_KiB), FILE_SUCCESS);

    // The range [160, 192] should have been removed.
    ASSERT_THAT(file->ranges(), ElementsAre(FileRange(32_KiB, 64_KiB), FileRange(96_KiB, 128_KiB)));

    // Truncate the file to 112KiB.
    ASSERT_EQ(truncate(112_KiB), FILE_SUCCESS);

    // The range [96, 128] should become [96, 112].
    ASSERT_THAT(file->ranges(), ElementsAre(FileRange(32_KiB, 64_KiB), FileRange(96_KiB, 112_KiB)));

    // Extend the file to 256KiB.
    ASSERT_EQ(truncate(256_KiB), FILE_SUCCESS);

    // The range [96, 112] should become [96, 256].
    ASSERT_THAT(file->ranges(), ElementsAre(FileRange(32_KiB, 64_KiB), FileRange(96_KiB, 256_KiB)));
}

TEST_F(FileServiceTests, truncate_without_ranges_succeeds)
{
    // Open the file for truncation.
    auto file = mClient->fileOpen(mFileHandle);

    // Make sure the file was opened.
    ASSERT_EQ(file.errorOr(FILE_SERVICE_SUCCESS), FILE_SERVICE_SUCCESS);

    // The events we expect to receive.
    FileEventVector expected;

    // So we can receive file events.
    auto fileObserver = observe(*file);
    auto serviceObserver = observe(mClient->fileService());

    // Get our hands on the file's attributes.
    auto info = file->info();

    // Files should be clean initially.
    ASSERT_FALSE(info.dirty());

    // Make sure the file has no active ranges.
    ASSERT_EQ(file->ranges().size(), 0u);

    // Latch the file's current modification time and size.
    auto modified = info.modified();
    auto size = info.size();

    // We should be able to reduce the file's size.
    ASSERT_EQ(execute(truncate, *file, size / 2), FILE_SUCCESS);

    expected.emplace_back(FileTruncateEvent{FileRange(size / 2, size), info.id(), size / 2});

    // Mak sure the file's become dirty.
    EXPECT_TRUE(info.dirty());

    // Make sure the file's modification time and size were updated.
    EXPECT_GE(info.modified(), modified);
    EXPECT_EQ(info.size(), size / 2);

    // The file should still have no active ranges.
    EXPECT_EQ(file->ranges().size(), 0u);

    // Latch the file's current modification time.
    modified = info.modified();

    // We should be able to grow the file's size.
    ASSERT_EQ(execute(truncate, *file, size), FILE_SUCCESS);

    expected.emplace_back(FileTruncateEvent{std::nullopt, info.id(), size});

    // Make sure the file's attributes were updated.
    EXPECT_GE(info.modified(), modified);
    EXPECT_EQ(info.size(), size);

    // There should be a single active range.
    ASSERT_THAT(file->ranges(), ElementsAre(FileRange(size / 2, size)));

    // Make sure we can still read the file's content.
    auto result = execute(read, *file, 0, size);

    // Make sure the read succeeded.
    ASSERT_EQ(result.errorOr(FILE_SUCCESS), FILE_SUCCESS);
    ASSERT_EQ(result->size(), static_cast<std::size_t>(size));

    // Convenience.
    auto length = size / 2;
    auto npos = std::string::npos;

    // Make sure we read what we expected.
    EXPECT_EQ(mFileContent.compare(0, length, *result, 0, length), 0);
    EXPECT_EQ(result->find_first_not_of('\0', length), npos);

    // Make sure we received the events we expected.
    ASSERT_EQ(expected, fileObserver.events());
    ASSERT_EQ(expected, serviceObserver.events());
}

TEST_F(FileServiceTests, write_cancels_orphan_reads)
{
    // Open our test file.
    auto file = mClient->fileOpen(mFileHandle);
    ASSERT_EQ(file.errorOr(FILE_SERVICE_SUCCESS), FILE_SERVICE_SUCCESS);

    // Disable readahead.
    mClient->fileService().options(DisableReadahead);

    // Generate some data to write to our file.
    auto expected = randomBytes(512_KiB);

    // Initiate a read of the file's data.
    //
    // We're not using the read(...) helper function in this case because we
    // don't really care if we get all of the data we've asked for.
    auto read = readOnce(*file, 0, 1_MiB);

    // Write our generated data to the file.
    //
    // This should cancel an orphan read that resulted when our read above
    // completed with only a subset of the requested data.
    ASSERT_EQ(execute(write, expected.data(), *file, 0, expected.size()), FILE_SUCCESS);

    // Make sure our read completed.
    ASSERT_NE(read.wait_for(mDefaultTimeout), timeout);

    // Make sure the data we read was valid.
    auto computed = read.get();

    ASSERT_EQ(computed.errorOr(FILE_SUCCESS), FILE_SUCCESS);
    EXPECT_FALSE(mFileContent.compare(0, computed->size(), *computed));
}

TEST_F(FileServiceTests, write_succeeds)
{
    // Disable readahead.
    mClient->fileService().options(DisableReadahead);

    // File content that's updated as we write.
    auto expected = mFileContent;

    // Open a file for writing.
    auto file = mClient->fileOpen(mFileHandle);

    // Make sure we could actually open the file.
    ASSERT_EQ(file.errorOr(FILE_SERVICE_SUCCESS), FILE_SERVICE_SUCCESS);

    // Read content from the file and make sure it matches our expectations.
    auto read = [&](std::uint64_t offset, std::uint64_t length)
    {
        // Try and read content from our file.
        auto result = execute(testing::read, *file, offset, length);

        // Read failed.
        if (!result)
            return result.error();

        // Content isn't what we expected.
        if (!compare(*result, expected, offset, length))
            return FILE_FAILED;

        // Content satisfies our expectations.
        return FILE_SUCCESS;
    }; // read

    // Write content to our file.
    auto write = [&](const void* content, std::uint64_t offset, std::uint64_t length)
    {
        // Events we want to receive.
        FileEventVector wanted;

        // So we can receive events.
        auto fileObserver = observe(*file);
        auto serviceObserver = observe(mClient->fileService());

        // Get our hands on the file's information.
        auto info = file->info();

        // Latch the file's current access time.
        auto accessed = info.accessed();

        // Latch the file's current modification time and size.
        auto modified = info.modified();

        // Try and write content to our file.
        auto result = execute(testing::write, content, *file, offset, length);

        // Write failed.
        if (result != FILE_SUCCESS)
            return result;

        wanted.emplace_back(FileWriteEvent{FileRange(offset, offset + length), info.id()});

        // Compute size of local file content.
        auto size = std::max<std::uint64_t>(expected.size(), offset + length);

        // Extend local file content as necessary.
        expected.resize(static_cast<std::size_t>(size));

        // Copy written content into our local file content buffer.
        std::memcpy(&expected[offset], content, length);

        // Make sure the file's become dirty.
        EXPECT_TRUE(info.dirty());

        // Make sure the file's access time has been updated.
        EXPECT_GE(info.accessed(), accessed);

        // Make sure the file's modification time hasn't gone backwards.
        EXPECT_GE(info.modified(), modified);

        // Make sure the file's size has been updated correctly.
        EXPECT_EQ(info.size(), size);

        // Make sure we received the events we wanted.
        EXPECT_EQ(fileObserver.events(), wanted);
        EXPECT_EQ(serviceObserver.events(), wanted);

        // One or more of our expectations weren't satisfied.
        if (HasFailure())
            return FILE_FAILED;

        // Let our caller know the write was successful.
        return FILE_SUCCESS;
    }; // write

    // Generate some content for us to write to the file.
    auto computed = randomBytes(256u * 1024);

    // Write 64KiB to the file.
    ASSERT_EQ(write(computed.data(), 64_KiB, 64_KiB), FILE_SUCCESS);

    // Make sure we can read back what we wrote.
    ASSERT_EQ(read(64_KiB, 64_KiB), FILE_SUCCESS);

    // And that the file has a single range.
    ASSERT_THAT(file->ranges(), ElementsAre(FileRange(64_KiB, 128_KiB)));

    // Read 128KiB from the file.
    //
    // This should cause us to download the first 64KiB of the file.
    ASSERT_EQ(read(0, 128_KiB), FILE_SUCCESS);

    // We should have a single 128KiB range.
    ASSERT_THAT(file->ranges(), ElementsAre(FileRange(0, 128_KiB)));

    // Read 128KiB from the file.
    //
    // This should cause the download of two new ranges.
    ASSERT_EQ(read(192_KiB, 64_KiB), FILE_SUCCESS);
    ASSERT_EQ(read(320_KiB, 64_KiB), FILE_SUCCESS);

    // We should now have three ranges.
    ASSERT_THAT(file->ranges(),
                ElementsAre(FileRange(0, 128_KiB),
                            FileRange(192_KiB, 256_KiB),
                            FileRange(320_KiB, 384_KiB)));

    // Write 192KiB to the file.
    ASSERT_EQ(write(computed.data(), 160_KiB, 192_KiB), FILE_SUCCESS);

    // We should now have two ranges.
    ASSERT_THAT(file->ranges(), ElementsAre(FileRange(0, 128_KiB), FileRange(160_KiB, 384_KiB)));

    // Read 384KiB from the file.
    //
    // This should cause us to download another range.
    ASSERT_EQ(read(0, 384_KiB), FILE_SUCCESS);

    // Which should coalesce with all the other ranges.
    ASSERT_THAT(file->ranges(), ElementsAre(FileRange(0, 384_KiB)));

    // Make sure writes can extend the file's size.
    ASSERT_EQ(write(computed.data(), 2_MiB, 64_KiB), FILE_SUCCESS);

    // We should now have two ranges.
    ASSERT_THAT(file->ranges(), ElementsAre(FileRange(0, 384_KiB), FileRange(1024_KiB, 2112_KiB)));

    // Make sure we can read back what we wrote.
    ASSERT_EQ(read(2_MiB, 64_KiB), FILE_SUCCESS);
}

void FileServiceTests::SetUp()
{
    ScopedWatch watch(mWatchdog, MaxTestRunTime);

    SingleClientTest::SetUp();

    // Make sure the service's options are in a known state.
    mClient->fileService().options(DefaultOptions);

    // Make sure the service contains no lingering data.
    ASSERT_EQ(mClient->fileService().purge(), FILE_SERVICE_SUCCESS);

    // Make sure transfers proceed at full speed.
    mClient->setDownloadSpeed(0);
    mClient->setUploadSpeed(0);

    // Make sure the client uses versioning unless disabled explicitly.
    mClient->useVersioning(true);

    // Don't disarm the watchdog.
    watch.release();
}

void FileServiceTests::SetUpTestSuite()
{
    ScopedWatch watch(mWatchdog, MaxTestSetupTime);

    SingleClientTest::SetUpTestSuite();

    // Make sure the test root is clean.
    ASSERT_THAT(mClient->remove("/z"), AnyOf(API_FUSE_ENOTFOUND, API_OK));

    // Recreate the test root.
    auto rootHandle = mClient->makeDirectory("z", "/");
    ASSERT_EQ(rootHandle.errorOr(API_OK), API_OK);

    // Generate content for our test file.
    mFileContent = randomBytes(1_MiB);

    // Generate a name for our test file.
    mFileName = randomName();

    // Upload our content to the cloud.
    auto fileHandle = mClient->upload(mFileContent, mFileName, *rootHandle);
    ASSERT_EQ(fileHandle.errorOr(API_OK), API_OK);

    // Latch the file's handle for later use.
    mFileHandle = *fileHandle;

    // Latch the root handle for later use.
    mRootHandle = *rootHandle;

    // Generate link.
    auto link = mClient->getPublicLink(*rootHandle);
    ASSERT_EQ(link.errorOr(API_OK), API_OK);
}

void FileServiceTests::TearDown()
{
    // Disarm the watchdog.
    mWatchdog.disarm();
}

auto append(const void* buffer, File file, std::uint64_t length) -> std::future<FileResult>
{
    // So we can signal when the request has completed.
    auto notifier = makeSharedPromise<FileResult>();

    // So our caller can wait until the request is completed.
    auto waiter = notifier->get_future();

    // Execute an append request.
    file.append(
        buffer,
        [=](FileResult result)
        {
            notifier->set_value(result);
        },
        length);

    // Return waiter to our caller.
    return waiter;
}

bool compare(const std::string& computed,
             const std::string& expected,
             std::uint64_t offset,
             std::uint64_t length)
{
    // Convenience.
    std::uint64_t size = expected.size();

    // Offset and/or length is out of bounds.
    if (offset + length > size)
        return false;

    // Content is smaller than expected.
    if (computed.size() != length)
        return false;

    // Make sure the content matches our file.
    return !expected.compare(offset, length, computed);
}

template<typename Function, typename... Parameters>
auto execute(Function&& function, Parameters&&... arguments)
    -> std::enable_if_t<IsAsynchronousFunctionCallV<Function, Parameters...>,
                        AsynchronousFunctionCallResultT<Function, Parameters...>>
{
    // Execute function to kick off our request.
    auto waiter =
        std::invoke(std::forward<Function>(function), std::forward<Parameters>(arguments)...);

    // Figure out the function's result type.
    using Result = decltype(waiter.get());

    // Request timed out.
    if (waiter.wait_for(std::chrono::minutes(60)) == timeout)
        return Result(GenerateFailure<Result>::value());

    // Return result to our caller.
    return waiter.get();
}

auto fetch(File file) -> std::future<FileResult>
{
    // So we can signal when the request has completed.
    auto notifier = makeSharedPromise<FileResult>();

    // So our caller can wait until the request has completed.
    auto waiter = notifier->get_future();

    // Execute the fetch request.
    file.fetch(
        [=](auto result)
        {
            notifier->set_value(result);
        });

    // Return waiter to our caller.
    return waiter;
}

auto fetchBarrier(File file) -> std::future<FileResult>
{
    // So we can signal when all fetches have completed.
    auto notifier = makeSharedPromise<FileResult>();

    // So our caller can wait until all fetches have completed.
    auto waiter = notifier->get_future();

    // Execute the fetch request.
    file.fetchBarrier(
        [=]()
        {
            notifier->set_value(FILE_SUCCESS);
        });

    // Return waiter to our caller.
    return waiter;
}

auto flush(File file) -> std::future<FileResult>
{
    // So we can signal when the flush has completed.
    auto notifier = makeSharedPromise<FileResult>();

    // So our caller can wait until the request has completed.
    auto waiter = notifier->get_future();

    // Execute the flush request.
    file.flush(
        [file, notifier](auto result)
        {
            notifier->set_value(result);
        });

    // Return waiter to our caller.
    return waiter;
}

auto purge(File file) -> std::future<FileResult>
{
    // So we can notify our caller when the file has been purged.
    auto notifier = makeSharedPromise<FileResult>();

    // So our caller can wait for the file to be purged.
    auto waiter = notifier->get_future();

    // Try and purge the file.
    file.purge(
        [notifier](auto result)
        {
            notifier->set_value(result);
        });

    // Return the waiter to our caller.
    return waiter;
}

auto read(File file, std::uint64_t offset, std::uint64_t length)
    -> std::future<FileResultOr<std::string>>
{
    struct ReadContext;

    // Convenience.
    using ReadContextPtr = std::shared_ptr<ReadContext>;

    // Tracks state necessary for our read.
    struct ReadContext
    {
        ReadContext(File file, std::uint64_t length):
            mBuffer(),
            mFile(std::move(file)),
            mLength(length),
            mNotifier(),
            mOffset(0u)
        {}

        // Called when we've received content.
        void onRead(ReadContextPtr& context, FileResultOr<FileReadResult> result)
        {
            // Couldn't read content.
            if (!result)
                return mNotifier.set_value(unexpected(result.error()));

            // Convenience.
            auto& source = result->mSource;

            // No more content to read.
            if (!result->mLength)
                return mNotifier.set_value(std::move(mBuffer));

            // Extend buffer as needed.
            mBuffer.resize(mOffset + result->mLength);

            // Try and copy the content to our buffer.
            auto [count, _] = source.read(&mBuffer[mOffset], 0, result->mLength);

            // Couldn't copy the content to our buffer.
            if (count != result->mLength)
                return mNotifier.set_value(unexpected(FILE_FAILED));

            // Bump our offset and length.
            mOffset += count;
            mLength -= count;

            // Read remaining content, if any.
            mFile.read(
                std::bind(&ReadContext::onRead, this, std::move(context), std::placeholders::_1),
                result->mOffset + count,
                mLength);
        }

        // Where we'll store content.
        std::string mBuffer;

        // What file are we reading from?
        File mFile;

        // How much content are we reading?
        std::uint64_t mLength;

        // Who should we notify when the read is complete?
        std::promise<FileResultOr<std::string>> mNotifier;

        // Where in the file are we reading from?
        std::uint64_t mOffset;
    }; // ReadContext

    // Create a context to track our read state.
    auto context = std::make_shared<ReadContext>(file, length);

    // Get our hands on the context's future.
    auto waiter = context->mNotifier.get_future();

    // Kick off the read.
    file.read(
        std::bind(&ReadContext::onRead, context.get(), std::move(context), std::placeholders::_1),
        offset,
        length);

    // Return waiter to our caller.
    return waiter;
}

auto readOnce(File file, std::uint64_t offset, std::uint64_t length)
    -> std::future<FileResultOr<std::string>>
{
    // So we can signal when the read has completed.
    auto notifier = makeSharedPromise<FileResultOr<std::string>>();

    // So our caller can wait until the read has completed.
    auto waiter = notifier->get_future();

    // Try and perform the read.
    file.read(
        [notifier](auto&& result)
        {
            // Couldn't read any data.
            if (!result)
                return notifier->set_value(unexpected(result.error()));

            std::string buffer;

            // Preallocate necessary buffer space.
            buffer.resize(result->mLength);

            // Try and copy data into our buffer.
            auto [count, _] = result->mSource.read(buffer.data(), 0, result->mLength);

            // Couldn't copy all of the data into our buffer.
            if (count < result->mLength)
                return notifier->set_value(unexpected(FILE_FAILED));

            // Transmit buffer to our waiter.
            notifier->set_value(std::move(buffer));
        },
        offset,
        length);

    // Return the waiter to our caller.
    return waiter;
}

auto reclaim(File file) -> std::future<FileResultOr<std::uint64_t>>
{
    // So we can notify our waiter when the request completes.
    auto notifier = makeSharedPromise<FileResultOr<std::uint64_t>>();

    // So our caller can wait until the request completes.
    auto waiter = notifier->get_future();

    // Try and reclaim this file's storage.
    file.reclaim(
        [notifier](FileResultOr<std::uint64_t> result)
        {
            notifier->set_value(result);
        });

    // Return the waiter to our caller.
    return waiter;
}

auto reclaimAll(ClientPtr& client) -> std::future<FileServiceResultOr<std::uint64_t>>
{
    // Sanity.
    assert(client);

    // So we can notify our client when files have been reclaimed.
    auto notifier = makeSharedPromise<FileServiceResultOr<std::uint64_t>>();

    // So our caller can wait until files have been reclaimed.
    auto waiter = notifier->get_future();

    // Try and reclaim zero or more files.
    client->fileService().reclaim(
        [notifier](auto result)
        {
            notifier->set_value(result);
        });

    // Return the waiter to our caller.
    return waiter;
}

auto remove(File file) -> std::future<FileResult>
{
    // So we can notify our caller when the file has been removed.
    auto notifier = makeSharedPromise<FileResult>();

    // So our caller can wait for the file to be removed.
    auto waiter = notifier->get_future();

    // Try and remove the file.
    file.remove(
        [notifier](auto result)
        {
            notifier->set_value(result);
        },
        false);

    // Return the waiter to our caller.
    return waiter;
}

auto touch(File file, std::int64_t modified) -> std::future<FileResult>
{
    // So we can notify our waiter when the request completes.
    auto notifier = makeSharedPromise<FileResult>();

    // So our caller can wait until the request completes.
    auto waiter = notifier->get_future();

    // Try and touch the file.
    file.touch(
        [notifier](FileResult result)
        {
            notifier->set_value(result);
        },
        modified);

    // Return the waiter to our caller.
    return waiter;
}

auto truncate(File file, std::uint64_t newSize) -> std::future<FileResult>
{
    // So we can notify our waiter when the request completes.
    auto notifier = makeSharedPromise<FileResult>();

    // So our caller can wait until the request completes.
    auto waiter = notifier->get_future();

    // Try and truncate the file.
    file.truncate(
        [notifier](FileResult result)
        {
            notifier->set_value(result);
        },
        newSize);

    // Return the waiter to our caller.
    return waiter;
}

auto write(const void* buffer, File file, std::uint64_t offset, std::uint64_t length)
    -> std::future<FileResult>
{
    struct WriteContext;

    // Convenience.
    using WriteContextPtr = std::shared_ptr<WriteContext>;

    // Tracks state necessary for our write.
    struct WriteContext
    {
        WriteContext(const void* buffer, File file, std::uint64_t length):
            mBuffer(static_cast<const std::uint8_t*>(buffer)),
            mFile(std::move(file)),
            mLength(length)
        {}

        // Called when we've written file content.
        void onWrite(WriteContextPtr& context, FileResultOr<FileWriteResult> result)
        {
            // Couldn't write content.
            if (!result)
                return mNotifier.set_value(result.error());

            // No more content to write.
            if (!result->mLength)
                return mNotifier.set_value(FILE_SUCCESS);

            // Bump buffer and length.
            mBuffer += result->mLength;
            mLength -= result->mLength;

            // Write remaning content, if any.
            mFile.write(
                mBuffer,
                std::bind(&WriteContext::onWrite, this, std::move(context), std::placeholders::_1),
                result->mLength + result->mOffset,
                mLength);
        }

        // The content we want to write.
        const std::uint8_t* mBuffer;

        // What file we should write content to.
        File mFile;

        // How much content we should write to our file.
        std::uint64_t mLength;

        // Who should we notify when the write is complete?
        std::promise<FileResult> mNotifier;
    }; // WriteContext

    // Create a context to track our write state.
    auto context = std::make_shared<WriteContext>(buffer, file, length);

    // Get our hands on the context's future.
    auto waiter = context->mNotifier.get_future();

    // Kick off the write.
    file.write(
        buffer,
        std::bind(&WriteContext::onWrite, context.get(), std::move(context), std::placeholders::_1),
        offset,
        length);

    // Return waiter to our caller.
    return waiter;
}

} // testing
} // file_service
} // mega
