/**
 * (c) 2019 by Mega Limited, Wellsford, New Zealand
 *
 * This file is part of the MEGA SDK - Client Access Engine.
 *
 * Applications using the MEGA API must present a valid application key
 * and comply with the the rules set forth in the Terms of Service.
 *
 * The MEGA SDK is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * @copyright Simplified (2-clause) BSD License.
 *
 * You should have received a copy of the license along with this
 * program.
 */

#include <array>
#include <tuple>

#include <gtest/gtest.h>

#include <mega/utils.h>

TEST(utils, hashCombine_integer)
{
    size_t hash = 0;
    mega::hashCombine(hash, 42);
    ASSERT_EQ(sizeof(hash) == 4 ? 286246808u : 2654435811u, hash);
}

TEST(utils, hashCombine_double)
{
    size_t hash = 2654435811;
    mega::hashCombine(hash, 42.);
    ASSERT_EQ(sizeof(hash) == 4 ? 1814078634u : 3535062602150868519u, hash);
}

TEST(utils, hashCombine_string)
{
    size_t hash = 3535062434184345740;
    mega::hashCombine(hash, std::string{"42"});
    ASSERT_EQ(sizeof(hash) == 4 ? 2424531155u : 14653466847519894273u, hash);
}

TEST(utils, forEach_tuple_const)
{
    const auto tup = std::make_tuple(42, 13);
    int sum = 0;
    mega::forEach(tup, [&sum](const int val)
                       {
                           sum += val;
                       });
    ASSERT_EQ(55, sum);
}

TEST(utils, forEach_tuple_empty_const)
{
    std::tuple<> tup;
    int sum = 0;
    mega::forEach(tup, [&sum](const int val)
                       {
                           sum += val;
                       });
    ASSERT_EQ(0, sum);
}

TEST(utils, forEach_tuple_mutate)
{
    auto tup = std::make_tuple(42, 13);
    mega::forEach(tup, [](int& val)
                       {
                           val *= 2;
                       });
    ASSERT_EQ(84, std::get<0>(tup));
    ASSERT_EQ(26, std::get<1>(tup));
}

TEST(utils, forEach_tuple_temporary)
{
    int sum = 0;
    mega::forEach(std::make_tuple(42, 13), [&sum](const int val)
                                           {
                                               sum += val;
                                           });
    ASSERT_EQ(55, sum);
}

TEST(utils, forEach_tuple_empty_temporary)
{
    int sum = 0;
    mega::forEach(std::tuple<>{}, [&sum](const int val)
                                  {
                                      sum += val;
                                  });
    ASSERT_EQ(0, sum);
}

namespace {

struct FunctorNotMutating
{
    int sum = 0;
    template<typename T>
    void operator()(const T value)
    {
        sum += static_cast<int>(value);
    }
};

}

TEST(utils, forEach_tuple_differentTypes_const)
{
    const auto tup = std::make_tuple(42, 13.);

    FunctorNotMutating functor;
    mega::forEach(tup, functor);
    ASSERT_EQ(55, functor.sum);
}

namespace {

struct FunctorMutating
{
    template<typename T>
    void operator()(T& value)
    {
        value *= 2;
    }
};

}

TEST(utils, forEach_tuple_differentTypes_mutate)
{
    auto tup = std::make_tuple(42, 13.);

    FunctorMutating functor;
    mega::forEach(tup, functor);
    ASSERT_EQ(84, std::get<0>(tup));
    ASSERT_EQ(26., std::get<1>(tup));
}
