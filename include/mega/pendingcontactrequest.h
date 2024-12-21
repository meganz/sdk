/**
* @file mega/pendingcontactrequest.h
* @brief Class for manipulating pending contact requests
*
* (c) 2013-2014 by Mega Limited, Wellsford, New Zealand
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

#ifndef MEGA_PENDING_CONTACT_REQUEST_H
#define MEGA_PENDING_CONTACT_REQUEST_H 1

#include "mega/utils.h"

namespace mega {

// pending contact request
struct MEGA_API PendingContactRequest : public Cacheable
{
    // id of the request
    handle id;

    // e-mail of request creator
    string originatoremail;

    // e-mail of the recipient (empty if to us)
    string targetemail;

    // creation timestamp
    m_time_t ts;

    // last update timestamp
    m_time_t uts;

    // message from originator
    string msg;

    // flag for ease of use identifying direction
    bool isoutgoing;

    // flag to notify that an incoming contact request is being autoaccepted
    bool autoaccepted;

    struct {
        bool accepted : 1;
        bool denied : 1;
        bool ignored : 1;
        bool deleted : 1;
        bool reminded : 1;
    } changed;

    bool serialize(string*) const override;
    static PendingContactRequest* unserialize(string*);

    PendingContactRequest(const handle id, const char *oemail, const char *temail, const m_time_t ts, const m_time_t uts, const char *msg, bool outgoing);
    PendingContactRequest(const handle id); // for dummy requests during gettree/fetchnodes

    void update(const char* oemail,
                const char* temail,
                const m_time_t newTs,
                const m_time_t newUts,
                const char* newMessage,
                bool outgoing);

    bool removed();
};

} //namespace

#endif

