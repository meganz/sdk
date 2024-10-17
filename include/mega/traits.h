#pragma once

namespace mega
{

template<typename T>
struct ResizeTraits
{
    static void resize(T& instance, std::size_t newSize)
    {
        instance.resize(newSize);
    }
}; // ResizeTraits<T>

template<typename T>
struct SizeTraits
{
    static std::size_t size(const T& instance)
    {
        return instance.size();
    }
}; // SizeTraits<T>

} // mega
