/**
 * @file SdkTestCreateAccount_test.cpp
 * @brief This file defines some tests that check account creation with different types of clients
 */

#include "env_var_accounts.h"
#include "megaapi.h"
#include "sdk_test_utils.h"
#include "SdkTest_test.h"

#include <gmock/gmock.h>

using namespace sdk_test;

/**
 * @class SdkTesCreateAccount
 * @brief An abstract class that provides a template fixture/test suite to test account creation
 * with different client types
 *
 */
class SdkTestCreateAccount: public SdkTest, virtual public ::testing::WithParamInterface<int>
{
public:
    void TearDown() override;

    void doCreateAccountTest(const string& testName, int clientType);
};

std::string getLinkFromMailbox(const string& exe, // Python
                               const string& script, // email_processor.py
                               const string& realAccount, // user
                               const string& realPswd, // password for user@host.domain
                               const string& toAddr, // user+testnewaccount@host.domain
                               const string& intent, // confirm / delete
                               const chrono::steady_clock::time_point& timeOfEmail)
{
    using namespace std::chrono; // Just for this little scope

    std::string command = exe + " \"" + script + "\" \"" + realAccount + "\" \"" + realPswd +
                          "\" \"" + toAddr + "\" " + intent;
    std::string output;

    // Wait for the link to be sent
    constexpr seconds delta = 10s;
    constexpr minutes maxTimeout = 10min;
    seconds spentTime = 0s;
    for (; spentTime < maxTimeout && output.empty(); spentTime += delta)
    {
        WaitMillisec(duration_cast<milliseconds>(delta).count());

        // get time interval to look for emails, add some seconds to account for delays related to
        // the python script call
        constexpr seconds safetyDelay = 5s;
        const auto attemptTime = steady_clock::now();
        seconds timeSinceEmail = duration_cast<seconds>(attemptTime - timeOfEmail) + safetyDelay;
        // Run Python script
        output = runProgram(command + ' ' + std::to_string(timeSinceEmail.count()),
                            PROG_OUTPUT_TYPE::TEXT);
    }
    LOG_debug << "Time spent trying to get the email: " << spentTime.count() << "s";

    // Print whatever was fetched from the mailbox
    LOG_debug << "Link from email (" << intent << "): " << (output.empty() ? "[empty]" : output);

    // Validate the link
    constexpr char expectedLinkPrefix[] = "https://";
    return output.substr(0, sizeof(expectedLinkPrefix) - 1) == expectedLinkPrefix ? output :
                                                                                    std::string();
}

std::string getUniqueAlias()
{
    // use n random chars
    int n = 4;
    std::string alias;
    auto t = std::time(nullptr);
    srand((unsigned int)t);
    for (int i = 0; i < n; ++i)
    {
        alias += static_cast<char>('a' + rand() % 26);
    }

    // add a timestamp
    auto tm = *std::localtime(&t);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y%m%d%H%M%S");
    alias += oss.str();

    return alias;
}

void SdkTestCreateAccount::TearDown()
{
    releaseMegaApi(1);
    releaseMegaApi(2);
    if (!megaApi.empty() && megaApi[0])
    {
        releaseMegaApi(0);
    }
    out() << "Teardown done, test exiting";
}

/*
 * doCreateAccountTest(testName, clientType)
 *
 * testName
 * - Name of the concrete test invoking this function.
 *
 * clientType
 * - The type of the client we want to perform this test on.
 *
 * This function tests the creation of a new account for a random user.
 *  - Create account and send confirmation link
 *  - Logout and resume the create-account process
 *  - Extract confirmation link from the mailbox
 *  - Use the link to confirm the account
 *
 *  - Request a reset password link
 *  - Confirm the reset password
 *
 *  - Login to the new account
 *  - Request cancel account link
 *  - Extract cancel account link from the mailbox
 *  - Use the link to cancel the account
 */
void SdkTestCreateAccount::doCreateAccountTest(const string& testName, int clientType)
{
    LOG_info << "___TEST " << testName << "____";

    // Make sure the new account details have been set up
    const auto bufRealEmail = Utils::getenv("MEGA_REAL_EMAIL", ""); // user@host.domain
    const auto bufRealPswd =
        Utils::getenv("MEGA_REAL_PWD", ""); // email password of user@host.domain
    fs::path bufScript = getLinkExtractSrciptPath();
    ASSERT_TRUE(!bufRealEmail.empty() && !bufRealPswd.empty())
        << "MEGA_REAL_EMAIL, MEGA_REAL_PWD env vars must all be defined";

    // test that Python 3 was installed
    std::string pyExe = "python";
    {
        const std::string pyOpt = " -V";
        const std::string pyExpected = "Python 3.";
        std::string output = runProgram(pyExe + pyOpt, PROG_OUTPUT_TYPE::TEXT); // Python -V
        if (output.substr(0, pyExpected.length()) != pyExpected)
        {
            pyExe += "3";
            output = runProgram(pyExe + pyOpt, PROG_OUTPUT_TYPE::TEXT); // Python3 -V
            ASSERT_EQ(pyExpected, output.substr(0, pyExpected.length()))
                << "Python 3 was not found.";
        }
        LOG_debug << "Using " << output;
    }

    megaApi.resize(1);
    mApi.resize(1);
    ASSERT_NO_FATAL_FAILURE(configureTestInstance(0, bufRealEmail, bufRealPswd, true, clientType));

    // create the account
    // ------------------
    LOG_debug << testName << ": Start account creation";

    const std::string realEmail(bufRealEmail); // user@host.domain
    std::string::size_type pos = realEmail.find('@');
    const std::string realAccount = realEmail.substr(0, pos); // user
    [[maybe_unused]] const auto [testEmail, _] = getEnvVarAccounts().getVarValues(0);
    const std::string newTestAcc = realAccount + '+' + testEmail.substr(0, testEmail.find("@")) +
                                   '+' + getUniqueAlias() +
                                   realEmail.substr(pos); // user+testUser+rand20210919@host.domain
    LOG_info << "Creating Mega account " << newTestAcc;
    const char* origTestPwd = "TestPswd!@#$"; // maybe this should be logged too, changed later

    // save point in time for account init
    std::chrono::time_point timeOfConfirmEmail = std::chrono::steady_clock::now();

    // Create an ephemeral session internally and send a confirmation link to email
    ASSERT_EQ(
        API_OK,
        synchronousCreateAccount(0, newTestAcc.c_str(), origTestPwd, "MyFirstname", "MyLastname"));

    // Wait for the client to import the "Welcome PDF."
    WaitMillisec(8000);

    if (clientType == MegaApi::CLIENT_TYPE_PASSWORD_MANAGER)
    {
        RequestTracker rt{megaApi[0].get()};
        megaApi[0]->getPasswordManagerBase(&rt);
        EXPECT_EQ(API_OK, rt.waitForResult())
            << "Getting Password Manager Base node through shortcut failed";
        EXPECT_NE(nullptr, rt.request);
        EXPECT_NE(INVALID_HANDLE, rt.request->getNodeHandle())
            << "Invalid Password Manager Base node retrieved";
    }

    LOG_debug << testName << ": Logout and resume";
    // Logout from ephemeral session and resume session
    ASSERT_NO_FATAL_FAILURE(locallogout());
    ASSERT_EQ(API_OK, synchronousResumeCreateAccount(0, mApi[0].getSid().c_str()));

    // Get confirmation link from the email
    {
        LOG_debug << testName << ": Get confirmation link from email";
        std::string conformLink = getLinkFromMailbox(pyExe,
                                                     bufScript.string(),
                                                     realAccount,
                                                     bufRealPswd,
                                                     newTestAcc,
                                                     MegaClient::confirmLinkPrefix(),
                                                     timeOfConfirmEmail);
        ASSERT_FALSE(conformLink.empty()) << "Confirmation link was not found.";

        LOG_debug << testName << ": Confirm account";
        // create another connection to confirm the account
        megaApi.resize(2);
        mApi.resize(2);
        ASSERT_NO_FATAL_FAILURE(
            configureTestInstance(1, bufRealEmail, bufRealPswd, true, clientType));

        PerApi& initialConn = mApi[0];
        initialConn.resetlastEvent();

        // Use confirmation link
        ASSERT_EQ(API_OK, synchronousConfirmSignupLink(1, conformLink.c_str(), origTestPwd));

        // check for event triggered by 'uec' action packet received after the confirmation
        EXPECT_TRUE(WaitFor(
            [&initialConn]()
            {
                return initialConn.lastEventsContain(MegaEvent::EVENT_CONFIRM_USER_EMAIL);
            },
            10000))
            << "EVENT_CONFIRM_USER_EMAIL event triggered by 'uec' action packet was not received";
    }

    // Login to the new account
    {
        LOG_debug << testName << ": Login to the new account";
        std::unique_ptr<RequestTracker> loginTracker =
            std::make_unique<RequestTracker>(megaApi[0].get());
        megaApi[0]->login(newTestAcc.c_str(), origTestPwd, loginTracker.get());
        ASSERT_EQ(API_OK, loginTracker->waitForResult())
            << " Failed to login to account " << newTestAcc.c_str();
    }

    // fetchnodes // needed internally to fill in user details, including email
    {
        LOG_debug << testName << ": fetch nodes from new account";
        std::unique_ptr<RequestTracker> fetchnodesTracker =
            std::make_unique<RequestTracker>(megaApi[0].get());
        megaApi[0]->fetchNodes(fetchnodesTracker.get());
        ASSERT_EQ(API_OK, fetchnodesTracker->waitForResult())
            << " Failed to fetchnodes for account " << newTestAcc.c_str();
    }

    // test resetting the password
    // ---------------------------

    LOG_debug << testName << ": Start reset password";
    std::chrono::time_point timeOfResetEmail = chrono::steady_clock::now();
    ASSERT_EQ(synchronousResetPassword(0, newTestAcc.c_str(), true), MegaError::API_OK)
        << "resetPassword failed";

    // Get cancel account link from the mailbox
    const char* newTestPwd = "PassAndGotHerPhoneNumber!#$**!";
    {
        LOG_debug << testName << ": Get password reset link from email";
        std::string recoverink = getLinkFromMailbox(pyExe,
                                                    bufScript.string(),
                                                    realAccount,
                                                    bufRealPswd,
                                                    newTestAcc,
                                                    MegaClient::recoverLinkPrefix(),
                                                    timeOfResetEmail);
        ASSERT_FALSE(recoverink.empty()) << "Recover account link was not found.";

        LOG_debug << testName << ": Confirm reset password";
        char* masterKey = megaApi[0]->exportMasterKey();
        ASSERT_EQ(synchronousConfirmResetPassword(0, recoverink.c_str(), newTestPwd, masterKey),
                  MegaError::API_OK)
            << "confirmResetPassword failed";
    }

    // Login using new password
    {
        LOG_debug << testName << ": Login with new password";
        std::unique_ptr<RequestTracker> loginTracker =
            std::make_unique<RequestTracker>(megaApi[0].get());
        megaApi[0]->login(newTestAcc.c_str(), newTestPwd, loginTracker.get());
        ASSERT_EQ(API_OK, loginTracker->waitForResult())
            << " Failed to login to account after change password with new password "
            << newTestAcc.c_str();
    }

    // fetchnodes - needed internally to fill in user details, to allow cancelAccount() to work
    {
        LOG_debug << testName << ": Fetching nodes";
        std::unique_ptr<RequestTracker> fetchnodesTracker =
            std::make_unique<RequestTracker>(megaApi[0].get());
        megaApi[0]->fetchNodes(fetchnodesTracker.get());
        ASSERT_EQ(API_OK, fetchnodesTracker->waitForResult())
            << " Failed to fetchnodes after change password for account " << newTestAcc.c_str();
    }

    // test changing the email (check change with auxiliar instance)
    // -----------------------

    LOG_debug << testName << ": Start email change";
    // login with auxiliar instance
    LOG_debug << testName << ": Login auxiliar account";
    megaApi.resize(2);
    mApi.resize(2);
    ASSERT_NO_FATAL_FAILURE(configureTestInstance(1, newTestAcc, newTestPwd, true, clientType));
    {
        std::unique_ptr<RequestTracker> loginTracker =
            std::make_unique<RequestTracker>(megaApi[1].get());
        megaApi[1]->login(newTestAcc.c_str(), newTestPwd, loginTracker.get());
        ASSERT_EQ(API_OK, loginTracker->waitForResult()) << " Failed to login to auxiliar account ";
    }

    LOG_debug << testName << ": Send change email request";
    const std::string changedTestAcc = Utils::replace(newTestAcc, "@", "-new@");
    std::chrono::time_point timeOfChangeEmail = chrono::steady_clock::now();
    ASSERT_EQ(synchronousChangeEmail(0, changedTestAcc.c_str()), MegaError::API_OK)
        << "changeEmail failed";

    {
        LOG_debug << testName << ": Get change email link from email inbox";
        std::string changelink = getLinkFromMailbox(pyExe,
                                                    bufScript.string(),
                                                    realAccount,
                                                    bufRealPswd,
                                                    changedTestAcc,
                                                    MegaClient::verifyLinkPrefix(),
                                                    timeOfChangeEmail);
        ASSERT_FALSE(changelink.empty()) << "Change email account link was not found.";

        LOG_debug << testName << ": Confirm email change";
        ASSERT_STRCASEEQ(newTestAcc.c_str(),
                         std::unique_ptr<char[]>{megaApi[0]->getMyEmail()}.get())
            << "email changed prematurely";
        ASSERT_EQ(synchronousConfirmChangeEmail(0, changelink.c_str(), newTestPwd),
                  MegaError::API_OK)
            << "confirmChangeEmail failed";
    }

    {
        // Check if our own email is updated after receive ug at auxiliar instance
        LOG_debug << testName << ": Check email is updated";
        std::unique_ptr<RequestTracker> userDataTracker =
            std::make_unique<RequestTracker>(megaApi[1].get());
        megaApi[1]->getUserData(userDataTracker.get());
        ASSERT_EQ(API_OK, userDataTracker->waitForResult())
            << " Failed to get user data at auxiliar account";
        ASSERT_EQ(changedTestAcc, std::unique_ptr<char[]>{megaApi[1]->getMyEmail()}.get())
            << "Email update error at auxiliar account";
        logout(1, false, maxTimeout);
    }

    // Login using new email
    ASSERT_STRCASEEQ(changedTestAcc.c_str(),
                     std::unique_ptr<char[]>{megaApi[0]->getMyEmail()}.get())
        << "email not changed correctly";
    {
        LOG_debug << testName << ": Login with new email";
        std::unique_ptr<RequestTracker> loginTracker =
            std::make_unique<RequestTracker>(megaApi[0].get());
        megaApi[0]->login(changedTestAcc.c_str(), newTestPwd, loginTracker.get());
        ASSERT_EQ(API_OK, loginTracker->waitForResult())
            << " Failed to login to account after change email with new email "
            << changedTestAcc.c_str();
    }

    // fetchnodes - needed internally to fill in user details, to allow cancelAccount() to work
    {
        LOG_debug << testName << ": Fetching nodes";
        std::unique_ptr<RequestTracker> fetchnodesTracker =
            std::make_unique<RequestTracker>(megaApi[0].get());
        megaApi[0]->fetchNodes(fetchnodesTracker.get());
        ASSERT_EQ(API_OK, fetchnodesTracker->waitForResult())
            << " Failed to fetchnodes after change password for account " << changedTestAcc.c_str();
    }

    ASSERT_STRCASEEQ(changedTestAcc.c_str(),
                     std::unique_ptr<char[]>{megaApi[0]->getMyEmail()}.get())
        << "my email not set correctly after changed";

    // delete the account
    // ------------------

    // Request cancel account link
    LOG_debug << testName << ": Start deleting account";
    chrono::time_point timeOfDeleteEmail = chrono::steady_clock::now();
    {
        LOG_debug << testName << ": Request account cancel";
        unique_ptr<RequestTracker> cancelLinkTracker =
            std::make_unique<RequestTracker>(megaApi[0].get());
        megaApi[0]->cancelAccount(cancelLinkTracker.get());
        ASSERT_EQ(API_OK, cancelLinkTracker->waitForResult())
            << " Failed to request cancel link for account " << changedTestAcc.c_str();
    }

    // Get cancel account link from the mailbox
    {
        LOG_debug << testName << ": Get cancel link from email";
        std::string deleteLink = getLinkFromMailbox(pyExe,
                                                    bufScript.string(),
                                                    realAccount,
                                                    bufRealPswd,
                                                    changedTestAcc,
                                                    MegaClient::cancelLinkPrefix(),
                                                    timeOfDeleteEmail);
        ASSERT_FALSE(deleteLink.empty()) << "Cancel account link was not found.";

        // Use cancel account link
        LOG_debug << testName << ": Confirm cancel link";
        std::unique_ptr<RequestTracker> useCancelLinkTracker =
            std::make_unique<RequestTracker>(megaApi[0].get());
        megaApi[0]->confirmCancelAccount(deleteLink.c_str(),
                                         newTestPwd,
                                         useCancelLinkTracker.get());
        // Allow API_ESID beside API_OK, due to the race between sc and cs channels
        ASSERT_PRED3(
            [](int t, int v1, int v2)
            {
                return t == v1 || t == v2;
            },
            useCancelLinkTracker->waitForResult(),
            API_OK,
            API_ESID)
            << " Failed to confirm cancel account " << changedTestAcc.c_str();
    }
}

/**
 * @brief Test_P CreateAccount
 *
 * Tests account creation for any client type client.
 *
 * See doCreateAccountTest(...).
 */
TEST_P(SdkTestCreateAccount, CreateAccount)
{
    int clientType = GetParam();
    ASSERT_NO_FATAL_FAILURE(doCreateAccountTest("SdkTestVPNCreateAccount", clientType));
}

INSTANTIATE_TEST_SUITE_P(CreateAccount,
                         SdkTestCreateAccount,
                         ::testing::Values(MegaApi::CLIENT_TYPE_DEFAULT,
                                           MegaApi::CLIENT_TYPE_VPN,
                                           MegaApi::CLIENT_TYPE_PASSWORD_MANAGER));
