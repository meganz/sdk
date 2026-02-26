#pragma once

#include <vector>
#include <string>
#include <unordered_map>
#include <ostream>

namespace mega {

// A simple arguments parser that only supports name=value or name
// See unit test for the usage
class Arguments
{
public:
    using size_type = std::unordered_map<std::string, std::string>::size_type;

    bool contains(const std::string& name) const;

    std::string getValue(const std::string& name, const std::string& defaultValue = "") const;

    bool empty() const;

    size_type size() const;

private:
    friend class ArgumentsParser;

    friend std::ostream& operator<<(std::ostream& os, const Arguments& arguments);

    std::unordered_map<std::string, std::string> mValues;
};

std::ostream& operator<<(std::ostream& os, const Arguments& arguments);


class ArgumentsParser
{
public:
    static Arguments parse(int argc, char* argv[]);

private:
    static std::pair<std::string, std::string> parseOneArgument(const std::string& argument);
};


}