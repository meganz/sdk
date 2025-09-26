#include <gtest/gtest.h>

#include <span>
#include <string>
#include <vector>

template<typename T>
concept Addable = requires(T a, T b)
{
    a + b;
};

static_assert(Addable<int>);
static_assert(Addable<std::string>);

// Test with a type that doesn't have operator+
struct NonAddable
{
    int value;
};

static_assert(!Addable<NonAddable>);

TEST(Cxx20Features, ConceptsWorks)
{
    // Test that Addable concept works with numeric types
    auto add = [](Addable auto a, Addable auto b)
    {
        return a + b;
    };

    EXPECT_EQ(add(5, 3), 8);
    EXPECT_EQ(add(2.5, 1.5), 4.0);
    EXPECT_EQ(add(10u, 20u), 30u);
}

TEST(Cxx20Features, SpanWorks)
{
    std::vector<int> v{1, 2, 3};
    std::span<int> s{v};
    EXPECT_EQ(s.size(), v.size());
}
