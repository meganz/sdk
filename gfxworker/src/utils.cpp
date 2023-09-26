#include "gfxworker/utils.h"

#include <cassert>

// TODO
std::string initutils::getHomeFolder()
{
    assert(false);
    return std::string();
}

bool initutils::extractArg(std::vector<const char *>& args, const char* what)
{
    for (auto i = args.size(); i--; )
    {
        if (!strcmp(args[i], what))
        {
            args.erase(args.begin() + (int)i);
            return true;
        }
    }
    return false;
}

bool initutils::extractArgParam(std::vector<const char *>& args, const char* what, std::string& param)
{
    using vsize_t = decltype(args.size());
    const auto argsSize = static_cast<int>(args.size());
    for (int i = argsSize - 1; --i >= 0; )
    {
        const auto index = static_cast<vsize_t>(i);
        if (!strcmp(args[index], what) && argsSize > i)
        {
            param = args[index + 1];
            args.erase(args.begin() + i, args.begin() + i + 2);
            return true;
        }
    }
    return false;
}

std::string initutils::getSanitizedTestFilter(std::vector<const char*>& args)
{
    std::string gtestFilter;
    initutils::extractArgParam(args, "--gtest_filter", gtestFilter);
    // strip surrounding quotes if any
    if (gtestFilter.size() && gtestFilter.back() == '"' && gtestFilter.front() == '"')
    {
        gtestFilter = gtestFilter.substr(1, gtestFilter.length() - 2);
    }
    if (gtestFilter.empty())
    {
        gtestFilter = "*";
    }
    return gtestFilter;
}
