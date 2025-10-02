#include <mega/file_service/type_traits.h>

#include <utility>

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

struct Object
{
    const int mConstMember{};
    int mMember{};
    static constexpr int sConstMember{};
}; // Object

using ConstMember = MemberPointerTraits<decltype(&Object::mConstMember)>;

static_assert(ConstMember::value);
static_assert(std::is_same_v<ConstMember::ClassType, Object>);
static_assert(std::is_same_v<ConstMember::MemberType, const int>);

using Member = MemberPointerTraits<decltype(&Object::mMember)>;

static_assert(Member::value);
static_assert(std::is_same_v<Member::ClassType, Object>);
static_assert(std::is_same_v<Member::MemberType, int>);

template<typename T>
using DetectClassType = typename T::ClassType;

template<typename T>
using DetectMemberType = typename T::MemberType;

using StaticConstMember = MemberPointerTraits<decltype(&Object::sConstMember)>;

static_assert(!StaticConstMember::value);
static_assert(!DetectedV<DetectClassType, StaticConstMember>);
static_assert(!DetectedV<DetectMemberType, StaticConstMember>);

static_assert(std::is_same_v<RemoveCVRefT<const int&>, int>);
static_assert(std::is_same_v<RemoveCVRefT<int&>, int>);
static_assert(std::is_same_v<RemoveCVRefT<int>, int>);

static_assert(IsEqualityComparableV<int>);
static_assert(IsEqualityComparableV<std::pair<int, int>>);
static_assert(!IsEqualityComparableV<Unrelated>);
static_assert(!IsEqualityComparableV<std::pair<int, Unrelated>>);

template<typename Provided, typename Expected>
constexpr auto SelectFirstResultIsV =
    std::is_same_v<std::invoke_result_t<SelectFirst, Provided>, Expected>;

// Convenience.
using IntPair = std::pair<int, int>;

static_assert(SelectFirstResultIsV<const IntPair&, const int&>);
static_assert(SelectFirstResultIsV<IntPair&, int&>);
static_assert(SelectFirstResultIsV<IntPair&&, int&&>);
static_assert(SelectFirstResultIsV<IntPair, int&&>);

} // file_service
} // mega
