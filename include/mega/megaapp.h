/**
 * @file mega/megaapp.h
 * @brief Mega SDK callback interface
 *
 * (c) 2013-2014 by Mega Limited, Wellsford, New Zealand
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

#ifndef MEGA_APP_H
#define MEGA_APP_H 1

#include <mega/types.h>

// FUSE.
#include <mega/fuse/common/mount_event_forward.h>

namespace mega {

struct Notification;
struct UnifiedSync;
class Set;
class SetElement;
struct PerSyncStats;
struct AccountDetails;
class MegaClient;
class LocalPath;
struct BusinessPlan;
struct CurrencyData;
struct TLVstore;
struct AchievementsDetails;
class Sync;

// callback interface
struct MEGA_API MegaApp
{
    MegaClient* client;

    // a request-level error occurred (other than API_EAGAIN, which will lead to a retry)
    virtual void request_error(error) { }

    // request response progress
    virtual void request_response_progress(m_off_t, m_off_t) { }

    // prelogin result
    virtual void prelogin_result(int, string*, string*, error) { }

    // login result
    virtual void login_result(error) { }

    virtual void loggedInStateChanged(sessiontype_t, handle /*me*/, const string& /*email*/) { }

    // user data result
    virtual void userdata_result(string*, string*, string*, Error) { }

    // user public key retrieval result
    virtual void pubkey_result(User *) { }

    // ephemeral session creation/resumption result
    virtual void ephemeral_result(error) { }
    virtual void ephemeral_result(handle, const byte*) { }
    virtual void cancelsignup_result(error) { }

    // check the reason of being blocked result
    virtual void whyamiblocked_result(int) { }

    // account creation
    virtual void sendsignuplink_result(error) { }
    virtual void confirmsignuplink2_result(handle, const char*, const char*, error) { }
    virtual void setkeypair_result(error) { }

    // account credentials, properties and history
    virtual void account_details(AccountDetails*, bool, bool, bool, bool, bool, bool) { }
    virtual void account_details(AccountDetails*, error) { }

    // query bandwidth quota result
    virtual void querytransferquota_result(int) { }

    // sessionid is undef if all sessions except the current were killed
    virtual void sessions_killed(handle /*sessionid*/, error) { }

    // node deletion failed (not invoked unless error != API_OK)
    virtual void unlink_result(handle, error) { }

    // remove versions result
    virtual void unlinkversions_result(error) { }

    // sets have been updated
    virtual void sets_updated(Set**, int) { }

    // set-elements have been updated
    virtual void setelements_updated(SetElement**, int) { }

    // nodes have been updated
    virtual void nodes_updated(sharedNode_vector*, int) { }

    // new actionpackets arrived with a new sequence tag
    virtual void sequencetag_update(const string&) { }

    // nodes have been updated
    virtual void pcrs_updated(PendingContactRequest**, int) { }

    // users have been added or updated
    virtual void users_updated(User**, int) { }

    // alerts have been added or updated
    virtual void useralerts_updated(UserAlert::Base**, int) { }

    // the account has been modified (upgraded/downgraded)
    virtual void account_updated() { }

    // password change result
    virtual void changepw_result(error) { }

    // user attribute update notification
    virtual void userattr_update(User*, int, const char*) { }

    // node fetch result
    virtual void fetchnodes_result(const Error&) { }

    // nodes now (nearly) current
    virtual void nodes_current() { }

    // up to date with API (regarding actionpackets)
    virtual void catchup_result() { }

    // notify about a modified key
    virtual void key_modified(handle, attr_t) { }

    // notify about cyptographyc security upgrade
    virtual void upgrading_security() { }

    // notify about detection of attempt to downgrade ^!keys
    virtual void downgrade_attack() { }

#ifndef NDEBUG
    // So that tests can make a change as soon as a cloud node is moved.
    virtual void move_begin(const LocalPath&, const LocalPath&) { };
#endif // ! NDEBUG

    // node addition has failed
    virtual void putnodes_result(const Error&, targettype_t, vector<NewNode>&, bool targetOverride, int tag) { }

    // outgoing pending contact result
    virtual void setpcr_result(handle, error, opcactions_t) { }
    // incoming pending contact result
    virtual void updatepcr_result(error, ipcactions_t) { }

    // file attribute fetch result
    virtual void fa_complete(handle, fatype, const char*, uint32_t) { }
    virtual int fa_failed(handle, fatype, int, error)
    {
        return 0;
    }

    // file attribute modification result
    virtual void putfa_result(handle, fatype, error) { }

    // purchase transactions
    virtual void enumeratequotaitems_result(unsigned,
                                            handle product,
                                            unsigned proLevel,
                                            int gbStorage,
                                            int gbTransfer,
                                            unsigned months,
                                            unsigned amount,
                                            unsigned amountMonth,
                                            unsigned localPrice,
                                            const char* description,
                                            map<string, uint32_t>&& features,
                                            const char* iosId,
                                            const char* androidId,
                                            unsigned int testCategory,
                                            std::unique_ptr<BusinessPlan> businessPlan,
                                            unsigned int trialDays)
    {}
    virtual void enumeratequotaitems_result(unique_ptr<CurrencyData>) {}
    virtual void enumeratequotaitems_result(error) { }
    virtual void additem_result(error) { }
    virtual void checkout_result(const char*, error) { }
    virtual void submitpurchasereceipt_result(error) { }
    virtual void creditcardstore_result(error) { }
    virtual void creditcardquerysubscriptions_result(int, error) {}
    virtual void creditcardcancelsubscriptions_result(error) {}
    virtual void getpaymentmethods_result(int, error) {}
    virtual void copysession_result(string*, error) { }

    // feedback from user/client
    virtual void userfeedbackstore_result(error) { }
    virtual void sendevent_result(error) { }
    virtual void supportticket_result(error) { }

    // user invites/attributes
    virtual void removecontact_result(error) { }
    virtual void putua_result(error) { }
    virtual void getua_result(error) { }
    virtual void getua_result(byte*, unsigned, attr_t) { }
    virtual void getua_result(TLVstore *, attr_t) { }
#ifdef DEBUG
    virtual void delua_result(error) { }

    // result of send dev subcommand's command
    virtual void senddevcommand_result(int) { }
#endif

    virtual void getuseremail_result(string *, error) { }

    // exported link access result
    virtual void openfilelink_result(const Error&) { }
    virtual void openfilelink_result(handle, const byte*, m_off_t, string*, string*, int) { }

    // pread result
    virtual dstime pread_failure(const Error&, int, void*, dstime) { return ~(dstime)0; }
    virtual bool pread_data(byte*, m_off_t, m_off_t, m_off_t, m_off_t, void*) { return false; }

    // event reporting result
    virtual void reportevent_result(error) { }

    // clean rubbish bin result
    virtual void cleanrubbishbin_result(error) { }

    // get account recovery link result
    virtual void getrecoverylink_result(error) {}

    // check account recovery link result
    virtual void queryrecoverylink_result(error) {}
    virtual void queryrecoverylink_result(int, const char *, const char *, time_t, handle, const vector<string> *) {}

    // get private key from recovery link result
    virtual void getprivatekey_result(error, const byte * = NULL, const size_t = 0) {}

    // confirm recovery link result
    virtual void confirmrecoverylink_result(error) {}

    // convirm cancellation link result
    virtual void confirmcancellink_result(error) {}

    // validation of password
    virtual void validatepassword_result(error) {}

    // get change email link result
    virtual void getemaillink_result(error) {}

    // resend verification email
    virtual void resendverificationemail_result(error) {};

    // reset the verified phone number
    virtual void resetSmsVerifiedPhoneNumber_result(error) {};

    // confirm change email link result
    virtual void confirmemaillink_result(error) {}

    // get version info
    virtual void getversion_result(int, const char*, error) {}

    // get local SSL certificate
    virtual void getlocalsslcertificate_result(m_time_t, string*, error){ }

#ifdef ENABLE_CHAT
    // chat-related command's result
    virtual void chatcreate_result(TextChat *, error) { }
    virtual void chatinvite_result(error) { }
    virtual void chatremove_result(error) { }
    virtual void chaturl_result(string*, error) { }
    virtual void chatgrantaccess_result(error) { }
    virtual void chatremoveaccess_result(error) { }
    virtual void chatupdatepermissions_result(error) { }
    virtual void chattruncate_result(error) { }
    virtual void chatsettitle_result(error) { }
    virtual void chatpresenceurl_result(string*, error) { }
    virtual void registerpushnotification_result(error) { }
    virtual void archivechat_result(error) { }
    virtual void setchatretentiontime_result(error){ }

    virtual void chats_updated(textchat_map *, int) { }
    virtual void richlinkrequest_result(string*, error) { }
    virtual void chatlink_result(handle, error) { }
    virtual void chatlinkurl_result (handle chatid, int shard, string* link, string* ct,
                                     int numPeers, m_time_t ts, bool meetingRoom, int chatOptions,
                                     const std::vector<std::unique_ptr<ScheduledMeeting>>* smList,
                                     handle callid, error e) { }

    virtual void chatlinkclose_result(error) { }
    virtual void chatlinkjoin_result(error) { }
#endif

    // get mega-achievements
    virtual void getmegaachievements_result(AchievementsDetails*, error) {}

    // codec-mappings received
    virtual void mediadetection_ready() {}

    // Locally calculated sum of sizes of files stored in cloud has changed
    virtual void storagesum_changed(int64_t newsum) {}

    // global transfer queue updates
    virtual void file_added(File*) { }
    virtual void file_removed(File*, const Error&) { }
    virtual void file_complete(File*) { }
    virtual File* file_resume(string*, direction_t*) { return NULL; }

    virtual void transfer_added(Transfer*) { }
    virtual void transfer_removed(Transfer*) { }
    virtual void transfer_prepare(Transfer*) { }
    virtual void transfer_failed(Transfer*, const Error&, dstime = 0) { }
    virtual void transfer_update(Transfer*) { }
    virtual void transfer_complete(Transfer*) { }

    // ----- sync callbacks below, which occur on the syncs thread
    // ----- (other callbacks occur on the client thread)

    // sync status updates and events
    virtual void syncupdate_stateconfig(const SyncConfig& config) { }
    virtual void syncupdate_stats(handle backupId, const PerSyncStats&) { }
    virtual void syncupdate_syncing(bool) { }
    virtual void syncupdate_scanning(bool) { }
    virtual void syncupdate_stalled(bool) { }
    virtual void syncupdate_conflicts(bool) { }
    virtual void syncupdate_totalstalls(bool) { }
    virtual void syncupdate_totalconflicts(bool) { }
    virtual void syncupdate_treestate(const SyncConfig &, const LocalPath&, treestate_t, nodetype_t) { }
    virtual bool isSyncStalledChanged() { return false; } // flag for syncupdate_totalstalls or syncupdate_totalstalls is set

#ifdef DEBUG
    // Called right before the sync engine processes a filesystem notification.
    virtual void syncdebug_notification(const SyncConfig& config,
                                        int queue,
                                        const Notification& notification) { };
#endif // DEBUG

    // after a root node of a sync changed its path
    virtual void syncupdate_remote_root_changed(const SyncConfig &) { }

    // after all sync configs have been loaded on startup
    virtual void syncs_restored(SyncError) { }

    // after all syncs have been disabled, eg due to overquota
    virtual void syncs_disabled(SyncError) { }

    // the sync could be auto-loaded on start, or one the user added
    virtual void sync_added(const SyncConfig& config) { }

    // after a sync has been removed
    virtual void sync_removed(const SyncConfig& config) { }

    // ----- that's the end of the sync callbacks, which occur on the syncs thread
    // ----- (other callbacks occur on the client thread)

    // Notify fatal errors (ie. DB, node unserialization, ...) to apps
    virtual void notifyError(const char*, ErrorReason) { }

    // reload forced automatically by server
    virtual void reloading() { }

    // wipe all users, nodes and shares
    virtual void clearing() { }

    // failed request retry notification
    virtual void notify_retry(dstime, retryreason_t) { }

    virtual void notify_dbcommit() { }

    virtual void notify_storage(int) { }

    virtual void notify_business_status(BizStatus) { }

    virtual void notify_change_to_https() { }

    // account confirmation via signup link
    virtual void notify_confirmation(const char* /*email*/) { }

    // account confirmation after signup link -> user, email have been confirmed
    virtual void notify_confirm_user_email(handle /*user*/, const char* /*email*/) { }

    // network layer disconnected
    virtual void notify_disconnect() { }

    // HTTP request finished
    virtual void http_result(error, int, byte*, int) { }

    // Timer ended
    virtual void timer_result(error) { }

    // contact link create
    virtual void contactlinkcreate_result(error, handle) { }

    // contact link query
    virtual void contactlinkquery_result(error, handle, string*, string*, string*, string*) { }

    // contact link delete
    virtual void contactlinkdelete_result(error) { }

    // multi-factor authentication setup
    virtual void multifactorauthsetup_result(string*, error) { }

    // multi-factor authentication get
    virtual void multifactorauthcheck_result(int) { }

    // multi-factor authentication disable
    virtual void multifactorauthdisable_result(error) { }

    // fetch time zone
    virtual void fetchtimezone_result(error, vector<string>*, vector<int>*, int) { }

    // keep me alive command for mobile apps
    virtual void keepmealive_result (error) { }

    // get the current PSA
    virtual void getpsa_result (error, int, string*, string*, string*, string*, string*, string*) { }

    // result of the user alert acknowledge request
    virtual void acknowledgeuseralerts_result(error) { }

    // get info about a folder link
    virtual void folderlinkinfo_result(error, handle , handle, string*, string* , m_off_t, uint32_t , uint32_t , m_off_t , uint32_t) {}

    // result of sms verification commands
    virtual void smsverificationsend_result(error) { }
    virtual void smsverificationcheck_result(error, string*) { }

    // result of get country calling codes command
    virtual void getcountrycallingcodes_result(error, map<string, vector<string>>*) { }

    virtual void getmiscflags_result(error) { }

    virtual void backupput_result(const Error&, handle /*backup id*/) { }

    virtual void getbanners_result(error) { }
    virtual void getbanners_result(vector< tuple<int, string, string, string, string, string, string> >&& banners) { }

    virtual void dismissbanner_result(error) { }

    // provides the per mil progress of a long-running API operation or -1 if there isn't any operation in progress
    virtual void reqstat_progress(int) { }

    virtual void notify_creditCardExpiry() { }

    virtual ~MegaApp() { }

    // External drive notifications
    virtual void drive_presence_changed(bool appeared, const LocalPath& driveRoot) { }

    // Called when a mount has been added, disabled, enabled or removed.
    virtual void onFuseEvent(const fuse::MountEvent&) { }
};
} // namespace

#endif
