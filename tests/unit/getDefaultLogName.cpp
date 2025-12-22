#include <string>

const std::string& getDefaultLogName()
{
    static const std::string k = "sdk_unit_tests.log";
    return k;
}
