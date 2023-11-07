#pragma once

#include <vector>
#include <string>
#include <unordered_map>

namespace mega {
namespace gfx {

// A simple arguments parser
class Arguments
{
public:
    using size_type = std::unordered_map<std::string, std::string>::size_type;

    explicit Arguments(std::vector<std::string> arguments) : mValues(parse(arguments)) {};

    bool contains(const std::string& name) const { return mValues.count(name) > 0;}

    std::string getValue(const std::string& name, const std::string& defaultValue = "") const;

    bool empty() const;

    size_type size() const;
private:
    static std::unordered_map<std::string, std::string> parse(const std::vector<std::string>& arguments);

    static std::pair<std::string, std::string> parseOneArgument(const std::string& argument);

    std::unordered_map<std::string, std::string> mValues;
};

}
}