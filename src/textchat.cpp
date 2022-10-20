/**
* @file textchat.cpp
* @brief Class for manipulating text chat
*
* (c) 2013-2022 by Mega Limited, Wellsford, New Zealand
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

#include "mega/textchat.h"
#include "mega/utils.h"
#include "mega/megaclient.h"
#include "mega/base64.h"

namespace mega {

#ifdef ENABLE_CHAT
TextChat::TextChat()
{
    id = UNDEF;
    priv = PRIV_UNKNOWN;
    shard = -1;
    userpriv = NULL;
    group = false;
    ou = UNDEF;
    resetTag();
    ts = 0;
    flags = 0;
    publicchat = false;
    chatOptions = 0;

    memset(&changed, 0, sizeof(changed));
}

TextChat::~TextChat()
{
    delete userpriv;
}

bool TextChat::serialize(string *d)
{
    unsigned short ll;

    d->append((char*)&id, sizeof id);
    d->append((char*)&priv, sizeof priv);
    d->append((char*)&shard, sizeof shard);

    ll = (unsigned short)(userpriv ? userpriv->size() : 0);
    d->append((char*)&ll, sizeof ll);
    if (userpriv)
    {
        userpriv_vector::iterator it = userpriv->begin();
        while (it != userpriv->end())
        {
            handle uh = it->first;
            d->append((char*)&uh, sizeof uh);

            privilege_t priv = it->second;
            d->append((char*)&priv, sizeof priv);

            it++;
        }
    }

    d->append((char*)&group, sizeof group);

    // title is a binary array
    ll = (unsigned short)title.size();
    d->append((char*)&ll, sizeof ll);
    d->append(title.data(), ll);

    d->append((char*)&ou, sizeof ou);
    d->append((char*)&ts, sizeof(ts));

    char hasAttachments = attachedNodes.size() != 0;
    d->append((char*)&hasAttachments, 1);

    d->append((char*)&flags, 1);

    char mode = publicchat ? 1 : 0;
    d->append((char*)&mode, 1);

    char hasUnifiedKey = unifiedKey.size() ? 1 : 0;
    d->append((char *)&hasUnifiedKey, 1);

    char meetingRoom = meeting ? 1 : 0;
    d->append((char*)&meetingRoom, 1);

    d->append((char*)&chatOptions, 1);

    char hasSheduledMeetings = !mScheduledMeetings.empty() ? 1 : 0;
    d->append((char*)&hasSheduledMeetings, 1);

    d->append("\0\0\0", 3); // additional bytes for backwards compatibility

    if (hasAttachments)
    {
        ll = (unsigned short)attachedNodes.size();  // number of nodes with granted access
        d->append((char*)&ll, sizeof ll);

        for (attachments_map::iterator it = attachedNodes.begin(); it != attachedNodes.end(); it++)
        {
            d->append((char*)&it->first, sizeof it->first); // nodehandle

            ll = (unsigned short)it->second.size(); // number of users with granted access to the node
            d->append((char*)&ll, sizeof ll);
            for (set<handle>::iterator ituh = it->second.begin(); ituh != it->second.end(); ituh++)
            {
                d->append((char*)&(*ituh), sizeof *ituh);   // userhandle
            }
        }
    }

    if (hasUnifiedKey)
    {
        ll = (unsigned short) unifiedKey.size();
        d->append((char *)&ll, sizeof ll);
        d->append((char*) unifiedKey.data(), unifiedKey.size());
    }

    if (hasSheduledMeetings)
    {
        if (mScheduledMeetingsOcurrences.size() && mScheduledMeetings.empty())
        {
            LOG_err << "The chat to serialize [" << Base64Str<MegaClient::CHATHANDLE>(id) << "] has scheduled meetings occurrences, but not scheduled meetings";
            assert(false);
        }

        // serialize the number of scheduledMeetings
        ll = (unsigned short) mScheduledMeetings.size();
        d->append((char *)&ll, sizeof ll);

        for (auto i = mScheduledMeetings.begin(); i != mScheduledMeetings.end(); i++)
        {
            std::string schedMeetingStr;
            if (i->second->serialize(&schedMeetingStr))
            {
                ll = (unsigned short) schedMeetingStr.size();
                d->append((char *)&ll, sizeof ll);
                d->append((char *)schedMeetingStr.data(), schedMeetingStr.size());
            }
        }

        // serialize the number of scheduledMeetings occurrences
        ll = (unsigned short) mScheduledMeetingsOcurrences.size();
        d->append((char *)&ll, sizeof ll);
        for (auto i = mScheduledMeetingsOcurrences.begin(); i != mScheduledMeetingsOcurrences.end(); i++)
        {
            std::string schedMeetingStr;
            if (i->second->serialize(&schedMeetingStr))
            {
                ll = (unsigned short) schedMeetingStr.size();
                d->append((char *)&ll, sizeof ll);
                d->append((char *)schedMeetingStr.data(), schedMeetingStr.size());
            }
        }
    }
    return true;
}

TextChat* TextChat::unserialize(class MegaClient *client, string *d)
{
    handle id;
    privilege_t priv;
    int shard;
    userpriv_vector *userpriv = NULL;
    bool group;
    string title;   // byte array
    handle ou;
    m_time_t ts;
    byte flags;
    char hasAttachments;
    attachments_map attachedNodes;
    bool publicchat;
    string unifiedKey;
    std::vector<string> scheduledMeetingsStr;
    std::vector<string> scheduledMeetingsOccurrStr;

    unsigned short ll;
    const char* ptr = d->data();
    const char* end = ptr + d->size();

    if (ptr + sizeof(handle) + sizeof(privilege_t) + sizeof(int) + sizeof(short) > end)
    {
        return NULL;
    }

    id = MemAccess::get<handle>(ptr);
    ptr += sizeof id;

    priv = MemAccess::get<privilege_t>(ptr);
    ptr += sizeof priv;

    shard = MemAccess::get<int>(ptr);
    ptr += sizeof shard;

    ll = MemAccess::get<unsigned short>(ptr);
    ptr += sizeof ll;
    if (ll)
    {
        if (ptr + ll * (sizeof(handle) + sizeof(privilege_t)) > end)
        {
            return NULL;
        }

        userpriv = new userpriv_vector();

        for (unsigned short i = 0; i < ll; i++)
        {
            handle uh = MemAccess::get<handle>(ptr);
            ptr += sizeof uh;

            privilege_t priv = MemAccess::get<privilege_t>(ptr);
            ptr += sizeof priv;

            userpriv->push_back(userpriv_pair(uh, priv));
        }

        if (priv == PRIV_RM)    // clear peerlist if removed
        {
            delete userpriv;
            userpriv = NULL;
        }
    }

    if (ptr + sizeof(bool) + sizeof(unsigned short) > end)
    {
        delete userpriv;
        return NULL;
    }

    group = MemAccess::get<bool>(ptr);
    ptr += sizeof group;

    ll = MemAccess::get<unsigned short>(ptr);
    ptr += sizeof ll;
    if (ll)
    {
        if (ptr + ll > end)
        {
            delete userpriv;
            return NULL;
        }
        title.assign(ptr, ll);
    }
    ptr += ll;

    if (ptr + sizeof(handle) + sizeof(m_time_t) + sizeof(char) + 9 > end)
    {
        delete userpriv;
        return NULL;
    }

    ou = MemAccess::get<handle>(ptr);
    ptr += sizeof ou;

    ts = MemAccess::get<m_time_t>(ptr);
    ptr += sizeof(m_time_t);

    hasAttachments = MemAccess::get<char>(ptr);
    ptr += sizeof hasAttachments;

    flags = MemAccess::get<char>(ptr);
    ptr += sizeof(char);

    char mode = MemAccess::get<char>(ptr);
    publicchat = (mode == 1);
    ptr += sizeof(char);

    char hasUnifiedKey = MemAccess::get<char>(ptr);
    ptr += sizeof(char);

    char meetingRoom = MemAccess::get<char>(ptr);
    ptr += sizeof(char);

    byte chatOptions = static_cast<byte>(MemAccess::get<char>(ptr));
    ptr += sizeof(char);

    char hasScheduledMeeting = MemAccess::get<char>(ptr);
    ptr += sizeof(char);

    for (int i = 3; i--;)
    {
        if (ptr + MemAccess::get<unsigned char>(ptr) < end)
        {
            ptr += MemAccess::get<unsigned char>(ptr) + 1;
        }
    }

    if (hasAttachments)
    {
        unsigned short numNodes = 0;
        if (ptr + sizeof numNodes > end)
        {
            delete userpriv;
            return NULL;
        }

        numNodes = MemAccess::get<unsigned short>(ptr);
        ptr += sizeof numNodes;

        for (int i = 0; i < numNodes; i++)
        {
            handle h = UNDEF;
            unsigned short numUsers = 0;
            if (ptr + sizeof h + sizeof numUsers > end)
            {
                delete userpriv;
                return NULL;
            }

            h = MemAccess::get<handle>(ptr);
            ptr += sizeof h;

            numUsers = MemAccess::get<unsigned short>(ptr);
            ptr += sizeof numUsers;

            handle uh = UNDEF;
            if (ptr + (numUsers * sizeof(uh)) > end)
            {
                delete userpriv;
                return NULL;
            }

            for (int j = 0; j < numUsers; j++)
            {
                uh = MemAccess::get<handle>(ptr);
                ptr += sizeof uh;

                attachedNodes[h].insert(uh);
            }
        }
    }

    if (hasUnifiedKey)
    {
        unsigned short keylen = 0;
        if (ptr + sizeof keylen > end)
        {
            delete userpriv;
            return NULL;
        }

        keylen = MemAccess::get<unsigned short>(ptr);
        ptr += sizeof keylen;

        if (ptr + keylen > end)
        {
            delete userpriv;
            return NULL;
        }

        unifiedKey.assign(ptr, keylen);
        ptr += keylen;
    }

    if (hasScheduledMeeting)
    {
        // unserialize the number of scheduled meetings
        unsigned short schedMeetingsSize = 0;
        if (ptr + sizeof schedMeetingsSize > end)
        {
            delete userpriv;
            return NULL;
        }

        schedMeetingsSize = MemAccess::get<unsigned short>(ptr);
        ptr += sizeof schedMeetingsSize;

        for (auto i = 0; i < schedMeetingsSize; i++)
        {
            unsigned short len = 0;
            if (ptr + sizeof len > end)
            {
                delete userpriv;
                return NULL;
            }

            len = MemAccess::get<unsigned short>(ptr);
            ptr += sizeof len;

            if (ptr + len > end)
            {
                delete userpriv;
                return NULL;
            }

            std::string aux(ptr, len);
            scheduledMeetingsStr.emplace_back(aux);
            ptr += len;
        }

        // unserialize the number of scheduled meetings occurrences
        unsigned short schedMeetingsOccurrSize = 0;
        if (ptr + sizeof schedMeetingsOccurrSize > end)
        {
            delete userpriv;
            return NULL;
        }

        schedMeetingsOccurrSize = MemAccess::get<unsigned short>(ptr);
        ptr += sizeof schedMeetingsOccurrSize;

        for (auto i = 0; i < schedMeetingsOccurrSize; i++)
        {
            unsigned short len = 0;
            if (ptr + sizeof len > end)
            {
                delete userpriv;
                return NULL;
            }

            len = MemAccess::get<unsigned short>(ptr);
            ptr += sizeof len;

            if (ptr + len > end)
            {
                delete userpriv;
                return NULL;
            }

            std::string aux(ptr, len);
            scheduledMeetingsOccurrStr.emplace_back(aux);
            ptr += len;
        }
    }

    if (scheduledMeetingsOccurrStr.size() && scheduledMeetingsStr.empty())
    {
        LOG_err << "The unserialized chat [" << Base64Str<MegaClient::CHATHANDLE>(id) << "] has scheduled meetings occurrences, but not scheduled meetings";
        assert(false);
        delete userpriv;
        return NULL;
    }

    if (ptr < end)
    {
        delete userpriv;
        return NULL;
    }

    if (client->chats.find(id) == client->chats.end())
    {
        client->chats[id] = new TextChat();
    }
    else
    {
        LOG_warn << "Unserialized a chat already in RAM";
    }
    TextChat* chat = client->chats[id];
    chat->id = id;
    chat->priv = priv;
    chat->shard = shard;
    chat->userpriv = userpriv;
    chat->group = group;
    chat->title = title;
    chat->ou = ou;
    chat->resetTag();
    chat->ts = ts;
    chat->flags = flags;
    chat->attachedNodes = attachedNodes;
    chat->publicchat = publicchat;
    chat->unifiedKey = unifiedKey;
    chat->meeting = meetingRoom;
    chat->chatOptions = chatOptions;

    for (auto i: scheduledMeetingsStr)
    {
        std::unique_ptr<ScheduledMeeting> auxMeet(ScheduledMeeting::unserialize(&i));
        if (auxMeet)
        {
            chat->addSchedMeeting(auxMeet.get(), false /*notify*/);
        }
    }

    for (auto i: scheduledMeetingsOccurrStr)
    {
        std::unique_ptr<ScheduledMeeting> auxMeet(ScheduledMeeting::unserialize(&i));
        if (auxMeet)
        {
            chat->addSchedMeetingOccurrence(std::move(auxMeet));
        }
    }

    memset(&chat->changed, 0, sizeof(chat->changed));

    return chat;
}

void TextChat::setTag(int tag)
{
    if (this->tag != 0)    // external changes prevail
    {
        this->tag = tag;
    }
}

int TextChat::getTag()
{
    return tag;
}

void TextChat::resetTag()
{
    tag = -1;
}

bool TextChat::setNodeUserAccess(handle h, handle uh, bool revoke)
{
    if (revoke)
    {
        attachments_map::iterator uhit = attachedNodes.find(h);
        if (uhit != attachedNodes.end())
        {
            uhit->second.erase(uh);
            if (uhit->second.empty())
            {
                attachedNodes.erase(h);
                changed.attachments = true;
            }
            return true;
        }
    }
    else
    {
        attachedNodes[h].insert(uh);
        changed.attachments = true;
        return true;
    }

    return false;
}

bool TextChat::setFlags(byte newFlags)
{
    if (flags == newFlags)
    {
        return false;
    }

    flags = newFlags;
    changed.flags = true;

    return true;
}

bool TextChat::isFlagSet(uint8_t offset) const
{
    return (flags >> offset) & 1U;
}

void TextChat::addSchedMeetingOccurrence(std::unique_ptr<ScheduledMeeting>&& sm)
{
    mScheduledMeetingsOcurrences.emplace(sm->callid(), std::move(sm));
}

void TextChat::clearSchedMeetingOccurrences()
{
    mScheduledMeetingsOcurrences.clear();
}

ScheduledMeeting* TextChat::getSchedMeetingById(handle id)
{
    auto it = mScheduledMeetings.find(id);
    if (it != mScheduledMeetings.end())
    {
        return it->second.get();
    }
    return nullptr;
}

bool TextChat::addSchedMeeting(const ScheduledMeeting *sm, bool notify)
{
    if (!sm)
    {
        assert(false);
        return false;
    }

    handle h = sm->callid();
    if (mScheduledMeetings.find(h) != mScheduledMeetings.end())
    {
        LOG_err << "addSchedMeeting: scheduled meeting with id: " << Base64Str<MegaClient::CHATHANDLE>(h) << " already exits";
        return false;
    }

    mScheduledMeetings.emplace(h, std::unique_ptr<ScheduledMeeting>(sm->copy()));
    if (notify)
    {
        mSchedMeetingsChanged.emplace_back(h);
    }
    return true;
}

bool TextChat::removeSchedMeeting(handle callid)
{
    assert(callid != UNDEF);
    if (mScheduledMeetings.find(callid) == mScheduledMeetings.end())
    {
        LOG_err << "removeSchedMeeting: scheduled meeting with id: " << Base64Str<MegaClient::CHATHANDLE>(callid) << " no longer exists";
        return false;
    }

    mScheduledMeetings.erase(callid);
    mSchedMeetingsChanged.emplace_back(callid);
    return true;
}

unsigned int TextChat::removeChildSchedMeetings(handle parentCallid)
{
    // remove all scheduled meeting whose parent is parentCallid
    unsigned int count = 0;
    for (auto it = mScheduledMeetings.begin(); it != mScheduledMeetings.end(); it++)
    {
        if (it->second->parentCallid() == parentCallid)
        {
            removeSchedMeeting(it->second->callid());
            count++;
        }
    }

    return count;
}

bool TextChat::updateSchedMeeting(const ScheduledMeeting *sm)
{
    assert(sm);
    auto it = mScheduledMeetings.find(sm->callid());
    if (it == mScheduledMeetings.end())
    {
        LOG_err << "updateSchedMeeting: scheduled meeting with id: " << Base64Str<MegaClient::CHATHANDLE>(sm->callid()) << " no longer exists";
        return false;
    }

    // compare current scheduled meeting with received from API
    if (!sm->equalTo(it->second.get()))
    {
        mSchedMeetingsChanged.emplace_back(sm->callid());
        it->second.reset(sm->copy());
    }

    return true;
}

bool TextChat::addOrUpdateSchedMeeting(const ScheduledMeeting* sm)
{
    if (!sm)
    {
        LOG_err << "addOrUpdateSchedMeeting: invalid scheduled meeting provided";
        assert(false);
        return false;
    }

    return mScheduledMeetings.find(sm->callid()) == mScheduledMeetings.end()
            ? addSchedMeeting(sm)
            : updateSchedMeeting(sm);
}

bool TextChat::setMode(bool publicchat)
{
    if (this->publicchat == publicchat)
    {
        return false;
    }

    this->publicchat = publicchat;
    changed.mode = true;

    return true;
}

bool TextChat::addOrUpdateChatOptions(int speakRequest, int waitingRoom, int openInvite)
{
    if (!group)
    {
        LOG_err << "addOrUpdateChatOptions: trying to update chat options for a non groupal chat: " << toNodeHandle(id);
        assert(false);
        return false;
    }

    ChatOptions currentOptions(static_cast<uint8_t>(chatOptions));
    if (speakRequest != -1) { currentOptions.updateSpeakRequest(speakRequest); }
    if (waitingRoom != -1)  { currentOptions.updateWaitingRoom(waitingRoom); }
    if (openInvite != -1)   { currentOptions.updateOpenInvite(openInvite); }

    if (!currentOptions.isValid())
    {
        LOG_err << "addOrUpdateChatOptions: options value (" << currentOptions.value() <<") is out of range";
        assert(false);
        return false;
    }

    if (chatOptions != currentOptions.value()) // just set as changed if value is different`
    {
        chatOptions = currentOptions.value();
        changed.options = true;
    }
    return true;
}

bool TextChat::setFlag(bool value, uint8_t offset)
{
    if (bool((flags >> offset) & 1U) == value)
    {
        return false;
    }

    flags ^= byte(1U << offset);
    changed.flags = true;

    return true;
}
#endif

} //namespace
