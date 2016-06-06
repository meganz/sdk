/**
 * @file mega/user.h
 * @brief Class for manipulating user / contact data
 *
 * (c) 2013-2014 by Mega Limited, Auckland, New Zealand
 *
 * This file is part of the MEGA SDK - Client Access Engine.
 *
 * Applications using the MEGA API must present a valid application key
 * and comply with the the rules set forth in the Terms of Service.
 *
 * The MEGA SDK is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * @copyright Simplified (2-clause) BSD License.
 *
 * You should have received a copy of the license along with this
 * program.
 */

#ifndef MEGA_USER_H
#define MEGA_USER_H 1

#include "attrmap.h"

namespace mega {
// user/contact
struct MEGA_API User : public Cachable
{
    // user handle
    handle userhandle;

    // string identifier for API requests (either e-mail address or ASCII user
    // handle)
    string uid;

    // e-mail address
    string email;

    // visibility status
    visibility_t show;

    // shares by this user
    handle_set sharing;

    // contact establishment timestamp
    m_time_t ctime;

    struct
    {
        bool keyring : 1;   // private keys
        bool authring : 1;  // authentication information of the contact
        bool lstint : 1;    // last interaction with the contact
        bool puEd255 : 1;   // public key for Ed25519
        bool puCu255 : 1;   // public key for Cu25519
        bool sigPubk : 1;   // signature for RSA public key
        bool sigCu255 : 1;  // signature for Cu255199 public key
        bool avatar : 1;    // avatar image
        bool firstname : 1;
        bool lastname : 1;
        bool country : 1;
        bool birthday : 1;  // wraps status of birthday, birthmonth, birthyear
        bool email : 1;
    } changed;

    // user's public key
    AsymmCipher pubk;
    int pubkrequested;

    // actions to take after arrival of the public key
    deque<class PubKeyAction*> pkrs;

private:
    // persistent attributes (keyring, firstname...)
    string_map attrs;

    // version of each attribute
    string_map attrsv;

public:
    void set(visibility_t, m_time_t);

    bool serialize(string*);
    static User* unserialize(class MegaClient *, string*);

    // attribute methods: set/get/invalidate...
    void setattr(string *an, string *av, string *v);
    const string *getattr(string an);
    const string *getattrversion(string an);
    void invalidateattr(string an);
    bool isattrvalid(string an);

    bool setChanged(const char*);

    User(const char* = NULL);
};
} // namespace

#endif
