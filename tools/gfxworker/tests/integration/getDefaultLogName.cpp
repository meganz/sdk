#include <string>

const std::string& getDefaultLogName()
{
    static const std::string k = "gfxworker_test_integration.log";
    return k;
}
