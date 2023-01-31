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
    CacheableReader r(in);
    if (!r.unserializeu32(flagsNum))
    {
        assert(false);
        LOG_err << "ScheduledFlags unserialization failed at field flagsNum";
        return nullptr;
    }

    return new ScheduledFlags(flagsNum);
}

/* class scheduledRules */
ScheduledRules::ScheduledRules(int freq,
                              int interval,
                              m_time_t until,
                              const rules_vector* byWeekDay,
                              const rules_vector* byMonthDay,
                              const rules_map* byMonthWeekDay)
    : mFreq(isValidFreq(freq) ? static_cast<freq_type_t>(freq) : FREQ_INVALID),
      mInterval(isValidInterval(interval) ? interval : INTERVAL_INVALID),
      mUntil(isValidUntil(until) ? until : mega_invalid_timestamp),
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
m_time_t ScheduledRules::until() const                                      { return mUntil;}
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
    if (mUntil != r->until())          { return false; }

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
    if (strcmp(freq, "d") == 0)   { return FREQ_DAILY; }
    if (strcmp(freq, "w") == 0)   { return FREQ_WEEKLY; }
    if (strcmp(freq, "m") == 0)   { return FREQ_MONTHLY; }
    return FREQ_INVALID;
}

bool ScheduledRules::serialize(string& out) const
{
    assert(isValidFreq(mFreq));
    bool hasInterval = isValidInterval(mInterval);
    bool hasUntil = isValidUntil(mUntil);
    bool hasByWeekDay = mByWeekDay.get() && !mByWeekDay->empty();
    bool hasByMonthDay = mByMonthDay.get() && !mByMonthDay->empty();
    bool hasByMonthWeekDay = mByMonthWeekDay.get() && !mByMonthWeekDay->empty();

    CacheableWriter w(out);
    w.serializei32(mFreq);
    w.serializeexpansionflags(hasInterval, hasUntil, hasByWeekDay, hasByMonthDay, hasByMonthWeekDay);

    if (hasInterval) { w.serializei32(mInterval); }
    if (hasUntil)    { w.serializei64(mUntil); }
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
    m_time_t until = mega_invalid_timestamp;
    rules_vector byWeekDay;
    rules_vector byMonthDay;
    rules_map byMonthWeekDay;
    constexpr unsigned int flagsSize = 5;
    unsigned char expansions[8];  // must be defined with size 8
    uint32_t auxSize = 0;

    CacheableReader r(in);
    if (!r.unserializei32(freq) || !r.unserializeexpansionflags(expansions, flagsSize))
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

    if (hasInterval && !r.unserializei32(interval))
    {
        assert(false);
        LOG_err << "Failure at schedule meeting rules unserialization interval";
        return nullptr;
    }

    if (hasUntil && !r.unserializei64(until))
    {
        assert(false);
        LOG_err << "Failure at schedule meeting rules unserialization until";
        return nullptr;
    }

    auxSize = 0;
    if (hasByWeekDay)
    {
        if (!r.unserializeu32(auxSize))
        {
            assert(false);
            LOG_err << "Failure at schedule meeting rules unserialization byWeekDay vector size";
            return nullptr;
        }

        for (size_t i = 0; i < auxSize; i++)
        {
           int8_t element = 0;
           if (r.unserializei8(element))
           {
               byWeekDay.emplace_back(element);
           }
           else
           {
               assert(false);
               LOG_err << "Failure at schedule meeting rules unserialization byWeekDay";
               return nullptr;
           }
        }
    }

    auxSize = 0;
    if (hasByMonthDay)
    {
        if (!r.unserializeu32(auxSize))
        {
            assert(false);
            LOG_err << "Failure at schedule meeting rules unserialization byMonthDay vector size";
            return nullptr;
        }

        for (size_t i = 0; i < auxSize; i++)
        {
           int8_t element = 0;
           if (r.unserializei8(element))
           {
               byMonthDay.emplace_back(element);
           }
           else
           {
               assert(false);
               LOG_err << "Failure at schedule meeting rules unserialization byMonthDay";
               return nullptr;
           }
        }
    }

    auxSize = 0;
    if (hasByMonthWeekDay)
    {
        if (!r.unserializeu32(auxSize))
        {
            assert(false);
            LOG_err << "Failure at schedule meeting rules unserialization byMonthWeekDay vector size";
            return nullptr;
        }


        for (size_t i = 0; i < auxSize / 2; i++)
        {
           int8_t key = 0;
           int8_t value = 0;
           if (r.unserializei8(key) && r.unserializei8(value))
           {
              byMonthWeekDay.emplace(key, value);
           }
           else
           {
               assert(false);
               LOG_err << "Failure at schedule meeting rules unserialization byMonthWeekDay";
               return nullptr;
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
ScheduledMeeting::ScheduledMeeting(handle chatid, const std::string &timezone, m_time_t startDateTime, m_time_t endDateTime,
                                const std::string &title, const std::string &description, handle organizerUserId, handle schedId,
                                handle parentSchedId, int cancelled, const std::string &attributes,
                                m_time_t overrides, ScheduledFlags* flags, ScheduledRules* rules)
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
void ScheduledMeeting::setChatid(handle chatid)                         { mChatid = chatid; }

handle ScheduledMeeting::chatid() const                                 { return mChatid; }
handle ScheduledMeeting::organizerUserid() const                        { return mOrganizerUserId; }
handle ScheduledMeeting::schedId() const                                { return mSchedId; }
handle ScheduledMeeting::parentSchedId() const                          { return mParentSchedId; }
const string& ScheduledMeeting::timezone() const                        { return mTimezone; }
m_time_t ScheduledMeeting::startDateTime() const                        { return mStartDateTime; }
m_time_t ScheduledMeeting::endDateTime() const                          { return mEndDateTime; }
const string& ScheduledMeeting::title() const                           { return mTitle; }
const string& ScheduledMeeting::description() const                     { return mDescription; }
const string& ScheduledMeeting::attributes() const                      { return mAttributes; }
m_time_t ScheduledMeeting::overrides() const                            { return mOverrides; }
int ScheduledMeeting::cancelled() const                                 { return mCancelled; }
const ScheduledFlags* ScheduledMeeting::flags() const                   { return mFlags.get(); }
const mega::ScheduledRules *ScheduledMeeting::rules() const             { return mRules.get(); }

bool ScheduledMeeting::isValid() const
{
    if (mSchedId == UNDEF)
    {
        LOG_warn << "Invalid scheduled meeting schedId. chatid: " << Base64Str<MegaClient::USERHANDLE>(mChatid);
        return false;
    }
    if (mChatid == UNDEF)
    {
        LOG_warn << "Invalid scheduled meeting chatid. schedId: " << Base64Str<MegaClient::USERHANDLE>(mSchedId);
        return false;
    }
    if (mOrganizerUserId == UNDEF)
    {
        LOG_warn << "Invalid scheduled meeting organizer user id. schedId: " << Base64Str<MegaClient::USERHANDLE>(mSchedId);
        return false;
    }
    if (mTimezone.empty())
    {
        LOG_warn << "Invalid scheduled meeting timezone. schedId: " << Base64Str<MegaClient::USERHANDLE>(mSchedId);
        return false;
    }
    if (!MegaClient::isValidMegaTimeStamp(mStartDateTime))
    {
        LOG_warn << "Invalid scheduled meeting StartDateTime. schedId: " << Base64Str<MegaClient::USERHANDLE>(mSchedId);
        return false;
    }
    if (!MegaClient::isValidMegaTimeStamp(mEndDateTime))
    {
        LOG_warn << "Invalid scheduled meeting EndDateTime. schedId: " << Base64Str<MegaClient::USERHANDLE>(mSchedId);
        return false;
    }
    if (mTitle.empty())
    {
        LOG_warn << "Invalid scheduled meeting title. schedId: " << Base64Str<MegaClient::USERHANDLE>(mSchedId);
        return false;
    }
    if (mRules && !mRules->isValid())
    {
        LOG_warn << "Invalid scheduled meeting rules. schedId: " << Base64Str<MegaClient::USERHANDLE>(mSchedId);
        return false;
    }
    if (mOverrides != mega_invalid_timestamp && !MegaClient::isValidMegaTimeStamp(mOverrides))
    {
        // overrides is an optional field so if it's not present, we will store mega_invalid_timestamp
        LOG_warn << "Invalid scheduled meeting overrides: " << mOverrides;
        return false;
    }
    return true;
}

bool ScheduledMeeting::equalTo(const ScheduledMeeting* sm) const
{
    if (!sm)                                            { return false; }
    if (parentSchedId() != sm->parentSchedId())         { return false; }
    if (mTimezone.compare(sm->timezone()))              { return false; }
    if (mStartDateTime != sm->startDateTime())          { return false; }
    if (mEndDateTime != sm->endDateTime())              { return false; }
    if (mTitle.compare(sm->title()))                    { return false; }
    if (mDescription.compare(sm->description()))		{ return false; }
    if (mAttributes.compare(sm->attributes()))          { return false; }
    if (mOverrides != sm->overrides())                  { return false; }
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
    bool hasOverrides = MegaClient::isValidMegaTimeStamp(overrides());
    bool hasCancelled = cancelled() >= 0;
    bool hasflags = flags();
    bool hasRules = rules();

    CacheableWriter w(out);
    w.serializehandle(schedId());
    w.serializehandle(organizerUserid());
    w.serializestring(mTimezone);
    w.serializei64(mStartDateTime);
    w.serializei64(mEndDateTime);
    w.serializestring(mTitle);
    w.serializestring(mDescription);
    w.serializeexpansionflags(hasParentSchedId, hasAttributes, hasOverrides, hasCancelled, hasflags, hasRules);

    if (hasParentSchedId) { w.serializehandle(parentSchedId());}
    if (hasAttributes)    { w.serializestring(mAttributes); }
    if (hasOverrides)     { w.serializei64(mOverrides); }
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
    m_time_t startDateTime = mega_invalid_timestamp;
    m_time_t endDateTime = mega_invalid_timestamp;
    std::string title;
    std::string description;
    std::string attributes;
    m_time_t overrides = mega_invalid_timestamp;
    std::string flagsStr;
    std::string rulesStr;
    int cancelled = -1;
    std::unique_ptr<ScheduledFlags> flags;
    std::unique_ptr<ScheduledRules> rules;
    constexpr unsigned int flagsSize = 6;
    unsigned char expansions[8]; // must be defined with size 8

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

    if (hasParentSchedId && !r.unserializehandle(parentSchedId))
    {
        assert(false);
        LOG_err << "Failure at schedule meeting unserialization parent Schedule id";
        return nullptr;
    }

    if (hasAttributes && !r.unserializestring(attributes))
    {
        assert(false);
        LOG_err << "Failure at schedule meeting unserialization attributes";
        return nullptr;
    }

    if (hasOverrides && !r.unserializei64(overrides))
    {
       assert(false);
       LOG_err << "Failure at schedule meeting unserialization override";
       return nullptr;
    }

    if (hasCancelled && !r.unserializei32(cancelled))
    {
        assert(false);
        LOG_err << "Failure at schedule meeting unserialization cancelled";
        return nullptr;
    }

    if (hasflags && r.unserializestring(flagsStr))
    {
       flags.reset(ScheduledFlags::unserialize(flagsStr));
       if (!flags)
       {
           assert(false);
           LOG_err << "Failure at schedule meeting unserialization flags";
           return nullptr;
       }
    }

    if (hasRules && r.unserializestring(rulesStr))
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

        for (auto i = 0; i < schedMeetingsSize; ++i)
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
        ScheduledMeeting* auxMeet = ScheduledMeeting::unserialize(i, chat->id);
        if (auxMeet)
        {
            chat->addSchedMeeting(std::unique_ptr<ScheduledMeeting>(auxMeet), false /*notify*/);
        }
        else
        {
            assert(false);
            LOG_err << "Failure at schedule meeting unserialization";
            delete userpriv;
            return NULL;
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

void TextChat::addSchedMeetingOccurrence(std::unique_ptr<ScheduledMeeting> sm)
{
    mScheduledMeetingsOcurrences.emplace(sm->schedId(), std::move(sm));
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

const map<handle/*schedId*/, std::unique_ptr<ScheduledMeeting>>& TextChat::getSchedMeetings()
{
    return mScheduledMeetings;
}

bool TextChat::addSchedMeeting(std::unique_ptr<ScheduledMeeting> sm, bool notify)
{
    if (!sm || id != sm->chatid())
    {
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
        LOG_err << "removeSchedMeeting: scheduled meeting with id: " << Base64Str<MegaClient::CHATHANDLE>(schedId) << " no longer exists";
        return false;
    }

    deleteSchedMeeting(schedId);
    return true;
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

handle_set TextChat::removeSchedMeetingsOccurrencesAndChildren(handle schedId)
{
    // removes all scheduled meeting occurrences, whose scheduled meeting id OR parent scheduled meeting id, is equal to schedId
    handle_set deletedOccurr;
    for (auto it = mScheduledMeetingsOcurrences.begin(); it != mScheduledMeetingsOcurrences.end(); it++)
    {
        if (it->second->schedId() == schedId || it->second->parentSchedId() == schedId)
        {
            deletedOccurr.insert(it->second->schedId());
        }
    }

    for_each(begin(deletedOccurr), end(deletedOccurr), [this](handle sm) { deleteSchedMeetingOccurrBySchedId(sm); });

    return deletedOccurr;
}

handle_set TextChat::removeChildSchedMeetingsOccurrences(handle parentSchedId)
{
    // remove all scheduled meeting occurrences, whose parent is scheduled meeting id, is equal to parentSchedId
    handle_set deletedOccurr;
    for (auto it = mScheduledMeetingsOcurrences.begin(); it != mScheduledMeetingsOcurrences.end(); it++)
    {
        if (it->second->parentSchedId() == parentSchedId)
        {
            deletedOccurr.insert(it->second->schedId());
        }
    }

    for_each(begin(deletedOccurr), end(deletedOccurr), [this](handle sm) { deleteSchedMeetingOccurrBySchedId(sm); });

    return deletedOccurr;
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

    return mScheduledMeetings.find(sm->schedId()) == mScheduledMeetings.end()
            ? addSchedMeeting(std::move(sm), notify)
            : updateSchedMeeting(std::move(sm));
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
