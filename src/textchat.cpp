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
bool ScheduledFlags::serialize(string& out) const
{
    CacheableWriter w(out);
    w.serializeu32(static_cast<uint32_t>(mFlags.to_ulong()));
    return true;
}

ScheduledFlags* ScheduledFlags::unserialize(const std::string &in)
{
    if (in.empty())  { return nullptr; }
    uint32_t flagsNum = schedEmptyFlags;
    CacheableReader r(in);
    if (!r.unserializeu32(flagsNum))
    {
        assert(false);
        LOG_err << "ScheduledFlags unserialization failed at field flagsNum";
        return nullptr;
    }

    return new ScheduledFlags(flagsNum);
}

ScheduledRules::ScheduledRules(const int freq, const int interval, const m_time_t until, const rules_vector* byWeekDay,
                               const rules_vector* byMonthDay, const rules_map* byMonthWeekDay)
    : mFreq(isValidFreq(freq) ? static_cast<freq_type_t>(freq) : FREQ_INVALID),
      mInterval(isValidInterval(interval) ? interval : INTERVAL_INVALID),
      mUntil(isValidUntil(until) ? until : mega_invalid_timestamp),
      mByWeekDay(byWeekDay ? std::make_unique<rules_vector>(*byWeekDay) : nullptr),
      mByMonthDay(byMonthDay ? std::make_unique<rules_vector>(*byMonthDay) : nullptr),
      mByMonthWeekDay(byMonthWeekDay ? std::make_unique<rules_map>(*byMonthWeekDay) : nullptr)
{}

ScheduledRules::ScheduledRules(const ScheduledRules* rules)
    : mFreq(isValidFreq(rules->freq()) ? rules->freq() : FREQ_INVALID),
      mInterval(isValidInterval(rules->interval()) ? rules->interval() : INTERVAL_INVALID),
      mUntil(rules->until()),
      mByWeekDay(rules->byWeekDay() ? std::make_unique<rules_vector>(*rules->byWeekDay()) : nullptr),
      mByMonthDay(rules->byMonthDay() ? std::make_unique<rules_vector>(*rules->byMonthDay()) : nullptr),
      mByMonthWeekDay(rules->byMonthWeekDay() ? std::make_unique<rules_map>(*rules->byMonthWeekDay()) : nullptr)
{}

const char* ScheduledRules::freqToString () const
{
    switch (mFreq)
    {
        case 0: return "d";
        case 1: return "w";
        case 2: return "m";
        default: return nullptr;
    }
}

bool ScheduledRules::equalTo(const mega::ScheduledRules *r) const
{
    if (!r)                            { return false; }
    if (freq() != r->freq())            { return false; }
    if (interval() != r->interval())    { return false; }
    if (until() != r->until())          { return false; }

    if (byWeekDay() || r->byWeekDay())
    {
        if (!byWeekDay() || !r->byWeekDay()) { return false; }
        if (*byWeekDay() != *r->byWeekDay()) { return false; }
    }

    if (byMonthDay() || r->byMonthDay())
    {
        if (!byMonthDay() || !r->byMonthDay()) { return false; }
        if (*byMonthDay() != *r->byMonthDay()) { return false; }
    }

    if (mByMonthWeekDay || r->byMonthWeekDay())
    {
        if (!byMonthWeekDay() || !r->byMonthWeekDay()) { return false; }
        if (*byMonthWeekDay() != *r->byMonthWeekDay()) { return false; }
    }

    return true;
}

int ScheduledRules::stringToFreq (const char* freq)
{
    if (strcmp(freq, "d") == 0)   { return FREQ_DAILY; }
    if (strcmp(freq, "w") == 0)   { return FREQ_WEEKLY; }
    if (strcmp(freq, "m") == 0)   { return FREQ_MONTHLY; }
    return FREQ_INVALID;
}

bool ScheduledRules::serialize(string& out) const
{
    assert(isValid());
    const bool hasInterval = isValidInterval(interval());
    const bool hasUntil = isValidUntil(until());
    const bool hasByWeekDay = byWeekDay() && !byWeekDay()->empty();
    const bool hasByMonthDay = byMonthDay() && !byMonthDay()->empty();
    const bool hasByMonthWeekDay = byMonthWeekDay() && !byMonthWeekDay()->empty();

    CacheableWriter w(out);
    w.serializei32(freq());
    w.serializeexpansionflags(hasInterval, hasUntil, hasByWeekDay, hasByMonthDay, hasByMonthWeekDay);

    if (hasInterval) { w.serializei32(interval()); }
    if (hasUntil)    { w.serializei64(until()); }
    const auto serializeSmallIntVector = [&w](const rules_vector& v)
    {
        w.serializeu32(static_cast<uint32_t>(v.size()));
        for (auto i: v)
        {
            w.serializei8(i);
        }
    };
    if (hasByWeekDay)
    {
        serializeSmallIntVector(*byWeekDay());
    }

    if (hasByMonthDay)
    {
        serializeSmallIntVector(*byMonthDay());
    }

    if (hasByMonthWeekDay)
    {
        w.serializeu32(static_cast<uint32_t>(byMonthWeekDay()->size()*2));
        for (auto i: *byMonthWeekDay())
        {
            w.serializei8(i.first);
            w.serializei8(i.second);
        }
    }
    return true;
}

ScheduledRules* ScheduledRules::unserialize(const string& in)
{
    if (in.empty())  { return nullptr; }
    int freq = FREQ_INVALID;
    constexpr unsigned int flagsSize = 5;
    unsigned char expansions[8];  // must be defined with size 8

    const auto logAndFail = [](const std::string& msg) -> ScheduledRules*
    {
        LOG_err << "Failure at schedule meeting rules unserialization " << msg;
        assert(false);
        return nullptr;
    };
    CacheableReader r(in);
    if (!r.unserializei32(freq) || !r.unserializeexpansionflags(expansions, flagsSize))
    {
        return logAndFail(std::string());
    }

    const bool hasInterval = expansions[0] > 0;
    const bool hasUntil = expansions[1] > 0;
    const bool hasByWeekDay = expansions[2] > 0;
    const bool hasByMonthDay = expansions[3] > 0;
    const bool hasByMonthWeekDay = expansions[4] > 0;

    int interval = INTERVAL_INVALID;
    if (hasInterval && !r.unserializei32(interval)) { return logAndFail("interval"); }

    m_time_t until = mega_invalid_timestamp;
    if (hasUntil && !r.unserializei64(until))       { return logAndFail("until");    }

    const auto unserializeVector = [&r, &logAndFail](rules_vector& outVec, const string& errMsg) -> bool
    {
        uint32_t s = 0;
        if (!r.unserializeu32(s))
        {
            return logAndFail(errMsg + " vector size") != nullptr;
        }

        outVec.reserve(s);
        for (size_t i = 0; i < s; ++i)
        {
            int8_t element = 0;
            if (r.unserializei8(element))
            {
                outVec.emplace_back(element);
            }
            else
            {
                return logAndFail(errMsg) != nullptr;
            }
        }

        return true;
    };
    rules_vector byWeekDay;
    if (hasByWeekDay && !unserializeVector(byWeekDay, "byWeekDay"))    { return nullptr; }

    rules_vector byMonthDay;
    if (hasByMonthDay && !unserializeVector(byMonthDay, "byMonthDay")) { return nullptr; }

    rules_map byMonthWeekDay;
    if (hasByMonthWeekDay)
    {
        static const std::string name {"byMonthWeekDay"};
        uint32_t auxSize = 0;
        if (!r.unserializeu32(auxSize))             { return logAndFail(name + " vector size"); }
        if (auxSize % 2)                            { return logAndFail(name + " odd vector size"); }

        auxSize /= 2;
        for (size_t i = 0; i < auxSize; ++i)
        {
            int8_t key = 0, value = 0;
            if (r.unserializei8(key) && r.unserializei8(value))
            {
                byMonthWeekDay.emplace(key, value);
            }
            else                                    { return logAndFail(name); }
        }
    }

    return new ScheduledRules(freq,
                              hasInterval ? interval : -1,
                              until,
                              hasByWeekDay ? &byWeekDay : nullptr,
                              hasByMonthDay ? &byMonthDay: nullptr,
                              hasByMonthWeekDay ? &byMonthWeekDay: nullptr);
}

ScheduledMeeting::ScheduledMeeting(const handle chatid, const std::string &timezone, const m_time_t startDateTime,
                                   const m_time_t endDateTime, const std::string &title, const std::string &description,
                                   const handle organizerUserId, const handle schedId, const handle parentSchedId,
                                   const int cancelled, const std::string &attributes, const m_time_t overrides,
                                   const ScheduledFlags* flags, const ScheduledRules* rules)
    : mChatid(chatid),
      mOrganizerUserId(organizerUserId),
      mSchedId(schedId),
      mParentSchedId(parentSchedId),
      mTimezone(timezone),
      mStartDateTime(startDateTime),
      mEndDateTime(endDateTime),
      mTitle(title),
      mDescription(description),
      mAttributes(attributes),
      mOverrides(overrides),
      mCancelled(cancelled),
      mFlags(flags ? flags->copy() : nullptr),
      mRules(rules ? rules->copy() : nullptr)
{}

ScheduledMeeting::ScheduledMeeting(const ScheduledMeeting* sm)
    : mChatid(sm->chatid()),
      mOrganizerUserId(sm->organizerUserid()),
      mSchedId(sm->schedId()),
      mParentSchedId(sm->parentSchedId()),
      mTimezone(sm->timezone()),
      mStartDateTime(sm->startDateTime()),
      mEndDateTime(sm->endDateTime()),
      mTitle(sm->title()),
      mDescription(sm->description()),
      mAttributes(sm->attributes()),
      mOverrides(sm->overrides()),
      mCancelled(sm->cancelled()),
      mFlags(sm->flags() ? sm->flags()->copy() : nullptr),
      mRules(sm->rules() ? sm->rules()->copy() : nullptr)
{}

bool ScheduledMeeting::isValid() const
{
    const std::string errMsg {"Invalid scheduled meeting "};
    if (mSchedId == UNDEF)
    {
        LOG_warn << errMsg << "schedId. chatid: " << Base64Str<MegaClient::USERHANDLE>(mChatid);
        return false;
    }
    const auto sId = schedId();
    const auto logAndFail = [&errMsg, &sId](const string& msg) -> bool
    {
        LOG_warn << errMsg << msg << " . schedId: " << Base64Str<MegaClient::USERHANDLE>(sId);;
        return false;
    };
    if (chatid() == UNDEF)
    {
        return logAndFail("chatid");
    }
    if (organizerUserid() == UNDEF)
    {
        return logAndFail("organizer user id");
    }
    if (timezone().empty())
    {
        return logAndFail("timezone");
    }
    if (!MegaClient::isValidMegaTimeStamp(startDateTime()))
    {
        return logAndFail("StartDateTime");
    }
    if (!MegaClient::isValidMegaTimeStamp(endDateTime()))
    {
        return logAndFail("EndDateTime");
    }
    if (title().empty())
    {
        return logAndFail("title");
    }
    if (rules() && !rules()->isValid())
    {
        return logAndFail("rules");
    }
    if (overrides() != mega_invalid_timestamp && !MegaClient::isValidMegaTimeStamp(overrides()))
    {
        // overrides is an optional field so if it's not present, we will store mega_invalid_timestamp
        return logAndFail(std::string{"overrides: " + std::to_string(overrides())});
    }
    return true;
}

bool ScheduledMeeting::equalTo(const ScheduledMeeting* sm) const
{
    if (!sm)                                               { return false; }
    if (parentSchedId() != sm->parentSchedId())            { return false; }
    if (timezone().compare(sm->timezone()))                { return false; }
    if (startDateTime() != sm->startDateTime())            { return false; }
    if (endDateTime() != sm->endDateTime())                { return false; }
    if (title().compare(sm->title()))                      { return false; }
    if (description().compare(sm->description()))		   { return false; }
    if (attributes().compare(sm->attributes()))            { return false; }
    if (overrides() != sm->overrides())                    { return false; }
    if (cancelled() != sm->cancelled())                    { return false; }

    if (flags() || sm->flags())
    {
        if (flags() && !flags()->equalTo(sm->flags()))     { return false; }
        if (sm->flags() && !sm->flags()->equalTo(flags())) { return false; }
    }

    if (rules() || sm->rules())
    {
        if (rules() && !rules()->equalTo(sm->rules()))     { return false; }
        if (sm->rules() && !sm->rules()->equalTo(rules())) { return false; }
    }

    return true;
}

bool ScheduledMeeting::serialize(string& out) const
{
    if (schedId() == UNDEF)
    {
        LOG_warn << "ScheduledMeeting::serialize: Invalid scheduled meeting with an UNDEF schedId";
        assert(false);
        return false;
    }

    const bool hasParentSchedId = parentSchedId() != UNDEF;
    const bool hasAttributes = !attributes().empty();
    const bool hasOverrides = MegaClient::isValidMegaTimeStamp(overrides());
    const bool hasCancelled = cancelled() > -1;
    const bool hasflags = flags() != nullptr;
    const bool hasRules = rules() != nullptr;

    CacheableWriter w(out);
    w.serializehandle(schedId());
    w.serializehandle(organizerUserid());
    w.serializestring(timezone());
    w.serializei64(startDateTime());
    w.serializei64(endDateTime());
    w.serializestring(title());
    w.serializestring(description());
    w.serializeexpansionflags(hasParentSchedId, hasAttributes, hasOverrides, hasCancelled, hasflags, hasRules);

    if (hasParentSchedId) { w.serializehandle(parentSchedId());}
    if (hasAttributes)    { w.serializestring(attributes()); }
    if (hasOverrides)     { w.serializei64(overrides()); }
    if (hasCancelled)     { w.serializei32(cancelled()); }
    if (hasflags)
    {
        std::string flagsStr;
        if (flags()->serialize(flagsStr))
        {
            w.serializestring(flagsStr);
        }
    }
    if (hasRules)
    {
        std::string rulesStr;
        if (rules()->serialize(rulesStr))
        {
            w.serializestring(rulesStr);
        }
    }
    return true;
}

ScheduledMeeting* ScheduledMeeting::unserialize(const string& in, const handle chatid)
{
    if (in.empty())  { return nullptr; }

    handle schedId = UNDEF;
    handle organizerUserid = UNDEF;
    std::string timezone;
    m_time_t startDateTime = mega_invalid_timestamp;
    m_time_t endDateTime = mega_invalid_timestamp;
    std::string title;
    std::string description;
    constexpr unsigned int flagsSize = 6;
    unsigned char expansions[8]; // must be defined with size 8

    const auto logAndFail = [](const string& /*msg*/) -> ScheduledMeeting*
    {
        LOG_err << "Failure at schedule meeting unserialization ";
        assert(false);
        return nullptr;
    };
    CacheableReader r(in);
    if (!r.unserializehandle(schedId) ||
            !r.unserializehandle(organizerUserid) ||
            !r.unserializestring(timezone) ||
            !r.unserializei64(startDateTime) ||
            !r.unserializei64(endDateTime) ||
            !r.unserializestring(title) ||
            !r.unserializestring(description) ||
            !r.unserializeexpansionflags(expansions, flagsSize))
    {
        return logAndFail("");
    }

    const bool hasParentSchedId = expansions[0] > 0;
    const bool hasAttributes = expansions[1] > 0;
    const bool hasOverrides = expansions[2] > 0;
    const bool hasCancelled = expansions[3] > 0;
    const bool hasflags = expansions[4] > 0;
    const bool hasRules = expansions[5] > 0;

    handle parentSchedId = UNDEF;
    if (hasParentSchedId && !r.unserializehandle(parentSchedId)) { return logAndFail("parent Schedule id"); }

    std::string attributes;
    if (hasAttributes && !r.unserializestring(attributes))       { return logAndFail("attributes"); }

    m_time_t overrides = mega_invalid_timestamp;
    if (hasOverrides && !r.unserializei64(overrides))            { return logAndFail("override"); }

    int cancelled = -1;
    if (hasCancelled && !r.unserializei32(cancelled))            { return logAndFail("cancelled"); }

    std::string flagsStr;
    std::unique_ptr<ScheduledFlags> flags;
    if (hasflags && r.unserializestring(flagsStr))
    {
       flags.reset(ScheduledFlags::unserialize(flagsStr));
       if (!flags)                                               { return logAndFail("flags"); }
    }

    std::string rulesStr;
    std::unique_ptr<ScheduledRules> rules;
    if (hasRules && r.unserializestring(rulesStr))
    {
       rules.reset(ScheduledRules::unserialize(rulesStr));
       if (!rules)                                               { return logAndFail("rules"); }
    }

    return new ScheduledMeeting(chatid, timezone, startDateTime, endDateTime,
                                title, description, organizerUserid, schedId,
                                hasParentSchedId ? parentSchedId : UNDEF,
                                hasCancelled ? cancelled : -1,
                                attributes, overrides,
                                flags.get(), rules.get());
}

TextChat::TextChat(const bool publicChat) : mPublicChat(publicChat)
{
}

bool TextChat::serialize(string *d) const
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

    char mode = mPublicChat ? 1 : 0;
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

        for (attachments_map::const_iterator it = attachedNodes.begin(); it != attachedNodes.end(); it++)
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
        // serialize the number of scheduledMeetings
        ll = static_cast<unsigned short>(mScheduledMeetings.size());
        d->append((char *)&ll, sizeof ll);

        for (auto i = mScheduledMeetings.begin(); i != mScheduledMeetings.end(); i++)
        {
            std::string schedMeetingStr;
            if (i->second->serialize(schedMeetingStr))
            {
                // records should fit in 64KB (unsigned short max), since the API restricts
                // the size of description/title to 4K/256 chars, but just in case it happened
                // to have a larger record, just throw an error
                if (schedMeetingStr.size() > std::numeric_limits<unsigned short>::max())
                {
                    assert(false);
                    LOG_err << "Scheduled meeting record too long. Skipping";

                    ll = 0;
                    d->append((char *)&ll, sizeof ll);
                    continue;
                }

                ll = static_cast<unsigned short>(schedMeetingStr.size());
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
    std::unique_ptr<userpriv_vector> userpriv;
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

        userpriv = std::make_unique<userpriv_vector>();

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
            userpriv.reset();
        }
    }

    if (ptr + sizeof(bool) + sizeof(unsigned short) > end)
    {
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
            return NULL;
        }
        title.assign(ptr, ll);
    }
    ptr += ll;

    if (ptr + sizeof(handle) + sizeof(m_time_t) + sizeof(char) + 9 > end)
    {
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
                return NULL;
            }

            h = MemAccess::get<handle>(ptr);
            ptr += sizeof h;

            numUsers = MemAccess::get<unsigned short>(ptr);
            ptr += sizeof numUsers;

            handle uh = UNDEF;
            if (ptr + (numUsers * sizeof(uh)) > end)
            {
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
            return NULL;
        }

        keylen = MemAccess::get<unsigned short>(ptr);
        ptr += sizeof keylen;

        if (ptr + keylen > end)
        {
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
            return NULL;
        }

        schedMeetingsSize = MemAccess::get<unsigned short>(ptr);
        ptr += sizeof schedMeetingsSize;

        for (auto i = 0; i < schedMeetingsSize; ++i)
        {
            unsigned short len = 0;
            if (ptr + sizeof len > end)
            {
                return NULL;
            }

            len = MemAccess::get<unsigned short>(ptr);
            ptr += sizeof len;

            if (ptr + len > end)
            {
                return NULL;
            }

            std::string aux(ptr, len);
            scheduledMeetingsStr.emplace_back(aux);
            ptr += len;
        }
    }

    if (ptr < end)
    {
        return NULL;
    }

    vector<unique_ptr<ScheduledMeeting>> schedMeetings;
    for (const auto& i : scheduledMeetingsStr)
    {
        ScheduledMeeting* auxMeet = ScheduledMeeting::unserialize(i, id);
        if (!auxMeet)
        {
            LOG_err << "Failure at schedule meeting unserialization";
            assert(auxMeet);
            return NULL;
        }
        schedMeetings.push_back(std::unique_ptr<ScheduledMeeting>(auxMeet));
    }

    TextChat*& chat = client->chats[id]; // use reference to pointer to avoid 3 searches instead of one
    if (!chat)
    {
        chat = new TextChat(publicchat);
    }
    else
    {
        LOG_warn << "Unserialized a chat already in RAM";
        chat->changed = {};
    }
    chat->id = id;
    chat->priv = priv;
    chat->shard = shard;
    chat->setUserPrivileges(userpriv.release());
    chat->group = group;
    chat->title = title;
    chat->ou = ou;
    chat->resetTag();
    chat->ts = ts;
    chat->flags = flags;
    chat->attachedNodes = attachedNodes;
    chat->unifiedKey = unifiedKey;
    chat->meeting = meetingRoom != 0;
    chat->chatOptions = chatOptions;

    for (auto& sm : schedMeetings)
    {
        chat->addSchedMeeting(std::move(sm), false /*notify*/);
    }

    return chat;
}

void TextChat::setChatId(handle newId)
{
    id = newId;
}

handle TextChat::getChatId() const
{
    return id;
}

void TextChat::setOwnPrivileges(privilege_t p)
{
    priv = p;
}

privilege_t TextChat::getOwnPrivileges() const
{
    return priv;
}

void TextChat::setShard(int sh)
{
    shard = sh;
}

int TextChat::getShard() const
{
    return shard;
}

void TextChat::addUserPrivileges(handle uid, privilege_t p)
{
    if (!userpriv)
    {
        userpriv = std::make_unique<userpriv_vector>();
    }
    userpriv->emplace_back(uid, p);
}

bool TextChat::updateUserPrivileges(handle uid, privilege_t p)
{
    if (!userpriv)
    {
        return false;
    }

    auto it = std::find_if(userpriv->begin(), userpriv->end(), [uid](const userpriv_pair& p) { return p.first == uid; });
    if (it != userpriv->end())
    {
        it->second = p;
        return true;
    }

    return false;
}

bool TextChat::removeUserPrivileges(handle uid)
{
    if (!userpriv)
    {
        return false;
    }

    auto it = std::find_if(userpriv->begin(), userpriv->end(), [uid](const userpriv_pair& p) { return p.first == uid; });
    if (it != userpriv->end())
    {
        userpriv->erase(it);
        if (userpriv->empty())
        {
            userpriv.reset();
        }
    }

    return true;
}

void TextChat::setUserPrivileges(userpriv_vector* pvs)
{
    userpriv.reset(pvs);
}

const userpriv_vector* TextChat::getUserPrivileges() const
{
    return userpriv.get();
}

void TextChat::setGroup(bool g)
{
    group = g;
}

bool TextChat::getGroup() const
{
    return group;
}

void TextChat::setTitle(const string& t)
{
    title = t;
}

const string& TextChat::getTitle() const
{
    return title;
}

void TextChat::setUnifiedKey(const string& uk)
{
    unifiedKey = uk;
}

const string& TextChat::getUnifiedKey() const
{
    return unifiedKey;
}

void TextChat::setOwnUser(handle u)
{
    ou = u;
}

handle TextChat::getOwnUser() const
{
    return ou;
}

void TextChat::setTs(m_time_t t)
{
    ts = t;
}

m_time_t TextChat::getTs() const
{
    return ts;
}

const attachments_map& TextChat::getAttachments() const
{
    return attachedNodes;
}

handle_set TextChat::getUsersOfAttachment(handle a) const
{
    auto ita = attachedNodes.find(a);
    if (ita != attachedNodes.end())
    {
        return ita->second;
    }

    return handle_set();
}

bool TextChat::isUserOfAttachment(handle a, handle uid) const
{
    auto ita = attachedNodes.find(a);
    if (ita != attachedNodes.end())
    {
        return ita->second.find(uid) != ita->second.end();
    }

    return false;
}

void TextChat::addUserForAttachment(handle a, handle uid)
{
    attachedNodes[a].insert(uid);
}

void TextChat::setMeeting(bool m)
{
    meeting = m;
}

bool TextChat::getMeeting() const
{
    return meeting;
}

byte TextChat::getChatOptions() const
{
    return chatOptions;
}

bool TextChat::hasScheduledMeeting(handle smid) const
{
    return mScheduledMeetings.find(smid) != mScheduledMeetings.end();
}

const handle_set& TextChat::getSchedMeetingsChanged() const
{
    return mSchedMeetingsChanged;
}

void TextChat::clearSchedMeetingsChanged()
{
    mSchedMeetingsChanged.clear();
}

const vector<std::unique_ptr<ScheduledMeeting>>& TextChat::getUpdatedOcurrences() const
{
    return mUpdatedOcurrences;
}

void TextChat::setTag(int tag)
{
    if (this->tag != 0)    // external changes prevail
    {
        this->tag = tag;
    }
}

int TextChat::getTag() const
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

void TextChat::clearUpdatedSchedMeetingOccurrences()
{
    mUpdatedOcurrences.clear();
}

void TextChat::addUpdatedSchedMeetingOccurrence(std::unique_ptr<ScheduledMeeting> sm)
{
    if (!sm) { return; }
    mUpdatedOcurrences.emplace_back(std::move(sm));
}

const ScheduledMeeting* TextChat::getSchedMeetingById(handle id) const
{
    auto it = mScheduledMeetings.find(id);
    if (it != mScheduledMeetings.end())
    {
        return it->second.get();
    }
    return nullptr;
}

const map<handle/*schedId*/, std::unique_ptr<ScheduledMeeting>>& TextChat::getSchedMeetings() const
{
    return mScheduledMeetings;
}

bool TextChat::addSchedMeeting(std::unique_ptr<ScheduledMeeting> sm, bool notify)
{
    if (!sm)
    {
        assert(false);
        return false;
    }

    if (id != sm->chatid())
    {
        LOG_err << "addSchedMeeting: scheduled meeting chatid: " << toHandle(sm->chatid())
                << " doesn't match with expected one: " << toHandle(id);
        assert(false);
        return false;
    }

    handle schedId = sm->schedId();
    if (mScheduledMeetings.find(schedId) != mScheduledMeetings.end())
    {
        LOG_err << "addSchedMeeting: scheduled meeting with id: " << Base64Str<MegaClient::CHATHANDLE>(schedId) << " already exits";
        return false;
    }

    mScheduledMeetings.emplace(schedId, std::move(sm));
    if (notify)
    {
        mSchedMeetingsChanged.insert(schedId);
    }
    return true;
}

bool TextChat::removeSchedMeeting(handle schedId)
{
    assert(schedId != UNDEF);
    if (mScheduledMeetings.find(schedId) == mScheduledMeetings.end())
    {
        return false;
    }

    deleteSchedMeeting(schedId);
    return true;
}

void TextChat::removeSchedMeetingsList(const handle_set& schedList)
{
    for_each(begin(schedList), end(schedList), [this](handle sm) { deleteSchedMeeting(sm); });
}

handle_set TextChat::removeChildSchedMeetings(handle parentSchedId)
{
    // remove all scheduled meeting whose parent is parentSchedId
    handle_set deletedChildren;
    for (auto it = mScheduledMeetings.begin(); it != mScheduledMeetings.end(); it++)
    {
        if (it->second->parentSchedId() == parentSchedId)
        {
            deletedChildren.insert(it->second->schedId());
        }
    }

    for_each(begin(deletedChildren), end(deletedChildren), [this](handle sm) { deleteSchedMeeting(sm); });

    return deletedChildren;
}

bool TextChat::updateSchedMeeting(std::unique_ptr<ScheduledMeeting> sm)
{
    assert(sm);
    auto it = mScheduledMeetings.find(sm->schedId());
    if (it == mScheduledMeetings.end())
    {
        LOG_err << "updateSchedMeeting: scheduled meeting with id: " << Base64Str<MegaClient::CHATHANDLE>(sm->schedId()) << " no longer exists";
        return false;
    }

    // compare current scheduled meeting with received from API
    if (!sm->equalTo(it->second.get()))
    {
        mSchedMeetingsChanged.insert(sm->schedId());
        it->second = std::move(sm);
    }

    return true;
}

bool TextChat::addOrUpdateSchedMeeting(std::unique_ptr<ScheduledMeeting> sm, bool notify)
{
    if (!sm)
    {
        LOG_err << "addOrUpdateSchedMeeting: invalid scheduled meeting provided";
        assert(false);
        return false;
    }

    if (sm->schedId() == UNDEF)
    {
        LOG_err << "addOrUpdateSchedMeeting: invalid schedid";
        assert(false);
        return false;
    }

    return mScheduledMeetings.find(sm->schedId()) == mScheduledMeetings.end()
            ? addSchedMeeting(std::move(sm), notify)
            : updateSchedMeeting(std::move(sm));
}

ErrorCodes TextChat::setMode(bool pubChat)
{
    if (mPublicChat == pubChat) { return API_EEXIST; }

    if (pubChat) { return API_EACCESS; } // trying to convert a chat from private into public

    LOG_debug << "TextChat::setMode: EKR enabled (private chat) for chat: " << Base64Str<MegaClient::CHATHANDLE>(id);
    mPublicChat = pubChat;
    changed.mode = true;
    return API_OK;
}

bool TextChat::publicChat() const
{
    return mPublicChat;
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
    if (speakRequest != -1)
    {
        currentOptions.updateSpeakRequest(speakRequest != 0);
    }
    if (waitingRoom != -1)
    {
        currentOptions.updateWaitingRoom(waitingRoom != 0);
    }
    if (openInvite != -1)
    {
        currentOptions.updateOpenInvite(openInvite != 0);
    }

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
