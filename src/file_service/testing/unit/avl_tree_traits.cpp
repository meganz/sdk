#include <gtest/gtest.h>
#include <mega/file_service/avl_tree_traits.h>

#include <functional>

namespace mega
{
namespace file_service
{

struct Node
{
    int mKey;
}; // Node

struct Traits
{
    static constexpr auto mKeyPointer = &Node::mKey;
}; // Traits

TEST(AVLTreeKeyTraits, compare)
{
    using KT = detail::KeyTraits<Traits>;

    Node n0{0};
    Node n1{1};

    EXPECT_EQ(KT::compare(n0.mKey, n0.mKey), 0);
    EXPECT_GT(KT::compare(n1.mKey, n0.mKey), 0);
    EXPECT_LT(KT::compare(n0.mKey, n1.mKey), 0);
}

TEST(AVLTreeKeyTraits, custom_compare)
{
    struct TraitsWithCustomCompare: Traits
    {
        using compare = std::greater<int>;
    }; // TraitsWithCustomCompare

    using KT = detail::KeyTraits<TraitsWithCustomCompare>;

    Node n0{0};
    Node n1{1};

    EXPECT_EQ(KT::compare(n0.mKey, n0.mKey), 0);
    EXPECT_GT(KT::compare(n0.mKey, n1.mKey), 0);
    EXPECT_LT(KT::compare(n1.mKey, n0.mKey), 0);
}

TEST(AVLTreeKeyTraits, key)
{
    using KT = detail::KeyTraits<Traits>;

    Node n0{0};
    Node n1{1};

    EXPECT_EQ(KT::key(n0), 0);
    EXPECT_EQ(KT::key(n1), 1);
}

} // file_service
} // mega
