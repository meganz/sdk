#pragma once

namespace mega
{
namespace file_service
{

class ConstructionLogger
{
    const char* mName;

public:
    ConstructionLogger(const char* name);

    ~ConstructionLogger();
}; // ConstructionLogger

} // file_service
} // mega
