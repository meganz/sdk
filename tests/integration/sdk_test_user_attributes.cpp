/**
 * @file sdk_test_user_attributes.cpp
 * @brief This file defines tests that involve interactions with User attributes. They include
 * operations like:
 *   - Set and get attribute using generic function
 *   - Set and get attribute using dedicated function
 *   - Delete attribute
 */

#include "SdkTest_test.h"

#include <gmock/gmock.h>

#include <charconv>

namespace
{

/**
 * @class SdkTestUserAttribute
 * @brief Setup an account with a certain user.
 *
 */
class SdkTestUserAttribute: public SdkTest
{
protected:
    void testStaticInformation(int at, const std::string& name, const std::string& longName)
    {
        ASSERT_GE(megaApi.size(), 1u);
        ASSERT_EQ(megaApi[0]->userAttributeFromString(name.c_str()), at);
        ASSERT_STREQ(megaApi[0]->userAttributeToString(at), name.c_str());
        ASSERT_STREQ(megaApi[0]->userAttributeToLongName(at), longName.c_str());
    }

    void testGenericSet(int at, ErrorCodes err)
    {
        ASSERT_GE(megaApi.size(), 1u);

        RequestTracker setAttrTracker(megaApi[0].get());
        megaApi[0]->setUserAttribute(at, "", &setAttrTracker);
        ASSERT_EQ(setAttrTracker.waitForResult(), err)
            << "Unexpected result of setUserAttribute() for "
            << megaApi[0]->userAttributeToLongName(at);
    }

    void testGenericGet(int at, const std::vector<ErrorCodes>& results, MegaUser* user = nullptr)
    {
        RequestTracker getAttrTracker(megaApi[0].get());
        megaApi[0]->getUserAttribute(user, at, &getAttrTracker);
        EXPECT_THAT(results, testing::Contains(getAttrTracker.waitForResult()))
            << "Unexpected result of getUserAttribute() for "
            << megaApi[0]->userAttributeToLongName(at);
    }

    template<typename T>
    void testValue(int at,
                   std::function<void(RequestTracker&)> get,
                   std::function<void(T, RequestTracker&)> set = nullptr,
                   const std::vector<T>& alternatives = {})
    {
        ASSERT_GE(megaApi.size(), 1u);
        string attributeName = megaApi[0]->userAttributeToLongName(at);
        T originalValue{};
        bool removeAttribute = false;

        if (get)
        {
            // get original attribute value
            auto [ec, v] = testValueGetOnly<T>(at, get);
            ASSERT_FALSE(HasFatalFailure());
            ASSERT_THAT(ec, testing::AnyOf(API_OK, API_ENOENT));
            originalValue = std::move(v);
            removeAttribute = ec == API_ENOENT;
        }

        ASSERT_GE(alternatives.size(), 2u);
        T newValue = originalValue == alternatives[0] ? alternatives[1] : alternatives[0];

        // set new value to attribute
        {
            RequestTracker setAttrTracker(megaApi[0].get());
            set(newValue, setAttrTracker);
            ASSERT_EQ(API_OK, setAttrTracker.waitForResult())
                << "Failed to set " << attributeName << " to new value";

            if (!get)
                return; // nothing more to test here

            // confirm that it worked
            auto [ec, v] = testValueGetOnly<T>(at, get);
            ASSERT_FALSE(HasFatalFailure());
            ASSERT_EQ(ec, API_OK);
            ASSERT_EQ(v, newValue);
        }

        // set attribute back to original value
        {
            RequestTracker setAttrTracker(megaApi[0].get());
            if (removeAttribute)
            {
                megaApi[0]->deleteUserAttribute(at, &setAttrTracker);
                ASSERT_EQ(API_OK, setAttrTracker.waitForResult())
                    << "Failed to deleteUserAttribute() " << attributeName;
            }
            else
            {
                set(originalValue, setAttrTracker);
                ASSERT_EQ(API_OK, setAttrTracker.waitForResult())
                    << "Failed to set " << attributeName << " to original value";

                // confirm that it worked
                auto [ec, v] = testValueGetOnly<T>(at, get);
                ASSERT_FALSE(HasFatalFailure());
                ASSERT_EQ(ec, API_OK);
                ASSERT_EQ(v, originalValue);
            }
        }
    }

    template<typename T>
    void testRawPointer(int at,
                        std::function<void(RequestTracker&)> get,
                        std::function<void(T*, RequestTracker&)> set = nullptr,
                        const std::vector<std::shared_ptr<T>>& alternatives = {})
    {
        ASSERT_GE(megaApi.size(), 1u);
        string attributeName = megaApi[0]->userAttributeToLongName(at);
        std::unique_ptr<T> originalValue;
        bool removeAttribute = false;

        if (get)
        {
            // get original attribute value
            RequestTracker getAttrTracker(megaApi[0].get());
            get(getAttrTracker);
            ErrorCodes ec = getAttrTracker.waitForResult();
            if (ec == API_ENOENT)
            {
                removeAttribute = true;
            }
            else
            {
                ASSERT_EQ(API_OK, ec) << "Failed to get " << attributeName;
                ASSERT_NO_FATAL_FAILURE(originalValue.reset(
                    std::get<T*>(getRelevantPointer<T*>(at, *getAttrTracker.request))->copy()));
            }
        }

        if (!set)
            return; // nothing left to do here

        ASSERT_GE(alternatives.size(), 2u);
        std::shared_ptr<T> newValue{
            (originalValue && equalValues(*originalValue, *alternatives[0])) ? alternatives[1] :
                                                                               alternatives[0]};

        // set new value to attribute
        {
            RequestTracker setAttrTracker(megaApi[0].get());
            set(newValue.get(), setAttrTracker);
            ASSERT_EQ(API_OK, setAttrTracker.waitForResult())
                << "Failed to set " << attributeName << " to new value";

            if (!get)
                return; // nothing more to test here

            // confirm that it worked
            RequestTracker getAttrTracker(megaApi[0].get());
            get(getAttrTracker);
            ASSERT_EQ(API_OK, getAttrTracker.waitForResult());
            T* v{};
            ASSERT_NO_FATAL_FAILURE(
                v = std::get<T*>(getRelevantPointer<T*>(at, *getAttrTracker.request)));
            ASSERT_TRUE(equalValues(*v, *newValue));
        }

        // set attribute back to original value
        {
            RequestTracker setAttrTracker(megaApi[0].get());
            if (removeAttribute)
            {
                megaApi[0]->deleteUserAttribute(at, &setAttrTracker);
                ASSERT_EQ(API_OK, setAttrTracker.waitForResult())
                    << "Failed to deleteUserAttribute() " << attributeName;
            }
            else
            {
                set(originalValue.get(), setAttrTracker);
                ASSERT_EQ(API_OK, setAttrTracker.waitForResult())
                    << "Failed to set " << attributeName << " to original value";

                // confirm that it worked
                RequestTracker getAttrTracker(megaApi[0].get());
                get(getAttrTracker);
                ASSERT_EQ(API_OK, getAttrTracker.waitForResult());
                T* v{};
                ASSERT_NO_FATAL_FAILURE(
                    v = std::get<T*>(getRelevantPointer<T*>(at, *getAttrTracker.request)));
                ASSERT_TRUE(equalValues(*v, *originalValue));
            }
        }
    }

private:
    template<typename T>
    std::tuple<ErrorCodes, T> testValueGetOnly(int at, std::function<void(RequestTracker&)> get)
    {
        RequestTracker getAttrTracker(megaApi[0].get());
        get(getAttrTracker);
        ErrorCodes ec = getAttrTracker.waitForResult();
        T originalValue = ec == API_OK ? getRelevantValue<T>(at, *getAttrTracker.request) : T{};
        return {ec, originalValue};
    }

    template<typename T>
    T getNumericValueFromText(const std::string& text) const
    {
        if constexpr (std::is_same_v<T, bool>)
        {
            EXPECT_EQ(text.size(), 1);
            return text.size() == 1 && text[0] == '1';
        }
        else
        {
            T v{};
            auto [ptr, error] = std::from_chars(text.data(), text.data() + text.size(), v);
            EXPECT_EQ(error, std::errc{});
            return v;
        }
    }

    template<typename T>
    T getRelevantValue(int at, const MegaRequest& request) const
    {
        bool testFlag = false;

        switch (at)
        {
            case MegaApi::USER_ATTR_RUBBISH_TIME:
            case MegaApi::USER_ATTR_STORAGE_STATE:
                return static_cast<T>(request.getNumber());
            case MegaApi::USER_ATTR_COOKIE_SETTINGS:
                return static_cast<T>(request.getNumDetails());
            case MegaApi::USER_ATTR_CAMERA_UPLOADS_FOLDER:
            case MegaApi::USER_ATTR_MY_CHAT_FILES_FOLDER:
                return static_cast<T>(request.getNodeHandle());
            case MegaApi::USER_ATTR_LAST_PSA:
            {
                EXPECT_THAT(request.getText(), testing::NotNull());
                T v{};
                if (request.getText())
                {
                    std::string textB64{request.getText()};
                    std::string text{Base64::atob(textB64)};
                    v = getNumericValueFromText<T>(text);
                }
                return v;
            }
            case MegaApi::USER_ATTR_DISABLE_VERSIONS:
            case MegaApi::USER_ATTR_CONTACT_LINK_VERIFICATION:
            case MegaApi::USER_ATTR_VISIBLE_WELCOME_DIALOG:
            case MegaApi::USER_ATTR_VISIBLE_TERMS_OF_SERVICE:
            case MegaApi::USER_ATTR_WELCOME_PDF_COPIED:
                testFlag = true;
                [[fallthrough]];
            default:
            {
                EXPECT_THAT(request.getText(), testing::NotNull());
                T v = request.getText() == nullptr ? T{} :
                                                     getNumericValueFromText<T>(request.getText());

                if (testFlag)
                {
                    EXPECT_EQ(v, request.getFlag());
                }

                return v;
            }
        }
    }

    template<typename T>
    std::variant<MegaStringMap*, MegaPushNotificationSettings*>
        getRelevantPointer(int at, const MegaRequest& request) const
    {
        switch (at)
        {
            case MegaApi::USER_ATTR_PUSH_SETTINGS:
                return const_cast<MegaPushNotificationSettings*>(
                    request.getMegaPushNotificationSettings());
            default:
                return request.getMegaStringMap();
        }
    }

    static bool equalValues(const MegaPushNotificationSettings& first,
                            const MegaPushNotificationSettings& second)
    {
        return static_cast<const MegaPushNotificationSettingsPrivate&>(first) ==
               static_cast<const MegaPushNotificationSettingsPrivate&>(second);
    }

    static bool equalValues(const MegaStringMap& first, const MegaStringMap& second)
    {
        return *static_cast<const MegaStringMapPrivate&>(first).getMap() ==
               *static_cast<const MegaStringMapPrivate&>(second).getMap();
    }
};

template<>
std::string SdkTestUserAttribute::getRelevantValue<std::string>([[maybe_unused]] int at,
                                                                const MegaRequest& request) const
{
    EXPECT_THAT(request.getText(), testing::NotNull());
    return request.getText() ? request.getText() : "";
}

/**
 * @brief SdkTestUserAttribute.NoAccess
 */
TEST_F(SdkTestUserAttribute, NoAccess)
{
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    int at = MegaApi::USER_ATTR_AUTHRING;
    ASSERT_NO_FATAL_FAILURE(testStaticInformation(at, "*!authring", "AUTHRING"));
    ASSERT_NO_FATAL_FAILURE(testGenericSet(at, API_EACCESS));
    ASSERT_NO_FATAL_FAILURE(testGenericGet(at, {API_OK, API_ENOENT}));

    at = MegaApi::USER_ATTR_ED25519_PUBLIC_KEY;
    ASSERT_NO_FATAL_FAILURE(testStaticInformation(at, "+puEd255", "ED25519_PUBK"));
    ASSERT_NO_FATAL_FAILURE(testGenericSet(at, API_EACCESS));
    ASSERT_NO_FATAL_FAILURE(testGenericGet(at, {API_OK, API_ENOENT}));

    at = MegaApi::USER_ATTR_CU25519_PUBLIC_KEY;
    ASSERT_NO_FATAL_FAILURE(testStaticInformation(at, "+puCu255", "CU25519_PUBK"));
    ASSERT_NO_FATAL_FAILURE(testGenericSet(at, API_EACCESS));
    ASSERT_NO_FATAL_FAILURE(testGenericGet(at, {API_OK, API_ENOENT}));

    at = MegaApi::USER_ATTR_KEYRING;
    ASSERT_NO_FATAL_FAILURE(testStaticInformation(at, "*keyring", "KEYRING"));
    ASSERT_NO_FATAL_FAILURE(testGenericSet(at, API_EACCESS));
    ASSERT_NO_FATAL_FAILURE(testGenericGet(at, {API_OK, API_ENOENT}));

    at = MegaApi::USER_ATTR_SIG_RSA_PUBLIC_KEY;
    ASSERT_NO_FATAL_FAILURE(testStaticInformation(at, "+sigPubk", "SIG_RSA_PUBK"));
    ASSERT_NO_FATAL_FAILURE(testGenericSet(at, API_EACCESS));
    ASSERT_NO_FATAL_FAILURE(testGenericGet(at, {API_OK, API_ENOENT}));

    at = MegaApi::USER_ATTR_SIG_CU255_PUBLIC_KEY;
    ASSERT_NO_FATAL_FAILURE(testStaticInformation(at, "+sigCu255", "SIG_CU255_PUBK"));
    ASSERT_NO_FATAL_FAILURE(testGenericSet(at, API_EACCESS));
    ASSERT_NO_FATAL_FAILURE(testGenericGet(at, {API_OK, API_ENOENT}));

    at = 29; // ATTR_AUTHCU255 (deprecated)
    ASSERT_NO_FATAL_FAILURE(testStaticInformation(at, "*!authCu255", "AUTHCU255"));
    ASSERT_NO_FATAL_FAILURE(testGenericSet(at, API_EACCESS));
    ASSERT_NO_FATAL_FAILURE(testGenericGet(at, {API_EARGS, API_ENOENT}));

    at = MegaApi::USER_ATTR_MY_BACKUPS_FOLDER;
    ASSERT_NO_FATAL_FAILURE(testStaticInformation(at, "^!bak", "MY_BACKUPS_FOLDER"));
    ASSERT_NO_FATAL_FAILURE(testGenericSet(at, API_EACCESS));
    ASSERT_NO_FATAL_FAILURE(testGenericGet(at, {API_OK, API_ENOENT}));

    at = MegaApi::USER_ATTR_JSON_SYNC_CONFIG_DATA;
    ASSERT_NO_FATAL_FAILURE(testStaticInformation(at, "*~jscd", "JSON_SYNC_CONFIG_DATA"));
    ASSERT_NO_FATAL_FAILURE(testGenericSet(at, API_EARGS));
    ASSERT_NO_FATAL_FAILURE(testGenericGet(at, {API_OK, API_ENOENT}));

    at = 37; // ATTR_KEYS
    ASSERT_NO_FATAL_FAILURE(testStaticInformation(at, "^!keys", "KEYS"));
    ASSERT_NO_FATAL_FAILURE(testGenericSet(at, API_EACCESS));
    ASSERT_NO_FATAL_FAILURE(testGenericGet(at, {API_OK, API_ENOENT}));
}

/**
 * @brief SdkTestUserAttribute.Lastname
 */
TEST_F(SdkTestUserAttribute, Lastname)
{
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(2));

    int at = MegaApi::USER_ATTR_LASTNAME;

    ASSERT_NO_FATAL_FAILURE(testStaticInformation(at, "lastname", "LASTNAME"));

    // test generic interfaces
    ASSERT_NO_FATAL_FAILURE(testValue<std::string>(
        at,
        [api = megaApi[0].get(), at](RequestTracker& tracker)
        {
            api->getUserAttribute(at, &tracker);
        },
        [api = megaApi[0].get(), at](std::string newValue, RequestTracker& tracker)
        {
            api->setUserAttribute(at, newValue.c_str(), &tracker);
        },
        {"LastName 1", "LastName 2"}));

    // test generic getUserAttribute for other user
    ASSERT_NO_FATAL_FAILURE(testGenericGet(at, {API_OK, API_ENOENT}, megaApi[1]->getMyUser()));
}

/**
 * @brief SdkTestUserAttribute.PasswordReminder
 */
TEST_F(SdkTestUserAttribute, PasswordReminder)
{
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    int at = MegaApi::USER_ATTR_PWD_REMINDER;

    ASSERT_NO_FATAL_FAILURE(testStaticInformation(at, "^!prd", "PWD_REMINDER"));

    ASSERT_NO_FATAL_FAILURE(testGenericSet(at, API_EARGS));
    ASSERT_NO_FATAL_FAILURE(testGenericGet(at, {API_OK, API_ENOENT}));
}

/**
 * @brief SdkTestUserAttribute.DisableVersions
 */
TEST_F(SdkTestUserAttribute, DisableVersions)
{
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    int at = MegaApi::USER_ATTR_DISABLE_VERSIONS;

    ASSERT_NO_FATAL_FAILURE(testStaticInformation(at, "^!dv", "DISABLE_VERSIONS"));

    // test generic interfaces
    ASSERT_NO_FATAL_FAILURE(testValue<bool>(
        at,
        [api = megaApi[0].get(), at](RequestTracker& tracker)
        {
            api->getUserAttribute(at, &tracker);
        },
        [api = megaApi[0].get(), at](bool newValue, RequestTracker& tracker)
        {
            std::string v{std::to_string(newValue)};
            api->setUserAttribute(at, v.c_str(), &tracker);
        },
        {true, false}));

    // test dedicated interfaces
    ASSERT_NO_FATAL_FAILURE(testValue<bool>(
        at,
        [api = megaApi[0].get()](RequestTracker& tracker)
        {
            api->getFileVersionsOption(&tracker);
        },
        [api = megaApi[0].get()](bool newValue, RequestTracker& tracker)
        {
            api->setFileVersionsOption(newValue, &tracker);
        },
        {true, false}));
}

/**
 * @brief SdkTestUserAttribute.ContactLinkVerification
 */
TEST_F(SdkTestUserAttribute, ContactLinkVerification)
{
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    int at = MegaApi::USER_ATTR_CONTACT_LINK_VERIFICATION;

    ASSERT_NO_FATAL_FAILURE(testStaticInformation(at, "^!clv", "CONTACT_LINK_VERIFICATION"));

    // test generic interfaces
    ASSERT_NO_FATAL_FAILURE(testValue<bool>(
        at,
        [api = megaApi[0].get(), at](RequestTracker& tracker)
        {
            api->getUserAttribute(at, &tracker);
        },
        [api = megaApi[0].get(), at](bool newValue, RequestTracker& tracker)
        {
            std::string v{std::to_string(newValue)};
            api->setUserAttribute(at, v.c_str(), &tracker);
        },
        {true, false}));

    // test dedicated interfaces
    ASSERT_NO_FATAL_FAILURE(testValue<bool>(
        at,
        [api = megaApi[0].get()](RequestTracker& tracker)
        {
            api->getContactLinksOption(&tracker);
        },
        [api = megaApi[0].get()](bool newValue, RequestTracker& tracker)
        {
            api->setContactLinksOption(newValue, &tracker);
        },
        {true, false}));
}

/**
 * @brief SdkTestUserAttribute.VisibleWelcomeDialog
 */
TEST_F(SdkTestUserAttribute, VisibleWelcomeDialog)
{
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    int at = MegaApi::USER_ATTR_VISIBLE_WELCOME_DIALOG;

    ASSERT_NO_FATAL_FAILURE(testStaticInformation(at, "^!weldlg", "VISIBLE_WELCOME_DIALOG"));

    // test generic interfaces
    ASSERT_NO_FATAL_FAILURE(testValue<bool>(
        at,
        [api = megaApi[0].get(), at](RequestTracker& tracker)
        {
            api->getUserAttribute(at, &tracker);
        },
        [api = megaApi[0].get(), at](bool newValue, RequestTracker& tracker)
        {
            std::string v{std::to_string(newValue)};
            api->setUserAttribute(at, v.c_str(), &tracker);
        },
        {true, false}));

    // test dedicated interfaces
    ASSERT_NO_FATAL_FAILURE(testValue<bool>(
        at,
        [api = megaApi[0].get()](RequestTracker& tracker)
        {
            api->getVisibleWelcomeDialog(&tracker);
        },
        [api = megaApi[0].get()](bool newValue, RequestTracker& tracker)
        {
            api->setVisibleWelcomeDialog(newValue, &tracker);
        },
        {true, false}));
}

/**
 * @brief SdkTestUserAttribute.VisibleTermsOfService
 */
TEST_F(SdkTestUserAttribute, VisibleTermsOfService)
{
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    int at = MegaApi::USER_ATTR_VISIBLE_TERMS_OF_SERVICE;

    ASSERT_NO_FATAL_FAILURE(testStaticInformation(at, "^!tos", "VISIBLE_TERMS_OF_SERVICE"));

    // test generic interfaces
    ASSERT_NO_FATAL_FAILURE(testValue<bool>(
        at,
        [api = megaApi[0].get(), at](RequestTracker& tracker)
        {
            api->getUserAttribute(at, &tracker);
        },
        [api = megaApi[0].get(), at](int newValue, RequestTracker& tracker)
        {
            std::string v{std::to_string(newValue)};
            api->setUserAttribute(at, v.c_str(), &tracker);
        },
        {true, false}));

    // test dedicated interfaces
    ASSERT_NO_FATAL_FAILURE(testValue<bool>(
        at,
        [api = megaApi[0].get()](RequestTracker& tracker)
        {
            api->getVisibleTermsOfService(&tracker);
        },
        [api = megaApi[0].get()](bool newValue, RequestTracker& tracker)
        {
            api->setVisibleTermsOfService(newValue, &tracker);
        },
        {true, false}));
}

/**
 * @brief SdkTestUserAttribute.CookieSettings
 */
TEST_F(SdkTestUserAttribute, CookieSettings)
{
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    int at = MegaApi::USER_ATTR_COOKIE_SETTINGS;

    ASSERT_NO_FATAL_FAILURE(testStaticInformation(at, "^!csp", "COOKIE_SETTINGS"));

    // test generic getUserAttribute (cannot be set using setUserAttribute)
    ASSERT_NO_FATAL_FAILURE(testGenericGet(at, {API_OK, API_ENOENT}));

    // test dedicated interfaces
    ASSERT_NO_FATAL_FAILURE(testValue<int>(
        at,
        [api = megaApi[0].get()](RequestTracker& tracker)
        {
            api->getCookieSettings(&tracker);
        },
        [api = megaApi[0].get()](int newValue, RequestTracker& tracker)
        {
            api->setCookieSettings(newValue, &tracker);
        },
        {1, 0}));
}

/**
 * @brief SdkTestUserAttribute.NoCallKit
 */
TEST_F(SdkTestUserAttribute, NoCallKit)
{
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    int at = MegaApi::USER_ATTR_NO_CALLKIT;

    ASSERT_NO_FATAL_FAILURE(testStaticInformation(at, "^!nokit", "NO_CALLKIT"));

    // test generic interfaces
    ASSERT_NO_FATAL_FAILURE(testValue<int>(
        at,
        [api = megaApi[0].get(), at](RequestTracker& tracker)
        {
            api->getUserAttribute(at, &tracker);
        },
        [api = megaApi[0].get(), at](int newValue, RequestTracker& tracker)
        {
            std::string v{std::to_string(newValue)};
            api->setUserAttribute(at, v.c_str(), &tracker);
        },
        {1, 0}));
}

/**
 * @brief SdkTestUserAttribute.RubbishTime
 */
TEST_F(SdkTestUserAttribute, RubbishTime)
{
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    int at = MegaApi::USER_ATTR_RUBBISH_TIME;

    ASSERT_NO_FATAL_FAILURE(testStaticInformation(at, "^!rubbishtime", "RUBBISH_TIME"));

    // test generic interfaces
    ASSERT_NO_FATAL_FAILURE(testValue<int>(
        at,
        [api = megaApi[0].get(), at](RequestTracker& tracker)
        {
            api->getUserAttribute(at, &tracker);
        },
        [api = megaApi[0].get(), at](int newValue, RequestTracker& tracker)
        {
            std::string v{std::to_string(newValue)};
            api->setUserAttribute(at, v.c_str(), &tracker);
        },
        {1, 2}));

    // test dedicated interfaces
    ASSERT_NO_FATAL_FAILURE(testValue<int>(
        at,
        [api = megaApi[0].get()](RequestTracker& tracker)
        {
            api->getRubbishBinAutopurgePeriod(&tracker);
        },
        [api = megaApi[0].get()](int newValue, RequestTracker& tracker)
        {
            api->setRubbishBinAutopurgePeriod(newValue, &tracker);
        },
        {1, 2}));
}

/**
 * @brief SdkTestUserAttribute.LastPSA
 */
TEST_F(SdkTestUserAttribute, LastPSA)
{
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    int at = MegaApi::USER_ATTR_LAST_PSA;

    ASSERT_NO_FATAL_FAILURE(testStaticInformation(at, "^!lastPsa", "LAST_PSA"));

    // test generic interfaces
    ASSERT_NO_FATAL_FAILURE(testValue<int>(
        at,
        [api = megaApi[0].get(), at](RequestTracker& tracker)
        {
            api->getUserAttribute(at, &tracker);
        },
        [api = megaApi[0].get(), at](int newValue, RequestTracker& tracker)
        {
            std::string v{std::to_string(newValue)};
            api->setUserAttribute(at, v.c_str(), &tracker);
        },
        {1, 2}));

    // test dedicated interfaces
    ASSERT_NO_FATAL_FAILURE(testValue<int>(
        at,
        [api = megaApi[0].get(), at](RequestTracker& tracker)
        {
            // has no dedicated interface for getting it
            api->getUserAttribute(at, &tracker);
        },
        [api = megaApi[0].get()](int newValue, RequestTracker& tracker)
        {
            api->setPSA(newValue, &tracker);
        },
        {1, 0}));
}

/**
 * @brief SdkTestUserAttribute.StorageState
 */
TEST_F(SdkTestUserAttribute, StorageState)
{
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    int at = MegaApi::USER_ATTR_STORAGE_STATE;

    ASSERT_NO_FATAL_FAILURE(testStaticInformation(at, "^!usl", "STORAGE_STATE"));

    // test generic getUserAttribute (cannot be set using setUserAttribute)
    ASSERT_NO_FATAL_FAILURE(testGenericGet(at, {API_OK, API_ENOENT}));
}

/**
 * @brief SdkTestUserAttribute.CameraUploadsFolder
 */
TEST_F(SdkTestUserAttribute, CameraUploadsFolder)
{
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    int at = MegaApi::USER_ATTR_CAMERA_UPLOADS_FOLDER;

    ASSERT_NO_FATAL_FAILURE(testStaticInformation(at, "*!cam", "CAMERA_UPLOADS_FOLDER"));

    // test generic getUserAttribute (cannot be set using setUserAttribute)
    ASSERT_NO_FATAL_FAILURE(testGenericGet(at, {API_OK, API_ENOENT}));

    // create 2 folders
    std::unique_ptr<MegaNode> rootnode{megaApi[0]->getRootNode()};
    RequestTracker folderTracker1(megaApi[0].get());
    megaApi[0]->createFolder("TestCameraFolder1", rootnode.get(), &folderTracker1);
    RequestTracker folderTracker2(megaApi[0].get());
    megaApi[0]->createFolder("TestCameraFolder2", rootnode.get(), &folderTracker2);
    ASSERT_EQ(folderTracker1.waitForResult(), API_OK);
    ASSERT_EQ(folderTracker2.waitForResult(), API_OK);

    // test dedicated interfaces
    ASSERT_NO_FATAL_FAILURE(testValue<MegaHandle>(
        at,
        [api = megaApi[0].get()](RequestTracker& tracker)
        {
            api->getCameraUploadsFolder(&tracker);
        },
        [api = megaApi[0].get()](MegaHandle newValue, RequestTracker& tracker)
        {
            api->setCameraUploadsFolder(newValue, &tracker);
        },
        {folderTracker1.getNodeHandle(), folderTracker2.getNodeHandle()}));
}

/**
 * @brief SdkTestUserAttribute.MyChatFilesFolder
 */
TEST_F(SdkTestUserAttribute, MyChatFilesFolder)
{
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    int at = MegaApi::USER_ATTR_MY_CHAT_FILES_FOLDER;

    ASSERT_NO_FATAL_FAILURE(testStaticInformation(at, "*!cf", "MY_CHAT_FILES_FOLDER"));

    // test generic getUserAttribute (cannot be set using setUserAttribute)
    ASSERT_NO_FATAL_FAILURE(testGenericGet(at, {API_OK, API_ENOENT}));

    // create 2 folders
    std::unique_ptr<MegaNode> rootnode{megaApi[0]->getRootNode()};
    RequestTracker folderTracker1(megaApi[0].get());
    megaApi[0]->createFolder("TestChatFilesFolder1", rootnode.get(), &folderTracker1);
    RequestTracker folderTracker2(megaApi[0].get());
    megaApi[0]->createFolder("TestChatFilesFolder2", rootnode.get(), &folderTracker2);
    ASSERT_EQ(folderTracker1.waitForResult(), API_OK);
    ASSERT_EQ(folderTracker2.waitForResult(), API_OK);

    // test dedicated interfaces
    ASSERT_NO_FATAL_FAILURE(testValue<MegaHandle>(
        at,
        [api = megaApi[0].get()](RequestTracker& tracker)
        {
            api->getMyChatFilesFolder(&tracker);
        },
        [api = megaApi[0].get()](MegaHandle newValue, RequestTracker& tracker)
        {
            api->setMyChatFilesFolder(newValue, &tracker);
        },
        {folderTracker1.getNodeHandle(), folderTracker2.getNodeHandle()}));
}

/**
 * @brief SdkTestUserAttribute.LastInteraction
 */
TEST_F(SdkTestUserAttribute, LastInteraction)
{
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    int at = MegaApi::USER_ATTR_LAST_INTERACTION;

    ASSERT_NO_FATAL_FAILURE(testStaticInformation(at, "*!lstint", "LAST_INT"));

    std::string v1{"0:1710410495"};
    std::unique_ptr<char[]> v1b64(megaApi[0]->binaryToBase64(v1.c_str(), v1.size()));
    std::shared_ptr<MegaStringMap> alternative1{MegaStringMap::createInstance()};
    alternative1->set("BODjmzqzD3g", v1b64.get());
    std::string v2{"0:1710410496"};
    std::unique_ptr<char[]> v2b64(megaApi[0]->binaryToBase64(v2.c_str(), v2.size()));
    std::shared_ptr<MegaStringMap> alternative2{MegaStringMap::createInstance()};
    alternative2->set("BODjmzqzD3g", v2b64.get());

    // test generic interfaces
    ASSERT_NO_FATAL_FAILURE(testRawPointer<MegaStringMap>(
        at,
        [api = megaApi[0].get(), at](RequestTracker& tracker)
        {
            api->getUserAttribute(at, &tracker);
        },
        [api = megaApi[0].get(), at](MegaStringMap* newValue, RequestTracker& tracker)
        {
            api->setUserAttribute(at, newValue, &tracker);
        },
        {alternative1, alternative2}));
}

/**
 * @brief SdkTestUserAttribute.PushSettings
 */
TEST_F(SdkTestUserAttribute, PushSettings)
{
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    int at = MegaApi::USER_ATTR_PUSH_SETTINGS;

    ASSERT_NO_FATAL_FAILURE(testStaticInformation(at, "^!ps", "PUSH_SETTINGS"));

    // test generic getUserAttribute (cannot be set using setUserAttribute)
    ASSERT_NO_FATAL_FAILURE(testRawPointer<MegaPushNotificationSettings>(
        at,
        [api = megaApi[0].get(), at](RequestTracker& tracker)
        {
            api->getUserAttribute(at, &tracker);
        }));

    // test dedicated interfaces
    std::shared_ptr<MegaPushNotificationSettings> alternative1{
        MegaPushNotificationSettings::createInstance()};
    std::shared_ptr<MegaPushNotificationSettings> alternative2{
        MegaPushNotificationSettings::createInstance()};
    alternative2->enableContacts(!alternative1->isContactsEnabled());
    ASSERT_NO_FATAL_FAILURE(testRawPointer<MegaPushNotificationSettings>(
        at,
        [api = megaApi[0].get()](RequestTracker& tracker)
        {
            api->getPushNotificationSettings(&tracker);
        },
        [api = megaApi[0].get()](MegaPushNotificationSettings* newValue, RequestTracker& tracker)
        {
            api->setPushNotificationSettings(newValue, &tracker);
        },
        {alternative1, alternative2}));
}

/**
 * @brief SdkTestUserAttribute.WelcomPdfCopied
 */
TEST_F(SdkTestUserAttribute, WelcomPdfCopied)
{
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    int at = MegaApi::USER_ATTR_WELCOME_PDF_COPIED;

    ASSERT_NO_FATAL_FAILURE(testStaticInformation(at, "^!welpdf", "WELCOME_PDF_COPIED"));

    // test generic interfaces
    ASSERT_NO_FATAL_FAILURE(testValue<bool>(
        at,
        [api = megaApi[0].get(), at](RequestTracker& tracker)
        {
            api->getUserAttribute(at, &tracker);
        },
        [api = megaApi[0].get(), at](int newValue, RequestTracker& tracker)
        {
            std::string v{std::to_string(newValue)};
            api->setUserAttribute(at, v.c_str(), &tracker);
        },
        {true, false}));

    // test dedicated interfaces
    ASSERT_NO_FATAL_FAILURE(testValue<bool>(
        at,
        [api = megaApi[0].get()](RequestTracker& tracker)
        {
            api->getWelcomePdfCopied(&tracker);
        },
        [api = megaApi[0].get()](bool newValue, RequestTracker& tracker)
        {
            api->setWelcomePdfCopied(newValue, &tracker);
        },
        {true, false}));
}

}
