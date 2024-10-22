/**
 * @file usernotifications.cpp
 * @brief additional megaclient code for user notifications
 *
 * (c) 2013-2018 by Mega Limited, Auckland, New Zealand
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

#include "mega.h"
#include "mega/megaclient.h"
#include "mega/useralerts.h"

#include <utility>

using std::to_string;
using std::make_pair;

namespace mega {

UserAlertRaw::UserAlertRaw()
    : t(0)
{
}

JSON UserAlertRaw::field(nameid nid) const
{
    map<nameid, string>::const_iterator i = fields.find(nid);
    JSON j;
    j.pos = i == fields.end() ? NULL : i->second.c_str();
    return j;
}

bool UserAlertRaw::has(nameid nid) const
{
    JSON j = field(nid);
    return j.pos != NULL;
}

int UserAlertRaw::getint(nameid nid, int default_value) const
{
    JSON j = field(nid);
    return j.pos && j.isnumeric() ? int(j.getint()) : default_value;
}

int64_t UserAlertRaw::getint64(nameid nid, int64_t default_value) const
{
    JSON j = field(nid);
    return j.pos && j.isnumeric() ? j.getint() : default_value;
}

handle UserAlertRaw::gethandle(nameid nid, int handlesize, handle default_value) const
{
    JSON j = field(nid);
    byte buf[9] = { 0 };
    return (j.pos && handlesize == Base64::atob(j.pos, buf, sizeof(buf))) ? MemAccess::get<handle>((const char*)buf) : default_value;
}

nameid UserAlertRaw::getnameid(nameid nid, nameid default_value) const
{
    JSON j = field(nid);
    nameid id = 0;
    while (*j.pos)
    {
        id = (id << 8) + static_cast<unsigned char>(*j.pos++);
    }

    return id ? id : default_value;
}

string UserAlertRaw::getstring(nameid nid, const char* default_value) const
{
    JSON j = field(nid);
    return j.pos ? j.pos : default_value;
}

bool UserAlertRaw::gethandletypearray(nameid nid, vector<handletype>& v) const
{
    JSON j = field(nid);
    if (j.pos && j.enterarray())
    {
        for (;;)
        {
            if (j.enterobject())
            {
                handletype ht;
                ht.h = UNDEF;
                ht.t = -1;
                bool fields = true;
                while (fields)
                {
                    switch (j.getnameid())
                    {
                    case 'h':
                        ht.h = j.gethandle(MegaClient::NODEHANDLE);
                        break;
                    case 't':
                        ht.t = int(j.getint());
                        break;
                    case EOO:
                        fields = false;
                        break;
                    default:
                        j.storeobject(NULL);
                    }
                }
                v.push_back(ht);
                j.leaveobject();
            }
            else
            {
                break;
            }
        }
        j.leavearray();
        return true;
    }
    return false;
}

bool UserAlertRaw::getstringarray(nameid nid, vector<string>& v) const
{
    JSON j = field(nid);
    if (j.pos && j.enterarray())
    {
        for (;;)
        {
            string s;
            if (j.storeobject(&s))
            {
                v.push_back(s);
            }
            else
            {
                break;
            }
        }
        j.leavearray();
    }
    return false;
}

UserAlertFlags::UserAlertFlags()
    : cloud_enabled(true)
    , contacts_enabled(true)
    , cloud_newfiles(true)
    , cloud_newshare(true)
    , cloud_delshare(true)
    , contacts_fcrin(true)
    , contacts_fcrdel(true)
    , contacts_fcracpt(true)
{
}

UserAlertPendingContact::UserAlertPendingContact()
    : u(0)
{
}



UserAlert::Base::Base(UserAlertRaw& un, unsigned int cid)
{
    id = cid;
    type = un.t;
    m_time_t timeDelta = un.getint64(MAKENAMEID2('t', 'd'), 0);
    pst.timestamp = m_time() - timeDelta;
    pst.userHandle = un.gethandle(name_id::u, MegaClient::USERHANDLE, UNDEF);
    pst.userEmail = un.getstring('m', "");

    tag = -1;
}

UserAlert::Base::Base(nameid t, handle uh, const string& email, m_time_t ts, unsigned int cid)
{
    id = cid;
    type = t;
    pst.userHandle = uh;
    pst.userEmail = email;
    pst.timestamp = ts;
    tag = -1;
}

UserAlert::Base::~Base()
{
}

void UserAlert::Base::updateEmail(MegaClient* mc)
{
    if (User* u = mc->finduser(user()))
    {
        pst.userEmail = u->email;
    }
}

bool UserAlert::Base::checkprovisional(handle , MegaClient*)
{
    return true;
}

void UserAlert::Base::text(string& header, string& title, MegaClient* mc)
{
    // should be overridden
    updateEmail(mc);
    ostringstream s;
    s << "notification: type " << type << " time " << ts() << " user " << user() << " seen " << seen();
    title =  s.str();
    header = email();
}

bool UserAlert::Base::serialize(string* d) const
{
    CacheableWriter w(*d);
    w.serializecompressedu64(type); // this will be unserialized in UserAlerts::unserializeAlert()
    w.serializecompressedi64(ts());
    w.serializehandle(user());
    w.serializestring(email());
    w.serializebool(relevant());
    w.serializebool(seen());

    return true;
}

unique_ptr<UserAlert::Base::Persistent> UserAlert::Base::readBase(CacheableReader& r)
{
    auto p = std::make_unique<Persistent>();
    if (r.unserializecompressedi64(p->timestamp)
        && r.unserializehandle(p->userHandle)
        && r.unserializestring(p->userEmail)
        && r.unserializebool(p->relevant)
        && r.unserializebool(p->seen))
    {
        return p;
    }

    return nullptr;
}

unique_ptr<UserAlert::Base::Persistent> UserAlert::Base::unserialize(std::string* d)
{
    CacheableReader r(*d);
    if (auto p = readBase(r))
    {
        r.eraseused(*d);
        return p;
    }

    return nullptr;
}

UserAlert::IncomingPendingContact::IncomingPendingContact(UserAlertRaw& un, unsigned int id)
    : Base(un, id)
{
    mPcrHandle = un.gethandle('p', MegaClient::PCRHANDLE, UNDEF);
    pst.userHandle = mPcrHandle;    // for backwards compatibility, due to legacy bug

    m_time_t dts = un.getint64(MAKENAMEID3('d', 't', 's'), 0);
    m_time_t rts = un.getint64(MAKENAMEID3('r', 't', 's'), 0);
    initTs(dts, rts);
}

UserAlert::IncomingPendingContact::IncomingPendingContact(m_time_t dts, m_time_t rts, handle p, const string& email, m_time_t timestamp, unsigned int id)
    : Base(UserAlert::type_ipc, p, email, timestamp, id)
    // passing PCR's handle as the user's handle for backwards compatibility, due to legacy bug
{
    mPcrHandle = p;

    initTs(dts, rts);
}

void UserAlert::IncomingPendingContact::initTs(m_time_t dts, m_time_t rts)
{
    requestWasDeleted = dts != 0;
    requestWasReminded = rts != 0;

    if (requestWasDeleted)       pst.timestamp = dts;
    else if (requestWasReminded) pst.timestamp = rts;
}

void UserAlert::IncomingPendingContact::text(string& header, string& title, MegaClient* mc)
{
    updateEmail(mc);
    if (requestWasDeleted)
    {
        title = "Cancelled their contact request"; // 7151
    }
    else if (requestWasReminded)
    {
        title = "Reminder: You have a contact request"; // 7150
    }
    else
    {
        title = "Sent you a contact request"; // 5851
    }
    header = email();
}

bool UserAlert::IncomingPendingContact::serialize(string* d) const
{
    Base::serialize(d);
    CacheableWriter w(*d);
    w.serializehandle(mPcrHandle);
    w.serializebool(requestWasDeleted);
    w.serializebool(requestWasReminded);
    w.serializeexpansionflags();

    return true;
}

UserAlert::IncomingPendingContact* UserAlert::IncomingPendingContact::unserialize(string* d, unsigned id)
{
    auto p = Base::unserialize(d);
    if (!p)
    {
        return nullptr;
    }

    handle pcrHandle = 0;
    bool deleted = false;
    bool reminded = false;
    unsigned char expF[8];

    CacheableReader r(*d);
    if (r.unserializehandle(pcrHandle) &&
        r.unserializebool(deleted) &&
        r.unserializebool(reminded) &&
        r.unserializeexpansionflags(expF, 0))
    {
        auto* ipc = new IncomingPendingContact(0, 0, p->userHandle, p->userEmail, p->timestamp, id);
        ipc->mPcrHandle = pcrHandle;
        ipc->requestWasDeleted = deleted;
        ipc->requestWasReminded = reminded;
        ipc->setRelevant(p->relevant);
        ipc->setSeen(p->seen);
        return ipc;
    }

    return nullptr;
}

UserAlert::ContactChange::ContactChange(UserAlertRaw& un, unsigned int id)
    : Base(un, id)
{
    action = un.getint(name_id::c, -1);
    pst.relevant = action >= 0 && action < 4;
    assert(action >= 0 && action < 4);
}

UserAlert::ContactChange::ContactChange(int c, handle uh, const string& email, m_time_t timestamp, unsigned int id)
    : Base(UserAlert::type_c, uh, email, timestamp, id)
{
    action = c;
    assert(action >= 0 && action < 4);
}

bool UserAlert::ContactChange::checkprovisional(handle ou, MegaClient* mc)
{
    return ou != mc->me;
}

void UserAlert::ContactChange::text(string& header, string& title, MegaClient* mc)
{
    updateEmail(mc);

    if (action == 0)
    {
        title = "Deleted you as a contact"; // 7146
    }
    else if (action == 1)
    {
        title = "Contact relationship established"; // 7145
    }
    else if (action == 2)
    {
        title = "Account has been deleted/deactivated"; // 7144
    }
    else if (action == 3)
    {
        title = "Blocked you as a contact"; //7143
    }
    header = email();
}

bool UserAlert::ContactChange::serialize(string* d) const
{
    Base::serialize(d);
    CacheableWriter w(*d);
    w.serializeu32(action);
    w.serializeexpansionflags();

    return true;
}

UserAlert::ContactChange* UserAlert::ContactChange::unserialize(string* d, unsigned id)
{
    auto p = Base::unserialize(d);
    if (!p)
    {
        return nullptr;
    }

    int act = 0;
    unsigned char expF[8];

    CacheableReader r(*d);
    if (r.unserializeu32(reinterpret_cast<unsigned&>(act)) &&
        r.unserializeexpansionflags(expF, 0))
    {
        auto* cc = new ContactChange(act, p->userHandle, p->userEmail, p->timestamp, id);
        cc->setRelevant(p->relevant);
        cc->setSeen(p->seen);
        return cc;
    }

    return nullptr;
}

UserAlert::UpdatedPendingContactIncoming::UpdatedPendingContactIncoming(UserAlertRaw& un, unsigned int id)
    : Base(un, id)
{
    action = un.getint('s', -1);
    pst.relevant = action >= 1 && action < 4;
}

UserAlert::UpdatedPendingContactIncoming::UpdatedPendingContactIncoming(int s, handle uh, const string& email, m_time_t timestamp, unsigned int id)
    : Base(type_upci, uh, email, timestamp, id)
    , action(s)
{
}

void UserAlert::UpdatedPendingContactIncoming::text(string& header, string& title, MegaClient* mc)
{
    updateEmail(mc);
    if (action == 1)
    {
        title = "You ignored a contact request"; // 7149
    }
    else if (action == 2)
    {
        title = "You accepted a contact request"; // 7148
    }
    else if (action == 3)
    {
        title = "You denied a contact request"; // 7147
    }
    header = email();
}

bool UserAlert::UpdatedPendingContactIncoming::serialize(string* d) const
{
    Base::serialize(d);
    CacheableWriter w(*d);
    w.serializeu32(action);
    w.serializeexpansionflags();

    return true;
}

UserAlert::UpdatedPendingContactIncoming* UserAlert::UpdatedPendingContactIncoming::unserialize(string* d, unsigned id)
{
    auto p = Base::unserialize(d);
    if (!p)
    {
        return nullptr;
    }

    int act = 0;
    unsigned char expF[8];

    CacheableReader r(*d);
    if (r.unserializeu32(reinterpret_cast<unsigned&>(act)) &&
        r.unserializeexpansionflags(expF, 0))
    {
        auto* upci = new UpdatedPendingContactIncoming(act, p->userHandle, p->userEmail, p->timestamp, id);
        upci->setRelevant(p->relevant);
        upci->setSeen(p->seen);
        return upci;
    }

    return nullptr;
}

UserAlert::UpdatedPendingContactOutgoing::UpdatedPendingContactOutgoing(UserAlertRaw& un, unsigned int id)
    : Base(un, id)
{
    action = un.getint('s', -1);
    pst.relevant = action == 2 || action == 3;
}

UserAlert::UpdatedPendingContactOutgoing::UpdatedPendingContactOutgoing(int s, handle uh, const string& email, m_time_t timestamp, unsigned int id)
    : Base(type_upco, uh, email, timestamp, id)
    , action(s)
{
}

void UserAlert::UpdatedPendingContactOutgoing::text(string& header, string& title, MegaClient* mc)
{
    updateEmail(mc);
    if (action == 2)
    {
        title = "Accepted your contact request"; // 5852
    }
    else if (action == 3)
    {
        title = "Denied your contact request"; // 5853
    }
    header = email();
}

bool UserAlert::UpdatedPendingContactOutgoing::serialize(string* d) const
{
    Base::serialize(d);
    CacheableWriter w(*d);
    w.serializeu32(action);
    w.serializeexpansionflags();

    return true;
}

UserAlert::UpdatedPendingContactOutgoing* UserAlert::UpdatedPendingContactOutgoing::unserialize(string* d, unsigned id)
{
    auto p = Base::unserialize(d);
    if (!p)
    {
        return nullptr;
    }

    int act = 0;
    unsigned char expF[8];

    CacheableReader r(*d);
    if (r.unserializeu32(reinterpret_cast<unsigned&>(act)) &&
        r.unserializeexpansionflags(expF, 0))
    {
        auto* upco = new UpdatedPendingContactOutgoing(act, p->userHandle, p->userEmail, p->timestamp, id);
        upco->setRelevant(p->relevant);
        upco->setSeen(p->seen);
        return upco;
    }

    return nullptr;
}

UserAlert::NewShare::NewShare(UserAlertRaw& un, unsigned int id)
    : Base(un, id)
{
    folderhandle = un.gethandle('n', MegaClient::NODEHANDLE, UNDEF);
}

UserAlert::NewShare::NewShare(handle h, handle uh, const string& email, m_time_t timestamp, unsigned int id)
    : Base(type_share, uh, email, timestamp, id)
{
    folderhandle = h;
}

void UserAlert::NewShare::text(string& header, string& title, MegaClient* mc)
{
    updateEmail(mc);
    if (!email().empty())
    {
        title =  "New shared folder from " + email(); // 824
    }
    else
    {
        title = "New shared folder"; // 825
    }
    header = email();
}

bool UserAlert::NewShare::serialize(string* d) const
{
    Base::serialize(d);
    CacheableWriter w(*d);
    w.serializehandle(folderhandle);
    w.serializeexpansionflags();

    return true;
}

UserAlert::NewShare* UserAlert::NewShare::unserialize(string* d, unsigned id)
{
    auto p = Base::unserialize(d);
    if (!p)
    {
        return nullptr;
    }

    handle h = 0;
    unsigned char expF[8];

    CacheableReader r(*d);
    if (r.unserializehandle(h) &&
        r.unserializeexpansionflags(expF, 0))
    {
        auto* ns = new NewShare(h, p->userHandle, p->userEmail, p->timestamp, id);
        ns->setRelevant(p->relevant);
        ns->setSeen(p->seen);
        return ns;
    }

    return nullptr;
}

UserAlert::DeletedShare::DeletedShare(UserAlertRaw& un, unsigned int id)
    : Base(un, id)
{
    ownerHandle = un.gethandle('o', MegaClient::USERHANDLE, UNDEF);
    folderHandle = un.gethandle('n', MegaClient::NODEHANDLE, UNDEF);
}

UserAlert::DeletedShare::DeletedShare(handle uh, const string& email, handle ownerhandle, handle folderhandle, m_time_t ts, unsigned int id)
    : Base(type_dshare, uh, email, ts, id)
{
    ownerHandle = ownerhandle;
    folderHandle = folderhandle;
}

void UserAlert::DeletedShare::updateEmail(MegaClient* mc)
{
    Base::updateEmail(mc);

    if (std::shared_ptr<Node> n = mc->nodebyhandle(folderHandle))
    {
        folderPath = n->displaypath();
        folderName = n->displayname();
    }
}

void UserAlert::DeletedShare::text(string& header, string& title, MegaClient* mc)
{
    updateEmail(mc);
    ostringstream s;

    if (user() == ownerHandle)
    {
        if (!email().empty())
        {
            s << "Access to folders shared by " << email() << " was removed"; // 7879
        }
        else
        {
            s << "Access to folders was removed"; // 7880
        }
    }
    else
    {
       if (!email().empty())
       {
           s << "User " << email() << " has left the shared folder " << folderName;  //19153
       }
       else
       {
           s << "A user has left the shared folder " << folderName;  //19154
       }
    }
    title = s.str();
    header = email();
}

bool UserAlert::DeletedShare::serialize(string* d) const
{
    Base::serialize(d);
    CacheableWriter w(*d);
    w.serializehandle(folderHandle);
    w.serializestring(folderPath);
    w.serializestring(folderName);
    w.serializehandle(ownerHandle);
    w.serializeexpansionflags();

    return true;
}

UserAlert::DeletedShare* UserAlert::DeletedShare::unserialize(string* d, unsigned id)
{
    auto p = Base::unserialize(d);
    if (!p)
    {
        return nullptr;
    }

    handle h = 0;
    string fp, fn;
    handle o = 0;
    unsigned char expF[8];

    CacheableReader r(*d);
    if (r.unserializehandle(h) &&
        r.unserializestring(fp) &&
        r.unserializestring(fn) &&
        r.unserializehandle(o) &&
        r.unserializeexpansionflags(expF, 0))
    {
        auto* ds = new DeletedShare(p->userHandle, p->userEmail, o, h, p->timestamp, id);
        ds->folderPath = fp;
        ds->folderName = fn;
        ds->setRelevant(p->relevant);
        ds->setSeen(p->seen);
        return ds;
    }

    return nullptr;
}

UserAlert::NewSharedNodes::NewSharedNodes(UserAlertRaw& un, unsigned int id)
    : Base(un, id)
{

    vector<UserAlertRaw::handletype> f;
    un.gethandletypearray('f', f);
    parentHandle = un.gethandle('n', MegaClient::NODEHANDLE, UNDEF);

    for (size_t n = f.size(); n--; )
    {
        if (f[n].t == FOLDERNODE)
        {
            folderNodeHandles.push_back(f[n].h);
        }
        else if (f[n].t == FILENODE)
        {
            fileNodeHandles.push_back(f[n].h);
        }
        // else should not be happening, we can add a sanity check
    }
}

UserAlert::NewSharedNodes::NewSharedNodes(handle uh, handle ph, m_time_t timestamp, unsigned int id,
                                          vector<handle>&& fileHandles, vector<handle>&& folderHandles)
    : Base(UserAlert::type_put, uh, string(), timestamp, id)
    , parentHandle(ph), fileNodeHandles(std::move(fileHandles)), folderNodeHandles(std::move(folderHandles))
{
    assert(!ISUNDEF(uh));
}

void UserAlert::NewSharedNodes::text(string& header, string& title, MegaClient* mc)
{
    updateEmail(mc);
    ostringstream notificationText;

    // Get wording for the number of files and folders added
    const auto folderCount = folderNodeHandles.size();
    const auto fileCount = fileNodeHandles.size();
    if ((folderCount > 1) && (fileCount > 1)) {
        notificationText << folderCount << " folders and " << fileCount << " files";
    }
    else if ((folderCount > 1) && (fileCount == 1)) {
        notificationText << folderCount << " folders and 1 file";
    }
    else if ((folderCount == 1) && (fileCount > 1)) {
        notificationText << "1 folder and " << fileCount << " files";
    }
    else if ((folderCount == 1) && (fileCount == 1)) {
        notificationText << "1 folder and 1 file";
    }
    else if (folderCount > 1) {
        notificationText << folderCount << " folders";
    }
    else if (fileCount > 1) {
        notificationText << fileCount << " files";
    }
    else if (folderCount == 1) {
        notificationText << "1 folder";
    }
    else if (fileCount == 1) {
        notificationText << "1 file";
    }
    else {
        notificationText << "nothing";
    }

    // Set wording of the title
    if (!email().empty())
    {
        title = email() + " added " + notificationText.str();
    }
    else if ((fileCount + folderCount) > 1)
    {
        title = notificationText.str() + " have been added";
    }
    else {
        title = notificationText.str() + " has been added";
    }
    header = email();
}

bool UserAlert::NewSharedNodes::serialize(string* d) const
{
    Base::serialize(d);
    CacheableWriter w(*d);
    w.serializehandle(parentHandle);

    w.serializecompressedu64(fileNodeHandles.size());
    for (auto& h : fileNodeHandles)
    {
        w.serializehandle(h);
    }

    w.serializecompressedu64(folderNodeHandles.size());
    for (auto& h : folderNodeHandles)
    {
        w.serializehandle(h);
    }

    w.serializeexpansionflags();
    return true;
}

UserAlert::NewSharedNodes* UserAlert::NewSharedNodes::unserialize(string* d, unsigned id)
{
    auto p = Base::unserialize(d);
    if (!p)
    {
        return nullptr;
    }

    handle ph = 0;

    CacheableReader r(*d);
    if (r.unserializehandle(ph))
    {
        uint64_t n = 0;
        if (r.unserializecompressedu64(n))
        {
            vector<handle> vh1(n, 0);
            if (n)
            {
                for (auto& h1 : vh1)
                {
                    if (!r.unserializehandle(h1))
                    {
                        return nullptr;
                    }
                }
            }

            n = 0;
            if (r.unserializecompressedu64(n))
            {
                vector<handle> vh2(n, 0);
                if (n)
                {
                    for (auto& h2 : vh2)
                    {
                        if (!r.unserializehandle(h2))
                        {
                            return nullptr;
                        }
                    }
                }

                unsigned char expF[8];
                if (!r.unserializeexpansionflags(expF, 0))
                {
                    return nullptr;
                }

                auto* nsn = new NewSharedNodes(p->userHandle, ph, p->timestamp, id, std::move(vh1), std::move(vh2));
                nsn->setRelevant(p->relevant);
                nsn->setSeen(p->seen);
                return nsn;
            }
        }
    }

    return nullptr;
}

UserAlert::RemovedSharedNode::RemovedSharedNode(UserAlertRaw& un, unsigned int id)
    : Base(un, id)
{
    vector<UserAlertRaw::handletype> handlesAndNodeTypes;
    un.gethandletypearray('f', handlesAndNodeTypes);

    for (const auto& handleAndType: handlesAndNodeTypes)
    {
        nodeHandles.push_back(handleAndType.h);
    }
}

UserAlert::RemovedSharedNode::RemovedSharedNode(handle uh, m_time_t timestamp, unsigned int id,
                                                vector<handle>&& handles)
    : Base(UserAlert::type_d, uh, string(), timestamp, id), nodeHandles(std::move(handles))
{
}

void UserAlert::RemovedSharedNode::text(string& header, string& title, MegaClient* mc)
{
    updateEmail(mc);
    ostringstream s;
    const auto itemsNumber = nodeHandles.size();
    if (itemsNumber > 1)
    {
        s << "Removed " << itemsNumber << " items from a share"; // 8913
    }
    else
    {
        s << "Removed item from shared folder"; // 8910
    }
    title = s.str();
    header = email();
}

bool UserAlert::RemovedSharedNode::serialize(string* d) const
{
    Base::serialize(d);
    CacheableWriter w(*d);

    w.serializecompressedu64(nodeHandles.size());
    for (auto& h : nodeHandles)
    {
        w.serializehandle(h);
    }

    w.serializeexpansionflags();
    return true;
}

UserAlert::RemovedSharedNode* UserAlert::RemovedSharedNode::unserialize(string* d, unsigned id)
{
    auto p = Base::unserialize(d);
    if (!p)
    {
        return nullptr;
    }

    uint64_t n = 0;

    CacheableReader r(*d);
    if (r.unserializecompressedu64(n))
    {
        vector<handle> vh(n, 0);
        if (n)
        {
            for (auto& h : vh)
            {
                if (!r.unserializehandle(h))
                {
                    break;
                }
            }
        }

        unsigned char expF[8];
        if (!r.unserializeexpansionflags(expF, 0))
        {
            return nullptr;
        }

        auto* rsn = new RemovedSharedNode(p->userHandle, p->timestamp, id, std::move(vh));
        rsn->setRelevant(p->relevant);
        rsn->setSeen(p->seen);
        return rsn;
    }

    return nullptr;
}

UserAlert::UpdatedSharedNode::UpdatedSharedNode(UserAlertRaw& un, unsigned int id)
    : Base(un, id)
{
    vector<UserAlertRaw::handletype> handlesAndNodeTypes;
    un.gethandletypearray('f', handlesAndNodeTypes);

    for (const auto& handleAndType: handlesAndNodeTypes)
    {
        nodeHandles.push_back(handleAndType.h);
    }
}

UserAlert::UpdatedSharedNode::UpdatedSharedNode(handle uh, m_time_t timestamp, unsigned int id,
                                                vector<handle>&& handles)
    : Base(UserAlert::type_u, uh, string(), timestamp, id), nodeHandles(std::move(handles))
{
}

void UserAlert::UpdatedSharedNode::text(string& header, string& title, MegaClient* mc)
{
    updateEmail(mc);
    header = email();
    const auto itemsNumber = nodeHandles.size();
    const string& itemText = (itemsNumber == 1) ? "" : "s";
    title = "Updated " + to_string(itemsNumber) + " item" + itemText + " in shared folder";
}

bool UserAlert::UpdatedSharedNode::serialize(string* d) const
{
    Base::serialize(d);
    CacheableWriter w(*d);

    w.serializecompressedu64(nodeHandles.size());
    for (auto& h : nodeHandles)
    {
        w.serializehandle(h);
    }

    w.serializeexpansionflags();
    return true;
}

UserAlert::UpdatedSharedNode* UserAlert::UpdatedSharedNode::unserialize(string* d, unsigned id)
{
    auto p = Base::unserialize(d);
    if (!p)
    {
        return nullptr;
    }

    uint64_t n = 0;
    unsigned char expF[8];

    CacheableReader r(*d);
    if (r.unserializecompressedu64(n))
    {
        vector<handle> vh(n, 0);
        if (n)
        {
            for (auto& h : vh)
            {
                if (!r.unserializehandle(h))
                {
                    break;
                }
            }
        }

        if (!r.unserializeexpansionflags(expF, 0))
        {
            return nullptr;
        }

        auto* usn = new UpdatedSharedNode(p->userHandle, p->timestamp, id, std::move(vh));
        usn->setRelevant(p->relevant);
        usn->setSeen(p->seen);
        return usn;
    }

    return nullptr;
}

string UserAlert::Payment::getProPlanName()
{
    switch (planNumber) {
    case ACCOUNT_TYPE_PROI:
        return "Pro I"; // 5819
    case ACCOUNT_TYPE_PROII:
        return "Pro II"; // 6125
    case ACCOUNT_TYPE_PROIII:
        return "Pro III"; // 6126
    case ACCOUNT_TYPE_LITE:
        return "Pro Lite"; // 8413
    case ACCOUNT_TYPE_BUSINESS:
        return "Business";  // 19530
    case ACCOUNT_TYPE_PRO_FLEXI:
        return "Pro Flexi";
    case ACCOUNT_TYPE_STARTER:
        return "Starter";
    case ACCOUNT_TYPE_BASIC:
        return "Basic";
    case ACCOUNT_TYPE_ESSENTIAL:
        return "Essential";
    case ACCOUNT_TYPE_FREE:
        [[fallthrough]];
    default:
        return "Free"; // 435
    }
}

UserAlert::Payment::Payment(UserAlertRaw& un, unsigned int id)
    : Base(un, id)
{
    success = 's' == un.getnameid('r', 0);
    planNumber = un.getint('p', 0);
}

UserAlert::Payment::Payment(bool s, int plan, m_time_t timestamp, unsigned int id, nameid paymentType)
    : Base(paymentType, UNDEF, "", timestamp, id)
{
    success = s;
    planNumber = plan;
}

void UserAlert::Payment::text(string& header, string& title, MegaClient* mc)
{
    updateEmail(mc);
    ostringstream s;
    if (success)
    {
        s << "Your payment for the " << getProPlanName() << " plan was received."; // 7142
    }
    else
    {
        s << "Your payment for the " << getProPlanName() << " plan was unsuccessful."; // 7141
    }
    title = s.str();
    header = "Payment info"; // 1230
}

bool UserAlert::Payment::serialize(string* d) const
{
    Base::serialize(d);
    CacheableWriter w(*d);
    w.serializebool(success);
    w.serializeu32(planNumber);
    w.serializeexpansionflags();

    return true;
}

UserAlert::Payment* UserAlert::Payment::unserialize(string* d, unsigned id, nameid paymentType)
{
    auto p = Base::unserialize(d);
    if (!p)
    {
        return nullptr;
    }

    bool s = false;
    int plan = 0;
    unsigned char expF[8];

    CacheableReader r(*d);
    if (r.unserializebool(s) &&
        r.unserializeu32(reinterpret_cast<unsigned&>(plan)) &&
        r.unserializeexpansionflags(expF, 0))
    {
        auto* pmt = new Payment(s, plan, p->timestamp, id, paymentType);
        pmt->setRelevant(p->relevant);
        pmt->setSeen(p->seen);
        return pmt;
    }

    return nullptr;
}

UserAlert::PaymentReminder::PaymentReminder(UserAlertRaw& un, unsigned int id)
    : Base(un, id)
{
    expiryTime = un.getint64(MAKENAMEID2('t', 's'), ts());
}

UserAlert::PaymentReminder::PaymentReminder(m_time_t expiryts, unsigned int id)
    : Base(type_pses, UNDEF, "", m_time(), id)
{
    expiryTime = expiryts;
}

void UserAlert::PaymentReminder::text(string& header, string& title, MegaClient* mc)
{
    updateEmail(mc);
    m_time_t now = m_time();
    int days = int((expiryTime - now) / 86400);

    ostringstream s;
    if (expiryTime < now)
    {
        s << "Your PRO membership plan expired " << -days << (days == -1 ? " day" : " days") << " ago";
    }
    else
    {
        s << "Your PRO membership plan will expire in " << days << (days == 1 ? " day." : " days.");   // 8596, 8597
    }
    title = s.str();
    header = "PRO membership plan expiring soon"; // 8598
}

bool UserAlert::PaymentReminder::serialize(string* d) const
{
    Base::serialize(d);
    CacheableWriter w(*d);
    w.serializecompressedi64(expiryTime);
    w.serializeexpansionflags();

    return true;
}

UserAlert::PaymentReminder* UserAlert::PaymentReminder::unserialize(string* d, unsigned id)
{
    auto p = Base::unserialize(d);
    if (!p)
    {
        return nullptr;
    }

    m_time_t exp = 0;
    unsigned char expF[8];

    CacheableReader r(*d);
    if (r.unserializecompressedi64(exp) &&
        r.unserializeexpansionflags(expF, 0))
    {
        auto* pmr = new PaymentReminder(exp, id);
        pmr->setRelevant(p->relevant);
        pmr->setSeen(p->seen);
        return pmr;
    }

    return nullptr;
}

UserAlert::Takedown::Takedown(UserAlertRaw& un, unsigned int id)
    : Base(un, id)
{
    int n = un.getint(MAKENAMEID4('d', 'o', 'w', 'n'), -1);
    isTakedown = n == 1;
    isReinstate = n == 0;
    nodeHandle = un.gethandle('h', MegaClient::NODEHANDLE, UNDEF);
    pst.relevant = isTakedown || isReinstate;
}

UserAlert::Takedown::Takedown(bool down, bool reinstate, int /*t*/, handle nh, m_time_t timestamp, unsigned int id)
    : Base(type_ph, UNDEF, "", timestamp, id)
{
    isTakedown = down;
    isReinstate = reinstate;
    nodeHandle = nh;
    pst.relevant = isTakedown || isReinstate;
}

void UserAlert::Takedown::text(string& header, string& title, MegaClient* mc)
{
    updateEmail(mc);
    const char* typestring = "node";
    string name;

    std::shared_ptr<Node> node = mc->nodebyhandle(nodeHandle);
    if (node)
    {
        if (node->type == FOLDERNODE)
        {
            typestring = "folder";
        }
        else if (node->type == FILENODE)
        {
            typestring = "file";
        }

        name = node->displaypath();
    }

    if (name.empty())
    {
        char buffer[12];
        Base64::btoa((byte*)&(nodeHandle), MegaClient::NODEHANDLE, buffer);
        name = "handle ";
        name += buffer;
    }

    ostringstream s;
    if (isTakedown)
    {
        header = "Takedown notice";  //8521
        s << "Your publicly shared " << typestring << " (" << name << ") has been taken down."; //8522
    }
    else if (isReinstate)
    {
        header = "Takedown reinstated";  //8524
        s << "Your taken down " << typestring << " (" << name << ") has been reinstated."; // 8523
    }
    title = s.str();
}

bool UserAlert::Takedown::serialize(string* d) const
{
    Base::serialize(d);
    CacheableWriter w(*d);
    w.serializebool(isTakedown);
    w.serializebool(isReinstate);
    w.serializehandle(nodeHandle);
    w.serializeexpansionflags();

    return true;
}

UserAlert::Takedown* UserAlert::Takedown::unserialize(string* d, unsigned id)
{
    auto p = Base::unserialize(d);
    if (!p)
    {
        return nullptr;
    }

    bool takedown = false;
    bool reinstate = false;
    handle h = 0;
    unsigned char expF[8];

    CacheableReader r(*d);
    if (r.unserializebool(takedown) &&
        r.unserializebool(reinstate) &&
        r.unserializehandle(h) &&
        r.unserializeexpansionflags(expF, 0))
    {
        auto* td = new Takedown(takedown, reinstate, 0, h, p->timestamp, id);
        td->setRelevant(p->relevant);
        td->setSeen(p->seen);
        return td;
    }

    return nullptr;
}

#ifdef ENABLE_CHAT
UserAlert::NewScheduledMeeting::NewScheduledMeeting(UserAlertRaw& un, unsigned int id)
    : Base(un, id)
{
    mChatid = un.gethandle(MAKENAMEID3('c', 'i', 'd'), MegaClient::CHATHANDLE, UNDEF);
    if (mChatid == UNDEF)
    {
        assert(false);
        LOG_err << "NewScheduledMeeting user alert ctor: invalid chatid";
        return;
    }

    mSchedMeetingHandle = un.gethandle(MAKENAMEID2('i', 'd'), MegaClient::CHATHANDLE, UNDEF);
    if (mSchedMeetingHandle == UNDEF)
    {
        assert(false);
        LOG_err << "NewScheduledMeeting user alert ctor: invalid scheduled meeting id";
        return;
    }

    // optional param parent scheduled meeting id (just for child scheduled meetings)
    mParentSchedId = un.gethandle(MAKENAMEID1('p'), MegaClient::USERHANDLE, UNDEF);

    // optional param start date time (just for child scheduled meetings)
    mStartDateTime = un.getint64(MAKENAMEID1('o'), mega_invalid_timestamp);
}

void UserAlert::NewScheduledMeeting::text(string& header, string& title, MegaClient* mc)
{
    Base::updateEmail(mc);
    ostringstream oss;
    oss << "New Scheduled Meeting details:"
        << "\n\tChatid : " << toHandle(mChatid)
        << "\n\tSched Meeting Id: " << toHandle(mSchedMeetingHandle)
        << "\n\tParent Sched Meeting Id: " << toHandle(mParentSchedId)
        << "\n\tMeeting start date time (overrides): " << mStartDateTime
        << "\n\tCreated by: " << pst.userEmail;

    header = "New Scheduled Meeting";
    title = oss.str();

    LOG_debug << title;
}

bool UserAlert::NewScheduledMeeting::serialize(string* d) const
{
    Base::serialize(d);
    CacheableWriter w(*d);
    w.serializeu8(subtype_new_Sched);
    w.serializehandle(mChatid);
    w.serializehandle(mSchedMeetingHandle);
    w.serializehandle(mParentSchedId);
    w.serializei64(mStartDateTime);
    w.serializeexpansionflags();

    return true;
}

UserAlert::Base* UserAlert::unserializeNewUpdSched(string* d, unsigned id)
{
    CacheableReader r(*d);
    if (!Base::readBase(r))
    {
        assert(false);
        LOG_err << "unserializeNewUpdSched: can't read UserAlert::Base";
        return nullptr;
    }

    uint8_t subType = subtype_invalid;
    if (!r.unserializeu8(subType))
    {
        assert(false);
        LOG_err << "unserializeNewUpdSched: ill-formed mcsmp user alert (sub-type is not present)";
        return nullptr;
    }

    switch (subType)
    {
        case subtype_new_Sched:
            return UserAlert::NewScheduledMeeting::unserialize(d, id);
            break;

        case subtype_upd_Sched:
            return UserAlert::UpdatedScheduledMeeting::unserialize(d, id);
            break;

        default:
        {
            assert(false);
            LOG_err << "unserializeNewUpdSched: invalid mcsmp user alert sub-type";
            return nullptr;
        }
    }
}

UserAlert::NewScheduledMeeting* UserAlert::NewScheduledMeeting::unserialize(string* d, unsigned id)
{
    auto b = Base::unserialize(d);
    if (!b) return nullptr;
    CacheableReader r(*d);

    uint8_t subType = subtype_invalid;
    r.unserializeu8(subType);
    if (subType != subtype_new_Sched)
    {
        return nullptr;
    }

    handle chatid = UNDEF;
    handle sm = UNDEF;
    handle parentSchedId = UNDEF;
    m_time_t overrides = mega_invalid_timestamp;
    unsigned char expF[8];
    if (r.unserializehandle(chatid)
        && r.unserializehandle(sm)
        && r.unserializehandle(parentSchedId)
        && r.unserializei64(overrides)
        && r.unserializeexpansionflags(expF, 0))
    {
        auto* nsm = new NewScheduledMeeting(b->userHandle, b->timestamp, id, chatid, sm, parentSchedId, overrides);
        nsm->setSeen(b->seen);
        nsm->setRelevant(b->relevant);
        return nsm;
    }

    return nullptr;
}

UserAlert::DeletedScheduledMeeting::DeletedScheduledMeeting(UserAlertRaw& un, unsigned int id)
    : Base(un, id)
{
    mChatid = un.gethandle(MAKENAMEID3('c', 'i', 'd'), MegaClient::CHATHANDLE, UNDEF);
    mSchedMeetingHandle = un.gethandle(MAKENAMEID2('i', 'd'), MegaClient::CHATHANDLE, UNDEF);
    if (mChatid == UNDEF)
    {
        assert(false);
        LOG_err << "DeletedScheduledMeeting user alert ctor: invalid scheduled chatid";
        return;
    }

    if (mSchedMeetingHandle == UNDEF)
    {
        assert(false);
        LOG_err << "DeletedScheduledMeeting user alert ctor: invalid scheduled meeting id";
        return;
    }
}

void UserAlert::DeletedScheduledMeeting::text(string& header, string& title, MegaClient* mc)
{
    Base::updateEmail(mc);
    ostringstream oss;
    oss << "Deleted Scheduled Meeting details:"
        << "\n\tChatid: " << toHandle(mChatid)
        << "\n\tSched Meeting Id: " << toHandle(mSchedMeetingHandle)
        << "\n\tDeleted by: " << pst.userEmail;

    header = "Deleted Scheduled Meeting";
    title = oss.str();

    LOG_debug << title;
}

bool UserAlert::DeletedScheduledMeeting::serialize(string* d) const
{
    Base::serialize(d);
    CacheableWriter w(*d);
    w.serializehandle(mChatid);
    w.serializehandle(mSchedMeetingHandle);
    w.serializeexpansionflags();

    return true;
}

UserAlert::DeletedScheduledMeeting* UserAlert::DeletedScheduledMeeting::unserialize(string* d, unsigned id)
{
    auto b = Base::unserialize(d);
    if (!b) return nullptr;

    handle chatid = UNDEF;
    handle sm = UNDEF;
    unsigned char expF[8];

    CacheableReader r(*d);
    if (r.unserializehandle(chatid)
        && r.unserializehandle(sm)
        && r.unserializeexpansionflags(expF, 0))
    {
        auto* dsm = new DeletedScheduledMeeting(b->userHandle, b->timestamp, id, chatid, sm);
        dsm->setSeen(b->seen);
        dsm->setRelevant(b->relevant);
        return dsm;
    }

    return nullptr;
}

UserAlert::UpdatedScheduledMeeting::UpdatedScheduledMeeting(UserAlertRaw& un, unsigned int id)
    : Base(un, id)
{
    mChatid = un.gethandle(MAKENAMEID3('c', 'i', 'd'), MegaClient::CHATHANDLE, UNDEF);
    mSchedMeetingHandle = un.gethandle(MAKENAMEID2('i', 'd'), MegaClient::CHATHANDLE, UNDEF);
    if (mChatid == UNDEF)
    {
        assert(false);
        LOG_err << "UpdatedScheduledMeeting user alert ctor: invalid scheduled chatid";
        return;
    }

    if (mSchedMeetingHandle == UNDEF)
    {
        assert(false);
        LOG_err << "UpdatedScheduledMeeting user alert ctor: invalid scheduled meeting id";
        return;
    }

    JSON auxJson = un.field(MAKENAMEID2('c', 's'));
    if (auxJson.pos)
    {
        if (auxJson.enterobject())
        {
            if (MegaClient::parseScheduledMeetingChangeset(&auxJson, &mUpdatedChangeset) != API_OK)
            {
                LOG_err << "UpdatedScheduledMeeting user alert ctor: error parsing cs array";
                assert(false);
                return;
            }
            auxJson.leaveobject();
        }
        else
        {
            assert(false);
            LOG_err << "UpdatedScheduledMeeting user alert ctor: Ill-formed user alert";
        }
    }

    // optional param parent scheduled meeting id (just for child scheduled meetings)
    mParentSchedId = un.gethandle(MAKENAMEID1('p'), MegaClient::USERHANDLE, UNDEF);

    // optional param start date time (just for child scheduled meetings)
    mStartDateTime = un.getint64(MAKENAMEID1('o'), mega_invalid_timestamp);
}

void UserAlert::UpdatedScheduledMeeting::text(string& header, string& title, MegaClient* mc)
{
    Base::updateEmail(mc);
    ostringstream oss;
    oss << "Updated Scheduled Meeting details:"
        << "\n\tChatid: " << toHandle(mChatid)
        << "\n\tSched Meeting Id: " << toHandle(mSchedMeetingHandle)
        << "\n\tParent Sched Meeting Id: " << toHandle(mParentSchedId)
        << "\n\tMeeting start date time (overrides): " << mStartDateTime
        << "\n\tUpdated by: " << pst.userEmail;

    for (size_t changeBitPos = 0; changeBitPos < Changeset::CHANGE_TYPE_SIZE; ++changeBitPos)
    {
        uint64_t changeType = 1ull << changeBitPos;
        if (!mUpdatedChangeset.hasChanged(changeType)) continue;

        oss << "\n\t\t" << mUpdatedChangeset.changeToString(changeType) << " updated";
        if (changeType == Changeset::CHANGE_TYPE_TITLE && mUpdatedChangeset.getUpdatedTitle())
        {
            const auto& titleCS = mUpdatedChangeset.getUpdatedTitle();
            oss << ": previous title |" << Base64::atob(titleCS->oldValue)
                << "| new title |" << Base64::atob(titleCS->newValue) << "|";
        }

        if (changeType == Changeset::CHANGE_TYPE_TIMEZONE && mUpdatedChangeset.getUpdatedTimeZone())
        {
            const auto& tzCS = mUpdatedChangeset.getUpdatedTimeZone();
            oss << ": previous timezone |" << Base64::atob(tzCS->oldValue)
                << "| new timezone |" << Base64::atob(tzCS->newValue) << "|";
        }

        if (changeType == Changeset::CHANGE_TYPE_STARTDATE && mUpdatedChangeset.getUpdatedStartDateTime())
        {
            const auto& sdCS = mUpdatedChangeset.getUpdatedStartDateTime();
            oss << ": previous startDateTime |" << sdCS->oldValue
                << "| new startDateTime |" << sdCS->newValue << "|";
        }

        if (changeType == Changeset::CHANGE_TYPE_ENDDATE && mUpdatedChangeset.getUpdatedEndDateTime())
        {
            const auto& edCS = mUpdatedChangeset.getUpdatedEndDateTime();
            oss << ": previous endDateTime |" << edCS->oldValue
                << "| new endDateTime |" << edCS->newValue << "|";
        }
    };

    header = "Updated Scheduled Meeting";
    title = oss.str();

    LOG_debug << title;
}

bool UserAlert::UpdatedScheduledMeeting::serialize(string* d) const
{
    Base::serialize(d);
    CacheableWriter w(*d);
    w.serializeu8(subtype_upd_Sched);
    w.serializehandle(mChatid);
    w.serializehandle(mSchedMeetingHandle);
    w.serializehandle(mParentSchedId);
    w.serializei64(mStartDateTime);
    w.serializeu64(static_cast<uint64_t>(mUpdatedChangeset.getChanges()));

    if (mUpdatedChangeset.hasChanged(Changeset::CHANGE_TYPE_TITLE) && mUpdatedChangeset.getUpdatedTitle())
    {
        const auto& titleCS = mUpdatedChangeset.getUpdatedTitle();
        w.serializestring(titleCS->oldValue);
        w.serializestring(titleCS->newValue);
    }

    if (mUpdatedChangeset.hasChanged(Changeset::CHANGE_TYPE_TIMEZONE) && mUpdatedChangeset.getUpdatedTimeZone())
    {
        const auto& tzCS = mUpdatedChangeset.getUpdatedTimeZone();
        w.serializestring(tzCS->oldValue);
        w.serializestring(tzCS->newValue);
    }

    if (mUpdatedChangeset.hasChanged(Changeset::CHANGE_TYPE_STARTDATE) && mUpdatedChangeset.getUpdatedStartDateTime())
    {
        const auto& sdCS = mUpdatedChangeset.getUpdatedStartDateTime();
        w.serializei64(sdCS->oldValue);
        w.serializei64(sdCS->newValue);
    }

    if (mUpdatedChangeset.hasChanged(Changeset::CHANGE_TYPE_ENDDATE) && mUpdatedChangeset.getUpdatedEndDateTime())
    {
        const auto& edCS = mUpdatedChangeset.getUpdatedEndDateTime();
        w.serializei64(edCS->oldValue);
        w.serializei64(edCS->newValue);
    }

    w.serializeexpansionflags();

    return true;
}

UserAlert::UpdatedScheduledMeeting* UserAlert::UpdatedScheduledMeeting::unserialize(string* d, unsigned id)
{
    auto b = Base::unserialize(d);
    if (!b) return nullptr;
    CacheableReader r(*d);

    uint8_t subType = subtype_invalid;
    r.unserializeu8(subType);
    if (subType != subtype_upd_Sched)
    {
        return nullptr;
    }

    handle chatid = UNDEF;
    handle sm = UNDEF;
    handle parentSchedId = UNDEF;
    m_time_t overrides = mega_invalid_timestamp;
    uint64_t changes = 0;
    unsigned char expF[8];
    if (r.unserializehandle(chatid)
        && r.unserializehandle(sm)
        && r.unserializehandle(parentSchedId)
        && r.unserializei64(overrides)
        && r.unserializeu64(changes))
    {
        unique_ptr<Changeset::StrChangeset> tcs;
        if (changes & Changeset::CHANGE_TYPE_TITLE)
        {
            string oldTitle, newTitle;
            if (r.unserializestring(oldTitle) && r.unserializestring(newTitle))
            {
                tcs.reset(new Changeset::StrChangeset{oldTitle, newTitle});
            }
        }

        unique_ptr<Changeset::StrChangeset> tzcs;
        if (changes & Changeset::CHANGE_TYPE_TIMEZONE)
        {
            string oldTz, newTz;
            if (r.unserializestring(oldTz) && r.unserializestring(newTz))
            {
                tzcs.reset(new Changeset::StrChangeset{oldTz, newTz});
            }
        }

        unique_ptr<Changeset::TsChangeset> sdcs;
        if (changes & Changeset::CHANGE_TYPE_STARTDATE)
        {
            m_time_t oldsd, newsd;
            if (r.unserializei64(oldsd) && r.unserializei64(newsd))
            {
                sdcs.reset(new Changeset::TsChangeset{oldsd, newsd});
            }
        }

        unique_ptr<Changeset::TsChangeset> edcs;
        if (changes & Changeset::CHANGE_TYPE_ENDDATE)
        {
            m_time_t olded, newed;
            if (r.unserializei64(olded) && r.unserializei64(newed))
            {
                edcs.reset(new Changeset::TsChangeset{olded, newed});
            }
        }

        if (r.unserializeexpansionflags(expF, 0))
        {
            auto* usm = new UpdatedScheduledMeeting(b->userHandle, b->timestamp, id, chatid, sm, parentSchedId, overrides, {changes, tcs, tzcs, sdcs, edcs});
            usm->setRelevant(b->relevant);
            usm->setSeen(b->seen);
            return usm;
        }
    }

    return nullptr;
}

UserAlert::UpdatedScheduledMeeting::Changeset::Changeset(const std::bitset<CHANGE_TYPE_SIZE>& _bs,
                                                         unique_ptr<StrChangeset>& _titleCS,
                                                         unique_ptr<StrChangeset>& _tzCS,
                                                         unique_ptr<TsChangeset>& _sdCS,
                                                         unique_ptr<TsChangeset>& _edCS)
    : mUpdatedFields(_bs),
      mUpdatedTitle(std::move(_titleCS)),
      mUpdatedTimeZone(std::move(_tzCS)),
      mUpdatedStartDateTime(std::move(_sdCS)),
      mUpdatedEndDateTime(std::move(_edCS))
{
    if (!invariant())
    {
        LOG_err << "ScheduledMeetings: Ill-formed Changeset construction";
        assert(false);
    }
}

string UserAlert::UpdatedScheduledMeeting::Changeset::changeToString(uint64_t changeType) const
{
    switch (changeType)
    {
    case CHANGE_TYPE_TITLE:        return "Title";
    case CHANGE_TYPE_DESCRIPTION:  return "Description";
    case CHANGE_TYPE_CANCELLED:    return "Cancelled";
    case CHANGE_TYPE_TIMEZONE:     return "TimeZone";
    case CHANGE_TYPE_STARTDATE:    return "StartDate";
    case CHANGE_TYPE_ENDDATE:      return "EndDate";
    case CHANGE_TYPE_RULES:        return "Rules";
    default:                       return "Unexpected Field";
    }
}

void UserAlert::UpdatedScheduledMeeting::Changeset::addChange(uint64_t changeType,
                             UpdatedScheduledMeeting::Changeset::StrChangeset* sSet,
                             UpdatedScheduledMeeting::Changeset::TsChangeset* tSet)
{
    mUpdatedFields |= changeType;
    switch (changeType)
    {
        case CHANGE_TYPE_TITLE:
            if (sSet) { mUpdatedTitle.reset(new StrChangeset{sSet->oldValue, sSet->newValue}); }
            break;
        case CHANGE_TYPE_TIMEZONE:
            if (sSet) { mUpdatedTimeZone.reset(new StrChangeset{sSet->oldValue, sSet->newValue}); }
            break;
        case CHANGE_TYPE_STARTDATE:
            if (tSet) { mUpdatedStartDateTime.reset(new TsChangeset{tSet->oldValue, tSet->newValue}); }
            break;
        case CHANGE_TYPE_ENDDATE:
            if (tSet) { mUpdatedEndDateTime.reset(new TsChangeset{tSet->oldValue, tSet->newValue}); }
            break;
        case CHANGE_TYPE_DESCRIPTION:
        case CHANGE_TYPE_CANCELLED:
        case CHANGE_TYPE_RULES:
            break;
        default:
            mUpdatedFields &= ~changeType;
            break;
    }
    if (!invariant())
    {
        LOG_err << "ScheduledMeetings: Ill-formed update changeset received";
        assert(false);
    }
}
#endif

UserAlerts::UserAlerts(MegaClient& cmc)
    : mc(cmc)
    , nextid(0)
    , begincatchup(false)
    , catchupdone(false)
    , catchup_last_timestamp(0)
    , lsn(UNDEF)
    , fsn(UNDEF)
    , lastTimeDelta(0)
    , provisionalmode(false)
    , notingSharedNodes(false)
    , ignoreNodesUnderShare(UNDEF)
{
}

unsigned int UserAlerts::nextId()
{
    return ++nextid;
}

void UserAlerts::ff::squash(const ff &rhs)
{
    areNodeVersions(rhs.areNodeVersions());
    std::for_each(std::begin(rhs.alertTypePerFileNode), std::end(rhs.alertTypePerFileNode),
                  [this](const std::pair<handle, nameid>& p) { alertTypePerFileNode[p.first] = p.second; });
    std::for_each(std::begin(rhs.alertTypePerFolderNode), std::end(rhs.alertTypePerFolderNode),
                  [this](const std::pair<handle, nameid>& p) { alertTypePerFolderNode[p.first] = p.second; });
}

bool UserAlerts::isUnwantedAlert(nameid type, int action)
{
    using namespace UserAlert;

    if (type == type_put || type == type_share || type == type_dshare)
    {
        if (!flags.cloud_enabled) {
            return true;
        }
    }
    else if (type == type_c || type == type_ipc || type == type_upci || type == type_upco)
    {
        if (!flags.contacts_enabled) {
            return true;
        }
    }

    if (type == type_put)
    {
        return !flags.cloud_newfiles;
    }
    else if (type == type_share)
    {
        return !flags.cloud_newshare;
    }
    else if (type == type_dshare)
    {
        return !flags.cloud_delshare;
    }
    else if (type == type_ipc)
    {
        return !flags.contacts_fcrin;
    }
    else if (type == type_c)
    {
        return (action == -1 || action == 0) && !flags.contacts_fcrdel;
    }
    else if (type == type_upco)
    {
        return (action == -1 || action == 2) && !flags.contacts_fcracpt;
    }

    return false;
}

void UserAlerts::add(UserAlertRaw& un)
{
    using namespace UserAlert;
    namespace u = UserAlert;
    Base* unb = NULL;

    switch (un.t) {
    case type_ipc:
        unb = new IncomingPendingContact(un, nextId());
        break;
    case type_c:
        unb = new ContactChange(un, nextId());
        break;
    case type_upci:
        unb = new UpdatedPendingContactIncoming(un, nextId());
        break;
    case type_upco:
        unb = new UpdatedPendingContactOutgoing(un, nextId());
        break;
    case type_share:
        unb = new u::NewShare(un, nextId());
        break;
    case type_dshare:
        unb = new DeletedShare(un, nextId());
        break;
    case type_put:
        unb = new NewSharedNodes(un, nextId());
        break;
    case type_d:
        unb = new RemovedSharedNode(un, nextId());
        break;
    case type_u:
        unb = new UpdatedSharedNode(un, nextId());
        break;
    case type_psts:
    case type_psts_v2:
        unb = new Payment(un, nextId());
        break;
    case type_pses:
        unb = new PaymentReminder(un, nextId());
        break;
    case type_ph:
        unb = new Takedown(un, nextId());
        break;
#ifdef ENABLE_CHAT
    case type_nusm:
    {
        if (!un.has(MAKENAMEID2('c', 's'))) // if cs is not present, is a new scheduled meeting
        {
            unb = new NewScheduledMeeting(un, nextId());
        }
        else
        {
            unb = new UpdatedScheduledMeeting(un, nextId());
        }
    }
    break;
    case type_dsm:
        unb = new DeletedScheduledMeeting(un, nextId());
        break;
#endif
    default:
        unb = NULL;   // If it's a notification type we do not recognise yet
    }

    if (unb)
    {
        add(unb);
    }
}

UserAlert::Base* UserAlerts::findAlertToCombineWith(const UserAlert::Base* a, nameid t) const
{
    if (a->type == t)
    {
        auto ait = std::find_if(alerts.rbegin(), alerts.rend(), [](UserAlert::Base* b) { return !b->removed(); });
        return ait != alerts.rend() && (*ait)->type == t ? *ait : nullptr;
    }

    return nullptr;
}

void UserAlerts::add(UserAlert::Base* unb)
{
    // Alerts received by this function should be persisted when coming from sc50 and action packets,
    // but not when being just loaded from persistent db.

    // unb is either directly from notification json, or constructed from actionpacket.
    // We take ownership.

    if (provisionalmode)
    {
        provisionals.push_back(unb);
        return;
    }

    if (!catchupdone && unb->ts() > catchup_last_timestamp)
    {
        catchup_last_timestamp = unb->ts();
    }
    else if (catchupdone && unb->ts() < catchup_last_timestamp)
    {
        // this is probably a duplicate from the initial set, generated from normal sc packets
        LOG_warn << "discarding duplicate user alert of type " << unb->type;
        delete unb;
        return;
    }

    // attempt to combine with previous NewSharedNodes
    UserAlert::Base* cmb = findAlertToCombineWith(unb, UserAlert::type_put);
    if (cmb)
    {
        // If it's file/folders added, and the prior one is for the same user and within 5 mins then we can combine instead
        UserAlert::NewSharedNodes* np = dynamic_cast<UserAlert::NewSharedNodes*>(unb);
        UserAlert::NewSharedNodes* op = dynamic_cast<UserAlert::NewSharedNodes*>(cmb);
        if (np && op)
        {
            if (np->user() == op->user() && np->ts() - op->ts() < 300 &&
                np->parentHandle == op->parentHandle && !ISUNDEF(np->parentHandle))
            {
                op->fileNodeHandles.insert(end(op->fileNodeHandles), begin(np->fileNodeHandles), end(np->fileNodeHandles));
                op->folderNodeHandles.insert(end(op->folderNodeHandles),
                                             begin(np->folderNodeHandles), end(np->folderNodeHandles));
                LOG_debug << "Merged user alert, type " << np->type << " ts " << np->ts();

                notifyAlert(op, false, 0);

                delete unb;
                return;
            }
        }
    }

    // attempt to combine with previous RemovedSharedNode
    cmb = findAlertToCombineWith(unb, UserAlert::type_d);
    if (cmb)
    {
        // If it's file/folders removed, and the prior one is for the same user and within 5 mins then we can combine instead
        UserAlert::RemovedSharedNode* nd = dynamic_cast<UserAlert::RemovedSharedNode*>(unb);
        UserAlert::RemovedSharedNode* od = dynamic_cast<UserAlert::RemovedSharedNode*>(cmb);
        if (nd && od)
        {
            if (nd->user() == od->user() && nd->ts() - od->ts() < 300)
            {
                od->nodeHandles.insert(end(od->nodeHandles), begin(nd->nodeHandles), end(nd->nodeHandles));
                LOG_debug << "Merged user alert, type " << nd->type << " ts " << nd->ts();

                notifyAlert(od, false, 0);

                delete unb;
                return;
            }
        }
    }

    // attempt to combine with previous UpdatedSharedNode
    cmb = findAlertToCombineWith(unb, UserAlert::type_u);
    if (cmb)
    {
        // If it's file/folders updated, and the prior one is for the same user and within 5 mins then we can combine instead
        UserAlert::UpdatedSharedNode* nd = dynamic_cast<UserAlert::UpdatedSharedNode*>(unb);
        UserAlert::UpdatedSharedNode* od = dynamic_cast<UserAlert::UpdatedSharedNode*>(cmb);
        if (nd && od)
        {
            if (nd->user() == od->user() && nd->ts() - od->ts() < 300)
            {
                od->nodeHandles.insert(end(od->nodeHandles), begin(nd->nodeHandles), end(nd->nodeHandles));
                LOG_debug << "Merged user alert, type " << nd->type << " ts " << nd->ts();

                notifyAlert(od, false, 0);

                delete unb;
                return;
            }
        }
    }

    // check for previous Payment-Reminder to ignore
    if (!alerts.empty() && (unb->type == UserAlert::type_psts || unb->type == UserAlert::type_psts_v2) && static_cast<UserAlert::Payment*>(unb)->success)
    {
        // if a successful payment is made then hide/remove any reminders received
        for (auto& a : alerts)
        {
            if (a->type == UserAlert::type_pses && a->relevant())
            {
                a->setRelevant(false);
                notifyAlert(a, a->seen(), a->tag);
            }
        }
    }

    unb->updateEmail(&mc);
    alerts.push_back(unb);
    LOG_debug << "Added user alert, type " << alerts.back()->type << " ts " << alerts.back()->ts();

    notifyAlert(unb, unb->seen(), 0);   // do not touch seen here, but tag
}

void UserAlerts::startprovisional()
{
    provisionalmode = true;
}

void UserAlerts::evalprovisional(handle originatinguser)
{
    provisionalmode = false;
    for (unsigned i = 0; i < provisionals.size(); ++i)
    {
        if (provisionals[i]->checkprovisional(originatinguser, &mc))
        {
            add(provisionals[i]);
        }
        else
        {
            delete provisionals[i];
        }
    }
    provisionals.clear();
}


void UserAlerts::beginNotingSharedNodes()
{
    notingSharedNodes = true;
    notedSharedNodes.clear();
}

void UserAlerts::noteSharedNode(handle user, int type, m_time_t ts, Node* n, nameid alertType)
{
    if (catchupdone && notingSharedNodes && (type == FILENODE || type == FOLDERNODE))
    {
        assert(!ISUNDEF(user));

        if (!ISUNDEF(ignoreNodesUnderShare) && (alertType != UserAlert::type_d))
        {
            // don't make alerts on files/folders already in the new share
            for (Node* p = n; p != NULL; p = p->parent.get())
            {
                if (p->nodehandle == ignoreNodesUnderShare)
                    return;
            }
        }

        ff& f = notedSharedNodes[make_pair(user, n ? n->parenthandle : UNDEF)];
        if (n && type == FOLDERNODE)
        {
            f.alertTypePerFolderNode[n->nodehandle] = alertType;
        }
        else if (n && type == FILENODE)
        {
            f.alertTypePerFileNode[n->nodehandle] = alertType;
	    f.areNodeVersions(n && n->parent && n->parent->type == FILENODE);
        }
        // there shouldn't be any other types

        if (!f.timestamp || (ts && ts < f.timestamp))
        {
            f.timestamp = ts;
        }
    }
}

bool UserAlerts::isConvertReadyToAdd(handle originatingUser) const
{
    return catchupdone && notingSharedNodes && (originatingUser != mc.me);
}

void UserAlerts::convertNotedSharedNodes(bool added)
{
    using namespace UserAlert;
    for (notedShNodesMap::iterator i = notedSharedNodes.begin(); i != notedSharedNodes.end(); ++i)
    {
        auto&& fileHandles = i->second.fileHandles();
        auto&& folderHandles = i->second.folderHandles();
        if (added)
        {
            add(new NewSharedNodes(i->first.first, i->first.second, i->second.timestamp, nextId(),
                                   std::move(fileHandles), std::move(folderHandles)));
        }
        else
        {
            std::move(folderHandles.begin(), folderHandles.end(), std::back_inserter(fileHandles));
            add(new RemovedSharedNode(i->first.first, m_time(), nextId(), std::move(fileHandles)));
        }
    }
}

void UserAlerts::clearNotedSharedMembers()
{
    notedSharedNodes.clear();
    notingSharedNodes = false;
    ignoreNodesUnderShare = UNDEF;
}

// make a notification out of the shared nodes noted
void UserAlerts::convertNotedSharedNodes(bool added, handle originatingUser)
{
    if (isConvertReadyToAdd(originatingUser))
    {
        convertNotedSharedNodes(added);
    }
    clearNotedSharedMembers();
}

void UserAlerts::ignoreNextSharedNodesUnder(handle h)
{
    ignoreNodesUnderShare = h;
}

pair<bool, UserAlert::handle_alerttype_map_t::difference_type>
UserAlerts::findNotedSharedNodeIn(handle nodeHandle, const notedShNodesMap& notedSharedNodesMap) const
{
    auto it = find_if(begin(notedSharedNodesMap), end(notedSharedNodesMap),
                        [nodeHandle](const pair<pair<handle, handle>, ff>& element)
                        {
                            const auto& fileAlertTypes = element.second.alertTypePerFileNode;
                            const auto& folderAlertTypes = element.second.alertTypePerFolderNode;
                            return (std::distance(fileAlertTypes.find(nodeHandle), end(fileAlertTypes))
                                || std::distance(folderAlertTypes.find(nodeHandle), end(folderAlertTypes)));
                        });

    return make_pair(it != end(notedSharedNodesMap), std::distance(begin(notedSharedNodesMap), it));
}

bool UserAlerts::containsRemovedNodeAlert(handle nh, const UserAlert::Base* a) const
{
    const UserAlert::RemovedSharedNode* delNodeAlert = dynamic_cast<const UserAlert::RemovedSharedNode*>(a);
    if (!delNodeAlert) return false;

    return (find(begin(delNodeAlert->nodeHandles), end(delNodeAlert->nodeHandles), nh)
            != end(delNodeAlert->nodeHandles));
}

UserAlert::NewSharedNodes* UserAlerts::eraseNodeHandleFromNewShareNodeAlert(handle nh, UserAlert::Base* a)
{
    UserAlert::NewSharedNodes* nsna = dynamic_cast<UserAlert::NewSharedNodes*>(a);

    if (nsna)
    {
        auto it = find(begin(nsna->fileNodeHandles), end(nsna->fileNodeHandles), nh);
        if (it != end(nsna->fileNodeHandles))
        {
            nsna->fileNodeHandles.erase(it);
            return nsna;
        }
        // no need to check nsna->folderNodeHandles since folders do not support versioning
    }

    return nullptr;
}

UserAlert::RemovedSharedNode* UserAlerts::eraseNodeHandleFromRemovedSharedNode(handle nh, UserAlert::Base* a)
{
    UserAlert::RemovedSharedNode* rsna = dynamic_cast<UserAlert::RemovedSharedNode*>(a);

    if (rsna)
    {
        auto it = find(begin(rsna->nodeHandles), end(rsna->nodeHandles), nh);
        if (it != end(rsna->nodeHandles))
        {
            rsna->nodeHandles.erase(it);
            return rsna;
        }
    }

    return nullptr;
}

bool UserAlerts::isSharedNodeNotedAsRemoved(handle nodeHandleToFind) const
{
    // check first in the stash
    return isSharedNodeNotedAsRemovedFrom(nodeHandleToFind, deletedSharedNodesStash)
        || isSharedNodeNotedAsRemovedFrom(nodeHandleToFind, notedSharedNodes);
}

bool UserAlerts::isSharedNodeNotedAsRemovedFrom(handle nodeHandleToFind,
                                                const notedShNodesMap& notedSharedNodesMap) const
{
    using handletoalert_t = UserAlert::handle_alerttype_map_t;
    if (catchupdone && notingSharedNodes)
    {
        auto itToNotedSharedNodes = find_if(begin(notedSharedNodesMap),
                                                 end(notedSharedNodesMap),
        [nodeHandleToFind](const pair<pair<handle, handle>, ff>& element)
        {
            const handletoalert_t& fileAlertTypes = element.second.alertTypePerFileNode;
            auto itToFileNodeHandleAndAlertType = fileAlertTypes.find(nodeHandleToFind);
            const handletoalert_t& folderAlertTypes = element.second.alertTypePerFolderNode;
            auto itToFolderNodeHandleAndAlertType = folderAlertTypes.find(nodeHandleToFind);

            bool isInFileNodes = ((itToFileNodeHandleAndAlertType != end(fileAlertTypes))
                                  && (itToFileNodeHandleAndAlertType->second == UserAlert::type_d));

            // shortcircuit in case it was already found
            bool isInFolderNodes = isInFileNodes ||
                ((itToFolderNodeHandleAndAlertType != end(folderAlertTypes))
                  && (itToFolderNodeHandleAndAlertType->second == UserAlert::type_d));

            return (isInFileNodes || isInFolderNodes);
        });

        return itToNotedSharedNodes != end(notedSharedNodesMap);
    }
    return false;
}

bool UserAlerts::removeNotedSharedNodeFrom(notedShNodesMap::iterator itToStashedNodeToRemove, Node* nodeToRemove, notedShNodesMap& notedSharedNodesMap)
{
    if (itToStashedNodeToRemove != end(notedSharedNodesMap))
    {
        ff& f = itToStashedNodeToRemove->second;
        if (nodeToRemove->type == FOLDERNODE)
        {
            f.alertTypePerFolderNode.erase(nodeToRemove->nodehandle);
        }
        else if (nodeToRemove->type == FILENODE)
        {
            f.alertTypePerFileNode.erase(nodeToRemove->nodehandle);
        }
        // there shouldn't be any other type

        if (f.alertTypePerFolderNode.empty() && f.alertTypePerFileNode.empty())
        {
            notedSharedNodesMap.erase(itToStashedNodeToRemove);
        }
        return true;
    }
    return false;
}

bool UserAlerts::removeNotedSharedNodeFrom(Node* n, notedShNodesMap& notedSharedNodesMap)
{
    if (catchupdone && notingSharedNodes)
    {
        auto found = findNotedSharedNodeIn(n->nodehandle, notedSharedNodesMap);
        if (found.first)
        {
            auto it = notedSharedNodesMap.begin();
            std::advance(it, found.second);
            return removeNotedSharedNodeFrom(it, n, notedSharedNodesMap);
        }
    }
    return false;
}

bool UserAlerts::setNotedSharedNodeToUpdate(Node* nodeToChange)
{
    // noted nodes stash contains only deleted noted nodes, thus, we only check noted nodes map
    if (catchupdone && notingSharedNodes && !notedSharedNodes.empty())
    {
        auto found = findNotedSharedNodeIn(nodeToChange->nodehandle, notedSharedNodes);
        if (!found.first) return false;
        auto itToNotedSharedNodes = notedSharedNodes.begin();
        std::advance(itToNotedSharedNodes, found.second);
        if (itToNotedSharedNodes == end(notedSharedNodes)) return false;

        add(new UserAlert::UpdatedSharedNode(itToNotedSharedNodes->first.first,
                                             itToNotedSharedNodes->second.timestamp,
                                             nextId(),
                                             {nodeToChange->nodehandle}));
        if (removeNotedSharedNodeFrom(itToNotedSharedNodes, nodeToChange, notedSharedNodes))
        {
            LOG_debug << "Node with node handle |" << nodeToChange->nodehandle << "| removed from annotated node add-alerts and update-alert created in its place";
        }
        return true;
    }
    return false;
}

bool UserAlerts::isHandleInAlertsAsRemoved(handle nodeHandleToFind) const
{
    std::function<bool (UserAlert::Base*)> isAlertWithTypeRemoved =
        [nodeHandleToFind, this](UserAlert::Base* alertToCheck)
            {
                return !alertToCheck->removed() && containsRemovedNodeAlert(nodeHandleToFind, alertToCheck);
            };

    std::string debug_msg = "Found removal-alert with nodehandle |" + std::to_string(nodeHandleToFind) + "| in ";
    // check in existing alerts
    if (find_if(begin(alerts), end(alerts), isAlertWithTypeRemoved) != end(alerts))
    {
        LOG_debug << debug_msg << "alerts";
        return true;
    }

    // check in existing notifications meant to become alerts
    if (find_if(begin(useralertnotify), end(useralertnotify), isAlertWithTypeRemoved)
        != end(useralertnotify))
    {
        LOG_debug << debug_msg << "useralertnotify";
        return true;
    }

    // check in annotated changes pending to become notifications to become alerts
    if (isSharedNodeNotedAsRemoved(nodeHandleToFind))
    {
        LOG_debug << debug_msg << "stash or noted nodes";
        return true;
    }

    return false;
}

void UserAlerts::removeNodeAlerts(Node* nodeToRemoveAlert)
{
    if (!nodeToRemoveAlert)
    {
        LOG_err << "Unable to remove alerts for node. Empty Node* passed.";
        return;
    }

    // Remove nodehandle for NewShareNodes and/or RemovedSharedNodes, releasing the alert if gets empty
    handle nodeHandleToRemove = nodeToRemoveAlert->nodehandle;
    std::string debug_msg = "Suppressed alert for node with handle |"
        + toNodeHandle(nodeHandleToRemove) + "| found as a ";
    for (UserAlert::Base* alertToCheck : alerts)
    {
        if (auto pNewSN = eraseNodeHandleFromNewShareNodeAlert(nodeHandleToRemove, alertToCheck))
        {
            LOG_debug << debug_msg << "new-alert type";
            if (pNewSN->fileNodeHandles.empty() && pNewSN->folderNodeHandles.empty())
            {
                pNewSN->setRemoved();
            }

            notifyAlert(pNewSN, pNewSN->seen(), pNewSN->tag);
        }
        else if (auto pRemovedSN = eraseNodeHandleFromRemovedSharedNode(nodeHandleToRemove, alertToCheck))
        {
            LOG_debug << debug_msg << "removal-alert type";
            if (pRemovedSN->nodeHandles.empty())
            {
                pRemovedSN->setRemoved();
            }

            notifyAlert(pRemovedSN, pRemovedSN->seen(), pRemovedSN->tag); // do not touch seen or tag // was updated, so persist it
        }
    }

    // remove from annotated changes pending to become notifications to become alerts
    if (removeNotedSharedNodeFrom(nodeToRemoveAlert, deletedSharedNodesStash))
    {
        LOG_debug << debug_msg << "removal-alert in the stash";
    }
    if (removeNotedSharedNodeFrom(nodeToRemoveAlert, notedSharedNodes))
    {
        LOG_debug << debug_msg << "new-alert in noted nodes";
    }
}

void UserAlerts::setNewNodeAlertToUpdateNodeAlert(Node* nodeToUpdate)
{
    if (!nodeToUpdate)
    {
        LOG_err << "Unable to set alert new-alert node to update-alert. Empty node* passed";
        return;
    }

    // Remove nodehandle for NewShareNodes that are an update; if the alert is empty after the
    // removal, it must be released
    handle nodeHandleToUpdate = nodeToUpdate->nodehandle;
    std::string debug_msg = "New-alert replaced by update-alert for nodehandle |"
        + std::to_string(nodeHandleToUpdate) + "|";
    vector<UserAlert::NewSharedNodes*> newSNToConvertToUpdatedSN;
    for (UserAlert::Base* alertToCheck : alerts)
    {
        bool ret = false;
        if (auto pNewSN = eraseNodeHandleFromNewShareNodeAlert(nodeHandleToUpdate, alertToCheck))
        {
            bool emptyAlert = pNewSN->fileNodeHandles.empty() && pNewSN->folderNodeHandles.empty();
            LOG_debug << debug_msg << " there are " << (ret ? "no " : "") << " remaining alters for this folder";

            if (emptyAlert) pNewSN->setRemoved();
            newSNToConvertToUpdatedSN.push_back(pNewSN);
        }
    }

    // Create proper UpdateSharedNodes
    for(auto n : newSNToConvertToUpdatedSN)
    {
        // for an update alert, only files are relevant because folders are not versioned
        add(new UserAlert::UpdatedSharedNode(n->user(), n->ts(), nextId(), {nodeHandleToUpdate}));
    }
    newSNToConvertToUpdatedSN.clear();

    // Remove NewSharedNode alert from noted node alerts
    if (setNotedSharedNodeToUpdate(nodeToUpdate))
    {
        LOG_verbose << debug_msg << " new-alert found in noted nodes";
    }
}

void UserAlerts::purgeNodeVersionsFromStash()
{
    auto& stash = deletedSharedNodesStash;
    if (stash.empty()) return;

    std::vector<notedShNodesMap::const_iterator> vers;
    for(auto stashIt = std::begin(stash); stashIt != std::end(stash); ++stashIt)
    {
        if (stashIt->second.areNodeVersions()) vers.push_back(stashIt);
    }

    std::for_each(std::begin(vers), std::end(vers), [&stash](notedShNodesMap::const_iterator it) { stash.erase(it); });
}

void UserAlerts::convertStashedDeletedSharedNodes()
{
    if (deletedSharedNodesStash.empty()) return;

    notedSharedNodes = deletedSharedNodesStash;
    deletedSharedNodesStash.clear();

    convertNotedSharedNodes(false);

    clearNotedSharedMembers();
    LOG_debug << "Removal-alert noted-nodes stashed alert notifications converted to notifications";
}

bool UserAlerts::isDeletedSharedNodesStashEmpty() const
{
    return deletedSharedNodesStash.empty();
}

void UserAlerts::stashDeletedNotedSharedNodes(handle originatingUser)
{
    if (isConvertReadyToAdd(originatingUser))
    {
        std::for_each(std::begin(notedSharedNodes), std::end(notedSharedNodes), [this](const std::pair<std::pair<handle, handle>, ff> p)
        {
            deletedSharedNodesStash[p.first].squash(p.second);
        });
    }

    clearNotedSharedMembers();
    LOG_verbose << "Removal-alert noted-nodes alert notifications stashed";
}

// process server-client notifications
bool UserAlerts::procsc_useralert(JSON& jsonsc)
{
    for (;;)
    {
        switch (jsonsc.getnameid())
        {
        case name_id::u:
            if (jsonsc.enterarray())
            {
                for (;;)
                {
                    UserAlertPendingContact ul;
                    if (jsonsc.enterobject())
                    {
                        bool inobject = true;
                        while (inobject)
                        {
                            switch (jsonsc.getnameid())
                            {
                            case name_id::u:
                                ul.u = jsonsc.gethandle(MegaClient::USERHANDLE);
                                break;
                            case 'm':
                                jsonsc.storeobject(&ul.m);
                                break;
                            case MAKENAMEID2('m', '2'):
                                if (jsonsc.enterarray())
                                {
                                    for (;;)
                                    {
                                        string s;
                                        if (jsonsc.storeobject(&s))
                                        {
                                            ul.m2.push_back(s);
                                        }
                                        else
                                        {
                                            break;
                                        }
                                    }
                                    jsonsc.leavearray();
                                }
                                break;
                            case 'n':
                                jsonsc.storeobject(&ul.n);
                                break;
                            case EOO:
                                inobject = false;
                            }
                        }
                        jsonsc.leaveobject();
                        if (ul.u)
                        {
                            pendingContactUsers[ul.u] = ul;
                        }
                    }
                    else
                    {
                        break;
                    }
                }
                jsonsc.leavearray();
            }
            break;

        case MAKENAMEID3('l', 's', 'n'):
            lsn = jsonsc.gethandle(8);
            break;

        case MAKENAMEID3('f', 's', 'n'):
            fsn = jsonsc.gethandle(8);
            break;

        case MAKENAMEID3('l', 't', 'd'):   // last notifcation seen time delta (or 0)
            lastTimeDelta = jsonsc.getint();
            break;

        case EOO:
            for (Alerts::iterator i = alerts.begin(); i != alerts.end(); ++i)
            {
                UserAlert::Base* b = *i;
                b->setSeen(b->ts() + lastTimeDelta < m_time());

                if (b->email().empty() && b->user() != UNDEF)
                {
                    map<handle, UserAlertPendingContact>::iterator i = pendingContactUsers.find(b->user());
                    if (i != pendingContactUsers.end())
                    {
                        b->setEmail(i->second.m);
                        if (b->email().empty() && !i->second.m2.empty())
                        {
                            b->setEmail(i->second.m2[0]);
                        }
                    }
                }
            }
            initscalerts();
            begincatchup = false;
            catchupdone = true;
            return true;

        case name_id::c: // notifications
            if (jsonsc.enterarray())
            {
                for (;;)
                {
                    if (jsonsc.enterobject())
                    {
                        UserAlertRaw un;
                        bool inobject = true;
                        while (inobject)
                        {
                            // 't' designates type - but it appears late in the packet
                            nameid nid = jsonsc.getnameid();
                            switch (nid)
                            {

                            case 't':
                                un.t = jsonsc.getnameid();
                                break;

                            case EOO:
                                inobject = false;
                                break;

                            default:
                                // gather up the fields to interpret later as we don't know what type some are
                                // until we get the 't' field which is late in the packet
                                jsonsc.storeobject(&un.fields[nid]);
                            }
                        }

                        if (!isUnwantedAlert(un.t, un.getint(name_id::c, -1)))
                        {
                            add(un);
                        }
                        jsonsc.leaveobject();
                    }
                    else
                    {
                        break;
                    }
                }
                jsonsc.leavearray();
                break;
            }

            // fall through
        default:
            assert(false);
            if (!jsonsc.storeobject())
            {
                LOG_err << "Error parsing sc user alerts";
                begincatchup = false;
                catchupdone = true;  // if we fail to get useralerts, continue anyway
                return true;
            }
        }
    }
}

void UserAlerts::acknowledgeAll()
{
    // notify the API.  Eg. on when user closes the useralerts list
    mc.reqs.add(new CommandSetLastAcknowledged(&mc));
}

void UserAlerts::acknowledgeAllSucceeded()
{
    for (auto& a : alerts)
    {
        if (!a->seen())
        {
            notifyAlert(a, true, mc.reqtag);
        }
    }
}

void UserAlerts::onAcknowledgeReceived()
{
    for (auto& a : alerts)
    {
        if (!a->seen())
        {
            notifyAlert(a, true, 0);
        }
    }
}

void UserAlerts::clear()
{
    useralertnotify.clear();

    for (Alerts::iterator i = alerts.begin(); i != alerts.end(); ++i)
    {
        delete *i;
    }
    alerts.clear();

    begincatchup = false;
    catchupdone = false;
    catchup_last_timestamp = 0;
    lsn = UNDEF;
    fsn = UNDEF;
    lastTimeDelta = 0;
    nextid = 0;
}

UserAlerts::~UserAlerts()
{
    clear();
}

bool UserAlerts::unserializeAlert(string* d, uint32_t dbid)
{
    nameid type = 0;
    CacheableReader r(*d);
    if (!r.unserializecompressedu64(type))
    {
        return false;
    }
    r.eraseused(*d);

    UserAlert::Base* a = nullptr;

    switch (type)
    {
    case UserAlert::type_ipc:
        a = UserAlert::IncomingPendingContact::unserialize(d, nextId());
        break;

    case UserAlert::type_c:
        a = UserAlert::ContactChange::unserialize(d, nextId());
        break;

    case UserAlert::type_upci:
        a = UserAlert::UpdatedPendingContactIncoming::unserialize(d, nextId());
        break;

    case UserAlert::type_upco:
        a = UserAlert::UpdatedPendingContactOutgoing::unserialize(d, nextId());
        break;

    case UserAlert::type_share:
        a = UserAlert::NewShare::unserialize(d, nextId());
        break;

    case UserAlert::type_dshare:
        a = UserAlert::DeletedShare::unserialize(d, nextId());
        break;

    case UserAlert::type_put:
        a = UserAlert::NewSharedNodes::unserialize(d, nextId());
        break;

    case UserAlert::type_d:
        a = UserAlert::RemovedSharedNode::unserialize(d, nextId());
        break;

    case UserAlert::type_u:
        a = UserAlert::UpdatedSharedNode::unserialize(d, nextId());
        break;

    case UserAlert::type_psts:
    case UserAlert::type_psts_v2:
        a = UserAlert::Payment::unserialize(d, nextId(), type);
        break;

    case UserAlert::type_pses:
        a = UserAlert::PaymentReminder::unserialize(d, nextId());
        break;

    case UserAlert::type_ph:
        a = UserAlert::Takedown::unserialize(d, nextId());
        break;

#ifdef ENABLE_CHAT
    case UserAlert::type_nusm:
        // this method disambiguates between NewScheduledMeeting and UpdatedScheduledMeeting
        a = UserAlert::unserializeNewUpdSched(d, nextId());
        assert(a);
        break;

    case UserAlert::type_dsm:
        a = UserAlert::DeletedScheduledMeeting::unserialize(d, nextId());
        break;
#endif
    }

    if (a)
    {
        a->dbid = dbid;
        add(a); // takes ownership of a
        return true;
    }

    return false;
}

void UserAlerts::initscalerts() // called after sc50 response has been received
{
    // Alerts are not critical. There is no need to break execution if db ops failed for some (rare) reason
    for (auto& a : alerts)
    {
        mc.persistAlert(a);
    }
}

void UserAlerts::purgescalerts() // called from MegaClient::notifypurge()
{
    if (useralertnotify.empty())
    {
        return; // don't just loop `alerts` every time
    }
    assert(catchupdone);

    trimAlertsToMaxCount();

    // send notification for all current alerts, even if some overflowed already
    LOG_debug << "Notifying " << useralertnotify.size() << " user alerts";
    mc.app->useralerts_updated(&useralertnotify[0], (int)useralertnotify.size());

    for (auto a : useralertnotify)
    {
        mc.persistAlert(a); // persist to db (add/update/remove)

        if (a->removed())
        {
            auto it = find(alerts.begin(), alerts.end(), a);
            assert(it != alerts.end());
            alerts.erase(it);

            delete a;
        }
        else
        {
            a->notified = false;
        }
    }

    useralertnotify.clear();
}

void UserAlerts::trimAlertsToMaxCount()
{
    static constexpr size_t maxAlertCount = 200; // value mentioned in the requirements

    if (alerts.size() < maxAlertCount)  return;

    size_t kept = 0;
    for (auto& a : alerts)
    {
        if (a->removed())   continue; // it's going to be removed, don't take it into account

        if (kept < maxAlertCount)
        {
            ++kept;
        }
        else
        {
            a->setRemoved();
            notifyAlert(a, a->seen(), a->tag);
        }
    }
}

void UserAlerts::notifyAlert(UserAlert::Base *alert, bool seen, int tag)
{
    // skip notifications until up to date
    if (!catchupdone)   return;

    alert->setSeen(seen);
    alert->tag = tag;

    if (!alert->notified)
    {

        alert->notified = true;
        useralertnotify.push_back(alert);
    }
}
} // namespace
