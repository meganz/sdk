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

PendingContactRequest::PendingContactRequest(const handle id)
{
    this->id = id;
    this->ts = 0;
    this->uts = 0;
    this->isoutgoing = true;
    this->autoaccepted = false;

    memset(&changed, 0, sizeof changed);
}

PendingContactRequest::PendingContactRequest(const handle id,const char *oemail, const char *temail, const m_time_t ts, const m_time_t uts, const char *msg, bool outgoing)
{
    this->id = id;
    this->targetemail = "";
    this->autoaccepted = false;
    this->update(oemail, temail, ts, uts, msg, outgoing);

    memset(&changed, 0, sizeof changed);
}

void PendingContactRequest::update(const char *oemail, const char *temail, const m_time_t ts, const m_time_t uts, const char *msg, bool outgoing)
{
    if (oemail)
    {
        JSON::copystring(&(this->originatoremail), oemail);
    }
    if (temail)
    {
        JSON::copystring(&(this->targetemail), temail);
    }
    this->ts = ts;
    this->uts = uts;
    if (msg)
    {
        JSON::copystring(&(this->msg), msg);
    }

    this->isoutgoing = outgoing;
}

bool PendingContactRequest::removed()
{
    return changed.accepted || changed.denied || changed.ignored || changed.deleted;
}

bool PendingContactRequest::serialize(string *d) const
{
    unsigned char l;

    d->append((char*)&id, sizeof id);

    l = (unsigned char)originatoremail.size();
    d->append((char*)&l, sizeof l);
    d->append(originatoremail.c_str(), l);

    l = (unsigned char)targetemail.size();
    d->append((char*)&l, sizeof l);
    d->append(targetemail.c_str(), l);

    d->append((char*)&ts, sizeof ts);
    d->append((char*)&uts, sizeof uts);

    l = (unsigned char)msg.size();
    d->append((char*)&l, sizeof l);
    d->append(msg.c_str(), l);

    d->append((char*)&isoutgoing, sizeof isoutgoing);

    return true;
}

PendingContactRequest* PendingContactRequest::unserialize(string *d)
{
    handle id;
    string oemail;
    string temail;
    m_time_t ts;
    m_time_t uts;
    string msg;
    bool isoutgoing = false;

    const char* ptr = d->data();
    const char* end = ptr + d->size();
    unsigned char l;

    if (ptr + sizeof id + sizeof l > end)
    {
        return NULL;
    }

    id = MemAccess::get<handle>(ptr);
    ptr += sizeof id;

    l = static_cast<unsigned char>(*ptr++);
    if (ptr + l + sizeof l > end)
    {
        return NULL;
    }

    oemail.assign(ptr, l);
    ptr += l;

    l = static_cast<unsigned char>(*ptr++);
    if (ptr + l + sizeof ts + sizeof uts + sizeof l > end)
    {
        return NULL;
    }

    temail.assign(ptr, l);
    ptr += l;

    ts = MemAccess::get<m_time_t>(ptr);
    ptr += sizeof ts;

    uts = MemAccess::get<m_time_t>(ptr);
    ptr += sizeof uts;

    l = static_cast<unsigned char>(*ptr++);
    if (ptr + l > end)
    // should be ptr+l+sizeof(isoutgoing), but legacy code writes 0 bytes when false
    {
        return NULL;
    }

    msg.assign(ptr, l);
    ptr += l;

    if (ptr == end) // legacy bug writes 0 bytes for incoming PCRs
    {
        isoutgoing = false;
    }
    else if (ptr + sizeof isoutgoing == end)
    {
        isoutgoing = MemAccess::get<bool>(ptr);
        ptr += sizeof isoutgoing;
    }

    if (ptr == end)
    {
        return new PendingContactRequest(id, oemail.c_str(), temail.c_str(), ts, uts, msg.c_str(), isoutgoing);
    }
    else
    {
        return NULL;
    }
}

} //namespace
