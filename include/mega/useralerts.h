/**
 * @file mega/usernotifications.h
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

#ifndef MEGAUSERNOTIFICATIONS_H
#define MEGAUSERNOTIFICATIONS_H 1

#include "json.h"
#include "name_id.h"

#include <bitset>

namespace mega {

struct UserAlertRaw
{
    // notifications have a very wide range of fields; so for most we interpret them once we know the type.
    map<nameid, string> fields;

    struct handletype {
        handle h;    // file/folder
        int t;       // type
    };

    nameid t;  // notification type

    JSON field(nameid nid) const;
    bool has(nameid nid) const;

    int getint(nameid nid, int) const;
    int64_t getint64(nameid nid, int64_t) const;
    handle gethandle(nameid nid, int handlesize, handle) const;
    nameid getnameid(nameid nid, nameid) const;
    string getstring(nameid nid, const char*) const;

    bool gethandletypearray(nameid nid, vector<handletype>& v) const;
    bool getstringarray(nameid nid, vector<string>& v) const;

    UserAlertRaw();
};

struct UserAlertPendingContact
{
    handle u; // user handle
    string m; // email
    vector<string> m2; // email list
    string n; // name

    UserAlertPendingContact();
};

namespace UserAlert
{
static constexpr nameid type_ipc = name_id::ipc; // incoming pending contact
static constexpr nameid type_c = name_id::c; // contact change
static constexpr nameid type_upci = name_id::upci; // updating pending contact incoming
static constexpr nameid type_upco = name_id::upco; // updating pending contact outgoing
static constexpr nameid type_share = name_id::share; // new share
static constexpr nameid type_dshare = name_id::dshare; // deleted share
static constexpr nameid type_put = name_id::put; // new shared nodes
static constexpr nameid type_d = name_id::d; // removed shared node
static constexpr nameid type_u = name_id::u; // updated shared node
static constexpr nameid type_psts = name_id::psts; // payment
    static const nameid type_psts_v2 = MAKENAMEID7('p', 's', 't', 's', '_', 'v', '2'); // payment v2 (VPN)
    static const nameid type_pses = MAKENAMEID4('p', 's', 'e', 's');                // payment reminder
    static const nameid type_ph = MAKENAMEID2('p', 'h');                            // takedown
#ifdef ENABLE_CHAT
    static const nameid type_nusm = MAKENAMEID5('m', 'c', 's', 'm', 'p');           // new or updated scheduled meeting
    static const nameid type_dsm = MAKENAMEID5('m', 'c', 's', 'm', 'r');            // deleted scheduled meeting
#endif

    enum userAlertsSubtype
    {
        subtype_invalid   = 0,
        subtype_new_Sched = 1,
        subtype_upd_Sched = 2,
    };

    struct Base;
    static Base* unserializeNewUpdSched(string*, unsigned id);
    using handle_alerttype_map_t = map<handle, nameid>;

    struct Base : public Cacheable
    {
        // shared fields from the notification or action
        nameid type;

        const m_time_t& ts() const { return pst.timestamp; }
        const handle& user() const { return pst.userHandle; }
        const string& email() const { return pst.userEmail; }
        void setEmail(const string& eml) { pst.userEmail = eml; }

        // if false, not worth showing, eg obsolete payment reminder
        bool relevant() const { return pst.relevant; }
        void setRelevant(bool r) { pst.relevant = r; }

        // user already saw it (based on 'last notified' time)
        bool seen() const { return pst.seen; }
        void setSeen(bool s) { pst.seen = s; }

        int tag;

        // incremented for each new one.  There will be gaps sometimes due to merging.
        unsigned int id;

        Base(UserAlertRaw& un, unsigned int id);
        Base(nameid t, handle uh, const string& email, m_time_t timestamp, unsigned int id);
        virtual ~Base();

        // get the same text the webclient would show for this alert (in english)
        virtual void text(string& header, string& title, MegaClient* mc);

        // look up the userEmail again in case it wasn't available before (or was changed)
        virtual void updateEmail(MegaClient* mc);

        virtual bool checkprovisional(handle ou, MegaClient* mc);

        void setRemoved() { mRemoved = true; }
        bool removed() const { return mRemoved; }

    protected:
        struct Persistent // variables to be persisted
        {
            m_time_t timestamp = 0;
            handle userHandle = 0;
            string userEmail;
            bool relevant = true;
            bool seen = false;
        } pst;

        bool serialize(string*) const override;
        static unique_ptr<Persistent> readBase(CacheableReader& r);
        static unique_ptr<Persistent> unserialize(string*);
        friend Base* unserializeNewUpdSched(string*, unsigned id);

    private:
        bool mRemoved = false; // useful to know when to remove from persist db
    };

    struct IncomingPendingContact : public Base
    {
        handle mPcrHandle = UNDEF;

        bool requestWasDeleted;
        bool requestWasReminded;

        IncomingPendingContact(UserAlertRaw& un, unsigned int id);
        IncomingPendingContact(m_time_t dts, m_time_t rts, handle p, const string& email, m_time_t timestamp, unsigned int id);

        void initTs(m_time_t dts, m_time_t rts);

        virtual void text(string& header, string& title, MegaClient* mc) override;

        bool serialize(string*) const override;
        static IncomingPendingContact* unserialize(string*, unsigned id);
    };

    struct ContactChange : public Base
    {
        int action;

        ContactChange(UserAlertRaw& un, unsigned int id);
        ContactChange(int c, handle uh, const string& email, m_time_t timestamp, unsigned int id);
        virtual void text(string& header, string& title, MegaClient* mc) override;
        virtual bool checkprovisional(handle ou, MegaClient* mc) override;

        bool serialize(string*) const override;
        static ContactChange* unserialize(string*, unsigned id);
    };

    struct UpdatedPendingContactIncoming : public Base
    {
        int action;

        UpdatedPendingContactIncoming(UserAlertRaw& un, unsigned int id);
        UpdatedPendingContactIncoming(int s, handle uh, const string& email, m_time_t timestamp, unsigned int id);
        virtual void text(string& header, string& title, MegaClient* mc) override;

        bool serialize(string*) const override;
        static UpdatedPendingContactIncoming* unserialize(string*, unsigned id);
    };

    struct UpdatedPendingContactOutgoing : public Base
    {
        int action;

        UpdatedPendingContactOutgoing(UserAlertRaw& un, unsigned int id);
        UpdatedPendingContactOutgoing(int s, handle uh, const string& email, m_time_t timestamp, unsigned int id);
        virtual void text(string& header, string& title, MegaClient* mc) override;

        bool serialize(string*) const override;
        static UpdatedPendingContactOutgoing* unserialize(string*, unsigned id);
    };

    struct NewShare : public Base
    {
        handle folderhandle;

        NewShare(UserAlertRaw& un, unsigned int id);
        NewShare(handle h, handle uh, const string& email, m_time_t timestamp, unsigned int id);
        virtual void text(string& header, string& title, MegaClient* mc) override;

        bool serialize(string*) const override;
        static NewShare* unserialize(string*, unsigned id);
    };

    struct DeletedShare : public Base
    {
        handle folderHandle;
        string folderPath;
        string folderName;
        handle ownerHandle;

        DeletedShare(UserAlertRaw& un, unsigned int id);
        DeletedShare(handle uh, const string& email, handle removerhandle, handle folderhandle, m_time_t timestamp, unsigned int id);
        virtual void text(string& header, string& title, MegaClient* mc) override;
        virtual void updateEmail(MegaClient* mc) override;

        bool serialize(string*) const override;
        static DeletedShare* unserialize(string*, unsigned id);
    };

    struct NewSharedNodes : public Base
    {
        handle parentHandle;
        vector<handle> fileNodeHandles;
        vector<handle> folderNodeHandles;

        NewSharedNodes(UserAlertRaw& un, unsigned int id);
        NewSharedNodes(handle uh, handle ph, m_time_t timestamp, unsigned int id,
                       vector<handle>&& fileHandles, vector<handle>&& folderHandles);

        virtual void text(string& header, string& title, MegaClient* mc) override;

        bool serialize(string*) const override;
        static NewSharedNodes* unserialize(string*, unsigned id);
    };

    struct RemovedSharedNode : public Base
    {
        vector<handle> nodeHandles;

        RemovedSharedNode(UserAlertRaw& un, unsigned int id);
        RemovedSharedNode(handle uh, m_time_t timestamp, unsigned int id,
                          vector<handle>&& handles);

        virtual void text(string& header, string& title, MegaClient* mc) override;

        bool serialize(string*) const override;
        static RemovedSharedNode* unserialize(string*, unsigned id);
    };

    struct UpdatedSharedNode : public Base
    {
        vector<handle> nodeHandles;

        UpdatedSharedNode(UserAlertRaw& un, unsigned int id);
        UpdatedSharedNode(handle uh, m_time_t timestamp, unsigned int id,
                          vector<handle>&& handles);
        virtual void text(string& header, string& title, MegaClient* mc) override;

        bool serialize(string*) const override;
        static UpdatedSharedNode* unserialize(string*, unsigned id);
    };

    struct Payment : public Base
    {
        bool success;
        int planNumber;

        Payment(UserAlertRaw& un, unsigned int id);
        Payment(bool s, int plan, m_time_t timestamp, unsigned int id, nameid paymentType);
        virtual void text(string& header, string& title, MegaClient* mc) override;
        string getProPlanName();

        bool serialize(string*) const override;
        static Payment* unserialize(string*, unsigned id, nameid paymentType);
    };

    struct PaymentReminder : public Base
    {
        m_time_t expiryTime;
        PaymentReminder(UserAlertRaw& un, unsigned int id);
        PaymentReminder(m_time_t timestamp, unsigned int id);
        virtual void text(string& header, string& title, MegaClient* mc) override;

        bool serialize(string*) const override;
        static PaymentReminder* unserialize(string*, unsigned id);
    };

    struct Takedown : public Base
    {
        bool isTakedown;
        bool isReinstate;
        handle nodeHandle;

        Takedown(UserAlertRaw& un, unsigned int id);
        Takedown(bool down, bool reinstate, int t, handle nh, m_time_t timestamp, unsigned int id);
        virtual void text(string& header, string& title, MegaClient* mc) override;

        bool serialize(string*) const override;
        static Takedown* unserialize(string*, unsigned id);
    };

#ifdef ENABLE_CHAT
    struct NewScheduledMeeting : public Base
    {
        handle mChatid = UNDEF;
        handle mSchedMeetingHandle = UNDEF;
        handle mParentSchedId = UNDEF;
        m_time_t mStartDateTime = mega_invalid_timestamp; // overrides param

        NewScheduledMeeting(UserAlertRaw& un, unsigned int id);
        NewScheduledMeeting(handle _ou, m_time_t _ts, unsigned int _id, handle _chatid, handle _sm, handle _parentSchedId, m_time_t _startDateTime)
            : Base(UserAlert::type_nusm, _ou, string(), _ts, _id)
            , mChatid(_chatid)
            , mSchedMeetingHandle(_sm)
            , mParentSchedId(_parentSchedId)
            , mStartDateTime(_startDateTime)
            {}

        virtual void text(string& header, string& title, MegaClient* mc) override;
        bool serialize(string* d) const override;
        static NewScheduledMeeting* unserialize(string*, unsigned id);
    };

    struct UpdatedScheduledMeeting : public Base
    {
        class Changeset
        {
        public:
            enum
            {
                CHANGE_TYPE_TITLE       = 0x01,
                CHANGE_TYPE_DESCRIPTION = 0x02,
                CHANGE_TYPE_CANCELLED   = 0x04,
                CHANGE_TYPE_TIMEZONE    = 0x08,
                CHANGE_TYPE_STARTDATE   = 0x10,
                CHANGE_TYPE_ENDDATE     = 0x20,
                CHANGE_TYPE_RULES       = 0x40,

                CHANGE_TYPE_SIZE        = 7 // remember to update this when adding new values
            };
            struct StrChangeset { string oldValue, newValue; };
            struct TsChangeset  { m_time_t oldValue, newValue; };

            const StrChangeset* getUpdatedTitle() const         { return mUpdatedTitle.get(); }
            const StrChangeset* getUpdatedTimeZone() const      { return mUpdatedTimeZone.get(); }
            const TsChangeset* getUpdatedStartDateTime() const  { return mUpdatedStartDateTime.get(); }
            const TsChangeset* getUpdatedEndDateTime() const    { return mUpdatedEndDateTime.get(); }
            uint64_t getChanges() const                         { return mUpdatedFields.to_ullong(); }
            bool hasChanged(uint64_t changeType) const
            {
                return getChanges() & changeType;
            }

            string changeToString(uint64_t changeType) const;
            void addChange(uint64_t changeType, StrChangeset* = nullptr, TsChangeset* = nullptr);
            Changeset() = default;
            Changeset(const std::bitset<CHANGE_TYPE_SIZE>& _bs,
                      unique_ptr<StrChangeset>& _titleCS,
                      unique_ptr<StrChangeset>& _tzCS,
                      unique_ptr<TsChangeset>& _sdCS,
                      unique_ptr<TsChangeset>& _edCS);

            Changeset(const Changeset& src) { replaceCurrent(src); }
            Changeset& operator=(const Changeset& src) { replaceCurrent(src); return *this; }
            Changeset(Changeset&&) = default;
            Changeset& operator=(Changeset&&) = default;
            ~Changeset() = default;

        private:
            /*
             * invariant:
             * - bitset size must be the maximum types of changes allowed
             * - if title changed, there must be previous and new title string
             * - if timezone changed, there must be previous and new timezone
             * - if StartDateTime changed, there must be previous and new StartDateTime
             * - if EndDateTime changed, there must be previous and new EndDateTime
             */
            bool invariant() const
            {
                const auto changes = getChanges();
                return (mUpdatedFields.size() == CHANGE_TYPE_SIZE
                        && (!(changes & CHANGE_TYPE_TITLE)     || mUpdatedTitle)
                        && (!(changes & CHANGE_TYPE_TIMEZONE)  || mUpdatedTimeZone)
                        && (!(changes & CHANGE_TYPE_STARTDATE) || mUpdatedStartDateTime)
                        && (!(changes & CHANGE_TYPE_ENDDATE)   || mUpdatedEndDateTime));
            }

            void replaceCurrent(const Changeset& src)
            {
                mUpdatedFields = src.mUpdatedFields;
                if (src.mUpdatedTitle)
                {
                    mUpdatedTitle.reset(new StrChangeset{src.mUpdatedTitle->oldValue, src.mUpdatedTitle->newValue});
                }
                if (src.mUpdatedTimeZone)
                {
                    mUpdatedTimeZone.reset(new StrChangeset{src.mUpdatedTimeZone->oldValue, src.mUpdatedTimeZone->newValue});
                }
                if (src.mUpdatedStartDateTime)
                {
                    mUpdatedStartDateTime.reset(new TsChangeset{src.mUpdatedStartDateTime->oldValue, src.mUpdatedStartDateTime->newValue});
                }
                if (src.mUpdatedEndDateTime)
                {
                    mUpdatedEndDateTime.reset(new TsChangeset{src.mUpdatedEndDateTime->oldValue, src.mUpdatedEndDateTime->newValue});
                }
            }

            std::bitset<CHANGE_TYPE_SIZE> mUpdatedFields;
            unique_ptr<StrChangeset> mUpdatedTitle;
            unique_ptr<StrChangeset> mUpdatedTimeZone;
            unique_ptr<TsChangeset> mUpdatedStartDateTime;
            unique_ptr<TsChangeset> mUpdatedEndDateTime;
        };

        handle mChatid = UNDEF;
        handle mSchedMeetingHandle = UNDEF;
        handle mParentSchedId = UNDEF;
        m_time_t mStartDateTime = mega_invalid_timestamp; // overrides param
        Changeset mUpdatedChangeset;

        UpdatedScheduledMeeting(UserAlertRaw& un, unsigned int id);
        UpdatedScheduledMeeting(handle _ou, m_time_t _ts, unsigned int _id, handle _chatid, handle _sm, handle _parentSchedId, m_time_t _startDateTime, Changeset&& _cs)
            : Base(UserAlert::type_nusm, _ou, string(),  _ts, _id)
            , mChatid(_chatid)
            , mSchedMeetingHandle(_sm)
            , mParentSchedId(_parentSchedId)
            , mStartDateTime(_startDateTime)
            , mUpdatedChangeset(_cs)
            {}

        virtual void text(string& header, string& title, MegaClient* mc) override;
        bool serialize(string*) const override;
        static UpdatedScheduledMeeting* unserialize(string*, unsigned id);
    };

    struct DeletedScheduledMeeting : public Base
    {
        handle mChatid = UNDEF;
        handle mSchedMeetingHandle = UNDEF;
        DeletedScheduledMeeting(UserAlertRaw& un, unsigned int id);
        DeletedScheduledMeeting(handle _ou, m_time_t _ts, unsigned int _id, handle _chatid, handle _sm)
            : Base(UserAlert::type_dsm, _ou, string(), _ts, _id)
            , mChatid(_chatid)
            , mSchedMeetingHandle(_sm)
            {}

        virtual void text(string& header, string& title, MegaClient* mc) override;
        bool serialize(string* d) const override;
        static DeletedScheduledMeeting* unserialize(string*, unsigned id);
    };
#endif
};

struct UserAlertFlags
{
    bool cloud_enabled;
    bool contacts_enabled;

    bool cloud_newfiles;
    bool cloud_newshare;
    bool cloud_delshare;
    bool contacts_fcrin;
    bool contacts_fcrdel;
    bool contacts_fcracpt;

    UserAlertFlags();
};

struct UserAlerts
{
private:
    MegaClient& mc;
    unsigned int nextid;

public:
    typedef deque<UserAlert::Base*> Alerts;
    Alerts alerts; // alerts created from sc (action packets) or received "raw" from sc50; newest go at the end

    // collect new/updated alerts to notify the app with; non-owning container of pointers owned by `alerts`
    useralert_vector useralertnotify; // alerts to be notified to the app (new/updated/removed)

    // set true after our initial query to MEGA to get the last 50 alerts on startup
    bool begincatchup;
    bool catchupdone;
    m_time_t catchup_last_timestamp;

private:
    map<handle, UserAlertPendingContact> pendingContactUsers;
    handle lsn, fsn;
    m_time_t lastTimeDelta;
    UserAlertFlags flags;
    bool provisionalmode;
    std::vector<UserAlert::Base*> provisionals;

    struct ff {
        m_time_t timestamp = 0;
        UserAlert::handle_alerttype_map_t alertTypePerFileNode;
        UserAlert::handle_alerttype_map_t alertTypePerFolderNode;

        vector<handle> fileHandles() const
        {
            vector<handle> v;
            std::transform(alertTypePerFileNode.begin(), alertTypePerFileNode.end(), std::back_inserter(v), [](const pair<handle, nameid>& p) { return p.first; });
            return v;
        }

        vector<handle> folderHandles() const
        {
            vector<handle> v;
            std::transform(alertTypePerFolderNode.begin(), alertTypePerFolderNode.end(), std::back_inserter(v), [](const pair<handle, nameid>& p) { return p.first; });
            return v;
        }

        bool areNodeVersions() const { return mAreNodeVersions; }
        void areNodeVersions(const bool theyAre) { mAreNodeVersions = areNodeVersions() || theyAre; }

        void squash(const ff& rhs);
    private:
	bool mAreNodeVersions = false;
    };
    using notedShNodesMap = map<pair<handle, handle>, ff>; // <<userhandle, parenthandle>,ff>
    notedShNodesMap notedSharedNodes;
    notedShNodesMap deletedSharedNodesStash;
    bool notingSharedNodes;
    handle ignoreNodesUnderShare;

    bool isUnwantedAlert(nameid type, int action);
    bool isConvertReadyToAdd(handle originatinguser) const;
    void convertNotedSharedNodes(bool added);
    void clearNotedSharedMembers();

    void trimAlertsToMaxCount(); // mark as removed the excess from 200
    void notifyAlert(UserAlert::Base* alert, bool seen, int tag);

    UserAlert::Base* findAlertToCombineWith(const UserAlert::Base* a, nameid t) const;

    bool containsRemovedNodeAlert(handle nh, const UserAlert::Base* a) const;
    // Returns param `a` downcasted if `nh` is found and erased; `nullptr` otherwise
    UserAlert::NewSharedNodes* eraseNodeHandleFromNewShareNodeAlert(handle nh, UserAlert::Base* a);
    // Returns param `a` downcasted if `nh` is found and erased; `nullptr` otherwise
    UserAlert::RemovedSharedNode* eraseNodeHandleFromRemovedSharedNode(handle nh, UserAlert::Base* a);
    pair<bool, UserAlert::handle_alerttype_map_t::difference_type>
        findNotedSharedNodeIn(handle nodeHandle, const notedShNodesMap& notedSharedNodesMap) const;
    bool isSharedNodeNotedAsRemoved(handle nodeHandleToFind) const;
    bool isSharedNodeNotedAsRemovedFrom(handle nodeHandleToFind, const notedShNodesMap& notedSharedNodesMap) const;
    bool removeNotedSharedNodeFrom(notedShNodesMap::iterator itToNodeToRemove, Node* node, notedShNodesMap& notedSharedNodesMap);
    bool removeNotedSharedNodeFrom(Node* n, notedShNodesMap& notedSharedNodesMap);
    bool setNotedSharedNodeToUpdate(Node* n);
public:

    // This is a separate class to encapsulate some MegaClient functionality
    // but it still needs to interact with other elements.
    UserAlerts(MegaClient&);
    ~UserAlerts();

    unsigned int nextId();

    // process notification response from MEGA
    bool procsc_useralert(JSON& jsonsc); // sc50

    // add an alert - from alert reply or constructed from actionpackets
    void add(UserAlertRaw& un); // from sc50
    void add(UserAlert::Base*); // from action packet or persistence

    // keep track of incoming nodes in shares, and convert to a notification
    void beginNotingSharedNodes();
    void noteSharedNode(handle user, int type, m_time_t timestamp, Node* n, nameid alertType = UserAlert::type_d);
    void convertNotedSharedNodes(bool added, handle originatingUser);
    void ignoreNextSharedNodesUnder(handle h);


    // enter provisional mode, added items will be checked for suitability before actually adding
    void startprovisional();
    void evalprovisional(handle originatinguser);

    // update node alerts management
    bool isHandleInAlertsAsRemoved(handle nodeHandleToFind) const;
    template <typename T>
    void eraseAlertsFromContainer(T& container, const set<UserAlert::Base*>& toErase)
    {
        container.erase(
            remove_if(begin(container), end(container),
                      [&toErase](UserAlert::Base* a) { return toErase.find(a) != end(toErase); })
            , end(container));
    }
    void removeNodeAlerts(Node* n);
    void setNewNodeAlertToUpdateNodeAlert(Node* n);

    void initscalerts(); // persist alerts received from sc50
    void purgescalerts(); // persist alerts from action packets
    bool unserializeAlert(string* d, uint32_t dbid);

    // stash removal-alert noted nodes
    void purgeNodeVersionsFromStash();
    void convertStashedDeletedSharedNodes();
    bool isDeletedSharedNodesStashEmpty() const;
    void stashDeletedNotedSharedNodes(handle originatingUser);

    // request from API to acknowledge all alerts
    void acknowledgeAll();

    // marks all as seen, after API request has succeeded
    void acknowledgeAllSucceeded();

    // the API notified us another client updated the last acknowleged
    void onAcknowledgeReceived();

    // re-init eg. on logout
    void clear();
};



} // namespace

#endif
