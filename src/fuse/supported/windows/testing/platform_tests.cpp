#include <mega/fuse/common/error_or.h>
#include <mega/fuse/common/inode_info.h>
#include <mega/fuse/common/node_info.h>
#include <mega/fuse/common/testing/client.h>
#include <mega/fuse/common/testing/cloud_path.h>
#include <mega/fuse/common/testing/directory.h>
#include <mega/fuse/common/testing/model.h>
#include <mega/fuse/common/testing/path.h>
#include <mega/fuse/common/testing/utility.h>
#include <mega/fuse/platform/date_time.h>
#include <mega/fuse/platform/handle.h>
#include <mega/fuse/platform/security_descriptor.h>
#include <mega/fuse/platform/security_identifier.h>
#include <mega/fuse/platform/testing/directory_monitor.h>
#include <mega/fuse/platform/testing/platform_tests.h>
#include <mega/fuse/platform/testing/printers.h>
#include <mega/fuse/platform/testing/wrappers.h>
#include <mega/fuse/platform/utility.h>

namespace mega
{
namespace fuse
{
namespace testing
{

using platform::Handle;
using platform::SecurityDescriptor;
using platform::SecurityIdentifier;
using platform::fromWideString;
using platform::readOnlySecurityDescriptor;
using platform::readWriteSecurityDescriptor;

union FileInfo
{
    BY_HANDLE_FILE_INFORMATION mByHandle;
    WIN32_FILE_ATTRIBUTE_DATA mByPath;
}; // FileInfo

TEST_P(FUSEPlatformTests, create_directory_fails_when_below_file)
{
    EXPECT_FALSE(CreateDirectoryP(MountPathW() / "sf0" / "sdx", nullptr));
    EXPECT_EQ(GetLastError(), ERROR_DIRECTORY);
}

TEST_P(FUSEPlatformTests, create_directory_fails_when_read_only)
{
    EXPECT_FALSE(CreateDirectoryP(MountPathR() / "sdx", nullptr));
    EXPECT_EQ(GetLastError(), ERROR_ACCESS_DENIED);

    std::error_code error;

    EXPECT_FALSE(fs::exists(MountPathR() / "sdx", error));
}

TEST_P(FUSEPlatformTests, create_directory_fails_when_unknown)
{
    EXPECT_FALSE(CreateDirectoryP(MountPathW() / "sdx" / "sdy", nullptr));
    EXPECT_EQ(GetLastError(), ERROR_PATH_NOT_FOUND);
}

TEST_P(FUSEPlatformTests, create_directory_succeeds)
{
    EXPECT_TRUE(CreateDirectoryP(MountPathW() / "sdx", nullptr));
    EXPECT_EQ(GetLastError(), ERROR_SUCCESS);

    EXPECT_TRUE(waitFor([&]() {
        // Make sure the new directory's visible in the cloud.
        auto info = ClientW()->get("/x/s/sdx");

        // Directory isn't in the cloud.
        if (!info)
            return false;

        // Wrong name or type.
        if (info->mName != "sdx" || !info->mIsDirectory)
            return false;

        std::error_code error;

        // Make sure the new directory is visible under observer.
        return fs::is_directory(MountPathO() / "sdx", error) && !error;
    }, mDefaultTimeout));
}

TEST_P(FUSEPlatformTests, create_file_fails_when_below_file)
{
    EXPECT_FALSE(CreateFileP(MountPathW() / "sf0" / "sfy",
                             GENERIC_WRITE,
                             0,
                             nullptr,
                             CREATE_NEW,
                             0,
                             Handle<>()));

    EXPECT_EQ(GetLastError(), ERROR_DIRECTORY);
}

TEST_P(FUSEPlatformTests, create_file_fails_when_read_only)
{
    EXPECT_FALSE(CreateFileP(MountPathR() / "sfx",
                             GENERIC_WRITE,
                             0,
                             nullptr,
                             CREATE_NEW,
                             0,
                             Handle<>()));

    EXPECT_EQ(GetLastError(), ERROR_ACCESS_DENIED);
}

TEST_P(FUSEPlatformTests, create_file_fails_when_unknown)
{
    EXPECT_FALSE(CreateFileP(MountPathW() / "sdx" / "sfx",
                             GENERIC_WRITE,
                             0,
                             nullptr,
                             CREATE_NEW,
                             0,
                             Handle<>()));

    EXPECT_EQ(GetLastError(), ERROR_PATH_NOT_FOUND);
}

TEST_P(FUSEPlatformTests, create_file_succeeds)
{
    auto handle = CreateFileP(MountPathW() / "sfx",
                              GENERIC_WRITE,
                              0,
                              nullptr,
                              CREATE_NEW,
                              0,
                              Handle<>());

    EXPECT_TRUE(handle);
    EXPECT_EQ(GetLastError(), ERROR_SUCCESS);

    FileInfo fileInfo;

    EXPECT_TRUE(GetFileInformationByHandle(handle.get(), &fileInfo.mByHandle));
    EXPECT_EQ(GetLastError(), ERROR_SUCCESS);

    EXPECT_FALSE(fileInfo.mByHandle.nFileSizeLow);
    EXPECT_FALSE(fileInfo.mByHandle.nFileSizeHigh);

    EXPECT_TRUE(waitFor([&]() {
        return GetFileAttributesExP(MountPathO() / "sfx",
                                    GetFileExInfoStandard,
                                    &fileInfo.mByPath)
               && GetLastError() == ERROR_SUCCESS
               && !fileInfo.mByPath.nFileSizeLow
               && !fileInfo.mByPath.nFileSizeHigh;
    }, mDefaultTimeout));

    EXPECT_TRUE(GetFileAttributesExP(MountPathO() / "sfx",
                                     GetFileExInfoStandard,
                                     &fileInfo.mByPath));
    EXPECT_EQ(GetLastError(), ERROR_SUCCESS);
    EXPECT_FALSE(fileInfo.mByPath.nFileSizeLow);
    EXPECT_FALSE(fileInfo.mByPath.nFileSizeHigh);

    EXPECT_TRUE(FlushFileBuffers(handle.get()));
    EXPECT_EQ(GetLastError(), ERROR_SUCCESS);

    handle.reset();

    EXPECT_TRUE(waitFor([&]() {
        auto info = ClientW()->get("/x/s/sfx");

        return info && !info->mIsDirectory && !info->mSize;
    }, mDefaultTimeout));

    EXPECT_TRUE(DeleteFileP(MountPathW() / "sfx"));
    EXPECT_EQ(GetLastError(), ERROR_SUCCESS);
}

TEST_P(FUSEPlatformTests, delete_file_fails_when_below_file)
{
    EXPECT_FALSE(DeleteFileP(MountPathW() / "sf0" / "sfx"));
    EXPECT_EQ(GetLastError(), ERROR_DIRECTORY);
}

TEST_P(FUSEPlatformTests, delete_file_fails_when_directory)
{
    EXPECT_FALSE(DeleteFileP(MountPathW() / "sd0"));
    EXPECT_EQ(GetLastError(), ERROR_ACCESS_DENIED);

    std::error_code error;

    EXPECT_TRUE(fs::exists(MountPathW() / "sd0", error));
    EXPECT_FALSE(error);
}

TEST_P(FUSEPlatformTests, delete_file_fails_when_read_only)
{
    EXPECT_FALSE(DeleteFileP(MountPathR() / "sf0"));
    EXPECT_EQ(GetLastError(), ERROR_ACCESS_DENIED);

    std::error_code error;

    EXPECT_TRUE(fs::exists(MountPathR() / "sf0", error));
    EXPECT_FALSE(error);
}

TEST_P(FUSEPlatformTests, delete_file_fails_when_unknown)
{
    EXPECT_FALSE(DeleteFileP(MountPathW() / "sfx"));
    EXPECT_EQ(GetLastError(), ERROR_FILE_NOT_FOUND);
}

TEST_P(FUSEPlatformTests, delete_file_succeeds)
{
    EXPECT_TRUE(DeleteFileP(MountPathW() / "sf0"));
    EXPECT_EQ(GetLastError(), ERROR_SUCCESS);

    std::error_code error;

    EXPECT_TRUE(waitFor([&]() {
        return !ClientW()->get("/x/s/sf0")
               && !fs::exists(MountPathO() / "sf0", error);
    }, mDefaultTimeout));

    EXPECT_FALSE(ClientW()->get("/x/s/sf0"));
    EXPECT_FALSE(fs::exists(MountPathO() / "sf0"));
}

TEST_P(FUSEPlatformTests, find_first_file_fails_when_no_match)
{
    auto info = WIN32_FIND_DATAW();
    auto handle = FindFirstFileP(MountPathW() / "x*", &info);
    EXPECT_FALSE(handle);
    EXPECT_EQ(GetLastError(), ERROR_FILE_NOT_FOUND);
}

TEST_P(FUSEPlatformTests, find_first_file_succeeds_when_singular)
{
    auto sf0 = ClientW()->get("/x/s/sf0");
    ASSERT_TRUE(sf0);

    auto info = WIN32_FIND_DATAW();
    auto handle = FindFirstFileP(MountPathW() / "sf0", &info);
    EXPECT_TRUE(handle);
    ASSERT_EQ(GetLastError(), ERROR_SUCCESS);

    auto& info_ = reinterpret_cast<WIN32_FILE_ATTRIBUTE_DATA&>(info);
    EXPECT_EQ(info_, *sf0);
}

TEST_P(FUSEPlatformTests, find_first_file_succeeds)
{
    std::map<std::string, NodeInfo> expectations;

    {
        auto s = ClientW()->get("/x/s");
        ASSERT_TRUE(s);

        expectations[".."] = *s;

        auto sd0 = ClientW()->get("/x/s/sd0");
        ASSERT_TRUE(sd0);

        expectations["."] = *sd0;

        for (const auto& name : ClientW()->childNames(sd0->mHandle))
        {
            auto child = ClientW()->get(sd0->mHandle, name);
            ASSERT_TRUE(child);

            expectations[child->mName] = *child;
        }

        ASSERT_GT(expectations.size(), 2u);
    }

    auto info = WIN32_FIND_DATAW();
    auto handle = FindFirstFileP(MountPathW() / "sd0" / "*", &info);

    EXPECT_TRUE(handle);

    while (GetLastError() == ERROR_SUCCESS)
    {
        auto name = fromWideString(info.cFileName);
        EXPECT_FALSE(name.empty());

        if (name.empty())
            continue;

        auto i = expectations.find(name);
        EXPECT_NE(i, expectations.end())
          << "Couldn't locate directory entry for: "
          << name;

        if (i == expectations.end())
            continue;

        auto& attributes = reinterpret_cast<WIN32_FILE_ATTRIBUTE_DATA&>(info);
        ASSERT_EQ(attributes, i->second);

        if (attributes != i->second)
            continue;

        expectations.erase(i);

        FindNextFileW(handle.get(), &info);
    }

    EXPECT_EQ(GetLastError(), ERROR_NO_MORE_FILES);
    EXPECT_TRUE(expectations.empty());
}

TEST_P(FUSEPlatformTests, get_file_attributes_fails_when_below_file)
{
    auto buffer = WIN32_FILE_ATTRIBUTE_DATA();

    EXPECT_FALSE(GetFileAttributesExP(MountPathW() / "sf0" / "sdx",
                                      GetFileExInfoStandard,
                                      &buffer));
    EXPECT_EQ(GetLastError(), ERROR_DIRECTORY);
}

TEST_P(FUSEPlatformTests, get_file_attributes_fails_when_unknown)
{
    auto buffer = WIN32_FILE_ATTRIBUTE_DATA();

    EXPECT_FALSE(GetFileAttributesExP(MountPathW() / "sdx",
                                      GetFileExInfoStandard,
                                      &buffer));
    EXPECT_EQ(GetLastError(), ERROR_FILE_NOT_FOUND);
}

TEST_P(FUSEPlatformTests, get_file_attributes_succeeds)
{
    auto buffer = WIN32_FILE_ATTRIBUTE_DATA();
    auto info = ClientRS()->describe(MountPathR() / "sd0");

    ASSERT_TRUE(info);
    EXPECT_TRUE(GetFileAttributesExP(MountPathR() / "sd0",
                                     GetFileExInfoStandard,
                                     &buffer));
    ASSERT_EQ(GetLastError(), ERROR_SUCCESS);
    EXPECT_EQ(buffer, *info);

    info = ClientWS()->describe(MountPathW() / "sf0");

    ASSERT_TRUE(info);
    EXPECT_TRUE(GetFileAttributesExP(MountPathW() / "sf0",
                                     GetFileExInfoStandard,
                                     &buffer));
    ASSERT_EQ(GetLastError(), ERROR_SUCCESS);
    EXPECT_EQ(buffer, *info);

    info = ClientRS()->describe(MountPathR() / "sf0");

    ASSERT_TRUE(info);
    EXPECT_TRUE(GetFileAttributesExP(MountPathR() / "sf0",
                                     GetFileExInfoStandard,
                                     &buffer));
    ASSERT_EQ(GetLastError(), ERROR_SUCCESS);
    EXPECT_EQ(buffer, *info);
}

TEST_P(FUSEPlatformTests, get_file_security_fails_when_below_file)
{
    EXPECT_FALSE(GetFileSecurityP(MountPathW() / "sf0" / "x"));
    EXPECT_EQ(GetLastError(), ERROR_DIRECTORY);
}

TEST_P(FUSEPlatformTests, get_file_security_fails_when_unknown)
{
    EXPECT_FALSE(GetFileSecurityP(MountPathW() / "x"));
    EXPECT_EQ(GetLastError(), ERROR_FILE_NOT_FOUND);
}

TEST_P(FUSEPlatformTests, get_file_security_succeeds)
{
    auto expected = toString(platform::readOnlySecurityDescriptor());

    auto computed = GetFileSecurityP(MountPathR() / "sd0");
    EXPECT_EQ(GetLastError(), ERROR_SUCCESS);
    EXPECT_EQ(toString(computed), expected);

    computed = GetFileSecurityP(MountPathR() / "sf0");
    EXPECT_EQ(GetLastError(), ERROR_SUCCESS);
    EXPECT_EQ(toString(computed), expected);

    expected = toString(platform::readWriteSecurityDescriptor());

    computed = GetFileSecurityP(MountPathW() / "sd0");
    EXPECT_EQ(GetLastError(), ERROR_SUCCESS);
    EXPECT_EQ(toString(computed), expected);

    computed = GetFileSecurityP(MountPathW() / "sf0");
    EXPECT_EQ(GetLastError(), ERROR_SUCCESS);
    EXPECT_EQ(toString(computed), expected);
}

TEST_P(FUSEPlatformTests, move_fails_when_below_file)
{
    EXPECT_FALSE(MoveFileExP(MountPathW() / "sd0",
                             MountPathW() / "sf0" / "sd0",
                             0));

    EXPECT_EQ(GetLastError(), ERROR_DIRECTORY);

    EXPECT_FALSE(MoveFileExP(MountPathW() / "sf0" / "sd0",
                             MountPathW() / "sd0",
                             0));

    EXPECT_EQ(GetLastError(), ERROR_DIRECTORY);

    std::error_code error;

    EXPECT_TRUE(fs::exists(MountPathW() / "sd0", error));
    EXPECT_FALSE(error);
}

TEST_P(FUSEPlatformTests, move_fails_when_read_only)
{
    EXPECT_FALSE(MoveFileExP(MountPathR() / "sf0",
                             MountPathR() / "sd0" / "sf0",
                             0));

    EXPECT_EQ(GetLastError(), ERROR_ACCESS_DENIED);

    std::error_code error;

    EXPECT_TRUE(fs::exists(MountPathR() / "sf0", error));
    EXPECT_FALSE(error);
}

TEST_P(FUSEPlatformTests, move_fails_when_target_exists)
{
    EXPECT_FALSE(MoveFileExP(MountPathW() / "sf0",
                             MountPathW() / "sf1",
                             0));

    EXPECT_EQ(GetLastError(), ERROR_ALREADY_EXISTS);

    std::error_code error;

    EXPECT_TRUE(fs::exists(MountPathW() / "sf0", error));
    EXPECT_FALSE(error);
}

TEST_P(FUSEPlatformTests, move_fails_when_unknown)
{
    EXPECT_FALSE(MoveFileExP(MountPathW() / "sfx",
                             MountPathW() / "sd0" / "sfx",
                             0));

    EXPECT_EQ(GetLastError(), ERROR_FILE_NOT_FOUND);
}

TEST_P(FUSEPlatformTests, move_move_succeeds)
{
    BY_HANDLE_FILE_INFORMATION before;

    EXPECT_TRUE(GetFileInformationByPath(MountPathW() / "sd0", before));
    EXPECT_EQ(GetLastError(), ERROR_SUCCESS);

    EXPECT_TRUE(MoveFileExP(MountPathW() / "sd0",
                            MountPathW() / "sd1" / "sd0",
                            0));

    EXPECT_EQ(GetLastError(), ERROR_SUCCESS);

    BY_HANDLE_FILE_INFORMATION after;

    EXPECT_TRUE(GetFileInformationByPath(MountPathW() / "sd1" / "sd0", after));
    EXPECT_EQ(GetLastError(), ERROR_SUCCESS);
    EXPECT_EQ(after, before);

    std::error_code error;

    EXPECT_TRUE(waitFor([&]() {
        return !ClientW()->get("/x/s/sd0/sd0d0")
               && ClientW()->get("/x/s/sd1/sd0/sd0d0")
               && !fs::exists(MountPathO() / "sd0" / "sd0d0", error)
               && fs::exists(MountPathO() / "sd1" / "sd0" / "sd0d0", error);
    }, mDefaultTimeout));

    EXPECT_FALSE(ClientW()->get("/x/s/sd0/sd0d0"));
    EXPECT_TRUE(ClientW()->get("/x/s/sd1/sd0/sd0d0"));
    EXPECT_FALSE(fs::exists(MountPathO() / "sd0" / "sd0d0", error));
    EXPECT_TRUE(fs::exists(MountPathO() / "sd1" / "sd0" / "sd0d0", error));
}

TEST_P(FUSEPlatformTests, move_rename_succeeds)
{
    BY_HANDLE_FILE_INFORMATION before;

    EXPECT_TRUE(GetFileInformationByPath(MountPathW() / "sf0", before));
    EXPECT_EQ(GetLastError(), ERROR_SUCCESS);

    EXPECT_TRUE(MoveFileExP(MountPathW() / "sf0",
                            MountPathW() / "sfx",
                            0));

    EXPECT_EQ(GetLastError(), ERROR_SUCCESS);

    BY_HANDLE_FILE_INFORMATION after;

    EXPECT_TRUE(GetFileInformationByPath(MountPathW() / "sfx", after));
    EXPECT_EQ(GetLastError(), ERROR_SUCCESS);
    EXPECT_EQ(after, before);

    std::error_code error;

    EXPECT_TRUE(waitFor([&]() {
        return !ClientW()->get("/x/s/sf0")
               && ClientW()->get("/x/s/sfx")
               && !fs::exists(MountPathO() / "sf0", error)
               && fs::exists(MountPathO() / "sfx", error);
    }, mDefaultTimeout));

    EXPECT_FALSE(ClientW()->get("/x/s/sf0"));
    EXPECT_TRUE(ClientW()->get("/x/s/sfx"));
    EXPECT_FALSE(fs::exists(MountPathO() / "sf0", error));
    EXPECT_TRUE(fs::exists(MountPathO() / "sfx", error));
}

TEST_P(FUSEPlatformTests, move_replace_directory_fails)
{
    EXPECT_FALSE(MoveFileExP(MountPathW() / "sd0",
                             MountPathW() / "sd1" / "sd1d0",
                             MOVEFILE_REPLACE_EXISTING));

    EXPECT_EQ(GetLastError(), ERROR_ALREADY_EXISTS);

    std::error_code error;

    EXPECT_TRUE(fs::exists(MountPathW() / "sd0", error));
    EXPECT_FALSE(error);
}

TEST_P(FUSEPlatformTests, move_replace_file_fails_when_target_is_directory)
{
    EXPECT_FALSE(MoveFileExP(MountPathW() / "sf0",
                             MountPathW() / "sd0" / "sd0d0",
                             MOVEFILE_REPLACE_EXISTING));

    EXPECT_EQ(GetLastError(), ERROR_ACCESS_DENIED);

    std::error_code error;

    EXPECT_TRUE(fs::exists(MountPathW() / "sf0", error));
    EXPECT_FALSE(error);
}

TEST_P(FUSEPlatformTests, move_replace_file_cloud_local_succeeds)
{
    BY_HANDLE_FILE_INFORMATION sf0o;
    BY_HANDLE_FILE_INFORMATION sf0w;

    EXPECT_TRUE(GetFileInformationByPath(MountPathO() / "sf0", sf0o));
    ASSERT_EQ(GetLastError(), ERROR_SUCCESS);

    EXPECT_TRUE(GetFileInformationByPath(MountPathW() / "sf0", sf0w));
    ASSERT_EQ(GetLastError(), ERROR_SUCCESS);

    EXPECT_TRUE(CreateFileP(MountPathW() / "sfx",
                            GENERIC_WRITE,
                            0,
                            nullptr,
                            CREATE_NEW,
                            0,
                            Handle<>()));

    EXPECT_EQ(GetLastError(), ERROR_SUCCESS);

    EXPECT_TRUE(MoveFileExP(MountPathW() / "sf0",
                            MountPathW() / "sfx",
                            MOVEFILE_REPLACE_EXISTING));

    ASSERT_EQ(GetLastError(), ERROR_SUCCESS);

    std::error_code error;

    EXPECT_FALSE(fs::exists(MountPathW() / "sf0", error));

    BY_HANDLE_FILE_INFORMATION sfx;

    EXPECT_TRUE(GetFileInformationByPath(MountPathW() / "sfx", sfx));
    EXPECT_EQ(GetLastError(), ERROR_SUCCESS);
    EXPECT_EQ(sf0w, sfx);

    EXPECT_TRUE(waitFor([&]() {
        return !ClientW()->get("/x/s/sf0")
               && ClientW()->get("/x/s/sfx")
               && !fs::exists(MountPathO() / "sf0", error)
               && GetFileInformationByPath(MountPathO() / "sfx", sfx)
               && GetLastError() == ERROR_SUCCESS
               && sf0o == sfx;
    }, mDefaultTimeout));

    EXPECT_FALSE(ClientW()->get("/x/s/sf0"));
    EXPECT_TRUE(ClientW()->get("/x/s/sfx"));
    EXPECT_FALSE(fs::exists(MountPathO() / "sf0", error));
    EXPECT_TRUE(GetFileInformationByPath(MountPathO() / "sfx", sfx));
    EXPECT_EQ(GetLastError(), ERROR_SUCCESS);
    EXPECT_EQ(sf0o, sfx);
}

TEST_P(FUSEPlatformTests, move_replace_file_local_local_succeeds)
{
    EXPECT_TRUE(CreateFileP(MountPathW() / "sfx",
                            GENERIC_WRITE,
                            0,
                            nullptr,
                            CREATE_NEW,
                            0,
                            Handle<>()));

    EXPECT_EQ(GetLastError(), ERROR_SUCCESS);

    EXPECT_TRUE(CreateFileP(MountPathW() / "sfy",
                            GENERIC_WRITE,
                            0,
                            nullptr,
                            CREATE_NEW,
                            0,
                            Handle<>()));

    EXPECT_EQ(GetLastError(), ERROR_SUCCESS);

    BY_HANDLE_FILE_INFORMATION sfxo;
    BY_HANDLE_FILE_INFORMATION sfxw;

    EXPECT_TRUE(GetFileInformationByPath(MountPathW() / "sfx", sfxw));
    EXPECT_EQ(GetLastError(), ERROR_SUCCESS);

    EXPECT_TRUE(waitFor([&]() {
        return GetFileInformationByPath(MountPathO() / "sfx", sfxo)
               && GetLastError() == ERROR_SUCCESS;
    }, mDefaultTimeout));

    EXPECT_TRUE(GetFileInformationByPath(MountPathO() / "sfx", sfxo));
    EXPECT_EQ(GetLastError(), ERROR_SUCCESS);

    EXPECT_TRUE(MoveFileExP(MountPathW() / "sfx",
                            MountPathW() / "sfy",
                            MOVEFILE_REPLACE_EXISTING));

    EXPECT_EQ(GetLastError(), ERROR_SUCCESS);

    std::error_code error;

    EXPECT_FALSE(fs::exists(MountPathW() / "sfx", error));

    BY_HANDLE_FILE_INFORMATION sfy;

    EXPECT_TRUE(GetFileInformationByPath(MountPathW() / "sfy", sfy));
    EXPECT_EQ(GetLastError(), ERROR_SUCCESS);
    EXPECT_EQ(sfxw, sfy);

    EXPECT_TRUE(waitFor([&]() {
        return !fs::exists(MountPathO() / "sfx", error)
               && GetFileInformationByPath(MountPathO() / "sfy", sfy)
               && sfxo == sfy;
    }, mDefaultTimeout));

    EXPECT_FALSE(fs::exists(MountPathO() / "sfx", error));
    EXPECT_TRUE(GetFileInformationByPath(MountPathO() / "sfy", sfy));
    EXPECT_EQ(GetLastError(), ERROR_SUCCESS);
    EXPECT_EQ(sfxo, sfy);

    EXPECT_TRUE(DeleteFileP(MountPathW() / "sfy"));
    EXPECT_EQ(GetLastError(), ERROR_SUCCESS);
}

TEST_P(FUSEPlatformTests, move_replace_file_local_cloud_succeeds)
{
    // Quick hack to make sure the cloud is regenerated.
    ASSERT_EQ(ClientW()->remove("/x/s/sf1"), API_OK);

    EXPECT_TRUE(CreateFileP(MountPathW() / "sfx",
                            GENERIC_WRITE,
                            0,
                            nullptr,
                            CREATE_NEW,
                            0,
                            Handle<>()));

    EXPECT_EQ(GetLastError(), ERROR_SUCCESS);

    BY_HANDLE_FILE_INFORMATION sfxo;
    BY_HANDLE_FILE_INFORMATION sfxw;

    EXPECT_TRUE(GetFileInformationByPath(MountPathW() / "sfx", sfxw));
    EXPECT_EQ(GetLastError(), ERROR_SUCCESS);

    EXPECT_TRUE(waitFor([&]() {
        return GetFileInformationByPath(MountPathO() / "sfx", sfxo);
    }, mDefaultTimeout));

    EXPECT_TRUE(GetFileInformationByPath(MountPathO() / "sfx", sfxo));
    EXPECT_EQ(GetLastError(), ERROR_SUCCESS);

    EXPECT_TRUE(MoveFileExP(MountPathW() / "sfx",
                            MountPathW() / "sf0",
                            MOVEFILE_REPLACE_EXISTING));

    EXPECT_EQ(GetLastError(), ERROR_SUCCESS);

    std::error_code error;

    EXPECT_FALSE(fs::exists(MountPathW() / "sfx", error));

    BY_HANDLE_FILE_INFORMATION sf0;

    EXPECT_TRUE(GetFileInformationByPath(MountPathW() / "sf0", sf0));
    EXPECT_EQ(GetLastError(), ERROR_SUCCESS);
    EXPECT_EQ(sfxw, sf0);

    EXPECT_TRUE(waitFor([&]() {
        return !fs::exists(MountPathO() / "sfx", error)
               && GetFileInformationByPath(MountPathO() / "sf0", sf0)
               && sfxo == sf0;
    }, mDefaultTimeout));

    EXPECT_FALSE(fs::exists(MountPathO() / "sfx", error));
    EXPECT_TRUE(GetFileInformationByPath(MountPathO() / "sf0", sf0));
    EXPECT_EQ(GetLastError(), ERROR_SUCCESS);
    EXPECT_EQ(sfxo, sf0);

    EXPECT_TRUE(DeleteFileP(MountPathW() / "sf0"));
    EXPECT_EQ(GetLastError(), ERROR_SUCCESS);
}

TEST_P(FUSEPlatformTests, move_replace_file_succeeds)
{
    BY_HANDLE_FILE_INFORMATION before;

    EXPECT_TRUE(GetFileInformationByPath(MountPathW() / "sf0", before));
    EXPECT_EQ(GetLastError(), ERROR_SUCCESS);

    EXPECT_TRUE(MoveFileExP(MountPathW() / "sf0",
                            MountPathW() / "sf1",
                            MOVEFILE_REPLACE_EXISTING));

    EXPECT_EQ(GetLastError(), ERROR_SUCCESS);

    BY_HANDLE_FILE_INFORMATION after;

    EXPECT_TRUE(GetFileInformationByPath(MountPathW() / "sf1", after));
    EXPECT_EQ(GetLastError(), ERROR_SUCCESS);
    EXPECT_EQ(after, before);

    std::error_code error;

    EXPECT_TRUE(waitFor([&]() {
        return !ClientW()->get("/x/s/sf0")
               && ClientW()->get("/x/s/sf1")
               && !fs::exists(MountPathO() / "sf0", error)
               && fs::exists(MountPathO() / "sf1", error);
    }, mDefaultTimeout));

    EXPECT_FALSE(ClientW()->get("/x/s/sf0"));
    EXPECT_TRUE(ClientW()->get("/x/s/sf1"));
    EXPECT_FALSE(fs::exists(MountPathO() / "sf0", error));
    EXPECT_TRUE(fs::exists(MountPathO() / "sf1", error));
}

TEST_P(FUSEPlatformTests, open_file_fails_when_below_file)
{
    EXPECT_FALSE(CreateFileP(MountPathR() / "sf0" / "sfx",
                             GENERIC_READ,
                             0,
                             nullptr,
                             OPEN_EXISTING,
                             0,
                             Handle<>()));

    EXPECT_EQ(GetLastError(), ERROR_DIRECTORY);
}

TEST_P(FUSEPlatformTests, open_file_fails_when_unknown)
{
    EXPECT_FALSE(CreateFileP(MountPathR() / "sfx",
                             GENERIC_READ,
                             0,
                             nullptr,
                             OPEN_EXISTING,
                             0,
                             Handle<>()));

    EXPECT_EQ(GetLastError(), ERROR_FILE_NOT_FOUND);
}

TEST_P(FUSEPlatformTests, open_file_succeeds)
{
    // Should be able to open a directory.
    EXPECT_TRUE(CreateFileP(MountPathR() / "sd0",
                            GENERIC_READ,
                            0,
                            nullptr,
                            OPEN_EXISTING,
                            FILE_FLAG_BACKUP_SEMANTICS,
                            Handle<>()));

    EXPECT_EQ(GetLastError(), ERROR_SUCCESS);

    // Should be able to open a file for reading.
    EXPECT_TRUE(CreateFileP(MountPathR() / "sf0",
                            GENERIC_READ,
                            0,
                            nullptr,
                            OPEN_EXISTING,
                            0,
                            Handle<>()));

    EXPECT_EQ(GetLastError(), ERROR_SUCCESS);

    // Should be able to open a file for reading and writing.
    EXPECT_TRUE(CreateFileP(MountPathW() / "sf0",
                            GENERIC_READ | GENERIC_WRITE,
                            0,
                            nullptr,
                            OPEN_EXISTING,
                            0,
                            Handle<>()));

    // Should be able to open a file for writing.
    EXPECT_TRUE(CreateFileP(MountPathW() / "sf0",
                            GENERIC_WRITE,
                            0,
                            nullptr,
                            OPEN_EXISTING,
                            0,
                            Handle<>()));

    EXPECT_EQ(GetLastError(), ERROR_SUCCESS);

    // Should be able to open a file for appending.
    EXPECT_TRUE(CreateFileP(MountPathW() / "sf0",
                            FILE_APPEND_DATA,
                            0,
                            nullptr,
                            OPEN_EXISTING,
                            0,
                            Handle<>()));

    EXPECT_EQ(GetLastError(), ERROR_SUCCESS);
}

TEST_P(FUSEPlatformTests, open_file_truncate_fails_when_read_only)
{
    EXPECT_FALSE(CreateFileP(MountPathR() / "sf0",
                             GENERIC_READ | GENERIC_WRITE,
                             0,
                             nullptr,
                             TRUNCATE_EXISTING,
                             0,
                             Handle<>()));

    EXPECT_EQ(GetLastError(), ERROR_ACCESS_DENIED);
}

TEST_P(FUSEPlatformTests, open_file_truncate_succeeds)
{
    auto handle = CreateFileP(MountPathW() / "sf0",
                              GENERIC_WRITE,
                              0,
                              nullptr,
                              TRUNCATE_EXISTING,
                              0,
                              Handle<>());

    EXPECT_EQ(GetLastError(), ERROR_SUCCESS);
    ASSERT_TRUE(handle);

    FileInfo fileInfo;

    EXPECT_TRUE(GetFileInformationByHandle(handle.get(), &fileInfo.mByHandle));
    EXPECT_EQ(GetLastError(), ERROR_SUCCESS);

    EXPECT_FALSE(fileInfo.mByHandle.nFileSizeLow);
    EXPECT_FALSE(fileInfo.mByHandle.nFileSizeHigh);

    EXPECT_TRUE(waitFor([&]() {
        return GetFileAttributesExP(MountPathO() / "sf0",
                                    GetFileExInfoStandard,
                                    &fileInfo.mByPath)
               && GetLastError() == ERROR_SUCCESS
               && !fileInfo.mByPath.nFileSizeLow
               && !fileInfo.mByPath.nFileSizeHigh;
    }, mDefaultTimeout));

    EXPECT_TRUE(GetFileAttributesExP(MountPathO() / "sf0",
                                     GetFileExInfoStandard,
                                     &fileInfo.mByPath));
    EXPECT_EQ(GetLastError(), ERROR_SUCCESS);
    EXPECT_FALSE(fileInfo.mByPath.nFileSizeLow);
    EXPECT_FALSE(fileInfo.mByPath.nFileSizeHigh);

    EXPECT_TRUE(FlushFileBuffers(handle.get()));
    EXPECT_EQ(GetLastError(), ERROR_SUCCESS);

    EXPECT_TRUE(waitFor([&]() {
        auto info = ClientW()->get("/x/s/sf0");
        return info && !info->mSize;
    }, mDefaultTimeout));

    auto info = ClientW()->get("/x/s/sf0");

    ASSERT_TRUE(info);
    EXPECT_TRUE(info && !info->mSize);
}

TEST_P(FUSEPlatformTests, read_directory_changes_succeeds)
{
    Directory directory(randomName(), mScratchPath);

    {
        Model model;

        model.generate("x/s", 2, 2, 2);
        model.populate(directory.path());
    }

    DirectoryMonitor monitor(directory.path());
}

TEST_P(FUSEPlatformTests, read_fails_when_write_only)
{
    auto handle = CreateFileP(MountPathW() / "sf0",
                              GENERIC_WRITE,
                              0,
                              nullptr,
                              OPEN_EXISTING,
                              0,
                              Handle<>());

    EXPECT_TRUE(handle);
    EXPECT_EQ(GetLastError(), ERROR_SUCCESS);

    char buffer;

    EXPECT_FALSE(ReadFile(handle.get(),
                          &buffer,
                          sizeof(buffer),
                          nullptr,
                          nullptr));

    EXPECT_EQ(GetLastError(), ERROR_ACCESS_DENIED);
}

TEST_P(FUSEPlatformTests, read_succeeds)
{
    auto handle = CreateFileP(MountPathR() / "sf0",
                              GENERIC_READ,
                              0,
                              nullptr,
                              OPEN_EXISTING,
                              0,
                              Handle<>());

    EXPECT_TRUE(handle);

    std::string buffer(32, '\x0');
    DWORD numRead = 0u;

    EXPECT_TRUE(ReadFile(handle.get(),
                         &buffer[0],
                         static_cast<DWORD>(buffer.size()),
                         &numRead,
                         nullptr));

    EXPECT_EQ(GetLastError(), ERROR_SUCCESS);

    buffer.resize(numRead);

    EXPECT_EQ(buffer, "sf0");
}

TEST_P(FUSEPlatformTests, read_write_succeeds)
{
    constexpr auto BYTES_PER_THREAD = 4u;
    constexpr auto NUM_ITERATIONS = 128u;
    constexpr auto NUM_THREADS = 4u;

    std::atomic<bool> terminate{false};

    auto loop = [&](std::size_t index) {
        auto handle = CreateFileP(MountPathW() / "sfx",
                                  GENERIC_READ | GENERIC_WRITE,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE,
                                  nullptr,
                                  OPEN_ALWAYS,
                                  0,
                                  Handle<>());

        if (!handle)
            return terminate.store(true);

        auto offset = static_cast<DWORD>(BYTES_PER_THREAD * index);

        for (auto i = 0u; !terminate && i < NUM_ITERATIONS; ++i)
        {
            if (SetFilePointer(handle.get(),
                               offset,
                               nullptr,
                               FILE_BEGIN)
                != offset)
                return terminate.store(true);

            auto written = randomBytes(BYTES_PER_THREAD);
            auto numWritten = 0ul;

            if (!WriteFile(handle.get(),
                           &written[0],
                           BYTES_PER_THREAD,
                           &numWritten,
                           nullptr))
                return terminate.store(true);

            if (numWritten != BYTES_PER_THREAD)
                return terminate.store(true);

            if (SetFilePointer(handle.get(),
                               offset,
                               nullptr,
                               FILE_BEGIN)
                != offset)
                return terminate.store(true);

            auto read = std::string(BYTES_PER_THREAD, '\x0');
            auto numRead = 0ul;

            if (!ReadFile(handle.get(),
                          &read[0],
                          BYTES_PER_THREAD,
                          &numRead,
                          nullptr))
                return terminate.store(true);

            if (numRead != BYTES_PER_THREAD || read != written)
                return terminate.store(true);
        }
    }; // loop

    std::vector<std::thread> threads;

    for (auto i = 0u; i < NUM_THREADS; ++i)
        threads.emplace_back(std::thread(std::bind(loop, i)));

    while (!threads.empty())
    {
        threads.back().join();
        threads.pop_back();
    }

    EXPECT_FALSE(terminate);

    EXPECT_TRUE(DeleteFileP(MountPathW() / "sfx"));
    EXPECT_EQ(GetLastError(), ERROR_SUCCESS);
}

TEST_P(FUSEPlatformTests, remove_directory_fails_when_below_file)
{
    EXPECT_FALSE(RemoveDirectoryP(MountPathW() / "sf0" / "sdx"));
    EXPECT_EQ(GetLastError(), ERROR_DIRECTORY);
}

TEST_P(FUSEPlatformTests, remove_directory_fails_when_file)
{
    EXPECT_FALSE(RemoveDirectoryP(MountPathW() / "sf0"));
    EXPECT_EQ(GetLastError(), ERROR_DIRECTORY);
}

TEST_P(FUSEPlatformTests, remove_directory_fails_when_not_empty)
{
    EXPECT_FALSE(RemoveDirectoryP(MountPathW() / "sd0"));
    EXPECT_EQ(GetLastError(), ERROR_DIR_NOT_EMPTY);

    std::error_code error;

    EXPECT_TRUE(fs::exists(MountPathW() / "sd0", error));
    EXPECT_FALSE(error);
}

TEST_P(FUSEPlatformTests, remove_directory_fails_when_read_only)
{
    EXPECT_FALSE(RemoveDirectoryP(MountPathR() / "sd0" / "sd0d0"));
    EXPECT_EQ(GetLastError(), ERROR_ACCESS_DENIED);

    std::error_code error;

    EXPECT_TRUE(fs::exists(MountPathW() / "sd0" / "sd0d0", error));
    EXPECT_FALSE(error);
}

TEST_P(FUSEPlatformTests, remove_directory_fails_when_unknown)
{
    EXPECT_FALSE(RemoveDirectoryP(MountPathW() / "sdx"));
    EXPECT_EQ(GetLastError(), ERROR_FILE_NOT_FOUND);
}

TEST_P(FUSEPlatformTests, remove_directory_succeeds)
{
    EXPECT_TRUE(RemoveDirectoryP(MountPathW() / "sd0" / "sd0d0"));
    EXPECT_EQ(GetLastError(), ERROR_SUCCESS);

    std::error_code error;

    EXPECT_TRUE(waitFor([&]() {
        return !ClientW()->get("/x/s/sd0/sd0d0")
               && !fs::exists(MountPathW() / "sd0" / "sd0d0", error);
    }, mDefaultTimeout));

    EXPECT_FALSE(ClientW()->get("/x/s/sd0/sd0d0"));
    EXPECT_FALSE(fs::exists(MountPathW() / "sd0" / "sd0d0", error));
}

TEST_P(FUSEPlatformTests, set_attributes_fails_when_attributes_changed)
{
    auto before = GetFileAttributesP(MountPathW() / "sf0");
    EXPECT_NE(before, INVALID_FILE_ATTRIBUTES);
    EXPECT_EQ(GetLastError(), ERROR_SUCCESS);

    EXPECT_FALSE(SetFileAttributesP(MountPathW() / "sf0",
                                    FILE_ATTRIBUTE_READONLY));
    EXPECT_EQ(GetLastError(), ERROR_ACCESS_DENIED);

    SetLastError(ERROR_SUCCESS);

    auto after = GetFileAttributesP(MountPathW() / "sf0");
    EXPECT_EQ(GetLastError(), ERROR_SUCCESS);
    EXPECT_EQ(after, before);
}

TEST_P(FUSEPlatformTests, set_attributes_fails_when_read_only)
{
    auto before = GetFileAttributesP(MountPathR() / "sf0");
    EXPECT_NE(before, INVALID_FILE_ATTRIBUTES);
    ASSERT_EQ(GetLastError(), ERROR_SUCCESS);

    EXPECT_FALSE(SetFileAttributesP(MountPathR() / "sf0", before));
    EXPECT_EQ(GetLastError(), ERROR_ACCESS_DENIED);

    SetLastError(ERROR_SUCCESS);

    auto after = GetFileAttributesP(MountPathR() / "sf0");
    EXPECT_EQ(GetLastError(), ERROR_SUCCESS);
    EXPECT_EQ(after, before);
}

TEST_P(FUSEPlatformTests, set_attributes_succeeds)
{
    auto before = GetFileAttributesP(MountPathW() / "sf0");
    EXPECT_NE(before, INVALID_FILE_ATTRIBUTES);
    ASSERT_EQ(GetLastError(), ERROR_SUCCESS);

    EXPECT_TRUE(SetFileAttributesP(MountPathW() / "sf0", before));
    EXPECT_EQ(GetLastError(), ERROR_SUCCESS);

    auto after = GetFileAttributesP(MountPathW() / "sf0");
    EXPECT_EQ(GetLastError(), ERROR_SUCCESS);
    EXPECT_EQ(after, before);
}

TEST_P(FUSEPlatformTests, set_file_security_fails_when_below_file)
{
    auto descriptor = readOnlySecurityDescriptor();

    EXPECT_FALSE(SetFileSecurityP(MountPathW() / "sf0" / "x", descriptor));
    EXPECT_EQ(GetLastError(), ERROR_DIRECTORY);
}

TEST_P(FUSEPlatformTests, set_file_security_fails_when_changed)
{
    auto before = GetFileSecurityP(MountPathW() / "sf0");
    EXPECT_TRUE(before);

    auto after = readOnlySecurityDescriptor();

    EXPECT_FALSE(SetFileSecurityP(MountPathW() / "sf0", after));
    EXPECT_EQ(GetLastError(), ERROR_ACCESS_DENIED);

    after = GetFileSecurityP(MountPathW() / "sf0");
    EXPECT_TRUE(after);
    EXPECT_EQ(GetLastError(), ERROR_SUCCESS);
    EXPECT_EQ(after, before);
}

TEST_P(FUSEPlatformTests, set_file_security_fails_when_read_only)
{
    auto before = GetFileSecurityP(MountPathR() / "sf0");
    EXPECT_TRUE(before);
    EXPECT_EQ(GetLastError(), ERROR_SUCCESS);

    auto after = readWriteSecurityDescriptor();

    EXPECT_FALSE(SetFileSecurityP(MountPathR() / "sf0", after));
    EXPECT_EQ(GetLastError(), ERROR_ACCESS_DENIED);

    after = GetFileSecurityP(MountPathR() / "sf0");
    EXPECT_TRUE(after);
    EXPECT_EQ(GetLastError(), ERROR_SUCCESS);
    EXPECT_EQ(after, before);
}

TEST_P(FUSEPlatformTests, set_file_security_fails_when_unknown)
{
    auto descriptor = readWriteSecurityDescriptor();

    EXPECT_FALSE(SetFileSecurityP(MountPathW() / "sfx", descriptor));
    EXPECT_EQ(GetLastError(), ERROR_FILE_NOT_FOUND);
}

TEST_P(FUSEPlatformTests, set_file_security_succeeds)
{
    auto before = GetFileSecurityP(MountPathW() / "sf0");
    EXPECT_TRUE(before);

    EXPECT_TRUE(SetFileSecurityP(MountPathW() / "sf0", before));
    EXPECT_EQ(GetLastError(), ERROR_SUCCESS);

    auto after = GetFileSecurityP(MountPathW() / "sf0");
    EXPECT_TRUE(after);
    EXPECT_EQ(GetLastError(), ERROR_SUCCESS);
    EXPECT_EQ(after, before);
}

TEST_P(FUSEPlatformTests, set_file_time_fails_when_changing_creation_time)
{
    auto handle = CreateFileP(MountPathW() / "sf0",
                              GENERIC_READ | GENERIC_WRITE,
                              0,
                              nullptr,
                              OPEN_EXISTING,
                              0,
                              Handle<>());

    EXPECT_TRUE(handle);
    EXPECT_EQ(GetLastError(), ERROR_SUCCESS);

    FileTimes before;

    EXPECT_TRUE(GetFileTime(handle.get(),
                            &before.mCreated,
                            &before.mAccessed,
                            &before.mWritten));
    EXPECT_EQ(GetLastError(), ERROR_SUCCESS);

    auto after = before;
    auto next = std::chrono::system_clock::now() + std::chrono::minutes(5);

    after.mCreated = DateTime(next);

    EXPECT_FALSE(SetFileTime(handle.get(),
                             &after.mCreated,
                             &after.mAccessed,
                             &after.mWritten));
    EXPECT_EQ(GetLastError(), ERROR_ACCESS_DENIED);

    SetLastError(ERROR_SUCCESS);

    EXPECT_TRUE(GetFileTime(handle.get(),
                            &after.mCreated,
                            &after.mAccessed,
                            &after.mWritten));
    EXPECT_EQ(GetLastError(), ERROR_SUCCESS);

    EXPECT_EQ(after, before);
}

TEST_P(FUSEPlatformTests, set_file_time_fails_when_read_only)
{
    auto handle = CreateFileP(MountPathR() / "sf0",
                              GENERIC_READ,
                              0,
                              nullptr,
                              OPEN_EXISTING,
                              0,
                              Handle<>());

    EXPECT_TRUE(handle);
    EXPECT_EQ(GetLastError(), ERROR_SUCCESS);

    FileTimes before;

    EXPECT_TRUE(GetFileTime(handle.get(),
                            &before.mCreated,
                            &before.mAccessed,
                            &before.mWritten));
    EXPECT_EQ(GetLastError(), ERROR_SUCCESS);

    auto after = before;
    auto next = std::chrono::system_clock::now() + std::chrono::minutes(5);

    after.mWritten = DateTime(next);

    EXPECT_FALSE(SetFileTime(handle.get(),
                             &after.mCreated,
                             &after.mAccessed,
                             &after.mWritten));
    EXPECT_EQ(GetLastError(), ERROR_ACCESS_DENIED);

    SetLastError(ERROR_SUCCESS);

    EXPECT_TRUE(GetFileTime(handle.get(),
                            &after.mCreated,
                            &after.mAccessed,
                            &after.mWritten));
    EXPECT_EQ(GetLastError(), ERROR_SUCCESS);
    EXPECT_EQ(after, before);
}

TEST_P(FUSEPlatformTests, set_file_time_succeeds)
{
    auto handle = CreateFileP(MountPathW() / "sf0",
                              GENERIC_READ | GENERIC_WRITE,
                              0,
                              nullptr,
                              OPEN_EXISTING,
                              0,
                              Handle<>());

    EXPECT_TRUE(handle);
    EXPECT_EQ(GetLastError(), ERROR_SUCCESS);

    BY_HANDLE_FILE_INFORMATION expected;

    EXPECT_TRUE(GetFileInformationByHandle(handle.get(), &expected));
    EXPECT_EQ(GetLastError(), ERROR_SUCCESS);

    auto next = std::chrono::system_clock::now() + std::chrono::minutes(5);

    expected.ftCreationTime = DateTime(next);
    expected.ftLastAccessTime = DateTime(next);
    expected.ftLastWriteTime = DateTime(next);

    EXPECT_TRUE(SetFileTime(handle.get(),
                            nullptr,
                            nullptr,
                            &expected.ftLastWriteTime));
    EXPECT_EQ(GetLastError(), ERROR_SUCCESS);

    FileInfo computed;

    EXPECT_TRUE(GetFileInformationByHandle(handle.get(), &computed.mByHandle));
    EXPECT_EQ(GetLastError(), ERROR_SUCCESS);
    EXPECT_EQ(computed.mByHandle, expected);

    EXPECT_TRUE(waitFor([&]() {
        return GetFileAttributesExP(MountPathO() / "sf0",
                                    GetFileExInfoStandard,
                                    &computed.mByPath)
               && GetLastError() == ERROR_SUCCESS
               && computed.mByPath == expected;
    }, mDefaultTimeout));

    EXPECT_TRUE(GetFileAttributesExP(MountPathO() / "sf0",
                                     GetFileExInfoStandard,
                                     &computed.mByPath));
    EXPECT_EQ(GetLastError(), ERROR_SUCCESS);
    EXPECT_EQ(computed.mByPath, expected);

    // Make sure our changes have hit the cloud.
    EXPECT_TRUE(FlushFileBuffers(handle.get()));
    EXPECT_EQ(GetLastError(), ERROR_SUCCESS);

    EXPECT_TRUE(waitFor([&]() {
        auto info = ClientW()->get("/x/s/sf0");
        return info && expected == *info;
    }, mDefaultTimeout));

    auto info = ClientW()->get("/x/s/sf0");
    ASSERT_TRUE(info);
    EXPECT_EQ(expected, *info);
}

TEST_P(FUSEPlatformTests, truncate_fails_when_read_only)
{
    auto sf0 = ClientW()->get("/x/s/sf0");
    ASSERT_TRUE(sf0);

    sf0->mPermissions = RDONLY;

    auto handle = CreateFileP(MountPathR() / "sf0",
                              GENERIC_READ,
                              0,
                              nullptr,
                              OPEN_EXISTING,
                              0,
                              Handle<>());

    EXPECT_EQ(GetLastError(), ERROR_SUCCESS);
    ASSERT_TRUE(handle);

    EXPECT_FALSE(SetFilePointer(handle.get(), 0, nullptr, FILE_BEGIN));
    EXPECT_EQ(GetLastError(), ERROR_SUCCESS);

    EXPECT_FALSE(SetEndOfFile(handle.get()));
    EXPECT_EQ(GetLastError(), ERROR_ACCESS_DENIED);

    WIN32_FILE_ATTRIBUTE_DATA info;

    SetLastError(ERROR_SUCCESS);

    EXPECT_TRUE(GetFileAttributesExP(MountPathR() / "sf0",
                                     GetFileExInfoStandard,
                                     &info));
    EXPECT_EQ(GetLastError(), ERROR_SUCCESS);
    EXPECT_EQ(info, *sf0);
}

TEST_P(FUSEPlatformTests, truncate_succeeds)
{
    auto handle = CreateFileP(MountPathW() / "sf0",
                              GENERIC_READ | GENERIC_WRITE,
                              0,
                              nullptr,
                              OPEN_EXISTING,
                              0,
                              Handle<>());

    EXPECT_EQ(GetLastError(), ERROR_SUCCESS);
    ASSERT_TRUE(handle);

    EXPECT_FALSE(SetFilePointer(handle.get(), 0, nullptr, FILE_BEGIN));
    EXPECT_EQ(GetLastError(), ERROR_SUCCESS);

    EXPECT_TRUE(SetEndOfFile(handle.get()));
    EXPECT_EQ(GetLastError(), ERROR_SUCCESS);

    FileInfo fileInfo;

    EXPECT_TRUE(GetFileInformationByHandle(handle.get(), &fileInfo.mByHandle));
    EXPECT_EQ(GetLastError(), ERROR_SUCCESS);
    EXPECT_FALSE(fileInfo.mByHandle.nFileSizeLow);
    EXPECT_FALSE(fileInfo.mByHandle.nFileSizeHigh);

    EXPECT_TRUE(waitFor([&]() {
        return GetFileAttributesExP(MountPathO() / "sf0",
                                    GetFileExInfoStandard,
                                    &fileInfo.mByPath)
               && GetLastError() == ERROR_SUCCESS
               && !fileInfo.mByPath.nFileSizeLow
               && !fileInfo.mByPath.nFileSizeHigh;
    }, mDefaultTimeout));

    EXPECT_TRUE(FlushFileBuffers(handle.get()));
    EXPECT_EQ(GetLastError(), ERROR_SUCCESS);

    EXPECT_TRUE(waitFor([&]() {
        auto info = ClientW()->get("/x/s/sf0");
        return info && !info->mSize;
    }, mDefaultTimeout));

    auto info = ClientW()->get("/x/s/sf0");

    EXPECT_TRUE(info);
    EXPECT_TRUE(info && !info->mSize);
}

TEST_P(FUSEPlatformTests, write_fails_when_read_only)
{
    auto handle = CreateFileP(MountPathW() / "sf0",
                              GENERIC_READ,
                              0,
                              nullptr,
                              OPEN_EXISTING,
                              0,
                              Handle<>());

    EXPECT_TRUE(handle);
    EXPECT_EQ(GetLastError(), ERROR_SUCCESS);

    char dummy;
    DWORD numWritten = 0u;

    EXPECT_FALSE(WriteFile(handle.get(),
                           &dummy,
                           sizeof(dummy),
                           &numWritten,
                           nullptr));

    EXPECT_EQ(GetLastError(), ERROR_ACCESS_DENIED);
    EXPECT_FALSE(numWritten);
}

TEST_P(FUSEPlatformTests, write_append_succeeds)
{
    auto handle = CreateFileP(MountPathW() / "sf0",
                              GENERIC_READ | FILE_APPEND_DATA,
                              0,
                              nullptr,
                              OPEN_EXISTING,
                              0,
                              Handle<>());

    EXPECT_TRUE(handle);
    EXPECT_EQ(GetLastError(), ERROR_SUCCESS);

    auto written = randomBytes(32);
    auto numWritten = 0ul;

    EXPECT_TRUE(WriteFile(handle.get(),
                          &written[0],
                          static_cast<DWORD>(written.size()),
                          &numWritten,
                          nullptr));

    EXPECT_EQ(GetLastError(), ERROR_SUCCESS);
    EXPECT_EQ(numWritten, static_cast<DWORD>(written.size()));

    SetFilePointer(handle.get(), 0, nullptr, FILE_BEGIN);

    EXPECT_EQ(GetLastError(), ERROR_SUCCESS);

    written.insert(0, "sf0");

    auto read = std::string(written.size(), '\x0');
    auto numRead = 0ul;

    EXPECT_TRUE(ReadFile(handle.get(),
                         &read[0],
                         static_cast<DWORD>(written.size()),
                         &numRead,
                         nullptr));

    EXPECT_EQ(GetLastError(), ERROR_SUCCESS);
    EXPECT_EQ(numRead, static_cast<DWORD>(written.size()));
    EXPECT_EQ(read, written);

    SetFilePointer(handle.get(), 0, nullptr, FILE_BEGIN);

    EXPECT_EQ(GetLastError(), ERROR_SUCCESS);

    EXPECT_TRUE(WriteFile(handle.get(),
                          &written[0],
                          static_cast<DWORD>(written.size()),
                          &numWritten,
                          nullptr));

    EXPECT_EQ(GetLastError(), ERROR_SUCCESS);
    EXPECT_EQ(numWritten, static_cast<DWORD>(written.size()));

    written.append(read);

    read.resize(written.size());

    SetFilePointer(handle.get(), 0, nullptr, FILE_BEGIN);

    EXPECT_TRUE(ReadFile(handle.get(),
                         &read[0],
                         static_cast<DWORD>(written.size()),
                         &numRead,
                         nullptr));

    EXPECT_EQ(GetLastError(), ERROR_SUCCESS);
    EXPECT_EQ(numRead, static_cast<DWORD>(written.size()));
    EXPECT_EQ(read, written);

    handle.reset();

    EXPECT_TRUE(DeleteFileP(MountPathW() / "sf0"));
    EXPECT_EQ(GetLastError(), ERROR_SUCCESS);
}

TEST_P(FUSEPlatformTests, write_succeeds)
{
    auto sfxW = CreateFileP(MountPathW() / "sfx",
                            GENERIC_READ | GENERIC_WRITE,
                            0,
                            nullptr,
                            CREATE_NEW,
                            0,
                            Handle<>());

    EXPECT_TRUE(sfxW);
    EXPECT_EQ(GetLastError(), ERROR_SUCCESS);

    std::error_code error;

    EXPECT_TRUE(waitFor([&]() {
        return fs::exists(MountPathO() / "sfx", error);
    }, mDefaultTimeout));

    auto sfxO = CreateFileP(MountPathO() / "sfx",
                            GENERIC_READ,
                            0,
                            nullptr,
                            OPEN_EXISTING,
                            0,
                            Handle<>());

    EXPECT_TRUE(sfxO);
    EXPECT_EQ(GetLastError(), ERROR_SUCCESS);

    auto written = randomBytes(32);
    auto numWritten = 0ul;

    EXPECT_TRUE(WriteFile(sfxW.get(),
                          &written[0],
                          static_cast<DWORD>(written.size()),
                          &numWritten,
                          nullptr));

    EXPECT_EQ(GetLastError(), ERROR_SUCCESS);
    EXPECT_EQ(numWritten, static_cast<DWORD>(written.size()));

    SetFilePointer(sfxW.get(), 0, nullptr, FILE_BEGIN);

    EXPECT_EQ(GetLastError(), ERROR_SUCCESS);

    auto read = std::string(numWritten, '\x0');
    auto numRead = 0ul;

    EXPECT_TRUE(ReadFile(sfxW.get(),
                         &read[0],
                         numWritten,
                         &numRead,
                         nullptr));

    EXPECT_EQ(GetLastError(), ERROR_SUCCESS);
    EXPECT_EQ(numRead, numWritten);
    EXPECT_EQ(read, written);

    EXPECT_TRUE(waitFor([&]() {
        return GetFileSize(sfxO.get(), nullptr) == numWritten;
    }, mDefaultTimeout));

    read.assign(32, '\x0');

    EXPECT_TRUE(ReadFile(sfxO.get(),
                         &read[0],
                         numWritten,
                         &numRead,
                         nullptr));

    EXPECT_EQ(GetLastError(), ERROR_SUCCESS);
    EXPECT_EQ(numRead, numWritten);
    EXPECT_EQ(read, written);

    SetFilePointer(sfxW.get(), 0, nullptr, FILE_BEGIN);

    EXPECT_EQ(GetLastError(), ERROR_SUCCESS);

    written = randomBytes(64);

    EXPECT_TRUE(WriteFile(sfxW.get(),
                          &written[0],
                          static_cast<DWORD>(written.size()),
                          &numWritten,
                          nullptr));

    EXPECT_EQ(GetLastError(), ERROR_SUCCESS);
    EXPECT_EQ(numWritten, static_cast<DWORD>(written.size()));

    EXPECT_TRUE(waitFor([&]() {
        return GetFileSize(sfxO.get(), nullptr) == numWritten;
    }, mDefaultTimeout));

    SetFilePointer(sfxO.get(), 0, nullptr, FILE_BEGIN);

    EXPECT_EQ(GetLastError(), ERROR_SUCCESS);

    read.resize(numWritten);

    EXPECT_TRUE(ReadFile(sfxO.get(),
                         &read[0],
                         numWritten,
                         &numRead,
                         nullptr));

    EXPECT_EQ(GetLastError(), ERROR_SUCCESS);
    EXPECT_EQ(numRead, numWritten);
    EXPECT_EQ(read, written);

    sfxO.reset();
    sfxW.reset();

    EXPECT_TRUE(DeleteFileP(MountPathW() / "sfx"));
    EXPECT_EQ(GetLastError(), ERROR_SUCCESS);
}

} // testing
} // fuse
} // mega

