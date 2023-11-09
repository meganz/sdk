#include "mega/arguments.h"

namespace mega {

std::string Arguments::getValue(const std::string& name, const std::string& defaultValue) const
{
    auto it = mValues.find(name);
    return it == mValues.end() ? defaultValue : it->second;
}

std::unordered_map<std::string, std::string> Arguments::parse(const std::vector<std::string>& arguments)
{
    std::unordered_map<std::string, std::string> result;
    for (const auto& argument : arguments)
    {
        result.emplace(Arguments::parseOneArgument(argument));
    }
    return result;
}

std::pair<std::string, std::string> Arguments::parseOneArgument(const std::string& argument)
{
    const auto pos = argument.find('=');
    return pos == argument.npos ?
                  std::make_pair(argument, "") :
                  std::make_pair(argument.substr(0, pos), argument.substr(pos + 1));
}

bool Arguments::empty() const
{
    return mValues.empty();
}

Arguments::size_type Arguments::size() const
{
    return mValues.size();
}

std::ostream& operator<<(std::ostream& os, const Arguments& arguments)
{
    for (auto& argument : arguments.mValues)
    {
        os << "  " << argument.first << "=" << argument.second << std::endl;
    }
    return os;
}

}
