#pragma once

namespace mega
{
namespace common
{

template<typename T>
class Badge
{
    friend T;

    Badge() = default;

public:
    Badge(const Badge& other) = default;

    ~Badge() = default;
}; // Badge<T>

} // common
} // mega

