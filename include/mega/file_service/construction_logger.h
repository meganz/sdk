#pragma once

#include <string>

namespace mega
{
namespace file_service
{

class ConstructionLogger
{
    std::string mName;

public:
    ConstructionLogger(const std::string& name);

    ~ConstructionLogger();
}; // ConstructionLogger

} // file_service
} // mega
