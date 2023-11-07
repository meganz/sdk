#include "gtest/gtest.h"
#include <vector>
#include "gfxworker/arguments.h"

using mega::gfx::Arguments;

TEST(Argumenmts, ParseNoArgumentsSuccessfully)
{
    std::vector<std::string> argv;
    Arguments arguments(argv);
    ASSERT_TRUE(arguments.empty());
}

TEST(Argumenmts, ParseOneNoValueArgumentSuccessfully)
{
    std::vector<std::string> argv = { std::string("-h")};
    Arguments arguments(argv);
    ASSERT_TRUE(!arguments.empty());
    ASSERT_TRUE(arguments.contains("-h"));
    ASSERT_EQ("", arguments.getValue("-h"));
}

TEST(Argumenmts, ParseOneHasValueArgumentSuccessfully)
{
    std::vector<std::string> argv = { std::string("-t=10")};
    Arguments arguments(argv);
    ASSERT_FALSE(arguments.empty());
    ASSERT_TRUE(arguments.contains("-t"));
    ASSERT_EQ("10", arguments.getValue("-t"));
}

TEST(Argumenmts, ParseOneListOfArgumentsSuccessfully)
{
    std::vector<std::string> argv = { std::string("-h"), std::string("-t=10"), std::string("-n=the name")};
    Arguments arguments(argv);
    ASSERT_FALSE(arguments.empty());
    ASSERT_EQ(argv.size(), arguments.size());
    ASSERT_EQ("", arguments.getValue("-h"));
    ASSERT_EQ("10", arguments.getValue("-t"));
    ASSERT_EQ("the name", arguments.getValue("-n"));
    ASSERT_FALSE(arguments.contains("-xxx"));
    ASSERT_EQ("", arguments.getValue("-xxx"));
}

TEST(Argumenmts, getValueDefaultIsNotReturnDefaultIfValueIsEmpty)
{
    std::vector<std::string> argv = { std::string("-h")};
    Arguments arguments(argv);
    ASSERT_EQ("", arguments.getValue("-h", "default"));
}

TEST(Argumenmts, getValueDefaultReturnedDefaultIfNameNotExist)
{
    std::vector<std::string> argv = { std::string("-h")};
    Arguments arguments(argv);
    ASSERT_EQ("default", arguments.getValue("-x", "default"));
}
