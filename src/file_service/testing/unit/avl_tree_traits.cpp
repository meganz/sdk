#include <gtest/gtest.h>
#include <mega/file_service/avl_tree_traits.h>
#include <mega/file_service/testing/unit/avl_node.h>

#include <functional>

namespace mega
{
namespace file_service
{

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
        using Compare = std::greater<int>;
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

TEST(AVLTreeLinkTraits, child)
{
    using LT = detail::LinkTraits<Traits>;

    Node n0{0};
    Node n1{1};
    Node n2{2};

    n1.mLink.mChildren[0] = &n0;
    n1.mLink.mChildren[1] = &n2;

    EXPECT_EQ(LT::child(n1, 0), &n0);
    EXPECT_EQ(LT::child(n1, 1), &n2);

    LT::child(n1, 0) = nullptr;

    EXPECT_EQ(n1.mLink.mChildren[0], nullptr);
}

TEST(AVLTreeLinkTraits, height)
{
    using LT = detail::LinkTraits<Traits>;

    Node n0{0};

    n0.mLink.mHeight = 1;

    EXPECT_EQ(LT::height(n0), 1);

    LT::height(n0) = 0;

    EXPECT_EQ(n0.mLink.mHeight, 0);
}

TEST(AVLTreeLinkTraits, left)
{
    using LT = detail::LinkTraits<Traits>;

    Node n0{0};
    Node n1{1};

    n1.mLink.mChildren[0] = &n0;

    EXPECT_EQ(LT::left(n1), &n0);

    LT::left(n1) = nullptr;

    EXPECT_EQ(n1.mLink.mChildren[0], nullptr);
}

TEST(AVLTreeLinkTraits, link)
{
    using LT = detail::LinkTraits<Traits>;

    Node n{0};

    EXPECT_EQ(&LT::link(n), &n.mLink);
}

TEST(AVLTreeLinkTraits, parent)
{
    using LT = detail::LinkTraits<Traits>;

    Node n0{0};
    Node n1{1};

    n1.mLink.mParent = &n0;

    EXPECT_EQ(LT::parent(n1), &n0);

    LT::parent(n1) = nullptr;

    EXPECT_EQ(n1.mLink.mParent, nullptr);
}

TEST(AVLTreeLinkTraits, right)
{
    using LT = detail::LinkTraits<Traits>;

    Node n0{0};
    Node n1{1};

    n0.mLink.mChildren[1] = &n1;

    EXPECT_EQ(LT::right(n0), &n1);

    LT::right(n0) = nullptr;

    EXPECT_EQ(n0.mLink.mChildren[1], nullptr);
}

TEST(AVLTreeMetadataTraits, update)
{
    using LT = detail::LinkTraits<Traits>;

    // No metadata.
    {
        using MT = detail::MetadataTraits<Traits>;

        Node n0{0};

        MT::update<LT>(n0);
    }

    // With metadata.
    using MT = detail::MetadataTraits<TraitsWithMetadata>;

    Node n0{0};
    Node n1{1};
    Node n2{2};

    LT::left(n1) = &n0;
    LT::right(n1) = &n2;

    MT::update<LT>(n0);
    EXPECT_EQ(n0.mSize, 1);

    MT::update<LT>(n2);
    EXPECT_EQ(n2.mSize, 1);

    MT::update<LT>(n1);
    EXPECT_EQ(n1.mSize, 3);
}

} // file_service
} // mega
