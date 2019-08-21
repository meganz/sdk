/**
 * @file examples/megaclient.cpp
 * @brief Sample application, interactive GNU Readline CLI
 *
 * (c) 2013-2014 by Mega Limited, Auckland, New Zealand
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
#include "megacli.h"
#include <fstream>

#define USE_VARARGS
#define PREFER_STDARG

#ifndef NO_READLINE
#include <readline/readline.h>
#include <readline/history.h>
#endif

#if (__cplusplus >= 201700L)
    #include <filesystem>
    namespace fs = std::filesystem;
    #define USE_FILESYSTEM
#elif !defined(__MINGW32__) && !defined(__ANDROID__) && (!defined(__GNUC__) || (__GNUC__*100+__GNUC_MINOR__) >= 503)
#define USE_FILESYSTEM
#ifdef WIN32
    #include <filesystem>
    namespace fs = std::experimental::filesystem;
#else
    #include <experimental/filesystem>
    namespace fs = std::experimental::filesystem;
#endif
#endif

#include <regex>

#ifdef USE_FREEIMAGE
#include "mega/gfx/freeimage.h"
#endif

namespace ac = ::mega::autocomplete;

#include <iomanip>

// TODO: Remove this once OSX supports std::filesystem
#ifdef __APPLE__
#undef USE_FILESYSTEM
#endif

using namespace mega;
using std::cout;
using std::cerr;
using std::endl;
using std::flush;
using std::ifstream;
using std::ofstream;
using std::setw;
using std::hex;
using std::dec;

MegaClient* client;
MegaClient* clientFolder;


// new account signup e-mail address and name
static string signupemail, signupname;

// signup code being confirmed
static string signupcode;

// signup password challenge and encrypted master key
static byte signuppwchallenge[SymmCipher::KEYLENGTH], signupencryptedmasterkey[SymmCipher::KEYLENGTH];

// password recovery e-mail address and code being confirmed
static string recoveryemail, recoverycode;

// password recovery code requires MK or not
static bool hasMasterKey;

// master key for password recovery
static byte masterkey[SymmCipher::KEYLENGTH];

// change email link to be confirmed
static string changeemail, changecode;

// chained folder link creation
static handle hlink = UNDEF;
static int del = 0;
static int ets = 0;

// import welcome pdf at account creation
static bool pdf_to_import = false;

// public link information
static string publiclink;

// local console
Console* console;

// loading progress of lengthy API responses
int responseprogress = -1;

//2FA pin attempts
int attempts = 0;

static const char* getAccessLevelStr(int access)
{
    switch(access)
    {
    case ACCESS_UNKNOWN:
        return "unkown";
    case RDONLY:
        return "read-only";
    case RDWR:
        return "read/write";
    case FULL:
        return "full access";
    case OWNER:
        return "owner access";
    case OWNERPRELOGIN:
        return "owner (prelogin) access";
    default:
        return "UNDEFINED";
    }
}

const char* errorstring(error e)
{
    switch (e)
    {
        case API_OK:
            return "No error";
        case API_EINTERNAL:
            return "Internal error";
        case API_EARGS:
            return "Invalid argument";
        case API_EAGAIN:
            return "Request failed, retrying";
        case API_ERATELIMIT:
            return "Rate limit exceeded";
        case API_EFAILED:
            return "Transfer failed";
        case API_ETOOMANY:
            return "Too many concurrent connections or transfers";
        case API_ERANGE:
            return "Out of range";
        case API_EEXPIRED:
            return "Expired";
        case API_ENOENT:
            return "Not found";
        case API_ECIRCULAR:
            return "Circular linkage detected";
        case API_EACCESS:
            return "Access denied";
        case API_EEXIST:
            return "Already exists";
        case API_EINCOMPLETE:
            return "Incomplete";
        case API_EKEY:
            return "Invalid key/integrity check failed";
        case API_ESID:
            return "Bad session ID";
        case API_EBLOCKED:
            return "Blocked";
        case API_EOVERQUOTA:
            return "Over quota";
        case API_ETEMPUNAVAIL:
            return "Temporarily not available";
        case API_ETOOMANYCONNECTIONS:
            return "Connection overflow";
        case API_EWRITE:
            return "Write error";
        case API_EREAD:
            return "Read error";
        case API_EAPPKEY:
            return "Invalid application key";
        case API_EGOINGOVERQUOTA:
            return "Not enough quota";
        case API_EMFAREQUIRED:
            return "Required 2FA pin";
        default:
            return "Unknown error";
    }
}

AppFile::AppFile()
{
    static int nextseqno;

    seqno = ++nextseqno;
}

// transfer start
void AppFilePut::start()
{
}

void AppFileGet::start()
{
}

// transfer completion
void AppFileGet::completed(Transfer*, LocalNode*)
{
    // (at this time, the file has already been placed in the final location)
    delete this;
}

void AppFilePut::completed(Transfer* t, LocalNode*)
{
    // perform standard completion (place node in user filesystem etc.)
    File::completed(t, NULL);

    delete this;
}

AppFileGet::~AppFileGet()
{
    appxferq[GET].erase(appxfer_it);
}

AppFilePut::~AppFilePut()
{
    appxferq[PUT].erase(appxfer_it);
}

void AppFilePut::displayname(string* dname)
{
    *dname = localname;
    transfer->client->fsaccess->local2name(dname);
}

// transfer progress callback
void AppFile::progress()
{
}

static void displaytransferdetails(Transfer* t, const char* action)
{
    string name;

    for (file_list::iterator it = t->files.begin(); it != t->files.end(); it++)
    {
        if (it != t->files.begin())
        {
            cout << "/";
        }

        (*it)->displayname(&name);
        cout << name;
    }

    cout << ": " << (t->type == GET ? "Incoming" : "Outgoing") << " file transfer " << action;
}

// a new transfer was added
void DemoApp::transfer_added(Transfer* /*t*/)
{
}

// a queued transfer was removed
void DemoApp::transfer_removed(Transfer* t)
{
    displaytransferdetails(t, "removed\n");
}

void DemoApp::transfer_update(Transfer* /*t*/)
{
    // (this is handled in the prompt logic)
}

void DemoApp::transfer_failed(Transfer* t, error e, dstime)
{
    displaytransferdetails(t, "failed (");
    cout << errorstring(e) << ")" << endl;
}

void DemoApp::transfer_complete(Transfer* t)
{
    displaytransferdetails(t, "completed, ");

    if (t->slot)
    {
        cout << t->slot->progressreported * 10 / (1024 * (Waiter::ds - t->slot->starttime + 1)) << " KB/s" << endl;
    }
    else
    {
        cout << "delayed" << endl;
    }
}

// transfer about to start - make final preparations (determine localfilename, create thumbnail for image upload)
void DemoApp::transfer_prepare(Transfer* t)
{
    displaytransferdetails(t, "starting\n");

    if (t->type == GET)
    {
        // only set localfilename if the engine has not already done so
        if (!t->localfilename.size())
        {
            client->fsaccess->tmpnamelocal(&t->localfilename);
        }
    }
}

#ifdef ENABLE_SYNC
static void syncstat(Sync* sync)
{
    cout << ", local data in this sync: " << sync->localbytes << " byte(s) in " << sync->localnodes[FILENODE]
         << " file(s) and " << sync->localnodes[FOLDERNODE] << " folder(s)" << endl;
}

void DemoApp::syncupdate_state(Sync*, syncstate_t newstate)
{
    switch (newstate)
    {
        case SYNC_ACTIVE:
            cout << "Sync is now active" << endl;
            break;

        case SYNC_FAILED:
            cout << "Sync failed." << endl;

        default:
            ;
    }
}

void DemoApp::syncupdate_scanning(bool active)
{
    if (active)
    {
        cout << "Sync - scanning files and folders" << endl;
    }
    else
    {
        cout << "Sync - scan completed" << endl;
    }
}

// sync update callbacks are for informational purposes only and must not change or delete the sync itself
void DemoApp::syncupdate_local_folder_addition(Sync* sync, LocalNode *, const char* path)
{
    cout << "Sync - local folder addition detected: " << path;
    syncstat(sync);
}

void DemoApp::syncupdate_local_folder_deletion(Sync* sync, LocalNode *localNode)
{
    cout << "Sync - local folder deletion detected: " << localNode->name;
    syncstat(sync);
}

void DemoApp::syncupdate_local_file_addition(Sync* sync, LocalNode *, const char* path)
{
    cout << "Sync - local file addition detected: " << path;
    syncstat(sync);
}

void DemoApp::syncupdate_local_file_deletion(Sync* sync, LocalNode *localNode)
{
    cout << "Sync - local file deletion detected: " << localNode->name;
    syncstat(sync);
}

void DemoApp::syncupdate_local_file_change(Sync* sync, LocalNode *, const char* path)
{
    cout << "Sync - local file change detected: " << path;
    syncstat(sync);
}

void DemoApp::syncupdate_local_move(Sync*, LocalNode *localNode, const char* path)
{
    cout << "Sync - local rename/move " << localNode->name << " -> " << path << endl;
}

void DemoApp::syncupdate_local_lockretry(bool locked)
{
    if (locked)
    {
        cout << "Sync - waiting for local filesystem lock" << endl;
    }
    else
    {
        cout << "Sync - local filesystem lock issue resolved, continuing..." << endl;
    }
}

void DemoApp::syncupdate_remote_move(Sync *, Node *n, Node *prevparent)
{
    cout << "Sync - remote move " << n->displayname() << ": " << (prevparent ? prevparent->displayname() : "?") <<
            " -> " << (n->parent ? n->parent->displayname() : "?") << endl;
}

void DemoApp::syncupdate_remote_rename(Sync *, Node *n, const char *prevname)
{
    cout << "Sync - remote rename " << prevname << " -> " <<  n->displayname() << endl;
}

void DemoApp::syncupdate_remote_folder_addition(Sync *, Node* n)
{
    cout << "Sync - remote folder addition detected " << n->displayname() << endl;
}

void DemoApp::syncupdate_remote_file_addition(Sync *, Node* n)
{
    cout << "Sync - remote file addition detected " << n->displayname() << endl;
}

void DemoApp::syncupdate_remote_folder_deletion(Sync *, Node* n)
{
    cout << "Sync - remote folder deletion detected " << n->displayname() << endl;
}

void DemoApp::syncupdate_remote_file_deletion(Sync *, Node* n)
{
    cout << "Sync - remote file deletion detected " << n->displayname() << endl;
}

void DemoApp::syncupdate_get(Sync*, Node *, const char* path)
{
    cout << "Sync - requesting file " << path << endl;
}

void DemoApp::syncupdate_put(Sync*, LocalNode *, const char* path)
{
    cout << "Sync - sending file " << path << endl;
}

void DemoApp::syncupdate_remote_copy(Sync*, const char* name)
{
    cout << "Sync - creating remote file " << name << " by copying existing remote file" << endl;
}

static const char* treestatename(treestate_t ts)
{
    switch (ts)
    {
        case TREESTATE_NONE:
            return "None/Undefined";
        case TREESTATE_SYNCED:
            return "Synced";
        case TREESTATE_PENDING:
            return "Pending";
        case TREESTATE_SYNCING:
            return "Syncing";
    }

    return "UNKNOWN";
}

void DemoApp::syncupdate_treestate(LocalNode* l)
{
    cout << "Sync - state change of node " << l->name << " to " << treestatename(l->ts) << endl;
}

// generic name filter
// FIXME: configurable regexps
static bool is_syncable(const char* name)
{
    return *name != '.' && *name != '~' && strcmp(name, "Thumbs.db") && strcmp(name, "desktop.ini");
}

// determines whether remote node should be synced
bool DemoApp::sync_syncable(Sync *, const char *, string *, Node *n)
{
    return is_syncable(n->displayname());
}

// determines whether local file should be synced
bool DemoApp::sync_syncable(Sync *, const char *name, string *)
{
    return is_syncable(name);
}
#endif

AppFileGet::AppFileGet(Node* n, handle ch, byte* cfilekey, m_off_t csize, m_time_t cmtime, string* cfilename,
                       string* cfingerprint, const string& targetfolder)
{
    if (n)
    {
        h = n->nodehandle;
        hprivate = true;

        *(FileFingerprint*) this = *n;
        name = n->displayname();
    }
    else
    {
        h = ch;
        memcpy(filekey, cfilekey, sizeof filekey);
        hprivate = false;

        size = csize;
        mtime = cmtime;

        if (!cfingerprint->size() || !unserializefingerprint(cfingerprint))
        {
            memcpy(crc, filekey, sizeof crc);
        }

        name = *cfilename;
    }

    localname = name;
    client->fsaccess->name2local(&localname);
    if (!targetfolder.empty())
    {
        string ltf, tf = targetfolder;
        client->fsaccess->path2local(&tf, &ltf);
        localname = ltf + client->fsaccess->localseparator + localname;
    }
}

AppFilePut::AppFilePut(string* clocalname, handle ch, const char* ctargetuser)
{
    // this assumes that the local OS uses an ASCII path separator, which should be true for most
    string separator = client->fsaccess->localseparator;

    // full local path
    localname = *clocalname;

    // target parent node
    h = ch;

    // target user
    targetuser = ctargetuser;

    // erase path component
    name = *clocalname;
    client->fsaccess->local2name(&name);
    client->fsaccess->local2name(&separator);

    name.erase(0, name.find_last_of(*separator.c_str()) + 1);
}

// user addition/update (users never get deleted)
void DemoApp::users_updated(User** u, int count)
{
    if (count == 1)
    {
        cout << "1 user received or updated" << endl;
    }
    else
    {
        cout << count << " users received or updated" << endl;
    }

    if (u)
    {
        User* user;
        for (int i = 0; i < count; i++)
        {
            user = u[i];
            cout << "User " << user->email;
            if (user->getTag()) // false if external change
            {
                cout << " has been changed by your own client" << endl;
            }
            else
            {
                cout << " has been changed externally" << endl;
            }
        }
    }
}

bool notifyAlerts = true;

string displayUser(handle user, MegaClient* mc)
{
    User* u = mc->finduser(user);
    return u ? u->email : "<user not found>";
}

string displayTime(m_time_t t)
{
    char timebuf[32];
    struct tm tmptr;
    m_localtime(t, &tmptr);
    strftime(timebuf, sizeof timebuf, "%c", &tmptr);
    return timebuf;
}

void printAlert(UserAlert::Base& b)
{
    string header, title;
    b.text(header, title, client);
    cout << "**alert " << b.id << ": " << header << " - " << title << " [at " << displayTime(b.timestamp) << "]" << " seen: " << b.seen << endl;
}

void DemoApp::useralerts_updated(UserAlert::Base** b, int count)
{
    if (b && notifyAlerts)
    {
        for (int i = 0; i < count; ++i)
        {
            if (!b[i]->seen)
            {
                printAlert(*b[i]);
            }
        }
    }
}


#ifdef ENABLE_CHAT

void DemoApp::chatcreate_result(TextChat *chat, error e)
{
    if (e)
    {
        cout << "Chat creation failed (" << errorstring(e) << ")" << endl;
    }
    else
    {
        cout << "Chat created successfully" << endl;
        printChatInformation(chat);
        cout << endl;
    }
}

void DemoApp::chatinvite_result(error e)
{
    if (e)
    {
        cout << "Chat invitation failed (" << errorstring(e) << ")" << endl;
    }
    else
    {
        cout << "Chat invitation successful" << endl;
    }
}

void DemoApp::chatremove_result(error e)
{
    if (e)
    {
        cout << "Peer removal failed (" << errorstring(e) << ")" << endl;
    }
    else
    {
        cout << "Peer removal successful" << endl;
    }
}

void DemoApp::chaturl_result(string *url, error e)
{
    if (e)
    {
        cout << "Chat URL retrieval failed (" << errorstring(e) << ")" << endl;
    }
    else
    {
        cout << "Chat URL: " << *url << endl;
    }
}

void DemoApp::chatgrantaccess_result(error e)
{
    if (e)
    {
        cout << "Grant access to node failed (" << errorstring(e) << ")" << endl;
    }
    else
    {
        cout << "Access to node granted successfully" << endl;
    }
}

void DemoApp::chatremoveaccess_result(error e)
{
    if (e)
    {
        cout << "Revoke access to node failed (" << errorstring(e) << ")" << endl;
    }
    else
    {
        cout << "Access to node removed successfully" << endl;
    }
}

void DemoApp::chatupdatepermissions_result(error e)
{
    if (e)
    {
        cout << "Permissions update failed (" << errorstring(e) << ")" << endl;
    }
    else
    {
        cout << "Permissions updated successfully" << endl;
    }
}

void DemoApp::chattruncate_result(error e)
{
    if (e)
    {
        cout << "Truncate message/s failed (" << errorstring(e) << ")" << endl;
    }
    else
    {
        cout << "Message/s truncated successfully" << endl;
    }
}

void DemoApp::chatsettitle_result(error e)
{
    if (e)
    {
        cout << "Set title failed (" << errorstring(e) << ")" << endl;
    }
    else
    {
        cout << "Title updated successfully" << endl;
    }
}

void DemoApp::chatpresenceurl_result(string *url, error e)
{
    if (e)
    {
        cout << "Presence URL retrieval failed (" << errorstring(e) << ")" << endl;
    }
    else
    {
        cout << "Presence URL: " << *url << endl;
    }
}

void DemoApp::chatlink_result(handle h, error e)
{
    if (e)
    {
        cout << "Chat link failed (" << errorstring(e) << ")" << endl;
    }
    else
    {
        if (ISUNDEF(h))
        {
            cout << "Chat link deleted successfully" << endl;
        }
        else
        {
            char hstr[sizeof(handle) * 4 / 3 + 4];
            Base64::btoa((const byte *)&h, MegaClient::CHATLINKHANDLE, hstr);
            cout << "Chat link: " << hstr << endl;
        }
    }
}

void DemoApp::chatlinkclose_result(error e)
{
    if (e)
    {
        cout << "Set private mode for chat failed  (" << errorstring(e) << ")" << endl;
    }
    else
    {
        cout << "Private mode successfully set" << endl;
    }
}

void DemoApp::chatlinkurl_result(handle chatid, int shard, string *url, string *ct, int, m_time_t ts, error e)
{
    if (e)
    {
        cout << "URL request for chat-link failed (" << errorstring(e) << ")" << endl;
    }
    else
    {
        char idstr[sizeof(handle) * 4 / 3 + 4];
        Base64::btoa((const byte *)&chatid, MegaClient::CHATHANDLE, idstr);
        cout << "Chatid: " << idstr << " (shard " << shard << ")" << endl;
        cout << "URL for chat-link: " << url->c_str() << endl;
        cout << "Encrypted chat-topic: " << ct->c_str() << endl;
        cout << "Creation timestamp: " << ts << endl;
    }
}

void DemoApp::chatlinkjoin_result(error e)
{
    if (e)
    {
        cout << "Join to openchat failed (" << errorstring(e) << ")" << endl;
    }
    else
    {
        cout << "Joined to openchat successfully." << endl;
    }
}

void DemoApp::chats_updated(textchat_map *chats, int count)
{
    if (count == 1)
    {
        cout << "1 chat received or updated" << endl;
    }
    else
    {
        cout << count << " chats received or updated" << endl;
    }

    if (chats)
    {
        textchat_map::iterator it;
        for (it = chats->begin(); it != chats->end(); it++)
        {
            printChatInformation(it->second);
        }
    }
}

void DemoApp::printChatInformation(TextChat *chat)
{
    if (!chat)
    {
        return;
    }

    cout << "Chat ID: " << Base64Str<sizeof(handle)>(chat->id) << endl;
    cout << "\tOwn privilege level: " << DemoApp::getPrivilegeString(chat->priv) << endl;
    cout << "\tCreation ts: " << chat->ts << endl;
    cout << "\tChat shard: " << chat->shard << endl;
    if (chat->group)
    {
        cout << "\tGroup chat: yes" << endl;
    }
    else
    {
        cout << "\tGroup chat: no" << endl;
    }
    if (chat->isFlagSet(TextChat::FLAG_OFFSET_ARCHIVE))
    {
        cout << "\tArchived chat: yes" << endl;
    }
    else
    {
        cout << "\tArchived chat: no" << endl;
    }
    if (chat->publicchat)
    {
        cout << "\tPublic chat: yes" << endl;
        cout << "\tUnified key: " << chat->unifiedKey.c_str() << endl;
    }
    else
    {
        cout << "\tPublic chat: no" << endl;
    }
    cout << "\tPeers:";

    if (chat->userpriv)
    {
        cout << "\t\t(userhandle)\t(privilege level)" << endl;
        for (unsigned i = 0; i < chat->userpriv->size(); i++)
        {
            Base64Str<sizeof(handle)> hstr(chat->userpriv->at(i).first);
            cout << "\t\t\t" << hstr;
            cout << "\t" << DemoApp::getPrivilegeString(chat->userpriv->at(i).second) << endl;
        }
    }
    else
    {
        cout << " no peers (only you as participant)" << endl;
    }
    if (chat->tag)
    {
        cout << "\tIs own change: yes" << endl;
    }
    else
    {
        cout << "\tIs own change: no" << endl;
    }
    if (!chat->title.empty())
    {
        cout << "\tTitle: " << chat->title.c_str() << endl;
    }
}

string DemoApp::getPrivilegeString(privilege_t priv)
{
    switch (priv)
    {
    case PRIV_STANDARD:
        return "PRIV_STANDARD (standard access)";
    case PRIV_MODERATOR:
        return "PRIV_MODERATOR (moderator)";
    case PRIV_RO:
        return "PRIV_RO (read-only)";
    case PRIV_RM:
        return "PRIV_RM (removed)";
    case PRIV_UNKNOWN:
    default:
        return "PRIV_UNKNOWN";
    }
}

#endif


void DemoApp::pcrs_updated(PendingContactRequest** list, int count)
{
    int deletecount = 0;
    int updatecount = 0;
    if (list != NULL)
    {
        for (int i = 0; i < count; i++)
        {
            if (list[i]->changed.deleted)
            {
                deletecount++; 
            } 
            else
            {
                updatecount++;
            }
        }
    } 
    else
    {
        // All pcrs are updated
        for (handlepcr_map::iterator it = client->pcrindex.begin(); it != client->pcrindex.end(); it++)
        {
            if (it->second->changed.deleted)
            {
                deletecount++; 
            } 
            else
            {
                updatecount++;
            }
        }
    }

    if (deletecount != 0)
    {
        cout << deletecount << " pending contact request" << (deletecount != 1 ? "s" : "") << " deleted" << endl;
    }
    if (updatecount != 0)
    {
        cout << updatecount << " pending contact request" << (updatecount != 1 ? "s" : "") << " received or updated" << endl;
    }
}

void DemoApp::setattr_result(handle, error e)
{
    if (e)
    {
        cout << "Node attribute update failed (" << errorstring(e) << ")" << endl;
    }
}

void DemoApp::rename_result(handle, error e)
{
    if (e)
    {
        cout << "Node move failed (" << errorstring(e) << ")" << endl;
    }
}

void DemoApp::unlink_result(handle, error e)
{
    if (e)
    {
        cout << "Node deletion failed (" << errorstring(e) << ")" << endl;
    }
}

void DemoApp::fetchnodes_result(error e)
{
    if (e)
    {
        cout << "File/folder retrieval failed (" << errorstring(e) << ")" << endl;
        pdf_to_import = false;
    }
    else
    {
        // check if we fetched a folder link and the key is invalid
        handle h = client->getrootpublicfolder();
        if (h != UNDEF)
        {
            Node *n = client->nodebyhandle(h);
            if (n && (n->attrs.map.find('n') == n->attrs.map.end()))
            {
                cout << "File/folder retrieval succeed, but encryption key is wrong." << endl;
            }
            else
            {
                cout << "Folder link loaded correctly." << endl;
            }
        }

        if (pdf_to_import)
        {
            client->getwelcomepdf();
        }
    }
}

void DemoApp::putnodes_result(error e, targettype_t t, NewNode* nn)
{
    if (t == USER_HANDLE)
    {
        delete[] nn;

        if (!e)
        {
            cout << "Success." << endl;
        }
    }

    if (pdf_to_import)   // putnodes from openfilelink_result()
    {
        if (!e)
        {
            cout << "Welcome PDF file has been imported successfully." << endl;
        }
        else
        {
            cout << "Failed to import Welcome PDF file" << endl;
        }

        pdf_to_import = false;
        return;
    }

    if (e)
    {
        cout << "Node addition failed (" << errorstring(e) << ")" << endl;
    }
}

void DemoApp::share_result(error e)
{
    if (e)
    {
        cout << "Share creation/modification request failed (" << errorstring(e) << ")" << endl;
    }
    else
    {
        if (hlink != UNDEF)
        {
            if (!del)
            {
                Node *n = client->nodebyhandle(hlink);
                if (!n)
                {
                    cout << "Node was not found. (" << Base64Str<sizeof hlink>(hlink) << ")" << endl;

                    hlink = UNDEF;
                    del = ets = 0;
                    return;
                }

                client->getpubliclink(n, del, ets);
            }
            else
            {
                hlink = UNDEF;
                del = ets = 0;
            }
        }
    }
}

void DemoApp::share_result(int, error e)
{
    if (e)
    {
        cout << "Share creation/modification failed (" << errorstring(e) << ")" << endl;
    }
    else
    {
        cout << "Share creation/modification succeeded" << endl;
    }
}

void DemoApp::setpcr_result(handle h, error e, opcactions_t action)
{
    if (e)
    {
        cout << "Outgoing pending contact request failed (" << errorstring(e) << ")" << endl;
    }
    else
    {
        if (h == UNDEF)
        {
            // must have been deleted
            cout << "Outgoing pending contact request " << (action == OPCA_DELETE ? "deleted" : "reminded") << " successfully" << endl;
        } 
        else
        {
            cout << "Outgoing pending contact request succeeded, id: " << Base64Str<MegaClient::PCRHANDLE>(h) << endl;
        }
    }
}

void DemoApp::updatepcr_result(error e, ipcactions_t action)
{
    if (e)
    {
        cout << "Incoming pending contact request update failed (" << errorstring(e) << ")" << endl;
    }
    else
    {
        string labels[3] = {"accepted", "denied", "ignored"};
        cout << "Incoming pending contact request successfully " << labels[(int)action] << endl;
    }
}

void DemoApp::fa_complete(handle h, fatype type, const char* /*data*/, uint32_t len)
{
    cout << "Got attribute of type " << type << " (" << len << " byte(s))";
    Node *n = client->nodebyhandle(h);
    if (n)
    {
        cout << " for " << n->displayname() << endl;
    }
}

int DemoApp::fa_failed(handle, fatype type, int retries, error e)
{
    cout << "File attribute retrieval of type " << type << " failed (retries: " << retries << ") error: " << e << endl;

    return retries > 2;
}

void DemoApp::putfa_result(handle, fatype, error e)
{
    if (e)
    {
        cout << "File attribute attachment failed (" << errorstring(e) << ")" << endl;
    }
}

void DemoApp::putfa_result(handle, fatype, const char*)
{
}

void DemoApp::removecontact_result(error e)
{
    if (e)
    {
        cout << "Contact removal failed (" << errorstring(e) << ")" << endl;
    }
    else
    {
        cout << "Success." << endl;
    }
}

void DemoApp::putua_result(error e)
{
    if (e)
    {
        cout << "User attribute update failed (" << errorstring(e) << ")" << endl;
    }
    else
    {
        cout << "Success." << endl;
    }
}

void DemoApp::getua_result(error e)
{
#ifdef ENABLE_CHAT
    if (client->fetchingkeys)
    {
        return;
    }
#endif

    cout << "User attribute retrieval failed (" << errorstring(e) << ")" << endl;
}

void DemoApp::getua_result(byte* data, unsigned l, attr_t)
{
#ifdef ENABLE_CHAT
    if (client->fetchingkeys)
    {
        return;
    }
#endif

    cout << "Received " << l << " byte(s) of user attribute: ";
    fwrite(data, 1, l, stdout);
    cout << endl;
}

void DemoApp::getua_result(TLVstore *tlv, attr_t)
{
#ifdef ENABLE_CHAT
    if (client->fetchingkeys)
    {
        return;
    }
#endif

    if (!tlv)
    {
        cout << "Error getting private user attribute" << endl;
    }
    else
    {
        cout << "Received a TLV with " << tlv->size() << " item(s) of user attribute: " << endl;

        vector<string> *keys = tlv->getKeys();
        vector<string>::const_iterator it;
        unsigned valuelen;
        string value, key;
        char *buf;
        for (it=keys->begin(); it != keys->end(); it++)
        {
            key = (*it).empty() ? "(no key)" : *it;
            value = tlv->get(*it);
            valuelen = unsigned(value.length());

            buf = new char[valuelen * 4 / 3 + 4];
            Base64::btoa((const byte *) value.data(), valuelen, buf);

            cout << "\t" << key << "\t" << buf << endl;

            delete [] buf;
        }
        delete keys;
    }
}

#ifdef DEBUG
void DemoApp::delua_result(error e)
{
    if (e)
    {
        cout << "User attribute removal failed (" << errorstring(e) << ")" << endl;
    }
    else
    {
        cout << "Success." << endl;
    }
}
#endif


void DemoApp::notify_retry(dstime dsdelta, retryreason_t)
{
    if (dsdelta)
    {
        cout << "API request failed, retrying in " << dsdelta * 100 << " ms - Use 'retry' to retry immediately..."
             << endl;
    }
    else
    {
        cout << "Retried API request completed" << endl;
    }
}

static void store_line(char*);
static void process_line(char *);
static char* line;

static AccountDetails account;

static handle cwd = UNDEF;

static const char* rootnodenames[] =
{ "ROOT", "INBOX", "RUBBISH" };
static const char* rootnodepaths[] =
{ "/", "//in", "//bin" };

static void nodestats(int* c, const char* action)
{
    if (c[FILENODE])
    {
        cout << c[FILENODE] << ((c[FILENODE] == 1) ? " file" : " files");
    }
    if (c[FILENODE] && c[FOLDERNODE])
    {
        cout << " and ";
    }
    if (c[FOLDERNODE])
    {
        cout << c[FOLDERNODE] << ((c[FOLDERNODE] == 1) ? " folder" : " folders");
    }

    if (c[FILENODE] || c[FOLDERNODE])
    {
        cout << " " << action << endl;
    }
}

// list available top-level nodes and contacts/incoming shares
static void listtrees()
{
    for (int i = 0; i < (int) (sizeof client->rootnodes / sizeof *client->rootnodes); i++)
    {
        if (client->rootnodes[i] != UNDEF)
        {
            cout << rootnodenames[i] << " on " << rootnodepaths[i] << endl;
        }
    }

    for (user_map::iterator uit = client->users.begin(); uit != client->users.end(); uit++)
    {
        User* u = &uit->second;
        Node* n;

        if (u->show == VISIBLE || u->sharing.size())
        {
            for (handle_set::iterator sit = u->sharing.begin(); sit != u->sharing.end(); sit++)
            {
                if ((n = client->nodebyhandle(*sit)) && n->inshare)
                {
                    cout << "INSHARE on " << u->email << ":" << n->displayname() << " ("
                         << getAccessLevelStr(n->inshare->access) << ")" << endl;
                }
            }
        }
    }

    if (clientFolder && !ISUNDEF(clientFolder->rootnodes[0]))
    {
        Node *n = clientFolder->nodebyhandle(clientFolder->rootnodes[0]);
        if (n)
        {
            cout << "FOLDERLINK on " << n->displayname() << ":" << endl;
        }
    }
}

// returns node pointer determined by path relative to cwd
// path naming conventions:
// * path is relative to cwd
// * /path is relative to ROOT
// * //in is in INBOX
// * //bin is in RUBBISH
// * X: is user X's INBOX
// * X:SHARE is share SHARE from user X
// * Y:name is folder in FOLDERLINK, Y is the public handle
// * : and / filename components, as well as the \, must be escaped by \.
// (correct UTF-8 encoding is assumed)
// returns NULL if path malformed or not found
static Node* nodebypath(const char* ptr, string* user = NULL, string* namepart = NULL)
{
    vector<string> c;
    string s;
    int l = 0;
    const char* bptr = ptr;
    int remote = 0;
    int folderlink = 0;
    Node* n = nullptr;
    Node* nn;

    // split path by / or :
    do {
        if (!l)
        {
            if (*(const signed char*)ptr >= 0)
            {
                if (*ptr == '\\')
                {
                    if (ptr > bptr)
                    {
                        s.append(bptr, ptr - bptr);
                    }

                    bptr = ++ptr;

                    if (*bptr == 0)
                    {
                        c.push_back(s);
                        break;
                    }

                    ptr++;
                    continue;
                }

                if (*ptr == '/' || *ptr == ':' || !*ptr)
                {
                    if (*ptr == ':')
                    {
                        if (c.size())
                        {
                            return NULL;
                        }

                        remote = 1;
                    }

                    if (ptr > bptr)
                    {
                        s.append(bptr, ptr - bptr);
                    }

                    bptr = ptr + 1;

                    c.push_back(s);

                    s.erase();
                }
            }
            else if ((*ptr & 0xf0) == 0xe0)
            {
                l = 1;
            }
            else if ((*ptr & 0xf8) == 0xf0)
            {
                l = 2;
            }
            else if ((*ptr & 0xfc) == 0xf8)
            {
                l = 3;
            }
            else if ((*ptr & 0xfe) == 0xfc)
            {
                l = 4;
            }
        }
        else
        {
            l--;
        }
    } while (*ptr++);

    if (l)
    {
        return NULL;
    }

    if (remote)
    {
        // target: user inbox - record username/email and return NULL
        if (c.size() == 2 && c[0].find("@") != string::npos && !c[1].size())
        {
            if (user)
            {
                *user = c[0];
            }

            return NULL;
        }

        // target is not a user, but a public folder link
        if (c.size() >= 2 && c[0].find("@") == string::npos)
        {
            if (!clientFolder)
            {
                return NULL;
            }

            n = clientFolder->nodebyhandle(clientFolder->rootnodes[0]);
            if (c.size() == 2 && c[1].empty())
            {
                return n;
            }
            l = 1;   // <folder_name>:[/<subfolder>][/<file>]
            folderlink = 1;
        }

        User* u;

        if ((u = client->finduser(c[0].c_str())))
        {
            // locate matching share from this user
            handle_set::iterator sit;
            string name;
            for (sit = u->sharing.begin(); sit != u->sharing.end(); sit++)
            {
                if ((n = client->nodebyhandle(*sit)))
                {
                    if(!name.size())
                    {
                        name =  c[1];
                        n->client->fsaccess->normalize(&name);
                    }

                    if (!strcmp(name.c_str(), n->displayname()))
                    {
                        l = 2;
                        break;
                    }
                }
            }
        }

        if (!l)
        {
            return NULL;
        }
    }
    else
    {
        // path starting with /
        if (c.size() > 1 && !c[0].size())
        {
            // path starting with //
            if (c.size() > 2 && !c[1].size())
            {
                if (c[2] == "in")
                {
                    n = client->nodebyhandle(client->rootnodes[1]);
                }
                else if (c[2] == "bin")
                {
                    n = client->nodebyhandle(client->rootnodes[2]);
                }
                else
                {
                    return NULL;
                }

                l = 3;
            }
            else
            {
                n = client->nodebyhandle(client->rootnodes[0]);

                l = 1;
            }
        }
        else
        {
            n = client->nodebyhandle(cwd);
        }
    }

    // parse relative path
    while (n && l < (int)c.size())
    {
        if (c[l] != ".")
        {
            if (c[l] == "..")
            {
                if (n->parent)
                {
                    n = n->parent;
                }
            }
            else
            {
                // locate child node (explicit ambiguity resolution: not implemented)
                if (c[l].size())
                {
                    if (folderlink)
                    {
                        nn = clientFolder->childnodebyname(n, c[l].c_str());
                    }
                    else
                    {
                        nn = client->childnodebyname(n, c[l].c_str());
                    }

                    if (!nn)
                    {
                        // mv command target? return name part of not found
                        if (namepart && l == (int) c.size() - 1)
                        {
                            *namepart = c[l];
                            return n;
                        }

                        return NULL;
                    }

                    n = nn;
                }
            }
        }

        l++;
    }

    return n;
}

static void listnodeshares(Node* n)
{
    if(n->outshares)
    {
        for (share_map::iterator it = n->outshares->begin(); it != n->outshares->end(); it++)
        {
            cout << "\t" << n->displayname();

            if (it->first)
            {
                cout << ", shared with " << it->second->user->email << " (" << getAccessLevelStr(it->second->access) << ")"
                     << endl;
            }
            else
            {
                cout << ", shared as exported folder link" << endl;
            }
        }
    }
}

void TreeProcListOutShares::proc(MegaClient*, Node* n)
{
    listnodeshares(n);
}

bool handles_on = false;

static void dumptree(Node* n, bool recurse, int depth = 0, const char* title = NULL)
{
    if (depth)
    {
        if (!title && !(title = n->displayname()))
        {
            title = "CRYPTO_ERROR";
        }

        for (int i = depth; i--; )
        {
            cout << "\t";
        }

        cout << title << " (";

        switch (n->type)
        {
            case FILENODE:
                cout << n->size;

                if (handles_on)
                {
                    Base64Str<MegaClient::NODEHANDLE> handlestr(n->nodehandle);
                    cout << " " << handlestr.chars;
                }

                const char* p;
                if ((p = strchr(n->fileattrstring.c_str(), ':')))
                {
                    cout << ", has attributes " << p + 1;
                }

                if (n->plink)
                {
                    cout << ", shared as exported";
                    if (n->plink->ets)
                    {
                        cout << " temporal";
                    }
                    else
                    {
                        cout << " permanent";
                    }
                    cout << " file link";
                }

                break;

            case FOLDERNODE:
                cout << "folder";

                if (handles_on)
                {
                    Base64Str<MegaClient::NODEHANDLE> handlestr(n->nodehandle);
                    cout << " " << handlestr.chars;
                }

                if(n->outshares)
                {
                    for (share_map::iterator it = n->outshares->begin(); it != n->outshares->end(); it++)
                    {
                        if (it->first)
                        {
                            cout << ", shared with " << it->second->user->email << ", access "
                                 << getAccessLevelStr(it->second->access);
                        }
                    }

                    if (n->plink)
                    {
                        cout << ", shared as exported";
                        if (n->plink->ets)
                        {
                            cout << " temporal";
                        }
                        else
                        {
                            cout << " permanent";
                        }
                        cout << " folder link";
                    }
                }

                if (n->pendingshares)
                {
                    for (share_map::iterator it = n->pendingshares->begin(); it != n->pendingshares->end(); it++)
                    {
                        if (it->first)
                        {
                            cout << ", shared (still pending) with " << it->second->pcr->targetemail << ", access "
                                 << getAccessLevelStr(it->second->access);
                        }                        
                    }
                }

                if (n->inshare)
                {
                    cout << ", inbound " << getAccessLevelStr(n->inshare->access) << " share";
                }
                break;

            default:
                cout << "unsupported type, please upgrade";
        }

        cout << ")" << (n->changed.removed ? " (DELETED)" : "") << endl;

        if (!recurse)
        {
            return;
        }
    }

    if (n->type != FILENODE)
    {
        for (node_list::iterator it = n->children.begin(); it != n->children.end(); it++)
        {
            dumptree(*it, recurse, depth + 1);
        }
    }
}

#ifdef USE_FILESYSTEM
static void local_dumptree(const fs::path& de, int recurse, int depth = 0)
{
    if (depth)
    {
        for (int i = depth; i--; )
        {
            cout << "\t";
        }

        cout << de.filename().u8string() << " (";

        if (fs::is_directory(de))
        {
            cout << "folder";
        }

        cout << ")" << endl;

        if (!recurse)
        {
            return;
        }
    }

    if (fs::is_directory(de))
    {
        for (auto i = fs::directory_iterator(de); i != fs::directory_iterator(); ++i)
        {
            local_dumptree(*i, recurse, depth + 1);
        }
    }
}
#endif

static void nodepath(handle h, string* path)
{
    Node* n = client->nodebyhandle(h);
    *path = n ? n->displaypath() : "";
}

appfile_list appxferq[2];

static char dynamicprompt[128];

static const char* prompts[] =
{
    "MEGAcli> ", "Password:", "Old Password:", "New Password:", "Retype New Password:", "Master Key (base64):", "Type 2FA pin:", "Type pin to enable 2FA:"
};

enum prompttype
{
    COMMAND, LOGINPASSWORD, OLDPASSWORD, NEWPASSWORD, PASSWORDCONFIRM, MASTERKEY, LOGINTFA, SETTFA
};

static prompttype prompt = COMMAND;

#if defined(WIN32) && defined(NO_READLINE)
static char pw_buf[512];  // double space for unicode
#else
static char pw_buf[256];  
#endif

static int pw_buf_pos;

static void setprompt(prompttype p)
{
    prompt = p;

    if (p == COMMAND)
    {
        console->setecho(true);
    }
    else
    {
        pw_buf_pos = 0;
#if defined(WIN32) && defined(NO_READLINE)
        static_cast<WinConsole*>(console)->updateInputPrompt(prompts[p]);
#else
        cout << prompts[p] << flush;
#endif
        console->setecho(false);
    }
}

class TreeProcCopy_mcli : public TreeProc
{
    // This is a duplicate of the TreeProcCopy declared in treeproc.h and defined in megaapi_impl.cpp.  
    // However some products are built with the megaapi_impl intermediate layer and some without so
    // we can avoid duplicated symbols in some products this way
public:
    NewNode * nn;
    unsigned nc;


    TreeProcCopy_mcli()
    {
        nn = NULL;
        nc = 0;
    }

    void allocnodes()
    {
        nn = new NewNode[nc];
    }

    ~TreeProcCopy_mcli()
    {
        delete[] nn;
    }

    // determine node tree size (nn = NULL) or write node tree to new nodes array
    void proc(MegaClient* mc, Node* n)
    {
        if (nn)
        {
            string attrstring;
            SymmCipher key;
            NewNode* t = nn + --nc;

            // copy node
            t->source = NEW_NODE;
            t->type = n->type;
            t->nodehandle = n->nodehandle;
            t->parenthandle = n->parent ? n->parent->nodehandle : UNDEF;

            // copy key (if file) or generate new key (if folder)
            if (n->type == FILENODE)
            {
                t->nodekey = n->nodekey;
            }
            else
            {
                byte buf[FOLDERNODEKEYLENGTH];
                mc->rng.genblock(buf, sizeof buf);
                t->nodekey.assign((char*) buf, FOLDERNODEKEYLENGTH);
            }

            key.setkey((const byte*) t->nodekey.data(), n->type);

            AttrMap tattrs;
            tattrs.map = n->attrs.map;
            nameid rrname = AttrMap::string2nameid("rr");
            attr_map::iterator it = tattrs.map.find(rrname);
            if (it != tattrs.map.end())
            {
                LOG_debug << "Removing rr attribute";
                tattrs.map.erase(it);
            }

            t->attrstring = new string;
            tattrs.getjson(&attrstring);
            mc->makeattr(&key, t->attrstring, attrstring.c_str());
        }
        else
        {
            nc++;
        }
    }
};

int loadfile(string* name, string* data)
{
    FileAccess* fa = client->fsaccess->newfileaccess();

    if (fa->fopen(name, 1, 0))
    {
        data->resize(size_t(fa->size));
        fa->fread(data, unsigned(data->size()), 0, 0);
        delete fa;

        return 1;
    }

    delete fa;

    return 0;
}

void xferq(direction_t d, int cancel)
{
    string name;

    for (appfile_list::iterator it = appxferq[d].begin(); it != appxferq[d].end(); )
    {
        if (cancel < 0 || cancel == (*it)->seqno)
        {
            (*it)->displayname(&name);

            cout << (*it)->seqno << ": " << name;

            if (d == PUT)
            {
                AppFilePut* f = (AppFilePut*) *it;

                cout << " -> ";

                if (f->targetuser.size())
                {
                    cout << f->targetuser << ":";
                }
                else
                {
                    string path;
                    nodepath(f->h, &path);
                    cout << path;
                }
            }

            if ((*it)->transfer && (*it)->transfer->slot)
            {
                cout << " [ACTIVE]";
            }
            cout << endl;

            if (cancel >= 0)
            {
                cout << "Canceling..." << endl;

                if ((*it)->transfer)
                {
                    client->stopxfer(*it);
                }
                delete *it++;
            }
            else
            {
                it++;
            }
        }
        else
        {
            it++;
        }
    }
}

#ifdef USE_MEDIAINFO

string showMediaInfo(const MediaProperties& mp, MediaFileInfo& mediaInfo, bool oneline)
{
    ostringstream out;
    string sep(oneline ? " " : "\n");

    MediaFileInfo::MediaCodecs::shortformatrec sf;
    sf.containerid = 0;
    sf.videocodecid = 0;
    sf.audiocodecid = 0;
    if (mp.shortformat == 255)
    {
        return "MediaInfo could not identify this file";
    }
    else if (mp.shortformat == 0)
    {
        // from attribute 9
        sf.containerid = mp.containerid;
        sf.videocodecid = mp.videocodecid;
        sf.audiocodecid = mp.audiocodecid;
    }
    else if (mp.shortformat < mediaInfo.mediaCodecs.shortformats.size())
    {
        sf = mediaInfo.mediaCodecs.shortformats[mp.shortformat];
    }

    for (std::map<std::string, unsigned>::const_iterator i = mediaInfo.mediaCodecs.containers.begin(); i != mediaInfo.mediaCodecs.containers.end(); ++i)
    {
        if (i->second == sf.containerid)
        {
            out << "Format: " << i->first << sep;
        }
    }
    for (std::map<std::string, unsigned>::const_iterator i = mediaInfo.mediaCodecs.videocodecs.begin(); i != mediaInfo.mediaCodecs.videocodecs.end(); ++i)
    {
        if (i->second == sf.videocodecid)
        {
            out << "Video: " << i->first << sep;
        }
    }

    for (std::map<std::string, unsigned>::const_iterator i = mediaInfo.mediaCodecs.audiocodecs.begin(); i != mediaInfo.mediaCodecs.audiocodecs.end(); ++i)
    {
        if (i->second == sf.audiocodecid)
        {
            out << "Audio: " << i->first << sep;
        }
    }

    if (mp.width > 0)
    {
        out << "Width: " << mp.width << sep;
    }
    if (mp.height > 0)
    {
        out << "Height: " << mp.height << sep;
    }
    if (mp.fps > 0)
    {
        out << "Fps: " << mp.fps << sep;
    }
    if (mp.playtime > 0)
    {
        out << "Playtime: " << mp.playtime << sep;
    }

    string result = out.str();
    result.erase(result.size() - (result.empty() ? 0 : 1));
    return result;
}

string showMediaInfo(const std::string& fileattributes, uint32_t fakey[4], MediaFileInfo& mediaInfo, bool oneline)
{
    MediaProperties mp = MediaProperties::decodeMediaPropertiesAttributes(fileattributes, fakey);
    return showMediaInfo(mp, mediaInfo, oneline);
}

string showMediaInfo(Node* n, MediaFileInfo& /*mediaInfo*/, bool oneline)
{
    if (n->hasfileattribute(fa_media))
    {
        MediaProperties mp = MediaProperties::decodeMediaPropertiesAttributes(n->fileattrstring, (uint32_t*)(n->nodekey.data() + FILENODEKEYLENGTH / 2));
        return showMediaInfo(mp, client->mediaFileInfo, oneline);
    }
    return "The node has no mediainfo attribute";
}

#endif

// password change-related state information
static byte pwkey[SymmCipher::KEYLENGTH];
static byte pwkeybuf[SymmCipher::KEYLENGTH];
static byte newpwkey[SymmCipher::KEYLENGTH];
static string newpassword;

// readline callback - exit if EOF, add to history unless password
#if !defined(WIN32) || !defined(NO_READLINE)
static void store_line(char* l)
{
    if (!l)
    {
        delete console;
        exit(0);
    }

#ifndef NO_READLINE
    if (*l && prompt == COMMAND)
    {
        add_history(l);
    }
#endif

    line = l;
}
#endif

class FileFindCommand : public Command
{
public:
    struct Stack : public std::deque<handle>
    {
        size_t filesLeft = 0;
        set<string> servers;
    };
        
    FileFindCommand(std::shared_ptr<Stack>& s, MegaClient* mc) : stack(s)
    {
        h = stack->front();
        stack->pop_front();

        client = mc;

        cmd("g");
        arg("n", (byte*)&h, MegaClient::NODEHANDLE);
        arg("g", 1);
        arg("v", 2);  // version 2: server can supply details for cloudraid files

        if (mc->usehttps)
        {
            arg("ssl", 2);
        }
    }

    static string server(const string& url)
    {
        const string pattern("://");
        size_t start_index = url.find(pattern);
        if (start_index != string::npos)
        {
            start_index += pattern.size();
            const size_t end_index = url.find("/", start_index);
            if (end_index != string::npos)
            {
                return url.substr(start_index, end_index - start_index);
            }
        }
        return "";
    }

    // process file credentials
    void procresult() override
    {
        if (client->json.isnumeric())
        {
            client->json.getint();
        }
        else
        {
            std::vector<string> tempurls;
            bool done = false;
            while (!done)
            {
                switch (client->json.getnameid())
                {
                case EOO:
                    done = true;
                    break;

                case 'g':
                    if (client->json.enterarray())   // now that we are requesting v2, the reply will be an array of 6 URLs for a raid download, or a single URL for the original direct download
                    {
                        for (;;)
                        {
                            std::string tu;
                            if (!client->json.storeobject(&tu))
                            {
                                break;
                            }
                            tempurls.push_back(tu);
                        }
                        client->json.leavearray();
                        if (tempurls.size() == 6)
                        {
                            if (Node* n = client->nodebyhandle(h))
                            {
                                cout << n->displaypath() << endl;

                                for (const auto& url : tempurls)
                                {
                                    stack->servers.insert(server(url));
                                }
                            }
                        }
                        break;
                    }
                    // otherwise fall through

                default:
                    client->json.storeobject();
                }
            }
        }

        // now query for the next one - we don't send them all at once as there may be a lot!
        --stack->filesLeft;
        if (!stack->empty())
        {
            client->reqs.add(new FileFindCommand(stack, client));
        }
        else if (!stack->filesLeft)
        {
            cout << "<find complete>" << endl;
            for (auto s : stack->servers)
            {
                cout << s << endl;
            }
        }
    }

private:
    handle h;
    std::shared_ptr<Stack> stack;
};


void getDepthFirstFileHandles(Node* n, deque<handle>& q)
{
    for (auto c : n->children)
    {
        if (c->type == FILENODE)
        {
            q.push_back(c->nodehandle);
        }
    }
    for (auto& c : n->children)
    {
        if (c->type > FILENODE)
        {
            getDepthFirstFileHandles(c, q);
        }
    }
}

void exec_find(autocomplete::ACState& s)
{
    if (s.words[1].s == "raided")
    {
        if (Node* n = client->nodebyhandle(cwd))
        {
            auto q = std::make_shared<FileFindCommand::Stack>();
            getDepthFirstFileHandles(n, *q);
            q->filesLeft = q->size();
            cout << "<find checking " << q->size() << " files>" << endl;
            if (q->empty())
            {
                cout << "<find complete>" << endl;
            }
            else
            {
                for (int i = 0; i < 25 && !q->empty(); ++i)
                {
                    client->reqs.add(new FileFindCommand(q, client));
                }
            }
        }
    }
}

bool typematchesnodetype(nodetype_t pathtype, nodetype_t nodetype)
{
    switch (pathtype)
    {
    case FILENODE: 
    case FOLDERNODE: return nodetype == pathtype;
    default: return false;
    }
}

#ifdef USE_FILESYSTEM
bool recursiveCompare(Node* mn, fs::path p)
{
    nodetype_t pathtype = fs::is_directory(p) ? FOLDERNODE : fs::is_regular_file(p) ? FILENODE : TYPE_UNKNOWN;
    if (!typematchesnodetype(pathtype, mn->type))
    {
        cout << "Path type mismatch: " << mn->displaypath() << ":" << mn->type << " " << p.u8string() << ":" << pathtype << endl;
        return false;
    }

    if (pathtype == FILENODE)
    {
        uint64_t size = (uint64_t) fs::file_size(p);
        if (size != (uint64_t) mn->size)
        {
            cout << "File size mismatch: " << mn->displaypath() << ":" << mn->size << " " << p.u8string() << ":" << size << endl;
        }
    }

    if (pathtype != FOLDERNODE)
    {
        return true;
    }

    multimap<string, Node*> ms;
    multimap<string, fs::path> ps;
    for (auto& m : mn->children) ms.emplace(m->displayname(), m);
    for (fs::directory_iterator pi(p); pi != fs::directory_iterator(); ++pi) ps.emplace(pi->path().filename().u8string(), pi->path());

    for (auto p_iter = ps.begin(); p_iter != ps.end(); )
    {
        auto er = ms.equal_range(p_iter->first);
        auto next_p = p_iter;
        ++next_p;
        for (auto i = er.first; i != er.second; ++i)
        {
            if (recursiveCompare(i->second, p_iter->second))
            {
                ms.erase(i);
                ps.erase(p_iter);
                break;
            }
        }
        p_iter = next_p;
    }
    if (ps.empty() && ms.empty())
    {
        return true;
    }
    else
    {
        cout << "Extra content detected between " << mn->displaypath() << " and " << p.u8string() << endl;
        for (auto& mi : ms) cout << "Extra remote: " << mi.first << endl;
        for (auto& pi : ps) cout << "Extra local: " << pi.second << endl;
        return false;
    };
}
#endif
Node* nodeFromRemotePath(const string& s)
{
    Node* n;
    if (s.empty())
    {
        n = client->nodebyhandle(cwd);
    }
    else 
    {
        n = nodebypath(s.c_str());
    }
    if (!n)
    {
        cout << "remote path not found: '" << s << "'" << endl;
    }
    return n;
}

#ifdef USE_FILESYSTEM
fs::path pathFromLocalPath(const string& s, bool mustexist)
{
    fs::path p = s.empty() ? fs::current_path() : fs::u8path(s);
    if (mustexist && !fs::exists(p))
    {
        cout << "local path not found: '" << s << "'";
        return fs::path();
    }
    return p;
}

void exec_treecompare(autocomplete::ACState& s)
{
    fs::path p = pathFromLocalPath(s.words[1].s, true);
    Node* n = nodeFromRemotePath(s.words[2].s);
    if (n && !p.empty())
    {
        recursiveCompare(n, p);
    }
}
#endif

void exec_getcloudstorageused(autocomplete::ACState& s)
{
    cout << client->mFingerprints.getSumSizes() << endl;
}

void exec_getuserquota(autocomplete::ACState& s)
{
    bool storage = s.extractflag("-storage");
    bool transfer = s.extractflag("-transfer");
    bool pro = s.extractflag("-pro");

    if (!storage && !transfer && !pro)
    {
        storage = transfer = pro = true;
    }    

    client->getaccountdetails(new AccountDetails, storage, transfer, pro, false, false, false, -1);
}

void exec_querytransferquota(autocomplete::ACState& ac)
{
    client->querytransferquota(atoll(ac.words[1].s.c_str()));
}

void DemoApp::querytransferquota_result(int n)
{
    cout << "querytransferquota_result: " << n << endl;
}

autocomplete::ACN autocompleteTemplate;

void exec_help(ac::ACState&)
{
    cout << *autocompleteTemplate << flush;
}

bool quit_flag = false;

void exec_quit(ac::ACState&)
{
    quit_flag = true;
}

void exec_showattributes(autocomplete::ACState& s)
{
    if (const Node* n = nodeFromRemotePath(s.words[1].s))
    {
        for (auto pair : n->attrs.map)
        {
            char namebuf[10]{};
            AttrMap::nameid2string(pair.first, namebuf);
            if (pair.first == 'c')
            {
                FileFingerprint f;
                f.unserializefingerprint(&pair.second);
                cout << namebuf << ": " << pair.second << " (fingerprint: size " << f.size << " mtime " << f.mtime
                    << " crc " << std::hex << f.crc[0] << " " << f.crc[1] << " " << f.crc[2] << " " << f.crc[3] << std::dec << ")"

                    << " (node fingerprint: size " << n->size << " mtime " << n->mtime
                    << " crc " << std::hex << n->crc[0] << " " << n->crc[1] << " " << n->crc[2] << " " << n->crc[3] << std::dec << ")" << endl;
            }
            else
            {
                cout << namebuf << ": " << pair.second << endl;
            }
        }
    }
}


autocomplete::ACN autocompleteSyntax()
{
    using namespace autocomplete;
    std::unique_ptr<Either> p(new Either("      "));

    p->Add(exec_apiurl, sequence(text("apiurl"), opt(sequence(param("url"), opt(param("disablepkp"))))));
    p->Add(exec_login, sequence(text("login"), either(sequence(param("email"), opt(param("password"))), exportedLink(false, true), param("session"), sequence(text("autoresume"), opt(param("id"))))));
    p->Add(exec_begin, sequence(text("begin"), opt(param("ephemeralhandle#ephemeralpw"))));
    p->Add(exec_signup, sequence(text("signup"), either(sequence(param("email"), param("name")), param("confirmationlink"))));
    p->Add(exec_cancelsignup, sequence(text("cancelsignup")));
    p->Add(exec_confirm, sequence(text("confirm")));
    p->Add(exec_session, sequence(text("session"), opt(sequence(text("autoresume"), opt(param("id"))))));
    p->Add(exec_mount, sequence(text("mount")));
    p->Add(exec_ls, sequence(text("ls"), opt(flag("-R")), opt(remoteFSFolder(client, &cwd))));
    p->Add(exec_cd, sequence(text("cd"), opt(remoteFSFolder(client, &cwd))));
    p->Add(exec_pwd, sequence(text("pwd")));
    p->Add(exec_lcd, sequence(text("lcd"), opt(localFSFolder())));
#ifdef USE_FILESYSTEM
    p->Add(exec_lls, sequence(text("lls"), opt(flag("-R")), opt(localFSFolder())));
    p->Add(exec_lpwd, sequence(text("lpwd")));
    p->Add(exec_lmkdir, sequence(text("lmkdir"), localFSFolder()));
#endif
    p->Add(exec_import, sequence(text("import"), exportedLink(true, false)));
    p->Add(exec_folderlinkinfo, sequence(text("folderlink"), opt(param("link"))));
    p->Add(exec_open, sequence(text("open"), exportedLink(false, true)));
    p->Add(exec_put, sequence(text("put"), localFSPath("localpattern"), opt(either(remoteFSPath(client, &cwd, "dst"),param("dstemail")))));
    p->Add(exec_putq, sequence(text("putq"), opt(param("cancelslot"))));
#ifdef USE_FILESYSTEM
    p->Add(exec_get, sequence(text("get"), opt(sequence(flag("-r"), opt(flag("-foldersonly")))), remoteFSPath(client, &cwd), opt(sequence(param("offset"), opt(param("length"))))));
#else
    p->Add(exec_get, sequence(text("get"), remoteFSPath(client, &cwd), opt(sequence(param("offset"), opt(param("length"))))));
#endif
    p->Add(exec_get, sequence(text("get"), flag("-re"), param("regularexpression")));
    p->Add(exec_get, sequence(text("get"), exportedLink(true, false), opt(sequence(param("offset"), opt(param("length"))))));
    p->Add(exec_getq, sequence(text("getq"), opt(param("cancelslot"))));
    p->Add(exec_pause, sequence(text("pause"), either(text("status"), sequence(opt(either(text("get"), text("put"))), opt(text("hard"))))));
    p->Add(exec_getfa, sequence(text("getfa"), wholenumber(1), opt(remoteFSPath(client, &cwd)), opt(text("cancel"))));
#ifdef USE_MEDIAINFO
    p->Add(exec_mediainfo, sequence(text("mediainfo"), either(sequence(text("calc"), localFSFile()), sequence(text("show"), remoteFSFile(client, &cwd)))));
#endif
    p->Add(exec_smsverify, sequence(text("smsverify"), either(sequence(text("send"), param("phonenumber"), opt(param("reverifywhitelisted"))), sequence(text("code"), param("verificationcode")))));
    p->Add(exec_verifiedphonenumber, sequence(text("verifiedphone")));
    p->Add(exec_mkdir, sequence(text("mkdir"), remoteFSFolder(client, &cwd)));
    p->Add(exec_rm, sequence(text("rm"), remoteFSPath(client, &cwd)));
    p->Add(exec_mv, sequence(text("mv"), remoteFSPath(client, &cwd, "src"), remoteFSPath(client, &cwd, "dst")));
    p->Add(exec_cp, sequence(text("cp"), remoteFSPath(client, &cwd, "src"), either(remoteFSPath(client, &cwd, "dst"), param("dstemail"))));
    p->Add(exec_du, sequence(text("du"), remoteFSPath(client, &cwd)));
#ifdef ENABLE_SYNC
    p->Add(exec_sync, sequence(text("sync"), opt(sequence(localFSPath(), either(remoteFSPath(client, &cwd, "dst"), param("cancelslot"))))));
#endif
    p->Add(exec_export, sequence(text("export"), remoteFSPath(client, &cwd), opt(either(param("expiretime"), text("del")))));
    p->Add(exec_share, sequence(text("share"), opt(sequence(remoteFSPath(client, &cwd), opt(sequence(contactEmail(client), opt(either(text("r"), text("rw"), text("full"))), opt(param("origemail"))))))));
    p->Add(exec_invite, sequence(text("invite"), param("dstemail"), opt(either(param("origemail"), text("del"), text("rmd")))));

    p->Add(exec_clink, sequence(text("clink"), either(text("renew"), sequence(text("query"), param("handle")), sequence(text("del"), opt(param("handle"))))));

    p->Add(exec_ipc, sequence(text("ipc"), param("handle"), either(text("a"), text("d"), text("i")))); 
    p->Add(exec_showpcr, sequence(text("showpcr")));
    p->Add(exec_users, sequence(text("users"), opt(sequence(contactEmail(client), text("del")))));
    p->Add(exec_getua, sequence(text("getua"), param("attrname"), opt(contactEmail(client))));
    p->Add(exec_putua, sequence(text("putua"), param("attrname"), opt(either(text("del"), sequence(text("set"), param("string")), sequence(text("load"), localFSFile())))));
#ifdef DEBUG
    p->Add(exec_delua, sequence(text("delua"), param("attrname")));
#endif
    p->Add(exec_alerts, sequence(text("alerts"), opt(either(text("new"), text("old"), wholenumber(10), text("notify"), text("seen")))));
    p->Add(exec_recentactions, sequence(text("recentactions"), param("hours"), param("maxcount")));
    p->Add(exec_recentnodes, sequence(text("recentnodes"), param("hours"), param("maxcount")));

    p->Add(exec_putbps, sequence(text("putbps"), opt(either(wholenumber(100000), text("auto"), text("none")))));
    p->Add(exec_killsession, sequence(text("killsession"), opt(either(text("all"), param("sessionid")))));
    p->Add(exec_whoami, sequence(text("whoami"), repeat(either(flag("-storage"), flag("-transfer"), flag("-pro"), flag("-transactions"), flag("-purchases"), flag("-sessions")))));
    p->Add(exec_passwd, sequence(text("passwd")));
    p->Add(exec_reset, sequence(text("reset"), contactEmail(client), opt(text("mk"))));
    p->Add(exec_recover, sequence(text("recover"), param("recoverylink")));
    p->Add(exec_cancel, sequence(text("cancel"), opt(param("cancellink"))));
    p->Add(exec_email, sequence(text("email"), opt(either(param("newemail"), param("emaillink")))));
    p->Add(exec_retry, sequence(text("retry")));
    p->Add(exec_recon, sequence(text("recon")));
    p->Add(exec_reload, sequence(text("reload"), opt(text("nocache"))));
    p->Add(exec_logout, sequence(text("logout")));
    p->Add(exec_locallogout, sequence(text("locallogout")));
    p->Add(exec_symlink, sequence(text("symlink")));
    p->Add(exec_version, sequence(text("version")));
    p->Add(exec_debug, sequence(text("debug")));
#if defined(WIN32) && defined(NO_READLINE)
    p->Add(exec_clear, sequence(text("clear")));
    p->Add(exec_codepage, sequence(text("codepage"), opt(sequence(wholenumber(65001), opt(wholenumber(65001))))));
    p->Add(exec_log, sequence(text("log"), either(text("utf8"), text("utf16"), text("codepage")), localFSFile()));
#endif
    p->Add(exec_test, sequence(text("test")));
#ifdef ENABLE_CHAT
    p->Add(exec_chats, sequence(text("chats")));
    p->Add(exec_chatc, sequence(text("chatc"), param("group"), repeat(opt(sequence(contactEmail(client), either(text("ro"), text("sta"), text("mod")))))));
    p->Add(exec_chati, sequence(text("chati"), param("chatid"), contactEmail(client), either(text("ro"), text("sta"), text("mod"))));
    p->Add(exec_chatcp, sequence(text("chatcp"), param("mownkey"), opt(sequence(text("t"), param("title64"))), repeat(sequence(contactEmail(client), either(text("ro"), text("sta"), text("mod"))))));
    p->Add(exec_chatr, sequence(text("chatr"), param("chatid"), opt(contactEmail(client))));
    p->Add(exec_chatu, sequence(text("chatu"), param("chatid")));
    p->Add(exec_chatup, sequence(text("chatup"), param("chatid"), param("userhandle"), either(text("ro"), text("sta"), text("mod"))));
    p->Add(exec_chatpu, sequence(text("chatpu")));
    p->Add(exec_chatga, sequence(text("chatga"), param("chatid"), param("nodehandle"), param("uid")));
    p->Add(exec_chatra, sequence(text("chatra"), param("chatid"), param("nodehandle"), param("uid")));
    p->Add(exec_chatst, sequence(text("chatst"), param("chatid"), param("title64")));
    p->Add(exec_chata, sequence(text("chata"), param("chatid"), param("archive")));
    p->Add(exec_chatl, sequence(text("chatl"), param("chatid"), either(text("del"), text("query"))));
    p->Add(exec_chatsm, sequence(text("chatsm"), param("chatid"), opt(param("title64"))));
    p->Add(exec_chatlu, sequence(text("chatlu"), param("publichandle")));
    p->Add(exec_chatlj, sequence(text("chatlj"), param("publichandle"), param("unifiedkey")));
#endif
    p->Add(exec_enabletransferresumption, sequence(text("enabletransferresumption"), opt(either(text("on"), text("off")))));
    p->Add(exec_setmaxdownloadspeed, sequence(text("setmaxdownloadspeed"), opt(wholenumber(10000))));
    p->Add(exec_setmaxuploadspeed, sequence(text("setmaxuploadspeed"), opt(wholenumber(10000))));
    p->Add(exec_handles, sequence(text("handles"), opt(either(text("on"), text("off")))));
    p->Add(exec_httpsonly, sequence(text("httpsonly"), opt(either(text("on"), text("off")))));

    p->Add(exec_mfac, sequence(text("mfac")));
    p->Add(exec_mfae, sequence(text("mfae")));
    p->Add(exec_mfad, sequence(text("mfad"), param("pin")));

#if defined(WIN32) && defined(NO_READLINE)
    p->Add(exec_autocomplete, sequence(text("autocomplete"), opt(either(text("unix"), text("dos")))));
    p->Add(exec_history, sequence(text("history")));
#endif
    p->Add(exec_help, either(text("help"), text("h"), text("?")));
    p->Add(exec_quit, either(text("quit"), text("q"), text("exit")));

    p->Add(exec_find, sequence(text("find"), text("raided")));
#ifdef USE_FILESYSTEM
    p->Add(exec_treecompare, sequence(text("treecompare"), localFSPath(), remoteFSPath(client, &cwd)));
#endif
    p->Add(exec_querytransferquota, sequence(text("querytransferquota"), param("filesize")));
    p->Add(exec_getcloudstorageused, sequence(text("getcloudstorageused")));
    p->Add(exec_getuserquota, sequence(text("getuserquota"), repeat(either(flag("-storage"), flag("-transfer"), flag("-pro")))));

    p->Add(exec_showattributes, sequence(text("showattributes"), remoteFSPath(client, &cwd)));

    return autocompleteTemplate = std::move(p);
}


#ifdef USE_FILESYSTEM
bool recursiveget(fs::path&& localpath, Node* n, bool folders, unsigned& queued)
{
    if (n->type == FILENODE)
    {
        if (!folders)
        {
            auto f = new AppFileGet(n, UNDEF, NULL, -1, 0, NULL, NULL, localpath.u8string());
            f->appxfer_it = appxferq[GET].insert(appxferq[GET].end(), f);
            client->startxfer(GET, f);
            queued += 1;
        }
    }
    else if (n->type == FOLDERNODE || n->type == ROOTNODE)
    {
        fs::path newpath = localpath / fs::u8path(n->type == ROOTNODE ? "ROOTNODE" : n->displayname());
        if (folders)
        {
            std::error_code ec; 
            if (fs::create_directory(newpath, ec) || !ec)
            {
                cout << newpath << endl;
            }
            else
            {
                cout << "Failed trying to create " << newpath << ": " << ec.message() << endl;
                return false;
            }
        }
        for (node_list::iterator it = n->children.begin(); it != n->children.end(); it++)
        {
            if (!recursiveget(std::move(newpath), *it, folders, queued))
            {
                return false;
            }
        }
    }
    return true;
}
#endif

bool regexget(const string& expression, Node* n, unsigned& queued)
{
    try
    {
        std::regex re(expression);

        if (n->type == FOLDERNODE || n->type == ROOTNODE)
        {
            for (node_list::iterator it = n->children.begin(); it != n->children.end(); it++)
            {
                if ((*it)->type == FILENODE)
                {
                    if (regex_search(string((*it)->displayname()), re))
                    {
                        auto f = new AppFileGet(n);
                        f->appxfer_it = appxferq[GET].insert(appxferq[GET].end(), f);
                        client->startxfer(GET, f);
                        queued += 1;
                    }
                }
            }
        }
    }
    catch (std::exception& e)
    {
        cout << "ERROR: " << e.what() << endl;
        return false;
    }
    return true;
}

struct Login
{
    string email, password, salt, pin;
    int version;

    Login() : version(0)
    {
    }

    void reset()
    {
        *this = Login();
    }

    void login(MegaClient* mc)
    {
        byte keybuf[SymmCipher::KEYLENGTH];

        if (version == 1)
        {
            if (error e = mc->pw_key(password.c_str(), keybuf))
            {
                cout << "Login error: " << e << endl;
            }
            else
            {
                mc->login(email.c_str(), keybuf, (!pin.empty()) ? pin.c_str() : NULL);
            }
        }
        else if (version == 2 && !salt.empty())
        {
            mc->login2(email.c_str(), password.c_str(), &salt, (!pin.empty()) ? pin.c_str() : NULL);
        }
        else
        {
            cout << "Login unexpected error" << endl;
        }
    }
};
static Login login;

ofstream* pread_file = NULL;
m_off_t pread_file_end = 0;


// execute command
static void process_line(char* l)
{
    switch (prompt)
    {
    case LOGINTFA:
        if (strlen(l) > 1)
        {
            login.pin = l;
            login.login(client);
        }
        else
        {
            cout << endl << "The pin length is invalid, please try to login again." << endl;
        }

        setprompt(COMMAND);
        return;

    case SETTFA:
        client->multifactorauthsetup(l);
        setprompt(COMMAND);
        return;

    case LOGINPASSWORD:

        if (signupcode.size())
        {
            // verify correctness of supplied signup password
            client->pw_key(l, pwkey);
            SymmCipher pwcipher(pwkey);
            pwcipher.ecb_decrypt(signuppwchallenge);

            if (MemAccess::get<int64_t>((const char*)signuppwchallenge + 4))
            {
                cout << endl << "Incorrect password, please try again." << endl;
            }
            else
            {
                client->confirmsignuplink((const byte*)signupcode.data(), unsigned(signupcode.size()),
                    MegaClient::stringhash64(&signupemail, &pwcipher));
            }

            signupcode.clear();
        }
        else if (recoverycode.size())   // cancelling account --> check password
        {
            client->pw_key(l, pwkey);
            client->validatepwd(pwkey);
        }
        else if (changecode.size())     // changing email --> check password to avoid creating an invalid hash
        {
            client->pw_key(l, pwkey);
            client->validatepwd(pwkey);
        }
        else
        {
            login.password = l;
            login.login(client);
            cout << endl << "Logging in..." << endl;
        }

        setprompt(COMMAND);
        return;

    case OLDPASSWORD:
        client->pw_key(l, pwkeybuf);

        if (!memcmp(pwkeybuf, pwkey, sizeof pwkey))
        {
            cout << endl;
            setprompt(NEWPASSWORD);
        }
        else
        {
            cout << endl << "Bad password, please try again" << endl;
            setprompt(COMMAND);
        }
        return;

    case NEWPASSWORD:
        newpassword = l;
        client->pw_key(l, newpwkey);

        cout << endl;
        setprompt(PASSWORDCONFIRM);
        return;

    case PASSWORDCONFIRM:
        client->pw_key(l, pwkeybuf);

        if (memcmp(pwkeybuf, newpwkey, sizeof pwkeybuf))
        {
            cout << endl << "Mismatch, please try again" << endl;
        }
        else
        {
            error e;

            if (signupemail.size())
            {
                client->sendsignuplink(signupemail.c_str(), signupname.c_str(), newpwkey);
            }
            else if (recoveryemail.size() && recoverycode.size())
            {
                cout << endl << "Resetting password..." << endl;

                if (hasMasterKey)
                {
                    client->confirmrecoverylink(recoverycode.c_str(), recoveryemail.c_str(), newpassword.c_str(), masterkey);
                }
                else
                {
                    client->confirmrecoverylink(recoverycode.c_str(), recoveryemail.c_str(), newpassword.c_str(), NULL);
                }

                recoverycode.clear();
                recoveryemail.clear();
                hasMasterKey = false;
                memset(masterkey, 0, sizeof masterkey);
            }
            else
            {
                if ((e = client->changepw(newpassword.c_str())) == API_OK)
                {
                    memcpy(pwkey, newpwkey, sizeof pwkey);
                    cout << endl << "Changing password..." << endl;
                }
                else
                {
                    cout << "You must be logged in to change your password." << endl;
                }
            }
        }

        setprompt(COMMAND);
        signupemail.clear();
        return;

    case MASTERKEY:
        cout << endl << "Retrieving private RSA key for checking integrity of the Master Key..." << endl;

        Base64::atob(l, masterkey, sizeof masterkey);
        client->getprivatekey(recoverycode.c_str());
        return;

    case COMMAND:
        try
        {
            std::string consoleOutput;
            ac::autoExec(string(l), string::npos, autocompleteTemplate, false, consoleOutput, true); // todo: pass correct unixCompletions flag
            if (!consoleOutput.empty())
            {
                cout << consoleOutput << flush;
            }
        }
        catch (std::exception& e)
        {
            cout << "Command failed: " << e.what() << endl;
        }
        return;
    }
}

void exec_ls(autocomplete::ACState& s)
{
    Node* n;
    bool recursive = s.extractflag("-R");

    if (s.words.size() > 1)
    {
        n = nodebypath(s.words[1].s.c_str());
    }
    else
    {
        n = client->nodebyhandle(cwd);
    }

    if (n)
    {
        dumptree(n, recursive);
    }
}

void exec_cd(autocomplete::ACState& s)
{
    if (s.words.size() > 1)
    {
        if (Node* n = nodebypath(s.words[1].s.c_str()))
        {
            if (n->type == FILENODE)
            {
                cout << s.words[1].s << ": Not a directory" << endl;
            }
            else
            {
                cwd = n->nodehandle;
            }
        }
        else
        {
            cout << s.words[1].s << ": No such file or directory" << endl;
        }
    }
    else
    {
        cwd = client->rootnodes[0];
    }
}

void exec_rm(autocomplete::ACState& s)
{
    if (s.words.size() > 1)
    {
        if (Node* n = nodebypath(s.words[1].s.c_str()))
        {
            if (client->checkaccess(n, FULL))
            {
                error e = client->unlink(n);

                if (e)
                {
                    cout << s.words[1].s << ": Deletion failed (" << errorstring(e) << ")" << endl;
                }
            }
            else
            {
                cout << s.words[1].s << ": Access denied" << endl;
            }
        }
        else
        {
            cout << s.words[1].s << ": No such file or directory" << endl;
        }
    }
}

void exec_mv(autocomplete::ACState& s)
{
    Node *n, *tn;
    string newname;

    if (s.words.size() > 2)
    {
        // source node must exist
        if ((n = nodebypath(s.words[1].s.c_str())))
        {
            // we have four situations:
            // 1. target path does not exist - fail
            // 2. target node exists and is folder - move
            // 3. target node exists and is file - delete and rename (unless same)
            // 4. target path exists, but filename does not - rename
            if ((tn = nodebypath(s.words[2].s.c_str(), NULL, &newname)))
            {
                error e;

                if (newname.size())
                {
                    if (tn->type == FILENODE)
                    {
                        cout << s.words[2].s << ": Not a directory" << endl;

                        return;
                    }
                    else
                    {
                        if ((e = client->checkmove(n, tn)) == API_OK)
                        {
                            if (!client->checkaccess(n, RDWR))
                            {
                                cout << "Write access denied" << endl;

                                return;
                            }

                            // rename
                            client->fsaccess->normalize(&newname);
                            n->attrs.map['n'] = newname;

                            if ((e = client->setattr(n)))
                            {
                                cout << "Cannot rename file (" << errorstring(e) << ")" << endl;
                            }
                        }
                    }
                }
                else
                {
                    if (tn->type == FILENODE)
                    {
                        // (there should never be any orphaned filenodes)
                        if (!tn->parent)
                        {
                            return;
                        }

                        if ((e = client->checkmove(n, tn->parent)) == API_OK)
                        {
                            if (!client->checkaccess(n, RDWR))
                            {
                                cout << "Write access denied" << endl;

                                return;
                            }

                            // overwrite existing target file: rename source...
                            n->attrs.map['n'] = tn->attrs.map['n'];
                            e = client->setattr(n);

                            if (e)
                            {
                                cout << "Rename failed (" << errorstring(e) << ")" << endl;
                            }

                            if (n != tn)
                            {
                                // ...delete target...
                                e = client->unlink(tn);

                                if (e)
                                {
                                    cout << "Remove failed (" << errorstring(e) << ")" << endl;
                                }
                            }
                        }

                        // ...and set target to original target's parent
                        tn = tn->parent;
                    }
                    else
                    {
                        e = client->checkmove(n, tn);
                    }
                }

                if (n->parent != tn)
                {
                    if (e == API_OK)
                    {
                        e = client->rename(n, tn);

                        if (e)
                        {
                            cout << "Move failed (" << errorstring(e) << ")" << endl;
                        }
                    }
                    else
                    {
                        cout << "Move not permitted - try copy" << endl;
                    }
                }
            }
            else
            {
                cout << s.words[2].s << ": No such directory" << endl;
            }
        }
        else
        {
            cout << s.words[1].s << ": No such file or directory" << endl;
        }
    }
}


void exec_cp(autocomplete::ACState& s)
{
    Node *n, *tn;
    string targetuser;
    string newname;
    error e;

    if (s.words.size() > 2)
    {
        if ((n = nodebypath(s.words[1].s.c_str())))
        {
            if ((tn = nodebypath(s.words[2].s.c_str(), &targetuser, &newname)))
            {
                if (!client->checkaccess(tn, RDWR))
                {
                    cout << "Write access denied" << endl;

                    return;
                }

                if (tn->type == FILENODE)
                {
                    if (n->type == FILENODE)
                    {
                        // overwrite target if source and taret are files

                        // (there should never be any orphaned filenodes)
                        if (!tn->parent)
                        {
                            return;
                        }

                        // ...delete target...
                        e = client->unlink(tn);

                        if (e)
                        {
                            cout << "Cannot delete existing file (" << errorstring(e) << ")"
                                << endl;
                        }

                        // ...and set target to original target's parent
                        tn = tn->parent;
                    }
                    else
                    {
                        cout << "Cannot overwrite file with folder" << endl;
                        return;
                    }
                }
            }

            TreeProcCopy_mcli tc;
            unsigned nc;
            handle ovhandle = UNDEF;

            if (!n->nodekey.size())
            {
                cout << "Cannot copy a node without key" << endl;
                return;
            }

            if (n->attrstring)
            {
                n->applykey();
                n->setattr();
                if (n->attrstring)
                {
                    cout << "Cannot copy undecryptable node" << endl;
                    return;
                }
            }

            string sname;
            if (newname.size())
            {
                sname = newname;
                client->fsaccess->normalize(&sname);
            }
            else
            {
                attr_map::iterator it = n->attrs.map.find('n');
                if (it != n->attrs.map.end())
                {
                    sname = it->second;
                }
            }

            if (!client->versions_disabled && tn && n->type == FILENODE)
            {
                Node *ovn = client->childnodebyname(tn, sname.c_str(), true);
                if (ovn)
                {
                    if (n->isvalid && ovn->isvalid && *(FileFingerprint*)n == *(FileFingerprint*)ovn)
                    {
                        cout << "Skipping identical node" << endl;
                        return;
                    }

                    ovhandle = ovn->nodehandle;
                }
            }

            // determine number of nodes to be copied
            client->proctree(n, &tc, false, ovhandle != UNDEF);

            tc.allocnodes();
            nc = tc.nc;

            // build new nodes array
            client->proctree(n, &tc, false, ovhandle != UNDEF);

            // if specified target is a filename, use it
            if (newname.size())
            {
                SymmCipher key;
                string attrstring;

                // copy source attributes and rename
                AttrMap attrs;

                attrs.map = n->attrs.map;
                attrs.map['n'] = sname;

                key.setkey((const byte*)tc.nn->nodekey.data(), tc.nn->type);

                // JSON-encode object and encrypt attribute string
                attrs.getjson(&attrstring);
                tc.nn->attrstring = new string;
                client->makeattr(&key, tc.nn->attrstring, attrstring.c_str());
            }

            // tree root: no parent
            tc.nn->parenthandle = UNDEF;
            tc.nn->ovhandle = ovhandle;

            if (tn)
            {
                // add the new nodes
                client->putnodes(tn->nodehandle, tc.nn, nc);

                // free in putnodes_result()
                tc.nn = NULL;
            }
            else
            {
                if (targetuser.size())
                {
                    cout << "Attempting to drop into user " << targetuser << "'s inbox..." << endl;

                    client->putnodes(targetuser.c_str(), tc.nn, nc);

                    // free in putnodes_result()
                    tc.nn = NULL;
                }
                else
                {
                    cout << s.words[2].s << ": No such file or directory" << endl;
                }
            }
        }
        else
        {
            cout << s.words[1].s << ": No such file or directory" << endl;
        }
    }
}

void exec_du(autocomplete::ACState& s)
{
    Node *n;
    TreeProcDU du;

    if (s.words.size() > 1)
    {
        if (!(n = nodebypath(s.words[1].s.c_str())))
        {
            cout << s.words[1].s << ": No such file or directory" << endl;

            return;
        }
    }
    else
    {
        n = client->nodebyhandle(cwd);
    }

    if (n)
    {
        client->proctree(n, &du);

        cout << "Total storage used: " << (du.numbytes / 1048576) << " MB" << endl;
        cout << "Total # of files: " << du.numfiles << endl;
        cout << "Total # of folders: " << du.numfolders << endl;
    }
}

void exec_get(autocomplete::ACState& s)
{
    Node *n;
    string regularexpression;
    if (s.extractflag("-r"))
    {
#ifdef USE_FILESYSTEM
        // recursive get.  create local folder structure first, then queue transfer of all files 
        bool foldersonly = s.extractflag("-foldersonly");

        if (!(n = nodebypath(s.words[1].s.c_str())))
        {
            cout << s.words[1].s << ": No such folder (or file)" << endl;
        }
        else if (n->type != FOLDERNODE && n->type != ROOTNODE)
        {
            cout << s.words[1].s << ": not a folder" << endl;
        }
        else
        {
            unsigned queued = 0;
            cout << "creating folders: " << endl;
            if (recursiveget(fs::current_path(), n, true, queued))
            {
                if (!foldersonly)
                {
                    cout << "queueing files..." << endl;
                    bool alldone = recursiveget(fs::current_path(), n, false, queued);
                    cout << "queued " << queued << " files for download" << (!alldone ? " before failure" : "") << endl;
                }
            }
        }
#else
        cout << "Sorry, -r not supported yet" << endl;
#endif
    }
    else if (s.extractflagparam("-re", regularexpression))
    {
        if (!(n = nodebypath(".")))
        {
            cout << ": No current folder" << endl;
        }
        else if (n->type != FOLDERNODE && n->type != ROOTNODE)
        {
            cout << ": not in a folder" << endl;
        }
        else
        {
            unsigned queued = 0;
            if (regexget(regularexpression, n, queued))
            {
                cout << "queued " << queued << " files for download" << endl;
            }
        }
    }
    else
    {
        if (client->openfilelink(s.words[1].s.c_str(), 0) == API_OK)
        {
            cout << "Checking link..." << endl;
            return;
        }

        n = nodebypath(s.words[1].s.c_str());

        if (n)
        {
            if (s.words.size() > 2)
            {
                // read file slice
                if (s.words.size() == 5)
                {
                    pread_file = new ofstream(s.words[4].s.c_str(), std::ios_base::binary);
                    pread_file_end = atol(s.words[2].s.c_str()) + atol(s.words[3].s.c_str());
                }

                client->pread(n, atol(s.words[2].s.c_str()), (s.words.size() > 3) ? atol(s.words[3].s.c_str()) : 0, NULL);
            }
            else
            {
                AppFile* f;

                // queue specified file...
                if (n->type == FILENODE)
                {
                    f = new AppFileGet(n);

                    string::size_type index = s.words[1].s.find(":");
                    // node from public folder link
                    if (index != string::npos && s.words[1].s.substr(0, index).find("@") == string::npos)
                    {
                        handle h = clientFolder->getrootpublicfolder();
                        char *pubauth = new char[12];
                        Base64::btoa((byte*)&h, MegaClient::NODEHANDLE, pubauth);
                        f->pubauth = pubauth;
                        f->hprivate = true;
                        f->hforeign = true;
                        memcpy(f->filekey, n->nodekey.data(), FILENODEKEYLENGTH);
                    }

                    f->appxfer_it = appxferq[GET].insert(appxferq[GET].end(), f);
                    client->startxfer(GET, f);
                }
                else
                {
                    // ...or all files in the specified folder (non-recursive)
                    for (node_list::iterator it = n->children.begin(); it != n->children.end(); it++)
                    {
                        if ((*it)->type == FILENODE)
                        {
                            f = new AppFileGet(*it);
                            f->appxfer_it = appxferq[GET].insert(appxferq[GET].end(), f);
                            client->startxfer(GET, f);
                        }
                    }
                }
            }
        }
        else
        {
            cout << s.words[1].s << ": No such file or folder" << endl;
        }
    }
}

void exec_put(autocomplete::ACState& s)
{
    AppFile* f;
    handle target = cwd;
    string targetuser;
    string newname;
    int total = 0;
    string localname;
    string name;
    nodetype_t type;
    Node* n = NULL;

    if (s.words.size() > 2)
    {
        if ((n = nodebypath(s.words[2].s.c_str(), &targetuser, &newname)))
        {
            target = n->nodehandle;
        }
    }
    else    // target is current path
    {
        n = client->nodebyhandle(target);
    }

    if (client->loggedin() == NOTLOGGEDIN && !targetuser.size())
    {
        cout << "Not logged in." << endl;

        return;
    }

    client->fsaccess->path2local(&s.words[1].s, &localname);

    DirAccess* da = client->fsaccess->newdiraccess();

    if (da->dopen(&localname, NULL, true))
    {
        while (da->dnext(NULL, &localname, true, &type))
        {
            client->fsaccess->local2path(&localname, &name);
            cout << "Queueing " << name << "..." << endl;

            if (type == FILENODE)
            {
                FileAccess *fa = client->fsaccess->newfileaccess();
                if (fa->fopen(&name, true, false))
                {
                    FileFingerprint fp;
                    fp.genfingerprint(fa);

                    Node *previousNode = client->childnodebyname(n, name.c_str(), true);
                    if (previousNode && previousNode->type == type)
                    {
                        if (fp.isvalid && previousNode->isvalid && fp == *((FileFingerprint *)previousNode))
                        {
                            cout << "Identical file already exist. Skipping transfer of " << name << endl;
                            delete fa;
                            continue;
                        }
                    }
                }
                delete fa;

                f = new AppFilePut(&localname, target, targetuser.c_str());
                f->appxfer_it = appxferq[PUT].insert(appxferq[PUT].end(), f);
                client->startxfer(PUT, f);
                total++;
            }
        }
    }

    delete da;

    cout << "Queued " << total << " file(s) for upload, " << appxferq[PUT].size()
        << " file(s) in queue" << endl;
}

void exec_pwd(autocomplete::ACState& s)
{
    string path;

    nodepath(cwd, &path);

    cout << path << endl;
}

void exec_lcd(autocomplete::ACState& s)
{
    string localpath;

    client->fsaccess->path2local(&s.words[1].s, &localpath);

    if (!client->fsaccess->chdirlocal(&localpath))
    {
        cout << s.words[1].s << ": Failed" << endl;
    }
}

#ifdef USE_FILESYSTEM
void exec_lls(autocomplete::ACState& s)
{
    bool recursive = s.extractflag("-R");
    fs::path ls_folder = s.words.size() > 1 ? fs::u8path(s.words[1].s) : fs::current_path();
    std::error_code ec;
    auto status = fs::status(ls_folder, ec);
    (void)status;
    if (ec)
    {
        cerr << ec.message() << endl;
    }
    else if (!fs::exists(ls_folder))
    {
        cerr << "not found" << endl;
    }
    else
    {
        local_dumptree(ls_folder, recursive);
    }
}
#endif

void exec_ipc(autocomplete::ACState& s)
{
    // incoming pending contact action
    handle phandle;
    if (s.words.size() == 3 && Base64::atob(s.words[1].s.c_str(), (byte*) &phandle, sizeof phandle) == sizeof phandle)
    {
        ipcactions_t action;
        if (s.words[2].s == "a")
        {
            action = IPCA_ACCEPT;
        }
        else if (s.words[2].s == "d")
        {
            action = IPCA_DENY;
        }
        else if (s.words[2].s == "i")
        {
            action = IPCA_IGNORE;
        }
        else
        {
            return;
        }
        client->updatepcr(phandle, action);
    }
}

#if defined(WIN32) && defined(NO_READLINE)
void exec_log(autocomplete::ACState& s)
{
    if (s.words.size() == 1)
    {
        // close log
        static_cast<WinConsole*>(console)->log("", WinConsole::no_log);
        cout << "log closed" << endl;
    }
    else if (s.words.size() == 3)
    {
        // open log
        WinConsole::logstyle style = WinConsole::no_log;
        if (s.words[1].s == "utf8")
        {
            style = WinConsole::utf8_log;
        }
        else if (s.words[1].s == "utf16")
        {
            style = WinConsole::utf16_log;
        }
        else if (s.words[1].s == "codepage")
        {
            style = WinConsole::codepage_log;
        }
        else
        {
            cout << "unknown log style" << endl;
        }
        if (!static_cast<WinConsole*>(console)->log(s.words[2].s, style))
        {
            cout << "failed to open log file" << endl;
        }
    }
}
#endif

void exec_putq(autocomplete::ACState& s)
{
    xferq(PUT, s.words.size() > 1 ? atoi(s.words[1].s.c_str()) : -1);
}

void exec_getq(autocomplete::ACState& s)
{
    xferq(GET, s.words.size() > 1 ? atoi(s.words[1].s.c_str()) : -1);
}

void exec_open(autocomplete::ACState& s)
{
    if (strstr(s.words[1].s.c_str(), "#F!"))  // folder link indicator
    {
        if (!clientFolder)
        {
            using namespace mega;
            // create a new MegaClient with a different MegaApp to process callbacks
            // from the client logged into a folder. Reuse the waiter and httpio
            clientFolder = new MegaClient(new DemoAppFolder, client->waiter,
                                            client->httpio, new FSACCESS_CLASS,
                #ifdef DBACCESS_CLASS
                                            new DBACCESS_CLASS,
                #else
                                            NULL,
                #endif
                #ifdef GFX_CLASS
                                            new GFX_CLASS,
                #else
                                            NULL,
                #endif
                                            "Gk8DyQBS",
                                            "megacli_folder/" TOSTRING(MEGA_MAJOR_VERSION)
                                            "." TOSTRING(MEGA_MINOR_VERSION)
                                            "." TOSTRING(MEGA_MICRO_VERSION));
        }
        else
        {
            clientFolder->logout();
        }

        return clientFolder->app->login_result(clientFolder->folderaccess(s.words[1].s.c_str()));
    }
    else
    {
        cout << "Invalid folder link." << endl;
    }
}

#ifdef ENABLE_SYNC
void exec_sync(autocomplete::ACState& s)
{
    if (s.words.size() == 3)
    {
        Node* n = nodebypath(s.words[2].s.c_str());

        if (client->checkaccess(n, FULL))
        {
            string localname;

            client->fsaccess->path2local(&s.words[1].s, &localname);

            if (!n)
            {
                cout << s.words[2].s << ": Not found." << endl;
            }
            else if (n->type == FILENODE)
            {
                cout << s.words[2].s << ": Remote sync root must be folder." << endl;
            }
            else
            {
                error e = client->addsync(&localname, DEBRISFOLDER, NULL, n);

                if (e)
                {
                    cout << "Sync could not be added: " << errorstring(e) << endl;
                }
            }
        }
        else
        {
            cout << s.words[2].s << ": Syncing requires full access to path." << endl;
        }
    }
    else if (s.words.size() == 2)
    {
        int i = 0, cancel = atoi(s.words[1].s.c_str());

        for (sync_list::iterator it = client->syncs.begin(); it != client->syncs.end(); it++)
        {
            if ((*it)->state > SYNC_CANCELED && i++ == cancel)
            {
                client->delsync(*it);

                cout << "Sync " << cancel << " deactivated and removed." << endl;
                break;
            }
        }
    }
    else if (s.words.size() == 1)
    {
        if (client->syncs.size())
        {
            int i = 0;
            string remotepath, localpath;

            for (sync_list::iterator it = client->syncs.begin(); it != client->syncs.end(); it++)
            {
                if ((*it)->state > SYNC_CANCELED)
                {
                    static const char* syncstatenames[] =
                    { "Initial scan, please wait", "Active", "Failed" };

                    if ((*it)->localroot.node)
                    {
                        nodepath((*it)->localroot.node->nodehandle, &remotepath);
                        client->fsaccess->local2path(&(*it)->localroot.localname, &localpath);

                        cout << i++ << ": " << localpath << " to " << remotepath << " - "
                                << syncstatenames[(*it)->state] << ", " << (*it)->localbytes
                                << " byte(s) in " << (*it)->localnodes[FILENODE] << " file(s) and "
                                << (*it)->localnodes[FOLDERNODE] << " folder(s)" << endl;
                    }
                }
            }
        }
        else
        {
            cout << "No syncs active at this time." << endl;
        }
    }
}
#endif

#ifdef USE_FILESYSTEM
void exec_lpwd(autocomplete::ACState& s)
{
    cout << fs::current_path().u8string() << endl;
}
#endif


void exec_test(autocomplete::ACState& s)
{
}

void exec_mfad(autocomplete::ACState& s)
{
    client->multifactorauthdisable(s.words[1].s.c_str());
}

void exec_mfac(autocomplete::ACState& s)
{
    client->multifactorauthcheck(login.email.c_str());
}

void exec_mfae(autocomplete::ACState& s)
{
    client->multifactorauthsetup();
}

void exec_login(autocomplete::ACState& s)
{
    if (client->loggedin() == NOTLOGGEDIN)
    {
        if (s.words.size() > 1)
        {
            if ((s.words.size() == 2 || s.words.size() == 3) && s.words[1].s == "autoresume")
            {
                string filename = "megacli_autoresume_session" + (s.words.size() == 3 ? "_" + s.words[2].s : "");
                ifstream file(filename.c_str());
                string session;
                file >> session;
                if (file.is_open() && session.size())
                {
                    byte sessionraw[64];
                    if (session.size() < sizeof sessionraw * 4 / 3)
                    {
                        int size = Base64::atob(session.c_str(), sessionraw, sizeof sessionraw);

                        cout << "Resuming session..." << endl;
                        return client->login(sessionraw, size);
                    }
                }
                cout << "Failed to get a valid session id from file " << filename << endl;
            }
            else if (strchr(s.words[1].s.c_str(), '@'))
            {
                login.reset();
                login.email = s.words[1].s;

                // full account login
                if (s.words.size() > 2)
                {
                    login.password = s.words[2].s;
                    cout << "Initiated login attempt..." << endl;
                }
                client->prelogin(login.email.c_str());
            }
            else
            {
                const char* ptr;
                if ((ptr = strchr(s.words[1].s.c_str(), '#')))  // folder link indicator
                {
                    return client->app->login_result(client->folderaccess(s.words[1].s.c_str()));
                }
                else
                {
                    byte session[64];
                    int size;

                    if (s.words[1].s.size() < sizeof session * 4 / 3)
                    {
                        size = Base64::atob(s.words[1].s.c_str(), session, sizeof session);

                        cout << "Resuming session..." << endl;

                        return client->login(session, size);
                    }
                }

                cout << "Invalid argument. Please specify a valid e-mail address, "
                    << "a folder link containing the folder key "
                    << "or a valid session." << endl;
            }
        }
        else
        {
            cout << "      login email [password]" << endl
                << "      login exportedfolderurl#key" << endl
                << "      login session" << endl;
        }
    }
    else
    {
        cout << "Already logged in. Please log out first." << endl;
    }
}

void exec_begin(autocomplete::ACState& s)
{
    if (s.words.size() == 1)
    {
        cout << "Creating ephemeral session..." << endl;
        pdf_to_import = true;
        client->createephemeral();
    }
    else if (s.words.size() == 2)
    {
        handle uh;
        byte pw[SymmCipher::KEYLENGTH];

        if (Base64::atob(s.words[1].s.c_str(), (byte*) &uh, MegaClient::USERHANDLE) == sizeof uh && Base64::atob(
            s.words[1].s.c_str() + 12, pw, sizeof pw) == sizeof pw)
        {
            client->resumeephemeral(uh, pw);
        }
        else
        {
            cout << "Malformed ephemeral session identifier." << endl;
        }
    }
}

void exec_mount(autocomplete::ACState& s)
{
    listtrees();
}

void exec_share(autocomplete::ACState& s)
{
    switch (s.words.size())
    {
    case 1:		// list all shares (incoming and outgoing)
    {
        TreeProcListOutShares listoutshares;
        Node* n;

        cout << "Shared folders:" << endl;

        for (unsigned i = 0; i < sizeof client->rootnodes / sizeof *client->rootnodes; i++)
        {
            if ((n = client->nodebyhandle(client->rootnodes[i])))
            {
                client->proctree(n, &listoutshares);
            }
        }

        for (user_map::iterator uit = client->users.begin();
            uit != client->users.end(); uit++)
        {
            User* u = &uit->second;
            Node* n;

            if (u->show == VISIBLE && u->sharing.size())
            {
                cout << "From " << u->email << ":" << endl;

                for (handle_set::iterator sit = u->sharing.begin();
                    sit != u->sharing.end(); sit++)
                {
                    if ((n = client->nodebyhandle(*sit)))
                    {
                        cout << "\t" << n->displayname() << " ("
                            << getAccessLevelStr(n->inshare->access) << ")" << endl;
                    }
                }
            }
        }
    }
    break;

    case 2:	    // list all outgoing shares on this path
    case 3:	    // remove outgoing share to specified e-mail address
    case 4:	    // add outgoing share to specified e-mail address
    case 5:     // user specified a personal representation to appear as for the invitation
        if (Node* n = nodebypath(s.words[1].s.c_str()))
        {
            if (s.words.size() == 2)
            {
                listnodeshares(n);
            }
            else
            {
                accesslevel_t a = ACCESS_UNKNOWN;
                const char* personal_representation = NULL;
                if (s.words.size() > 3)
                {
                    if (s.words[3].s == "r" || s.words[3].s == "ro")
                    {
                        a = RDONLY;
                    }
                    else if (s.words[3].s == "rw")
                    {
                        a = RDWR;
                    }
                    else if (s.words[3].s == "full")
                    {
                        a = FULL;
                    }
                    else
                    {
                        cout << "Access level must be one of r, rw or full" << endl;

                        return;
                    }

                    if (s.words.size() > 4)
                    {
                        personal_representation = s.words[4].s.c_str();
                    }
                }

                client->setshare(n, s.words[2].s.c_str(), a, personal_representation);
            }
        }
        else
        {
            cout << s.words[1].s << ": No such directory" << endl;
        }
        break;
    }
}

void exec_users(autocomplete::ACState& s)
{
    if (s.words.size() == 1)
    {
        for (user_map::iterator it = client->users.begin(); it != client->users.end(); it++)
        {
            if (it->second.email.size())
            {
                cout << "\t" << it->second.email;

                if (it->second.userhandle == client->me)
                {
                    cout << ", session user";
                }
                else if (it->second.show == VISIBLE)
                {
                    cout << ", visible";
                }
                else if (it->second.show == HIDDEN)
                {
                    cout << ", hidden";
                }
                else if (it->second.show == INACTIVE)
                {
                    cout << ", inactive";
                }
                else if (it->second.show == BLOCKED)
                {
                    cout << ", blocked";
                }
                else
                {
                    cout << ", unknown visibility (" << it->second.show << ")";
                }

                if (it->second.sharing.size())
                {
                    cout << ", sharing " << it->second.sharing.size() << " folder(s)";
                }

                if (it->second.pubk.isvalid())
                {
                    cout << ", public key cached";
                }

                cout << endl;
            }
        }
    }
    else if (s.words.size() == 3 && s.words[2].s == "del")
    {
        client->removecontact(s.words[1].s.c_str(), HIDDEN);
    }
}

void exec_mkdir(autocomplete::ACState& s)
{
    if (s.words.size() > 1)
    {
        string newname;

        if (Node* n = nodebypath(s.words[1].s.c_str(), NULL, &newname))
        {
            if (!client->checkaccess(n, RDWR))
            {
                cout << "Write access denied" << endl;

                return;
            }

            if (newname.size())
            {
                SymmCipher key;
                string attrstring;
                byte buf[FOLDERNODEKEYLENGTH];
                NewNode* newnode = new NewNode[1];

                // set up new node as folder node
                newnode->source = NEW_NODE;
                newnode->type = FOLDERNODE;
                newnode->nodehandle = 0;
                newnode->parenthandle = UNDEF;

                // generate fresh random key for this folder node
                client->rng.genblock(buf, FOLDERNODEKEYLENGTH);
                newnode->nodekey.assign((char*)buf, FOLDERNODEKEYLENGTH);
                key.setkey(buf);

                // generate fresh attribute object with the folder name
                AttrMap attrs;

                client->fsaccess->normalize(&newname);
                attrs.map['n'] = newname;

                // JSON-encode object and encrypt attribute string
                attrs.getjson(&attrstring);
                newnode->attrstring = new string;
                client->makeattr(&key, newnode->attrstring, attrstring.c_str());

                // add the newly generated folder node
                client->putnodes(n->nodehandle, newnode, 1);
            }
            else
            {
                cout << s.words[1].s << ": Path already exists" << endl;
            }
        }
        else
        {
            cout << s.words[1].s << ": Target path not found" << endl;
        }
    }
}

void exec_getfa(autocomplete::ACState& s)
{
    Node* n;
    int cancel = s.words.size() > 2 && s.words.back().s == "cancel";

    if (s.words.size() < 3)
    {
        n = client->nodebyhandle(cwd);
    }
    else if (!(n = nodebypath(s.words[2].s.c_str())))
    {
        cout << s.words[2].s << ": Path not found" << endl;
    }

    if (n)
    {
        int c = 0;
        fatype type;

        type = fatype(atoi(s.words[1].s.c_str()));

        if (n->type == FILENODE)
        {
            if (n->hasfileattribute(type))
            {
                client->getfa(n->nodehandle, &n->fileattrstring, &n->nodekey, type, cancel);
                c++;
            }
        }
        else
        {
            for (node_list::iterator it = n->children.begin(); it != n->children.end(); it++)
            {
                if ((*it)->type == FILENODE && (*it)->hasfileattribute(type))
                {
                    client->getfa((*it)->nodehandle, &(*it)->fileattrstring, &(*it)->nodekey, type, cancel);
                    c++;
                }
            }
        }

        cout << (cancel ? "Canceling " : "Fetching ") << c << " file attribute(s) of type " << type << "..." << endl;
    }
}

void exec_getua(autocomplete::ACState& s)
{
    User* u = NULL;

    if (s.words.size() == 3)
    {
        // get other user's attribute
        if (!(u = client->finduser(s.words[2].s.c_str())))
        {
            cout << "Retrieving user attribute for unknown user: " << s.words[2].s << endl;
            client->getua(s.words[2].s.c_str(), User::string2attr(s.words[1].s.c_str()));
            return;
        }
    }
    else if (s.words.size() != 2)
    {
        cout << "      getua attrname [email]" << endl;
        return;
    }

    if (!u)
    {
        // get logged in user's attribute
        if (!(u = client->ownuser()))
        {
            cout << "Must be logged in to query own attributes." << endl;
            return;
        }
    }

    client->getua(u, User::string2attr(s.words[1].s.c_str()));
}

void exec_putua(autocomplete::ACState& s)
{
    attr_t attrtype = User::string2attr(s.words[1].s.c_str());
    if (attrtype == ATTR_UNKNOWN)
    {
        cout << "Attribute not recognized" << endl;
        return;
    }

    if (s.words.size() == 2)
    {
        // delete attribute
        client->putua(attrtype);

        return;
    }
    else if (s.words.size() == 3)
    {
        if (s.words[2].s == "del")
        {
            client->putua(attrtype);

            return;
        }
    }
    else if (s.words.size() == 4)
    {
        if (s.words[2].s == "set")
        {
            client->putua(attrtype, (const byte*)s.words[3].s.c_str(), unsigned(s.words[3].s.size()));

            return;
        }
        else if (s.words[2].s == "set64")
        {
            int len = int(s.words[3].s.size() * 3 / 4 + 3);
            byte *value = new byte[len];
            int valuelen = Base64::atob(s.words[3].s.data(), value, len);
            client->putua(attrtype, value, valuelen);
            delete [] value;
            return;
        }
        else if (s.words[2].s == "load")
        {
            string data, localpath;

            client->fsaccess->path2local(&s.words[3].s, &localpath);

            if (loadfile(&localpath, &data))
            {
                client->putua(attrtype, (const byte*) data.data(), unsigned(data.size()));
            }
            else
            {
                cout << "Cannot read " << s.words[3].s << endl;
            }

            return;
        }
    }
}

#ifdef DEBUG
void exec_delua(autocomplete::ACState& s)
{
    client->delua(s.words[1].s.c_str());
}
#endif

void exec_pause(autocomplete::ACState& s)
{
    bool getarg = false, putarg = false, hardarg = false, statusarg = false;

    for (size_t i = s.words.size(); --i; )
    {
        if (s.words[i].s == "get")
        {
            getarg = true;
        }
        if (s.words[i].s == "put")
        {
            putarg = true;
        }
        if (s.words[i].s == "hard")
        {
            hardarg = true;
        }
        if (s.words[i].s == "status")
        {
            statusarg = true;
        }
    }

    if (statusarg)
    {
        if (!hardarg && !getarg && !putarg)
        {
            if (!client->xferpaused[GET] && !client->xferpaused[PUT])
            {
                cout << "Transfers not paused at the moment." << endl;
            }
            else
            {
                if (client->xferpaused[GET])
                {
                    cout << "GETs currently paused." << endl;
                }
                if (client->xferpaused[PUT])
                {
                    cout << "PUTs currently paused." << endl;
                }
            }
        }
        return;
    }

    if (!getarg && !putarg)
    {
        getarg = true;
        putarg = true;
    }

    if (getarg)
    {
        client->pausexfers(GET, client->xferpaused[GET] ^= true, hardarg);
        if (client->xferpaused[GET])
        {
            cout << "GET transfers paused. Resume using the same command." << endl;
        }
        else
        {
            cout << "GET transfers unpaused." << endl;
        }
    }

    if (putarg)
    {
        client->pausexfers(PUT, client->xferpaused[PUT] ^= true, hardarg);
        if (client->xferpaused[PUT])
        {
            cout << "PUT transfers paused. Resume using the same command." << endl;
        }
        else
        {
            cout << "PUT transfers unpaused." << endl;
        }
    }
}

void exec_debug(autocomplete::ACState& s)
{
    cout << "Debug mode " << (client->toggledebug() ? "on" : "off") << endl;
}

#if defined(WIN32) && defined(NO_READLINE)
void exec_clear(autocomplete::ACState& s)
{
    static_cast<WinConsole*>(console)->clearScreen();
}
#endif

void exec_retry(autocomplete::ACState& s)
{
    if (client->abortbackoff())
    {
        cout << "Retrying..." << endl;
    }
    else
    {
        cout << "No failed request pending." << endl;
    }
}

void exec_recon(autocomplete::ACState& s)
{
    cout << "Closing all open network connections..." << endl;

    client->disconnect();
}

void exec_email(autocomplete::ACState& s)
{
    if (s.words.size() == 1)
    {
        User *u = client->finduser(client->me);
        if (u)
        {
            cout << "Your current email address is " << u->email << endl;
        }
        else
        {
            cout << "Please, login first" << endl;
        }
    }
    else if (s.words.size() == 2)
    {
        if (s.words[1].s.find("@") != string::npos)    // get change email link
        {
            client->getemaillink(s.words[1].s.c_str());
        }
        else    // confirm change email link
        {
            string link = s.words[1].s;

            size_t pos = link.find("#verify");
            if (pos == link.npos)
            {
                cout << "Invalid email change link." << endl;
                return;
            }

            changecode.assign(link.substr(pos + strlen("#verify")));
            client->queryrecoverylink(changecode.c_str());
        }
    }
}

#ifdef ENABLE_CHAT
void exec_chatc(autocomplete::ACState& s)
{
    size_t wordscount = s.words.size();
    if (wordscount < 2 || wordscount == 3)
    {
        cout << "Invalid syntax to create chatroom" << endl;
        cout << "      chatc group [email ro|sta|mod]* " << endl;
        return;
    }

    int group = atoi(s.words[1].s.c_str());
    if (group != 0 && group != 1)
    {
        cout << "Invalid syntax to create chatroom" << endl;
        cout << "      chatc group [email ro|sta|mod]* " << endl;
        return;
    }

    unsigned parseoffset = 2;
    if (((wordscount - parseoffset) % 2) == 0)
    {
        if (!group && (wordscount - parseoffset) != 2)
        {
            cout << "Peer to peer chats must have only one peer" << endl;
            return;
        }

        userpriv_vector *userpriv = new userpriv_vector;

        unsigned numUsers = 0;
        while ((numUsers + 1) * 2 + parseoffset <= wordscount)
        {
            string email = s.words[numUsers * 2 + parseoffset].s;
            User *u = client->finduser(email.c_str(), 0);
            if (!u)
            {
                cout << "User not found: " << email << endl;
                delete userpriv;
                return;
            }

            string privstr = s.words[numUsers * 2 + parseoffset + 1].s;
            privilege_t priv;
            if (!group) // 1:1 chats enforce peer to be moderator
            {
                priv = PRIV_MODERATOR;
            }
            else
            {
                if (privstr == "ro")
                {
                    priv = PRIV_RO;
                }
                else if (privstr == "sta")
                {
                    priv = PRIV_STANDARD;
                }
                else if (privstr == "mod")
                {
                    priv = PRIV_MODERATOR;
                }
                else
                {
                    cout << "Unknown privilege for " << email << endl;
                    delete userpriv;
                    return;
                }
            }

            userpriv->push_back(userpriv_pair(u->userhandle, priv));
            numUsers++;
        }

        client->createChat(group, false, userpriv);
        delete userpriv;
    }
}

void exec_chati(autocomplete::ACState& s)
{
    if (s.words.size() >= 4 && s.words.size() <= 7)
    {
        handle chatid;
        Base64::atob(s.words[1].s.c_str(), (byte*)&chatid, MegaClient::CHATHANDLE);

        string email = s.words[2].s;
        User *u = client->finduser(email.c_str(), 0);
        if (!u)
        {
            cout << "User not found: " << email << endl;
            return;
        }

        string privstr = s.words[3].s;
        privilege_t priv;
        if (privstr == "ro")
        {
            priv = PRIV_RO;
        }
        else if (privstr == "sta")
        {
            priv = PRIV_STANDARD;
        }
        else if (privstr == "mod")
        {
            priv = PRIV_MODERATOR;
        }
        else
        {
            cout << "Unknown privilege for " << email << endl;
            return;
        }

        string title;
        string unifiedKey;
        if (s.words.size() == 5)
        {
            unifiedKey = s.words[4].s;
        }
        else if (s.words.size() >= 6 && s.words[4].s == "t")
        {
            title = s.words[5].s;
            if (s.words.size() == 7)
            {
                unifiedKey = s.words[6].s;
            }
        }
        const char *t = !title.empty() ? title.c_str() : NULL;
        const char *uk = !unifiedKey.empty() ? unifiedKey.c_str() : NULL;

        client->inviteToChat(chatid, u->userhandle, priv, uk, t);
        return;
    }
}

void exec_chatr(autocomplete::ACState& s)
{
    if (s.words.size() > 1 && s.words.size() < 4)
    {
        handle chatid;
        Base64::atob(s.words[1].s.c_str(), (byte*)&chatid, MegaClient::CHATHANDLE);

        if (s.words.size() == 2)
        {
            client->removeFromChat(chatid, client->me);
            return;
        }
        else if (s.words.size() == 3)
        {
            string email = s.words[2].s;
            User *u = client->finduser(email.c_str(), 0);
            if (!u)
            {
                cout << "User not found: " << email << endl;
                return;
            }

            client->removeFromChat(chatid, u->userhandle);
            return;
        }
    }
}

void exec_chatu(autocomplete::ACState& s)
{
    handle chatid;
    Base64::atob(s.words[1].s.c_str(), (byte*)&chatid, MegaClient::CHATHANDLE);

    client->getUrlChat(chatid);
}

void exec_chata(autocomplete::ACState& s)
{
    handle chatid;
    Base64::atob(s.words[1].s.c_str(), (byte*)&chatid, MegaClient::CHATHANDLE);
    bool archive = (s.words[2].s == "1");
    if (!archive && (s.words[2].s != "0"))
    {
        cout << "Use 1 or 0 to archive/unarchive chats" << endl;
        return;
    }

    client->archiveChat(chatid, archive);
}

void exec_chats(autocomplete::ACState& s)
{
    if (s.words.size() == 1)
    {
        textchat_map::iterator it;
        for (it = client->chats.begin(); it != client->chats.end(); it++)
        {
            DemoApp::printChatInformation(it->second);
        }
        return;
    }
    if (s.words.size() == 2)
    {
        handle chatid;
        Base64::atob(s.words[1].s.c_str(), (byte*)&chatid, MegaClient::CHATHANDLE);

        textchat_map::iterator it = client->chats.find(chatid);
        if (it == client->chats.end())
        {
            cout << "Chatid " << s.words[1].s.c_str() << " not found" << endl;
            return;
        }

        DemoApp::printChatInformation(it->second);
        return;
    }
}

void exec_chatl(autocomplete::ACState& s)
{
    handle chatid;
    Base64::atob(s.words[1].s.c_str(), (byte*) &chatid, MegaClient::CHATHANDLE);
    bool delflag = (s.words.size() == 3 && s.words[2].s == "del");
    bool createifmissing = s.words.size() == 2 || (s.words.size() == 3 && s.words[2].s != "query");

    client->chatlink(chatid, delflag, createifmissing);
}
#endif

void exec_reset(autocomplete::ACState& s)
{
    if (client->loggedin() != NOTLOGGEDIN)
    {
        cout << "You're logged in. Please, logout first." << endl;
    }
    else if (s.words.size() == 2 ||
        (s.words.size() == 3 && (hasMasterKey = (s.words[2].s == "mk"))))
    {
        recoveryemail = s.words[1].s;
        client->getrecoverylink(recoveryemail.c_str(), hasMasterKey);
    }
    else
    {
        cout << "      reset email [mk]" << endl;
    }
}

void exec_clink(autocomplete::ACState& s)
{
    bool renew = false;
    if (s.words.size() == 1 || (s.words.size() == 2 && (renew = s.words[1].s == "renew")))
    {
        client->contactlinkcreate(renew);
    }
    else if ((s.words.size() == 3) && (s.words[1].s == "query"))
    {
        handle clink = UNDEF;
        Base64::atob(s.words[2].s.c_str(), (byte*)&clink, MegaClient::CONTACTLINKHANDLE);

        client->contactlinkquery(clink);

    }
    else if (((s.words.size() == 3) || (s.words.size() == 2)) && (s.words[1].s == "del"))
    {
        handle clink = UNDEF;

        if (s.words.size() == 3)
        {
            Base64::atob(s.words[2].s.c_str(), (byte*)&clink, MegaClient::CONTACTLINKHANDLE);
        }

        client->contactlinkdelete(clink);
    }
}

void exec_apiurl(autocomplete::ACState& s)
{
    if (s.words.size() == 1)
    {
        cout << "Current APIURL = " << MegaClient::APIURL << endl;
        cout << "Current disablepkp = " << (MegaClient::disablepkp ? "true" : "false") << endl;
    }
    else if (client->loggedin() != NOTLOGGEDIN)
    {
        cout << "You must not be logged in, to change APIURL" << endl;
    }
    else if (s.words.size() == 3 || s.words.size() == 2)
    {
        if (s.words[1].s.size() < 8 || s.words[1].s.substr(0, 8) != "https://")
        {
            s.words[1].s = "https://" + s.words[1].s;
        }
        if (s.words[1].s.empty() || s.words[1].s[s.words[1].s.size() - 1] != '/')
        {
            s.words[1].s += '/';
        }
        MegaClient::APIURL = s.words[1].s;
        if (s.words.size() == 3)
        {
            MegaClient::disablepkp = s.words[2].s == "true";
        }
    }
}

void exec_passwd(autocomplete::ACState& s)
{
    if (client->loggedin() != NOTLOGGEDIN)
    {
        setprompt(NEWPASSWORD);
    }
    else
    {
        cout << "Not logged in." << endl;
    }
}

void exec_putbps(autocomplete::ACState& s)
{
    if (s.words.size() > 1)
    {
        if (s.words[1].s == "auto")
        {
            client->putmbpscap = -1;
        }
        else if (s.words[1].s == "none")
        {
            client->putmbpscap = 0;
        }
        else
        {
            int t = atoi(s.words[1].s.c_str());

            if (t > 0)
            {
                client->putmbpscap = t;
            }
            else
            {
                cout << "      putbps [limit|auto|none]" << endl;
                return;
            }
        }
    }

    cout << "Upload speed limit set to ";

    if (client->putmbpscap < 0)
    {
        cout << "AUTO (approx. 90% of your available bandwidth)" << endl;
    }
    else if (!client->putmbpscap)
    {
        cout << "NONE" << endl;
    }
    else
    {
        cout << client->putmbpscap << " byte(s)/second" << endl;
    }
}

void exec_invite(autocomplete::ACState& s)
{
    if (client->loggedin() != FULLACCOUNT)
    {
        cout << "Not logged in." << endl;
    }
    else
    {
        if (client->ownuser()->email.compare(s.words[1].s))
        {
            int delflag = s.words.size() == 3 && s.words[2].s == "del";
            int rmd = s.words.size() == 3 && s.words[2].s == "rmd";
            int clink = s.words.size() == 4 && s.words[2].s == "clink";
            if (s.words.size() == 2 || s.words.size() == 3 || s.words.size() == 4)
            {
                if (delflag || rmd)
                {
                    client->setpcr(s.words[1].s.c_str(), delflag ? OPCA_DELETE : OPCA_REMIND);
                }
                else
                {
                    handle contactLink = UNDEF;
                    if (clink)
                    {
                        Base64::atob(s.words[3].s.c_str(), (byte*)&contactLink, MegaClient::CONTACTLINKHANDLE);
                    }

                    // Original email is not required, but can be used if this account has multiple email addresses associated,
                    // to have the invite come from a specific email
                    client->setpcr(s.words[1].s.c_str(), OPCA_ADD, "Invite from MEGAcli", s.words.size() == 3 ? s.words[2].s.c_str() : NULL, contactLink);
                }
            }
            else
            {
                cout << "      invite dstemail [origemail|del|rmd|clink <link>]" << endl;
            }
        }
        else
        {
            cout << "Cannot send invitation to your own user" << endl;
        }
    }
}

void exec_signup(autocomplete::ACState& s)
{
    if (s.words.size() == 2)
    {
        const char* ptr = s.words[1].s.c_str();
        const char* tptr;

        if ((tptr = strstr(ptr, "#confirm")))
        {
            ptr = tptr + 8;
        }

        unsigned len = unsigned((s.words[1].s.size() - (ptr - s.words[1].s.c_str())) * 3 / 4 + 4);

        byte* c = new byte[len];
        len = Base64::atob(ptr, c, len);
        // we first just query the supplied signup link,
        // then collect and verify the password,
        // then confirm the account
        client->querysignuplink(c, len);
        delete[] c;
    }
    else if (s.words.size() == 3)
    {
        switch (client->loggedin())
        {
        case FULLACCOUNT:
            cout << "Already logged in." << endl;
            break;

        case CONFIRMEDACCOUNT:
            cout << "Current account already confirmed." << endl;
            break;

        case EPHEMERALACCOUNT:
            if (s.words[1].s.find('@') + 1 && s.words[1].s.find('.') + 1)
            {
                signupemail = s.words[1].s;
                signupname = s.words[2].s;

                cout << endl;
                setprompt(NEWPASSWORD);
            }
            else
            {
                cout << "Please enter a valid e-mail address." << endl;
            }
            break;

        case NOTLOGGEDIN:
            cout << "Please use the begin command to commence or resume the ephemeral session to be upgraded." << endl;
        }
    }
}

void exec_cancelsignup(autocomplete::ACState& s)
{
    client->cancelsignup();
}

void exec_whoami(autocomplete::ACState& s)
{
    if (client->loggedin() == NOTLOGGEDIN)
    {
        cout << "Not logged in." << endl;
    }
    else
    {
        User* u;

        if ((u = client->finduser(client->me)))
        {
            cout << "Account e-mail: " << u->email << " handle: " << Base64Str<MegaClient::USERHANDLE>(client->me) << endl;
#ifdef ENABLE_CHAT
            if (client->signkey)
            {
                cout << "Fingerprint: " << client->signkey->genFingerprintHex() << endl;
            }
#endif
        }

        bool storage = s.extractflag("-storage");
        bool transfer = s.extractflag("-transfer");
        bool pro = s.extractflag("-pro");
        bool transactions = s.extractflag("-transactions");
        bool purchases = s.extractflag("-purchases");
        bool sessions = s.extractflag("-sessions");

        bool all = !storage && !transfer && !pro && !transactions && !purchases && !sessions;

        cout << "Retrieving account status..." << endl;

        client->getaccountdetails(&account, all || storage, all || transfer, all || pro, all || transactions, all || purchases, all || sessions);
    }
}

void exec_export(autocomplete::ACState& s)
{
    hlink = UNDEF;
    del = ets = 0;

    Node* n;
    int deltmp = 0;
    int etstmp = 0;

    if ((n = nodebypath(s.words[1].s.c_str())))
    {
        if (s.words.size() > 2)
        {
            deltmp = (s.words[2].s == "del");
            if (!deltmp)
            {
                etstmp = atoi(s.words[2].s.c_str());
            }
        }


        cout << "Exporting..." << endl;

        error e;
        if ((e = client->exportnode(n, deltmp, etstmp)))
        {
            cout << s.words[1].s << ": Export rejected (" << errorstring(e) << ")" << endl;
        }
        else
        {
            hlink = n->nodehandle;
            ets = etstmp;
            del = deltmp;
        }
    }
    else
    {
        cout << s.words[1].s << ": Not found" << endl;
    }
}

void exec_import(autocomplete::ACState& s)
{
    if (client->openfilelink(s.words[1].s.c_str(), 1) == API_OK)
    {
        cout << "Opening link..." << endl;
    }
    else
    {
        cout << "Malformed link. Format: Exported URL or fileid#filekey" << endl;
    }
}

void exec_folderlinkinfo(autocomplete::ACState& s)
{
    publiclink = s.words[1].s;

    handle ph = UNDEF;
    byte folderkey[SymmCipher::KEYLENGTH];
    if (client->parsefolderlink(publiclink.c_str(), ph, folderkey) == API_OK)
    {
        cout << "Loading public folder link info..." << endl;
        client->getpubliclinkinfo(ph);
    }
    else
    {
        cout << "Malformed link: " << publiclink << endl;
    }
}

void exec_reload(autocomplete::ACState& s)
{
    cout << "Reloading account..." << endl;

    bool nocache = false;
    if (s.words.size() == 2 && s.words[1].s == "nocache")
    {
        nocache = true;
    }

    cwd = UNDEF;
    client->cachedscsn = UNDEF;
    client->fetchnodes(nocache);
}

void exec_logout(autocomplete::ACState& s)
{
    cout << "Logging off..." << endl;

    cwd = UNDEF;
    client->logout();

    if (clientFolder)
    {
        clientFolder->logout();
        delete clientFolder;
        clientFolder = NULL;
    }
}

#ifdef ENABLE_CHAT
void exec_chatga(autocomplete::ACState& s)
{
    handle chatid;
    Base64::atob(s.words[1].s.c_str(), (byte*) &chatid, MegaClient::CHATHANDLE);

    handle nodehandle;
    Base64::atob(s.words[2].s.c_str(), (byte*) &nodehandle, MegaClient::NODEHANDLE);

    const char *uid = s.words[3].s.c_str();

    client->grantAccessInChat(chatid, nodehandle, uid);
}

void exec_chatra(autocomplete::ACState& s)
{
    handle chatid;
    Base64::atob(s.words[1].s.c_str(), (byte*)&chatid, MegaClient::CHATHANDLE);

    handle nodehandle;
    Base64::atob(s.words[2].s.c_str(), (byte*)&nodehandle, MegaClient::NODEHANDLE);

    const char *uid = s.words[3].s.c_str();

    client->removeAccessInChat(chatid, nodehandle, uid);
}

void exec_chatst(autocomplete::ACState& s)
{
    handle chatid;
    Base64::atob(s.words[1].s.c_str(), (byte*)&chatid, MegaClient::CHATHANDLE);

    if (s.words.size() == 2)  // empty title / remove title
    {
        client->setChatTitle(chatid, "");
    }
    else if (s.words.size() == 3)
    {
        client->setChatTitle(chatid, s.words[2].s.c_str());
    }
}

void exec_chatpu(autocomplete::ACState& s)
{
    client->getChatPresenceUrl();
}

void exec_chatup(autocomplete::ACState& s)
{
    handle chatid;
    Base64::atob(s.words[1].s.c_str(), (byte*)&chatid, MegaClient::CHATHANDLE);

    handle uh;
    Base64::atob(s.words[2].s.c_str(), (byte*)&uh, MegaClient::USERHANDLE);

    string privstr = s.words[3].s;
    privilege_t priv;
    if (privstr == "ro")
    {
        priv = PRIV_RO;
    }
    else if (privstr == "sta")
    {
        priv = PRIV_STANDARD;
    }
    else if (privstr == "mod")
    {
        priv = PRIV_MODERATOR;
    }
    else
    {
        cout << "Unknown privilege for " << s.words[2].s << endl;
        return;
    }

    client->updateChatPermissions(chatid, uh, priv);
}

void exec_chatlu(autocomplete::ACState& s)
{
    handle publichandle = 0;
    Base64::atob(s.words[1].s.c_str(), (byte*)&publichandle, MegaClient::CHATLINKHANDLE);

    client->chatlinkurl(publichandle);
}

void exec_chatsm(autocomplete::ACState& s)
{
    handle chatid;
    Base64::atob(s.words[1].s.c_str(), (byte*)&chatid, MegaClient::CHATHANDLE);

    const char *title = (s.words.size() == 3) ? s.words[2].s.c_str() : NULL;
    client->chatlinkclose(chatid, title);
}

void exec_chatlj(autocomplete::ACState& s)
{
    handle publichandle = 0;
    Base64::atob(s.words[1].s.c_str(), (byte*)&publichandle, MegaClient::CHATLINKHANDLE);

    client->chatlinkjoin(publichandle, s.words[2].s.c_str());
}

void exec_chatcp(autocomplete::ACState& s)
{
    size_t wordscount = s.words.size();
    userpriv_vector *userpriv = new userpriv_vector;
    string_map *userkeymap = new string_map;
    string mownkey = s.words[1].s;
    unsigned parseoffset = 2;
    const char *title = NULL;

    if (wordscount >= 4)
    {
        if (s.words[2].s == "t")
        {
            if (s.words[3].s.empty())
            {
                cout << "Title cannot be set to empty string" << endl;
                delete userpriv;
                delete userkeymap;
                return;
            }
            title = s.words[3].s.c_str();
            parseoffset = 4;
        }

        if (((wordscount - parseoffset) % 3) != 0)
        {
            cout << "Invalid syntax to create chatroom" << endl;
            cout << "      chatcp mownkey [t title64] [email ro|sta|mod unifiedkey]* " << endl;
            delete userpriv;
            delete userkeymap;
            return;
        }

        unsigned numUsers = 0;
        while ((numUsers + 1) * 3 + parseoffset <= wordscount)
        {
            string email = s.words[numUsers * 3 + parseoffset].s;
            User *u = client->finduser(email.c_str(), 0);
            if (!u)
            {
                cout << "User not found: " << email << endl;
                delete userpriv;
                delete userkeymap;
                return;
            }

            string privstr = s.words[numUsers * 3 + parseoffset + 1].s;
            privilege_t priv;
            if (privstr == "ro")
            {
                priv = PRIV_RO;
            }
            else if (privstr == "sta")
            {
                priv = PRIV_STANDARD;
            }
            else if (privstr == "mod")
            {
                priv = PRIV_MODERATOR;
            }
            else
            {
                cout << "Unknown privilege for " << email << endl;
                delete userpriv;
                delete userkeymap;
                return;
            }
            userpriv->push_back(userpriv_pair(u->userhandle, priv));
            string unifiedkey = s.words[numUsers * 3 + parseoffset + 2].s;
            char uhB64[12];
            Base64::btoa((byte *)&u->userhandle, MegaClient::USERHANDLE, uhB64);
            uhB64[11] = '\0';
            userkeymap->insert(std::pair<string, string>(uhB64, unifiedkey));
            numUsers++;
        }
    }
    char ownHandleB64[12];
    Base64::btoa((byte *)&client->me, MegaClient::USERHANDLE, ownHandleB64);
    ownHandleB64[11] = '\0';
    userkeymap->insert(std::pair<string, string>(ownHandleB64, mownkey));
    client->createChat(true, true, userpriv, userkeymap, title);
    delete userpriv;
    delete userkeymap;
}
#endif

void exec_cancel(autocomplete::ACState& s)
{
    if (client->loggedin() != FULLACCOUNT)
    {
        cout << "Please, login into your account first." << endl;
        return;
    }

    if (s.words.size() == 1)  // get link
    {
        User *u = client->finduser(client->me);
        if (!u)
        {
            cout << "Error retrieving logged user." << endl;
            return;
        }
        client->getcancellink(u->email.c_str());
    }
    else if (s.words.size() == 2) // link confirmation
    {
        string link = s.words[1].s;

        size_t pos = link.find("#cancel");
        if (pos == link.npos)
        {
            cout << "Invalid cancellation link." << endl;
            return;
        }

        recoverycode.assign(link.substr(pos + strlen("#cancel")));
        setprompt(LOGINPASSWORD);
    }
}

void exec_alerts(autocomplete::ACState& s)
{
    bool shownew = false, showold = false;
    size_t showN = 0; 
    if (s.words.size() == 1)
    {
        shownew = showold = true;
    }
    else if (s.words.size() == 2)
    {
        if (s.words[1].s == "seen")
        {
            client->useralerts.acknowledgeAll();
            return;
        }
        else if (s.words[1].s == "notify")
        {
            notifyAlerts = !notifyAlerts;
            cout << "notification of alerts is now " << (notifyAlerts ? "on" : "off") << endl;
            return;
        }
        else if (s.words[1].s == "old")
        {
            showold = true;
        }
        else if (s.words[1].s == "new")
        {
            shownew = true;
        }
        else if (s.words[1].s == "test_reminder")
        {
            client->useralerts.add(new UserAlert::PaymentReminder(time(NULL) - 86000*3 /2, client->useralerts.nextId()));
        }
        else if (s.words[1].s == "test_payment")
        {
            client->useralerts.add(new UserAlert::Payment(true, 1, time(NULL) + 86000 * 1, client->useralerts.nextId()));
        }
        else if (atoi(s.words[1].s.c_str()) > 0)
        {
            showN = atoi(s.words[1].s.c_str());
        }
    }
    if (showold || shownew || showN > 0)
    {
        UserAlerts::Alerts::const_iterator i = client->useralerts.alerts.begin();
        if (showN)
        {
            size_t n = 0;
            for (UserAlerts::Alerts::const_reverse_iterator i = client->useralerts.alerts.rbegin(); i != client->useralerts.alerts.rend(); ++i, ++n)
            {
                showN += ((*i)->relevant || n >= showN) ? 0 : 1;
            }
        }

        size_t n = client->useralerts.alerts.size();
        for (; i != client->useralerts.alerts.end(); ++i)
        {
            if ((*i)->relevant)
            {
                if (--n < showN || (shownew && !(*i)->seen) || (showold && (*i)->seen))
                {
                    printAlert(**i);
                }
            }
        }
    }
}

#ifdef USE_FILESYSTEM
void exec_lmkdir(autocomplete::ACState& s)
{
    std::error_code ec;
    if (!fs::create_directory(s.words[1].s.c_str(), ec))
    {
        cerr << "Create directory failed: " << ec.message() << endl;
    }
}
#endif


void exec_confirm(autocomplete::ACState& s)
{
    if (signupemail.size() && signupcode.size())
    {
        cout << "Please type " << signupemail << "'s password to confirm the signup." << endl;
        setprompt(LOGINPASSWORD);
    }
}

void exec_recover(autocomplete::ACState& s)
{
    if (client->loggedin() != NOTLOGGEDIN)
    {
        cout << "You're logged in. Please, logout first." << endl;
    }
    else if (s.words.size() == 2)
    {
        string link = s.words[1].s;

        size_t pos = link.find("#recover");
        if (pos == link.npos)
        {
            cout << "Invalid recovery link." << endl;
        }

        recoverycode.assign(link.substr(pos + strlen("#recover")));
        client->queryrecoverylink(recoverycode.c_str());
    }
}

void exec_session(autocomplete::ACState& s)
{
    byte session[64];
    int size;

    size = client->dumpsession(session, sizeof session);

    if (size > 0)
    {
        Base64Str<sizeof session> buf(session, size);

        if ((s.words.size() == 2 || s.words.size() == 3) && s.words[1].s == "autoresume")
        {
            string filename = "megacli_autoresume_session" + (s.words.size() == 3 ? "_" + s.words[2].s : "");
            ofstream file(filename.c_str());
            if (file.fail() || !file.is_open())
            {
                cout << "could not open file: " << filename << endl;
            }
            else
            {
                file << buf;
                cout << "Your (secret) session is saved in file '" << filename << "'" << endl;
            }
        }
        else
        {
            cout << "Your (secret) session is: " << buf << endl;
        }
    }
    else if (!size)
    {
        cout << "Not logged in." << endl;
    }
    else
    {
        cout << "Internal error." << endl;
    }
}

void exec_symlink(autocomplete::ACState& s)
{
    if (client->followsymlinks ^= true)
    {
        cout << "Now following symlinks. Please ensure that sync does not see any filesystem item twice!" << endl;
    }
    else
    {
        cout << "No longer following symlinks." << endl;
    }
}

void exec_version(autocomplete::ACState& s)
{
    cout << "MEGA SDK version: " << MEGA_MAJOR_VERSION << "." << MEGA_MINOR_VERSION << "." << MEGA_MICRO_VERSION << endl;

    cout << "Features enabled:" << endl;

#ifdef USE_CRYPTOPP
    cout << "* CryptoPP" << endl;
#endif

#ifdef USE_SQLITE
    cout << "* SQLite" << endl;
#endif

#ifdef USE_BDB
    cout << "* Berkeley DB" << endl;
#endif

#ifdef USE_INOTIFY
    cout << "* inotify" << endl;
#endif

#ifdef HAVE_FDOPENDIR
    cout << "* fdopendir" << endl;
#endif

#ifdef HAVE_SENDFILE
    cout << "* sendfile" << endl;
#endif

#ifdef _LARGE_FILES
    cout << "* _LARGE_FILES" << endl;
#endif

#ifdef USE_FREEIMAGE
    cout << "* FreeImage" << endl;
#endif

#ifdef ENABLE_SYNC
    cout << "* sync subsystem" << endl;
#endif

#ifdef USE_MEDIAINFO
    cout << "* MediaInfo" << endl;
#endif

    cwd = UNDEF;
}

void exec_showpcr(autocomplete::ACState& s)
{
    string outgoing = "";
    string incoming = "";
    for (handlepcr_map::iterator it = client->pcrindex.begin(); it != client->pcrindex.end(); it++)
    {
        if (it->second->isoutgoing)
        {
            ostringstream os;
            os << setw(34) << it->second->targetemail;

            os << "\t(id: ";
            os << Base64Str<MegaClient::PCRHANDLE>(it->second->id);

            os << ", ts: ";

            os << it->second->ts;

            outgoing.append(os.str());
            outgoing.append(")\n");
        }
        else
        {
            ostringstream os;
            os << setw(34) << it->second->originatoremail;

            os << "\t(id: ";
            os << Base64Str<MegaClient::PCRHANDLE>(it->second->id);

            os << ", ts: ";

            os << it->second->ts;

            incoming.append(os.str());
            incoming.append(")\n");
        }
    }
    cout << "Incoming PCRs:" << endl << incoming << endl;
    cout << "Outgoing PCRs:" << endl << outgoing << endl;
}

#if defined(WIN32) && defined(NO_READLINE)
void exec_history(autocomplete::ACState& s)
{
    static_cast<WinConsole*>(console)->outputHistory();
}
#endif

void exec_handles(autocomplete::ACState& s)
{
    if (s.words.size() == 2)
    {
        if (s.words[1].s == "on")
        {
            handles_on = true;
        }
        else if (s.words[1].s == "off")
        {
            handles_on = false;
        }
        else
        {
            cout << "invalid handles setting" << endl;
        }
    }
    else
    {
        cout << "      handles on|off " << endl;
    }
}


#if defined(WIN32) && defined(NO_READLINE)
void exec_codepage(autocomplete::ACState& s)
{
    WinConsole* wc = static_cast<WinConsole*>(console);
    if (s.words.size() == 1)
    {
        UINT cp1, cp2;
        wc->getShellCodepages(cp1, cp2);
        cout << "Current codepage is " << cp1;
        if (cp2 != cp1)
        {
            cout << " with failover to codepage " << cp2 << " for any absent glyphs";
        }
        cout << endl;
        for (int i = 32; i < 256; ++i)
        {
            string theCharUtf8 = WinConsole::toUtf8String(WinConsole::toUtf16String(string(1, (char)i), cp1));
            cout << "  dec/" << i << " hex/" << hex << i << dec << ": '" << theCharUtf8 << "'";
            if (i % 4 == 3)
            {
                cout << endl;
            }
        }
    }
    else if (s.words.size() == 2 && atoi(s.words[1].s.c_str()) != 0)
    {
        if (!wc->setShellConsole(atoi(s.words[1].s.c_str()), atoi(s.words[1].s.c_str())))
        {
            cout << "Code page change failed - unicode selected" << endl;
        }
    }
    else if (s.words.size() == 3 && atoi(s.words[1].s.c_str()) != 0 && atoi(s.words[2].s.c_str()) != 0)
    {
        if (!wc->setShellConsole(atoi(s.words[1].s.c_str()), atoi(s.words[2].s.c_str())))
        {
            cout << "Code page change failed - unicode selected" << endl;
        }
    }
}
#endif

void exec_httpsonly(autocomplete::ACState& s)
{
    if (s.words.size() == 1)
    {
        cout << "httpsonly: " << (client->usehttps ? "on" : "off") << endl;
    }
    else if (s.words.size() == 2)
    {
        if (s.words[1].s == "on")
        {
            client->usehttps = true;
        }
        else if (s.words[1].s == "off")
        {
            client->usehttps = false;
        }
        else
        {
            cout << "invalid setting" << endl;
        }
    }
}

#ifdef USE_MEDIAINFO
void exec_mediainfo(autocomplete::ACState& s)
{
    if (client->mediaFileInfo.mediaCodecsFailed)
    {
        cout << "Sorry, mediainfo lookups could not be retrieved." << endl;
        return;
    }
    else if (!client->mediaFileInfo.mediaCodecsReceived)
    {
        client->mediaFileInfo.requestCodecMappingsOneTime(client, NULL);
        cout << "Mediainfo lookups requested" << endl;
    }

    if (s.words.size() == 3 && s.words[1].s == "calc")
    {
        MediaProperties mp;
        string localFilename;
        client->fsaccess->path2local(&s.words[2].s, &localFilename);

        char ext[8];
        if (client->fsaccess->getextension(&localFilename, ext, sizeof(ext)) && MediaProperties::isMediaFilenameExt(ext))
        {
            mp.extractMediaPropertyFileAttributes(localFilename, client->fsaccess);
                                uint32_t dummykey[4] = { 1, 2, 3, 4 };  // check encode/decode
                                string attrs = mp.convertMediaPropertyFileAttributes(dummykey, client->mediaFileInfo);
                                MediaProperties dmp = MediaProperties::decodeMediaPropertiesAttributes(":" + attrs, dummykey);
                                cout << showMediaInfo(dmp, client->mediaFileInfo, false) << endl;
        }
        else
        {
            cout << "Filename extension is not suitable for mediainfo analysis." << endl;
        }
    }
    else if (s.words.size() == 3 && s.words[1].s == "show")
    {
        if (Node *n = nodebypath(s.words[2].s.c_str()))
        {
            switch (n->type)
            {
            case FILENODE:
                cout << showMediaInfo(n, client->mediaFileInfo, false) << endl;
                break;

            case FOLDERNODE:
            case ROOTNODE:
            case INCOMINGNODE:
            case RUBBISHNODE:
                for (node_list::iterator m = n->children.begin(); m != n->children.end(); ++m)
                {
                    if ((*m)->type == FILENODE && (*m)->hasfileattribute(fa_media))
                    {
                        cout << (*m)->displayname() << "   " << showMediaInfo(*m, client->mediaFileInfo, true) << endl;
                    }
                }
                break;
            case TYPE_UNKNOWN: break;
            }
        }
        else
        {
            cout << "remote file not found: " << s.words[2].s << endl;
        }
    }
}
#endif

void exec_smsverify(autocomplete::ACState& s)
{
    if (s.words[1].s == "send")
    {
        bool reverifywhitelisted = (s.words.size() == 4 && s.words[3].s == "reverifywhitelisted");
        if (client->smsverificationsend(s.words[2].s, reverifywhitelisted) != API_OK)
        {
            cout << "phonenumber is invalid" << endl;
        }
    }
    else if (s.words[1].s == "code")
    {
        if (client->smsverificationcheck(s.words[2].s) != API_OK)
        {
            cout << "verificationcode is invalid" << endl;
        }
    }
}

void exec_verifiedphonenumber(autocomplete::ACState& s)
{
    cout << "Verified phone number: " << client->mSmsVerifiedPhone << endl;
}

void exec_killsession(autocomplete::ACState& s)
{
    if (s.words[1].s == "all")
    {
        // Kill all sessions (except current)
        client->killallsessions();
    }
    else
    {
        handle sessionid;
        if (Base64::atob(s.words[1].s.c_str(), (byte*)&sessionid, sizeof sessionid) == sizeof sessionid)
        {
            client->killsession(sessionid);
        }
        else
        {
            cout << "invalid session id provided" << endl;
        }
    }
}

void exec_locallogout(autocomplete::ACState& s)
{
    cout << "Logging off locally..." << endl;

    cwd = UNDEF;
    client->locallogout();
}

void exec_recentnodes(autocomplete::ACState& s)
{
    if (s.words.size() == 3)
    {
        node_vector nv = client->getRecentNodes(atoi(s.words[2].s.c_str()), m_time() - 60 * 60 * atoi(s.words[1].s.c_str()), false);
        for (unsigned i = 0; i < nv.size(); ++i)
        {
            cout << nv[i]->displaypath() << endl;
        }
    }
}

#if defined(WIN32) && defined(NO_READLINE)
void exec_autocomplete(autocomplete::ACState& s)
{
    if (s.words[1].s == "unix")
    {
        static_cast<WinConsole*>(console)->setAutocompleteStyle(true);
    }
    else if (s.words[1].s == "dos")
    {
        static_cast<WinConsole*>(console)->setAutocompleteStyle(false);
    }
    else
    {
        cout << "invalid autocomplete style" << endl;
    }
}
#endif

void exec_recentactions(autocomplete::ACState& s)
{
    recentactions_vector nvv = client->getRecentActions(atoi(s.words[2].s.c_str()), m_time() - 60 * 60 * atoi(s.words[1].s.c_str()));
    for (unsigned i = 0; i < nvv.size(); ++i)
    {
        if (i != 0)
        {
            cout << "---" << endl;
        }
        cout << displayTime(nvv[i].time) << " " << displayUser(nvv[i].user, client) << " " << (nvv[i].updated ? "updated" : "uploaded") << " " << (nvv[i].media ? "media" : "files") << endl;
        for (unsigned j = 0; j < nvv[i].nodes.size(); ++j)
        {
            cout << nvv[i].nodes[j]->displaypath() << "  (" << displayTime(nvv[i].nodes[j]->ctime) << ")" << endl;
        }
    }
}

void exec_setmaxuploadspeed(autocomplete::ACState& s)
{
    if (s.words.size() > 1)
    {
        bool done = client->setmaxuploadspeed(atoi(s.words[1].s.c_str()));
        cout << (done ? "Success. " : "Failed. ");
    }
    cout << "Max Upload Speed: " << client->getmaxuploadspeed() << endl;
}

void exec_setmaxdownloadspeed(autocomplete::ACState& s)
{
    if (s.words.size() > 1)
    {
        bool done = client->setmaxdownloadspeed(atoi(s.words[1].s.c_str()));
        cout << (done ? "Success. " : "Failed. ");
    }
    cout << "Max Download Speed: " << client->getmaxdownloadspeed() << endl;
}

void exec_enabletransferresumption(autocomplete::ACState& s)
{
    if (s.words.size() > 1 && s.words[1].s == "off")
    {
        client->disabletransferresumption(NULL);
        cout << "transfer resumption disabled" << endl;
    }
    else
    {
        client->enabletransferresumption(NULL);
        cout << "transfer resumption enabled" << endl;
    }
}

// callback for non-EAGAIN request-level errors
// in most cases, retrying is futile, so the application exits
// this can occur e.g. with syntactically malformed requests (due to a bug), an invalid application key
void DemoApp::request_error(error e)
{
    if ((e == API_ESID) || (e == API_ENOENT))   // Invalid session or Invalid folder handle
    {
        cout << "Invalid or expired session, logging out..." << endl;
        client->locallogout();
        return;
    }
    else if (e == API_EBLOCKED)
    {
        if (client->sid.size())
        {
            cout << "Your account is blocked." << endl;
            client->whyamiblocked();
        }
        else
        {
            cout << "The link has been blocked." << endl;
        }
        return;
    }

    cout << "FATAL: Request failed (" << errorstring(e) << "), exiting" << endl;

    delete console;
    exit(0);
}

void DemoApp::request_response_progress(m_off_t current, m_off_t total)
{
    if (total > 0)
    {
        responseprogress = int(current * 100 / total);
    }
    else
    {
        responseprogress = -1;
    }
}

//2FA disable result
void DemoApp::multifactorauthdisable_result(error e)
{
    if (!e)
    {
        cout << "2FA, disabled succesfully..." << endl;
    }
    else
    {
        cout << "Error enabling 2FA : " << errorstring(e) << endl;
    }
    setprompt(COMMAND);
}

//2FA check result
void DemoApp::multifactorauthcheck_result(int enabled)
{
    if (enabled)
    {
        cout << "2FA is enabled for this account" << endl;
    }
    else
    {
        cout << "2FA is disabled for this account" << endl;
    }
    setprompt(COMMAND);
}

//2FA enable result
void DemoApp::multifactorauthsetup_result(string *code, error e)
{
    if (!e)
    {
        if (!code)
        {
            cout << "2FA enabled successfully" << endl;
            setprompt(COMMAND);
            attempts = 0;
        }
        else
        {
            cout << "2FA code: " << *code << endl;
            setprompt(SETTFA);
        }
    }
    else
    {
        cout << "Error enabling 2FA : " << errorstring(e) << endl;
        if (e == API_EFAILED)
        {
            if (++attempts >= 3)
            {
                attempts = 0;
                cout << "Too many attempts"<< endl;
                setprompt(COMMAND);
            }
            else
            {
                setprompt(SETTFA);
            }
        }
    }
}


void DemoApp::prelogin_result(int version, string* /*email*/, string *salt, error e)
{
    if (e)
    {
        cout << "Login error: " << e << endl;
        setprompt(COMMAND);
        return;
    }

    login.version = version;
    login.salt = (version == 2 && salt ? *salt : string());
    
    if (login.password.empty())
    {
        setprompt(LOGINPASSWORD);
    }
    else
    {
        login.login(client);
    }
}


// login result
void DemoApp::login_result(error e)
{
    if (!e)
    {
        login.reset();
        cout << "Login successful, retrieving account..." << endl;
        client->fetchnodes();
    }
    else if (e == API_EMFAREQUIRED)
    {
        setprompt(LOGINTFA);
    }
    else
    {
        login.reset();
        cout << "Login failed: " << errorstring(e) << endl;
    }
}

// ephemeral session result
void DemoApp::ephemeral_result(error e)
{
    if (e)
    {
        cout << "Ephemeral session error (" << errorstring(e) << ")" << endl;
    }
    pdf_to_import = false;
}

// signup link send request result
void DemoApp::sendsignuplink_result(error e)
{
    if (e)
    {
        cout << "Unable to send signup link (" << errorstring(e) << ")" << endl;
    }
    else
    {
        cout << "Thank you. Please check your e-mail and enter the command signup followed by the confirmation link." << endl;
    }
}

// signup link query result
void DemoApp::querysignuplink_result(handle /*uh*/, const char* email, const char* name, const byte* pwc, const byte* /*kc*/,
                                     const byte* c, size_t len)
{
    cout << "Ready to confirm user account " << email << " (" << name << ") - enter confirm to execute." << endl;

    signupemail = email;
    signupcode.assign((char*) c, len);
    memcpy(signuppwchallenge, pwc, sizeof signuppwchallenge);
    memcpy(signupencryptedmasterkey, pwc, sizeof signupencryptedmasterkey);
}

// signup link query failed
void DemoApp::querysignuplink_result(error e)
{
    cout << "Signuplink confirmation failed (" << errorstring(e) << ")" << endl;
}

// signup link (account e-mail) confirmation result
void DemoApp::confirmsignuplink_result(error e)
{
    if (e)
    {
        cout << "Signuplink confirmation failed (" << errorstring(e) << ")" << endl;
    }
    else
    {
        cout << "Signup confirmed, logging in..." << endl;
        client->login(signupemail.c_str(), pwkey);
    }
}

// asymmetric keypair configuration result
void DemoApp::setkeypair_result(error e)
{
    if (e)
    {
        cout << "RSA keypair setup failed (" << errorstring(e) << ")" << endl;
    }
    else
    {
        cout << "RSA keypair added. Account setup complete." << endl;
    }
}

void DemoApp::getrecoverylink_result(error e)
{
    if (e)
    {
        cout << "Unable to send the link (" << errorstring(e) << ")" << endl;
    }
    else
    {
        cout << "Please check your e-mail and enter the command \"recover\" / \"cancel\" followed by the link." << endl;
    }
}

void DemoApp::queryrecoverylink_result(error e)
{
        cout << "The link is invalid (" << errorstring(e) << ")." << endl;
}

void DemoApp::queryrecoverylink_result(int type, const char *email, const char* /*ip*/, time_t /*ts*/, handle /*uh*/, const vector<string>* /*emails*/)
{
    recoveryemail = email ? email : "";
    hasMasterKey = (type == RECOVER_WITH_MASTERKEY);

    cout << "The link is valid";

    if (type == RECOVER_WITH_MASTERKEY)
    {
        cout <<  " to reset the password for " << email << " with masterkey." << endl;

        setprompt(MASTERKEY);
    }
    else if (type == RECOVER_WITHOUT_MASTERKEY)
    {
        cout <<  " to reset the password for " << email << " without masterkey." << endl;

        setprompt(NEWPASSWORD);
    }
    else if (type == CANCEL_ACCOUNT)
    {
        cout << " to cancel the account for " << email << "." << endl;
    }
    else if (type == CHANGE_EMAIL)
    {
        cout << " to change the email from " << client->finduser(client->me)->email << " to " << email << "." << endl;

        changeemail = email ? email : "";
        setprompt(LOGINPASSWORD);
    }
}

void DemoApp::getprivatekey_result(error e,  const byte *privk, const size_t len_privk)
{
    if (e)
    {
        cout << "Unable to get private key (" << errorstring(e) << ")" << endl;
        setprompt(COMMAND);
    }
    else
    {
        // check the private RSA is valid after decryption with master key
        SymmCipher key;
        key.setkey(masterkey);

        byte privkbuf[AsymmCipher::MAXKEYLENGTH * 2];
        memcpy(privkbuf, privk, len_privk);
        key.ecb_decrypt(privkbuf, len_privk);

        AsymmCipher uk;
        if (!uk.setkey(AsymmCipher::PRIVKEY, privkbuf, unsigned(len_privk)))
        {
            cout << "The master key doesn't seem to be correct." << endl;

            recoverycode.clear();
            recoveryemail.clear();
            hasMasterKey = false;
            memset(masterkey, 0, sizeof masterkey);

            setprompt(COMMAND);
        }
        else
        {
            cout << "Private key successfully retrieved for integrity check masterkey." << endl;
            setprompt(NEWPASSWORD);
        }
    }
}

void DemoApp::confirmrecoverylink_result(error e)
{
    if (e)
    {
        cout << "Unable to reset the password (" << errorstring(e) << ")" << endl;
    }
    else
    {
        cout << "Password changed successfully." << endl;
    }
}

void DemoApp::confirmcancellink_result(error e)
{
    if (e)
    {
        cout << "Unable to cancel the account (" << errorstring(e) << ")" << endl;
    }
    else
    {
        cout << "Account cancelled successfully." << endl;
    }
}

void DemoApp::validatepassword_result(error e)
{
    if (e)
    {
        cout << "Wrong password (" << errorstring(e) << ")" << endl;
        setprompt(LOGINPASSWORD);
    }
    else
    {
        if (recoverycode.size())
        {
            cout << "Password is correct, cancelling account..." << endl;

            client->confirmcancellink(recoverycode.c_str());
            recoverycode.clear();
        }
        else if (changecode.size())
        {
            cout << "Password is correct, changing email..." << endl;

            client->confirmemaillink(changecode.c_str(), changeemail.c_str(), pwkey);
            changecode.clear();
            changeemail.clear();
        }
    }
}

void DemoApp::getemaillink_result(error e)
{
    if (e)
    {
        cout << "Unable to send the link (" << errorstring(e) << ")" << endl;
    }
    else
    {
        cout << "Please check your e-mail and enter the command \"email\" followed by the link." << endl;
    }
}

void DemoApp::confirmemaillink_result(error e)
{
    if (e)
    {
        cout << "Unable to change the email address (" << errorstring(e) << ")" << endl;
    }
    else
    {
        cout << "Email address changed successfully to " << changeemail << "." << endl;
    }
}

void DemoApp::ephemeral_result(handle uh, const byte* pw)
{
    cout << "Ephemeral session established, session ID: ";
    cout << Base64Str<MegaClient::USERHANDLE>(uh) << "#";
    cout << Base64Str<SymmCipher::KEYLENGTH>(pw) << endl;

    client->fetchnodes();
}

void DemoApp::cancelsignup_result(error)
{
    cout << "Singup link canceled. Start again!" << endl;
    signupcode.clear();
    signupemail.clear();
    signupname.clear();
}

void DemoApp::whyamiblocked_result(int code)
{
    if (code < 0)
    {
        error e = (error) code;
        cout << "Why am I blocked failed: " << errorstring(e) << endl;
    }
    else if (code == 0)
    {
        cout << "You're not blocked" << endl;
    }
    else    // code > 0
    {
        string reason = "Your account was terminated due to breach of Mega's Terms of Service, such as abuse of rights of others; sharing and/or importing illegal data; or system abuse.";

        if (code == 100)    // deprecated
        {
            reason = "You have been suspended due to excess data usage.";
        }
        else if (code == 200)
        {
            reason = "Your account has been suspended due to multiple breaches of Mega's Terms of Service. Please check your email inbox.";
        }
        else if (code == 500)
        {
            reason = "Your account has been blocked, pending verification via SMS.";
        }
        //else if (code == 300) --> default reason


        cout << "Reason: " << reason << endl;

        if (code != 500)
        {
            cout << "Logging out..." << endl;
            client->locallogout();
        }
    }
}

// password change result
void DemoApp::changepw_result(error e)
{
    if (e)
    {
        cout << "Password update failed: " << errorstring(e) << endl;
    }
    else
    {
        cout << "Password updated." << endl;
    }
}

// node export failed
void DemoApp::exportnode_result(error e)
{
    if (e)
    {
        cout << "Export failed: " << errorstring(e) << endl;
    }

    del = ets = 0;
    hlink = UNDEF;
}

void DemoApp::exportnode_result(handle h, handle ph)
{
    Node* n;

    if ((n = client->nodebyhandle(h)))
    {
        string path;
        nodepath(h, &path);
        cout << "Exported " << path << ": ";

        if (n->type != FILENODE && !n->sharekey)
        {
            cout << "No key available for exported folder" << endl;

            del = ets = 0;
            hlink = UNDEF;
            return;
        }

        cout << "https://mega.co.nz/#" << (n->type ? "F" : "") << "!" << Base64Str<MegaClient::NODEHANDLE>(ph) << "!";
        if (n->type == FILENODE)
        {
            cout << Base64Str<FILENODEKEYLENGTH>((const byte*)n->nodekey.data()) << endl;
        }
        else
        {
            cout << Base64Str<FOLDERNODEKEYLENGTH>(n->sharekey->key) << endl;
        }
    }
    else
    {
        cout << "Exported node no longer available" << endl;
    }

    del = ets = 0;
    hlink = UNDEF;
}

// the requested link could not be opened
void DemoApp::openfilelink_result(error e)
{
    if (e)
    {
        if (pdf_to_import) // import welcome pdf has failed
        {
            cout << "Failed to import Welcome PDF file" << endl;
        }
        else
        {
            cout << "Failed to open link: " << errorstring(e) << endl;
        }
    }
    pdf_to_import = false;
}

// the requested link was opened successfully - import to cwd
void DemoApp::openfilelink_result(handle ph, const byte* key, m_off_t size,
                                  string* a, string* /*fa*/, int)
{
    Node* n;

    if (!key)
    {
        cout << "File is valid, but no key was provided." << endl;
        pdf_to_import = false;
        return;
    }

    // check if the file is decryptable
    string attrstring;

    attrstring.resize(a->length()*4/3+4);
    attrstring.resize(Base64::btoa((const byte *)a->data(), int(a->length()), (char *)attrstring.data()));

    SymmCipher nodeKey;
    nodeKey.setkey(key, FILENODE);

    byte *buf = Node::decryptattr(&nodeKey,attrstring.c_str(), attrstring.size());
    if (!buf)
    {
        cout << "The file won't be imported, the provided key is invalid." << endl;
        pdf_to_import = false;
    }
    else if (client->loggedin() != NOTLOGGEDIN)
    {
        if (pdf_to_import)
        {
            n = client->nodebyhandle(client->rootnodes[0]);
        }
        else
        {
            n = client->nodebyhandle(cwd);
        }

        if (!n)
        {
            cout << "Target folder not found." << endl;
            pdf_to_import = false;
            delete [] buf;
            return;
        }

        AttrMap attrs;
        JSON json;
        nameid name;
        string* t;
        json.begin((char*)buf + 5);
        NewNode* newnode = new NewNode[1];

        // set up new node as folder node
        newnode->source = NEW_PUBLIC;
        newnode->type = FILENODE;
        newnode->nodehandle = ph;
        newnode->parenthandle = UNDEF;
        newnode->nodekey.assign((char*)key, FILENODEKEYLENGTH);
        newnode->attrstring = new string(*a);

        while ((name = json.getnameid()) != EOO && json.storeobject((t = &attrs.map[name])))
        {
            JSON::unescape(t);

            if (name == 'n')
            {
                client->fsaccess->normalize(t);
            }
        }

        attr_map::iterator it = attrs.map.find('n');
        if (it != attrs.map.end())
        {
            Node *ovn = client->childnodebyname(n, it->second.c_str(), true);
            if (ovn)
            {
                attr_map::iterator it2 = attrs.map.find('c');
                if (it2 != attrs.map.end())
                {
                    FileFingerprint ffp;
                    if (ffp.unserializefingerprint(&it2->second))
                    {
                        ffp.size = size;
                        if (ffp.isvalid && ovn->isvalid && ffp == *(FileFingerprint*)ovn)
                        {
                            cout << "Success. (identical node skipped)" << endl;
                            pdf_to_import = false;
                            delete [] buf;
                            return;
                        }
                    }
                }

                newnode->ovhandle = !client->versions_disabled ? ovn->nodehandle : UNDEF;
            }
        }

        client->putnodes(n->nodehandle, newnode, 1);
    }
    else
    {
        cout << "Need to be logged in to import file links." << endl;
        pdf_to_import = false;
    }

    delete [] buf;
}

void DemoApp::folderlinkinfo_result(error e, handle owner, handle /*ph*/, string *attr, string* k, m_off_t currentSize, uint32_t numFiles, uint32_t numFolders, m_off_t versionsSize, uint32_t numVersions)
{
    if (e != API_OK)
    {
        cout << "Retrieval of public folder link information failed: " << e << endl;
        return;
    }

    handle ph;
    byte folderkey[SymmCipher::KEYLENGTH];
    error eaux = client->parsefolderlink(publiclink.c_str(), ph, folderkey);
    assert(eaux == API_OK);

    // Decrypt nodekey with the key of the folder link
    SymmCipher cipher;
    cipher.setkey(folderkey);
    const char *nodekeystr = k->data() + 9;    // skip the userhandle(8) and the `:`
    byte nodekey[FOLDERNODEKEYLENGTH];
    if (client->decryptkey(nodekeystr, nodekey, sizeof(nodekey), &cipher, 0, UNDEF))
    {
        // Decrypt node attributes with the nodekey
        cipher.setkey(nodekey);
        byte* buf = Node::decryptattr(&cipher, attr->c_str(), attr->size());
        if (buf)
        {
            AttrMap attrs;
            string fileName;
            string fingerprint;
            FileFingerprint ffp;
            m_time_t mtime = 0;
            Node::parseattr(buf, attrs, currentSize, mtime, fileName, fingerprint, ffp);

            // Normalize node name to UTF-8 string
            attr_map::iterator it = attrs.map.find('n');
            if (it != attrs.map.end() && !it->second.empty())
            {
                client->fsaccess->normalize(&(it->second));
                fileName = it->second.c_str();
            }

            std::string ownerStr, ownerBin((const char *)&owner, sizeof(owner));
            Base64::btoa(ownerBin, ownerStr);

            cout << "Folder link information:" << publiclink << endl;
            cout << "\tFolder name: " << fileName << endl;
            cout << "\tOwner: " << ownerStr << endl;
            cout << "\tNum files: " << numFiles << endl;
            cout << "\tNum folders: " << numFolders - 1 << endl;
            cout << "\tNum versions: " << numVersions << endl;

            delete [] buf;
        }
        else
        {
            cout << "folderlink: error decrypting node attributes with decrypted nodekey" << endl;
        }
    }
    else
    {
        cout << "folderlink: error decrypting nodekey with folder link key";
    }

    publiclink.clear();
}

void DemoApp::checkfile_result(handle /*h*/, error e)
{
    cout << "Link check failed: " << errorstring(e) << endl;
}

void DemoApp::checkfile_result(handle h, error e, byte* filekey, m_off_t size, m_time_t /*ts*/, m_time_t tm, string* filename,
                               string* fingerprint, string* fileattrstring)
{
    cout << "Name: " << *filename << ", size: " << size;

    if (fingerprint->size())
    {
        cout << ", fingerprint available";
    }

    if (fileattrstring->size())
    {
        cout << ", has attributes";
    }

    cout << endl;

    if (e)
    {
        cout << "Not available: " << errorstring(e) << endl;
    }
    else
    {
        cout << "Initiating download..." << endl;

        AppFileGet* f = new AppFileGet(NULL, h, filekey, size, tm, filename, fingerprint);
        f->appxfer_it = appxferq[GET].insert(appxferq[GET].end(), f);
        client->startxfer(GET, f);
    }
}

bool DemoApp::pread_data(byte* data, m_off_t len, m_off_t pos, m_off_t, m_off_t, void* /*appdata*/)
{
    if (pread_file)
    {
        pread_file->write((const char*)data, (size_t)len);
        cout << "Received " << len << " partial read byte(s) at position " << pos << endl;
        if (pread_file_end == pos + len)
        {
            delete pread_file;
            pread_file = NULL;
            cout << "Completed pread" << endl;
        }
    }
    else
    {
        cout << "Received " << len << " partial read byte(s) at position " << pos << ": ";
        fwrite(data, 1, size_t(len), stdout);
        cout << endl;
    }
    return true;
}

dstime DemoApp::pread_failure(error e, int retry, void* /*appdata*/, dstime)
{
    if (retry < 5)
    {
        cout << "Retrying read (" << errorstring(e) << ", attempt #" << retry << ")" << endl;
        return (dstime)(retry*10);
    }
    else
    {
        cout << "Too many failures (" << errorstring(e) << "), giving up" << endl;
        if (pread_file)
        {
            delete pread_file;
            pread_file = NULL;
        }
        return ~(dstime)0;
    }
}

// reload needed
void DemoApp::reload(const char* reason)
{
    cout << "Reload suggested (" << reason << ") - use 'reload' to trigger" << endl;
}

// reload initiated
void DemoApp::clearing()
{
    LOG_debug << "Clearing all nodes/users...";
}

// nodes have been modified
// (nodes with their removed flag set will be deleted immediately after returning from this call,
// at which point their pointers will become invalid at that point.)
void DemoApp::nodes_updated(Node** n, int count)
{
    int c[2][6] = { { 0 } };

    if (n)
    {
        while (count--)
        {
            if ((*n)->type < 6)
            {
                c[!(*n)->changed.removed][(*n)->type]++;
                n++;
            }
        }
    }
    else
    {
        for (node_map::iterator it = client->nodes.begin(); it != client->nodes.end(); it++)
        {
            if (it->second->type < 6)
            {
                c[1][it->second->type]++;
            }
        }
    }

    nodestats(c[1], "added or updated");
    nodestats(c[0], "removed");

    if (ISUNDEF(cwd))
    {
        cwd = client->rootnodes[0];
    }
}

// nodes now (almost) current, i.e. no server-client notifications pending
void DemoApp::nodes_current()
{
    LOG_debug << "Nodes current.";
}

void DemoApp::account_updated()
{
    if (client->loggedin() == EPHEMERALACCOUNT)
    {
        LOG_debug << "Account has been confirmed by another client. Proceed to login with credentials.";
    }
    else
    {
        LOG_debug << "Account has been upgraded/downgraded.";
    }
}

void DemoApp::notify_confirmation(const char *email)
{
    if (client->loggedin() == EPHEMERALACCOUNT)
    {
        LOG_debug << "Account has been confirmed with email " << email << ". Proceed to login with credentials.";
    }
}

void DemoApp::enumeratequotaitems_result(handle, unsigned, unsigned, unsigned, unsigned, unsigned, const char*, const char*, const char*, const char*)
{
    // FIXME: implement
}

void DemoApp::enumeratequotaitems_result(error)
{
    // FIXME: implement
}

void DemoApp::additem_result(error)
{
    // FIXME: implement
}

void DemoApp::checkout_result(const char*, error)
{
    // FIXME: implement
}

void DemoApp::getmegaachievements_result(AchievementsDetails *details, error /*e*/)
{
    // FIXME: implement display of values
    delete details;
}

void DemoApp::getwelcomepdf_result(handle ph, string *k, error e)
{
    if (e)
    {
        cout << "Failed to get Welcome PDF. Error: " << e << endl;
        pdf_to_import = false;
    }
    else
    {
        cout << "Importing Welcome PDF file. Public handle: " << LOG_NODEHANDLE(ph) << endl;
        client->reqs.add(new CommandGetPH(client, ph, (const byte *)k->data(), 1));
    }
}

#ifdef ENABLE_CHAT
void DemoApp::richlinkrequest_result(string *json, error e)
{
    if (!e)
    {
        cout << "Result:" << endl << *json << endl;
    }
    else
    {
        cout << "Failed to request rich link. Error: " << e << endl;

    }
}
#endif

void DemoApp::contactlinkcreate_result(error e, handle h)
{
    if (e)
    {
        cout << "Failed to create contact link. Error: " << e << endl;
    }
    else
    {
        cout << "Contact link created successfully: " << LOG_NODEHANDLE(h) << endl;
    }
}

void DemoApp::contactlinkquery_result(error e, handle h, string *email, string *fn, string *ln, string* /*avatar*/)
{
    if (e)
    {
        cout << "Failed to get contact link details. Error: " << e << endl;
    }
    else
    {
        cout << "Contact link created successfully: " << endl;
        cout << "\tUserhandle: " << LOG_HANDLE(h) << endl;
        cout << "\tEmail: " << *email << endl;
        cout << "\tFirstname: " << *fn << endl;
        cout << "\tLastname: " << *ln << endl;
    }
}

void DemoApp::contactlinkdelete_result(error e)
{
    if (e)
    {
        cout << "Failed to delete contact link. Error: " << e << endl;
    }
    else
    {
        cout << "Contact link deleted successfully." << endl;
    }
}

// display account details/history
void DemoApp::account_details(AccountDetails* ad, bool storage, bool transfer, bool pro, bool purchases,
                              bool transactions, bool sessions)
{
    char timebuf[32], timebuf2[32];

    if (storage)
    {
        cout << "\tAvailable storage: " << ad->storage_max << " byte(s)  used:  " << ad->storage_used << " available: " << (ad->storage_max - ad->storage_used) << endl;

        for (unsigned i = 0; i < sizeof rootnodenames/sizeof *rootnodenames; i++)
        {
            NodeStorage* ns = &ad->storage[client->rootnodes[i]];

            cout << "\t\tIn " << rootnodenames[i] << ": " << ns->bytes << " byte(s) in " << ns->files << " file(s) and " << ns->folders << " folder(s)" << endl;
            cout << "\t\tUsed storage by versions: " << ns->version_bytes << " byte(s) in " << ns->version_files << " file(s)" << endl;
        }
    }

    if (transfer)
    {
        if (ad->transfer_max)
        {
            cout << "\tTransfer in progress: " << ad->transfer_own_reserved << "/" << ad->transfer_srv_reserved << endl;
            cout << "\tTransfer completed: " << ad->transfer_own_used << "/" << ad->transfer_srv_used << " of "
                 << ad->transfer_max << " ("
                 << (100 * (ad->transfer_own_used + ad->transfer_srv_used) / ad->transfer_max) << "%)" << endl;
            cout << "\tServing bandwidth ratio: " << ad->srv_ratio << "%" << endl;
        }

        if (ad->transfer_hist_starttime)
        {
            m_time_t t = m_time() - ad->transfer_hist_starttime;

            cout << "\tTransfer history:\n";

            for (unsigned i = 0; i < ad->transfer_hist.size(); i++)
            {
                t -= ad->transfer_hist_interval;
                cout << "\t\t" << t;
                if (t < ad->transfer_hist_interval)
                {
                    cout << " second(s) ago until now: ";
                }
                else
                {
                    cout << "-" << t - ad->transfer_hist_interval << " second(s) ago: ";
                }
                cout << ad->transfer_hist[i] << " byte(s)" << endl;
            }
        }

        if (ad->transfer_limit)
        {
            cout << "Per-IP transfer limit: " << ad->transfer_limit << endl;
        }
    }

    if (pro)
    {
        cout << "\tPro level: " << ad->pro_level << endl;
        cout << "\tSubscription type: " << ad->subscription_type << endl;
        cout << "\tAccount balance:" << endl;

        for (vector<AccountBalance>::iterator it = ad->balances.begin(); it != ad->balances.end(); it++)
        {
            printf("\tBalance: %.3s %.02f\n", it->currency, it->amount);
        }
    }

    if (purchases)
    {
        cout << "Purchase history:" << endl;

        for (vector<AccountPurchase>::iterator it = ad->purchases.begin(); it != ad->purchases.end(); it++)
        {
            time_t ts = it->timestamp;
            strftime(timebuf, sizeof timebuf, "%c", localtime(&ts));
            printf("\tID: %.11s Time: %s Amount: %.3s %.02f Payment method: %d\n", it->handle, timebuf, it->currency,
                   it->amount, it->method);
        }
    }

    if (transactions)
    {
        cout << "Transaction history:" << endl;

        for (vector<AccountTransaction>::iterator it = ad->transactions.begin(); it != ad->transactions.end(); it++)
        {
            time_t ts = it->timestamp;
            strftime(timebuf, sizeof timebuf, "%c", localtime(&ts));
            printf("\tID: %.11s Time: %s Delta: %.3s %.02f\n", it->handle, timebuf, it->currency, it->delta);
        }
    }

    if (sessions)
    {
        cout << "Currently Active Sessions:" << endl;
        for (vector<AccountSession>::iterator it = ad->sessions.begin(); it != ad->sessions.end(); it++)
        {
            if (it->alive)
            {
                time_t ts = it->timestamp;
                strftime(timebuf, sizeof timebuf, "%c", localtime(&ts));
                ts = it->mru;
                strftime(timebuf2, sizeof timebuf, "%c", localtime(&ts));

                Base64Str<MegaClient::SESSIONHANDLE> id(it->id);

                if (it->current)
                {
                    printf("\t* Current Session\n");
                }
                printf("\tSession ID: %s\n\tSession start: %s\n\tMost recent activity: %s\n\tIP: %s\n\tCountry: %.2s\n\tUser-Agent: %s\n\t-----\n",
                        id.chars, timebuf, timebuf2, it->ip.c_str(), it->country, it->useragent.c_str());
            }
        }

        if(client->debugstate())
        {
            cout << endl << "Full Session history:" << endl;

            for (vector<AccountSession>::iterator it = ad->sessions.begin(); it != ad->sessions.end(); it++)
            {
                time_t ts = it->timestamp;
                strftime(timebuf, sizeof timebuf, "%c", localtime(&ts));
                ts = it->mru;
                strftime(timebuf2, sizeof timebuf, "%c", localtime(&ts));
                printf("\tSession start: %s\n\tMost recent activity: %s\n\tIP: %s\n\tCountry: %.2s\n\tUser-Agent: %s\n\t-----\n",
                        timebuf, timebuf2, it->ip.c_str(), it->country, it->useragent.c_str());
            }
        }
    }
}

// account details could not be retrieved
void DemoApp::account_details(AccountDetails* /*ad*/, error e)
{
    if (e)
    {
        cout << "Account details retrieval failed (" << errorstring(e) << ")" << endl;
    }
}

// account details could not be retrieved
void DemoApp::sessions_killed(handle sessionid, error e)
{
    if (e)
    {
        cout << "Session killing failed (" << errorstring(e) << ")" << endl;
        return;
    }

    if (sessionid == UNDEF)
    {
        cout << "All sessions except current have been killed" << endl;
    }
    else
    {
        Base64Str<MegaClient::SESSIONHANDLE> id(sessionid);
        cout << "Session with id " << id << " has been killed" << endl;
    }
}

void DemoApp::smsverificationsend_result(error e)
{
    if (e)
    {
        cout << "SMS send failed: " << e << endl;
    }
    else
    {
        cout << "SMS send succeeded" << endl;
    }
}

void DemoApp::smsverificationcheck_result(error e, string *phoneNumber)
{
    if (e)
    {
        cout << "SMS verification failed: " << e << endl;
    }
    else
    {
        cout << "SMS verification succeeded" << endl;
        if (phoneNumber)
        {
            cout << "Phone number: " << *phoneNumber << ")" << endl;
        }
    }
}

// user attribute update notification
void DemoApp::userattr_update(User* u, int priv, const char* n)
{
    cout << "Notification: User " << u->email << " -" << (priv ? " private" : "") << " attribute "
          << n << " added or updated" << endl;
}

#ifndef NO_READLINE
char* longestCommonPrefix(ac::CompletionState& acs)
{
    string s = acs.completions[0].s;
    for (size_t i = acs.completions.size(); i--; )
    {
        for (unsigned j = 0; j < s.size() && j < acs.completions[i].s.size(); ++j)
        {
            if (s[j] != acs.completions[i].s[j])
            {
                s.erase(j, string::npos);
                break;
            }
        }
    }
    return strdup(s.c_str());
}

char** my_rl_completion(const char */*text*/, int /*start*/, int end)
{
    rl_attempted_completion_over = 1;

    std::string line(rl_line_buffer, end);
    ac::CompletionState acs = ac::autoComplete(line, line.size(), autocompleteTemplate, true);

    if (acs.completions.empty())
    {
        return NULL;
    }

    if (acs.completions.size() == 1 && !acs.completions[0].couldExtend)
    {
        acs.completions[0].s += " ";
    }

    char** result = (char**)malloc((sizeof(char*)*(2+acs.completions.size())));
    for (size_t i = acs.completions.size(); i--; )
    {
        result[i+1] = strdup(acs.completions[i].s.c_str());
    }
    result[acs.completions.size()+1] = NULL;
    result[0] = longestCommonPrefix(acs);
    //for (int i = 0; i <= acs.completions.size(); ++i)
    //{
    //    cout << "i " << i << ": " << result[i] << endl;
    //}
    rl_completion_suppress_append = true;
    rl_basic_word_break_characters = " \r\n";
    rl_completer_word_break_characters = strdup(" \r\n");
    rl_completer_quote_characters = "";
    rl_special_prefixes = "";
    return result;
}
#endif

// main loop
void megacli()
{
#ifndef NO_READLINE
    char *saved_line = NULL;
    int saved_point = 0;
    rl_attempted_completion_function = my_rl_completion;

    rl_save_prompt();

#elif defined(WIN32) && defined(NO_READLINE)

    static_cast<WinConsole*>(console)->setShellConsole(CP_UTF8, GetConsoleOutputCP());

    COORD fontSize;
    string fontname = static_cast<WinConsole*>(console)->getConsoleFont(fontSize);
    cout << "Using font '" << fontname << "', " << fontSize.X << "x" << fontSize.Y
         << ". <CHAR/hex> will be used for absent characters.  If seen, try the 'codepage' command or a different font." << endl;

#else
    #error non-windows platforms must use the readline library
#endif

    for (;;)
    {
        if (prompt == COMMAND)
        {
            // display put/get transfer speed in the prompt
            if (client->tslots.size() || responseprogress >= 0)
            {
                unsigned xferrate[2] = { 0 };
                Waiter::bumpds();

                for (transferslot_list::iterator it = client->tslots.begin(); it != client->tslots.end(); it++)
                {
                    if ((*it)->fa)
                    {
                        xferrate[(*it)->transfer->type]
                            += unsigned( (*it)->progressreported * 10 / (1024 * (Waiter::ds - (*it)->starttime + 1)) );
                    }
                }

                strcpy(dynamicprompt, "MEGA");

                if (xferrate[GET] || xferrate[PUT] || responseprogress >= 0)
                {
                    strcpy(dynamicprompt + 4, " (");

                    if (xferrate[GET])
                    {
                        sprintf(dynamicprompt + 6, "In: %u KB/s", xferrate[GET]);

                        if (xferrate[PUT])
                        {
                            strcat(dynamicprompt + 9, "/");
                        }
                    }

                    if (xferrate[PUT])
                    {
                        sprintf(strchr(dynamicprompt, 0), "Out: %u KB/s", xferrate[PUT]);
                    }

                    if (responseprogress >= 0)
                    {
                        sprintf(strchr(dynamicprompt, 0), "%d%%", responseprogress);
                    }

                    strcat(dynamicprompt + 6, ")");
                }

                strcat(dynamicprompt + 4, "> ");
            }
            else
            {
                *dynamicprompt = 0;
            }

#if defined(WIN32) && defined(NO_READLINE)
            static_cast<WinConsole*>(console)->updateInputPrompt(*dynamicprompt ? dynamicprompt : prompts[COMMAND]);
#else
            rl_callback_handler_install(*dynamicprompt ? dynamicprompt : prompts[COMMAND], store_line);

            // display prompt
            if (saved_line)
            {
                rl_replace_line(saved_line, 0);
                free(saved_line);
            }

            rl_point = saved_point;
            rl_redisplay();
#endif
        }

        // command editing loop - exits when a line is submitted or the engine requires the CPU
        for (;;)
        {
            int w = client->wait();

            if (w & Waiter::HAVESTDIN)
            {
#if defined(WIN32) && defined(NO_READLINE)
                line = static_cast<WinConsole*>(console)->checkForCompletedInputLine();
#else
                if (prompt == COMMAND)
                {
                    rl_callback_read_char();
                }
                else
                {
                    console->readpwchar(pw_buf, sizeof pw_buf, &pw_buf_pos, &line);
                }
#endif
            }

            if (w & Waiter::NEEDEXEC || line)
            {
                break;
            }
        }

#ifndef NO_READLINE
        // save line
        saved_point = rl_point;
        saved_line = rl_copy_text(0, rl_end);

        // remove prompt
        rl_save_prompt();
        rl_replace_line("", 0);
        rl_redisplay();
#endif

        if (line)
        {
            // execute user command
            if (*line)
            {
                process_line(line);
            }
            free(line);
            line = NULL;

            if (quit_flag)
            {
                return;
            }

            if (!cerr)
            {
                cerr.clear();
                cerr << "Console error output failed, perhaps on a font related utf8 error or on NULL.  It is now reset." << endl;
            }
            if (!cout)
            {
                cout.clear();
                cerr << "Console output failed, perhaps on a font related utf8 error or on NULL.  It is now reset." << endl;
            }
        }

        // pass the CPU to the engine (nonblocking)
        client->exec();

        if (clientFolder)
        {
            clientFolder->exec();
        }
    }
}


class MegaCLILogger : public ::mega::Logger {
public:
    void log(const char* time, int loglevel, const char* source, const char *message) override
    {
#ifdef _WIN32
        OutputDebugStringA(message);
        OutputDebugStringA("\r\n");
#else
        if (loglevel >= SimpleLogger::logCurrentLevel)
        {
            std::cout << "[" << time << "] " << SimpleLogger::toStr(static_cast<LogLevel>(loglevel)) << ": " << message << " (" << source << ")" << std::endl;
        }
#endif
    }
};

MegaCLILogger logger;

int main()
{
#ifdef _WIN32
    SimpleLogger::setLogLevel(logMax);  // warning and stronger to console; info and weaker to VS output window
    SimpleLogger::setOutputClass(&logger);
#else
    SimpleLogger::setOutputClass(&logger);
#endif

    console = new CONSOLE_CLASS;

    // instantiate app components: the callback processor (DemoApp),
    // the HTTP I/O engine (WinHttpIO) and the MegaClient itself
    client = new MegaClient(new DemoApp, 
#ifdef WIN32        
                            new CONSOLE_WAIT_CLASS(static_cast<CONSOLE_CLASS*>(console)),
#else
                            new CONSOLE_WAIT_CLASS,
#endif
                            new HTTPIO_CLASS, new FSACCESS_CLASS,
#ifdef DBACCESS_CLASS
                            new DBACCESS_CLASS,
#else
                            NULL,
#endif
#ifdef GFX_CLASS
                            new GFX_CLASS,
#else
                            NULL,
#endif
                            "Gk8DyQBS",
                            "megacli/" TOSTRING(MEGA_MAJOR_VERSION)
                            "." TOSTRING(MEGA_MINOR_VERSION)
                            "." TOSTRING(MEGA_MICRO_VERSION));

    ac::ACN acs = autocompleteSyntax();
#if defined(WIN32) && defined(NO_READLINE)
    static_cast<WinConsole*>(console)->setAutocompleteSyntax((acs));
#endif

    clientFolder = NULL;    // additional for folder links
    megacli();
}


void DemoAppFolder::login_result(error e)
{
    if (e)
    {
        cout << "Failed to load the folder link: " << errorstring(e) << endl;
    }
    else
    {
        cout << "Folder link loaded, retrieving account..." << endl;
        clientFolder->fetchnodes();
    }
}

void DemoAppFolder::fetchnodes_result(error e)
{
    if (e)
    {
        cout << "File/folder retrieval failed (" << errorstring(e) << ")" << endl;
        pdf_to_import = false;
    }
    else
    {
        // check if we fetched a folder link and the key is invalid
        handle h = clientFolder->getrootpublicfolder();
        if (h != UNDEF)
        {
            Node *n = clientFolder->nodebyhandle(h);
            if (n && (n->attrs.map.find('n') == n->attrs.map.end()))
            {
                cout << "File/folder retrieval succeed, but encryption key is wrong." << endl;
            }
        }
        else
        {
            cout << "Failed to load folder link" << endl;

            delete clientFolder;
            clientFolder = NULL;
        }

        if (pdf_to_import)
        {
            client->getwelcomepdf();
        }
    }
}

void DemoAppFolder::nodes_updated(Node **n, int count)
{
    int c[2][6] = { { 0 } };

    if (n)
    {
        while (count--)
        {
            if ((*n)->type < 6)
            {
                c[!(*n)->changed.removed][(*n)->type]++;
                n++;
            }
        }
    }
    else
    {
        for (node_map::iterator it = clientFolder->nodes.begin(); it != clientFolder->nodes.end(); it++)
        {
            if (it->second->type < 6)
            {
                c[1][it->second->type]++;
            }
        }
    }

    cout << "The folder link contains ";
    nodestats(c[1], "");
}

