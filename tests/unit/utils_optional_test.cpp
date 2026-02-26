#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <mega/utils.h>
#include <mega/utils_optional.h>

using namespace mega;

class HelperClass
{
public:
    HelperClass(const std::string& s):
        str{s} {};

    std::string getStr() const
    {
        return str;
    }

    std::optional<int> toInt() const
    {
        return stringToNumber<int>(str);
    }

private:
    std::string str;
};

TEST(OptMonadicOp, Transform)
{
    const std::optional<std::string> a = "hello";
    const auto getSize = [](const std::string& s) -> int
    {
        return static_cast<int>(s.size());
    };
    // With empty
    auto result = std::optional<std::string>{} | transform(getSize);
    static_assert(std::is_same_v<decltype(result), std::optional<int>>);
    EXPECT_FALSE(result.has_value());

    // With lvalues
    result = a | transform(getSize);
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), 5);

    // With rvalues
    result = std::move(a) | transform(std::move(getSize));
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), 5);

    // With class method
    auto fromClass = std::optional<HelperClass>(std::string{"6"}) | transform(&HelperClass::getStr);
    EXPECT_EQ(fromClass, std::optional<std::string>("6"));
    fromClass = std::optional<HelperClass>() | transform(&HelperClass::getStr);
    EXPECT_EQ(fromClass, std::optional<std::string>());
}

TEST(OptMonadicOp, OrElse)
{
    const std::optional<std::string> a = "hello";
    const auto getEmpty = []
    {
        return std::optional{std::string{"EMPTY"}};
    };
    // With empty
    auto result = std::optional<std::string>{} | or_else(getEmpty);
    static_assert(std::is_same_v<decltype(result), std::remove_const_t<decltype(a)>>);
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), "EMPTY");

    // With lvalues
    result = a | or_else(getEmpty);
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), "hello");

    // With rvalues
    result = std::move(a) | or_else(std::move(getEmpty));
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), "hello");
}

TEST(OptMonadicOp, AndThen)
{
    const std::optional<std::string> a = "5";
    const auto toInt = [](const std::string& s) -> std::optional<int>
    {
        return stringToNumber<int>(s);
    };
    // With empty
    auto result = std::optional<std::string>{} | and_then(toInt);
    static_assert(std::is_same_v<decltype(result), std::optional<int>>);
    EXPECT_FALSE(result.has_value());

    // With lvalues
    result = a | and_then(toInt);
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), 5);

    // With rvalues
    result = std::move(a) | and_then(std::move(toInt));
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), 5);

    // With class method
    auto fromClass = std::optional<HelperClass>(std::string{"6"}) | and_then(&HelperClass::toInt);
    EXPECT_EQ(fromClass, std::optional<int>(6));
    fromClass = std::optional<HelperClass>(std::string{"not"}) | and_then(&HelperClass::toInt);
    EXPECT_EQ(fromClass, std::optional<int>());
}

/**
 * @brief Reproduction of the example at https://en.cppreference.com/w/cpp/utility/optional/and_then
 */
TEST(OptMonadicOp, Combined)
{
    using namespace std::literals;

    const std::vector<std::optional<std::string>>
        v{"1234", "15 foo", "bar", "42", "5000000000", " 5", std::nullopt, "-43"};
    const auto manipulate = [](auto&& o)
    {
        // Disable format to match the one in the example at cppreference
        // clang-format off
        return (o
            // if optional is nullopt convert it to optional with "" string
            | or_else([]{ return std::optional{""s}; })
            // flatmap from strings to ints (making empty optionals where it fails)
            | and_then(stringToNumber<int>)
            // map int to int + 1
            | transform([](int n) { return n + 1; })
            // convert back to strings
            | transform([](int n) { return std::to_string(n); }))
            // replace all empty optionals that were left by
            // and_then and ignored by transforms with "NaN"
            .value_or("NaN"s);
        // clang-format on
    };

    const std::vector<std::string>
        resultExpected{"1235", "16", "NaN", "43", "NaN", "NaN", "NaN", "-42"};
    std::vector<std::string> result;
    result.reserve(resultExpected.size());
    std::transform(begin(v), end(v), std::back_inserter(result), manipulate);

    EXPECT_THAT(result, testing::ElementsAreArray(resultExpected));
}

TEST(OptMonadicOp, ChainingMixRvaluesAndLvalues)
{
    std::optional<std::string> persistent = "250";
    auto fallback = []
    {
        return std::optional<std::string>{"fallback"};
    };

    auto result = persistent |
                  transform(
                      [](const std::string& s)
                      {
                          return s + "0";
                      }) // lvalue operation: "2500"
                  | and_then(
                        [](std::string s) -> std::optional<int>
                        {
                            return stringToNumber<int>(s); // converts "2500" to 2500
                        }) |
                  transform(
                      [](int n) -> int
                      {
                          return n / 10;
                      }); // should yield 250

    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), 250);

    // Also test with a temporary (rvalue) empty optional to trigger the fallback.
    auto resultEmpty = std::optional<std::string>{} |
                       transform(
                           [](const std::string& s)
                           {
                               return s + "0";
                           }) |
                       and_then(
                           [](std::string s) -> std::optional<int>
                           {
                               return stringToNumber<int>(s);
                           }) |
                       transform(
                           [](const int i)
                           {
                               return numberToString(i);
                           }) |
                       or_else(fallback) |
                       transform(
                           [](const std::string& s) -> std::string
                           {
                               return s + "!";
                           });
    EXPECT_TRUE(resultEmpty.has_value());
    EXPECT_EQ(resultEmpty.value(), "fallback!");
}

/**
 * @class MoveTracker
 * @brief Helper struct to validate move semantics
 */
struct MoveTracker
{
    int value;
    static int copy_count;
    static int move_count;

    MoveTracker(int v):
        value(v)
    {}

    MoveTracker(const MoveTracker& other):
        value(other.value)
    {
        ++copy_count;
    }

    MoveTracker(MoveTracker&& other) noexcept:
        value(other.value)
    {
        ++move_count;
    }

    MoveTracker& operator=(const MoveTracker& other)
    {
        value = other.value;
        ++copy_count;
        return *this;
    }

    MoveTracker& operator=(MoveTracker&& other) noexcept
    {
        value = other.value;
        ++move_count;
        return *this;
    }
};

int MoveTracker::copy_count = 0;
int MoveTracker::move_count = 0;

TEST(OptMonadicOp, MoveSemantics)
{
    // Reset counters to ensure a clean test.
    MoveTracker::copy_count = 0;
    MoveTracker::move_count = 0;

    std::optional<MoveTracker> opt{MoveTracker(100)};
    auto result = std::move(opt) |
                  and_then(
                      [](MoveTracker&& mt) -> std::optional<MoveTracker>
                      {
                          mt.value += 50;
                          return std::optional<MoveTracker>{std::move(mt)};
                      }) |
                  transform(
                      [](const MoveTracker& mt) -> int
                      {
                          return mt.value;
                      });
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 150);
    // Validate that no copies occurred.
    EXPECT_EQ(MoveTracker::copy_count, 0);
    EXPECT_EQ(MoveTracker::move_count, 2);
}

TEST(OptMonadicOp, FallbackAfterConversionFailure)
{
    const std::optional<std::string> invalid = "invalid_number";
    auto fallback = []
    {
        return std::optional<int>{-999};
    };
    auto result = invalid |
                  and_then(
                      [](const std::string& s) -> std::optional<int>
                      {
                          return stringToNumber<int>(s);
                      }) |
                  or_else(fallback) |
                  transform(
                      [](int n) -> std::string
                      {
                          return std::to_string(n);
                      });
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), "-999");
}

TEST(OptMonadicOp, NonCopyableMoveOnly)
{
    std::optional<std::unique_ptr<int>> opt = std::make_unique<int>(42);
    auto result = std::move(opt) |
                  and_then(
                      [](std::unique_ptr<int>&& ptr) -> std::optional<std::unique_ptr<int>>
                      {
                          if (ptr)
                              *ptr += 8;
                          return std::optional<std::unique_ptr<int>>{std::move(ptr)};
                      }) |
                  transform(
                      [](const std::unique_ptr<int>& ptr) -> int
                      {
                          return *ptr;
                      });
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 50);
}
