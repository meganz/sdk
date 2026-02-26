/**
 * @file canceller_test.cpp
 * @brief This file is expected to contain unit tests involving ScopedCanceller logic.
 */

#include <gtest/gtest.h>
#include <mega/canceller.h>

using ::mega::cancel_epoch_bump;
using ::mega::ScopedCanceller;

TEST(Canceller, SnapshotNotTriggeredUntilBumped)
{
    ScopedCanceller s1;
    EXPECT_FALSE(s1.triggered());
    // No bump -> still false
    EXPECT_FALSE(s1.triggered())
        << "The same ScopedCanceller should remain untriggered without a previous cancel";
}

TEST(Canceller, TriggeredAfterBump)
{
    ScopedCanceller s1;
    cancel_epoch_bump();
    EXPECT_TRUE(s1.triggered());

    ScopedCanceller s2;
    EXPECT_FALSE(s2.triggered())
        << "A new snapshot should see the new epoch and not be triggered yet";
}

TEST(Canceller, MultipleBumpsStillTriggerOldSnapshots)
{
    ScopedCanceller s1;
    cancel_epoch_bump();
    cancel_epoch_bump();
    cancel_epoch_bump();
    EXPECT_TRUE(s1.triggered());
}