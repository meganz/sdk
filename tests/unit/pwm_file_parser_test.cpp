#include "mega/pwm_file_parser.h"
#include "sdk_test_utils.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

using namespace mega::pwm::import;
using testing::HasSubstr;
using testing::Not;

TEST(PWMImportGooglePasswordCSVFile, WellFormatedFile)
{
    constexpr std::string_view fileContents{R"(name,url,username,password,note
hello.co,https://hello.co/,hello,hello.1234,Description with ñ
test.com,https://test.com/,test3,hello.1234,
test.com,https://test.com/,txema,hello.1234,
test2.com,https://test2.com/,test,hello.1234,
)"};
    const std::vector<std::vector<std::string_view>> expected{
        {"hello.co",  "https://hello.co/",  "hello", "hello.1234", "Description with ñ"},
        {"test.com",  "https://test.com/",  "test3", "hello.1234", ""                   },
        {"test.com",  "https://test.com/",  "txema", "hello.1234", ""                   },
        {"test2.com", "https://test2.com/", "test",  "hello.1234", ""                   },
    };
    const std::string fname = "test.csv";
    sdk_test::LocalTempFile f{fname, fileContents};

    auto results = parseGooglePasswordCSVFile(fname);
    ASSERT_TRUE(results.mErrMsg.empty());
    ASSERT_EQ(results.mErrCode, EPassFileParseError::ok);

    size_t i = 0;
    ASSERT_EQ(results.mResults.size(), expected.size());
    for (const auto& result: results.mResults)
    {
        EXPECT_EQ(result.mErrCode, EPassEntryParseError::ok);
        const auto& e = expected[i++];
        EXPECT_EQ(result.mName, e[0]);

        EXPECT_NE(result.mData.url(), nullptr);
        EXPECT_EQ(result.mData.url(), e[1]);

        EXPECT_NE(result.mData.userName(), nullptr);
        EXPECT_EQ(result.mData.userName(), e[2]);

        EXPECT_NE(result.mData.password(), nullptr);
        EXPECT_EQ(result.mData.password(), e[3]);

        if (e[4].empty())
        {
            EXPECT_EQ(result.mData.notes(), nullptr);
        }
        else
        {
            EXPECT_NE(result.mData.notes(), nullptr);
            EXPECT_EQ(result.mData.notes(), e[4]);
        }
    }
}

TEST(PWMImportGooglePasswordCSVFile, MissingHeader)
{
    constexpr std::string_view fileContents{
        R"(hello.co,https://hello.co/,hello,hello.1234,Description with ñ
test2.com,https://test2.com/,test,hello.1234,
)"};
    const std::string fname = "test.csv";
    sdk_test::LocalTempFile f{fname, fileContents};

    auto results = parseGooglePasswordCSVFile(fname);
    ASSERT_THAT(results.mErrMsg, HasSubstr("column with name: name"));
    ASSERT_THAT(results.mErrMsg, HasSubstr("column with name: url"));
    ASSERT_THAT(results.mErrMsg, HasSubstr("column with name: username"));
    ASSERT_THAT(results.mErrMsg, HasSubstr("column with name: password"));
    ASSERT_THAT(results.mErrMsg, HasSubstr("column with name: note"));
    ASSERT_THAT(results.mErrMsg, HasSubstr("expected to be a header with the column"));

    ASSERT_EQ(results.mErrCode, EPassFileParseError::missingColumn);
    ASSERT_TRUE(results.mResults.empty());
}

TEST(PWMImportGooglePasswordCSVFile, MissingColumnInHeader)
{
    constexpr std::string_view fileContents{
        R"(name,url,username,password,noteWrong
hello.co,https://hello.co/,hello,hello.1234,Description with ñ
test2.com,https://test2.com/,test,hello.1234,
)"};
    const std::string fname = "test.csv";
    sdk_test::LocalTempFile f{fname, fileContents};

    auto results = parseGooglePasswordCSVFile(fname);
    ASSERT_THAT(results.mErrMsg, HasSubstr("column with name: note"));
    ASSERT_THAT(results.mErrMsg, Not(HasSubstr("expected to be a header with the column")));

    ASSERT_EQ(results.mErrCode, EPassFileParseError::missingColumn);
    ASSERT_TRUE(results.mResults.empty());
}

TEST(PWMImportGooglePasswordCSVFile, MissingColumnInEntry)
{
    constexpr std::string_view fileContents{
        R"(name,url,username,password,note
https://hello.co/,hello,hello.1234,Description with ñ
test.com,https://test.com/,test3,hello.1234,
)"};
    const std::string fname = "test.csv";
    sdk_test::LocalTempFile f{fname, fileContents};

    auto results = parseGooglePasswordCSVFile(fname);
    ASSERT_TRUE(results.mErrMsg.empty());

    ASSERT_EQ(results.mErrCode, EPassFileParseError::ok);
    ASSERT_EQ(results.mResults.size(), 2);

    // The first is wrong
    const PassEntryParseResult& first = results.mResults[0];
    EXPECT_EQ(first.mErrCode, EPassEntryParseError::invalidNumOfColumns);
    EXPECT_EQ(first.mLineNumber, 1);

    const PassEntryParseResult& second = results.mResults[1];
    EXPECT_EQ(second.mErrCode, EPassEntryParseError::ok);
    EXPECT_EQ(second.mName, "test.com");
    EXPECT_STREQ(second.mData.url(), "https://test.com/");
    EXPECT_STREQ(second.mData.userName(), "test3");
    EXPECT_STREQ(second.mData.password(), "hello.1234");
    EXPECT_EQ(second.mData.notes(), nullptr);
}

TEST(PWMImportGooglePasswordCSVFile, AllEntriesWrong)
{
    constexpr std::string_view fileContents{
        R"(name,url,username,password,note
https://hello.co/,hello,hello.1234,Description with ñ
test.com,https://test.com/,hello.1234,
)"};
    const std::string fname = "test.csv";
    sdk_test::LocalTempFile f{fname, fileContents};

    auto results = parseGooglePasswordCSVFile(fname);

    ASSERT_EQ(results.mErrCode, EPassFileParseError::noValidEntries);
    EXPECT_EQ(results.mErrMsg, "All the entries in the file were wrongly formatted");
    ASSERT_EQ(results.mResults.size(), 2);

    // The first is wrong
    const PassEntryParseResult& first = results.mResults[0];
    EXPECT_EQ(first.mErrCode, EPassEntryParseError::invalidNumOfColumns);
    EXPECT_EQ(first.mLineNumber, 1);

    const PassEntryParseResult& second = results.mResults[1];
    EXPECT_EQ(second.mErrCode, EPassEntryParseError::invalidNumOfColumns);
    EXPECT_EQ(second.mLineNumber, 2);
}

TEST(PWMImportGooglePasswordCSVFile, CompletelyWrongFile)
{
    constexpr std::string_view fileContents{
        R"(This is the conent of a text file not a csv
so this should trigger some errors.
)"};
    const std::string fname = "test.csv";
    sdk_test::LocalTempFile f{fname, fileContents};

    auto results = parseGooglePasswordCSVFile(fname);

    ASSERT_EQ(results.mErrCode, EPassFileParseError::missingColumn);
    ASSERT_THAT(results.mErrMsg, HasSubstr("column with name: name"));
    ASSERT_THAT(results.mErrMsg, HasSubstr("column with name: url"));
    ASSERT_THAT(results.mErrMsg, HasSubstr("column with name: username"));
    ASSERT_THAT(results.mErrMsg, HasSubstr("column with name: password"));
    ASSERT_THAT(results.mErrMsg, HasSubstr("column with name: note"));
    ASSERT_THAT(results.mErrMsg, HasSubstr("expected to be a header with the column"));
}

TEST(PWMImportGooglePasswordCSVFile, FileDoesNotExist)
{
    const std::string fname = "test.csv";
    auto results = parseGooglePasswordCSVFile(fname);
    // The file existence is checked at higher levels but a cantOpenFile should be triggered
    ASSERT_EQ(results.mErrCode, EPassFileParseError::cantOpenFile);
    ASSERT_THAT(results.mErrMsg, HasSubstr("cannot be opened"));
}
