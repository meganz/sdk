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

#define USE_VARARGS
#define PREFER_STDARG
#include <readline/readline.h>
#include <readline/history.h>
#include <iomanip>

using namespace mega;

MegaClient* client;
MegaClient* clientFolder;

// login e-mail address
static string login;

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

// local console
Console* console;

// loading progress of lengthy API responses
int responseprogress = -1;

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
void DemoApp::transfer_added(Transfer* t)
{
}

// a queued transfer was removed
void DemoApp::transfer_removed(Transfer* t)
{
    displaytransferdetails(t, "removed\n");
}

void DemoApp::transfer_update(Transfer* t)
{
    // (this is handled in the prompt logic)
}

void DemoApp::transfer_failed(Transfer* t, error e)
{
    displaytransferdetails(t, "failed (");
    cout << errorstring(e) << ")" << endl;
}

void DemoApp::transfer_limit(Transfer *t)
{
    displaytransferdetails(t, "bandwidth limit reached\n");
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
                       string* cfingerprint)
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

    char hstr[sizeof(handle) * 4 / 3 + 4];
    Base64::btoa((const byte *)&chat->id, sizeof(handle), hstr);

    cout << "Chat ID: " << hstr << endl;
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
    cout << "\tPeers:";

    if (chat->userpriv)
    {
        cout << "\t\t(userhandle)\t(privilege level)" << endl;
        for (unsigned i = 0; i < chat->userpriv->size(); i++)
        {
            Base64::btoa((const byte *)&chat->userpriv->at(i).first, sizeof(handle), hstr);
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
        char *tstr = new char[chat->title.size() * 4 / 3 + 4];
        Base64::btoa((const byte *)chat->title.data(), chat->title.size(), tstr);

        cout << "\tTitle: " << tstr << endl;
        delete [] tstr;
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
                    char buf[sizeof hlink * 4 / 3 + 3];
                    Base64::btoa((byte *)&hlink, sizeof hlink, buf);

                    cout << "Node was not found. (" << buf << ")" << endl;

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
            char buffer[12];
            Base64::btoa((byte*)&h, MegaClient::PCRHANDLE, buffer);
            cout << "Outgoing pending contact request succeeded, id: " << buffer << endl;
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

void DemoApp::fa_complete(handle h, fatype type, const char* data, uint32_t len)
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

void DemoApp::getua_result(byte* data, unsigned l)
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

void DemoApp::getua_result(TLVstore *tlv)
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
            valuelen = value.length();

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


void DemoApp::notify_retry(dstime dsdelta)
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
    Node* n;
    Node* nn;

    // split path by / or :
    do {
        if (!l)
        {
            if (*ptr >= 0)
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

static void dumptree(Node* n, int recurse, int depth = 0, const char* title = NULL)
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

static void nodepath(handle h, string* path)
{
    path->clear();

    if (h == client->rootnodes[0])
    {
        *path = "/";
        return;
    }

    Node* n = client->nodebyhandle(h);

    while (n)
    {
        switch (n->type)
        {
            case FOLDERNODE:
                path->insert(0, n->displayname());

                if (n->inshare)
                {
                    path->insert(0, ":");
                    if (n->inshare->user)
                    {
                        path->insert(0, n->inshare->user->email);
                    }
                    else
                    {
                        path->insert(0, "UNKNOWN");
                    }
                    return;
                }
                break;

            case INCOMINGNODE:
                path->insert(0, "//in");
                return;

            case ROOTNODE:
                return;

            case RUBBISHNODE:
                path->insert(0, "//bin");
                return;

            case TYPE_UNKNOWN:
            case FILENODE:
                path->insert(0, n->displayname());
        }

        path->insert(0, "/");

        n = n->parent;
    }
}

appfile_list appxferq[2];

static char dynamicprompt[128];

static const char* prompts[] =
{
    "MEGA> ", "Password:", "Old Password:", "New Password:", "Retype New Password:", "Master Key (base64):"
};

enum prompttype
{
    COMMAND, LOGINPASSWORD, OLDPASSWORD, NEWPASSWORD, PASSWORDCONFIRM, MASTERKEY
};

static prompttype prompt = COMMAND;

static char pw_buf[256];
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
        cout << prompts[p] << flush;
        console->setecho(false);
    }
}

TreeProcCopy::TreeProcCopy()
{
    nn = NULL;
    nc = 0;
}

void TreeProcCopy::allocnodes()
{
    nn = new NewNode[nc];
}

TreeProcCopy::~TreeProcCopy()
{
    delete[] nn;
}

// determine node tree size (nn = NULL) or write node tree to new nodes array
void TreeProcCopy::proc(MegaClient* client, Node* n)
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
        t->parenthandle = n->parent->nodehandle;

        // copy key (if file) or generate new key (if folder)
        if (n->type == FILENODE)
        {
            t->nodekey = n->nodekey;
        }
        else
        {
            byte buf[FOLDERNODEKEYLENGTH];
            PrnGen::genblock(buf, sizeof buf);
            t->nodekey.assign((char*) buf, FOLDERNODEKEYLENGTH);
        }

        key.setkey((const byte*) t->nodekey.data(), n->type);

        n->attrs.getjson(&attrstring);
        t->attrstring = new string;
        client->makeattr(&key, t->attrstring, attrstring.c_str());
    }
    else
    {
        nc++;
    }
}

int loadfile(string* name, string* data)
{
    FileAccess* fa = client->fsaccess->newfileaccess();

    if (fa->fopen(name, 1, 0))
    {
        data->resize(fa->size);
        fa->fread(data, data->size(), 0, 0);
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

// password change-related state information
static byte pwkey[SymmCipher::KEYLENGTH];
static byte pwkeybuf[SymmCipher::KEYLENGTH];
static byte newpwkey[SymmCipher::KEYLENGTH];

// readline callback - exit if EOF, add to history unless password
static void store_line(char* l)
{
    if (!l)
    {
        delete console;
        exit(0);
    }

    if (*l && prompt == COMMAND)
    {
        add_history(l);
    }

    line = l;
}

// execute command
static void process_line(char* l)
{
    switch (prompt)
    {
        case LOGINPASSWORD:
            client->pw_key(l, pwkey);

            if (signupcode.size())
            {
                // verify correctness of supplied signup password
                SymmCipher pwcipher(pwkey);
                pwcipher.ecb_decrypt(signuppwchallenge);

                if (MemAccess::get<int64_t>((const char*)signuppwchallenge + 4))
                {
                    cout << endl << "Incorrect password, please try again." << endl;
                }
                else
                {
                    client->confirmsignuplink((const byte*) signupcode.data(), signupcode.size(),
                                              MegaClient::stringhash64(&signupemail, &pwcipher));
                }

                signupcode.clear();
            }
            else if (recoverycode.size())   // cancelling account --> check password
            {
                client->validatepwd(pwkey);
            }
            else if (changecode.size())     // changing email --> check password to avoid creating an invalid hash
            {
                client->validatepwd(pwkey);
            }
            else
            {
                client->login(login.c_str(), pwkey);
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
                    cout << endl << "Reseting password..." << endl;

                    if (hasMasterKey)
                    {
                        client->confirmrecoverylink(recoverycode.c_str(), recoveryemail.c_str(), newpwkey, masterkey);
                    }
                    else
                    {
                        client->confirmrecoverylink(recoverycode.c_str(), recoveryemail.c_str(), newpwkey, NULL);
                    }

                    recoverycode.clear();
                    recoveryemail.clear();
                    hasMasterKey = false;
                    memset(masterkey, 0, sizeof masterkey);
                }
                else
                {
                    if ((e = client->changepw(pwkey, newpwkey)) == API_OK)
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
            if (!l || !strcmp(l, "q") || !strcmp(l, "quit") || !strcmp(l, "exit"))
            {
                store_line(NULL);
            }

            vector<string> words;

            char* ptr = l;
            char* wptr;

            // split line into words with quoting and escaping
            for (;;)
            {
                // skip leading blank space
                while (*ptr > 0 && *ptr <= ' ')
                {
                    ptr++;
                }

                if (!*ptr)
                {
                    break;
                }

                // quoted arg / regular arg
                if (*ptr == '"')
                {
                    ptr++;
                    wptr = ptr;
                    words.push_back(string());

                    for (;;)
                    {
                        if (*ptr == '"' || *ptr == '\\' || !*ptr)
                        {
                            words[words.size() - 1].append(wptr, ptr - wptr);

                            if (!*ptr || *ptr++ == '"')
                            {
                                break;
                            }

                            wptr = ptr - 1;
                        }
                        else
                        {
                            ptr++;
                        }
                    }
                }
                else
                {
                    wptr = ptr;

                    while ((unsigned char) *ptr > ' ')
                    {
                        ptr++;
                    }

                    words.push_back(string(wptr, ptr - wptr));
                }
            }

            if (!words.size())
            {
                return;
            }

            Node* n;

            if (words[0] == "?" || words[0] == "h" || words[0] == "help")
            {
                cout << "      login email [password]" << endl;
                cout << "      login exportedfolderurl#key" << endl;
                cout << "      login session" << endl;
                cout << "      begin [ephemeralhandle#ephemeralpw]" << endl;
                cout << "      signup [email name|confirmationlink]" << endl;
                cout << "      confirm" << endl;
                cout << "      session" << endl;
                cout << "      mount" << endl;
                cout << "      ls [-R] [remotepath]" << endl;
                cout << "      cd [remotepath]" << endl;
                cout << "      pwd" << endl;
                cout << "      lcd [localpath]" << endl;
                cout << "      import exportedfilelink#key" << endl;
                cout << "      open exportedfolderlink#key" << endl;
                cout << "      put localpattern [dstremotepath|dstemail:]" << endl;
                cout << "      putq [cancelslot]" << endl;
                cout << "      get remotepath [offset [length]]" << endl;
                cout << "      get exportedfilelink#key [offset [length]]" << endl;
                cout << "      getq [cancelslot]" << endl;
                cout << "      pause [get|put] [hard] [status]" << endl;
                cout << "      getfa type [path] [cancel]" << endl;
                cout << "      mkdir remotepath" << endl;
                cout << "      rm remotepath" << endl;
                cout << "      mv srcremotepath dstremotepath" << endl;
                cout << "      cp srcremotepath dstremotepath|dstemail:" << endl;
#ifdef ENABLE_SYNC
                cout << "      sync [localpath dstremotepath|cancelslot]" << endl;
#endif
                cout << "      export remotepath [expireTime|del]" << endl;
                cout << "      share [remotepath [dstemail [r|rw|full] [origemail]]]" << endl;
                cout << "      invite dstemail [origemail|del|rmd]" << endl;
                cout << "      ipc handle a|d|i" << endl;
                cout << "      showpcr" << endl;
                cout << "      users [email del]" << endl;
                cout << "      getua attrname [email]" << endl;
                cout << "      putua attrname [del|set string|load file]" << endl;
#ifdef DEBUG
                cout << "      delua attrname" << endl;
#endif
                cout << "      putbps [limit|auto|none]" << endl;
                cout << "      killsession [all|sessionid]" << endl;
                cout << "      whoami" << endl;
                cout << "      passwd" << endl;
                cout << "      reset email [mk]" << endl;   // reset password w/wo masterkey
                cout << "      recover recoverylink" << endl;
                cout << "      cancel [cancellink]" << endl;
                cout << "      email [newemail|emaillink]" << endl;
                cout << "      retry" << endl;
                cout << "      recon" << endl;
                cout << "      reload [nocache]" << endl;
                cout << "      logout" << endl;
                cout << "      locallogout" << endl;
                cout << "      symlink" << endl;
                cout << "      version" << endl;
                cout << "      debug" << endl;
                cout << "      test" << endl;
#ifdef ENABLE_CHAT
                cout << "      chats [chatid]" << endl;
                cout << "      chatc group [email ro|sta|mod]*" << endl;
                cout << "      chati chatid email ro|sta|mod" << endl;
                cout << "      chatr chatid [email]" << endl;
                cout << "      chatu chatid" << endl;
                cout << "      chatup chatid userhandle ro|sta|mod" << endl;
                cout << "      chatpu" << endl;
                cout << "      chatga chatid nodehandle uid" << endl;
                cout << "      chatra chatid nodehandle uid" << endl;
                cout << "      chatst chatid title64" << endl;
#endif
                cout << "      quit" << endl;

                return;
            }

            switch (words[0].size())
            {
                case 2:
                    if (words[0] == "ls")
                    {
                        int recursive = words.size() > 1 && words[1] == "-R";

                        if ((int) words.size() > recursive + 1)
                        {
                            n = nodebypath(words[recursive + 1].c_str());
                        }
                        else
                        {
                            n = client->nodebyhandle(cwd);
                        }

                        if (n)
                        {
                            dumptree(n, recursive);
                        }

                        return;
                    }
                    else if (words[0] == "cd")
                    {
                        if (words.size() > 1)
                        {
                            if ((n = nodebypath(words[1].c_str())))
                            {
                                if (n->type == FILENODE)
                                {
                                    cout << words[1] << ": Not a directory" << endl;
                                }
                                else
                                {
                                    cwd = n->nodehandle;
                                }
                            }
                            else
                            {
                                cout << words[1] << ": No such file or directory" << endl;
                            }
                        }
                        else
                        {
                            cwd = client->rootnodes[0];
                        }

                        return;
                    }
                    else if (words[0] == "rm")
                    {
                        if (words.size() > 1)
                        {
                            if ((n = nodebypath(words[1].c_str())))
                            {
                                if (client->checkaccess(n, FULL))
                                {
                                    error e = client->unlink(n);

                                    if (e)
                                    {
                                        cout << words[1] << ": Deletion failed (" << errorstring(e) << ")" << endl;
                                    }
                                }
                                else
                                {
                                    cout << words[1] << ": Access denied" << endl;
                                }
                            }
                            else
                            {
                                cout << words[1] << ": No such file or directory" << endl;
                            }
                        }
                        else
                        {
                            cout << "      rm remotepath" << endl;
                        }

                        return;
                    }
                    else if (words[0] == "mv")
                    {
                        Node* tn;
                        string newname;

                        if (words.size() > 2)
                        {
                            // source node must exist
                            if ((n = nodebypath(words[1].c_str())))
                            {
                                // we have four situations:
                                // 1. target path does not exist - fail
                                // 2. target node exists and is folder - move
                                // 3. target node exists and is file - delete and rename (unless same)
                                // 4. target path exists, but filename does not - rename
                                if ((tn = nodebypath(words[2].c_str(), NULL, &newname)))
                                {
                                    error e;

                                    if (newname.size())
                                    {
                                        if (tn->type == FILENODE)
                                        {
                                            cout << words[2] << ": Not a directory" << endl;

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
                                    cout << words[2] << ": No such directory" << endl;
                                }
                            }
                            else
                            {
                                cout << words[1] << ": No such file or directory" << endl;
                            }
                        }
                        else
                        {
                            cout << "      mv srcremotepath dstremotepath" << endl;
                        }

                        return;
                    }
                    else if (words[0] == "cp")
                    {
                        Node* tn;
                        string targetuser;
                        string newname;
                        error e;

                        if (words.size() > 2)
                        {
                            if ((n = nodebypath(words[1].c_str())))
                            {
                                if ((tn = nodebypath(words[2].c_str(), &targetuser, &newname)))
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

                                TreeProcCopy tc;
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

                                    key.setkey((const byte*) tc.nn->nodekey.data(), tc.nn->type);

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
                                        cout << words[2] << ": No such file or directory" << endl;
                                    }
                                }
                            }
                            else
                            {
                                cout << words[1] << ": No such file or directory" << endl;
                            }
                        }
                        else
                        {
                            cout << "      cp srcremotepath dstremotepath|dstemail:" << endl;
                        }

                        return;
                    }
                    else if (words[0] == "du")
                    {
                        TreeProcDU du;

                        if (words.size() > 1)
                        {
                            if (!(n = nodebypath(words[1].c_str())))
                            {
                                cout << words[1] << ": No such file or directory" << endl;

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

                        return;
                    }
                    break;

                case 3:
                    if (words[0] == "get")
                    {
                        if (words.size() > 1)
                        {
                            if (client->openfilelink(words[1].c_str(), 0) == API_OK)
                            {
                                cout << "Checking link..." << endl;
                                return;
                            }

                            n = nodebypath(words[1].c_str());

                            if (n)
                            {
                                if (words.size() > 2)
                                {
                                    // read file slice
                                    client->pread(n, atol(words[2].c_str()), (words.size() > 3) ? atol(words[3].c_str()) : 0, NULL);
                                }
                                else
                                {
                                    AppFile* f;

                                    // queue specified file...
                                    if (n->type == FILENODE)
                                    {
                                        f = new AppFileGet(n);

                                        string::size_type index = words[1].find(":");
                                        // node from public folder link
                                        if (index != string::npos && words[1].substr(0, index).find("@") == string::npos)
                                        {
                                            handle h = clientFolder->getrootpublicfolder();
                                            char *pubauth = new char[12];
                                            Base64::btoa((byte*) &h, MegaClient::NODEHANDLE, pubauth);
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
                                cout << words[1] << ": No such file or folder" << endl;
                            }
                        }
                        else
                        {
                            cout << "      get remotepath [offset [length]]" << endl << "      get exportedfilelink#key [offset [length]]" << endl;
                        }

                        return;
                    }
                    else if (words[0] == "put")
                    {
                        if (words.size() > 1)
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

                            if (words.size() > 2)
                            {
                                if ((n = nodebypath(words[2].c_str(), &targetuser, &newname)))
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

                            client->fsaccess->path2local(&words[1], &localname);

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
                        else
                        {
                            cout << "      put localpattern [dstremotepath|dstemail:]" << endl;
                        }

                        return;
                    }
                    else if (words[0] == "pwd")
                    {
                        string path;

                        nodepath(cwd, &path);

                        cout << path << endl;

                        return;
                    }
                    else if (words[0] == "lcd")
                    {
                        if (words.size() > 1)
                        {
                            string localpath;

                            client->fsaccess->path2local(&words[1], &localpath);

                            if (!client->fsaccess->chdirlocal(&localpath))
                            {
                                cout << words[1] << ": Failed" << endl;
                            }
                        }
                        else
                        {
                            cout << "      lcd [localpath]" << endl;
                        }

                        return;
                    }
                    else if (words[0] == "ipc")
                    {
                        // incoming pending contact action
                        handle phandle;
                        if (words.size() == 3 && Base64::atob(words[1].c_str(), (byte*) &phandle, sizeof phandle) == sizeof phandle)
                        {
                            ipcactions_t action;
                            if (words[2] == "a")
                            {
                                action = IPCA_ACCEPT;
                            }
                            else if (words[2] == "d")
                            {
                                action = IPCA_DENY;
                            }
                            else if (words[2] == "i")
                            {
                                action = IPCA_IGNORE;
                            }
                            else
                            {
                                cout << "      ipc handle a|d|i" << endl;
                                return;
                            }

                            client->updatepcr(phandle, action);
                        }
                        else
                        {
                            cout << "      ipc handle a|d|i" << endl;
                        }
                        return;
                    }
                    break;

                case 4:
                    if (words[0] == "putq")
                    {
                        xferq(PUT, words.size() > 1 ? atoi(words[1].c_str()) : -1);
                        return;
                    }
                    else if (words[0] == "getq")
                    {
                        xferq(GET, words.size() > 1 ? atoi(words[1].c_str()) : -1);
                        return;
                    }
                    else if (words[0] == "open")
                    {
                        if (words.size() > 1)
                        {
                            if (strstr(words[1].c_str(), "#F!"))  // folder link indicator
                            {
                                if (!clientFolder)
                                {
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
                                                                    "SDKSAMPLE",
                                                                    "megacli_folder/" TOSTRING(MEGA_MAJOR_VERSION)
                                                                    "." TOSTRING(MEGA_MINOR_VERSION)
                                                                    "." TOSTRING(MEGA_MICRO_VERSION));
                                }
                                else
                                {
                                    clientFolder->logout();
                                }

                                return clientFolder->app->login_result(clientFolder->folderaccess(words[1].c_str()));
                            }
                            else
                            {
                                cout << "Invalid folder link." << endl;
                            }
                        }
                        else
                        {
                             cout << "      open exportedfolderlink#key" << endl;
                        }
                        return;
                    }
#ifdef ENABLE_SYNC
                    else if (words[0] == "sync")
                    {
                        if (words.size() == 3)
                        {
                            Node* n = nodebypath(words[2].c_str());

                            if (client->checkaccess(n, FULL))
                            {
                                string localname;

                                client->fsaccess->path2local(&words[1], &localname);

                                if (!n)
                                {
                                    cout << words[2] << ": Not found." << endl;
                                }
                                else if (n->type == FILENODE)
                                {
                                    cout << words[2] << ": Remote sync root must be folder." << endl;
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
                                cout << words[2] << ": Syncing requires full access to path." << endl;
                            }
                        }
                        else if (words.size() == 2)
                        {
                            int i = 0, cancel = atoi(words[1].c_str());

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
                        else if (words.size() == 1)
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
                        else
                        {
                            cout << "      sync [localpath dstremotepath|cancelslot]" << endl;
                        }

                        return;
                    }
#endif
                    else if (words[0] == "test")
                    {
                        return;
                    }
                    break;

                case 5:
                    if (words[0] == "login")
                    {
                        if (client->loggedin() == NOTLOGGEDIN)
                        {
                            if (words.size() > 1)
                            {
                                if (strchr(words[1].c_str(), '@'))
                                {
                                    // full account login
                                    if (words.size() > 2)
                                    {
                                        client->pw_key(words[2].c_str(), pwkey);
                                        client->login(words[1].c_str(), pwkey);
                                        cout << "Initiated login attempt..." << endl;
                                    }
                                    else
                                    {
                                        login = words[1];
                                        setprompt(LOGINPASSWORD);
                                    }
                                }
                                else
                                {
                                    const char* ptr;
                                    if ((ptr = strchr(words[1].c_str(), '#')))  // folder link indicator
                                    {
                                        return client->app->login_result(client->folderaccess(words[1].c_str()));
                                    }
                                    else
                                    {
                                        byte session[64];
                                        int size;

                                        if (words[1].size() < sizeof session * 4 / 3)
                                        {
                                            size = Base64::atob(words[1].c_str(), session, sizeof session);

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

                        return;
                    }
                    else if (words[0] == "begin")
                    {
                        if (words.size() == 1)
                        {
                            cout << "Creating ephemeral session..." << endl;
                            pdf_to_import = true;
                            client->createephemeral();
                        }
                        else if (words.size() == 2)
                        {
                            handle uh;
                            byte pw[SymmCipher::KEYLENGTH];

                            if (Base64::atob(words[1].c_str(), (byte*) &uh, sizeof uh) == sizeof uh && Base64::atob(
                                    words[1].c_str() + 12, pw, sizeof pw) == sizeof pw)
                            {
                                client->resumeephemeral(uh, pw);
                            }
                            else
                            {
                                cout << "Malformed ephemeral session identifier." << endl;
                            }
                        }
                        else
                        {
                            cout << "      begin [ephemeralhandle#ephemeralpw]" << endl;
                        }

                        return;
                    }
                    else if (words[0] == "mount")
                    {
                        listtrees();
                        return;
                    }
                    else if (words[0] == "share")
                    {
                        switch (words.size())
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
                                if ((n = nodebypath(words[1].c_str())))
                                {
                                    if (words.size() == 2)
                                    {
                                        listnodeshares(n);
                                    }
                                    else
                                    {
                                        accesslevel_t a = ACCESS_UNKNOWN;
                                        const char* personal_representation = NULL;
                                        if (words.size() > 3)
                                        {
                                            if (words[3] == "r" || words[3] == "ro")
                                            {
                                                a = RDONLY;
                                            }
                                            else if (words[3] == "rw")
                                            {
                                                a = RDWR;
                                            }
                                            else if (words[3] == "full")
                                            {
                                                a = FULL;
                                            }
                                            else
                                            {
                                                cout << "Access level must be one of r, rw or full" << endl;

                                                return;
                                            }

                                            if (words.size() > 4)
                                            {
                                                personal_representation = words[4].c_str();
                                            }
                                        }

                                        client->setshare(n, words[2].c_str(), a, personal_representation);
                                    }
                                }
                                else
                                {
                                    cout << words[1] << ": No such directory" << endl;
                                }

                                break;

                            default:
                                cout << "      share [remotepath [dstemail [r|rw|full] [origemail]]]" << endl;
                        }

                        return;
                    }
                    else if (words[0] == "users")
                    {
                        if (words.size() == 1)
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
                        else if (words.size() == 3 && words[2] == "del")
                        {
                            client->removecontact(words[1].c_str(), HIDDEN);
                        }
                        else
                        {
                            cout << "      users [email del]" << endl;
                        }

                        return;
                    }
                    else if (words[0] == "mkdir")
                    {
                        if (words.size() > 1)
                        {
                            string newname;

                            if ((n = nodebypath(words[1].c_str(), NULL, &newname)))
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
                                    PrnGen::genblock(buf, FOLDERNODEKEYLENGTH);
                                    newnode->nodekey.assign((char*) buf, FOLDERNODEKEYLENGTH);
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
                                    cout << words[1] << ": Path already exists" << endl;
                                }
                            }
                            else
                            {
                                cout << words[1] << ": Target path not found" << endl;
                            }
                        }
                        else
                        {
                            cout << "      mkdir remotepath" << endl;
                        }

                        return;
                    }
                    else if (words[0] == "getfa")
                    {
                        if (words.size() > 1)
                        {
                            Node* n;
                            int cancel = words.size() > 2 && words[words.size() - 1] == "cancel";

                            if (words.size() < 3)
                            {
                                n = client->nodebyhandle(cwd);
                            }
                            else if (!(n = nodebypath(words[2].c_str())))
                            {
                                cout << words[2] << ": Path not found" << endl;
                            }

                            if (n)
                            {
                                int c = 0;
                                fatype type;

                                type = atoi(words[1].c_str());

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
                        else
                        {
                            cout << "      getfa type [path] [cancel]" << endl;
                        }

                        return;
                    }
                    else if (words[0] == "getua")
                    {
                        User* u = NULL;

                        if (words.size() == 3)
                        {
                            // get other user's attribute
                            if (!(u = client->finduser(words[2].c_str())))
                            {
                                cout << "Retrieving user attribute for unknown user: " << words[2] << endl;
                                client->getua(words[2].c_str(), User::string2attr(words[1].c_str()));
                                return;
                            }
                        }
                        else if (words.size() != 2)
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

                        client->getua(u, User::string2attr(words[1].c_str()));

                        return;
                    }
                    else if (words[0] == "putua")
                    {
                        if (words.size() >= 2)
                        {
                            attr_t attrtype = User::string2attr(words[1].c_str());
                            if (attrtype == ATTR_UNKNOWN)
                            {
                                cout << "Attribute not recognized" << endl;
                                return;
                            }

                            if (words.size() == 2)
                            {
                                // delete attribute
                                client->putua(attrtype);

                                return;
                            }
                            else if (words.size() == 3)
                            {
                                if (words[2] == "del")
                                {
                                    client->putua(attrtype);

                                    return;
                                }
                            }
                            else if (words.size() == 4)
                            {
                                if (words[2] == "set")
                                {
                                    client->putua(attrtype, (const byte*) words[3].c_str(), words[3].size());

                                    return;
                                }
                                else if (words[2] == "set64")
                                {
                                    int len = words[3].size() * 3 / 4 + 3;
                                    byte *value = new byte[len];
                                    int valuelen = Base64::atob(words[3].data(), value, len);
                                    client->putua(attrtype, value, valuelen);
                                    delete [] value;
                                    return;
                                }
                                else if (words[2] == "load")
                                {
                                    string data, localpath;

                                    client->fsaccess->path2local(&words[3], &localpath);

                                    if (loadfile(&localpath, &data))
                                    {
                                        client->putua(attrtype, (const byte*) data.data(), data.size());
                                    }
                                    else
                                    {
                                        cout << "Cannot read " << words[3] << endl;
                                    }

                                    return;
                                }
                            }
                        }

                        cout << "      putua attrname [del|set string|load file]" << endl;

                        return;
                    }
#ifdef DEBUG
                    else if (words[0] == "delua")
                    {
                        if (words.size() == 2)
                        {
                            client->delua(words[1].c_str());
                            return;
                        }

                        cout << "      delua attrname" << endl;

                        return;
                    }
#endif
                    else if (words[0] == "pause")
                    {
                        bool getarg = false, putarg = false, hardarg = false, statusarg = false;

                        for (int i = words.size(); --i; )
                        {
                            if (words[i] == "get")
                            {
                                getarg = true;
                            }
                            if (words[i] == "put")
                            {
                                putarg = true;
                            }
                            if (words[i] == "hard")
                            {
                                hardarg = true;
                            }
                            if (words[i] == "status")
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
                            else
                            {
                                cout << "      pause [get|put] [hard] [status]" << endl;
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

                        return;
                    }
                    else if (words[0] == "debug")
                    {
                        cout << "Debug mode " << (client->toggledebug() ? "on" : "off") << endl;

                        return;
                    }
                    else if (words[0] == "retry")
                    {
                        if (client->abortbackoff())
                        {
                            cout << "Retrying..." << endl;
                        }
                        else
                        {
                            cout << "No failed request pending." << endl;
                        }

                        return;
                    }
                    else if (words[0] == "recon")
                    {
                        cout << "Closing all open network connections..." << endl;

                        client->disconnect();

                        return;
                    }
                    else if (words[0] == "email")
                    {
                        if (words.size() == 1)
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
                        else if (words.size() == 2)
                        {
                            if (words[1].find("@") != words[1].npos)    // get change email link
                            {
                                client->getemaillink(words[1].c_str());
                            }
                            else    // confirm change email link
                            {
                                string link = words[1];

                                size_t pos = link.find("#verify");
                                if (pos == link.npos)
                                {
                                    cout << "Invalid email change link." << endl;
                                    return;
                                }

                                changecode.assign(link.substr(pos+strlen("#verify")));
                                client->queryrecoverylink(changecode.c_str());
                            }
                        }
                        else
                        {
                            cout << "      email [newemail|emaillink]" << endl;
                        }

                        return;
                    }
#ifdef ENABLE_CHAT
                    else if (words[0] == "chatc")
                    {
                        unsigned wordscount = words.size();
                        if (wordscount > 1 && ((wordscount - 2) % 2) == 0)
                        {
                            int group = atoi(words[1].c_str());
                            if (!group && (wordscount - 2) != 2)
                            {
                                cout << "Only group chats can have more than one peer" << endl;
                                return;
                            }

                            userpriv_vector *userpriv = new userpriv_vector;

                            unsigned numUsers = 0;
                            while ((numUsers+1)*2 + 2 <= wordscount)
                            {
                                string email = words[numUsers*2 + 2];
                                User *u = client->finduser(email.c_str(), 0);
                                if (!u)
                                {
                                    cout << "User not found: " << email << endl;
                                    delete userpriv;
                                    return;
                                }

                                string privstr = words[numUsers*2 + 2 + 1];
                                privilege_t priv;
                                if (!group) // 1:1 chats enforce peer to be moderator
                                {
                                    priv = PRIV_MODERATOR;
                                }
                                else
                                {
                                    if (privstr ==  "ro")
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

                            client->createChat(group, userpriv);
                            delete userpriv;
                            return;
                        }
                        else
                        {
                            cout << "Invalid syntax to create chatroom" << endl;
                            cout << "       chatc group [email ro|sta|mod]*" << endl;
                            return;
                        }
                    }
                    else if (words[0] == "chati")
                    {
                        if (words.size() == 4)
                        {
                            handle chatid;
                            Base64::atob(words[1].c_str(), (byte*) &chatid, sizeof chatid);

                            string email = words[2];
                            User *u = client->finduser(email.c_str(), 0);
                            if (!u)
                            {
                                cout << "User not found: " << email << endl;
                                return;
                            }

                            string privstr = words[3];
                            privilege_t priv;
                            if (privstr ==  "ro")
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

                            client->inviteToChat(chatid, u->userhandle, priv);
                            return;
                        }
                        else
                        {
                            cout << "Invalid syntax to invite new peer" << endl;
                            cout << "       chati chatid email ro|sta|mod" << endl;
                            return;

                        }
                    }
                    else if (words[0] == "chatr")
                    {
                        if (words.size() > 1 && words.size() < 4)
                        {
                            handle chatid;
                            Base64::atob(words[1].c_str(), (byte*) &chatid, sizeof chatid);

                            if (words.size() == 2)
                            {
                                client->removeFromChat(chatid, client->me);
                                return;
                            }
                            else if (words.size() == 3)
                            {
                                string email = words[2];
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
                        else
                        {
                            cout << "Invalid syntax to leave chat / remove peer" << endl;
                            cout << "       chatr chatid [email]" << endl;
                            return;
                        }

                    }
                    else if (words[0] == "chatu")
                    {
                        if (words.size() == 2)
                        {
                            handle chatid;
                            Base64::atob(words[1].c_str(), (byte*) &chatid, sizeof chatid);

                            client->getUrlChat(chatid);
                            return;
                        }
                        else
                        {
                            cout << "Invalid syntax to get chatd URL" << endl;
                            cout << "      chatu chatid" << endl;
                            return;
                        }
                    }
                    else if (words[0] == "chats")
                    {
                        if (words.size() == 1)
                        {
                            textchat_map::iterator it;
                            for (it = client->chats.begin(); it != client->chats.end(); it++)
                            {
                                DemoApp::printChatInformation(it->second);
                            }
                            return;
                        }
                        if (words.size() == 2)
                        {
                            handle chatid;
                            Base64::atob(words[1].c_str(), (byte*) &chatid, sizeof chatid);

                            textchat_map::iterator it = client->chats.find(chatid);
                            if (it == client->chats.end())
                            {
                                cout << "Chatid " << words[1].c_str() << " not found" << endl;
                                return;
                            }

                            DemoApp::printChatInformation(it->second);
                            return;
                        }
                        else
                        {
                            cout << "Invalid syntax to list chatrooms" << endl;
                            cout << "      chats" << endl;
                            return;
                        }
                    }
#endif
                    else if (words[0] == "reset")
                    {
                        if (client->loggedin() != NOTLOGGEDIN)
                        {
                            cout << "You're logged in. Please, logout first." << endl;
                        }
                        else if (words.size() == 2 ||
                            (words.size() == 3 && (hasMasterKey = (words[2] == "mk"))))
                        {
                            recoveryemail = words[1];
                            client->getrecoverylink(recoveryemail.c_str(), hasMasterKey);
                        }
                        else
                        {
                            cout << "      reset email [mk]" << endl;
                        }
                        return;
                    }

                    break;

                case 6:
                    if (words[0] == "passwd")
                    {
                        if (client->loggedin() != NOTLOGGEDIN)
                        {
                            setprompt(OLDPASSWORD);
                        }
                        else
                        {
                            cout << "Not logged in." << endl;
                        }

                        return;
                    }
                    else if (words[0] == "putbps")
                    {
                        if (words.size() > 1)
                        {
                            if (words[1] == "auto")
                            {
                                client->putmbpscap = -1;
                            }
                            else if (words[1] == "none")
                            {
                                client->putmbpscap = 0;
                            }
                            else
                            {
                                int t = atoi(words[1].c_str());

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

                        return;
                    }
                    else if (words[0] == "invite")
                    {
                        if (client->loggedin() != FULLACCOUNT)
                        {
                            cout << "Not logged in." << endl;
                        }
                        else
                        {
                            if (client->ownuser()->email.compare(words[1]))
                            {
                                int del = words.size() == 3 && words[2] == "del";
                                int rmd = words.size() == 3 && words[2] == "rmd";
                                if (words.size() == 2 || words.size() == 3)
                                {
                                    if (del || rmd)
                                    {
                                        client->setpcr(words[1].c_str(), del ? OPCA_DELETE : OPCA_REMIND);
                                    }
                                    else
                                    {
                                        // Original email is not required, but can be used if this account has multiple email addresses associated,
                                        // to have the invite come from a specific email
                                        client->setpcr(words[1].c_str(), OPCA_ADD, "Invite from MEGAcli", words.size() == 3 ? words[2].c_str() : NULL);
                                    }
                                }
                                else
                                {
                                    cout << "      invite dstemail [origemail|del|rmd]" << endl;
                                }
                            }
                            else
                            {
                                cout << "Cannot send invitation to your own user" << endl;
                            }
                        }

                        return;
                    }
                    else if (words[0] == "signup")
                    {
                        if (words.size() == 2)
                        {
                            const char* ptr = words[1].c_str();
                            const char* tptr;

                            if ((tptr = strstr(ptr, "#confirm")))
                            {
                                ptr = tptr + 8;
                            }

                            unsigned len = (words[1].size() - (ptr - words[1].c_str())) * 3 / 4 + 4;

                            byte* c = new byte[len];
                            len = Base64::atob(ptr, c, len);
                            // we first just query the supplied signup link,
                            // then collect and verify the password,
                            // then confirm the account
                            client->querysignuplink(c, len);
                            delete[] c;
                        }
                        else if (words.size() == 3)
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
                                    if (words[1].find('@') + 1 && words[1].find('.') + 1)
                                    {
                                        signupemail = words[1];
                                        signupname = words[2];

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

                        return;
                    }
                    else if (words[0] == "whoami")
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
                                cout << "Account e-mail: " << u->email << endl;
#ifdef ENABLE_CHAT
                                if (client->signkey)
                                {
                                    cout << "Fingerprint: " << client->signkey->genFingerprintHex() << endl;
                                }
#endif
                            }

                            cout << "Retrieving account status..." << endl;

                            client->getaccountdetails(&account, true, true, true, true, true, true);
                        }

                        return;
                    }
                    else if (words[0] == "export")
                    {
                        if (words.size() > 1)
                        {
                            hlink = UNDEF;
                            del = ets = 0;

                            Node* n;
                            int deltmp = 0;
                            int etstmp = 0;

                            if ((n = nodebypath(words[1].c_str())))
                            {
                                if (words.size() > 2)
                                {
                                    deltmp = (words[2] == "del");
                                    if (!deltmp)
                                    {
                                        etstmp = atol(words[2].c_str());
                                    }
                                }


                                cout << "Exporting..." << endl;

                                error e;
                                if ((e = client->exportnode(n, deltmp, etstmp)))
                                {
                                    cout << words[1] << ": Export rejected (" << errorstring(e) << ")" << endl;
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
                                cout << words[1] << ": Not found" << endl;
                            }
                        }
                        else
                        {
                            cout << "      export remotepath [expireTime|del]" << endl;
                        }

                        return;
                    }
                    else if (words[0] == "import")
                    {
                        if (words.size() > 1)
                        {
                            if (client->openfilelink(words[1].c_str(), 1) == API_OK)
                            {
                                cout << "Opening link..." << endl;
                            }
                            else
                            {
                                cout << "Malformed link. Format: Exported URL or fileid#filekey" << endl;
                            }
                        }
                        else
                        {
                            cout << "      import exportedfilelink#key" << endl;
                        }

                        return;
                    }
                    else if (words[0] == "reload")
                    {
                        cout << "Reloading account..." << endl;

                        bool nocache = false;
                        if (words.size() == 2 && words[1] == "nocache")
                        {
                            nocache = true;
                        }

                        cwd = UNDEF;
                        client->cachedscsn = UNDEF;
                        client->fetchnodes(nocache);

                        return;
                    }
                    else if (words[0] == "logout")
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

                        return;
                    }
#ifdef ENABLE_CHAT
                    else if (words[0] == "chatga")
                    {
                        if (words.size() == 4)
                        {
                            handle chatid;
                            Base64::atob(words[1].c_str(), (byte*) &chatid, sizeof chatid);

                            handle nodehandle;
                            Base64::atob(words[2].c_str(), (byte*) &nodehandle, sizeof nodehandle);

                            const char *uid = words[3].c_str();

                            client->grantAccessInChat(chatid, nodehandle, uid);
                            return;
                        }
                        else
                        {
                            cout << "Invalid syntax to grant access to a user/node" << endl;
                            cout << "       chatga chatid nodehandle uid" << endl;
                            return;
                        }

                    }
                    else if (words[0] == "chatra")
                    {
                        if (words.size() == 4)
                        {
                            handle chatid;
                            Base64::atob(words[1].c_str(), (byte*) &chatid, sizeof chatid);

                            handle nodehandle;
                            Base64::atob(words[2].c_str(), (byte*) &nodehandle, sizeof nodehandle);

                            const char *uid = words[3].c_str();

                            client->removeAccessInChat(chatid, nodehandle, uid);
                            return;
                        }
                        else
                        {
                            cout << "Invalid syntax to revoke access to a user/node" << endl;
                            cout << "       chatra chatid nodehandle uid" << endl;
                            return;
                        }
                    }
                    else if (words[0] == "chatst")
                    {
                        if (words.size() == 2 || words.size() == 3)
                        {
                            handle chatid;
                            Base64::atob(words[1].c_str(), (byte*) &chatid, sizeof chatid);

                            if (words.size() == 2)  // empty title / remove title
                            {
                                client->setChatTitle(chatid, "");
                            }
                            else if (words.size() == 3)
                            {
                                client->setChatTitle(chatid, words[2].c_str());
                            }
                            return;
                        }
                        else
                        {
                            cout << "Invalid syntax to set chat title" << endl;
                            cout << "       chatst chatid title64" << endl;
                            return;
                        }
                    }
                    else if (words[0] == "chatpu")
                    {
                        if (words.size() == 1)
                        {
                            client->getChatPresenceUrl();
                            return;
                        }
                        else
                        {
                            cout << "Invalid syntax to get presence URL" << endl;
                            cout << "       chatpu" << endl;
                            return;
                        }
                    }
                    else if (words[0] == "chatup")
                    {
                        if (words.size() == 4)
                        {
                            handle chatid;
                            Base64::atob(words[1].c_str(), (byte*) &chatid, sizeof chatid);

                            handle uh;
                            Base64::atob(words[2].c_str(), (byte*) &uh, sizeof uh);

                            string privstr = words[3];
                            privilege_t priv;
                            if (privstr ==  "ro")
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
                                cout << "Unknown privilege for " << words[2] << endl;
                                return;
                            }

                            client->updateChatPermissions(chatid, uh, priv);
                            return;
                        }
                        else
                        {
                            cout << "Invalid syntax to update privileges" << endl;
                            cout << "       chatpu chatid userhandle ro|sta|mod" << endl;
                            return;

                        }
                    }
#endif
                    else if (words[0] == "cancel")
                    {
                        if (client->loggedin() != FULLACCOUNT)
                        {
                            cout << "Please, login into your account first." << endl;
                            return;
                        }

                        if (words.size() == 1)  // get link
                        {
                            User *u = client->finduser(client->me);
                            if (!u)
                            {
                                cout << "Error retrieving logged user." << endl;
                                return;
                            }
                            client->getcancellink(u->email.c_str());
                        }
                        else if (words.size() == 2) // link confirmation
                        {
                            string link = words[1];

                            size_t pos = link.find("#cancel");
                            if (pos == link.npos)
                            {
                                cout << "Invalid cancellation link." << endl;
                                return;
                            }

                            recoverycode.assign(link.substr(pos+strlen("#cancel")));
                            setprompt(LOGINPASSWORD);
                        }
                        else
                        {
                            cout << "       cancel [link]" << endl;
                        }
                        return;
                    }
                    break;


                case 7:
                    if (words[0] == "confirm")
                    {
                        if (signupemail.size() && signupcode.size())
                        {
                            cout << "Please type " << signupemail << "'s password to confirm the signup." << endl;
                            setprompt(LOGINPASSWORD);
                        }
                        else
                        {
                            cout << "No signup confirmation pending." << endl;
                        }

                        return;
                    }
                    else if (words[0] == "recover")
                    {
                        if (client->loggedin() != NOTLOGGEDIN)
                        {
                            cout << "You're logged in. Please, logout first." << endl;
                        }
                        else if (words.size() == 2)
                        {
                            string link = words[1];

                            size_t pos = link.find("#recover");
                            if (pos == link.npos)
                            {
                                cout << "Invalid recovery link." << endl;
                            }

                            recoverycode.assign(link.substr(pos+strlen("#recover")));
                            client->queryrecoverylink(recoverycode.c_str());
                        }
                        else
                        {
                            cout << "      recover recoverylink" << endl;
                        }
                        return;
                    }
                    else if (words[0] == "session")
                    {
                        byte session[64];
                        int size;

                        size = client->dumpsession(session, sizeof session);

                        if (size > 0)
                        {
                            char buf[sizeof session * 4 / 3 + 3];

                            Base64::btoa(session, size, buf);

                            cout << "Your (secret) session is: " << buf << endl;
                        }
                        else if (!size)
                        {
                            cout << "Not logged in." << endl;
                        }
                        else
                        {
                            cout << "Internal error." << endl;
                        }

                        return;
                    }
                    else if (words[0] == "symlink")
                    {
                        if (client->followsymlinks ^= true)
                        {
                            cout << "Now following symlinks. Please ensure that sync does not see any filesystem item twice!" << endl;
                        }
                        else
                        {
                            cout << "No longer following symlinks." << endl;
                        }

                        return;
                    }
                    else if (words[0] == "version")
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


                        cwd = UNDEF;

                        return;
                    } 
                    else if (words[0] == "showpcr")
                    {
                        string outgoing = "";
                        string incoming = "";
                        for (handlepcr_map::iterator it = client->pcrindex.begin(); it != client->pcrindex.end(); it++)
                        {
                            if (it->second->isoutgoing)
                            {
                                ostringstream os;
                                os << setw(34) << it->second->targetemail;

                                char buffer[12];
                                Base64::btoa((byte*)&(it->second->id), MegaClient::PCRHANDLE, buffer);
                                os << "\t(id: ";
                                os << buffer;
                                
                                os << ", ts: ";
                                
                                os << it->second->ts;                                

                                outgoing.append(os.str());
                                outgoing.append(")\n");
                            }
                            else
                            {
                                ostringstream os;
                                os << setw(34) << it->second->originatoremail;

                                char buffer[12];
                                Base64::btoa((byte*)&(it->second->id), MegaClient::PCRHANDLE, buffer);
                                os << "\t(id: ";
                                os << buffer;
                                
                                os << ", ts: ";
                                
                                os << it->second->ts;                                

                                incoming.append(os.str());
                                incoming.append(")\n");
                            }
                        }
                        cout << "Incoming PCRs:" << endl << incoming << endl;
                        cout << "Outgoing PCRs:" << endl << outgoing << endl;
                        return;
                    }
                    break;

                case 11:                    
                    if (words[0] == "killsession")
                    {
                        if (words.size() == 2)
                        {
                            if (words[1] == "all")
                            {
                                // Kill all sessions (except current)
                                client->killallsessions();
                            }
                            else
                            {
                                handle sessionid;
                                if (Base64::atob(words[1].c_str(), (byte*) &sessionid, sizeof sessionid) == sizeof sessionid)
                                {                                    
                                    client->killsession(sessionid);
                                }
                                else
                                {
                                    cout << "invalid session id provided" << endl;
                                }                         
                            }
                        }
                        else
                        {
                            cout << "      killsession [all|sessionid] " << endl;
                        }
                        return;
                    }
                    else if (words[0] == "locallogout")
                    {
                        cout << "Logging off locally..." << endl;

                        cwd = UNDEF;
                        client->locallogout();

                        return;
                    }
                    break;
            }

            cout << "?Invalid command" << endl;
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

    cout << "FATAL: Request failed (" << errorstring(e) << "), exiting" << endl;

    delete console;
    exit(0);
}

void DemoApp::request_response_progress(m_off_t current, m_off_t total)
{
    if (total > 0)
    {
        responseprogress = current * 100 / total;
    }
    else
    {
        responseprogress = -1;
    }
}

// login result
void DemoApp::login_result(error e)
{
    if (e)
    {
        cout << "Login failed: " << errorstring(e) << endl;
    }
    else
    {
        cout << "Login successful, retrieving account..." << endl;
        client->fetchnodes();
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
void DemoApp::querysignuplink_result(handle uh, const char* email, const char* name, const byte* pwc, const byte* kc,
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

void DemoApp::queryrecoverylink_result(int type, const char *email, const char *ip, time_t ts, handle uh, const vector<string> *emails)
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
        if (!uk.setkey(AsymmCipher::PRIVKEY, privkbuf, len_privk))
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
    char buf[SymmCipher::KEYLENGTH * 4 / 3 + 3];

    cout << "Ephemeral session established, session ID: ";
    Base64::btoa((byte*) &uh, sizeof uh, buf);
    cout << buf << "#";
    Base64::btoa(pw, SymmCipher::KEYLENGTH, buf);
    cout << buf << endl;

    client->fetchnodes();
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
        char node[9];
        char key[FILENODEKEYLENGTH * 4 / 3 + 3];

        nodepath(h, &path);

        cout << "Exported " << path << ": ";

        Base64::btoa((byte*) &ph, MegaClient::NODEHANDLE, node);

        // the key
        if (n->type == FILENODE)
        {
            Base64::btoa((const byte*) n->nodekey.data(), FILENODEKEYLENGTH, key);
        }
        else if (n->sharekey)
        {
            Base64::btoa(n->sharekey->key, FOLDERNODEKEYLENGTH, key);
        }
        else
        {
            cout << "No key available for exported folder" << endl;

            del = ets = 0;
            hlink = UNDEF;
            return;
        }

        cout << "https://mega.co.nz/#" << (n->type ? "F" : "") << "!" << node << "!" << key << endl;
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
                                  string* a, string* fa, int)
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
    string keystring;

    attrstring.resize(a->length()*4/3+4);
    attrstring.resize(Base64::btoa((const byte *)a->data(),a->length(), (char *)attrstring.data()));

    SymmCipher nodeKey;
    keystring.assign((char*)key,FILENODEKEYLENGTH);
    nodeKey.setkey(key, FILENODE);

    byte *buf = Node::decryptattr(&nodeKey,attrstring.c_str(),attrstring.size());
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

void DemoApp::checkfile_result(handle h, error e)
{
    cout << "Link check failed: " << errorstring(e) << endl;
}

void DemoApp::checkfile_result(handle h, error e, byte* filekey, m_off_t size, m_time_t ts, m_time_t tm, string* filename,
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

bool DemoApp::pread_data(byte* data, m_off_t len, m_off_t pos, m_off_t, m_off_t, void* appdata)
{
    cout << "Received " << len << " partial read byte(s) at position " << pos << ": ";
    fwrite(data, 1, len, stdout);
    cout << endl;

    return true;
}

dstime DemoApp::pread_failure(error e, int retry, void* appdata)
{
    if (retry < 5)
    {
        cout << "Retrying read (" << errorstring(e) << ", attempt #" << retry << ")" << endl;
        return (dstime)(retry*10);
    }
    else
    {
        cout << "Too many failures (" << errorstring(e) << "), giving up" << endl;
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

void DemoApp::enumeratequotaitems_result(handle, unsigned, unsigned, unsigned, unsigned, unsigned, const char*)
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

void DemoApp::checkout_result(error)
{
    // FIXME: implement
}

void DemoApp::checkout_result(const char*)
{
    // FIXME: implement
}

void DemoApp::getmegaachievements_result(AchievementsDetails *details, error e)
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

// display account details/history
void DemoApp::account_details(AccountDetails* ad, bool storage, bool transfer, bool pro, bool purchases,
                              bool transactions, bool sessions)
{
    char timebuf[32], timebuf2[32];

    if (storage)
    {
        cout << "\tAvailable storage: " << ad->storage_max << " byte(s)" << endl;

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
            time_t t = time(NULL) - ad->transfer_hist_starttime;

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

                char id[12];
                Base64::btoa((byte*)&(it->id), MegaClient::SESSIONHANDLE, id);

                if (it->current)
                {
                    printf("\t* Current Session\n");
                }
                printf("\tSession ID: %s\n\tSession start: %s\n\tMost recent activity: %s\n\tIP: %s\n\tCountry: %.2s\n\tUser-Agent: %s\n\t-----\n",
                        id, timebuf, timebuf2, it->ip.c_str(), it->country, it->useragent.c_str());
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
void DemoApp::account_details(AccountDetails* ad, error e)
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
        char id[12];
        Base64::btoa((byte*)&(sessionid), MegaClient::SESSIONHANDLE, id);
        cout << "Session with id " << id << " has been killed" << endl;
    }
}


// user attribute update notification
void DemoApp::userattr_update(User* u, int priv, const char* n)
{
    cout << "Notification: User " << u->email << " -" << (priv ? " private" : "") << " attribute "
          << n << " added or updated" << endl;
}

// main loop
void megacli()
{
    char *saved_line = NULL;
    int saved_point = 0;

    rl_save_prompt();

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
                            += (*it)->progressreported * 10 / (1024 * (Waiter::ds - (*it)->starttime + 1));
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

            rl_callback_handler_install(*dynamicprompt ? dynamicprompt : prompts[COMMAND], store_line);

            // display prompt
            if (saved_line)
            {
                rl_replace_line(saved_line, 0);
                free(saved_line);
            }

            rl_point = saved_point;
            rl_redisplay();
        }

        // command editing loop - exits when a line is submitted or the engine requires the CPU
        for (;;)
        {
            int w = client->wait();

            if (w & Waiter::HAVESTDIN)
            {
                if (prompt == COMMAND)
                {
                    rl_callback_read_char();
                }
                else
                {
                    console->readpwchar(pw_buf, sizeof pw_buf, &pw_buf_pos, &line);
                }
            }

            if (w & Waiter::NEEDEXEC || line)
            {
                break;
            }
        }

        // save line
        saved_point = rl_point;
        saved_line = rl_copy_text(0, rl_end);

        // remove prompt
        rl_save_prompt();
        rl_replace_line("", 0);
        rl_redisplay();

        if (line)
        {
            // execute user command
            process_line(line);
            free(line);
            line = NULL;
        }

        // pass the CPU to the engine (nonblocking)
        client->exec();

        if (clientFolder)
        {
            clientFolder->exec();
        }
    }
}

int main()
{
    SimpleLogger::setAllOutputs(&std::cout);

    // instantiate app components: the callback processor (DemoApp),
    // the HTTP I/O engine (WinHttpIO) and the MegaClient itself
    client = new MegaClient(new DemoApp, new CONSOLE_WAIT_CLASS,
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
                            "SDKSAMPLE",
                            "megacli/" TOSTRING(MEGA_MAJOR_VERSION)
                            "." TOSTRING(MEGA_MINOR_VERSION)
                            "." TOSTRING(MEGA_MICRO_VERSION));

    clientFolder = NULL;    // additional for folder links

    console = new CONSOLE_CLASS;

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

