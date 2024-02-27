#include "gtest/gtest.h"
#include <array>
#include <iterator>
#include <vector>
#include <algorithm>
#include <initializer_list>
#include "mega/arguments.h"

using mega::ArgumentsParser;

//
// Provides argc and argv like parameters in int main(int argc, char** argv)
//
class Argv
{
public:
    Argv(std::initializer_list<std::string> init);

    char** argv();

    int argc();
private:
    std::vector<std::string> mInit;
    std::vector<char*> mArgv;
};

Argv::Argv(std::initializer_list<std::string> init)
    : mInit(init)
    , mArgv(init.size() + 1 , nullptr) // extra 1 for nullptr ending
{
    std::transform(std::begin(mInit),
                   std::end(mInit),
                   std::begin(mArgv),
                   [](const std::string& elem) {
                        return const_cast<char*>(elem.data());
                   });
}

char** Argv::argv()
{
    return mArgv.data();
}

int Argv::argc()
{
    return static_cast<int>(mInit.size());
}

TEST(Argumenmts, ParseNoArgumentsSuccessfully)
{
    Argv argv = { "executable.exe" };
    auto arguments = ArgumentsParser::parse(argv.argc(), argv.argv());
    ASSERT_TRUE(arguments.empty());
}

TEST(Argumenmts, ParseOneNoValueArgumentSuccessfully)
{
    Argv argv = { "executable.exe",
                  "-h",
                };
    auto arguments = ArgumentsParser::parse(argv.argc(), argv.argv());
    ASSERT_TRUE(!arguments.empty());
    ASSERT_TRUE(arguments.contains("-h"));
    ASSERT_EQ("", arguments.getValue("-h"));
}

TEST(Argumenmts, ParseOneHasValueArgumentSuccessfully)
{
    Argv argv = { "executable.exe",
                  "-t=10",
                };
    auto arguments = ArgumentsParser::parse(argv.argc(), argv.argv());
    ASSERT_FALSE(arguments.empty());
    ASSERT_TRUE(arguments.contains("-t"));
    ASSERT_EQ("10", arguments.getValue("-t"));
}

TEST(Argumenmts, ParseOneListOfArgumentsSuccessfully)
{
    Argv argv = { "executable.exe",
                  "-h",
                  "-t=10",
                  "-n=the name",
                };
    auto arguments = ArgumentsParser::parse(argv.argc(), argv.argv());
    ASSERT_FALSE(arguments.empty());
    ASSERT_EQ(3, static_cast<int>(arguments.size()));
    ASSERT_EQ("", arguments.getValue("-h"));
    ASSERT_EQ("10", arguments.getValue("-t"));
    ASSERT_EQ("the name", arguments.getValue("-n"));
    ASSERT_FALSE(arguments.contains("-xxx"));
    ASSERT_EQ("", arguments.getValue("-xxx"));
}

TEST(Argumenmts, getValueDefaultIsNotReturnDefaultIfValueIsEmpty)
{
    Argv argv = { "executable.exe",
                  "-h",
                };
    auto arguments = ArgumentsParser::parse(argv.argc(), argv.argv());
    ASSERT_EQ("", arguments.getValue("-h", "default"));
}

TEST(Argumenmts, getValueDefaultReturnedDefaultIfNameNotExist)
{
    Argv argv = { "executable.exe",
                  "-h",
                };
    auto arguments = ArgumentsParser::parse(argv.argc(), argv.argv());
    ASSERT_EQ("default", arguments.getValue("-x", "default"));
}
