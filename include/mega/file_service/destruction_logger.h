#pragma once

namespace mega
{
namespace file_service
{

class DestructionLogger
{
    const char* mName;

public:
    DestructionLogger(const char* name);

    ~DestructionLogger();
}; // DestructionLogger

} // file_service
} // mega
