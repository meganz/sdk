#include "impl/share.h"
#include "mega/types.h"

#include <gtest/gtest.h>

#include <vector>

using namespace ::mega::impl;
using ::mega::Share;

class ShareSorterTest: public ::testing::Test
{
protected:
    void SetUp() override;

    std::vector<Share> mShares;
    std::vector<ShareData> mShareDatas;
};

void ShareSorterTest::SetUp()
{
    Test::SetUp();
    // Share with a smaller timestamp is created earlier
    mShares = {
        Share{nullptr, mega::RDONLY, 20},
        Share{nullptr, mega::RDONLY, 10},
        Share{nullptr, mega::RDONLY, 30},
    };
    mShareDatas = {
        {1, &mShares[0], true},
        {2, &mShares[1], true},
        {3, &mShares[2], true},
    };
}

TEST_F(ShareSorterTest, SortByShareCreationTimeAscendingly)
{
    ShareSorter::sort(mShareDatas, ::mega::MegaApi::ORDER_SHARE_CREATION_ASC);
    ASSERT_EQ(mShareDatas[0].getNodeHandle(), 2);
    ASSERT_EQ(mShareDatas[1].getNodeHandle(), 1);
    ASSERT_EQ(mShareDatas[2].getNodeHandle(), 3);
}

TEST_F(ShareSorterTest, SortByShareCreationTimeDescendingly)
{
    ShareSorter::sort(mShareDatas, ::mega::MegaApi::ORDER_SHARE_CREATION_DESC);
    ASSERT_EQ(mShareDatas[0].getNodeHandle(), 3);
    ASSERT_EQ(mShareDatas[1].getNodeHandle(), 1);
    ASSERT_EQ(mShareDatas[2].getNodeHandle(), 2);
}

TEST_F(ShareSorterTest, SortByOthersDoesNotChangeOrder)
{
    ShareSorter::sort(mShareDatas, ::mega::MegaApi::ORDER_NONE);
    ASSERT_EQ(mShareDatas[0].getNodeHandle(), 1);
    ASSERT_EQ(mShareDatas[1].getNodeHandle(), 2);
    ASSERT_EQ(mShareDatas[2].getNodeHandle(), 3);

    ShareSorter::sort(mShareDatas, ::mega::MegaApi::ORDER_CREATION_DESC);
    ASSERT_EQ(mShareDatas[0].getNodeHandle(), 1);
    ASSERT_EQ(mShareDatas[1].getNodeHandle(), 2);
    ASSERT_EQ(mShareDatas[2].getNodeHandle(), 3);
}
