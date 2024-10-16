#include "mega/arguments.h"

namespace mega {

std::string Arguments::getValue(const std::string& name, const std::string& defaultValue) const
{
    auto it = mValues.find(name);
    return it == mValues.end() ? defaultValue : it->second;
}

bool Arguments::empty() const
{
    return mValues.empty();
}

Arguments::size_type Arguments::size() const
{
    return mValues.size();
}

bool Arguments::contains(const std::string& name) const
{
    return mValues.count(name) > 0;
}

std::ostream& operator<<(std::ostream& os, const Arguments& arguments)
{
    for (auto& argument : arguments.mValues)
    {
        os << "  " << argument.first << "=" << argument.second << std::endl;
    }
    return os;
}

Arguments ArgumentsParser::parse(int argc, char* argv[])
{
    std::vector<std::string> argVec;
    std::copy(argv + 1, argv + argc, std::back_inserter(argVec));

    Arguments arguments;
    for (const auto& arg : argVec)
    {
        // A argument wouldn't be emplaced (thus dropped) if it is duplicated with a previous one
        arguments.mValues.emplace(parseOneArgument(arg));
    }
    return arguments;

}

std::pair<std::string, std::string> ArgumentsParser::parseOneArgument(const std::string& argument)
{
    const auto pos = argument.find('=');
    return pos == argument.npos ?
                  std::make_pair(argument, "") :
                  std::make_pair(argument.substr(0, pos), argument.substr(pos + 1));
}

}
