#pragma once

#include <string>

namespace mega
{
namespace file_service
{

class DestructionLogger
{
    std::string mName;

public:
    DestructionLogger(const std::string& name);

    ~DestructionLogger();
}; // DestructionLogger

} // file_service
} // mega
