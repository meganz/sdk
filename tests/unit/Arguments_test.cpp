#include "gtest/gtest.h"
#include <array>
#include <vector>
#include "mega/arguments.h"

using mega::ArgumentsParser;

TEST(Argumenmts, ParseNoArgumentsSuccessfully)
{
    std::array<char*, 2> argv = { "executable.exe",
                                   nullptr
                                };
    auto arguments = ArgumentsParser::parse(static_cast<int>(argv.size() - 1), argv.data());
    ASSERT_TRUE(arguments.empty());
}

TEST(Argumenmts, ParseOneNoValueArgumentSuccessfully)
{
    std::array<char*, 3> argv = { "executable.exe",
                                  "-h",
                                   nullptr
                                };
    auto arguments = ArgumentsParser::parse(static_cast<int>(argv.size() - 1), argv.data());
    ASSERT_TRUE(!arguments.empty());
    ASSERT_TRUE(arguments.contains("-h"));
    ASSERT_EQ("", arguments.getValue("-h"));
}

TEST(Argumenmts, ParseOneHasValueArgumentSuccessfully)
{
    std::array<char*, 3> argv = { "executable.exe",
                                  "-t=10",
                                   nullptr
                                };
    auto arguments = ArgumentsParser::parse(static_cast<int>(argv.size() - 1), argv.data());
    ASSERT_FALSE(arguments.empty());
    ASSERT_TRUE(arguments.contains("-t"));
    ASSERT_EQ("10", arguments.getValue("-t"));
}

TEST(Argumenmts, ParseOneListOfArgumentsSuccessfully)
{
    std::array<char*, 5> argv = { "executable.exe",
                                  "-h",
                                  "-t=10",
                                  "-n=the name",
                                   nullptr
                                };
    auto arguments = ArgumentsParser::parse(static_cast<int>(argv.size() - 1), argv.data());
    ASSERT_FALSE(arguments.empty());
    ASSERT_EQ(3, arguments.size());
    ASSERT_EQ("", arguments.getValue("-h"));
    ASSERT_EQ("10", arguments.getValue("-t"));
    ASSERT_EQ("the name", arguments.getValue("-n"));
    ASSERT_FALSE(arguments.contains("-xxx"));
    ASSERT_EQ("", arguments.getValue("-xxx"));
}

TEST(Argumenmts, getValueDefaultIsNotReturnDefaultIfValueIsEmpty)
{
    std::array<char*, 3> argv = { "executable.exe",
                                  "-h",
                                   nullptr
                                };
    auto arguments = ArgumentsParser::parse(static_cast<int>(argv.size() - 1), argv.data());
    ASSERT_EQ("", arguments.getValue("-h", "default"));
}

TEST(Argumenmts, getValueDefaultReturnedDefaultIfNameNotExist)
{
    std::array<char*, 3> argv = { "executable.exe",
                                  "-h",
                                   nullptr
                                };
    auto arguments = ArgumentsParser::parse(static_cast<int>(argv.size() - 1), argv.data());
    ASSERT_EQ("default", arguments.getValue("-x", "default"));
}
