/**
 * @file
 * @brief Definition for the SdkTestPasswordManager class, designed to be used as base class for
 * test fixtures involving password manager use cases.
 */

#ifndef INCLUDE_PASSWORDMANAGER_SDKTESTPASSWORDMANAGER_H_
#define INCLUDE_PASSWORDMANAGER_SDKTESTPASSWORDMANAGER_H_

#include "SdkTest_test.h"

/**
 * @class SdkTestPasswordManager
 * @brief Base class to be used in password manager test cases.
 *
 * Sets up a password manager account with a valid base node.
 */
class SdkTestPasswordManager: public SdkTest
{
public:
    static constexpr auto MAX_TIMEOUT{3min};

    void SetUp() override;

    handle getBaseHandle() const
    {
        return mPWMBaseNodeHandle;
    }

    std::unique_ptr<MegaNode> getBaseNode() const
    {
        return std::unique_ptr<MegaNode>(mApi->getNodeByHandle(getBaseHandle()));
    }

protected:
    handle mPWMBaseNodeHandle{UNDEF};
    MegaApi* mApi{};

    void initPasswordManagerBase();
};

#endif // INCLUDE_PASSWORDMANAGER_SDKTESTPASSWORDMANAGER_H_
