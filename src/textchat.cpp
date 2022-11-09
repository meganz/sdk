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
/* class scheduledFlags */
ScheduledFlags::ScheduledFlags (unsigned long numericValue)
    : mFlags(numericValue)
{
}

ScheduledFlags::ScheduledFlags(const mega::ScheduledFlags *flags)
    : mFlags(flags ? flags->getNumericValue() : 0)
{
}

ScheduledFlags::~ScheduledFlags()
{
}

ScheduledFlags* ScheduledFlags::copy() const
{
    return new ScheduledFlags(this);
}

unsigned long ScheduledFlags::getNumericValue() const       { return mFlags.to_ulong(); }
bool ScheduledFlags::isEmpty() const                        { return mFlags.none(); }
bool ScheduledFlags::equalTo(const ScheduledFlags* f) const
{
    if (!f) { return false; }
    return mFlags.to_ulong() == f->mFlags.to_ulong();
}

bool ScheduledFlags::serialize(string& out) const
{
    CacheableWriter w(out);
    w.serializeu32(static_cast<uint32_t>(mFlags.to_ulong()));
    return true;
}

ScheduledFlags* ScheduledFlags::unserialize(const std::string &in)
{
    if (in.empty())  { return nullptr; }
    uint32_t flagsNum = 0;
    CacheableReader w(in);
    w.unserializeu32(flagsNum);
    return new ScheduledFlags(flagsNum);
}

/* class scheduledRules */
ScheduledRules::ScheduledRules(int freq,
                              int interval,
                              const string& until,
                              const rules_vector* byWeekDay,
                              const rules_vector* byMonthDay,
                              const rules_map* byMonthWeekDay)
    : mFreq(isValidFreq(freq) ? static_cast<freq_type_t>(freq) : FREQ_INVALID),
      mInterval(isValidInterval(interval) ? interval : INTERVAL_INVALID),
      mUntil(until),
      mByWeekDay(byWeekDay ? new rules_vector(*byWeekDay) : nullptr),
      mByMonthDay(byMonthDay ? new rules_vector(*byMonthDay) : nullptr),
      mByMonthWeekDay(byMonthWeekDay ? new rules_map(byMonthWeekDay->begin(), byMonthWeekDay->end()) : nullptr)
{
}

ScheduledRules::ScheduledRules(const ScheduledRules* rules)
    : mFreq(isValidFreq(rules->freq()) ? rules->freq() : FREQ_INVALID),
      mInterval(isValidInterval(rules->interval()) ? rules->interval() : INTERVAL_INVALID),
      mUntil(rules->until()),
      mByWeekDay(rules->byWeekDay() ? new rules_vector(*rules->byWeekDay()) : nullptr),
      mByMonthDay(rules->byMonthDay() ? new rules_vector(*rules->byMonthDay()) : nullptr),
      mByMonthWeekDay(rules->byMonthWeekDay() ? new rules_map(rules->byMonthWeekDay()->begin(), rules->byMonthWeekDay()->end()) : nullptr)
{
}

ScheduledRules* ScheduledRules::copy() const
{
    return new ScheduledRules(this);
}

ScheduledRules::~ScheduledRules()
{
}

ScheduledRules::freq_type_t ScheduledRules::freq() const                    { return mFreq; }
int ScheduledRules::interval() const                                        { return mInterval; }
const std::string& ScheduledRules::until() const                            { return mUntil;}
const ScheduledRules::rules_vector* ScheduledRules::byWeekDay() const       { return mByWeekDay.get(); }
const ScheduledRules::rules_vector* ScheduledRules::byMonthDay() const      { return mByMonthDay.get(); }
const ScheduledRules::rules_map* ScheduledRules::byMonthWeekDay() const     { return mByMonthWeekDay.get(); }
bool ScheduledRules::isValid() const
{
    return isValidFreq(mFreq);
}

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
    if (mFreq != r->freq())            { return false; }
    if (mInterval != r->interval())    { return false; }
    if (mUntil.compare(r->until()))    { return false; }

    if (mByWeekDay || r->byWeekDay())
    {
        if (!mByWeekDay || !r->byWeekDay()) { return false; }
        if (*mByWeekDay != *r->byWeekDay()) { return false; }
    }

    if (mByMonthDay || r->byMonthDay())
    {
        if (!mByMonthDay || !r->byMonthDay()) { return false; }
        if (*mByMonthDay != *r->byMonthDay()) { return false; }
    }

    if (mByMonthWeekDay || r->byMonthWeekDay())
    {
        if (!mByMonthWeekDay || !r->byMonthWeekDay()) { return false; }
        if (*mByMonthWeekDay != *r->byMonthWeekDay()) { return false; }
    }

    return true;
}

int ScheduledRules::stringToFreq (const char* freq)
{
    if (strcmp(freq, "d") == 0)     { return FREQ_DAILY; }
    if (strcmp(freq, "w") == 0)    { return FREQ_WEEKLY; }
    if (strcmp(freq, "m") == 0)   { return FREQ_WEEKLY; }
    return FREQ_INVALID;
}

bool ScheduledRules::serialize(string& out) const
{
    assert(isValidFreq(mFreq));
    bool hasInterval = isValidInterval(mInterval);
    bool hasUntil = !mUntil.empty();
    bool hasByWeekDay = mByWeekDay.get() && !mByWeekDay->empty();
    bool hasByMonthDay = mByMonthDay.get() && !mByMonthDay->empty();
    bool hasByMonthWeekDay = mByMonthWeekDay.get() && !mByMonthWeekDay->empty();

    CacheableWriter w(out);
    w.serializei32(mFreq);
    w.serializeexpansionflags(hasInterval, hasUntil, hasByWeekDay, hasByMonthDay, hasByMonthWeekDay);

    if (hasInterval) { w.serializei32(mInterval); }
    if (hasUntil)    { w.serializestring(mUntil); }
    if (hasByWeekDay)
    {
        w.serializeu32(static_cast<uint32_t>(mByWeekDay->size()));
        for (auto i: *mByWeekDay)
        {
            w.serializei8(i);
        }
    }

    if (hasByMonthDay)
    {
        w.serializeu32(static_cast<uint32_t>(mByMonthDay->size()));
        for (auto i: *mByMonthDay)
        {
            w.serializei8(i);
        }
    }

    if (hasByMonthWeekDay)
    {
        w.serializeu32(static_cast<uint32_t>(mByMonthWeekDay->size()*2));
        for (auto i: *mByMonthWeekDay)
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
    int interval = INTERVAL_INVALID;
    std::string until;
    rules_vector byWeekDay;
    rules_vector byMonthDay;
    rules_map byMonthWeekDay;
    constexpr unsigned int flagsSize = 5;
    unsigned char expansions[8];  // must be defined with size 8
    uint32_t auxSize = 0;

    CacheableReader w(in);
    if (!w.unserializei32(freq) || !w.unserializeexpansionflags(expansions, flagsSize))
    {
       assert(false);
       LOG_err << "Failure at schedule meeting rules unserialization";
       return nullptr;
    }

    bool hasInterval        = expansions[0];
    bool hasUntil           = expansions[1];
    bool hasByWeekDay       = expansions[2];
    bool hasByMonthDay      = expansions[3];
    bool hasByMonthWeekDay  = expansions[4];

    if (hasInterval && !w.unserializei32(interval))
    {
        assert(false);
        LOG_err << "Failure at schedule meeting rules unserialization interval";
        return nullptr;
    }

    if (hasUntil && !w.unserializestring(until))
    {
        assert(false);
        LOG_err << "Failure at schedule meeting rules unserialization until";
        return nullptr;
    }

    auxSize = 0;
    if (hasByWeekDay && w.unserializeu32(auxSize))
    {
        for (size_t i = 0; i < auxSize; i++)
        {
           int8_t element = 0;
           if (w.unserializei8(element))
           {
               byWeekDay.emplace_back(element);
           }
        }
    }

    auxSize = 0;
    if (hasByMonthDay && w.unserializeu32(auxSize))
    {
        for (size_t i = 0; i < auxSize; i++)
        {
           int8_t element = 0;
           if (w.unserializei8(element))
           {
               byMonthDay.emplace_back(element);
           }
        }
    }

    auxSize = 0;
    if (hasByMonthWeekDay && w.unserializeu32(auxSize))
    {
        for (size_t i = 0; i < auxSize / 2; i++)
        {
           int8_t key = 0;
           int8_t value = 0;
           if (w.unserializei8(key) && w.unserializei8(value))
           {
              byMonthWeekDay.emplace(key, value);
           }
        }
    }

    return new ScheduledRules(freq,
                              hasInterval ? interval : -1,
                              until,
                              hasByWeekDay ? &byWeekDay : nullptr,
                              hasByMonthDay ? &byMonthDay: nullptr,
                              hasByMonthWeekDay ? &byMonthWeekDay: nullptr);
}

/* class scheduledMeeting */
ScheduledMeeting::ScheduledMeeting(handle chatid, const std::string &timezone, const std::string &startDateTime, const std::string &endDateTime,
                                const std::string &title, const std::string &description, handle organizerUserId, handle schedId,
                                handle parentSchedId, int cancelled, const std::string &attributes,
                                const std::string &overrides, ScheduledFlags* flags, ScheduledRules* rules)
    : mChatid(chatid),
      mOrganizerUserId(organizerUserId),
      mSchedId(schedId),
      mParentSchedId(parentSchedId),
      mTimezone(timezone),
      mStartDateTime(startDateTime ),
      mEndDateTime(endDateTime),
      mTitle(title),
      mDescription(description),
      mAttributes(attributes),
      mOverrides(overrides),
      mCancelled(cancelled),
      mFlags(flags ? flags->copy() : nullptr),
      mRules(rules ? rules->copy() : nullptr)
{
}

ScheduledMeeting::ScheduledMeeting(const ScheduledMeeting* scheduledMeeting)
    : mChatid(scheduledMeeting->chatid()),
      mOrganizerUserId(scheduledMeeting->organizerUserid()),
      mSchedId(scheduledMeeting->schedId()),
      mParentSchedId(scheduledMeeting->parentSchedId()),
      mTimezone(scheduledMeeting->timezone()),
      mStartDateTime(scheduledMeeting->startDateTime()),
      mEndDateTime(scheduledMeeting->endDateTime()),
      mTitle(scheduledMeeting->title()),
      mDescription(scheduledMeeting->description()),
      mAttributes(scheduledMeeting->attributes()),
      mOverrides(scheduledMeeting->overrides()),
      mCancelled(scheduledMeeting->cancelled()),
      mFlags(scheduledMeeting->flags() ? scheduledMeeting->flags()->copy() : nullptr),
      mRules(scheduledMeeting->rules() ? scheduledMeeting->rules()->copy() : nullptr)
{
}

ScheduledMeeting* ScheduledMeeting::copy() const
{
   return new ScheduledMeeting(this);
}

ScheduledMeeting::~ScheduledMeeting()
{
}

void ScheduledMeeting::setSchedId(handle schedId)                       { mSchedId = schedId; }

handle ScheduledMeeting::chatid() const                                 { return mChatid; }
handle ScheduledMeeting::organizerUserid() const                        { return mOrganizerUserId; }
handle ScheduledMeeting::schedId() const                                { return mSchedId; }
handle ScheduledMeeting::parentSchedId() const                          { return mParentSchedId; }
const string& ScheduledMeeting::timezone() const                        { return mTimezone; }
const string& ScheduledMeeting::startDateTime() const                   { return mStartDateTime; }
const string& ScheduledMeeting::endDateTime() const                     { return mEndDateTime; }
const string& ScheduledMeeting::title() const                           { return mTitle; }
const string& ScheduledMeeting::description() const                     { return mDescription; }
const string& ScheduledMeeting::attributes() const                      { return mAttributes; }
const string& ScheduledMeeting::overrides() const                       { return mOverrides; }
int ScheduledMeeting::cancelled() const                                 { return mCancelled; }
const ScheduledFlags* ScheduledMeeting::flags() const                   { return mFlags.get(); }
const mega::ScheduledRules *ScheduledMeeting::rules() const             { return mRules.get(); }

bool ScheduledMeeting::isValid() const
{
    return mChatid != UNDEF
            && mOrganizerUserId != UNDEF
            && mSchedId != UNDEF
            && !mTimezone.empty()
            && !mStartDateTime.empty()
            && !mEndDateTime.empty()
            && !mTitle.empty()
            && !mDescription.empty()
            && (!mRules || mRules->isValid());
}

bool ScheduledMeeting::equalTo(const ScheduledMeeting* sm) const
{
    if (!sm)                                            { return false; }
    if (parentSchedId() != sm->parentSchedId())         { return false; }
    if (mTimezone.compare(sm->timezone()))              { return false; }
    if (mStartDateTime.compare(sm->startDateTime()))	{ return false; }
    if (mEndDateTime.compare(sm->endDateTime()))		{ return false; }
    if (mTitle.compare(sm->title()))                    { return false; }
    if (mDescription.compare(sm->description()))		{ return false; }
    if (mAttributes.compare(sm->attributes()))          { return false; }
    if (mOverrides.compare(sm->overrides()))            { return false; }
    if (mCancelled != sm->cancelled())                  { return false; }

    if (mFlags || sm->flags())
    {
        if (mFlags && !mFlags->equalTo(sm->flags()))            { return false; }
        if (sm->flags() && !sm->flags()->equalTo(mFlags.get())) { return false; }
    }

    if (mRules || sm->rules())
    {
        if (mRules && !mRules->equalTo(sm->rules()))            { return false; }
        if (sm->rules() && !sm->rules()->equalTo(mRules.get())) { return false; }
    }

    return true;
}

bool ScheduledMeeting::serialize(string& out) const
{
    if (schedId() == UNDEF)
    {
        assert(false);
        LOG_warn << "ScheduledMeeting::serialize: Invalid scheduled meeting with an UNDEF schedId";
        return false;
    }

    bool hasParentSchedId = parentSchedId() != UNDEF;
    bool hasAttributes = !attributes().empty();
    bool hasOverrides = !overrides().empty();
    bool hasCancelled = cancelled() >= 0;
    bool hasflags = flags();
    bool hasRules = rules();

    CacheableWriter w(out);
    w.serializehandle(schedId());
    w.serializehandle(organizerUserid());
    w.serializestring(mTimezone);
    w.serializestring(mStartDateTime);
    w.serializestring(mEndDateTime);
    w.serializestring(mTitle);
    w.serializestring(mDescription);
    w.serializeexpansionflags(hasParentSchedId, hasAttributes, hasOverrides, hasCancelled, hasflags, hasRules);

    if (hasParentSchedId) { w.serializehandle(parentSchedId());}
    if (hasAttributes)    { w.serializestring(mAttributes); }
    if (hasOverrides)     { w.serializestring(mOverrides); }
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

ScheduledMeeting* ScheduledMeeting::unserialize(const string& in, handle chatid)
{
    if (in.empty())  { return nullptr; }
    handle organizerUserid = UNDEF;
    handle schedId = UNDEF;
    handle parentSchedId = UNDEF;
    std::string timezone;
    std::string startDateTime;
    std::string endDateTime;
    std::string title;
    std::string description;
    std::string attributes;
    std::string overrides;
    std::string flagsStr;
    std::string rulesStr;
    int cancelled = -1;
    std::unique_ptr<ScheduledFlags> flags;
    std::unique_ptr<ScheduledRules> rules;
    constexpr unsigned int flagsSize = 6;
    unsigned char expansions[8]; // must be defined with size 8

    CacheableReader w(in);
    if (!w.unserializehandle(schedId) ||
            !w.unserializehandle(organizerUserid) ||
            !w.unserializestring(timezone) ||
            !w.unserializestring(startDateTime) ||
            !w.unserializestring(endDateTime) ||
            !w.unserializestring(title) ||
            !w.unserializestring(description) ||
            !w.unserializeexpansionflags(expansions, flagsSize))
    {
        assert(false);
        LOG_err << "Failure at schedule meeting unserialization";
        return nullptr;
    }

    bool hasParentSchedId   = expansions[0];
    bool hasAttributes      = expansions[1];
    bool hasOverrides       = expansions[2];
    bool hasCancelled       = expansions[3];
    bool hasflags           = expansions[4];
    bool hasRules           = expansions[5];

    if (hasParentSchedId && !w.unserializehandle(parentSchedId))
    {
        assert(false);
        LOG_err << "Failure at schedule meeting unserialization parent Schedule id";
        return nullptr;
    }

    if (hasAttributes && !w.unserializestring(attributes))
    {
        assert(false);
        LOG_err << "Failure at schedule meeting unserialization attributes";
        return nullptr;
    }

    if (hasOverrides && !w.unserializestring(overrides))
    {
       assert(false);
       LOG_err << "Failure at schedule meeting unserialization override";
       return nullptr;
    }

    if (hasCancelled && !w.unserializei32(cancelled))
    {
        assert(false);
        LOG_err << "Failure at schedule meeting unserialization cancelled";
        return nullptr;
    }

    if (hasflags && w.unserializestring(flagsStr))
    {
       flags.reset(ScheduledFlags::unserialize(flagsStr));
       if (!flags)
       {
           assert(false);
           LOG_err << "Failure at schedule meeting unserialization flags";
           return nullptr;
       }
    }

    if (hasRules && w.unserializestring(rulesStr))
    {
       rules.reset(ScheduledRules::unserialize(rulesStr));
       if (!rules)
       {
           assert(false);
           LOG_err << "Failure at schedule meeting unserialization rules";
           return nullptr;
       }
    }

    return new ScheduledMeeting(chatid, timezone, startDateTime, endDateTime,
                                title, description, organizerUserid, schedId,
                                hasParentSchedId ? parentSchedId : UNDEF,
                                hasCancelled ? cancelled : -1,
                                attributes,
                                overrides,
                                flags.get(), rules.get());
}

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
        // serialize the number of scheduledMeetings
        ll = (unsigned short) mScheduledMeetings.size();
        d->append((char *)&ll, sizeof ll);

        for (auto i = mScheduledMeetings.begin(); i != mScheduledMeetings.end(); i++)
        {
            std::string schedMeetingStr;
            if (i->second->serialize(schedMeetingStr))
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
        std::unique_ptr<ScheduledMeeting> auxMeet(ScheduledMeeting::unserialize(i, chat->id));
        if (auxMeet)
        {
            chat->addSchedMeeting(auxMeet.get(), false /*notify*/);
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

void TextChat::addSchedMeetingOccurrence(const ScheduledMeeting* sm)
{
    mScheduledMeetingsOcurrences.emplace(sm->schedId(), std::unique_ptr<ScheduledMeeting>(sm->copy()));
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
    assert(id == sm->chatid());
    handle schedId = sm->schedId();
    if (mScheduledMeetings.find(schedId) != mScheduledMeetings.end())
    {
        LOG_err << "addSchedMeeting: scheduled meeting with id: " << Base64Str<MegaClient::CHATHANDLE>(schedId) << " already exits";
        return false;
    }

    mScheduledMeetings.emplace(schedId, std::unique_ptr<ScheduledMeeting>(sm->copy()));
    if (notify)
    {
        mSchedMeetingsChanged.emplace_back(schedId);
    }
    return true;
}

bool TextChat::removeSchedMeeting(handle schedId)
{
    assert(schedId != UNDEF);
    if (mScheduledMeetings.find(schedId) == mScheduledMeetings.end())
    {
        LOG_err << "removeSchedMeeting: scheduled meeting with id: " << Base64Str<MegaClient::CHATHANDLE>(schedId) << " no longer exists";
        return false;
    }

    mScheduledMeetings.erase(schedId);
    mSchedMeetingsChanged.emplace_back(schedId);
    return true;
}

unsigned int TextChat::removeChildSchedMeetings(handle parentSchedId)
{
    // remove all scheduled meeting whose parent is parentSchedId
    unsigned int count = 0;
    for (auto it = mScheduledMeetings.begin(); it != mScheduledMeetings.end(); it++)
    {
        if (it->second->parentSchedId() == parentSchedId)
        {
            removeSchedMeeting(it->second->schedId());
            count++;
        }
    }

    return count;
}

bool TextChat::updateSchedMeeting(const ScheduledMeeting *sm)
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
        mSchedMeetingsChanged.emplace_back(sm->schedId());
        it->second.reset(sm->copy());
    }

    return true;
}

bool TextChat::addOrUpdateSchedMeeting(const ScheduledMeeting* sm, bool notify)
{
    if (!sm)
    {
        LOG_err << "addOrUpdateSchedMeeting: invalid scheduled meeting provided";
        assert(false);
        return false;
    }

    return mScheduledMeetings.find(sm->schedId()) == mScheduledMeetings.end()
            ? addSchedMeeting(sm, notify)
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
