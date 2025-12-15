#ifndef SDK_TEST_SHARE_H
#define SDK_TEST_SHARE_H

#include "SdkTest_test.h"

class SdkTestShare: public virtual SdkTest
{
protected:
    struct Party
    {
        unsigned apiIndex;
        bool wait; // wait for response
    };

    void createShareAtoB(MegaNode* node,
                         const Party& partyA,
                         const Party& partyB,
                         int accessType = MegaShare::ACCESS_READWRITE);

    // Use mApi[0] as party A and mApi[1] as party B
    void createShareAtoB(MegaNode* node,
                         bool waitForA = true,
                         bool waitForB = true,
                         int accessType = MegaShare::ACCESS_READWRITE);

    // Remove a share ensuring node changes are notified.
    void removeShareAtoB(MegaNode* node, unsigned apiIndexA, unsigned apiIndexB);

    // Use mApi[0] and mApi[1]
    void removeShareAtoB(MegaNode* node);

    // Reset credential between two accounts if contact found
    void resetCredentialsIfContactFound(const unsigned i, const unsigned j);

    // Reset credential between two accounts
    void resetCredential(unsigned apiIndexA, unsigned apiIndexB);

    // Verify contact credentials between two accounts.
    void verifyContactCredentials(unsigned apiIndexA, unsigned apiIndexB);

    void addContactsAndVerifyCredential(unsigned apiIndexA, unsigned apiIndexB);

    std::pair<MegaHandle, std::unique_ptr<MegaNode>> createFolder(unsigned int apiIndex,
                                                                  const char* name,
                                                                  MegaNode* parent);
};

#endif // SDK_TEST_SHARE_H
