#pragma once

#include <string>

namespace mega_test
{

//
// It provides setting up the executable's directory and then getting later globally
//
class ExecutableDir
{
public:
    static void init(const std::string& executable);

    static std::string get();

private:
    static std::string mDir;
};

}