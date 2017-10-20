/**
 * @file megaclient.cpp
 * @brief sample application, interactive GNU Readline CLI
 *
 * (c) 2013 by Mega Limited, Wellsford, New Zealand
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

using namespace mega;

extern MegaClient* client;
extern MegaClient* clientFolder;

extern void megacli();

extern void term_init();
extern void term_restore();
extern void term_echo(int);

extern void read_pw_char(char*, int, int*, char**);

typedef list<struct AppFile*> appfile_list;

struct AppFile : public File
{
    // app-internal sequence number for queue management
    int seqno;

    void progress();

    appfile_list::iterator appxfer_it;

    AppFile();
};

// application-managed GET and PUT queues (only pending and active files)
extern appfile_list appxferq[2];

struct AppFileGet : public AppFile
{
    void start();
    void update();
    void completed(Transfer*, LocalNode*);

    AppFileGet(Node*, handle = UNDEF, byte* = NULL, m_off_t = -1, m_time_t = 0, string* = NULL, string* = NULL);
    ~AppFileGet();
};

struct AppFilePut : public AppFile
{
    void start();
    void update();
    void completed(Transfer*, LocalNode*);

    void displayname(string*);

    AppFilePut(string*, handle, const char*);
    ~AppFilePut();
};

struct AppReadContext
{
    SymmCipher key;
};

class TreeProcListOutShares : public TreeProc
{
public:
    void proc(MegaClient*, Node*);
};

struct DemoApp : public MegaApp
{
    FileAccess* newfile();

    void request_error(error);
    
    void request_response_progress(m_off_t, m_off_t);
    
    void login_result(error);

    void ephemeral_result(error);
    void ephemeral_result(handle, const byte*);

    void sendsignuplink_result(error);
    void querysignuplink_result(error);
    void querysignuplink_result(handle, const char*, const char*, const byte*, const byte*, const byte*, size_t);
    void confirmsignuplink_result(error);
    void setkeypair_result(error);

    virtual void getrecoverylink_result(error);
    virtual void queryrecoverylink_result(error);
    virtual void queryrecoverylink_result(int type, const char *email, const char *ip, time_t ts, handle uh, const vector<string> *emails);    
    virtual void getprivatekey_result(error,  const byte *privk, const size_t len_privk);
    virtual void confirmrecoverylink_result(error);
    virtual void confirmcancellink_result(error);
    virtual void validatepassword_result(error);
    virtual void getemaillink_result(error);
    virtual void confirmemaillink_result(error);

    void users_updated(User**, int);
    void nodes_updated(Node**, int);
    void pcrs_updated(PendingContactRequest**, int);
    void nodes_current();
    void account_updated();
    void notify_confirmation(const char *email);

#ifdef ENABLE_CHAT
    void chatcreate_result(TextChat *, error);
    void chatinvite_result(error);
    void chatremove_result(error);
    void chaturl_result(string *, error);
    void chatgrantaccess_result(error);
    void chatremoveaccess_result(error);
    virtual void chatupdatepermissions_result(error);
    virtual void chattruncate_result(error);
    virtual void chatsettitle_result(error);
    virtual void chatpresenceurl_result(string *, error);

    void chats_updated(textchat_map*, int);

    static void printChatInformation(TextChat *);
    static string getPrivilegeString(privilege_t priv);
#endif

    int prepare_download(Node*);

    void setattr_result(handle, error);
    void rename_result(handle, error);
    void unlink_result(handle, error);

    void fetchnodes_result(error);

    void putnodes_result(error, targettype_t, NewNode*);

    void share_result(error);
    void share_result(int, error);

    void setpcr_result(handle, error, opcactions_t);
    void updatepcr_result(error, ipcactions_t);

    void fa_complete(handle, fatype, const char*, uint32_t);
    int fa_failed(handle, fatype, int, error);

    void putfa_result(handle, fatype, error);

    void removecontact_result(error);
    void putua_result(error);
    void getua_result(error);
    void getua_result(byte*, unsigned);
    void getua_result(TLVstore *);
#ifdef DEBUG
    void delua_result(error);
#endif

    void account_details(AccountDetails*, bool, bool, bool, bool, bool, bool);
    void account_details(AccountDetails*, error);

    // sessionid is undef if all sessions except the current were killed
    void sessions_killed(handle sessionid, error e);

    void exportnode_result(error);
    void exportnode_result(handle, handle);

    void openfilelink_result(error);
    void openfilelink_result(handle, const byte*, m_off_t, string*, string*, int);

    void checkfile_result(handle, error);
    void checkfile_result(handle, error, byte*, m_off_t, m_time_t, m_time_t, string*, string*, string*);

    dstime pread_failure(error, int, void*);
    bool pread_data(byte*, m_off_t, m_off_t, m_off_t, m_off_t, void*);

    void transfer_added(Transfer*);
    void transfer_removed(Transfer*);
    void transfer_prepare(Transfer*);
    void transfer_failed(Transfer*, error);
    void transfer_update(Transfer*);
    void transfer_limit(Transfer*);
    void transfer_complete(Transfer*);

#ifdef ENABLE_SYNC
    void syncupdate_state(Sync*, syncstate_t);
    void syncupdate_scanning(bool);
    void syncupdate_local_folder_addition(Sync*, LocalNode*, const char*);
    void syncupdate_local_folder_deletion(Sync* , LocalNode*);
    void syncupdate_local_file_addition(Sync*, LocalNode*, const char*);
    void syncupdate_local_file_deletion(Sync*, LocalNode*);
    void syncupdate_local_file_change(Sync*, LocalNode*, const char*);
    void syncupdate_local_move(Sync*, LocalNode*, const char*);
    void syncupdate_local_lockretry(bool);
    void syncupdate_get(Sync*, Node*, const char*);
    void syncupdate_put(Sync*, LocalNode*, const char*);
    void syncupdate_remote_file_addition(Sync*, Node*);
    void syncupdate_remote_file_deletion(Sync*, Node*);
    void syncupdate_remote_folder_addition(Sync*, Node*);
    void syncupdate_remote_folder_deletion(Sync*, Node*);
    void syncupdate_remote_copy(Sync*, const char*);
    void syncupdate_remote_move(Sync*, Node*, Node*);
    void syncupdate_remote_rename(Sync*, Node*, const char*);
    void syncupdate_treestate(LocalNode*);

    bool sync_syncable(Sync*, const char*, string*, Node*);
    bool sync_syncable(Sync*, const char*, string*);
#endif

    void changepw_result(error);

    void userattr_update(User*, int, const char*);

    void enumeratequotaitems_result(handle, unsigned, unsigned, unsigned, unsigned, unsigned, const char*);
    void enumeratequotaitems_result(error);
    void additem_result(error);
    void checkout_result(error);
    void checkout_result(const char*);

    void getmegaachievements_result(AchievementsDetails*, error);
    void getwelcomepdf_result(handle, string*, error);

    void reload(const char*);
    void clearing();

    void notify_retry(dstime);
};

struct DemoAppFolder : public DemoApp
{
    void login_result(error);
    void fetchnodes_result(error);

    void nodes_updated(Node **, int);
    void users_updated(User**, int) {}
    void pcrs_updated(PendingContactRequest**, int) {}
};
