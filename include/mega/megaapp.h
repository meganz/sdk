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

namespace mega {
// callback interface
struct MEGA_API MegaApp
{
    MegaClient* client;

    // a request-level error occurred (other than API_EAGAIN, which will lead to a retry)
    virtual void request_error(error) { }

    // request response progress
    virtual void request_response_progress(m_off_t, m_off_t) { }

    // login result
    virtual void login_result(error) { }

    // logout result
    virtual void logout_result(error) { }

    // user data result
    virtual void userdata_result(string*, string*, string*, handle, error) { }

    // user public key retrieval result
    virtual void pubkey_result(User *) { }

    // ephemeral session creation/resumption result
    virtual void ephemeral_result(error) { }
    virtual void ephemeral_result(handle, const byte*) { }

    // account creation
    virtual void sendsignuplink_result(error) { }
    virtual void querysignuplink_result(error) { }
    virtual void querysignuplink_result(handle, const char*, const char*,
                                        const byte*, const byte*, const byte*,
                                        size_t) { }
    virtual void confirmsignuplink_result(error) { }
    virtual void setkeypair_result(error) { }

    // account credentials, properties and history
    virtual void account_details(AccountDetails*, bool, bool, bool, bool, bool, bool) { }
    virtual void account_details(AccountDetails*, error) { }

    // query bandwidth quota result
    virtual void querytransferquota_result(int) { }

    // sessionid is undef if all sessions except the current were killed
    virtual void sessions_killed(handle /*sessionid*/, error) { }

    // node attribute update failed (not invoked unless error != API_OK)
    virtual void setattr_result(handle, error) { }

    // move node failed (not invoked unless error != API_OK)
    virtual void rename_result(handle, error) { }

    // node deletion failed (not invoked unless error != API_OK)
    virtual void unlink_result(handle, error) { }

    // remove versions result
    virtual void unlinkversions_result(error) { }

    // nodes have been updated
    virtual void nodes_updated(Node**, int) { }

    // nodes have been updated
    virtual void pcrs_updated(PendingContactRequest**, int) { }

    // users have been added or updated
    virtual void users_updated(User**, int) { }

    // the account has been modified (upgraded/downgraded)
    virtual void account_updated() { }

    // password change result
    virtual void changepw_result(error) { }

    // user attribute update notification
    virtual void userattr_update(User*, int, const char*) { }

    // node fetch result
    virtual void fetchnodes_result(error) { }

    // nodes now (nearly) current
    virtual void nodes_current() { }

    // node addition has failed
    virtual void putnodes_result(error, targettype_t, NewNode*) { }

    // share update result
    virtual void share_result(error) { }
    virtual void share_result(int, error) { }

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
    virtual void putfa_result(handle, fatype, const char*) { }

    // purchase transactions
    virtual void enumeratequotaitems_result(handle, unsigned, unsigned, unsigned, unsigned, unsigned, const char*, const char*, const char*, const char*) { }
    virtual void enumeratequotaitems_result(error) { }
    virtual void additem_result(error) { }
    virtual void checkout_result(const char*, error) { }
    virtual void submitpurchasereceipt_result(error) { }
    virtual void creditcardstore_result(error) { }
    virtual void creditcardquerysubscriptions_result(int, error) {}
    virtual void creditcardcancelsubscriptions_result(error) {}
    virtual void getpaymentmethods_result(int, error) {}
    virtual void copysession_result(string*, error) { }
    virtual void userfeedbackstore_result(error) { }
    virtual void sendevent_result(error) { }

    // user invites/attributes
    virtual void removecontact_result(error) { }
    virtual void putua_result(error) { }
    virtual void getua_result(error) { }
    virtual void getua_result(byte*, unsigned) { }
    virtual void getua_result(TLVstore *) { }
#ifdef DEBUG
    virtual void delua_result(error) { }
#endif

    virtual void getuseremail_result(string *, error) { }

    // file node export result
    virtual void exportnode_result(error) { }
    virtual void exportnode_result(handle, handle) { }

    // exported link access result
    virtual void openfilelink_result(error) { }
    virtual void openfilelink_result(handle, const byte*, m_off_t, string*, string*, int) { }

    // node opening result
    virtual void checkfile_result(handle, error) { }
    virtual void checkfile_result(handle, error, byte*, m_off_t, m_time_t, m_time_t, string*, string*, string*) { }

    // pread result
    virtual dstime pread_failure(error, int, void*, dstime) { return ~(dstime)0; }
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

    virtual void chats_updated(textchat_map *, int) { }
#endif

    // get mega-achievements
    virtual void getmegaachievements_result(AchievementsDetails*, error) {}

    // get welcome pdf
    virtual void getwelcomepdf_result(handle, string*, error) {}

    // global transfer queue updates
    virtual void file_added(File*) { }
    virtual void file_removed(File*, error) { }
    virtual void file_complete(File*) { }
    virtual File* file_resume(string*, direction_t*) { return NULL; }

    virtual void transfer_added(Transfer*) { }
    virtual void transfer_removed(Transfer*) { }
    virtual void transfer_prepare(Transfer*) { }
    virtual void transfer_failed(Transfer*, error, dstime = 0) { }
    virtual void transfer_update(Transfer*) { }
    virtual void transfer_complete(Transfer*) { }

    // sync status updates and events
    virtual void syncupdate_state(Sync*, syncstate_t) { }
    virtual void syncupdate_scanning(bool) { }
    virtual void syncupdate_local_folder_addition(Sync*, LocalNode*, const char*) { }
    virtual void syncupdate_local_folder_deletion(Sync*, LocalNode*) { }
    virtual void syncupdate_local_file_addition(Sync*, LocalNode*, const char*) { }
    virtual void syncupdate_local_file_deletion(Sync*, LocalNode*) { }
    virtual void syncupdate_local_file_change(Sync*, LocalNode*, const char*) { }
    virtual void syncupdate_local_move(Sync*, LocalNode*, const char*) { }
    virtual void syncupdate_local_lockretry(bool) { }
    virtual void syncupdate_get(Sync*, Node*, const char*) { }
    virtual void syncupdate_put(Sync*, LocalNode*, const char*) { }
    virtual void syncupdate_remote_file_addition(Sync*, Node*) { }
    virtual void syncupdate_remote_file_deletion(Sync*, Node*) { }
    virtual void syncupdate_remote_folder_addition(Sync*, Node*) { }
    virtual void syncupdate_remote_folder_deletion(Sync*, Node*) { }
    virtual void syncupdate_remote_copy(Sync*, const char*) { }
    virtual void syncupdate_remote_move(Sync*, Node*, Node*) { }
    virtual void syncupdate_remote_rename(Sync*, Node*, const char*) { }
    virtual void syncupdate_treestate(LocalNode*) { }

    // sync filename filter
    virtual bool sync_syncable(Sync*, const char*, string*, Node*)
    {
        return true;
    }

    virtual bool sync_syncable(Sync*, const char*, string*)
    {
        return true;
    }

    // suggest reload due to possible race condition with other clients
    virtual void reload(const char*) { }

    // wipe all users, nodes and shares
    virtual void clearing() { }

    // failed request retry notification
    virtual void notify_retry(dstime) { }

    virtual void notify_dbcommit() { }

    virtual void notify_change_to_https() { }

    // account confirmation via signup link
    virtual void notify_confirmation(const char* /*email*/) { }

    // network layer disconnected
    virtual void notify_disconnect() { }

    // HTTP request finished
    virtual void http_result(error, int, byte*, int) { }

    virtual ~MegaApp() { }
};
} // namespace

#endif
