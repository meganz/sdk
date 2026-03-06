/**
 * @file sdk_test_user_alerts.cpp
 * @brief Tests that involve interactions with User alerts.
 */

#include "integration_test_utils.h"
#include "sdk_test_utils.h"
#include "SdkTest_test.h"

namespace
{

static bool ComapreResults(const std::vector<int>& alertTypes, unique_ptr<MegaUserAlertList>&& list)
{
    if (alertTypes.size() != static_cast<size_t>(list->size()))
    {
        return false;
    }
    for (size_t idx = 0; idx < alertTypes.size(); ++idx)
    {
        if (alertTypes[idx] != list->get(static_cast<int>(idx))->getType())
        {
            return false;
        }
    }
    return true;
}

/**
 * @brief SdkTest.UserAlertPaymentVsReminder
 */
TEST_F(SdkTest, UserAlertPaymentVsReminder)
{
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    ASSERT_TRUE(WaitFor(
        [this]()
        {
            return megaApi[0]->isLoggedIn() && megaApi[0]->getClient()->useralerts.catchupdone;
        },
        3000));

    // there should be no existing alerts of test account
    UserAlerts& userAlerts = megaApi[0]->getClient()->useralerts;

    // add a Payment Reminder
    auto referenceTime = time(nullptr);
    userAlerts.add(
        new UserAlert::PaymentReminder(referenceTime, referenceTime + 100, userAlerts.nextId()));
    unique_ptr<MegaUserAlertList> megaAlerts{megaApi[0]->getUserAlerts()};
    ASSERT_TRUE(ComapreResults(std::vector<int>{MegaUserAlert::TYPE_PAYMENTREMINDER},
                               std::move(megaAlerts)));

    // add a failed Payment done after the reminder - reminder should be kept
    userAlerts.add(
        new UserAlert::Payment(false, 1, referenceTime, userAlerts.nextId(), name_id::psts_v2));
    megaAlerts.reset(megaApi[0]->getUserAlerts());
    ASSERT_TRUE(ComapreResults(
        std::vector<int>{MegaUserAlert::TYPE_PAYMENTREMINDER, MegaUserAlert::TYPE_PAYMENT_FAILED},
        std::move(megaAlerts)));

    // add a successful Payment done after the reminder - reminder should be removed
    userAlerts.add(
        new UserAlert::Payment(true, 1, referenceTime, userAlerts.nextId(), name_id::psts_v2));
    megaAlerts.reset(megaApi[0]->getUserAlerts());
    ASSERT_TRUE(ComapreResults(
        std::vector<int>{MegaUserAlert::TYPE_PAYMENT_FAILED, MegaUserAlert::TYPE_PAYMENT_SUCCEEDED},
        std::move(megaAlerts)));

    // prepare an empty file to upload
    // this will give alerts a chance to be persisted in database
    PerApi& target = mApi[0];
    target.resetlastEvent();
    std::unique_ptr<MegaNode> rootNode(megaApi[0]->getRootNode());
    ASSERT_NE(rootNode.get(), nullptr);

    const auto newNode = sdk_test::uploadFile(megaApi[0].get(),
                                              sdk_test::LocalTempFile{"alerts", 1},
                                              rootNode.get());
    ASSERT_TRUE(newNode) << "Cannot create node in Cloud Drive";
    ASSERT_TRUE(WaitFor(
        [&target]()
        {
            return target.lastEventsContain(MegaEvent::EVENT_COMMIT_DB);
        },
        10000));

    // store current session, for reading the same database later
    std::unique_ptr<char[]> session(dumpSession(0));

    // logout and use session to login, imitate app's restart
    ASSERT_NO_FATAL_FAILURE(locallogout(0));
    ASSERT_NO_FATAL_FAILURE(resumeSession(session.get(), 0));

    // this will make sure that alerts are loaded from database
    ASSERT_NO_FATAL_FAILURE(fetchnodes(0, maxTimeout));

    // verify the alerts after restart
    // the removed reminder won't appear anymore
    megaAlerts.reset(megaApi[0]->getUserAlerts());
    ASSERT_TRUE(ComapreResults(
        std::vector<int>{MegaUserAlert::TYPE_PAYMENT_FAILED, MegaUserAlert::TYPE_PAYMENT_SUCCEEDED},
        std::move(megaAlerts)));

    // add an old payment reminder - it should be removed
    userAlerts.add(new UserAlert::PaymentReminder(referenceTime - 100,
                                                  referenceTime + 100,
                                                  userAlerts.nextId()));
    megaAlerts.reset(megaApi[0]->getUserAlerts());
    ASSERT_TRUE(ComapreResults(
        std::vector<int>{MegaUserAlert::TYPE_PAYMENT_FAILED, MegaUserAlert::TYPE_PAYMENT_SUCCEEDED},
        std::move(megaAlerts)));

    // add new reminder later than the successful payment - reminder should be kept
    userAlerts.add(new UserAlert::PaymentReminder(referenceTime + 3,
                                                  referenceTime + 100,
                                                  userAlerts.nextId()));
    megaAlerts.reset(megaApi[0]->getUserAlerts());
    ASSERT_TRUE(ComapreResults(std::vector<int>{MegaUserAlert::TYPE_PAYMENT_FAILED,
                                                MegaUserAlert::TYPE_PAYMENT_SUCCEEDED,
                                                MegaUserAlert::TYPE_PAYMENTREMINDER},
                               std::move(megaAlerts)));

    // add an old successful Payment done before the reminder - reminder should be kept
    userAlerts.add(
        new UserAlert::Payment(true, 1, referenceTime + 1, userAlerts.nextId(), name_id::psts_v2));
    megaAlerts.reset(megaApi[0]->getUserAlerts());
    ASSERT_TRUE(ComapreResults(std::vector<int>{MegaUserAlert::TYPE_PAYMENT_FAILED,
                                                MegaUserAlert::TYPE_PAYMENT_SUCCEEDED,
                                                MegaUserAlert::TYPE_PAYMENTREMINDER,
                                                MegaUserAlert::TYPE_PAYMENT_SUCCEEDED},
                               std::move(megaAlerts)));
}

static bool containsAlertType(MegaUserAlertList* userList, int type)
{
    if (!userList)
    {
        return false;
    }

    for (int i = 0; i < userList->size(); ++i)
    {
        if (userList->get(i)->getType() == type)
        {
            return true;
        }
    }

    return false;
}

static bool compareAlertLists(MegaUserAlertList* baseline, MegaUserAlertList* current)
{
    if (!baseline || !current)
    {
        return false;
    }

    if (baseline->size() != current->size())
    {
        return false;
    }

    for (int i = 0; i < baseline->size(); ++i)
    {
        if (baseline->get(i)->getType() != current->get(i)->getType())
        {
            return false;
        }
    }
    return true;
}

/**
 * @brief SdkTest.UserAlertReminderDroppedForActivePro
 *
 * Test setup:
 *  - Ensure login and initial user alerts catchup are complete
 *  - Promote account to PRO and refresh account details
 *
 * Test steps:
 *  - Capture baseline user alerts list
 *  - Add an expired PaymentReminder
 *  - Verify alerts list remains unchanged (reminder dropped)
 *  - Demote to FREE and refresh account details
 *  - Local logout and resume session
 *  - Fetch nodes from cache/server
 *  - Verify alerts list remains unchanged after restart
 */
TEST_F(SdkTest, UserAlertReminderDroppedForActivePro)
{
    constexpr int catchupWaitMs = 3000;
    constexpr int proAccountLevel = 1;
    constexpr int freeAccountLevel = 0;
    constexpr auto reminderStartOffsetSec = 200;
    constexpr auto reminderEndOffsetSec = 100;

    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    ASSERT_TRUE(WaitFor(
        [this]()
        {
            return megaApi[0]->getClient()->useralerts.catchupdone;
        },
        catchupWaitMs));

    MegaClient* client = megaApi[0]->getClient();
    UserAlerts& userAlerts = client->useralerts;

    ASSERT_EQ(setAccountLevel(*megaApi[0],
                              MegaAccountDetails::ACCOUNT_TYPE_PROI,
                              proAccountLevel,
                              nullptr),
              API_OK);
    ASSERT_EQ(synchronousGetSpecificAccountDetails(0, false, false, true), API_OK);

    unique_ptr<MegaUserAlertList> baselineAlerts{megaApi[0]->getUserAlerts()};

    const auto referenceTime = time(nullptr);
    userAlerts.add(new UserAlert::PaymentReminder(referenceTime - reminderStartOffsetSec,
                                                  referenceTime - reminderEndOffsetSec,
                                                  userAlerts.nextId()));

    unique_ptr<MegaUserAlertList> megaAlerts{megaApi[0]->getUserAlerts()};
    ASSERT_TRUE(compareAlertLists(baselineAlerts.get(), megaAlerts.get()));

    // switch back to Free, restart session, and verify the reminder is still absent
    ASSERT_EQ(setAccountLevel(*megaApi[0],
                              MegaAccountDetails::ACCOUNT_TYPE_FREE,
                              freeAccountLevel,
                              nullptr),
              API_OK);
    ASSERT_EQ(synchronousGetSpecificAccountDetails(0, false, false, true), API_OK);

    std::unique_ptr<char[]> session(dumpSession(0));
    ASSERT_NO_FATAL_FAILURE(locallogout(0));
    ASSERT_NO_FATAL_FAILURE(resumeSession(session.get(), 0));
    ASSERT_NO_FATAL_FAILURE(fetchnodes(0, maxTimeout));

    ASSERT_TRUE(WaitFor(
        [this]()
        {
            return megaApi[0]->getClient()->useralerts.catchupdone;
        },
        catchupWaitMs));

    megaAlerts.reset(megaApi[0]->getUserAlerts());
    ASSERT_TRUE(compareAlertLists(baselineAlerts.get(), megaAlerts.get()));
}

/**
 * @brief SdkTest.UserAlertReminderPurgedAfterAccountUpgrade
 *
 * Test setup:
 *  - Ensure login and initial user alerts catchup are complete
 *
 * Test steps:
 *  - Add an expired PaymentReminder and verify it appears
 *  - Promote account to PRO and refresh account details
 *  - Wait until the reminder is purged
 *  - Force a DB commit to persist removals
 *  - Demote to FREE and refresh account details
 *  - Local logout and resume session
 *  - Fetch nodes from cache/server
 *  - Verify the reminder is still absent after restart
 */
TEST_F(SdkTest, UserAlertReminderPurgedAfterAccountUpgrade)
{
    constexpr int catchupWaitMs = 3000;
    constexpr int purgeWaitMs = 5000;
    constexpr int commitWaitMs = 10000;
    constexpr int proAccountLevel = 1;
    constexpr int freeAccountLevel = 0;
    constexpr auto reminderStartOffsetSec = 200;
    constexpr auto reminderEndOffsetSec = 100;
    constexpr size_t uploadBytes = 1;

    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    ASSERT_TRUE(WaitFor(
        [this]()
        {
            return megaApi[0]->getClient()->useralerts.catchupdone;
        },
        catchupWaitMs));

    MegaClient* client = megaApi[0]->getClient();
    UserAlerts& userAlerts = client->useralerts;

    const auto referenceTime = time(nullptr);
    userAlerts.add(new UserAlert::PaymentReminder(referenceTime - reminderStartOffsetSec,
                                                  referenceTime - reminderEndOffsetSec,
                                                  userAlerts.nextId()));

    unique_ptr<MegaUserAlertList> megaAlerts{megaApi[0]->getUserAlerts()};
    ASSERT_TRUE(containsAlertType(megaAlerts.get(), MegaUserAlert::TYPE_PAYMENTREMINDER));

    ASSERT_EQ(setAccountLevel(*megaApi[0],
                              MegaAccountDetails::ACCOUNT_TYPE_PROI,
                              proAccountLevel,
                              nullptr),
              API_OK);
    ASSERT_EQ(synchronousGetSpecificAccountDetails(0, false, false, true), API_OK);

    ASSERT_TRUE(WaitFor(
        [this]()
        {
            unique_ptr<MegaUserAlertList> megaAlerts{megaApi[0]->getUserAlerts()};
            return !containsAlertType(megaAlerts.get(), MegaUserAlert::TYPE_PAYMENTREMINDER);
        },
        purgeWaitMs));

    // Force a DB commit so the removal is persisted
    PerApi& target = mApi[0];
    target.resetlastEvent();
    std::unique_ptr<MegaNode> rootNode(megaApi[0]->getRootNode());
    ASSERT_NE(rootNode.get(), nullptr);

    const auto newNode = sdk_test::uploadFile(megaApi[0].get(),
                                              sdk_test::LocalTempFile{"alerts_purge", uploadBytes},
                                              rootNode.get());
    ASSERT_TRUE(newNode) << "Cannot create node in Cloud Drive";
    ASSERT_TRUE(WaitFor(
        [&target]()
        {
            return target.lastEventsContain(MegaEvent::EVENT_COMMIT_DB);
        },
        commitWaitMs));

    // switch back to Free, restart session, and verify the reminder is still absent
    ASSERT_EQ(setAccountLevel(*megaApi[0],
                              MegaAccountDetails::ACCOUNT_TYPE_FREE,
                              freeAccountLevel,
                              nullptr),
              API_OK);
    ASSERT_EQ(synchronousGetSpecificAccountDetails(0, false, false, true), API_OK);

    std::unique_ptr<char[]> session(dumpSession(0));
    ASSERT_NO_FATAL_FAILURE(locallogout(0));
    ASSERT_NO_FATAL_FAILURE(resumeSession(session.get(), 0));
    ASSERT_NO_FATAL_FAILURE(fetchnodes(0, maxTimeout));

    ASSERT_TRUE(WaitFor(
        [this]()
        {
            return megaApi[0]->getClient()->useralerts.catchupdone;
        },
        catchupWaitMs));

    megaAlerts.reset(megaApi[0]->getUserAlerts());
    ASSERT_FALSE(containsAlertType(megaAlerts.get(), MegaUserAlert::TYPE_PAYMENTREMINDER));
}
}
