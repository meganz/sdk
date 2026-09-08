#include <gtest/gtest.h>
#include <mega/common/move_only_function.h>

#include <functional>
#include <future>
#include <memory>

namespace mega
{
namespace common
{
namespace testing
{

TEST(MoveOnlyFunctionTests, assign_from_move_only)
{
    MoveOnlyFunction<void()> function;

    function = [promise = std::promise<void>()]() {};
}

TEST(MoveOnlyFunctionTests, assign_from_nullptr)
{
    MoveOnlyFunction<void()> function = []() {};

    ASSERT_TRUE(function);
    ASSERT_NE(function, nullptr);

    function = nullptr;

    ASSERT_FALSE(function);
    ASSERT_EQ(function, nullptr);
}

TEST(MoveOnlyFunctionTests, call_by_const_lvalue_reference)
{
    MoveOnlyFunction<int(const int&)> function(
        [](const int& value)
        {
            return value;
        });

    ASSERT_EQ(function(2), 2);
}

TEST(MoveOnlyFunctionTests, call_by_lvalue_reference)
{
    MoveOnlyFunction<int&(int&)> function(
        [](int& value) -> int&
        {
            return value;
        });
    int value = 2;

    ASSERT_EQ(&function(value), &value);
}

TEST(MoveOnlyFunctionTests, call_by_rvalue_reference)
{
    using IntPtr = std::unique_ptr<int>;

    MoveOnlyFunction<IntPtr(IntPtr&&)> function(
        [](IntPtr&& value)
        {
            return std::move(value);
        });

    auto value = function(std::make_unique<int>(2));

    ASSERT_TRUE(value);
    ASSERT_EQ(*value, 2);
}

TEST(MoveOnlyFunctionTests, call_by_value)
{
    MoveOnlyFunction<int(int)> function(
        [](int value)
        {
            return value;
        });
    ASSERT_EQ(function(2), 2);
}

TEST(MoveOnlyFunctionTests, construct)
{
    MoveOnlyFunction<void()> function;

    ASSERT_FALSE(function);
    ASSERT_EQ(function, nullptr);
}

TEST(MoveOnlyFunctionTests, construct_from_move_only)
{
    MoveOnlyFunction<void()> function = [promise = std::promise<void>()]() {};
}

TEST(MoveOnlyFunctionTests, move_assign)
{
    MoveOnlyFunction<void()> function0 = [promise = std::promise<void>()]() {};
    MoveOnlyFunction<void()> function1 = std::move(function0);

    ASSERT_FALSE(function0);
    ASSERT_EQ(function0, nullptr);

    ASSERT_TRUE(function1);
    ASSERT_NE(function1, nullptr);
}

} // testing
} // common
} // mega
