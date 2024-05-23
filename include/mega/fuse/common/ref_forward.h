#pragma once

namespace mega
{
namespace fuse
{

// Specify that a Ref should "steal" an existing reference.
struct AdoptRefTag { };

// Forward declaration for all Ref types.
template<typename T>
class Ref;

// Ensures that only Ref<T> instances can call certain methods.
class RefBadge
{
    template<typename T>
    friend class Ref;

    RefBadge() = default;

public:
    RefBadge(const RefBadge& other) = default;

    ~RefBadge() = default;
}; // RefBadge

// For convenience.
constexpr AdoptRefTag AdoptRef;

} // fuse
} // mega

