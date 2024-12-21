/**
* @file mega/textchat.h
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

#ifndef TEXT_CHAT_H
#define TEXT_CHAT_H 1

#include "types.h"

#include <vector>
#include <map>
#include <bitset>

namespace mega {

using std::map;
using std::vector;

#ifdef ENABLE_CHAT
typedef enum { PRIV_UNKNOWN = -2, PRIV_RM = -1, PRIV_RO = 0, PRIV_STANDARD = 2, PRIV_MODERATOR = 3 } privilege_t;
typedef pair<handle, privilege_t> userpriv_pair;
typedef vector< userpriv_pair > userpriv_vector;
typedef map <handle, set <handle> > attachments_map;

class ScheduledFlags
{
public:
    typedef enum
    {
        FLAGS_SEND_EMAILS      = 0, // API will send out calendar emails for this meeting if it's enabled
        FLAGS_SIZE             = 1, // size in bits of flags bitmask
    } scheduled_flags_t;            // 3 Bytes (maximum)

    static constexpr unsigned int schedEmptyFlags = 0;

    ScheduledFlags() = default;
    ScheduledFlags (const unsigned long numericValue) : mFlags(numericValue) {};
    ScheduledFlags (const ScheduledFlags* flags)      : mFlags(flags ? flags->getNumericValue() : schedEmptyFlags) {};
    virtual ~ScheduledFlags() = default;
    ScheduledFlags(const ScheduledFlags&) = delete;
    ScheduledFlags(const ScheduledFlags&&) = delete;
    ScheduledFlags& operator=(const ScheduledFlags&) = delete;
    ScheduledFlags& operator=(const ScheduledFlags&&) = delete;

    void reset()                                   { mFlags.reset(); }
    void setSendEmails(const bool enabled)         { mFlags[FLAGS_SEND_EMAILS] = enabled; }
    void importFlagsValue(const unsigned long val) { mFlags = val; }

    bool sendEmails() const                        { return mFlags[FLAGS_SEND_EMAILS]; }
    unsigned long getNumericValue() const          { return mFlags.to_ulong(); }
    bool isEmpty() const                           { return mFlags.none(); }
    bool equalTo(const ScheduledFlags* sf) const
    {
        return sf && (getNumericValue() == sf->getNumericValue());
    }

    virtual ScheduledFlags* copy() const { return new ScheduledFlags(this); }

    bool serialize(string& out) const;
    static ScheduledFlags* unserialize(const string& in);

protected:
    std::bitset<FLAGS_SIZE> mFlags = schedEmptyFlags;
};

class ScheduledRules
{
public:
    typedef enum {
        FREQ_INVALID    = -1,
        FREQ_DAILY      = 0,
        FREQ_WEEKLY     = 1,
        FREQ_MONTHLY    = 2,
    } freq_type_t;

    // just for SDK core usage (matching mega::SmallInt{Vector|Map})
    typedef vector<int8_t> rules_vector;
    typedef multimap<int8_t, int8_t> rules_map;

    constexpr static int INTERVAL_INVALID = 0;

    ScheduledRules(const int freq,
                   const int interval = INTERVAL_INVALID,
                   const m_time_t until = mega_invalid_timestamp,
                   const rules_vector* byWeekDay = nullptr,
                   const rules_vector* byMonthDay = nullptr,
                   const rules_map* byMonthWeekDay = nullptr);

    ScheduledRules(const ScheduledRules* rules);
    virtual ~ScheduledRules() = default;
    ScheduledRules(const ScheduledRules&) = delete;
    ScheduledRules(const ScheduledRules&&) = delete;
    ScheduledRules& operator=(const ScheduledRules&) = delete;
    ScheduledRules& operator=(const ScheduledRules&&) = delete;

    ScheduledRules::freq_type_t freq() const { return mFreq; }
    int interval() const                     { return mInterval; }
    m_time_t until() const                   { return mUntil;}
    const rules_vector* byWeekDay() const    { return mByWeekDay.get(); }
    const rules_vector* byMonthDay() const   { return mByMonthDay.get(); }
    const rules_map* byMonthWeekDay() const  { return mByMonthWeekDay.get(); }

    virtual ScheduledRules* copy() const { return new ScheduledRules(this); }
    bool equalTo(const ScheduledRules*) const;
    const char* freqToString() const;
    static int stringToFreq (const char* freq);
    bool isValid() const                              { return isValidFreq(mFreq); }
    static bool isValidFreq(const int freq)           { return (freq >= FREQ_DAILY && freq <= FREQ_MONTHLY); }
    static bool isValidInterval(const int interval)   { return interval > INTERVAL_INVALID; }
    static bool isValidUntil(const m_time_t interval) { return interval > mega_invalid_timestamp; }

    bool serialize(string& out) const;
    static ScheduledRules* unserialize(const string& in);

protected:
    // scheduled meeting frequency (DAILY | WEEKLY | MONTHLY), this is used in conjunction with interval to allow for a repeatable skips in the event timeline
    freq_type_t mFreq;

    // repetition interval in relation to the frequency
    int mInterval = 0;

    // specifies when the repetitions should end (unix timestamp)
    m_time_t mUntil = mega_invalid_timestamp;

    // allows us to specify that an event will only occur on given week day/s
    std::unique_ptr<rules_vector> mByWeekDay;

    // allows us to specify that an event will only occur on a given day/s of the month
    std::unique_ptr<rules_vector> mByMonthDay;

    // allows us to specify that an event will only occurs on a specific weekday offset of the month. For example, every 2nd Sunday of each month
    std::unique_ptr<rules_map> mByMonthWeekDay;
};

class ScheduledMeeting
{
public:
    ScheduledMeeting(const handle chatid,
                     const string& timezone,
                     const m_time_t startDateTime,
                     const m_time_t endDateTime,
                     const string& title,
                     const string& description,
                     const handle organizerUserId,
                     const handle schedId = UNDEF,
                     const handle parentSchedId = UNDEF,
                     const int cancelled = -1,
                     const string& attributes = std::string(),
                     const m_time_t overrides = mega_invalid_timestamp,
                     const ScheduledFlags* flags = nullptr,
                     const ScheduledRules* rules = nullptr);

    ScheduledMeeting(const ScheduledMeeting *scheduledMeeting);
    virtual ~ScheduledMeeting() = default;
    ScheduledMeeting(const ScheduledMeeting&) = delete;
    ScheduledMeeting(const ScheduledMeeting&&) = delete;
    ScheduledMeeting& operator=(const ScheduledMeeting&) = delete;
    ScheduledMeeting& operator=(const ScheduledMeeting&&) = delete;

    void setSchedId(const handle schedId)        { mSchedId = schedId; }
    void setChatid(const handle chatid)          { mChatid = chatid; }

    handle chatid() const                        { return mChatid; }
    handle organizerUserid() const               { return mOrganizerUserId; }
    handle schedId() const                       { return mSchedId; }
    handle parentSchedId() const                 { return mParentSchedId; }
    const std::string &timezone() const          { return mTimezone; }
    m_time_t startDateTime() const               { return mStartDateTime; }
    m_time_t endDateTime() const                 { return mEndDateTime; }
    const std::string &title() const             { return mTitle; }
    const std::string &description() const       { return mDescription; }
    const std::string &attributes() const        { return mAttributes; }
    m_time_t overrides() const                   { return mOverrides; }
    int cancelled() const                        { return mCancelled; }
    virtual const ScheduledFlags* flags() const  { return mFlags.get(); }
    virtual const ScheduledRules* rules() const  { return mRules.get(); }


    virtual ScheduledMeeting* copy() const { return new ScheduledMeeting(this); }
    bool equalTo(const ScheduledMeeting* sm) const;
    bool isValid() const;

    bool serialize(string& out) const;
    static ScheduledMeeting* unserialize(const std::string &in, const handle chatid);

private:
    handle mChatid;
    handle mOrganizerUserId;
    handle mSchedId;
    handle mParentSchedId;
    std::string mTimezone;
    m_time_t mStartDateTime; // (unix timestamp)
    m_time_t mEndDateTime; // (unix timestamp)
    std::string mTitle;
    std::string mDescription;
    std::string mAttributes; // attributes to store any additional data
    m_time_t mOverrides; // start dateTime of the original meeting series event to be replaced (unix timestamp)
    int mCancelled;
    std::unique_ptr<ScheduledFlags> mFlags; // flags bitmask (used to store additional boolean settings as a bitmask)
    std::unique_ptr<ScheduledRules> mRules;
};

class TextChat : public Cacheable
{
public:
    enum {
        FLAG_OFFSET_ARCHIVE = 0
    };

private:
    handle id = UNDEF;
    privilege_t priv = PRIV_UNKNOWN;
    int shard = -1;
    std::unique_ptr<userpriv_vector> userpriv;
    bool group = false;
    string title;        // byte array
    string unifiedKey;   // byte array
    handle ou = UNDEF;
    m_time_t ts = 0;     // creation time
    attachments_map attachedNodes;
    bool meeting = false;     // chat is meeting room
    byte chatOptions = 0; // each chat option is represented in 1 bit (check ChatOptions struct at types.h)

    // maps a scheduled meeting id to a scheduled meeting
    // a scheduled meetings allows the user to specify an event that will occur in the future (check ScheduledMeeting class documentation)
    map<handle/*schedId*/, std::unique_ptr<ScheduledMeeting>> mScheduledMeetings;

    // list of scheduled meetings changed
    handle_set mSchedMeetingsChanged;

    // vector of scheduled meeting occurrences that needs to be notified
    std::vector<std::unique_ptr<ScheduledMeeting>> mUpdatedOcurrences;

    bool mPublicChat = false;  // whether the chat is public or private
    byte flags = 0;     // currently only used for "archive" flag at first bit
    void deleteSchedMeeting(const handle sm)
    {
        mScheduledMeetings.erase(sm);
        mSchedMeetingsChanged.insert(sm);
    }

    int tag = -1;    // source tag, to identify own changes

public:
    TextChat(const bool publicChat);
    ~TextChat() = default;

    bool serialize(string *d) const override;
    static TextChat* unserialize(class MegaClient *client, string *d);

    void setChatId(handle newId);
    handle getChatId() const;
    void setOwnPrivileges(privilege_t p);
    privilege_t getOwnPrivileges() const;
    void setShard(int sh);
    int getShard() const;
    void addUserPrivileges(handle uid, privilege_t p);
    bool updateUserPrivileges(handle uid, privilege_t p);
    bool removeUserPrivileges(handle uid);
    void setUserPrivileges(userpriv_vector* pvs);
    const userpriv_vector* getUserPrivileges() const;
    void setGroup(bool g);
    bool getGroup() const;
    void setTitle(const string& t);
    const string& getTitle() const;
    void setUnifiedKey(const string& uk);
    const string& getUnifiedKey() const;
    void setOwnUser(handle u);
    handle getOwnUser() const;
    void setTs(m_time_t t);
    m_time_t getTs() const;
    const attachments_map& getAttachments() const;
    handle_set getUsersOfAttachment(handle a) const;
    bool isUserOfAttachment(handle a, handle uid) const;
    void addUserForAttachment(handle a, handle uid);
    void setMeeting(bool m);
    bool getMeeting() const;
    byte getChatOptions() const;
    bool hasScheduledMeeting(handle smid) const;
    const handle_set& getSchedMeetingsChanged() const;
    void clearSchedMeetingsChanged();
    const vector<std::unique_ptr<ScheduledMeeting>>& getUpdatedOcurrences() const;
    void setTag(int newTag);
    int getTag() const;
    void resetTag();

    struct
    {
        bool attachments : 1;
        bool flags : 1;
        bool mode : 1;
        bool options : 1;
        bool schedOcurrReplace : 1;
        bool schedOcurrAppend : 1;
    } changed = {};

    // return false if failed
    bool setNodeUserAccess(handle h, handle uh, bool revoke = false);
    bool addOrUpdateChatOptions(int speakRequest = -1, int waitingRoom = -1, int openInvite = -1);
    bool setFlag(bool value, uint8_t offset = 0xFF);
    bool setFlags(byte newFlags);
    bool isFlagSet(uint8_t offset) const;
    void clearUpdatedSchedMeetingOccurrences();
    void addUpdatedSchedMeetingOccurrence(std::unique_ptr<ScheduledMeeting> sm);
    ErrorCodes setMode(bool pubChat);
    bool publicChat() const;

    // add or update a scheduled meeting, SDK adquires the ownership of provided ScheduledMeeting
    bool addOrUpdateSchedMeeting(std::unique_ptr<ScheduledMeeting> sm, bool notify = true);

    // add a scheduled meeting, SDK adquires the ownership of provided ScheduledMeeting
    bool addSchedMeeting(std::unique_ptr<ScheduledMeeting> sm, bool notify = true);

    // removes a scheduled meeting given a scheduled meeting id
    bool removeSchedMeeting(handle schedId);

    // removes all scheduled meetings in provided list as param
    void removeSchedMeetingsList(const handle_set& schedList);

    // removes all scheduled meeting whose parent scheduled meeting id, is equal to parentSchedId provided
    // returns handle_set with the meeting id of the removed children
    handle_set removeChildSchedMeetings(handle parentSchedId);

    // updates scheduled meeting, SDK adquires the ownership of provided ScheduledMeeting
    bool updateSchedMeeting(std::unique_ptr<ScheduledMeeting> sm);

    // returns a scheduled meeting (if any) whose schedId is equal to provided id. Otherwise returns nullptr
    const ScheduledMeeting* getSchedMeetingById(handle meetingID) const;

    // returns a map of schedId to ScheduledMeeting
    const map<handle/*schedId*/, std::unique_ptr<ScheduledMeeting>>& getSchedMeetings() const;
};

#endif

} //namespace

#endif
