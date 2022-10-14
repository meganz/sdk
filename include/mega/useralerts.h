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
    static const nameid type_ipc = MAKENAMEID3('i', 'p', 'c');                      // incoming pending contact
    static const nameid type_c = 'c';                                               // contact change
    static const nameid type_upci = MAKENAMEID4('u', 'p', 'c', 'i');                // updating pending contact incoming
    static const nameid type_upco = MAKENAMEID4('u', 'p', 'c', 'o');                // updating pending contact outgoing
    static const nameid type_share = MAKENAMEID5('s', 'h', 'a', 'r', 'e');          // new shared node
    static const nameid type_dshare = MAKENAMEID6('d', 's', 'h', 'a', 'r', 'e');    // deleted shared node
    static const nameid type_put = MAKENAMEID3('p', 'u', 't');                      // new shared nodes
    static const nameid type_d = 'd';                                               // removed shared node
    static const nameid type_u = 'u';                                               // updated shared node
    static const nameid type_psts = MAKENAMEID4('p', 's', 't', 's');                // payment
    static const nameid type_pses = MAKENAMEID4('p', 's', 'e', 's');                // payment reminder
    static const nameid type_ph = MAKENAMEID2('p', 'h');                            // takedown

    using handle_alerttype_map_t = map<handle, nameid>;

    struct Base
    {
        // shared fields from the notification or action
        nameid type;
        m_time_t timestamp;
        handle userHandle;
        string userEmail;
        int tag;

        // incremented for each new one.  There will be gaps sometimes due to merging.
        unsigned int id;

        // user already saw it (based on 'last notified' time)
        bool seen;

        // if false, not worth showing, eg obsolete payment reminder
        bool relevant;

        Base(UserAlertRaw& un, unsigned int id);
        Base(nameid t, handle uh, const string& email, m_time_t timestamp, unsigned int id);
        virtual ~Base();

        // get the same text the webclient would show for this alert (in english)
        virtual void text(string& header, string& title, MegaClient* mc);

        // look up the userEmail again in case it wasn't available before (or was changed)
        virtual void updateEmail(MegaClient* mc);

        virtual bool checkprovisional(handle ou, MegaClient* mc);
    };

    struct IncomingPendingContact : public Base
    {
        handle mPcrHandle = UNDEF;

        bool requestWasDeleted;
        bool requestWasReminded;

        IncomingPendingContact(UserAlertRaw& un, unsigned int id);
        IncomingPendingContact(m_time_t dts, m_time_t rts, handle p, const string& email, m_time_t timestamp, unsigned int id);

        void initTs(m_time_t dts, m_time_t rts);

        virtual void text(string& header, string& title, MegaClient* mc);
    };

    struct ContactChange : public Base
    {
        int action;
        handle otherUserHandle;

        ContactChange(UserAlertRaw& un, unsigned int id);
        ContactChange(int c, handle uh, const string& email, m_time_t timestamp, unsigned int id);
        virtual void text(string& header, string& title, MegaClient* mc);
        virtual bool checkprovisional(handle ou, MegaClient* mc);
    };

    struct UpdatedPendingContactIncoming : public Base
    {
        int action;

        UpdatedPendingContactIncoming(UserAlertRaw& un, unsigned int id);
        UpdatedPendingContactIncoming(int s, handle uh, const string& email, m_time_t timestamp, unsigned int id);
        virtual void text(string& header, string& title, MegaClient* mc);
    };

    struct UpdatedPendingContactOutgoing : public Base
    {
        int action;

        UpdatedPendingContactOutgoing(UserAlertRaw& un, unsigned int id);
        UpdatedPendingContactOutgoing(int s, handle uh, const string& email, m_time_t timestamp, unsigned int id);
        virtual void text(string& header, string& title, MegaClient* mc);
    };

    struct NewShare : public Base
    {
        handle folderhandle;

        NewShare(UserAlertRaw& un, unsigned int id);
        NewShare(handle h, handle uh, const string& email, m_time_t timestamp, unsigned int id);
        virtual void text(string& header, string& title, MegaClient* mc);
    };

    struct DeletedShare : public Base
    {
        handle folderHandle;
        string folderPath;
        string folderName;
        handle ownerHandle;

        DeletedShare(UserAlertRaw& un, unsigned int id);
        DeletedShare(handle uh, const string& email, handle removerhandle, handle folderhandle, m_time_t timestamp, unsigned int id);
        virtual void text(string& header, string& title, MegaClient* mc);
        virtual void updateEmail(MegaClient* mc);
    };

    struct NewSharedNodes : public Base
    {
        handle parentHandle;
        vector<handle> fileNodeHandles;
        vector<handle> folderNodeHandles;

        NewSharedNodes(UserAlertRaw& un, unsigned int id);
        NewSharedNodes(handle uh, handle ph, m_time_t timestamp, unsigned int id,
                       handle_alerttype_map_t alertTypePerFileNode,
                       handle_alerttype_map_t alertTypePerFolderNode);

        virtual void text(string& header, string& title, MegaClient* mc);
    };

    struct RemovedSharedNode : public Base
    {
        vector<handle> nodeHandles;

        RemovedSharedNode(UserAlertRaw& un, unsigned int id);
        RemovedSharedNode(handle uh, m_time_t timestamp, unsigned int id,
                          handle_alerttype_map_t alertTypePerFileNode,
                          handle_alerttype_map_t alertTypePerFolderNode);

        virtual void text(string& header, string& title, MegaClient* mc);
    };

    struct UpdatedSharedNode : public Base
    {
        vector<handle> nodeHandles;

        UpdatedSharedNode(UserAlertRaw& un, unsigned int id);
        UpdatedSharedNode(handle uh, m_time_t timestamp, unsigned int id,
                          handle_alerttype_map_t alertTypePerFileNode,
                          handle_alerttype_map_t alertTypePerFolderNode);
        virtual void text(string& header, string& title, MegaClient* mc);
    };

    struct Payment : public Base
    {
        bool success;
        int planNumber;

        Payment(UserAlertRaw& un, unsigned int id);
        Payment(bool s, int plan, m_time_t timestamp, unsigned int id);
        virtual void text(string& header, string& title, MegaClient* mc);
        string getProPlanName();
    };

    struct PaymentReminder : public Base
    {
        m_time_t expiryTime;
        PaymentReminder(UserAlertRaw& un, unsigned int id);
        PaymentReminder(m_time_t timestamp, unsigned int id);
        virtual void text(string& header, string& title, MegaClient* mc);
    };

    struct Takedown : public Base
    {
        bool isTakedown;
        bool isReinstate;
        int type;
        handle nodeHandle;

        Takedown(UserAlertRaw& un, unsigned int id);
        Takedown(bool down, bool reinstate, int t, handle nh, m_time_t timestamp, unsigned int id);
        virtual void text(string& header, string& title, MegaClient* mc);
    };
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
    Alerts alerts;

    // collect new/updated alerts to notify the app with
    useralert_vector useralertnotify;

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
    };
    using notedShNodesMap = map<pair<handle, handle>, ff>;
    notedShNodesMap notedSharedNodes;
    notedShNodesMap deletedSharedNodesStash;
    bool notingSharedNodes;
    handle ignoreNodesUnderShare;

    bool isUnwantedAlert(nameid type, int action);
    bool isConvertReadyToAdd(handle originatinguser) const;
    void convertNotedSharedNodes(bool added);
    void clearNotedSharedMembers();

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
    bool procsc_useralert(JSON& jsonsc);

    // add an alert - from alert reply or constructed from actionpackets
    void add(UserAlertRaw& un);
    void add(UserAlert::Base*);

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
    void removeNodeAlerts(Node* n);
    void setNewNodeAlertToUpdateNodeAlert(Node* n);

    // stash removal-alert noted nodes
    void convertStashedDeletedSharedNodes();
    bool isDeletedSharedNodesStashEmpty() const;
    void stashDeletedNotedSharedNodes(handle originatingUser);

    // marks all as seen, and notifies the API also
    void acknowledgeAll();

    // the API notified us another client updated the last acknowleged
    void onAcknowledgeReceived();

    // re-init eg. on logout
    void clear();
};



} // namespace

#endif
