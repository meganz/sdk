#include <mega/file_service/type_traits.h>

namespace mega
{
namespace file_service
{

static_assert(IsNoneSuchV<NoneSuch>);
static_assert(!IsNoneSuchV<int>);

static_assert(IsNotNoneSuchV<int>);
static_assert(!IsNotNoneSuchV<NoneSuch>);

struct DetectedTest0
{
    using type = void;
}; // DetectedTest0

struct DetectedTest1
{}; // DetectedTest1

template<typename Type>
using DetectType = typename Type::type;

static_assert(DetectedV<DetectType, DetectedTest0>);
static_assert(std::is_same_v<DetectedT<DetectType, DetectedTest0>, void>);

static_assert(!DetectedV<DetectType, DetectedTest1>);
static_assert(std::is_same_v<DetectedT<DetectType, DetectedTest1>, NoneSuch>);

static_assert(DetectedOrV<int, DetectType, DetectedTest0>);
static_assert(std::is_same_v<DetectedOrT<int, DetectType, DetectedTest0>, void>);

static_assert(!DetectedOrV<int, DetectType, DetectedTest1>);
static_assert(std::is_same_v<DetectedOrT<int, DetectType, DetectedTest1>, int>);

struct Base
{}; // Base

struct DerivedA: Base
{}; // DerivedA

struct DerivedB: DerivedA
{}; // DerivedB

struct Unrelated
{}; // Unrelated

static_assert(std::is_same_v<MostSpecificClassT<Base, DerivedA>, DerivedA>);
static_assert(std::is_same_v<MostSpecificClassT<Base, DerivedA, DerivedB>, DerivedB>);

static_assert(std::is_same_v<MostSpecificClassT<Base, Unrelated>, NoneSuch>);
static_assert(std::is_same_v<MostSpecificClassT<Base, DerivedA, Unrelated>, NoneSuch>);

} // file_service
} // mega
