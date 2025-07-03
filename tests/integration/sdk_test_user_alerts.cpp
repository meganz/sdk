/**
 * @file sdk_test_user_alerts.cpp
 * @brief Tests that involve interactions with User alerts.
 */

#include "SdkTest_test.h"

#include <gmock/gmock.h>

namespace
{

/**
 * @brief SdkTest.UserAlertPaymentVsReminder
 */
TEST_F(SdkTest, UserAlertPaymentVsReminder)
{
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    // clear user alerts and notifications
    UserAlerts& userAlerts = megaApi[0]->getClient()->useralerts;
    userAlerts.useralertnotify.clear();
    userAlerts.clear();

    // add a Payment Reminder
    auto referenceTime = time(nullptr);
    userAlerts.add(new UserAlert::PaymentReminder(referenceTime, userAlerts.nextId()));
    unique_ptr<MegaUserAlertList> megaAlerts{megaApi[0]->getUserAlerts()};
    ASSERT_EQ(1, megaAlerts->size());
    ASSERT_EQ(MegaUserAlert::TYPE_PAYMENTREMINDER, megaAlerts->get(0)->getType());

    // add a failed Payment done after the reminder - reminder should be kept
    userAlerts.add(
        new UserAlert::Payment(false, 1, referenceTime + 1, userAlerts.nextId(), name_id::psts_v2));
    megaAlerts.reset(megaApi[0]->getUserAlerts());
    ASSERT_EQ(2, megaAlerts->size());
    ASSERT_EQ(MegaUserAlert::TYPE_PAYMENTREMINDER, megaAlerts->get(0)->getType());
    ASSERT_EQ(MegaUserAlert::TYPE_PAYMENT_FAILED, megaAlerts->get(1)->getType());

    // add a successful Payment done after the reminder - reminder should be removed
    userAlerts.add(
        new UserAlert::Payment(true, 1, referenceTime + 2, userAlerts.nextId(), name_id::psts_v2));
    megaAlerts.reset(megaApi[0]->getUserAlerts());
    ASSERT_EQ(2, megaAlerts->size());
    ASSERT_EQ(MegaUserAlert::TYPE_PAYMENT_FAILED, megaAlerts->get(0)->getType());
    ASSERT_EQ(MegaUserAlert::TYPE_PAYMENT_SUCCEEDED, megaAlerts->get(1)->getType());

    // add a Payment Reminder for an expiry before the previous payment - it should be removed
    userAlerts.add(new UserAlert::PaymentReminder(referenceTime, userAlerts.nextId()));
    megaAlerts.reset(megaApi[0]->getUserAlerts());
    ASSERT_EQ(2, megaAlerts->size());
    ASSERT_EQ(MegaUserAlert::TYPE_PAYMENT_FAILED, megaAlerts->get(0)->getType());
    ASSERT_EQ(MegaUserAlert::TYPE_PAYMENT_SUCCEEDED, megaAlerts->get(1)->getType());

    // add a Payment Reminder for an expiry after the previous payment - it should be kept
    userAlerts.add(new UserAlert::PaymentReminder(referenceTime + 10, userAlerts.nextId()));
    megaAlerts.reset(megaApi[0]->getUserAlerts());
    ASSERT_EQ(3, megaAlerts->size());
    ASSERT_EQ(MegaUserAlert::TYPE_PAYMENT_FAILED, megaAlerts->get(0)->getType());
    ASSERT_EQ(MegaUserAlert::TYPE_PAYMENT_SUCCEEDED, megaAlerts->get(1)->getType());
    ASSERT_EQ(MegaUserAlert::TYPE_PAYMENTREMINDER, megaAlerts->get(2)->getType());

    // add a successful Payment done before the reminder - reminder should be kept
    userAlerts.add(
        new UserAlert::Payment(true, 1, referenceTime + 3, userAlerts.nextId(), name_id::psts_v2));
    megaAlerts.reset(megaApi[0]->getUserAlerts());
    ASSERT_EQ(4, megaAlerts->size());
    ASSERT_EQ(MegaUserAlert::TYPE_PAYMENT_FAILED, megaAlerts->get(0)->getType());
    ASSERT_EQ(MegaUserAlert::TYPE_PAYMENT_SUCCEEDED, megaAlerts->get(1)->getType());
    ASSERT_EQ(MegaUserAlert::TYPE_PAYMENTREMINDER, megaAlerts->get(2)->getType());
    ASSERT_EQ(MegaUserAlert::TYPE_PAYMENT_SUCCEEDED, megaAlerts->get(3)->getType());
}

}
