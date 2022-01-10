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

#include "mega.h"

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
    void start() override;
    void update();
    void completed(Transfer*, LocalNode*) override;
    void terminated() override;

    AppFileGet(Node*, NodeHandle = NodeHandle(), byte* = NULL, m_off_t = -1, m_time_t = 0, string* = NULL, string* = NULL, const string& targetfolder = "");
    ~AppFileGet();
};

struct AppFilePut : public AppFile
{
    void start() override;
    void update();
    void completed(Transfer*, LocalNode*) override;
    void terminated() override;

    void displayname(string*);

    AppFilePut(const LocalPath&, NodeHandle, const char*);
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

    void request_error(error) override;

    void request_response_progress(m_off_t, m_off_t) override;

    void prelogin_result(int version, string* email, string *salt, error e) override;
    void login_result(error) override;
    void multifactorauthdisable_result(error) override;
    void multifactorauthsetup_result(string *code, error e) override;
    void multifactorauthcheck_result(int enabled) override;

    void ephemeral_result(error) override;
    void ephemeral_result(handle, const byte*) override;
    void cancelsignup_result(error) override;

    void whyamiblocked_result(int) override;

    void sendsignuplink_result(error) override;
    void querysignuplink_result(error) override;
    void querysignuplink_result(handle, const char*, const char*, const byte*, const byte*, const byte*, size_t) override;
    void confirmsignuplink_result(error) override;
    void confirmsignuplink2_result(handle, const char*, const char*, error) override;
    void setkeypair_result(error) override;

    void getrecoverylink_result(error) override;
    void queryrecoverylink_result(error) override;
    void queryrecoverylink_result(int type, const char *email, const char *ip, time_t ts, handle uh, const vector<string> *emails) override;
    void getprivatekey_result(error,  const byte *privk, const size_t len_privk) override;
    void confirmrecoverylink_result(error) override;
    void confirmcancellink_result(error) override;
    void validatepassword_result(error) override;
    void getemaillink_result(error) override;
    void confirmemaillink_result(error) override;

    void users_updated(User**, int) override;
    void useralerts_updated(UserAlert::Base** ua, int count) override;
    void nodes_updated(Node**, int) override;
    void pcrs_updated(PendingContactRequest**, int) override;
    void nodes_current() override;
    void account_updated() override;
    void notify_confirmation(const char *email) override;

#ifdef ENABLE_CHAT
    void chatcreate_result(TextChat *, error) override;
    void chatinvite_result(error) override;
    void chatremove_result(error) override;
    void chaturl_result(string *, error) override;
    void chatgrantaccess_result(error) override;
    void chatremoveaccess_result(error) override;
    virtual void chatupdatepermissions_result(error) override;
    virtual void chattruncate_result(error) override;
    virtual void chatsettitle_result(error) override;
    virtual void chatpresenceurl_result(string *, error) override;
    void chatlink_result(handle, error) override;
    void chatlinkclose_result(error) override;
    void chatlinkurl_result(handle, int, string*, string*, int, m_time_t, bool, handle, error) override;
    void chatlinkjoin_result(error) override;

    void chats_updated(textchat_map*, int) override;

    static void printChatInformation(TextChat *);
    static string getPrivilegeString(privilege_t priv);

    void richlinkrequest_result(string*, error) override;
#endif

    void unlink_result(handle, error) override;

    void fetchnodes_result(const Error&) override;

    void putnodes_result(const Error&, targettype_t, vector<NewNode>&, bool targetOverride) override;

    void setpcr_result(handle, error, opcactions_t) override;
    void updatepcr_result(error, ipcactions_t) override;

    void fa_complete(handle, fatype, const char*, uint32_t) override;
    int fa_failed(handle, fatype, int, error) override;

    void putfa_result(handle, fatype, error) override;

    void removecontact_result(error) override;
    void putua_result(error) override;
    void getua_result(error) override;
    void getua_result(byte*, unsigned, attr_t) override;
    void getua_result(TLVstore *, attr_t) override;
#ifdef DEBUG
    void delua_result(error) override;
    void senddevcommand_result(int) override;
#endif

    void querytransferquota_result(int) override;

    void account_details(AccountDetails*, bool, bool, bool, bool, bool, bool) override;
    void account_details(AccountDetails*, error) override;

    // sessionid is undef if all sessions except the current were killed
    void sessions_killed(handle sessionid, error e) override;

    void openfilelink_result(const Error&) override;
    void openfilelink_result(handle, const byte*, m_off_t, string*, string*, int) override;

    void folderlinkinfo_result(error, handle, handle, string *, string*, m_off_t, uint32_t, uint32_t, m_off_t, uint32_t) override;

    dstime pread_failure(const Error&, int, void*, dstime) override;
    bool pread_data(byte*, m_off_t, m_off_t, m_off_t, m_off_t, void*) override;

    void transfer_added(Transfer*) override;
    void transfer_removed(Transfer*) override;
    void transfer_prepare(Transfer*) override;
    void transfer_failed(Transfer*, const Error&, dstime) override;
    void transfer_update(Transfer*) override;
    void transfer_complete(Transfer*) override;

#ifdef ENABLE_SYNC
    void syncupdate_stateconfig(const SyncConfig& config) override;
    void syncupdate_active(const SyncConfig& config, bool active) override;
    void sync_auto_resume_result(const SyncConfig&, bool attempted, bool hadAnError) override;
    void sync_removed(const SyncConfig& config) override;

    void syncupdate_scanning(bool) override;
    void syncupdate_local_lockretry(bool) override;
    void syncupdate_treestate(const SyncConfig& config, const LocalPath&, treestate_t, nodetype_t) override;

    bool sync_syncable(Sync*, const char*, LocalPath&, Node*) override;
    bool sync_syncable(Sync*, const char*, LocalPath&) override;
#endif

    void changepw_result(error) override;

    void userattr_update(User*, int, const char*) override;
    void resetSmsVerifiedPhoneNumber_result(error e) override;

    void enumeratequotaitems_result(unsigned, handle, unsigned, int, int, unsigned, unsigned,
                                    unsigned, unsigned, const char*, const char*, const char*,
                                    std::unique_ptr<BusinessPlan>) override;
    void enumeratequotaitems_result(unique_ptr<CurrencyData>) override;
    void enumeratequotaitems_result(error) override;
    void additem_result(error) override;
    void checkout_result(const char*, error) override;

    void getmegaachievements_result(AchievementsDetails*, error) override;

    void contactlinkcreate_result(error, handle) override;
    void contactlinkquery_result(error, handle, string*, string*, string*, string*) override;
    void contactlinkdelete_result(error) override;

    void smsverificationsend_result(error) override;
    void smsverificationcheck_result(error, string*) override;

    void getbanners_result(error) override;
    void getbanners_result(vector< tuple<int, string, string, string, string, string, string> >&& banners) override;

    void dismissbanner_result(error) override;
    void backupremove_result(const Error&, handle /*backup id*/) override;

    void reload(const char*) override;
    void clearing() override;

    void notify_retry(dstime, retryreason_t) override;

    static string getExtraInfoErrorString(const Error&);

protected:
#ifdef USE_DRIVE_NOTIFICATIONS
    void drive_presence_changed(bool appeared, const LocalPath& driveRoot) override;
#endif // USE_DRIVE_NOTIFICATIONS
};

struct DemoAppFolder : public DemoApp
{
    void login_result(error);
    void fetchnodes_result(const Error&);

    void nodes_updated(Node **, int);
    void users_updated(User**, int) {}
    void pcrs_updated(PendingContactRequest**, int) {}
};

#include <mega/autocomplete.h>

void exec_apiurl(autocomplete::ACState& s);
void exec_login(autocomplete::ACState& s);
void exec_begin(autocomplete::ACState& s);
void exec_signup(autocomplete::ACState& s);
void exec_cancelsignup(autocomplete::ACState& s);
void exec_confirm(autocomplete::ACState& s);
void exec_session(autocomplete::ACState& s);
void exec_mount(autocomplete::ACState& s);
void exec_ls(autocomplete::ACState& s);
void exec_cd(autocomplete::ACState& s);
void exec_pwd(autocomplete::ACState& s);
void exec_lcd(autocomplete::ACState& s);
void exec_lls(autocomplete::ACState& s);
void exec_lpwd(autocomplete::ACState& s);
void exec_lmkdir(autocomplete::ACState& s);
void exec_import(autocomplete::ACState& s);
void exec_folderlinkinfo(autocomplete::ACState& s);
void exec_open(autocomplete::ACState& s);
void exec_put(autocomplete::ACState& s);
void exec_putq(autocomplete::ACState& s);
void exec_get(autocomplete::ACState& s);
void exec_getq(autocomplete::ACState& s);
void exec_more(autocomplete::ACState& s);
void exec_pause(autocomplete::ACState& s);
void exec_getfa(autocomplete::ACState& s);
void exec_mediainfo(autocomplete::ACState& s);
void exec_smsverify(autocomplete::ACState& s);
void exec_verifiedphonenumber(autocomplete::ACState& s);
void exec_mkdir(autocomplete::ACState& s);
void exec_rm(autocomplete::ACState& s);
void exec_mv(autocomplete::ACState& s);
void exec_cp(autocomplete::ACState& s);
void exec_du(autocomplete::ACState& s);
void exec_export(autocomplete::ACState& s);
void exec_share(autocomplete::ACState& s);
void exec_invite(autocomplete::ACState& s);
void exec_clink(autocomplete::ACState& s);
void exec_ipc(autocomplete::ACState& s);
void exec_showpcr(autocomplete::ACState& s);
void exec_users(autocomplete::ACState& s);
void exec_getua(autocomplete::ACState& s);
void exec_putua(autocomplete::ACState& s);
void exec_delua(autocomplete::ACState& s);
void exec_alerts(autocomplete::ACState& s);
void exec_recentactions(autocomplete::ACState& s);
void exec_recentnodes(autocomplete::ACState& s);
void exec_putbps(autocomplete::ACState& s);
void exec_killsession(autocomplete::ACState& s);
void exec_whoami(autocomplete::ACState& s);
void exec_verifycredentials(autocomplete::ACState& s);
void exec_passwd(autocomplete::ACState& s);
void exec_reset(autocomplete::ACState& s);
void exec_recover(autocomplete::ACState& s);
void exec_cancel(autocomplete::ACState& s);
void exec_email(autocomplete::ACState& s);
void exec_retry(autocomplete::ACState& s);
void exec_recon(autocomplete::ACState& s);
void exec_reload(autocomplete::ACState& s);
void exec_logout(autocomplete::ACState& s);
void exec_locallogout(autocomplete::ACState& s);
void exec_symlink(autocomplete::ACState& s);
void exec_version(autocomplete::ACState& s);
void exec_debug(autocomplete::ACState& s);
void exec_verbose(autocomplete::ACState& s);
void exec_clear(autocomplete::ACState& s);
void exec_codepage(autocomplete::ACState& s);
void exec_log(autocomplete::ACState& s);
void exec_test(autocomplete::ACState& s);
void exec_chats(autocomplete::ACState& s);
void exec_chatc(autocomplete::ACState& s);
void exec_chati(autocomplete::ACState& s);
void exec_chatcp(autocomplete::ACState& s);
void exec_chatr(autocomplete::ACState& s);
void exec_chatu(autocomplete::ACState& s);
void exec_chatup(autocomplete::ACState& s);
void exec_chatpu(autocomplete::ACState& s);
void exec_chatga(autocomplete::ACState& s);
void exec_chatra(autocomplete::ACState& s);
void exec_chatst(autocomplete::ACState& s);
void exec_chata(autocomplete::ACState& s);
void exec_chatl(autocomplete::ACState& s);
void exec_chatsm(autocomplete::ACState& s);
void exec_chatlu(autocomplete::ACState& s);
void exec_chatlj(autocomplete::ACState& s);
void exec_setmaxdownloadspeed(autocomplete::ACState& s);
void exec_setmaxuploadspeed(autocomplete::ACState& s);
void exec_handles(autocomplete::ACState& s);
void exec_httpsonly(autocomplete::ACState& s);
void exec_mfac(autocomplete::ACState& s);
void exec_mfae(autocomplete::ACState& s);
void exec_mfad(autocomplete::ACState& s);
void exec_autocomplete(autocomplete::ACState& s);
void exec_history(autocomplete::ACState& s);
void exec_help(autocomplete::ACState& s);
void exec_quit(autocomplete::ACState& s);
void exec_find(autocomplete::ACState& s);
#ifdef USE_FILESYSTEM
void exec_treecompare(autocomplete::ACState& s);
void exec_querytransferquota(autocomplete::ACState& s);
#endif
void exec_metamac(autocomplete::ACState& s);
void exec_resetverifiedphonenumber(autocomplete::ACState& s);
void exec_banner(autocomplete::ACState& s);
void exec_drivemonitor(autocomplete::ACState& s);
void exec_driveid(autocomplete::ACState& s);

#ifdef ENABLE_SYNC

void exec_syncadd(autocomplete::ACState& s);
void exec_syncrename(autocomplete::ACState& s);
void exec_syncclosedrive(autocomplete::ACState& s);
void exec_syncexport(autocomplete::ACState& s);
void exec_syncimport(autocomplete::ACState& s);
void exec_syncopendrive(autocomplete::ACState& s);
void exec_synclist(autocomplete::ACState& s);
void exec_syncremove(autocomplete::ACState& s);
void exec_syncxable(autocomplete::ACState& s);

#endif // ENABLE_SYNC
