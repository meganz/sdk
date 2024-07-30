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
foo.com,https://foo.com/,tx,"hola""""\""\"".,,",
hello.co,https://hello.co/,hello,hello.1234,Description with ñ
test.com,https://test.com/,test3,"hello.12,34",
test.com,https://test.com/,txema,hel\nlo.1234,""
test2.com,https://test2.com/,test,hello.1234,
)"};
    const std::vector<std::vector<std::string_view>> expected{
        {"foo.com",   "https://foo.com/",   "tx",    R"(hola""\"\".,,)", ""                   },
        {"hello.co",  "https://hello.co/",  "hello", "hello.1234",       "Description with ñ"},
        {"test.com",  "https://test.com/",  "test3", "hello.12,34",      ""                   },
        {"test.com",  "https://test.com/",  "txema", "hel\\nlo.1234",    ""                   },
        {"test2.com", "https://test2.com/", "test",  "hello.1234",       ""                   },
    };
    const std::string fname = "test.csv";
    sdk_test::LocalTempFile f{fname, fileContents};

    auto results = parseGooglePasswordCSVFile(fname);
    ASSERT_TRUE(results.mErrMsg.empty());
    ASSERT_EQ(results.mErrCode, PassFileParseResult::ErrCode::OK);

    size_t i = 0;
    ASSERT_EQ(results.mResults.size(), expected.size());
    for (const auto& result: results.mResults)
    {
        EXPECT_EQ(result.mErrCode, PassEntryParseResult::ErrCode::OK);
        const auto& e = expected[i++];
        EXPECT_EQ(result.mName, e[0]);
        EXPECT_EQ(result.mUrl, e[1]);
        EXPECT_EQ(result.mUserName, e[2]);
        EXPECT_EQ(result.mPassword, e[3]);
        EXPECT_EQ(result.mNote, e[4]);
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

    ASSERT_EQ(results.mErrCode, PassFileParseResult::ErrCode::MISSING_COLUMN);
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

    ASSERT_EQ(results.mErrCode, PassFileParseResult::ErrCode::MISSING_COLUMN);
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

    ASSERT_EQ(results.mErrCode, PassFileParseResult::ErrCode::OK);
    ASSERT_EQ(results.mResults.size(), 2);

    // The first is wrong
    const PassEntryParseResult& first = results.mResults[0];
    EXPECT_EQ(first.mErrCode, PassEntryParseResult::ErrCode::INVALID_NUM_OF_COLUMN);
    EXPECT_EQ(first.mLineNumber, 1);

    const PassEntryParseResult& second = results.mResults[1];
    EXPECT_EQ(second.mErrCode, PassEntryParseResult::ErrCode::OK);
    EXPECT_EQ(second.mName, "test.com");
    EXPECT_EQ(second.mUrl, "https://test.com/");
    EXPECT_EQ(second.mUserName, "test3");
    EXPECT_EQ(second.mPassword, "hello.1234");
    EXPECT_EQ(second.mNote, "");
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

    ASSERT_EQ(results.mErrCode, PassFileParseResult::ErrCode::NO_VALID_ENTRIES);
    EXPECT_EQ(results.mErrMsg, "All the entries in the file were wrongly formatted");
    ASSERT_EQ(results.mResults.size(), 2);

    // The first is wrong
    const PassEntryParseResult& first = results.mResults[0];
    EXPECT_EQ(first.mErrCode, PassEntryParseResult::ErrCode::INVALID_NUM_OF_COLUMN);
    EXPECT_EQ(first.mLineNumber, 1);

    const PassEntryParseResult& second = results.mResults[1];
    EXPECT_EQ(second.mErrCode, PassEntryParseResult::ErrCode::INVALID_NUM_OF_COLUMN);
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

    ASSERT_EQ(results.mErrCode, PassFileParseResult::ErrCode::MISSING_COLUMN);
    ASSERT_THAT(results.mErrMsg, HasSubstr("column with name: name"));
    ASSERT_THAT(results.mErrMsg, HasSubstr("column with name: url"));
    ASSERT_THAT(results.mErrMsg, HasSubstr("column with name: username"));
    ASSERT_THAT(results.mErrMsg, HasSubstr("column with name: password"));
    ASSERT_THAT(results.mErrMsg, HasSubstr("column with name: note"));
    ASSERT_THAT(results.mErrMsg, HasSubstr("expected to be a header with the column"));
}

TEST(PWMReadImportFile, FileDoesNotExist)
{
    const std::string fname = "test.csv";
    auto results = readPasswordImportFile(fname, FileSource::GOOGLE_PASSWORD);
    // The file existence is checked at higher levels but a cantOpenFile should be triggered
    ASSERT_EQ(results.mErrCode, PassFileParseResult::ErrCode::CANT_OPEN_FILE);
    ASSERT_THAT(results.mErrMsg, HasSubstr("could not be opened"));
}

TEST(PWMReadImportFile, GooglePassword)
{
    const std::string fname = "test.csv";

    constexpr std::string_view fileContents{R"(name,url,username,password,note
foo.com,https://foo.com/,tx,"hola""""\""\"".,,",
hello.co,https://hello.co/,hello,hello.1234,Description with ñ
test.com,https://test.com/,test3,"hello.12,34",
test.com,https://test.com/,txema,hel\nlo.1234,""
test2.com,https://test2.com/,test,hello.1234,
)"};
    sdk_test::LocalTempFile f{fname, fileContents};
    auto resultsRead = readPasswordImportFile(fname, FileSource::GOOGLE_PASSWORD);
    auto resultsDirect = parseGooglePasswordCSVFile(fname);
    ASSERT_EQ(resultsDirect.mErrMsg, resultsRead.mErrMsg);
    ASSERT_EQ(resultsDirect.mErrCode, resultsRead.mErrCode);
    ASSERT_EQ(resultsDirect.mResults.size(), resultsRead.mResults.size());
}
