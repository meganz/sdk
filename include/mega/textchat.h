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
            FLAGS_DONT_SEND_EMAILS = 0, // API won't send out calendar emails for this meeting if it's enabled
            FLAGS_SIZE             = 1, // size in bits of flags bitmask
        } scheduled_flags_t;            // 3 Bytes (maximum)

        typedef std::bitset<FLAGS_SIZE> scheduledFlagsBitSet;

        ScheduledFlags() = default;
        ScheduledFlags (unsigned long numericValue);
        ScheduledFlags (const ScheduledFlags* flags);
        ScheduledFlags* copy() const;
        ~ScheduledFlags();

        // getters
        unsigned long getNumericValue() const;
        bool isEmpty() const;
        bool equalTo(const ScheduledFlags*) const;

        // serialization
        bool serialize(string& out) const;
        static ScheduledFlags* unserialize(const string& in);

    private:
        scheduledFlagsBitSet mFlags = 0;
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

        // just for SDK core usage
        typedef vector<int8_t> rules_vector;
        typedef multimap<int8_t, int8_t> rules_map;

        constexpr static int INTERVAL_INVALID = 0;

        ScheduledRules(int freq,
                       int interval = INTERVAL_INVALID,
                       const string& until = std::string(),
                       const rules_vector* byWeekDay = nullptr,
                       const rules_vector* byMonthDay = nullptr,
                       const rules_map* byMonthWeekDay = nullptr);

        ScheduledRules(const ScheduledRules* rules);
        ScheduledRules* copy() const;
        ~ScheduledRules();

        // getters
        ScheduledRules::freq_type_t freq() const;
        int interval() const;
        const std::string &until() const;
        const rules_vector* byWeekDay() const;
        const rules_vector* byMonthDay() const;
        const rules_map* byMonthWeekDay() const;
        bool isValid() const;
        const char* freqToString() const;
        bool equalTo(const ScheduledRules*) const;
        static int stringToFreq (const char* freq);
        static bool isValidFreq(int freq) { return (freq >= FREQ_DAILY && freq <= FREQ_MONTHLY); }
        static bool isValidInterval(int interval) { return interval > INTERVAL_INVALID; }

        // serialization
        bool serialize(string& out) const;
        static ScheduledRules* unserialize(const string& in);

    private:
        // scheduled meeting frequency (DAILY | WEEKLY | MONTHLY), this is used in conjunction with interval to allow for a repeatable skips in the event timeline
        freq_type_t mFreq;

        // repetition interval in relation to the frequency
        int mInterval = 0;

        // specifies when the repetitions should end
        std::string mUntil;

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
    ScheduledMeeting(handle chatid, const string& timezone, const string& startDateTime, const string& endDateTime,
                     const string& title, const string& description, handle organizerUserId, handle schedId = UNDEF,
                     handle parentSchedId = UNDEF, int cancelled = -1, const string& attributes = std::string(),
                     const string& overrides = std::string(), ScheduledFlags* flags = nullptr, ScheduledRules* rules = nullptr);

    ScheduledMeeting(const ScheduledMeeting *scheduledMeeting);
    ScheduledMeeting* copy() const;
    ~ScheduledMeeting();

    // setters
    void setSchedId(handle schedId);

    // getters
    handle chatid() const;
    handle organizerUserid() const;
    handle schedId() const;
    handle parentSchedId() const;
    const std::string &timezone() const;
    const std::string &startDateTime() const;
    const std::string &endDateTime() const;
    const std::string &title() const;
    const std::string &description() const;
    const std::string &attributes() const;
    const std::string &overrides() const;
    int cancelled() const;
    const ScheduledFlags* flags() const;
    const ScheduledRules* rules() const;
    bool isValid() const;

    // check if 2 scheduled meetings objects are equal or not
    bool equalTo(const ScheduledMeeting* sm) const;

    // serialization
    bool serialize(string& out) const;
    static ScheduledMeeting* unserialize(const std::string &in, handle chatid);

private:
    // chat handle
    handle mChatid;

    // organizer user handle
    handle mOrganizerUserId;

    // scheduled meeting handle
    handle mSchedId;

    // parent scheduled meeting handle
    handle mParentSchedId;

    // timeZone
    std::string mTimezone;

    // start dateTime (format: 20220726T133000)
    std::string mStartDateTime;

    // end dateTime (format: 20220726T133000)
    std::string mEndDateTime;

    // meeting title
    std::string mTitle;

    // meeting description
    std::string mDescription;

    // attributes to store any additional data
    std::string mAttributes;

    // start dateTime of the original meeting series event to be replaced (format: 20220726T133000)
    std::string mOverrides;

    // cancelled flag
    int mCancelled;

    // flags bitmask (used to store additional boolean settings as a bitmask)
    std::unique_ptr<ScheduledFlags> mFlags;

    // scheduled meetings rules
    std::unique_ptr<ScheduledRules> mRules;
};

struct TextChat : public Cacheable
{
    enum {
        FLAG_OFFSET_ARCHIVE = 0
    };

    handle id;
    privilege_t priv;
    int shard;
    userpriv_vector *userpriv;
    bool group;
    string title;        // byte array
    string unifiedKey;   // byte array
    handle ou;
    m_time_t ts;     // creation time
    attachments_map attachedNodes;
    bool publicchat;  // whether the chat is public or private
    bool meeting;     // chat is meeting room
    byte chatOptions; // each chat option is represented in 1 bit (check ChatOptions struct at types.h)

    // maps a scheduled meeting id to a scheduled meeting
    // a scheduled meetings allows the user to specify an event that will occur in the future (check ScheduledMeeting class documentation)
    map<handle/*schedId*/, std::unique_ptr<ScheduledMeeting>> mScheduledMeetings;

    // list of scheduled meetings changed
    std::vector<handle> mSchedMeetingsChanged;

    // maps a scheduled meeting id to a scheduled meeting occurrence
    // a scheduled meetings ocurrence is an event based on a scheduled meeting
    // a scheduled meeting could have one or multiple ocurrences (unique key: <schedId, startdatetime>)
    // (check ScheduledMeeting class documentation)
    multimap<handle/*schedId*/, std::unique_ptr<ScheduledMeeting>> mScheduledMeetingsOcurrences;


private:        // use setter to modify these members
    byte flags;     // currently only used for "archive" flag at first bit

public:
    int tag;    // source tag, to identify own changes

    TextChat();
    ~TextChat();

    bool serialize(string *d);
    static TextChat* unserialize(class MegaClient *client, string *d);

    void setTag(int tag);
    int getTag();
    void resetTag();

    struct
    {
        bool attachments : 1;
        bool flags : 1;
        bool mode : 1;
        bool options : 1;
        bool schedOcurr : 1;
    } changed;

    // return false if failed
    bool setNodeUserAccess(handle h, handle uh, bool revoke = false);
    bool addOrUpdateChatOptions(int speakRequest = -1, int waitingRoom = -1, int openInvite = -1);
    bool setFlag(bool value, uint8_t offset = 0xFF);
    bool setFlags(byte newFlags);
    bool isFlagSet(uint8_t offset) const;
    bool setMode(bool publicchat);

    // scheduled meetings ocurrences
    void addSchedMeetingOccurrence(const ScheduledMeeting* sm);
    void clearSchedMeetingOccurrences();

    // scheduled meetings
    bool addOrUpdateSchedMeeting(const ScheduledMeeting *sm, bool notify = true);
    bool addSchedMeeting(const ScheduledMeeting *sm, bool notify = true);
    bool removeSchedMeeting(handle schedId);
    unsigned int removeChildSchedMeetings(handle parentSchedId);
    bool updateSchedMeeting(const ScheduledMeeting *sm);
    ScheduledMeeting* getSchedMeetingById(handle id);
};

typedef vector<TextChat*> textchat_vector;
typedef map<handle, TextChat*> textchat_map;
#endif

} //namespace

#endif
