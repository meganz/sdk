#pragma once

namespace mega
{
namespace fuse
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

} // fuse
} // mega

