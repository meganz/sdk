/**
* @file pendingcontactrequest.cpp
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

#include "mega/pendingcontactrequest.h"
#include "mega/megaclient.h"

namespace mega {

PendingContactRequest::PendingContactRequest(const handle id, const char *oemail, const char *temail, const m_time_t ts, const m_time_t uts, const char *msg)
{
    this.id = id;
    if (oemail) {
        this.orginatoremail = oemail;
    }
    if (temail) {
        this.targetemail = temail;
    }
    this.ts = ts;
    this.uts = uts;
    if (msg) {
        this.msg = msg;
    }
}

bool PendingContactRequest::serialize(string *d)
{
    unsigned char l;

    d->append((char*)&id, sizeof id);

    l = oemail.size();
    d->append((char*)&l, sizeof l);
    d->append(oemail.c_str(), l);

    l = temail.size();
    d->append((char*)&l, sizeof l);
    d->append(temail.c_str(), l);

    d->append((char*)&ts, sizeof ts);
    d->append((char*)&uts, sizeof uts);

    l = msg.size();
    d->append((char*)&l, sizeof l);
    d->append(msg.c_str(), l);

    return true;
}

PendingContactRequest* PendingContactRequest::unserialize(class MegaClient *client, string *d)
{
    const handle id;
    const char *oemail;
    const char *temail;
    const m_time_t ts;
    const m_time_t uts;
    const string msg;

    const char* ptr = d->data();
    const char* end = ptr + d->size();
    unsigned char l;

    id = MemAccess::get<handle>(ptr);
    ptr += sizeof id;

    l = *ptr++;
    if (l)
    {
        oemail.assign(ptr, l);
    }
    ptr += l;

    l = *ptr++;
    if (l)
    {
        temail.assign(ptr, l);
    }
    ptr += l;

    ts = MemAccess::get<m_time_t>(ptr);
    ptr += sizeof ts;

    uts = MemAccess::get<m_time_t>(ptr);
    ptr += sizeof uts;

    l = *ptr++;
    if (l)
    {
        msg.assign(ptr, l);
    }
    ptr += l;

    return new PendingContactRequest(id, oemail, temail, ts, uts, msg);
}

} //namespace
