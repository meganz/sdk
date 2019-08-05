#pragma once

#include <stdexcept>

namespace mt {

class NotImplemented : public std::runtime_error
{
public:
    NotImplemented(const std::string& function)
    : std::runtime_error{"Function not implemented: " + function}
    {}
};

} // mt
